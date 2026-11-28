#include "../../src/world/WorldChunkStreamer.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"
#include "../../Libft/Modules/Basic/limits.hpp"
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

	static bool has_pending_edit_geometry(const WorldChunk &chunk) noexcept
	{
		return (chunk.pending_mesh_request_id != 0U
			&& chunk.pending_mesh_request_interactive != FT_FALSE
			&& chunk.pending_mesh_request_voxel_revision == chunk.voxel_revision
			&& chunk.pending_mesh_request_content_version == chunk.content_version
			&& chunk.last_mesh_publication_voxel_revision != chunk.voxel_revision);
	}

	static bool uses_cached_preview_light(const WorldGenerationPipeline::Result &result) noexcept
	{
		using Result = WorldGenerationPipeline::Result;
		return ((result.stage_mask & Result::STAGE_GEOMETRY_ONLY) != 0U
			&& (result.stage_mask & Result::STAGE_GEOMETRY_FINAL) == 0U);
	}

	static void seed_border_opening_light(
		WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		const std::shared_ptr<const WorldChunkReadState> (&states)[9],
		int32_t source_x, int32_t source_z, int32_t receiver_x, int32_t y,
		int32_t receiver_z) noexcept
	{
		const int32_t x = receiver_x + (source_x - snapshot.chunk_x);
		const int32_t z = receiver_z + (source_z - snapshot.chunk_z);
		constexpr int32_t halo = WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO;
		constexpr int32_t edge = GAME_VOXEL_CHUNK_WIDTH + 2 * halo;
		if (x < -1 || x > GAME_VOXEL_CHUNK_WIDTH
			|| z < -1 || z > GAME_VOXEL_CHUNK_DEPTH
			|| y < 0 || y >= GAME_VOXEL_CHUNK_HEIGHT)
			return ;
		const uint32_t column = snapshot.lighting_ring_offsets[
			static_cast<std::size_t>(z + halo) * edge + x + halo];
		if (column == UINT32_MAX)
			return ;
		const std::size_t opening = static_cast<std::size_t>(column) + y;
		if (snapshot.lighting_blocks[opening] != GAME_VOXEL_AIR_BLOCK)
			return ;
		constexpr int32_t offsets[6][3] = {{0, 1, 0}, {0, -1, 0},
			{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}};
		const int32_t world_x = snapshot.chunk_x * GAME_VOXEL_CHUNK_WIDTH + x;
		const int32_t world_z = snapshot.chunk_z * GAME_VOXEL_CHUNK_DEPTH + z;
		uint8_t sky = voxel_light_sky(snapshot.lighting_existing_light[opening]);
		uint8_t block = voxel_light_block(snapshot.lighting_existing_light[opening]);
		for (const auto &offset : offsets)
		{
			const int32_t sample_y = y + offset[1];
			if (sample_y == GAME_VOXEL_CHUNK_HEIGHT)
				sky = 15U;
			if (sample_y < 0 || sample_y >= GAME_VOXEL_CHUNK_HEIGHT)
				continue ;
			uint8_t sample = 0U;
			if (WorldChunkSnapshotReader::lookup_snapshot_light(&snapshot,
					world_x + offset[0], sample_y, world_z + offset[2], &sample)
				!= FT_ERR_SUCCESS)
			{
				/* A preview captures only the outermost row from each neighbour.
				 * Sample the next row inward from the immutable source state. */
				for (const auto &state : states)
				{
					if (state == nullptr || state->initialized == FT_FALSE
						|| state->light_valid == FT_FALSE)
						continue ;
					const int32_t sample_x = world_x + offset[0] - state->world_x;
					const int32_t sample_z = world_z + offset[2] - state->world_z;
					if (sample_x < 0 || sample_x >= GAME_VOXEL_CHUNK_WIDTH
						|| sample_z < 0 || sample_z >= GAME_VOXEL_CHUNK_DEPTH)
						continue ;
					sample = (*state->light)[WorldChunkReadState::light_offset(
						sample_x, sample_y, sample_z)];
					break ;
				}
			}
			const uint8_t sample_sky = voxel_light_sky(sample);
			const uint8_t sample_block = voxel_light_block(sample);
			if (offset[1] == 1 && sample_sky == 15U)
				sky = 15U;
			else if (sample_sky > sky + 1U)
				sky = static_cast<uint8_t>(sample_sky - 1U);
			if (sample_block > block + 1U)
				block = static_cast<uint8_t>(sample_block - 1U);
		}
		snapshot.lighting_existing_light[opening] = voxel_light_pack(sky, block);
	}

	static void seed_border_preview_light(
		WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		const std::shared_ptr<const WorldChunkReadState> (&states)[9],
		int32_t source_x, int32_t source_z) noexcept
	{
		const int32_t dx = source_x - snapshot.chunk_x;
		const int32_t dz = source_z - snapshot.chunk_z;
		if (std::abs(dx) + std::abs(dz) != 1)
			return ;
		/* Several deletions can share one receiver preview. Seed the face
		 * from the top down so a stacked opening preserves direct sunlight. */
		const int32_t span = dx != 0 ? GAME_VOXEL_CHUNK_DEPTH : GAME_VOXEL_CHUNK_WIDTH;
		for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1; y >= 0; --y)
		{
			for (int32_t along = 0; along < span; ++along)
			{
				const int32_t x = dx == 0 ? along : (dx < 0 ? 0 : GAME_VOXEL_CHUNK_WIDTH - 1);
				const int32_t z = dz == 0 ? along : (dz < 0 ? 0 : GAME_VOXEL_CHUNK_DEPTH - 1);
				seed_border_opening_light(snapshot, states, source_x, source_z, x, y, z);
			}
		}
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
		stale_capture = task_cancelled || (task.interactive == FT_FALSE
			&& task.relevance_epoch != this->remesh_capture_relevance_epoch_.load());
		if (stale_capture)
			this->stale_remesh_capture_count_ += 1U;
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		if (stale_capture
			&& this->stale_remesh_capture_count_ % 64U == 0U)
			std::fprintf(stderr,
				"[WorldGen] stale remesh capture request=" FT_UINT64_DECIMAL_FORMAT
				" chunk=(%d,%d) task_epoch=" FT_UINT64_DECIMAL_FORMAT
				" current_epoch=" FT_UINT64_DECIMAL_FORMAT "\n",
				task.request_id, task.chunk_x,
				task.chunk_z,
				task.relevance_epoch,
				this->remesh_capture_relevance_epoch_.load());
	#endif
		if (!stale_capture)
			capture_start = std::chrono::steady_clock::now();
		const bool needs_full_lighting_halo = task.read_states[0] == nullptr
			|| task.read_states[0]->light_valid == FT_FALSE
			|| (!task.geometry_only && !task.geometry_preview_only
				&& task.incremental_light_seeds.empty());
		error_code = stale_capture ? FT_ERR_INVALID_STATE
			: WorldChunkSnapshotCapture::capture_read_states(
				task.read_states[0], task.read_states[1], task.read_states[2],
				task.read_states[3], task.read_states[4], task.read_states[5],
				task.read_states[6], task.read_states[7], task.read_states[8],
				snapshot, needs_full_lighting_halo);
		if (error_code == FT_ERR_SUCCESS
			&& task.light_dependency_mask != 0U)
		{
			/* Version counters alone cannot identify a neighbour whose published
			 * light is intentionally retained while it waits for this source.
			 * Such a field is immutable but not a valid source for relighting. */
			snapshot.lighting_halo_blocked = FT_TRUE;
			snapshot.lighting_halo_valid = FT_FALSE;
			snapshot.blocked_light_faces = static_cast<uint8_t>(
				(task.light_dependency_mask >> 1U) & 0x0fU);
			for (int32_t source_index = 1; source_index < 9; ++source_index)
			{
				if ((task.light_dependency_mask
						& static_cast<uint16_t>(1U << source_index)) != 0U
					&& source_index <= 4)
					snapshot.lighting_cardinal_halo_valid = FT_FALSE;
			}
		}
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
				|| (task.interactive == FT_FALSE
					&& task.relevance_epoch != this->remesh_capture_relevance_epoch_.load())))
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
			snapshot.geometry_preview_only = task.geometry_preview_only;
			/* Patch the private preview only. The final light field is solved
			 * independently against the current voxels. */
			if (task.geometry_preview_only && task.dependency_valid != FT_FALSE)
				seed_border_preview_light(snapshot, task.read_states,
					task.dependency_chunk_x, task.dependency_chunk_z);
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
	/* Stream candidates are relative to the camera. Interactive remeshes
	 * belong to loaded chunks and can keep going across a chunk crossing. */
	this->stream_relevance_epoch_ += 1U;
	this->remesh_capture_relevance_epoch_.store(
		this->stream_relevance_epoch_);
	for (int32_t index = 0; index < this->world_.chunk_count; ++index)
	{
		WorldChunk &chunk = this->world_.chunks[index];
		if (!chunk.initialized || chunk.pending_mesh_request_id == 0U
			|| chunk.owns_current_interactive_remesh() || has_pending_edit_geometry(chunk))
			continue ;
		/* A capture can already be entering the pipeline. Invalidate its
		 * token so it cannot replace the next request for this chunk. */
		if (chunk.remesh_cancellation_token != nullptr)
			chunk.remesh_cancellation_token->fetch_add(1U, std::memory_order_acq_rel);
		chunk.clear_pending_remesh_request();
		chunk.mesh_dirty = true;
	}
	this->generation_pipeline_.cancel_queued(true);
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

int32_t WorldChunkStreamer::update_pending_remeshes() noexcept
{
	this->stream_frame_++;
	this->drain_remesh_capture_failures();
	this->recover_orphaned_remesh_markers();
	const int32_t result = WorldGenerationResultCommitter::drain(*this,
		this->world_, true);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (WorldChunkAsyncSubmitter::submit_dirty_remeshes(*this));
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
		const int32_t recenter_scope = RuntimeAnalytics::begin_scope(
			RuntimeAnalyticsScope::WORLD_STREAM_RECENTER);
	#if defined(DEBUG)
		std::fprintf(stderr,
			"[WorldGen] recenter center=(%d,%d) previous=(%d,%d)\n",
			this->world_.center_chunk_x, this->world_.center_chunk_z,
			this->world_.chunk_index_center_x,
			this->world_.chunk_index_center_z);
	#endif
		this->handle_recenter();
		if (recenter_scope == FT_ERR_SUCCESS)
			(void)RuntimeAnalytics::end_scope();
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
	/* The frame timings are recorded by analytics. Console writes can block
	 * here when the process output is redirected or the console is busy. */
	#if defined(DEBUG)
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
	const WorldChunk *light_dependency = nullptr;
	bool explicit_incremental_metadata;
	bool interactive_request;
	bool geometry_preview_only = false;
	bool border_delete_preview = false;

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
	const auto find_matching_priority = [&]() noexcept -> const RemeshPriority *
	{
		for (const RemeshPriority &priority : this->priority_remeshes_)
		{
			if (priority.chunk_x == chunk.chunk_x && priority.chunk_z == chunk.chunk_z)
				return (&priority);
		}
		return (nullptr);
	};
	matching_priority = find_matching_priority();
	explicit_incremental_metadata = incremental_additive_light != FT_FALSE
		|| incremental_removal_light != FT_FALSE
		|| (incremental_light_seeds != nullptr
			&& !incremental_light_seeds->empty());
	const bool priority_seeds_selected = matching_priority != nullptr
		&& (matching_priority->interactive
			|| (!explicit_incremental_metadata
				&& (matching_priority->incremental_additive_light != FT_FALSE
					|| matching_priority->incremental_removal_light != FT_FALSE
					|| !matching_priority->incremental_light_seeds.empty())));
	if (priority_seeds_selected)
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
	const bool edit_needs_visible_mesh = (explicit_incremental_metadata
		|| (matching_priority != nullptr && matching_priority->interactive))
		&& chunk.last_mesh_publication_voxel_revision != chunk.voxel_revision
		&& chunk.light_ready_for_render && chunk.light_buffer_is_valid();
	if (chunk.waits_for_neighbor_light)
	{
		const WorldChunk *dependency = this->world_.find_chunk(
			chunk.light_dependency_chunk_x, chunk.light_dependency_chunk_z);
		/* A source can still be rebuilding its mesh after its light is ready.
		 * The receiver can use that published light immediately. */
		if (dependency != nullptr && !dependency->light_is_current())
		{
			border_delete_preview = matching_priority != nullptr
				&& matching_priority->geometry_only && matching_priority->interactive
				&& matching_priority->origin_chunk_x == dependency->chunk_x
				&& matching_priority->origin_chunk_z == dependency->chunk_z
				&& incremental_new_block_id == GAME_VOXEL_AIR_BLOCK
				&& voxel_block_occludes_faces(incremental_old_block_id) != FT_FALSE
				&& chunk.last_mesh_publication_voxel_revision == chunk.voxel_revision
				&& has_published_light_baseline(chunk)
				&& has_published_light_baseline(*dependency);
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
					#if defined(DEBUG)
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
			/* Promoting the dependency may have moved the target's deque entry. */
			matching_priority = find_matching_priority();
			if (priority_seeds_selected)
				incremental_light_seeds = matching_priority != nullptr
					? &matching_priority->incremental_light_seeds : nullptr;
		#if defined(DEBUG)
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
			else if (border_delete_preview || edit_needs_visible_mesh)
			{
				/* The neighbour may take several slices to relight. The edit's
				 * geometry can already use this chunk's last published light. */
				geometry_preview_only = true;
				if (border_delete_preview)
					light_dependency = dependency;
			}
			else
				return (FT_ERR_FULL);
		}
		if (!geometry_preview_only && dependency != nullptr && dependency->initialized
			&& dependency->light_is_current())
			light_dependency = dependency;
	}
	const std::size_t occupied_remesh_slots =
		this->generation_pipeline_.remesh_in_flight_count()
		+ this->remesh_capture_in_flight_.load();
	/* A long relight can retain both normal slots across several frames.
	 * Give a newly edited voxel one extra slot for its first drawable mesh.
	 * Its full lighting work goes back through the normal budget. */
	if (occupied_remesh_slots >= WORLD_STREAM_MAX_REMESH_IN_FLIGHT
		&& !geometry_preview_only && edit_needs_visible_mesh && !chunk.light_is_current())
	{
		geometry_preview_only = true;
		light_dependency = nullptr;
	}
	/* Input versions decide whether this chunk needs a solve. A queued seed
	 * can outlive the publication that already applied it, while another
	 * border notification may only need to refresh exposed faces. */
	geometry_only = !geometry_preview_only && chunk.light_is_current()
		&& chunk.light_ready_for_render;
	if (geometry_only || geometry_preview_only)
	{
		/* The standalone local preview needs all the queued openings, even
		 * though it leaves the authoritative light solve for a later request. */
		if (geometry_preview_only && !border_delete_preview
			&& incremental_new_block_id == GAME_VOXEL_AIR_BLOCK
			&& incremental_old_block_id != GAME_VOXEL_AIR_BLOCK)
			incremental_additive_light = FT_TRUE;
		else
			incremental_additive_light = FT_FALSE;
		incremental_removal_light = FT_FALSE;
		if (geometry_only || border_delete_preview)
			incremental_light_seeds = nullptr;
	}
	if (chunk.remesh_cancellation_token == nullptr)
		return (FT_ERR_INVALID_STATE);
	interactive_request = geometry_preview_only;
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
	std::size_t remesh_limit = WORLD_STREAM_MAX_REMESH_IN_FLIGHT - 1U;
	if (interactive_request)
		remesh_limit = WORLD_STREAM_MAX_REMESH_IN_FLIGHT;
	if (geometry_preview_only)
		remesh_limit += 1U;
	if (occupied_remesh_slots >= remesh_limit)
		return (FT_ERR_FULL);
#if defined(DEBUG)
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
	task.light_dependency_mask = 0U;
	task.interactive = interactive_request ? FT_TRUE : FT_FALSE;
	task.geometry_only = geometry_only;
	task.geometry_preview_only = geometry_preview_only;
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
	task.light_update_config = *resolved_light_config;
	task.dependency_valid = light_dependency != nullptr ? FT_TRUE : FT_FALSE;
	task.dependency_chunk_x = light_dependency != nullptr
		? light_dependency->chunk_x : 0;
	task.dependency_chunk_z = light_dependency != nullptr
		? light_dependency->chunk_z : 0;
	{
		const int32_t offsets[9][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1},
			{0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
		for (int32_t index = 0; index < 9; ++index)
		{
			WorldChunk *source = this->world_.find_chunk_mutable(
				chunk.chunk_x + offsets[index][0],
				chunk.chunk_z + offsets[index][1]);
			if (source != nullptr)
			{
				task.read_states[index] = source->read_state;
				if (index != 0 && source->waits_for_neighbor_light
					&& !source->light_is_current())
					task.light_dependency_mask |= static_cast<uint16_t>(
						1U << index);
			}
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
				if (iterator->cancellation_token != nullptr
					&& iterator->cancellation_token != task.cancellation_token)
					iterator->cancellation_token->fetch_add(1U,
						std::memory_order_acq_rel);
				iterator = this->remesh_capture_tasks_.erase(iterator);
				this->remesh_capture_in_flight_.fetch_sub(1U);
				chunk.clear_pending_remesh_request();
			}
			else
				++iterator;
		}
		/* Issue the new token under the capture lock after retiring any old
		 * tasks. The queued and active requests for this slot share it. */
		task.cancellation_token_value = task.cancellation_token->fetch_add(1U,
			std::memory_order_acq_rel) + 1U;
		this->remesh_capture_tasks_.push_back(std::move(task));
		/* Publish ownership only after the capture queue accepted the task.  This
		 * keeps the chunk marker transactional if queue allocation fails. */
		chunk.set_pending_remesh_request(request_id,
			interactive_request ? FT_TRUE : FT_FALSE);
		chunk.pending_mesh_request_preview_only = geometry_preview_only;
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
		if (capture_owned
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
					/* Retained edits can survive a recenter, but a captured
					 * border from an evicted neighbor cannot. */
					if (target->pending_mesh_request_id != 0U)
					{
						if (target->remesh_cancellation_token != nullptr)
							target->remesh_cancellation_token->fetch_add(1U, std::memory_order_acq_rel);
						target->clear_pending_remesh_request();
					}
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
			face_index += 1;
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
	/* Preserve the last drawable buffer and invalidate its input revision.
	 * If a border seed is lost to cancellation, the retry still needs a solve. */
	this->mark_remesh_dirty(chunk, FT_TRUE);
}

void WorldChunkStreamer::invalidate_boundary_receivers(
	const WorldGenerationPipeline::Result &result) noexcept
{
	if (result.changed_light_faces == 0U)
		return ;
	for (std::size_t face = 0; face < result.boundary_read_states.size(); ++face)
	{
		const uint8_t bit = static_cast<uint8_t>(1U << face);
		if ((result.changed_light_faces & bit) == 0U)
			continue ;
		const auto &offset = WorldGenerationPipeline::BOUNDARY_NEIGHBORS[face];
		WorldChunk *target = this->world_.find_chunk_mutable(
			result.chunk_x + offset[0], result.chunk_z + offset[1]);
		if (target == nullptr || !target->initialized)
			continue ;
		const auto captured = result.boundary_read_states[face].lock();
		bool relight = (result.neighbor_relight_faces & bit) != 0U;
		bool remesh = (result.neighbor_remesh_faces & bit) != 0U;
		if (captured == nullptr || captured != target->read_state)
		{
			/* Another edit can replace the read state without changing how this
			 * face receives light. Recheck small changes against the live snapshot
			 * before replacing that edit's bounded frontier with a full solve. */
			const auto &changes = result.boundary_light_changes[face];
			if (changes.complete && changes.count != 0U && target->read_state != nullptr
				&& target->read_state->initialized != FT_FALSE)
			{
				uint8_t effect = 0U;
				for (std::size_t index = 0U; index < changes.count; ++index)
					effect |= changes.cells[index].effect_on(*target->read_state);
				relight = (effect & WorldGenerationPipeline::BoundaryLightChange::RELIGHT) != 0U;
				remesh = (effect & WorldGenerationPipeline::BoundaryLightChange::REMESH) != 0U;
			}
			else
				relight = true;
		}
		if (!relight && !remesh)
			continue ;
		bool preserve_preview = false;
		if (relight && has_pending_edit_geometry(*target)
			&& has_published_light_baseline(*target))
		{
			/* Preview-only work can finish on its existing light snapshot. A full
			 * solve is kept only if it already produced a drawable preview. */
			preserve_preview = target->pending_mesh_request_preview_only;
			if (!preserve_preview)
			{
				std::lock_guard<std::mutex> lock(this->generation_pipeline_.results_mutex_);
				preserve_preview = std::any_of(this->generation_pipeline_.results_.begin(),
					this->generation_pipeline_.results_.end(), [target](const auto &ready)
					{
						return (ready != nullptr && ready->request_id == target->pending_mesh_request_id
							&& ready->error_code == FT_ERR_SUCCESS && ready->mesh != nullptr
							&& uses_cached_preview_light(*ready));
					});
			}
		}
		this->mark_remesh_dirty(*target, relight ? FT_TRUE : FT_FALSE);
		if (target->pending_mesh_request_id != 0U && !preserve_preview)
		{
			if (target->remesh_cancellation_token != nullptr)
				target->remesh_cancellation_token->fetch_add(1U, std::memory_order_acq_rel);
			target->clear_pending_remesh_request();
		}
		if (!relight)
			continue ;
		/* The changed set can span an entire face. Solve from the authoritative
		 * voxel halo so a neighboring copy of the removed light cannot feed back
		 * into the result. Keep the previous drawable mesh until publication. */
		target->clear_light_dependency();
		for (RemeshPriority &priority : this->priority_remeshes_)
		{
			if (priority.chunk_x == target->chunk_x && priority.chunk_z == target->chunk_z)
			{
				priority.requires_full_light = true;
				priority.geometry_only = false;
				priority.incremental_additive_light = FT_FALSE;
				priority.incremental_removal_light = FT_FALSE;
				priority.incremental_light_seeds.clear();
				priority.voxel_revision = target->voxel_revision;
				priority.content_version = target->content_version;
				priority.light_input_version = target->light_input_version;
				break ;
			}
		}
	}
}

void WorldChunkStreamer::mark_edit_remeshes(int32_t chunk_x,
	int32_t chunk_z, int32_t local_x, int32_t local_y,
	int32_t local_z) noexcept
{
	this->mark_edit_remeshes(chunk_x, chunk_z, local_x, local_y, local_z, true);
}

void WorldChunkStreamer::mark_edit_remeshes(int32_t chunk_x,
	int32_t chunk_z, int32_t local_x, int32_t local_y,
	int32_t local_z, bool light_input_changed) noexcept
{
	WorldChunk *chunk;

	chunk = this->world_.find_chunk_mutable(chunk_x, chunk_z);
	if (chunk != nullptr)
		this->mark_remesh_dirty(*chunk, FT_FALSE);
	if (local_x == 0)
	{
		chunk = this->world_.find_chunk_mutable(chunk_x - 1, chunk_z);
		if (chunk != nullptr && chunk->initialized)
		{
			const bool light_needed = light_input_changed
				&& neighbor_edge_requires_remesh(chunk,
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
			const bool light_needed = light_input_changed
				&& neighbor_edge_requires_remesh(chunk, 0,
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
			const bool light_needed = light_input_changed
				&& neighbor_edge_requires_remesh(chunk, local_x,
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
			const bool light_needed = light_input_changed
				&& neighbor_edge_requires_remesh(chunk, local_x,
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
	bool coalesced_single_cell_edit = false;
	bool reverted_to_light_baseline = false;
	bool keep_border_preview_coordinates = false;
	RemeshPriority promoted;
	std::deque<RemeshPriority>::iterator insert_position;

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
			const bool border_geometry_changed = interactive
				&& (chunk_x != origin_chunk_x || chunk_z != origin_chunk_z)
				&& voxel_get_block_metadata(incremental_old_block_id).occludes_faces
					!= voxel_get_block_metadata(incremental_new_block_id).occludes_faces;
			keep_border_preview_coordinates = border_geometry_changed && geometry_only
				&& target->last_mesh_publication_voxel_revision == target->voxel_revision;
			/* A neighbour can retain its own voxel revision even when the
			 * source edit invalidates its captured border geometry. */
			if (border_geometry_changed && target->pending_mesh_request_id != 0U)
			{
				if (target->remesh_cancellation_token != nullptr)
					target->remesh_cancellation_token->fetch_add(1U,
						std::memory_order_acq_rel);
				target->clear_pending_remesh_request();
			}
			/* An opaque neighbor need not solve its own light just because it
			 * gained a face. The face must sample the newly opened cell after
			 * the source's light has been published. */
			if (geometry_only
				&& (chunk_x != origin_chunk_x || chunk_z != origin_chunk_z)
				&& voxel_get_block_metadata(incremental_old_block_id)
					.occludes_faces != FT_FALSE
				&& voxel_get_block_metadata(incremental_new_block_id)
					.occludes_faces == FT_FALSE)
				target->set_light_dependency(origin_chunk_x, origin_chunk_z);
			/* An immediate submission can finish before the scheduler sees
			 * its queue entry. Its seeds are already included in the baseline. */
			if (existing && target->light_buffer_is_valid()
				&& target->light_version == promoted.content_version
				&& target->computed_light_input_version
					== promoted.light_input_version)
			{
				promoted.incremental_light_seeds.clear();
				promoted.incremental_additive_light = FT_FALSE;
				promoted.incremental_removal_light = FT_FALSE;
				promoted.requires_full_light = false;
				promoted.single_cell_baseline_input_version =
					world_light_version::INVALID_VERSION;
			}
			/* A consecutive chain of edits to the same cell may alternate
			 * between its original block and new states. Keep the original
			 * publication as the base until another lighting input changes. */
			const bool local_edit = incremental_external_source == FT_FALSE
				&& origin_chunk_x == chunk_x && origin_chunk_z == chunk_z;
			const bool follows_same_border_source =
				existing && incremental_external_source != FT_FALSE
				&& target->waits_for_neighbor_light
				&& target->light_dependency_chunk_x == origin_chunk_x
				&& target->light_dependency_chunk_z == origin_chunk_z
				&& promoted.origin_chunk_x == origin_chunk_x
				&& promoted.origin_chunk_z == origin_chunk_z;
			const bool consecutive_content = local_edit
				? !target->waits_for_neighbor_light
					&& world_light_version::next(promoted.content_version)
						== target->content_version
					&& promoted.voxel_revision + 1U == target->voxel_revision
				: follows_same_border_source
					&& promoted.content_version == target->content_version
					&& promoted.voxel_revision == target->voxel_revision;
			const bool first_edit_from_baseline =
				(local_edit ? world_light_version::next(target->light_version)
					: target->light_version)
					== promoted.content_version
				&& world_light_version::next(target->computed_light_input_version)
					== promoted.light_input_version;
			const bool extends_coalesced_chain =
				promoted.single_cell_baseline_input_version
					!= world_light_version::INVALID_VERSION
				&& promoted.single_cell_baseline_input_version
					== target->computed_light_input_version;
			if (existing && interactive && promoted.interactive
				&& !promoted.requires_full_light
				&& promoted.incremental_light_seeds.size() == 1U
				&& target->light_buffer_is_valid() && consecutive_content
				&& (first_edit_from_baseline || extends_coalesced_chain)
				&& world_light_version::next(promoted.light_input_version)
					== target->light_input_version)
			{
				const WorldGenerationPipeline::IncrementalLightSeed &prior =
					promoted.incremental_light_seeds.front();
				coalesced_single_cell_edit =
					prior.external_source == incremental_external_source
					&& prior.local_x == incremental_local_x
					&& prior.local_y == incremental_local_y
					&& prior.local_z == incremental_local_z
					&& prior.new_block_id == incremental_old_block_id
					&& prior.old_source_light == incremental_old_source_light
					&& (prior.old_block_id == incremental_new_block_id
						|| prior.old_block_id == prior.new_block_id);
				if (coalesced_single_cell_edit)
				{
					reverted_to_light_baseline =
						prior.old_block_id == incremental_new_block_id;
					promoted.single_cell_baseline_input_version =
						target->computed_light_input_version;
				}
			}
			if (!coalesced_single_cell_edit
				&& (promoted.voxel_revision != target->voxel_revision
					|| promoted.content_version != target->content_version
					|| promoted.light_input_version != target->light_input_version))
				promoted.single_cell_baseline_input_version =
					world_light_version::INVALID_VERSION;
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
	if (!promoted.requires_full_light && (incremental_additive_light != FT_FALSE
		|| incremental_removal_light != FT_FALSE))
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
			/* A block edit and incoming light are distinct inputs, even when
			 * both enter the solver at the same boundary cell. */
			if (existing_seed.external_source != seed.external_source)
				continue ;
			if (existing_seed.local_x == seed.local_x
				&& existing_seed.local_y == seed.local_y
				&& existing_seed.local_z == seed.local_z
				&& (existing_seed.additive != seed.additive
					|| existing_seed.removal != seed.removal
					|| existing_seed.old_block_id != seed.old_block_id
					|| existing_seed.new_block_id != seed.new_block_id
					|| existing_seed.old_source_light != seed.old_source_light))
			{
				conflicting_cell = true;
				break ;
			}
			if (existing_seed.additive == seed.additive
				&& existing_seed.removal == seed.removal
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
		if (coalesced_single_cell_edit)
		{
			if (reverted_to_light_baseline)
			{
				seed.old_block_id = seed.new_block_id;
				seed.additive = FT_FALSE;
				seed.removal = FT_FALSE;
			}
			promoted.incremental_light_seeds.front() = seed;
		}
		else if (conflicting_cell)
		{
			/* A seed describes a transition from the captured old state to
			 * the final state.  Two different transitions for one cell cannot
			 * safely be replayed against one snapshot.  Discard the ambiguous
			 * incremental description and let the revision-gated full solver
			 * rebuild the current state. */
			promoted.incremental_light_seeds.clear();
			promoted.incremental_additive_light = FT_FALSE;
			promoted.incremental_removal_light = FT_FALSE;
			promoted.requires_full_light = true;
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
				promoted.requires_full_light = true;
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
	if (interactive && incremental_external_source == FT_FALSE
		&& incremental_old_block_id != incremental_new_block_id
		&& ((origin_chunk_x == chunk_x && origin_chunk_z == chunk_z)
			|| keep_border_preview_coordinates))
	{
		/* A full relight may discard conflicting solver seeds. Keep the
		 * most recent local edit for the visual preview. */
		promoted.incremental_local_x = incremental_local_x;
		promoted.incremental_local_y = incremental_local_y;
		promoted.incremental_local_z = incremental_local_z;
		promoted.incremental_old_block_id = incremental_old_block_id;
		promoted.incremental_new_block_id = incremental_new_block_id;
	}
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
	this->priority_remeshes_.insert(insert_position, std::move(promoted));
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
	const bool light_input_changed = incremental_additive_light != FT_FALSE
		|| incremental_removal_light != FT_FALSE;
	if (incremental_new_block_id == GAME_VOXEL_AIR_BLOCK
		&& voxel_block_occludes_faces(incremental_old_block_id) != FT_FALSE)
	{
		WorldChunk *source = this->world_.find_chunk_mutable(chunk_x, chunk_z);
		const bool edited_faces[4] = {local_x == 0, local_x == GAME_VOXEL_CHUNK_WIDTH - 1,
			local_z == 0, local_z == GAME_VOXEL_CHUNK_DEPTH - 1};
		if (source != nullptr)
		{
			for (std::size_t face = 0U; face < source->border_delete_dependencies.size(); ++face)
			{
				if (!edited_faces[face])
					continue ;
				const auto &offset = WorldGenerationPipeline::BOUNDARY_NEIGHBORS[face];
				const WorldChunk *receiver = this->world_.find_chunk(chunk_x + offset[0], chunk_z + offset[1]);
				const int32_t x = local_x - offset[0] * (GAME_VOXEL_CHUNK_WIDTH - 1);
				const int32_t z = local_z - offset[1] * (GAME_VOXEL_CHUNK_DEPTH - 1);
				if (receiver != nullptr && receiver->initialized && receiver->read_state != nullptr
					&& voxel_block_occludes_faces(receiver->read_state->block_at(x, local_y, z)) != FT_FALSE)
					source->border_delete_dependencies[face] = {source->voxel_revision, receiver->mesh_revision + 1U};
			}
		}
	}
	if (light_input_changed
		|| voxel_get_block_metadata(incremental_old_block_id).occludes_faces
			!= voxel_get_block_metadata(incremental_new_block_id).occludes_faces)
	{
		WorldChunk *chunk;
		if (local_x == 0)
		{
			chunk = this->world_.find_chunk_mutable(chunk_x - 1, chunk_z);
			if (chunk != nullptr && chunk->initialized)
			{
				const bool light_needed = light_input_changed
					&& neighbor_edge_requires_remesh(chunk,
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
				const bool light_needed = light_input_changed
					&& neighbor_edge_requires_remesh(chunk, 0, local_y, local_z);
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
				const bool light_needed = light_input_changed
					&& neighbor_edge_requires_remesh(chunk,
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
				const bool light_needed = light_input_changed
					&& neighbor_edge_requires_remesh(chunk, local_x, local_y, 0);
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

	for (RemeshPriority &priority : this->priority_remeshes_)
	{
		if (priority.interactive
			|| this->stream_frame_ < priority.queued_frame
			|| this->stream_frame_ - priority.queued_frame
			< STARVATION_FRAME_LIMIT)
			continue ;
		WorldChunk *chunk = this->world_.find_chunk_mutable(
			priority.chunk_x, priority.chunk_z);
		if (chunk != nullptr && (!chunk->mesh_dirty
			|| chunk->pending_mesh_request_id != 0U))
			continue ;
		priority.interactive = true;
		priority.origin_chunk_x = this->world_.center_chunk_x;
		priority.origin_chunk_z = this->world_.center_chunk_z;
		priority.origin_distance = static_cast<uint32_t>(
			std::abs(priority.chunk_x - this->world_.center_chunk_x)
			+ std::abs(priority.chunk_z - this->world_.center_chunk_z));
		this->remesh_starvation_promotions_ += 1U;
	}
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
