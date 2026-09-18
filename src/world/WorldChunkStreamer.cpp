#include "../../src/world/WorldChunkStreamer.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include "../../src/world/WorldChunkSnapshotCapture.hpp"
#if defined(LIBFT_ENABLE_ANALYTICS)
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
	static bool neighbor_edge_requires_remesh(const WorldChunk *chunk,
		int32_t local_x, int32_t local_y, int32_t local_z) noexcept
	{
		uint32_t block_id;

		if (chunk == nullptr || !chunk->initialized)
			return (false);
		if (chunk->chunk.read_block(local_x, local_y, local_z,
				&block_id) != FT_ERR_SUCCESS)
			return (true);
		return (voxel_block_is_transparent(block_id) != FT_FALSE);
	}

	static bool has_published_light_baseline(const WorldChunk &chunk) noexcept
	{
		return (chunk.initialized
			&& chunk.light_ready_for_render
			&& chunk.light_buffer_is_valid());
	}

	/* A newly arrived neighbour only invalidates a published light field when
	 * its shared face can actually add light to a transparent boundary cell.
	 * The summaries are refreshed whenever a chunk publishes a read state, so
	 * this avoids rescanning the face while the world write lock is held.  A
	 * maximum above one is sufficient because light attenuation is at least
	 * one.  The result is conservative: it can schedule extra work, but cannot
	 * miss a possible incoming light contribution. */
	static bool source_arrival_changes_target_face(const WorldChunk &source,
		const WorldChunk &target, uint8_t face) noexcept
	{
		uint8_t source_summary_face;
		uint8_t target_summary_face;

		if (face >= 4U)
			return (false);
		/* The notification face describes the direction from source to target,
		 * while the cached summaries describe the physical face on each chunk.
		 * Those indices are opposite for every cardinal direction. */
		source_summary_face = 0U;
		target_summary_face = 0U;
		if (face == 0U)
		{
			source_summary_face = 1U;
			target_summary_face = 1U;
		}
		else if (face == 1U)
		{
			source_summary_face = 0U;
			target_summary_face = 0U;
		}
		else if (face == 2U)
		{
			source_summary_face = 3U;
			target_summary_face = 3U;
		}
		else
		{
			source_summary_face = 2U;
			target_summary_face = 2U;
		}
		return (source.get_boundary_source_light_max(source_summary_face) > 1U
			&& target.boundary_has_transparent_target(target_summary_face));
	}

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
	/* Interactive edits must remain responsive, but a giant one-shot solve can
	 * starve the renderer and makes the old mesh appear to flicker.  Keep each
	 * worker slice bounded just like background work; the edit remains
	 * authoritative immediately and the mesh/light result converges in slices. */
	this->interactive_light_update_config_.min_nodes_per_frame = 512U;
	this->interactive_light_update_config_.target_nodes_per_frame = 2048U;
	this->interactive_light_update_config_.max_nodes_per_frame = 8192U;
	this->interactive_light_update_config_.time_budget_microseconds = 2000U;
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
	{
		std::lock_guard<std::mutex> lock(this->remesh_capture_mutex_);
		this->remesh_capture_failures_.clear();
	}
	(void)this->generation_pipeline_.destroy();
	this->stream_candidates_radius_ = -1;
	this->stream_candidate_cursor_ = 0U;
	this->stream_candidate_lookup_.clear();
	this->dirty_remesh_cursor_ = 0;
	this->remesh_queue_peak_ = 0U;
	this->remesh_starvation_promotions_ = 0U;
	this->stale_result_count_ = 0U;
	this->stale_stream_result_count_ = 0U;
	this->stale_remesh_result_count_ = 0U;
	this->stale_remesh_capture_count_ = 0U;
	this->stale_remesh_dependency_count_ = 0U;
	this->stale_remesh_pending_count_ = 0U;
	this->stale_remesh_revision_count_ = 0U;
	this->staged_border_source_result_.reset();
	this->staged_border_source_frame_ = 0U;
	this->remesh_snapshot_bytes_.store(0U);
	this->remesh_capture_duration_nanoseconds_.store(0U);
	this->remesh_capture_count_.store(0U);
	this->remesh_scanned_cells_ = 0U;
	this->remesh_propagated_cells_ = 0U;
	this->remesh_light_queue_peak_ = 0U;
	this->remesh_completed_count_ = 0U;
	this->remesh_incremental_completed_count_ = 0U;
	this->remesh_full_completed_count_ = 0U;
	this->remesh_geometry_only_count_ = 0U;
	this->remesh_canceled_count_ = 0U;
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
	{
		std::lock_guard<std::mutex> lock(this->remesh_capture_mutex_);
		this->remesh_capture_processing_request_id_ = 0U;
	}
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
		bool task_cancelled;
		std::chrono::steady_clock::time_point capture_start;

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
			task = std::move(this->remesh_capture_tasks_.front());
			this->remesh_capture_tasks_.pop_front();
			this->remesh_capture_processing_request_id_ = task.request_id;
		}
		task_cancelled = task.cancellation_token == nullptr
			|| task.cancellation_token->load(std::memory_order_acquire)
			!= task.cancellation_token_value;
		stale_capture = task_cancelled || task.relevance_epoch
			!= this->remesh_capture_relevance_epoch_.load();
		if (stale_capture)
			this->stale_remesh_capture_count_ += 1U;
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		if (stale_capture
			&& this->stale_remesh_capture_count_ % 64U == 0U)
			std::fprintf(stderr,
				"[WorldGen] stale remesh capture request=%llu chunk=(%d,%d) "
				"task_epoch=%llu current_epoch=%llu\n",
				static_cast<unsigned long long>(task.request_id), task.chunk_x,
				task.chunk_z,
				task.relevance_epoch,
				this->remesh_capture_relevance_epoch_.load());
	#endif
		if (!stale_capture)
			capture_start = std::chrono::steady_clock::now();
		error_code = stale_capture ? FT_ERR_INVALID_STATE
			: WorldChunkSnapshotCapture::capture_read_states(
				task.read_states[0], task.read_states[1], task.read_states[2],
				task.read_states[3], task.read_states[4], task.read_states[5],
				task.read_states[6], task.read_states[7], task.read_states[8],
				snapshot);
		if (!stale_capture)
		{
			const uint64_t capture_duration = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - capture_start).count());
		#if defined(LIBFT_ENABLE_ANALYTICS)
			this->remesh_capture_duration_nanoseconds_.fetch_add(
				capture_duration);
			this->remesh_capture_count_.fetch_add(1U);
		#endif
		#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
			if (capture_duration >= 250000000ULL)
				std::fprintf(stderr,
					"[WorldGen] slow remesh capture request=%llu chunk=(%d,%d) "
					"duration_ms=%llu\n",
					static_cast<unsigned long long>(task.request_id), task.chunk_x,
					task.chunk_z,
					capture_duration / 1000000ULL);
		#else
			(void)capture_duration;
		#endif
		}
		if (error_code == FT_ERR_SUCCESS
			&& (task.cancellation_token == nullptr
				|| task.cancellation_token->load(std::memory_order_acquire)
					!= task.cancellation_token_value
				|| task.relevance_epoch
					!= this->remesh_capture_relevance_epoch_.load()))
		{
			error_code = FT_ERR_INVALID_STATE;
			if (!stale_capture)
			{
				stale_capture = true;
				this->stale_remesh_capture_count_ += 1U;
			}
		}
		if (error_code == FT_ERR_SUCCESS)
		{
			snapshot.dependency_valid = task.dependency_valid;
			snapshot.dependency_chunk_x = task.dependency_chunk_x;
			snapshot.dependency_chunk_z = task.dependency_chunk_z;
			if (task.dependency_valid != FT_FALSE)
			{
				for (const std::shared_ptr<const WorldChunkReadState> &state
					: task.read_states)
				{
					if (state != nullptr
						&& state->chunk_x == task.dependency_chunk_x
						&& state->chunk_z == task.dependency_chunk_z)
					{
						snapshot.dependency_voxel_revision =
							state->voxel_revision;
						snapshot.dependency_light_revision =
							state->light_revision;
						snapshot.dependency_content_version =
							state->content_version;
						snapshot.dependency_light_input_version =
							state->light_input_version;
						break ;
					}
				}
			}
		uint64_t snapshot_bytes;

			snapshot_bytes = (snapshot.blocks.size()
				+ snapshot.lighting_blocks.size()
				+ snapshot.lighting_ring_offsets.size()
				+ snapshot.west_border.size()
				+ snapshot.east_border.size()
				+ snapshot.north_border.size()
				+ snapshot.south_border.size()) * sizeof(uint32_t);
			snapshot_bytes += snapshot.lighting_existing_light.size()
				+ snapshot.existing_light.size();
			this->remesh_snapshot_bytes_.fetch_add(snapshot_bytes);
			error_code = this->generation_pipeline_.submit_remesh(
				task.request_id, task.world_epoch, task.relevance_epoch,
				task.generation_revision, task.chunk_x, task.chunk_z,
				task.voxel_revision, task.light_revision, task.content_version,
			task.light_input_version, std::move(snapshot),
				&task.light_update_config, task.interactive,
				task.geometry_only,
				task.incremental_additive_light,
				task.incremental_removal_light, task.incremental_local_x,
				task.incremental_local_y, task.incremental_local_z,
				task.incremental_old_block_id, task.incremental_new_block_id,
				&task.incremental_light_seeds,
				task.cancellation_token, task.cancellation_token_value);
		}
		if (error_code != FT_ERR_SUCCESS)
		{
		#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
			if (error_code != FT_ERR_FULL
				&& error_code != FT_ERR_INVALID_STATE)
				std::fprintf(stderr,
					"[WorldGen] remesh capture failed request=%llu chunk=(%d,%d) "
					"error=%d\n",
					static_cast<unsigned long long>(task.request_id), task.chunk_x,
					task.chunk_z, error_code);
		#endif
			try
			{
				std::lock_guard<std::mutex> failure_lock(
					this->remesh_capture_mutex_);
				this->remesh_capture_failures_.push_back({task.chunk_x,
					task.chunk_z, task.request_id, error_code});
			}
			catch (...)
			{
				/* The main-thread cleanup queue is bounded by the capture queue;
				 * if reporting itself cannot allocate, the pending request will be
				 * invalidated by the normal cancellation/relevance path. */
			}
		}
		{
			std::lock_guard<std::mutex> lock(this->remesh_capture_mutex_);
			if (this->remesh_capture_processing_request_id_ == task.request_id)
				this->remesh_capture_processing_request_id_ = 0U;
		}
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

	/* Invalidate capture tasks before touching their queues.  A capture worker
	 * may already be copying immutable read states when recenter begins; the
	 * post-capture relevance check must observe the new epoch and reject that
	 * task before it can submit an old request after pending markers were
	 * cleared below. */
	this->stream_relevance_epoch_ += 1U;
	this->remesh_capture_relevance_epoch_.store(
		this->stream_relevance_epoch_);
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
			cancelled_chunk->clear_pending_remesh_request();
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
			chunk.clear_pending_remesh_request();
			chunk.mesh_dirty = true;
			chunk.light_revision += 1U;
			/* Let the bounded dirty scheduler resubmit this replacement.  An empty
			 * priority record here can overwrite the immutable seed metadata of a
			 * newer gameplay edit. */
		}
		index += 1;
	}
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

void WorldChunkStreamer::cancel_pending_remeshes() noexcept
{
	std::deque<RemeshCaptureTask> cancelled_capture_tasks;
	std::size_t cancelled_capture_count;

	this->stream_relevance_epoch_ += 1U;
	this->remesh_capture_relevance_epoch_.store(
		this->stream_relevance_epoch_);
	this->generation_pipeline_.cancel_queued();
	{
		std::lock_guard<std::mutex> lock(this->remesh_capture_mutex_);
		cancelled_capture_tasks.swap(this->remesh_capture_tasks_);
	}
	cancelled_capture_count = cancelled_capture_tasks.size();
	while (!cancelled_capture_tasks.empty())
	{
		WorldChunk *chunk = this->world_.find_chunk_mutable(
			cancelled_capture_tasks.front().chunk_x,
			cancelled_capture_tasks.front().chunk_z);
		if (chunk != nullptr
			&& chunk->pending_mesh_request_id
				== cancelled_capture_tasks.front().request_id)
		{
			chunk->clear_pending_remesh_request();
			chunk->mesh_dirty = true;
		}
		cancelled_capture_tasks.pop_front();
	}
	if (cancelled_capture_count != 0U)
		this->remesh_capture_in_flight_.fetch_sub(cancelled_capture_count);
	for (int32_t index = 0; index < this->world_.chunk_count; ++index)
	{
		WorldChunk &chunk = this->world_.chunks[index];
		if (chunk.initialized && chunk.pending_mesh_request_id != 0U)
		{
			chunk.cancel_remesh_work();
			chunk.clear_pending_remesh_request();
			chunk.mesh_dirty = true;
		}
	}
}

int32_t WorldChunkStreamer::stream_full_sync(int32_t stream_radius,
	int32_t generation_budget, int32_t *generated) noexcept
{
	this->cancel_pending_remeshes();
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
	this->drain_remesh_capture_failures();
	this->recover_orphaned_remesh_markers();
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

int32_t WorldChunkStreamer::queue_chunk_remesh(WorldChunk &chunk,
	ft_bool incremental_additive_light, ft_bool incremental_removal_light,
	int32_t incremental_local_x, int32_t incremental_local_y,
	int32_t incremental_local_z, uint32_t incremental_old_block_id,
	uint32_t incremental_new_block_id,
	const std::vector<WorldGenerationPipeline::IncrementalLightSeed>
		*incremental_light_seeds, bool geometry_only) noexcept
{
	uint64_t request_id;
	const voxel_light_update_config *resolved_light_config;
	const RemeshPriority *matching_priority;
	bool explicit_incremental_metadata;
	bool interactive_request;

	if (!chunk.initialized || !chunk.mesh_dirty
		|| chunk.pending_mesh_request_id != 0U)
	{
		return (FT_ERR_SUCCESS);
	}
	/* The priority record is the authoritative hand-off for an edit.  Most
	 * callers pass its fields directly, but the generic dirty path and recovery
	 * paths can reach this function without them.  Recover the immutable seed
	 * here before any geometry-only decision is made; otherwise the edit can be
	 * silently submitted to the full-light solver and the published light can
	 * briefly regress to zero. */
	matching_priority = nullptr;
	for (const RemeshPriority &priority : this->priority_remeshes_)
	{
		if (priority.chunk_x == chunk.chunk_x
			&& priority.chunk_z == chunk.chunk_z)
		{
			matching_priority = &priority;
			break ;
		}
	}
	explicit_incremental_metadata = incremental_additive_light != FT_FALSE
		|| incremental_removal_light != FT_FALSE
		|| (incremental_light_seeds != nullptr
			&& !incremental_light_seeds->empty());
	if (!explicit_incremental_metadata && matching_priority != nullptr
		&& (matching_priority->incremental_additive_light != FT_FALSE
			|| matching_priority->incremental_removal_light != FT_FALSE
			|| !matching_priority->incremental_light_seeds.empty()))
	{
		incremental_additive_light =
			matching_priority->incremental_additive_light;
		incremental_removal_light =
			matching_priority->incremental_removal_light;
		incremental_local_x = matching_priority->incremental_local_x;
		incremental_local_y = matching_priority->incremental_local_y;
		incremental_local_z = matching_priority->incremental_local_z;
		incremental_old_block_id = matching_priority->incremental_old_block_id;
		incremental_new_block_id = matching_priority->incremental_new_block_id;
		incremental_light_seeds =
			&matching_priority->incremental_light_seeds;
		geometry_only = matching_priority->geometry_only;
	}
	if (chunk.waits_for_neighbor_light)
	{
		const WorldChunk *dependency = this->world_.find_chunk(
			chunk.light_dependency_chunk_x, chunk.light_dependency_chunk_z);
		if (dependency != nullptr
			&& (dependency->pending_mesh_request_id != 0U
				|| dependency->light_is_current() == false))
		{
			const bool dependency_waits_back = dependency->waits_for_neighbor_light
				&& dependency->light_dependency_chunk_x == chunk.chunk_x
				&& dependency->light_dependency_chunk_z == chunk.chunk_z;
			const bool chunk_has_lower_coordinate =
				chunk.chunk_x < dependency->chunk_x
				|| (chunk.chunk_x == dependency->chunk_x
					&& chunk.chunk_z < dependency->chunk_z);
			if (dependency_waits_back)
			{
				/* Two edits can arrive on opposite sides of the same face before
				 * either light result is published.  Waiting on each other would
				 * permanently strand both priority entries.  The lower coordinate
				 * wins the first solve; the other side remains gated and is rebuilt
				 * from that newly current source afterward. */
				if (chunk_has_lower_coordinate)
				{
					chunk.clear_light_dependency();
					#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
					std::fprintf(stderr,
						"[WorldGen] light dependency cycle broken winner="
						"(%d,%d) blocked=(%d,%d)\n", chunk.chunk_x,
						chunk.chunk_z, dependency->chunk_x, dependency->chunk_z);
					#endif
				}
				else
				{
					WorldChunk *mutable_dependency = this->world_.find_chunk_mutable(
						dependency->chunk_x, dependency->chunk_z);
					if (mutable_dependency != nullptr)
					{
						mutable_dependency->clear_light_dependency();
						mutable_dependency->mesh_dirty = true;
						this->prioritize_chunk_remesh(
							mutable_dependency->chunk_x,
							mutable_dependency->chunk_z);
					}
				}
			}
			else
			{
			/* A dependency can outlive the request that originally made it
			 * necessary.  In that case the target must not wait forever on a
			 * neighbour that is neither pending nor dirty.  Re-arm the neighbour's
			 * normal remesh path; the next scheduler pass will capture a fresh
			 * immutable halo and the target can then converge from it. */
			bool dependency_prioritized = false;
			for (const RemeshPriority &priority : this->priority_remeshes_)
			{
				if (priority.chunk_x == dependency->chunk_x
					&& priority.chunk_z == dependency->chunk_z)
				{
					dependency_prioritized = true;
					break ;
				}
			}
			if (dependency->pending_mesh_request_id == 0U
				&& !dependency_prioritized
				&& !this->generation_pipeline_.has_remesh_for_chunk(
					dependency->chunk_x, dependency->chunk_z))
			{
				WorldChunk *mutable_dependency = this->world_.find_chunk_mutable(
					dependency->chunk_x, dependency->chunk_z);
				if (mutable_dependency != nullptr)
				{
					if (!mutable_dependency->mesh_dirty)
						this->mark_remesh_dirty(*mutable_dependency, FT_TRUE);
					this->prioritize_chunk_remesh(dependency->chunk_x,
						dependency->chunk_z);
				}
			}
			}
		#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
			if (this->stream_frame_ % 32U == 0U)
				std::fprintf(stderr,
					"[WorldGen] neighbour remesh waiting target=(%d,%d) "
					"source=(%d,%d) source_pending=%llu source_light_current=%d\n",
					chunk.chunk_x, chunk.chunk_z,
					dependency->chunk_x, dependency->chunk_z,
					static_cast<unsigned long long>(
						dependency->pending_mesh_request_id),
					dependency->light_is_current() ? 1 : 0);
		#endif
			if (dependency_waits_back && chunk_has_lower_coordinate)
			{
				/* The deterministic winner may proceed with the previous neighbour
				 * light as its conservative boundary; the losing side is retried
				 * after this result makes the source current. */
			}
			else
				return (FT_ERR_FULL);
		}
		chunk.clear_light_dependency();
	}
	/* A neighbour can invalidate this chunk's mesh without invalidating its
	 * light input.  Do not turn that geometry-only notification into a full
	 * relight after an interactive edit; the current light buffer remains the
	 * authoritative visible value until a real light-input revision changes. */
	if (incremental_additive_light == FT_FALSE
		&& incremental_removal_light == FT_FALSE
		&& chunk.light_is_current()
		&& chunk.light_ready_for_render)
		geometry_only = true;
	if (chunk.remesh_cancellation_token == nullptr)
		return (FT_ERR_INVALID_STATE);
	interactive_request = false;
	/* The edit metadata is authoritative.  Do not rely only on the separate
	 * priority deque: a recenter, cancellation, or queue rotation can make the
	 * deque temporarily lag behind the direct edit submission. */
	if (incremental_additive_light != FT_FALSE
		|| incremental_removal_light != FT_FALSE
		|| (incremental_light_seeds != nullptr
			&& !incremental_light_seeds->empty()))
		interactive_request = true;
	if (matching_priority != nullptr && matching_priority->interactive)
		interactive_request = true;
	/* Keep one bounded remesh slot available for an interactive edit.  Without
	 * this reservation, startup neighbour-arrival work can occupy both slots;
	 * the edit then reports FT_ERR_FULL until background work finishes even
	 * though it has higher scheduling priority.  The total remains bounded and
	 * interactive work still cannot exceed the full limit. */
	{
		const std::size_t occupied_remesh_slots =
			this->generation_pipeline_.remesh_in_flight_count()
			+ this->remesh_capture_in_flight_.load();
		const std::size_t background_limit =
			WORLD_STREAM_MAX_REMESH_IN_FLIGHT > 1U
			? WORLD_STREAM_MAX_REMESH_IN_FLIGHT - 1U : 0U;
		if ((!interactive_request && occupied_remesh_slots >= background_limit)
			|| (interactive_request
				&& occupied_remesh_slots >= WORLD_STREAM_MAX_REMESH_IN_FLIGHT))
			return (FT_ERR_FULL);
	}
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (!interactive_request && chunk.mesh_dirty
		&& chunk.last_mesh_publication_voxel_revision != chunk.voxel_revision)
	{
		std::fprintf(stderr,
			"[WorldGen] generic edited remesh queued chunk=(%d,%d) "
			"voxel=%llu priority_present=%d priority_interactive=%d "
			"priority_add=%d priority_remove=%d priority_seeds=%zu "
			"priority_depth=%zu\n",
			chunk.chunk_x, chunk.chunk_z,
			static_cast<unsigned long long>(chunk.voxel_revision),
			matching_priority != nullptr ? 1 : 0,
			matching_priority != nullptr && matching_priority->interactive ? 1 : 0,
			matching_priority != nullptr
				&& matching_priority->incremental_additive_light != FT_FALSE ? 1 : 0,
			matching_priority != nullptr
				&& matching_priority->incremental_removal_light != FT_FALSE ? 1 : 0,
			matching_priority != nullptr
				? matching_priority->incremental_light_seeds.size() : 0U,
			this->priority_remeshes_.size());
	}
#endif
	std::shared_ptr<std::atomic<uint64_t>> cancellation_token =
		chunk.remesh_cancellation_token;
	const uint64_t cancellation_token_value =
		cancellation_token->fetch_add(1U, std::memory_order_acq_rel) + 1U;
	resolved_light_config = &this->light_update_config_;
	if (interactive_request)
	{
		resolved_light_config = &this->interactive_light_update_config_;
	}
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
	task.content_version = chunk.content_version;
	task.light_input_version = chunk.light_input_version;
	task.interactive = interactive_request ? FT_TRUE : FT_FALSE;
	task.geometry_only = geometry_only;
	task.incremental_additive_light = incremental_additive_light;
	task.incremental_removal_light = incremental_removal_light;
	task.incremental_local_x = incremental_local_x;
	task.incremental_local_y = incremental_local_y;
	task.incremental_local_z = incremental_local_z;
	task.incremental_old_block_id = incremental_old_block_id;
	task.incremental_new_block_id = incremental_new_block_id;
	try
	{
		if (incremental_light_seeds != nullptr)
			task.incremental_light_seeds = *incremental_light_seeds;
		if (task.incremental_light_seeds.empty()
			&& (incremental_additive_light != FT_FALSE
				|| incremental_removal_light != FT_FALSE))
			task.incremental_light_seeds.push_back({incremental_additive_light,
				incremental_removal_light, FT_FALSE, incremental_local_x, incremental_local_y,
				incremental_local_z, incremental_old_block_id,
				incremental_new_block_id, 0U});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	#if defined(DEBUG)
	if (chunk.chunk_x == 0 && chunk.chunk_z == -1)
		std::fprintf(stderr,
			"[WorldGen] remesh submit candidate chunk=(%d,%d) "
			"incremental_add=%d incremental_remove=%d seeds=%zu "
			"geometry_only=%d\n", chunk.chunk_x, chunk.chunk_z,
			incremental_additive_light != FT_FALSE ? 1 : 0,
			incremental_removal_light != FT_FALSE ? 1 : 0,
			task.incremental_light_seeds.size(), geometry_only ? 1 : 0);
	#endif
	task.cancellation_token = cancellation_token;
	task.cancellation_token_value = cancellation_token_value;
	task.light_update_config = *resolved_light_config;
	task.dependency_valid = chunk.waits_for_neighbor_light ? FT_TRUE : FT_FALSE;
	task.dependency_chunk_x = chunk.light_dependency_chunk_x;
	task.dependency_chunk_z = chunk.light_dependency_chunk_z;
	{
		const int32_t offsets[9][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1},
			{0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
		for (int32_t index = 0; index < 9; ++index)
		{
			WorldChunk *source = this->world_.find_chunk_mutable(
				chunk.chunk_x + offsets[index][0],
				chunk.chunk_z + offsets[index][1]);
			if (source != nullptr)
				task.read_states[index] = source->read_state;
		}
	}
	try
	{
		std::lock_guard<std::mutex> lock(this->remesh_capture_mutex_);
		std::deque<RemeshCaptureTask>::iterator iterator;

		if (this->remesh_capture_stopping_)
			return (FT_ERR_INVALID_STATE);
		iterator = this->remesh_capture_tasks_.begin();
		while (iterator != this->remesh_capture_tasks_.end())
		{
			if (iterator->chunk_x == task.chunk_x
				&& iterator->chunk_z == task.chunk_z)
			{
				if (iterator->cancellation_token != nullptr)
					iterator->cancellation_token->fetch_add(1U,
						std::memory_order_acq_rel);
				iterator = this->remesh_capture_tasks_.erase(iterator);
				this->remesh_capture_in_flight_.fetch_sub(1U);
				chunk.clear_pending_remesh_request();
			}
			else
				++iterator;
		}
		this->remesh_capture_tasks_.push_back(task);
		/* Publish ownership only after the capture queue accepted the task.  This
		 * keeps the chunk marker transactional if queue allocation fails. */
		chunk.set_pending_remesh_request(request_id,
			interactive_request ? FT_TRUE : FT_FALSE);
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

void WorldChunkStreamer::drain_remesh_capture_failures() noexcept
{
	std::deque<RemeshCaptureFailure> failures;
	{
		std::lock_guard<std::mutex> lock(this->remesh_capture_mutex_);
		failures.swap(this->remesh_capture_failures_);
	}
	while (!failures.empty())
	{
		this->world_.clear_pending_remesh_unlocked(failures.front().chunk_x,
			failures.front().chunk_z, failures.front().request_id);
		failures.pop_front();
	}
}

void WorldChunkStreamer::recover_orphaned_remesh_markers() noexcept
{
	for (int32_t index = 0; index < this->world_.chunk_count; ++index)
	{
		WorldChunk &chunk = this->world_.chunks[index];
		bool capture_owned = false;
		bool priority_owned = false;

		if (!chunk.initialized || chunk.pending_mesh_request_id == 0U)
			continue ;
		{
			std::lock_guard<std::mutex> lock(this->remesh_capture_mutex_);
			if (this->remesh_capture_processing_request_id_
				== chunk.pending_mesh_request_id)
				capture_owned = true;
			for (const RemeshCaptureTask &task : this->remesh_capture_tasks_)
			{
				if (task.request_id == chunk.pending_mesh_request_id)
				{
					capture_owned = true;
					break ;
				}
			}
		}
		for (const RemeshPriority &priority : this->priority_remeshes_)
		{
			if (priority.chunk_x == chunk.chunk_x
				&& priority.chunk_z == chunk.chunk_z)
			{
				priority_owned = true;
				break ;
			}
		}
		if (capture_owned
			|| priority_owned
			|| this->generation_pipeline_.has_remesh_for_chunk(
				chunk.chunk_x, chunk.chunk_z)
			|| this->generation_pipeline_.has_request_or_result(
				chunk.pending_mesh_request_id))
			continue ;
		chunk.clear_pending_remesh_request();
		chunk.mesh_dirty = true;
		/* Recovery only restores ownership state.  The normal dirty scheduler
		 * will choose geometry-only or full lighting from the current versions;
		 * creating an empty priority entry here can discard a newer edit seed. */
	}
}

void WorldChunkStreamer::mark_remesh_dirty(WorldChunk &chunk,
	ft_bool light_input_changed) noexcept
{
	/* Mesh visibility and lighting validity are separate state.  A geometry
	 * notification must not throw away a valid light buffer; only callers that
	 * know the lighting input changed may advance this version. */
	if (light_input_changed != FT_FALSE)
	{
		chunk.light_revision += 1U;
		chunk.mark_light_input_changed();
	}
	chunk.mesh_dirty = true;
	return ;
}

void WorldChunkStreamer::mark_neighbor_remeshes(int32_t chunk_x,
	int32_t chunk_z, bool incremental_edit, bool source_arrival) noexcept
{
	struct face_descriptor
	{
		int32_t target_x;
		int32_t target_z;
		int32_t target_fixed;
		int32_t source_fixed;
		uint8_t face;
	};
	static const face_descriptor faces[4] = {
		{-1, 0, GAME_VOXEL_CHUNK_WIDTH - 1, 0, 0U},
		{1, 0, 0, GAME_VOXEL_CHUNK_WIDTH - 1, 1U},
		{0, -1, GAME_VOXEL_CHUNK_DEPTH - 1, 0, 2U},
		{0, 1, 0, GAME_VOXEL_CHUNK_DEPTH - 1, 3U}};
	WorldChunk *source;
	WorldChunk *target;
	int32_t face_index;

	/* The source chunk can only inject light through a shared cardinal face.
	 * A ready source uses an additive frontier seeded only at receiving cells
	 * that can actually see non-zero light across that face.  An unavailable or
	 * not-yet-current source remains a conservative full-solver case, while
	 * diagonal arrivals are geometry-only. */
	target = this->world_.find_chunk_mutable(chunk_x, chunk_z);
	/* A newly arrived chunk already carries the mesh/light pair produced by
	 * generation.  Re-queueing the source itself here only creates a redundant
	 * remesh and can starve the face-sharing border work.  Interactive edits do
	 * need the source remesh, so retain that path. */
	if (target != nullptr && !source_arrival)
		this->mark_remesh_dirty(*target, FT_FALSE);
	if (!incremental_edit && source_arrival)
	{
		/* A newly arrived source must also refresh the light frontier on each
		 * face-sharing neighbour.  A geometry-only notification is insufficient:
		 * it republishes the old light buffer and can leave a valid neighbour
		 * permanently dark at the newly available border.  Targets without a
		 * usable baseline still take the dependency/full-solve path; targets with
		 * a baseline fall through to the bounded face scan below. */
		face_index = 0;
		while (face_index < 4)
		{
			const face_descriptor &face = faces[face_index];
			bool interactive_pending = false;
			target = this->world_.find_chunk_mutable(chunk_x + face.target_x,
				chunk_z + face.target_z);
			if (target != nullptr && target->initialized)
			{
				/* Recenter calls this notification after the source slot has been
				 * retired.  Rebuild the target against the missing-neighbour air
				 * boundary while retaining its valid light baseline.  Otherwise its
				 * old mesh can permanently leave a through-world opening. */
				const WorldChunk *arrived_source = this->world_.find_chunk(
					chunk_x, chunk_z);
				if (source_arrival && (arrived_source == nullptr
					|| !arrived_source->initialized))
				{
					target->set_border_mesh_publication_pending(true);
					this->mark_remesh_dirty(*target, FT_FALSE);
					target->clear_light_dependency();
					face_index += 1;
					continue ;
				}
				interactive_pending = target->owns_current_interactive_remesh();
				for (const RemeshPriority &priority : this->priority_remeshes_)
				{
					if (priority.interactive
						&& priority.chunk_x == target->chunk_x
						&& priority.chunk_z == target->chunk_z
						&& priority.voxel_revision == target->voxel_revision
						&& priority.content_version == target->content_version
						&& priority.light_input_version
							== target->light_input_version)
					{
						interactive_pending = true;
						break ;
					}
				}
				/* A generation notification must never invalidate an edit that
				 * is already queued or being solved.  Its revision-gated result
				 * will include the authoritative block change and the required
				 * border frontier. */
				/* A neighbour arrival must not invalidate an already published
				 * lighting baseline merely because the neighbour is still catching
				 * up.  Keep that baseline drawable and let the neighbour's eventual
				 * current result trigger the border refinement.  Only a target with
				 * no usable light baseline needs to wait for the dependency. */
				if (interactive_pending)
				{
					/* The pending interactive request owns the next light solve.
					 * Do not replace its seeds with an arrival notification. */
					this->mark_remesh_dirty(*target, FT_FALSE);
				}
				else
				{
					/* Do not launch a worker solve unless the newly available source
					 * face can change the target's currently published boundary.  The
					 * old unconditional invalidation made normal startup repeatedly
					 * discard valid light and filled the remesh queue with stale work. */
					source = this->world_.find_chunk_mutable(chunk_x, chunk_z);
					if (source != nullptr
						&& source_arrival_changes_target_face(*source, *target,
							face.face))
					{
						this->mark_remesh_dirty(*target, FT_TRUE);
						target->set_light_dependency(chunk_x, chunk_z);
					}
					else
					{
						target->clear_light_dependency();
					}
				}
			}
			face_index += 1;
		}
	}
	face_index = 0;
	while (face_index < 4)
	{
		const face_descriptor &face = faces[face_index];
		bool seeded = false;
		target = this->world_.find_chunk_mutable(chunk_x + face.target_x,
			chunk_z + face.target_z);
		source = this->world_.find_chunk_mutable(chunk_x, chunk_z);
		if (target == nullptr)
		{
			face_index += 1;
			continue ;
		}
		if (source_arrival && !has_published_light_baseline(*target))
		{
			/* The target was already placed on the full-solve dependency path
			 * above.  Never downgrade that request to an incremental frontier
			 * using an uninitialized light buffer. */
			face_index += 1;
			continue ;
		}
		if (source_arrival && target->pending_mesh_request_id != 0U)
		{
			/* Preserve an already captured request.  Its immutable snapshot is
			 * authoritative for this publication; a later source notification
			 * will retry the border once that request completes. */
			face_index += 1;
			continue ;
		}
		if (source_arrival)
		{
			/* Arrival work was intentionally reduced to a worker-owned full
			 * solve above.  The edit path below is the only path that performs
			 * the precise synchronous seed inspection. */
			face_index += 1U;
			continue ;
		}
		if (source == nullptr || source->light_is_current() == false)
		{
			/* An existing but not-yet-current neighbour is a dependency, not
			 * evidence that the target light buffer is unusable.  Keeping the
			 * current target light avoids publishing a dark full-rebuild
			 * intermediate; the neighbour's next current result revisits this
			 * border and supplies the required frontier. */
			if (source != nullptr
				&& !has_published_light_baseline(*target))
			{
				/* Never launch a full solve from an obsolete neighbour halo.  The
				 * target keeps its last valid light buffer and waits for the source
				 * frontier to become current; otherwise publication briefly replaces
				 * correct lighting with a dark/partial fallback. */
				this->mark_remesh_dirty(*target, FT_FALSE);
				target->set_light_dependency(chunk_x, chunk_z);
			}
			else
			{
				this->mark_remesh_dirty(*target, FT_FALSE);
				target->clear_light_dependency();
			}
			face_index += 1;
			continue ;
		}
		if (this->stream_frame_
			<= target->incremental_light_protection_until_frame)
		{
			this->mark_remesh_dirty(*target, FT_FALSE);
			continue ;
		}
		for (int32_t first = 0; first < GAME_VOXEL_CHUNK_HEIGHT; ++first)
		{
			for (int32_t second = 0; second < GAME_VOXEL_CHUNK_WIDTH; ++second)
			{
				int32_t target_x = second;
				int32_t target_z = second;
				int32_t source_x = second;
				int32_t source_z = second;
				if (face.face == 0U || face.face == 1U)
				{
					target_x = face.target_fixed;
					source_x = face.source_fixed;
				}
				else if (face.face == 2U || face.face == 3U)
				{
					target_z = face.target_fixed;
					source_z = face.source_fixed;
				}
				const uint8_t source_light = source->light.get(source_x, first,
					source_z);
				const uint8_t target_light = target->light.get(target_x, first,
					target_z);
				uint32_t target_block_id = GAME_VOXEL_AIR_BLOCK;
				const voxel_block_metadata *target_metadata;
				uint8_t attenuation;
				uint8_t candidate_sky;
				uint8_t candidate_block;
				if (source_light == 0U
					|| target->chunk.read_block(target_x, first, target_z,
						&target_block_id) != FT_ERR_SUCCESS)
					continue ;
				target_metadata = &voxel_get_block_metadata(target_block_id);
				if (target_metadata->occludes_faces != FT_FALSE
					|| target_metadata->light_attenuation >= 15U)
				{
					/* An opaque target cannot receive a cross-border light
					 * contribution.  It still needs a geometry rebuild when the
					 * source arrives, but never a light solve for this cell. */
					continue ;
				}
				attenuation = target_metadata->light_attenuation;
				if (attenuation < 1U)
					attenuation = 1U;
				candidate_sky = voxel_light_sky(source_light) > attenuation
					? static_cast<uint8_t>(voxel_light_sky(source_light)
						- attenuation) : 0U;
				candidate_block = voxel_light_block(source_light) > attenuation
					? static_cast<uint8_t>(voxel_light_block(source_light)
						- attenuation) : 0U;
				/* Do not enqueue a frontier whose result is already present.  A
				 * lower existing value is deliberately conservative: arrival does
				 * not carry the previous source edge, so only a full solve can
				 * safely remove a stale contribution. */
				if (candidate_sky == voxel_light_sky(target_light)
					&& candidate_block == voxel_light_block(target_light))
					continue ;
				if (candidate_sky < voxel_light_sky(target_light)
					|| candidate_block < voxel_light_block(target_light))
				{
					this->mark_remesh_dirty(*target, FT_TRUE);
					seeded = true;
					break ;
				}
				if (!seeded)
				{
					this->mark_remesh_dirty(*target, FT_TRUE);
					seeded = true;
				}
				this->prioritize_chunk_remesh_from_origin(
					target->chunk_x, target->chunk_z, chunk_x, chunk_z,
					FT_TRUE, FT_FALSE, target_x, first, target_z,
					GAME_VOXEL_AIR_BLOCK, GAME_VOXEL_AIR_BLOCK, 0U,
					FT_FALSE, false, false);
			}
		}
		if (!seeded)
			this->mark_remesh_dirty(*target, FT_FALSE);
		face_index += 1;
	}
	return ;
}

void WorldChunkStreamer::mark_light_remesh_pending(WorldChunk &chunk) noexcept
{
	/* A border source may be newer than this chunk while this chunk still has
	 * a valid light buffer that must remain drawable.  Advance the request
	 * revision without invalidating light_input_version or the published light
	 * values; the completed dependent result will carry this revision. */
	chunk.light_revision += 1U;
	chunk.mesh_dirty = true;
}

void WorldChunkStreamer::mark_edit_remeshes(int32_t chunk_x,
	int32_t chunk_z, int32_t local_x, int32_t local_y,
	int32_t local_z) noexcept
{
	WorldChunk *chunk;

	chunk = this->world_.find_chunk_mutable(chunk_x, chunk_z);
	if (chunk != nullptr)
	{
		chunk->set_border_mesh_publication_pending(local_x == 0
			|| local_x == GAME_VOXEL_CHUNK_WIDTH - 1
			|| local_z == 0
			|| local_z == GAME_VOXEL_CHUNK_DEPTH - 1);
		this->mark_remesh_dirty(*chunk, FT_FALSE);
	}
	if (local_x == 0)
	{
		chunk = this->world_.find_chunk_mutable(chunk_x - 1, chunk_z);
		if (chunk != nullptr && chunk->initialized)
		{
			const bool light_needed = neighbor_edge_requires_remesh(chunk,
				GAME_VOXEL_CHUNK_WIDTH - 1, local_y, local_z);
			/* The source edit may require a border-light remesh, but it must
			 * not invalidate the neighbour's last valid light publication while
			 * that source revision is being solved. */
			if (light_needed)
				this->mark_light_remesh_pending(*chunk);
			else
				this->mark_remesh_dirty(*chunk, FT_FALSE);
			if (light_needed)
				chunk->set_light_dependency(chunk_x, chunk_z);
		}
	}
	if (local_x == GAME_VOXEL_CHUNK_WIDTH - 1)
	{
		chunk = this->world_.find_chunk_mutable(chunk_x + 1, chunk_z);
		if (chunk != nullptr && chunk->initialized)
		{
			const bool light_needed = neighbor_edge_requires_remesh(chunk, 0,
				local_y, local_z);
			if (light_needed)
				this->mark_light_remesh_pending(*chunk);
			else
				this->mark_remesh_dirty(*chunk, FT_FALSE);
			if (light_needed)
				chunk->set_light_dependency(chunk_x, chunk_z);
		}
	}
	if (local_z == 0)
	{
		chunk = this->world_.find_chunk_mutable(chunk_x, chunk_z - 1);
		if (chunk != nullptr && chunk->initialized)
		{
			const bool light_needed = neighbor_edge_requires_remesh(chunk, local_x,
				local_y, GAME_VOXEL_CHUNK_DEPTH - 1);
			if (light_needed)
				this->mark_light_remesh_pending(*chunk);
			else
				this->mark_remesh_dirty(*chunk, FT_FALSE);
			if (light_needed)
				chunk->set_light_dependency(chunk_x, chunk_z);
		}
	}
	if (local_z == GAME_VOXEL_CHUNK_DEPTH - 1)
	{
		chunk = this->world_.find_chunk_mutable(chunk_x, chunk_z + 1);
		if (chunk != nullptr && chunk->initialized)
		{
			const bool light_needed = neighbor_edge_requires_remesh(chunk, local_x,
				local_y, 0);
			if (light_needed)
				this->mark_light_remesh_pending(*chunk);
			else
				this->mark_remesh_dirty(*chunk, FT_FALSE);
			if (light_needed)
				chunk->set_light_dependency(chunk_x, chunk_z);
		}
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
	int32_t chunk_z, int32_t origin_chunk_x, int32_t origin_chunk_z,
	ft_bool incremental_additive_light, ft_bool incremental_removal_light,
	int32_t incremental_local_x, int32_t incremental_local_y,
	int32_t incremental_local_z, uint32_t incremental_old_block_id,
	uint32_t incremental_new_block_id, uint8_t incremental_old_source_light,
	ft_bool incremental_external_source, bool geometry_only,
	bool interactive) noexcept
{
	bool existing;
	RemeshPriority promoted;
	std::deque<RemeshPriority>::iterator insert_position;

	if (interactive)
		this->generation_pipeline_.cancel_active_background_remeshes();

	existing = false;
	for (auto iterator = this->priority_remeshes_.begin();
		iterator != this->priority_remeshes_.end(); ++iterator)
	{
		if (iterator->chunk_x == chunk_x && iterator->chunk_z == chunk_z)
		{
			promoted = std::move(*iterator);
			this->priority_remeshes_.erase(iterator);
			existing = true;
			break ;
		}
	}
	if (!existing)
	{
		promoted.chunk_x = chunk_x;
		promoted.chunk_z = chunk_z;
		promoted.incremental_additive_light = FT_FALSE;
		promoted.incremental_removal_light = FT_FALSE;
		promoted.incremental_local_x = 0;
		promoted.incremental_local_y = 0;
		promoted.incremental_local_z = 0;
		promoted.incremental_old_block_id = GAME_VOXEL_AIR_BLOCK;
		promoted.incremental_new_block_id = GAME_VOXEL_AIR_BLOCK;
		promoted.voxel_revision = 0U;
		promoted.content_version = 0U;
		promoted.light_input_version = 0U;
		promoted.interactive = false;
	}
	{
		WorldChunk *target = this->world_.find_chunk_mutable(chunk_x, chunk_z);
		if (target != nullptr && target->initialized)
		{
			promoted.voxel_revision = target->voxel_revision;
			promoted.content_version = target->content_version;
			promoted.light_input_version = target->light_input_version;
		}
	}
	/* A gameplay edit supersedes an arrival/background seed list for the same
	 * chunk.  Those seeds were captured from an older neighbourhood state and
	 * combining them with the new edit would force the worker onto the full
	 * reference solver.  Preserve already-interactive seeds so rapid player
	 * edits can still coalesce, but rebuild background work from this edit's
	 * immutable snapshot. */
	if (existing && interactive && !promoted.interactive)
	{
		promoted.incremental_light_seeds.clear();
		promoted.incremental_additive_light = FT_FALSE;
		promoted.incremental_removal_light = FT_FALSE;
	}
	/* A request carrying any light transition must remain a normal remesh;
	 * geometry-only is reserved for a neighbor whose light input is unchanged. */
	promoted.geometry_only = geometry_only
		&& incremental_additive_light == FT_FALSE
		&& incremental_removal_light == FT_FALSE;
	if (incremental_additive_light != FT_FALSE
		|| incremental_removal_light != FT_FALSE)
	{
		WorldGenerationPipeline::IncrementalLightSeed seed;
		bool duplicate = false;
		bool conflicting_cell = false;
		seed.additive = incremental_additive_light;
		seed.removal = incremental_removal_light;
		seed.external_source = incremental_external_source;
		seed.local_x = incremental_local_x;
		seed.local_y = incremental_local_y;
		seed.local_z = incremental_local_z;
		seed.old_block_id = incremental_old_block_id;
		seed.new_block_id = incremental_new_block_id;
		seed.old_source_light = incremental_old_source_light;
		for (const WorldGenerationPipeline::IncrementalLightSeed &existing_seed
			: promoted.incremental_light_seeds)
		{
			if (existing_seed.local_x == seed.local_x
				&& existing_seed.local_y == seed.local_y
				&& existing_seed.local_z == seed.local_z
				&& (existing_seed.additive != seed.additive
					|| existing_seed.removal != seed.removal
					|| existing_seed.external_source != seed.external_source
					|| existing_seed.old_block_id != seed.old_block_id
					|| existing_seed.new_block_id != seed.new_block_id
					|| existing_seed.old_source_light != seed.old_source_light))
			{
				conflicting_cell = true;
				break ;
			}
			if (existing_seed.additive == seed.additive
				&& existing_seed.removal == seed.removal
				&& existing_seed.external_source == seed.external_source
				&& existing_seed.local_x == seed.local_x
				&& existing_seed.local_y == seed.local_y
				&& existing_seed.local_z == seed.local_z
				&& existing_seed.old_block_id == seed.old_block_id
				&& existing_seed.new_block_id == seed.new_block_id
				&& existing_seed.old_source_light == seed.old_source_light)
			{
				duplicate = true;
				break ;
			}
		}
		if (conflicting_cell)
		{
			/* A seed describes a transition from the captured old state to
			 * the final state.  Two different transitions for one cell cannot
			 * safely be replayed against one snapshot.  Discard the ambiguous
			 * incremental description and let the revision-gated full solver
			 * rebuild the current state. */
			promoted.incremental_light_seeds.clear();
			promoted.incremental_additive_light = FT_FALSE;
			promoted.incremental_removal_light = FT_FALSE;
		}
		else if (!duplicate)
		{
			try
			{
				promoted.incremental_light_seeds.push_back(seed);
			}
			catch (...)
			{
				/* Allocation failure must remain a safe full-solver request. */
				promoted.incremental_light_seeds.clear();
				promoted.incremental_additive_light = FT_FALSE;
				promoted.incremental_removal_light = FT_FALSE;
			}
		}
		if (!promoted.incremental_light_seeds.empty())
		{
			const WorldGenerationPipeline::IncrementalLightSeed &first_seed =
				promoted.incremental_light_seeds.front();
			promoted.incremental_additive_light = first_seed.additive;
			promoted.incremental_removal_light = first_seed.removal;
			promoted.incremental_local_x = first_seed.local_x;
			promoted.incremental_local_y = first_seed.local_y;
			promoted.incremental_local_z = first_seed.local_z;
			promoted.incremental_old_block_id = first_seed.old_block_id;
			promoted.incremental_new_block_id = first_seed.new_block_id;
		}
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
	/* Gameplay edits are interactive. Arrival notifications may carry
	 * incremental light seeds too, but remain background work during startup. */
	promoted.interactive = promoted.interactive || interactive;
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
	int32_t chunk_z, int32_t local_x, int32_t local_y,
	int32_t local_z, ft_bool incremental_additive_light,
	ft_bool incremental_removal_light, uint32_t incremental_old_block_id,
	uint32_t incremental_new_block_id, uint8_t incremental_old_source_light) noexcept
{
	/* The edited chunk must enter the priority queue before its request is
	 * captured.  Otherwise queue_chunk_remesh() observes no interactive entry
	 * and starts the edit with the slower background light budget. */
	this->prioritize_chunk_remesh_from_origin(chunk_x, chunk_z, chunk_x,
		chunk_z, incremental_additive_light, incremental_removal_light,
		local_x, local_y, local_z, incremental_old_block_id,
		incremental_new_block_id, incremental_old_source_light, FT_FALSE,
		false, true);
	/* Face-sharing neighbours are promoted only for an edge edit.  Their
	 * dependency gate keeps the source light current before the snapshot is
	 * captured, so the worker can remove/re-add only the affected frontier. */
	if (incremental_additive_light != FT_FALSE
		|| incremental_removal_light != FT_FALSE)
	{
		WorldChunk *chunk;
		if (local_x == 0)
		{
			chunk = this->world_.find_chunk_mutable(chunk_x - 1, chunk_z);
			if (chunk != nullptr && chunk->initialized)
			{
				const bool light_needed = neighbor_edge_requires_remesh(chunk,
					GAME_VOXEL_CHUNK_WIDTH - 1, local_y, local_z);
				this->prioritize_chunk_remesh_from_origin(chunk_x - 1, chunk_z,
					chunk_x, chunk_z, light_needed ? incremental_additive_light
						: FT_FALSE, light_needed ? incremental_removal_light : FT_FALSE,
					GAME_VOXEL_CHUNK_WIDTH - 1, local_y, local_z,
					incremental_old_block_id, incremental_new_block_id,
					incremental_old_source_light, light_needed ? FT_TRUE : FT_FALSE,
					!light_needed);
			}
		}
		if (local_x == GAME_VOXEL_CHUNK_WIDTH - 1)
		{
			chunk = this->world_.find_chunk_mutable(chunk_x + 1, chunk_z);
			if (chunk != nullptr && chunk->initialized)
			{
				const bool light_needed = neighbor_edge_requires_remesh(chunk, 0,
					local_y, local_z);
				this->prioritize_chunk_remesh_from_origin(chunk_x + 1, chunk_z,
					chunk_x, chunk_z, light_needed ? incremental_additive_light
						: FT_FALSE, light_needed ? incremental_removal_light : FT_FALSE, 0,
					local_y, local_z, incremental_old_block_id,
					incremental_new_block_id, incremental_old_source_light,
					light_needed ? FT_TRUE : FT_FALSE, !light_needed);
			}
		}
		if (local_z == 0)
		{
			chunk = this->world_.find_chunk_mutable(chunk_x, chunk_z - 1);
			if (chunk != nullptr && chunk->initialized)
			{
				const bool light_needed = neighbor_edge_requires_remesh(chunk,
					local_x, local_y, GAME_VOXEL_CHUNK_DEPTH - 1);
				this->prioritize_chunk_remesh_from_origin(chunk_x, chunk_z - 1,
					chunk_x, chunk_z, light_needed ? incremental_additive_light
						: FT_FALSE, light_needed ? incremental_removal_light : FT_FALSE,
					local_x,
					local_y, GAME_VOXEL_CHUNK_DEPTH - 1,
					incremental_old_block_id, incremental_new_block_id,
					incremental_old_source_light, light_needed ? FT_TRUE : FT_FALSE,
					!light_needed);
			}
		}
		if (local_z == GAME_VOXEL_CHUNK_DEPTH - 1)
		{
			chunk = this->world_.find_chunk_mutable(chunk_x, chunk_z + 1);
			if (chunk != nullptr && chunk->initialized)
			{
				const bool light_needed = neighbor_edge_requires_remesh(chunk, local_x,
					local_y, 0);
				this->prioritize_chunk_remesh_from_origin(chunk_x, chunk_z + 1,
					chunk_x, chunk_z, light_needed ? incremental_additive_light
						: FT_FALSE, light_needed ? incremental_removal_light : FT_FALSE,
					local_x,
					local_y, 0, incremental_old_block_id,
					incremental_new_block_id, incremental_old_source_light,
					light_needed ? FT_TRUE : FT_FALSE, !light_needed);
			}
		}
	}
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
			0U, this->stream_frame_, false, false, FT_FALSE, FT_FALSE, 0, 0, 0,
			GAME_VOXEL_AIR_BLOCK, GAME_VOXEL_AIR_BLOCK, 0U, 0U, 0U, {}});
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

void WorldChunkStreamer::promote_starved_remeshes() noexcept
{
	static const uint64_t STARVATION_FRAME_LIMIT = 120U;
	bool promoted_any = false;

	for (RemeshPriority &priority : this->priority_remeshes_)
	{
		if (priority.interactive
			|| this->stream_frame_ < priority.queued_frame
			|| this->stream_frame_ - priority.queued_frame
			< STARVATION_FRAME_LIMIT)
			continue ;
		priority.interactive = true;
		priority.origin_chunk_x = this->world_.center_chunk_x;
		priority.origin_chunk_z = this->world_.center_chunk_z;
		priority.origin_distance = static_cast<uint32_t>(
			std::abs(priority.chunk_x - this->world_.center_chunk_x)
			+ std::abs(priority.chunk_z - this->world_.center_chunk_z));
		{
			WorldChunk *chunk = this->world_.find_chunk_mutable(
				priority.chunk_x, priority.chunk_z);
			if (chunk != nullptr && chunk->initialized)
			{
				priority.voxel_revision = chunk->voxel_revision;
				priority.content_version = chunk->content_version;
				priority.light_input_version = chunk->light_input_version;
			}
		}
		this->remesh_starvation_promotions_ += 1U;
		promoted_any = true;
	}
	/* A promoted entry is now allowed to use the interactive lane.  Release
	 * the two bounded remesh slots from disposable background slices so stale
	 * work cannot keep the promoted entry waiting.  Generation requests retain
	 * their separate reservation and are not cancelled here. */
	if (promoted_any)
		this->generation_pipeline_.cancel_active_background_remeshes();
	return ;
}

int32_t WorldChunkStreamer::queue_neighbor_remeshes(int32_t chunk_x,
	int32_t chunk_z) noexcept
{
	WorldChunk *chunk;

	/* The edited/deferred chunk already advanced its light-input version when
	 * the block was applied.  Neighbours, however, must not be invalidated just
	 * because their border mesh needs to be reconsidered.  Reuse the normal
	 * dependency-aware path: retain a valid published baseline and schedule only
	 * the affected cardinal frontier; a full light input invalidation is used
	 * only when a neighbour has no usable baseline yet. */
	chunk = this->world_.find_chunk_mutable(chunk_x, chunk_z);
	if (chunk != nullptr)
		this->mark_remesh_dirty(*chunk, FT_FALSE);
	this->mark_neighbor_remeshes(chunk_x, chunk_z, false);
	return (FT_ERR_SUCCESS);
}
