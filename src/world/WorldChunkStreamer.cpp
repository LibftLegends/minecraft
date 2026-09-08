#include "../../src/world/WorldChunkStreamer.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"
#include <cstdlib>
#include <cstdio>
#if defined(LIBFT_ENABLE_ANALYTICS)
# include <chrono>
#endif

const uint8_t WorldChunkStreamer::CANDIDATE_ABSENT = 0U;
const uint8_t WorldChunkStreamer::CANDIDATE_QUEUED = 1U;
const uint8_t WorldChunkStreamer::CANDIDATE_GENERATING = 2U;
const uint8_t WorldChunkStreamer::CANDIDATE_GENERATED = 3U;
const uint8_t WorldChunkStreamer::CANDIDATE_MESHING = 4U;
const uint8_t WorldChunkStreamer::CANDIDATE_READY = 5U;
const uint8_t WorldChunkStreamer::CANDIDATE_FAILED_RETRYABLE = 6U;

namespace
{
	/* Keep one slot available for an interactive edit while another background
	 * remesh is already solving. The worker queue still limits discovery and
	 * the interactive request is selected first. */
	static const std::size_t WORLD_STREAM_MAX_REMESH_IN_FLIGHT = 2U;

}

WorldChunkStreamer::WorldChunkStreamer(World &world) : world_(world)
{
	voxel_light_update_config_defaults(this->light_update_config_);
	voxel_light_update_config_defaults(this->interactive_light_update_config_);
	/* A remesh snapshot is already immutable and the solve is worker-owned.
	 * The generic Libft defaults are intentionally small, but using them here
	 * would make a 15-block halo require thousands of scheduler round trips on
	 * a single-worker host.  Keep the work bounded while making each worker
	 * slice large enough to finish in a practical time. */
	this->light_update_config_.min_nodes_per_frame = 2048U;
	this->light_update_config_.target_nodes_per_frame = 8192U;
	this->light_update_config_.max_nodes_per_frame = 32768U;
	this->light_update_config_.time_budget_microseconds = 8000U;
	/* Interactive edits must converge in one worker solve whenever possible.
	 * This remains off the render thread; the normal configuration continues
	 * to bound background remesh slices more conservatively. */
	this->interactive_light_update_config_.min_nodes_per_frame = 4096U;
	this->interactive_light_update_config_.target_nodes_per_frame = 65536U;
	this->interactive_light_update_config_.max_nodes_per_frame = 131072U;
	this->interactive_light_update_config_.time_budget_microseconds = 8000U;
}

WorldChunkStreamer::WorldChunkStreamer(const WorldChunkStreamer &other)
	: world_(other.world_)
{
	(void)other;
}

WorldChunkStreamer::~WorldChunkStreamer()
{
	this->reset();
}

WorldChunkStreamer &WorldChunkStreamer::operator=(const WorldChunkStreamer &other)
{
	(void)other;
	return (*this);
}

int32_t WorldChunkStreamer::initialize_pipeline() noexcept
{
	int32_t error_code;

	error_code = this->generation_pipeline_.initialize(0U, 0U);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = this->start_remesh_capture_worker();
	if (error_code != FT_ERR_SUCCESS)
		(void)this->generation_pipeline_.destroy();
	return (error_code);
}

void WorldChunkStreamer::reset() noexcept
{
	this->world_epoch_ += 1U;
	this->remesh_capture_relevance_epoch_.store(this->stream_relevance_epoch_);
	this->stop_remesh_capture_worker();
	(void)this->generation_pipeline_.destroy();
	this->stream_candidates_radius_ = -1;
	this->stream_candidate_cursor_ = 0U;
	this->stream_candidate_lookup_.clear();
	this->dirty_remesh_cursor_ = 0;
	this->remesh_queue_peak_ = 0U;
	this->stale_result_count_ = 0U;
	this->stale_stream_result_count_ = 0U;
	this->stale_remesh_result_count_ = 0U;
	this->remesh_snapshot_bytes_.store(0U);
	this->remesh_scanned_cells_ = 0U;
	this->remesh_propagated_cells_ = 0U;
	this->remesh_light_queue_peak_ = 0U;
	this->remesh_completed_count_ = 0U;
	this->next_remesh_submission_frame_ = 0U;
	this->priority_remesh_pending_ = false;
	this->remesh_priority_anchor_valid_ = false;
	this->remesh_priority_anchor_x_ = 0;
	this->remesh_priority_anchor_z_ = 0;
	this->remesh_priority_anchor_expiry_frame_ = 0U;
	this->generation_credit_ = 0;
	this->stream_last_error_ = FT_ERR_SUCCESS;
	this->stream_retryable_count_ = 0;
	this->deferred_edits_.clear();
	this->deferred_edits_sorted_ = false;
	this->deferred_apply_cursor_ = 0U;
	this->deferred_sorted_end_ = 0U;
	this->deferred_pending_edits_.clear();
	this->deferred_touched_chunks_.clear();
	this->priority_remeshes_.clear();
}

int32_t WorldChunkStreamer::start_remesh_capture_worker() noexcept
{
	if (this->remesh_capture_thread_.joinable())
		return (FT_ERR_SUCCESS);
	{
		std::lock_guard<std::mutex> lock(this->remesh_capture_mutex_);
		this->remesh_capture_stopping_ = false;
	}
	try
	{
		this->remesh_capture_thread_ = std::thread(
			&WorldChunkStreamer::run_remesh_capture_worker, this);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

void WorldChunkStreamer::stop_remesh_capture_worker() noexcept
{
	std::deque<RemeshCaptureTask> cancelled_tasks;

	{
		std::lock_guard<std::mutex> lock(this->remesh_capture_mutex_);
		this->remesh_capture_stopping_ = true;
		cancelled_tasks.swap(this->remesh_capture_tasks_);
	}
	this->remesh_capture_condition_.notify_all();
	if (this->remesh_capture_thread_.joinable())
		this->remesh_capture_thread_.join();
	while (!cancelled_tasks.empty())
	{
		this->world_.clear_pending_remesh(cancelled_tasks.front().chunk_x,
			cancelled_tasks.front().chunk_z, cancelled_tasks.front().request_id);
		cancelled_tasks.pop_front();
	}
	this->remesh_capture_in_flight_.store(0U);
}

void WorldChunkStreamer::run_remesh_capture_worker() noexcept
{
	while (true)
	{
		RemeshCaptureTask task;
		WorldGenerationPipeline::WorldChunkSnapshot snapshot;
		int32_t error_code;
		bool should_stop;
		bool stale_capture;

		{
			std::unique_lock<std::mutex> lock(this->remesh_capture_mutex_);
			this->remesh_capture_condition_.wait(lock, [this]()
			{
				return (this->remesh_capture_stopping_
					|| !this->remesh_capture_tasks_.empty());
			});
			should_stop = this->remesh_capture_stopping_
				&& this->remesh_capture_tasks_.empty();
			if (should_stop)
				return ;
			task = this->remesh_capture_tasks_.front();
			this->remesh_capture_tasks_.pop_front();
		}
		stale_capture = task.relevance_epoch
			!= this->remesh_capture_relevance_epoch_.load();
		error_code = stale_capture ? FT_ERR_INVALID_STATE
			: this->world_.capture_remesh_snapshot(task.chunk_x,
				task.chunk_z, snapshot);
		if (error_code == FT_ERR_SUCCESS
			&& task.relevance_epoch
			!= this->remesh_capture_relevance_epoch_.load())
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
		{
			uint64_t snapshot_element_count;

			snapshot_element_count = snapshot.blocks.size()
				+ snapshot.lighting_blocks.size()
				+ snapshot.west_border.size()
				+ snapshot.east_border.size()
				+ snapshot.north_border.size()
				+ snapshot.south_border.size();
			this->remesh_snapshot_bytes_.fetch_add(
				(snapshot_element_count)
				* sizeof(uint32_t));
			error_code = this->generation_pipeline_.submit_remesh(
				task.request_id, task.world_epoch, task.relevance_epoch,
				task.generation_revision, task.chunk_x, task.chunk_z,
				task.voxel_revision, task.light_revision, std::move(snapshot),
				&task.light_update_config, task.interactive);
		}
		if (error_code != FT_ERR_SUCCESS)
			this->world_.clear_pending_remesh(task.chunk_x, task.chunk_z,
				task.request_id);
		this->remesh_capture_in_flight_.fetch_sub(1U);
	}
}

int32_t WorldChunkStreamer::seed_initial_stream(int32_t stream_radius,
	int32_t budget, int32_t *generated) noexcept
{
	int32_t error_code;
	int32_t initial_async_budget;

	this->stream_frame_ += 1U;
	WorldChunkCandidateScanner::prepare_stream_candidates(*this, stream_radius);
	error_code = WorldChunkCandidateScanner::stream_chunks_sync(*this,
		stream_radius, budget, generated);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	/* Queue the initial playable ring immediately. The center chunk and one
	 * nearest neighbor are prepared synchronously; the remaining adjacent
	 * chunks are handed to the persistent workers in one burst so loading does
	 * not wait for several foreground frames before work begins. */
	initial_async_budget = 4;
	return (WorldChunkAsyncSubmitter::stream_chunks_async(*this, stream_radius,
			initial_async_budget, generated));
}

void WorldChunkStreamer::handle_recenter() noexcept
{
	std::deque<RemeshCaptureTask> cancelled_capture_tasks;
	std::size_t cancelled_capture_count;
	int32_t index;

	this->generation_pipeline_.cancel_queued();
	{
		std::lock_guard<std::mutex> lock(this->remesh_capture_mutex_);
		cancelled_capture_tasks.swap(this->remesh_capture_tasks_);
	}
	cancelled_capture_count = cancelled_capture_tasks.size();
	while (!cancelled_capture_tasks.empty())
	{
		WorldChunk *cancelled_chunk = this->world_.find_chunk_mutable(
			cancelled_capture_tasks.front().chunk_x,
			cancelled_capture_tasks.front().chunk_z);
		if (cancelled_chunk != nullptr
			&& cancelled_chunk->pending_mesh_request_id
				== cancelled_capture_tasks.front().request_id)
		{
			cancelled_chunk->pending_mesh_request_id = 0U;
			cancelled_chunk->mesh_dirty = true;
		}
		cancelled_capture_tasks.pop_front();
	}
	if (cancelled_capture_count != 0U)
		this->remesh_capture_in_flight_.fetch_sub(cancelled_capture_count);
	/* Recentring cancels queued generation work, including remesh requests.
	 * Clear the chunk-side ownership markers for those requests as well; if a
	 * marker survives cancellation, the scheduler treats the chunk as already
	 * in flight forever and no replacement mesh can be submitted. */
	index = 0;
	while (index < this->world_.chunk_count)
	{
		WorldChunk &chunk = this->world_.chunks[index];
		if (chunk.initialized && chunk.pending_mesh_request_id != 0U)
		{
			chunk.pending_mesh_request_id = 0U;
			chunk.mesh_dirty = true;
			chunk.light_revision += 1U;
			this->prioritize_chunk_remesh(chunk.chunk_x, chunk.chunk_z);
		}
		index += 1;
	}
	this->stream_relevance_epoch_ += 1U;
	this->remesh_capture_relevance_epoch_.store(
		this->stream_relevance_epoch_);
	this->remesh_priority_anchor_valid_ = true;
	this->remesh_priority_anchor_x_ = this->world_.center_chunk_x;
	this->remesh_priority_anchor_z_ = this->world_.center_chunk_z;
	this->remesh_priority_anchor_expiry_frame_ = this->stream_frame_ + 32U;
	this->stream_candidate_cursor_ = 0U;
	for (StreamCandidate &candidate : this->stream_candidates_)
	{
		candidate.state = WorldChunkStreamer::CANDIDATE_ABSENT;
		candidate.retry_count = 0U;
		candidate.retry_frames = 0;
		candidate.last_error = FT_ERR_SUCCESS;
		candidate.relevance_epoch = this->stream_relevance_epoch_;
		candidate.generation_revision = this->generation_revision_;
		candidate.queued_frame = 0U;
		candidate.request_id = 0U;
	}
	this->stream_retryable_count_ = 0;
}

int32_t WorldChunkStreamer::stream_full_sync(int32_t stream_radius,
	int32_t generation_budget, int32_t *generated) noexcept
{
	this->generation_pipeline_.cancel_queued();
	this->invalidate_non_ready_candidates();
	return (WorldChunkCandidateScanner::stream_chunks_sync(*this, stream_radius,
			generation_budget, generated));
}

int32_t WorldChunkStreamer::dispatch_incremental_stream(int32_t stream_radius,
	int32_t generation_budget, int32_t *generated) noexcept
{
	if (generation_budget <= 0)
	{
		if (this->generation_credit_ < 4)
			this->generation_credit_ = this->generation_credit_ + 1;
		if (this->generation_credit_ < 4)
			return (WorldChunkAsyncSubmitter::stream_chunks_async(*this,
					stream_radius, 0, generated));
		this->generation_credit_ = 0;
		generation_budget = 1;
	}
	else
		this->generation_credit_ = 0;
	return (WorldChunkAsyncSubmitter::stream_chunks_async(*this, stream_radius,
			generation_budget, generated));
}

int32_t WorldChunkStreamer::update(int32_t generation_budget,
	int32_t stream_radius, bool center_changed) noexcept
{
	int32_t drain_error;
	int32_t generated;
	int32_t analytics_error;

	this->stream_frame_++;
	if (center_changed)
	{
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		std::fprintf(stderr,
			"[WorldGen] recenter center=(%d,%d) previous=(%d,%d)\n",
			this->world_.center_chunk_x, this->world_.center_chunk_z,
			this->world_.chunk_index_center_x,
			this->world_.chunk_index_center_z);
	#endif
		this->handle_recenter();
	}
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::WORLD_STREAM_DRAIN);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: stream drain scope start failed (%d)\n",
			analytics_error);
	drain_error = WorldGenerationResultCommitter::drain(*this, this->world_);
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: stream drain scope end failed (%d)\n",
			analytics_error);
	if (drain_error != FT_ERR_SUCCESS)
		return (drain_error);
	/* Startup deliberately seeds only the minimum playable ring.  Rebuild the
	 * candidate set after draining old results when the active render distance
	 * grows, otherwise the persistent async submitter would keep scanning the
	 * startup-sized list forever. */
	if (this->stream_candidates_radius_ != stream_radius)
	{
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		std::fprintf(stderr,
			"[WorldGen] stream radius changed old=%d new=%d\n",
			this->stream_candidates_radius_, stream_radius);
	#endif
		WorldChunkCandidateScanner::prepare_stream_candidates(*this,
			stream_radius);
	}
	generated = 0;
	if (generation_budget >= WorldCoordinates::CHUNK_COUNT)
		return (this->stream_full_sync(stream_radius, generation_budget,
				&generated));
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::WORLD_STREAM_DISPATCH);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: stream dispatch scope start failed (%d)\n",
			analytics_error);
	{
		int32_t result = this->dispatch_incremental_stream(stream_radius,
			generation_budget, &generated);
		analytics_error = RuntimeAnalytics::end_scope();
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr, "Analytics: stream dispatch scope end failed (%d)\n",
				analytics_error);
		return (result);
	}
}

int32_t WorldChunkStreamer::stream_last_error() const noexcept
{
	return (this->stream_last_error_);
}

int32_t WorldChunkStreamer::stream_retryable_count() const noexcept
{
	return (this->stream_retryable_count_);
}

WorldChunkStreamer::Diagnostics WorldChunkStreamer::diagnostics() const noexcept
{
	return (WorldChunkStreamDiagnosticsBuilder::build(*this));
}

uint64_t WorldChunkStreamer::allocate_request_id() noexcept
{
	return (this->next_request_id_++);
}

uint64_t WorldChunkStreamer::world_epoch() const noexcept
{
	return (this->world_epoch_);
}

uint32_t WorldChunkStreamer::generation_revision() const noexcept
{
	return (this->generation_revision_);
}

int32_t WorldChunkStreamer::set_light_update_config(
	const voxel_light_update_config &config) noexcept
{
	if (voxel_light_update_config_is_valid(config) == FT_FALSE)
		return (FT_ERR_INVALID_ARGUMENT);
	this->light_update_config_ = config;
	return (FT_ERR_SUCCESS);
}

const voxel_light_update_config &WorldChunkStreamer::light_update_config()
	const noexcept
{
	return (this->light_update_config_);
}

int32_t WorldChunkStreamer::set_interactive_light_update_config(
	const voxel_light_update_config &config) noexcept
{
	if (voxel_light_update_config_is_valid(config) == FT_FALSE)
		return (FT_ERR_INVALID_ARGUMENT);
	this->interactive_light_update_config_ = config;
	return (FT_ERR_SUCCESS);
}

const voxel_light_update_config &
	WorldChunkStreamer::interactive_light_update_config() const noexcept
{
	return (this->interactive_light_update_config_);
}

void WorldChunkStreamer::bump_generation_revision() noexcept
{
	this->generation_revision_ += 1U;
}

WorldGenerationPipeline &WorldChunkStreamer::pipeline() noexcept
{
	return (this->generation_pipeline_);
}

const WorldGenerationPipeline &WorldChunkStreamer::pipeline() const noexcept
{
	return (this->generation_pipeline_);
}

void WorldChunkStreamer::invalidate_non_ready_candidates() noexcept
{
	for (StreamCandidate &candidate : this->stream_candidates_)
	{
		if (candidate.state != WorldChunkStreamer::CANDIDATE_READY)
		{
			candidate.state = WorldChunkStreamer::CANDIDATE_ABSENT;
			candidate.request_id = 0U;
		}
	}
}

void WorldChunkStreamer::reset_candidates_after_regeneration() noexcept
{
	for (StreamCandidate &candidate : this->stream_candidates_)
	{
		if (candidate.state != WorldChunkStreamer::CANDIDATE_READY)
			candidate.state = WorldChunkStreamer::CANDIDATE_ABSENT;
		candidate.request_id = 0U;
		candidate.generation_revision = this->generation_revision_;
	}
}

int32_t WorldChunkStreamer::queue_chunk_remesh(WorldChunk &chunk) noexcept
{
	uint64_t request_id;
	const voxel_light_update_config *resolved_light_config;

	if (!chunk.initialized || !chunk.mesh_dirty
		|| chunk.pending_mesh_request_id != 0U)
		return (FT_ERR_SUCCESS);
	resolved_light_config = &this->light_update_config_;
	if (this->priority_remesh_pending_
		&& !this->priority_remeshes_.empty()
		&& this->priority_remeshes_.front().interactive
		&& this->priority_remeshes_.front().chunk_x == chunk.chunk_x
		&& this->priority_remeshes_.front().chunk_z == chunk.chunk_z)
	{
		resolved_light_config = &this->interactive_light_update_config_;
	}
	if (this->generation_pipeline_.remesh_in_flight_count()
		+ this->remesh_capture_in_flight_.load()
		>= WORLD_STREAM_MAX_REMESH_IN_FLIGHT)
		return (FT_ERR_FULL);
	request_id = this->next_request_id_++;
	RemeshCaptureTask task;
	task.request_id = request_id;
	task.world_epoch = this->world_epoch_;
	task.relevance_epoch = this->stream_relevance_epoch_;
	task.generation_revision = this->generation_revision_;
	task.chunk_x = chunk.chunk_x;
	task.chunk_z = chunk.chunk_z;
	task.voxel_revision = chunk.voxel_revision;
	task.light_revision = chunk.light_revision;
	task.interactive = FT_FALSE;
	if (this->priority_remesh_pending_ && !this->priority_remeshes_.empty()
		&& this->priority_remeshes_.front().interactive
		&& this->priority_remeshes_.front().chunk_x == chunk.chunk_x
		&& this->priority_remeshes_.front().chunk_z == chunk.chunk_z)
		task.interactive = FT_TRUE;
	task.light_update_config = *resolved_light_config;
	try
	{
		std::lock_guard<std::mutex> lock(this->remesh_capture_mutex_);
		if (this->remesh_capture_stopping_)
			return (FT_ERR_INVALID_STATE);
		chunk.pending_mesh_request_id = request_id;
		this->remesh_capture_tasks_.push_back(task);
		this->remesh_capture_in_flight_.fetch_add(1U);
		{
			const std::size_t pending_remeshes =
				this->priority_remeshes_.size()
				+ this->remesh_capture_in_flight_.load();
			if (pending_remeshes > this->remesh_queue_peak_)
				this->remesh_queue_peak_ = pending_remeshes;
		}
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	this->remesh_capture_condition_.notify_one();
	return (FT_ERR_SUCCESS);
}

void WorldChunkStreamer::mark_remesh_dirty(WorldChunk &chunk) noexcept
{
	/* A clean chunk needs a new lighting revision.  An already dirty chunk
	 * has not yet published a newer mesh, so repeated notifications can be
	 * coalesced.  An in-flight request is different: advance the revision so
	 * its result is rejected and a later request captures the newest state. */
	if (!chunk.mesh_dirty || chunk.pending_mesh_request_id != 0U)
		chunk.light_revision += 1U;
	chunk.mesh_dirty = true;
	return ;
}

void WorldChunkStreamer::mark_neighbor_remeshes(int32_t chunk_x,
	int32_t chunk_z) noexcept
{
	const int32_t coordinates[9][2] = {{chunk_x - 1, chunk_z - 1},
		{chunk_x, chunk_z - 1}, {chunk_x + 1, chunk_z - 1}, {chunk_x - 1,
		chunk_z}, {chunk_x, chunk_z}, {chunk_x + 1, chunk_z}, {chunk_x - 1,
		chunk_z + 1}, {chunk_x, chunk_z + 1}, {chunk_x + 1, chunk_z + 1}};
	int32_t index;
	WorldChunk *chunk;

	index = 0;
	while (index < 9)
	{
		chunk = this->world_.find_chunk_mutable(coordinates[index][0],
			coordinates[index][1]);
		if (chunk != nullptr)
			this->mark_remesh_dirty(*chunk);
		index += 1;
	}
	return ;
}

void WorldChunkStreamer::prioritize_chunk_remesh(int32_t chunk_x,
	int32_t chunk_z) noexcept
{
	this->prioritize_chunk_remesh_from_origin(chunk_x, chunk_z, chunk_x,
		chunk_z);
	return ;
}

void WorldChunkStreamer::prioritize_chunk_remesh_from_origin(int32_t chunk_x,
	int32_t chunk_z, int32_t origin_chunk_x, int32_t origin_chunk_z) noexcept
{
	bool existing;
	RemeshPriority promoted;
	std::deque<RemeshPriority>::iterator insert_position;

	existing = false;
	for (auto iterator = this->priority_remeshes_.begin();
		iterator != this->priority_remeshes_.end(); ++iterator)
	{
		if (iterator->chunk_x == chunk_x && iterator->chunk_z == chunk_z)
		{
			promoted = *iterator;
			this->priority_remeshes_.erase(iterator);
			existing = true;
			break ;
		}
	}
	if (!existing)
	{
		promoted.chunk_x = chunk_x;
		promoted.chunk_z = chunk_z;
	}
	/* An explicit edit re-anchors an existing background notification.  The
	 * queue then remains ordered by propagation distance, with stable FIFO
	 * ordering for equal-distance cells. */
	promoted.origin_chunk_x = origin_chunk_x;
	promoted.origin_chunk_z = origin_chunk_z;
	promoted.origin_distance = static_cast<uint32_t>(
		std::abs(chunk_x - origin_chunk_x)
		+ std::abs(chunk_z - origin_chunk_z));
	promoted.queued_frame = this->stream_frame_;
	promoted.interactive = true;
	insert_position = this->priority_remeshes_.begin();
	while (insert_position != this->priority_remeshes_.end()
		&& insert_position->interactive
		&& insert_position->origin_distance <= promoted.origin_distance)
		++insert_position;
	this->priority_remeshes_.insert(insert_position, promoted);
	if (this->priority_remeshes_.size() > this->remesh_queue_peak_)
		this->remesh_queue_peak_ = this->priority_remeshes_.size();
	this->priority_remesh_pending_ = !this->priority_remeshes_.empty();
	if (this->priority_remesh_pending_)
	{
		this->priority_remesh_chunk_x_ = this->priority_remeshes_.front().chunk_x;
		this->priority_remesh_chunk_z_ = this->priority_remeshes_.front().chunk_z;
	}
	this->remesh_priority_anchor_valid_ = true;
	this->remesh_priority_anchor_x_ = chunk_x;
	this->remesh_priority_anchor_z_ = chunk_z;
	this->remesh_priority_anchor_expiry_frame_ = this->stream_frame_ + 32U;
	/* An edit is already visible in authoritative storage.  Allow the next
	 * update pass to submit its replacement mesh immediately instead of making
	 * it wait for the normal cadence used by background generation. */
	if (this->next_remesh_submission_frame_ > this->stream_frame_)
		this->next_remesh_submission_frame_ = this->stream_frame_;
	return ;
}

void WorldChunkStreamer::prioritize_edit_border_remeshes(int32_t chunk_x,
	int32_t chunk_z) noexcept
{
	const int32_t coordinates[4][2] = {{chunk_x - 1, chunk_z},
		{chunk_x + 1, chunk_z}, {chunk_x, chunk_z - 1},
		{chunk_x, chunk_z + 1}};
	int32_t index;

	/* Keep the four face-sharing chunks in the same bounded interactive queue
	 * as the edited chunk.  Diagonal lighting work remains background work; it
	 * will be coalesced by the normal dirty-remesh scheduler.  Queue neighbors
	 * first so the edited chunk is left at the front of the deque. */
	index = 0;
	while (index < 4)
	{
		if (this->world_.find_chunk(coordinates[index][0],
				coordinates[index][1]) != nullptr)
			this->prioritize_chunk_remesh_from_origin(coordinates[index][0],
				coordinates[index][1], chunk_x, chunk_z);
		index += 1;
	}
	this->prioritize_chunk_remesh_from_origin(chunk_x, chunk_z, chunk_x,
		chunk_z);
	return ;
}

void WorldChunkStreamer::enqueue_background_remesh(int32_t chunk_x,
	int32_t chunk_z) noexcept
{
	bool already_queued;

	already_queued = false;
	for (const RemeshPriority &priority : this->priority_remeshes_)
	{
		if (priority.chunk_x == chunk_x && priority.chunk_z == chunk_z)
		{
			already_queued = true;
			break ;
		}
	}
	if (!already_queued)
	{
		this->priority_remeshes_.push_back({chunk_x, chunk_z, chunk_x, chunk_z,
			0U, this->stream_frame_, false});
	}
	if (this->priority_remeshes_.size() > this->remesh_queue_peak_)
		this->remesh_queue_peak_ = this->priority_remeshes_.size();
	this->priority_remesh_pending_ = !this->priority_remeshes_.empty();
	if (this->priority_remesh_pending_)
	{
		this->priority_remesh_chunk_x_ =
			this->priority_remeshes_.front().chunk_x;
		this->priority_remesh_chunk_z_ =
			this->priority_remeshes_.front().chunk_z;
	}
	return ;
}

int32_t WorldChunkStreamer::queue_neighbor_remeshes(int32_t chunk_x,
	int32_t chunk_z) noexcept
{
	const int32_t coordinates[5][2] = {{chunk_x, chunk_z}, {chunk_x - 1,
		chunk_z}, {chunk_x + 1, chunk_z}, {chunk_x, chunk_z - 1}, {chunk_x,
		chunk_z + 1}};
	int32_t index;
	WorldChunk *chunk;

	index = 0;
	while (index < 5)
	{
		chunk = this->world_.find_chunk_mutable(coordinates[index][0],
				coordinates[index][1]);
		if (chunk != nullptr)
			this->mark_remesh_dirty(*chunk);
		index += 1;
	}
	return (FT_ERR_SUCCESS);
}
