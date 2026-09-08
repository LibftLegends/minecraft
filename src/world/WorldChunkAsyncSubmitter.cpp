#include "../../src/world/WorldChunkAsyncSubmitter.hpp"
#include <cstdio>

namespace
{
	static bool playable_ring_is_ready(
		const WorldChunkStreamer &streamer) noexcept
	{
		const int32_t radius = WorldCoordinates::render_distance_to_chunk_radius(
			WorldCoordinates::MIN_RENDER_DISTANCE);
		const int32_t radius_squared = radius * radius;

		for (const WorldChunkStreamer::StreamCandidate &candidate
			: streamer.stream_candidates_)
		{
			if (candidate.dist_sq <= radius_squared
				&& candidate.state != WorldChunkStreamer::CANDIDATE_READY)
				return (false);
		}
		return (true);
	}

	static bool remesh_candidate_is_better(const WorldChunk &candidate,
		const WorldChunk *best, int32_t center_chunk_x,
		int32_t center_chunk_z) noexcept
	{
		int64_t candidate_dx;
		int64_t candidate_dz;
		int64_t best_dx;
		int64_t best_dz;
		int64_t candidate_distance;
		int64_t best_distance;

		if (best == nullptr)
			return (true);
		candidate_dx = static_cast<int64_t>(candidate.chunk_x)
			- static_cast<int64_t>(center_chunk_x);
		candidate_dz = static_cast<int64_t>(candidate.chunk_z)
			- static_cast<int64_t>(center_chunk_z);
		best_dx = static_cast<int64_t>(best->chunk_x)
			- static_cast<int64_t>(center_chunk_x);
		best_dz = static_cast<int64_t>(best->chunk_z)
			- static_cast<int64_t>(center_chunk_z);
		candidate_distance = candidate_dx * candidate_dx
			+ candidate_dz * candidate_dz;
		best_distance = best_dx * best_dx + best_dz * best_dz;
		if (candidate_distance != best_distance)
			return (candidate_distance < best_distance);
		return (candidate.chunk_x < best->chunk_x
			|| (candidate.chunk_x == best->chunk_x
				&& candidate.chunk_z < best->chunk_z));
	}
}

WorldChunkAsyncSubmitter::WorldChunkAsyncSubmitter()
{
}

WorldChunkAsyncSubmitter::WorldChunkAsyncSubmitter(const WorldChunkAsyncSubmitter &other)
{
	(void)other;
}

WorldChunkAsyncSubmitter::~WorldChunkAsyncSubmitter()
{
}

WorldChunkAsyncSubmitter &WorldChunkAsyncSubmitter::operator=(const WorldChunkAsyncSubmitter &other)
{
	(void)other;
	return (*this);
}

bool WorldChunkAsyncSubmitter::submit_async_candidate(WorldChunkStreamer &streamer,
	WorldChunkStreamer::StreamCandidate &candidate, int32_t chunk_x,
	int32_t chunk_z, int32_t *submitted, int32_t budget) noexcept
{
	uint64_t request_id;
	int32_t error_code;

	request_id = streamer.next_request_id_++;
	error_code = streamer.generation_pipeline_.submit_generation(request_id,
			streamer.world_epoch_, streamer.stream_relevance_epoch_,
			streamer.generation_revision_, chunk_x, chunk_z,
			streamer.world_.seed, streamer.world_.voxel_context.config(),
			VOXEL_STAGE_BASE_TERRAIN | VOXEL_STAGE_CAVES | VOXEL_STAGE_FLUIDS | VOXEL_STAGE_DECORATION | VOXEL_STAGE_STRUCTURES | VOXEL_STAGE_ORES,
			WorldGenerationPipeline::WorldGenerationOperation::STREAM);
	if (error_code == FT_ERR_FULL)
		return (true);
	if (error_code != FT_ERR_SUCCESS)
	{
		candidate.state = WorldChunkStreamer::CANDIDATE_FAILED_RETRYABLE;
		candidate.last_error = error_code;
		candidate.retry_frames = 1;
		return (false);
	}
	candidate.state = WorldChunkStreamer::CANDIDATE_GENERATING;
	candidate.queued_frame = streamer.stream_frame_;
	candidate.request_id = request_id;
	*submitted += 1;
	if (budget > 0 && *submitted >= budget)
		return (true);
	return (false);
}

bool WorldChunkAsyncSubmitter::process_async_candidate(WorldChunkStreamer &streamer,
	WorldChunkStreamer::StreamCandidate &candidate, int32_t *submitted,
	int32_t budget) noexcept
{
	int32_t chunk_x;
	int32_t chunk_z;

	if (candidate.state == WorldChunkStreamer::CANDIDATE_READY
		|| candidate.state == WorldChunkStreamer::CANDIDATE_GENERATING
		|| candidate.state == WorldChunkStreamer::CANDIDATE_MESHING
		|| candidate.state == WorldChunkStreamer::CANDIDATE_QUEUED)
		return (false);
	if (candidate.retry_frames > 0)
	{
		candidate.retry_frames -= 1;
		return (false);
	}
	WorldChunkCandidateScanner::refresh_stale_candidate(streamer, candidate);
	chunk_x = streamer.world_.center_chunk_x + candidate.offset_x;
	chunk_z = streamer.world_.center_chunk_z + candidate.offset_z;
	if (streamer.world_.find_chunk(chunk_x, chunk_z) != nullptr)
	{
		candidate.state = WorldChunkStreamer::CANDIDATE_READY;
		return (false);
	}
	return (WorldChunkAsyncSubmitter::submit_async_candidate(streamer,
			candidate, chunk_x, chunk_z, submitted, budget));
}

int32_t WorldChunkAsyncSubmitter::submit_dirty_remeshes(
	WorldChunkStreamer &streamer) noexcept
{
	int32_t dirty_index;
	int32_t scanned_count;
	int32_t error_code;
	/* Dirty chunks are coalesced by mesh_dirty. Keep the discovery scan small
	 * so a large loaded world cannot consume a frame while looking for work;
	 * the cursor preserves eventual progress across frames. */
	int32_t scan_budget;
	int32_t queue_budget;
	int32_t best_dirty_index;
	WorldChunk *best_dirty_chunk;
	int32_t priority_center_x;
	int32_t priority_center_z;
	const voxel_light_update_config &light_config =
		streamer.light_update_config_;
	scan_budget = static_cast<int32_t>(light_config.target_nodes_per_frame);
	if (scan_budget < static_cast<int32_t>(light_config.min_nodes_per_frame))
		scan_budget = static_cast<int32_t>(light_config.min_nodes_per_frame);
	if (scan_budget > static_cast<int32_t>(light_config.max_nodes_per_frame))
		scan_budget = static_cast<int32_t>(light_config.max_nodes_per_frame);
	queue_budget = static_cast<int32_t>(light_config.max_nodes_per_frame);
	if (queue_budget > 1)
		queue_budget = 1;

	if (streamer.world_.chunk_count <= 0)
		return (FT_ERR_SUCCESS);
	/* Initial generation owns the shared workers until every required
	 * playable candidate is published. Initial meshes already contain local
	 * light; border relights can safely follow once the ring exists. */
	if (playable_ring_is_ready(streamer) == false
		&& !streamer.priority_remesh_pending_)
		return (FT_ERR_SUCCESS);
	if (streamer.stream_frame_ < streamer.next_remesh_submission_frame_)
		return (FT_ERR_SUCCESS);
	if (streamer.remesh_priority_anchor_valid_
		&& streamer.stream_frame_
			>= streamer.remesh_priority_anchor_expiry_frame_)
		streamer.remesh_priority_anchor_valid_ = false;
	/* Keep the visible neighborhood ahead of distant arrival work, but retain
	 * one bounded interactive solve at a time. The priority queue deduplicates
	 * repeated notifications and the normal scan still services everything
	 * outside this local window. */
	{
		static const int32_t offsets[9][2] = {{-1, -1}, {0, -1}, {1, -1},
			{-1, 0}, {0, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
		int32_t offset_index = 0;
		while (offset_index < 9)
		{
			WorldChunk *nearby_chunk = streamer.world_.find_chunk_mutable(
				streamer.world_.center_chunk_x + offsets[offset_index][0],
				streamer.world_.center_chunk_z + offsets[offset_index][1]);
			if (nearby_chunk != nullptr && nearby_chunk->initialized
				&& nearby_chunk->mesh_dirty
				&& nearby_chunk->pending_mesh_request_id == 0U)
				streamer.enqueue_background_remesh(nearby_chunk->chunk_x,
					nearby_chunk->chunk_z);
			offset_index += 1;
		}
	}
	if (streamer.priority_remesh_pending_)
	{
		const WorldChunkStreamer::RemeshPriority priority =
			streamer.priority_remeshes_.front();
		WorldChunk *priority_chunk = streamer.world_.find_chunk_mutable(
			priority.chunk_x, priority.chunk_z);
		if (priority_chunk == nullptr || !priority_chunk->mesh_dirty)
		{
			streamer.priority_remeshes_.pop_front();
			streamer.priority_remesh_pending_ =
				!streamer.priority_remeshes_.empty();
		}
		else
		{
			error_code = streamer.queue_chunk_remesh(*priority_chunk);
		#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
			if (error_code != FT_ERR_SUCCESS
				&& (error_code != FT_ERR_FULL
					|| streamer.stream_frame_ % 32U == 0U))
				std::fprintf(stderr,
					"[WorldGen] priority remesh deferred chunk=(%d,%d) "
					"error=%d in_flight=%zu queued=%zu\n",
					priority.chunk_x, priority.chunk_z, error_code,
					streamer.generation_pipeline_.remesh_in_flight_count(),
					streamer.generation_pipeline_.queued_count());
		#endif
			if (error_code == FT_ERR_SUCCESS
				&& priority_chunk->pending_mesh_request_id != 0U)
			{
				streamer.priority_remeshes_.pop_front();
				streamer.priority_remesh_pending_ =
					!streamer.priority_remeshes_.empty();
				if (streamer.priority_remesh_pending_)
				{
					streamer.priority_remesh_chunk_x_ =
						streamer.priority_remeshes_.front().chunk_x;
					streamer.priority_remesh_chunk_z_ =
						streamer.priority_remeshes_.front().chunk_z;
				}
				streamer.next_remesh_submission_frame_ = streamer.stream_frame_ + 2U;
			}
			else if (error_code != FT_ERR_FULL)
				return (error_code);
		}
	}
	dirty_index = streamer.dirty_remesh_cursor_;
	scanned_count = 0;
	best_dirty_index = -1;
	best_dirty_chunk = nullptr;
	priority_center_x = streamer.world_.center_chunk_x;
	priority_center_z = streamer.world_.center_chunk_z;
	if (streamer.remesh_priority_anchor_valid_)
	{
		priority_center_x = streamer.remesh_priority_anchor_x_;
		priority_center_z = streamer.remesh_priority_anchor_z_;
	}
	while (scanned_count < scan_budget
		&& scanned_count < streamer.world_.chunk_count)
	{
		if (streamer.world_.chunks[dirty_index].initialized
			&& streamer.world_.chunks[dirty_index].mesh_dirty
			&& streamer.world_.chunks[dirty_index].pending_mesh_request_id == 0U
			&& remesh_candidate_is_better(streamer.world_.chunks[dirty_index],
				best_dirty_chunk, priority_center_x, priority_center_z))
		{
			best_dirty_chunk = &streamer.world_.chunks[dirty_index];
			best_dirty_index = dirty_index;
		}
		dirty_index = (dirty_index + 1) % streamer.world_.chunk_count;
		scanned_count += 1;
	}
	streamer.dirty_remesh_cursor_ = dirty_index;
	if (best_dirty_index >= 0
		&& streamer.generation_pipeline_.remesh_in_flight_count()
			< static_cast<std::size_t>(queue_budget))
	{
		error_code = streamer.queue_chunk_remesh(
			*best_dirty_chunk);
		if (error_code != FT_ERR_SUCCESS && error_code != FT_ERR_FULL)
			return (error_code);
		if (error_code == FT_ERR_SUCCESS
			&& best_dirty_chunk->pending_mesh_request_id != 0U)
			streamer.next_remesh_submission_frame_ = streamer.stream_frame_ + 2U;
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkAsyncSubmitter::stream_chunks_async(WorldChunkStreamer &streamer,
	int32_t stream_radius, int32_t budget, int32_t *generated) noexcept
{
	int32_t error_code;
	int32_t submitted;
	int32_t scanned;
	int32_t candidate_count;
	bool interactive_pending;

	(void)generated;
	(void)stream_radius;
	/* Dirty edit/remesh work has priority over streaming new chunks.  This
	 * submits the edited geometry and its bounded lighting work before the
	 * candidate scanner can consume the frame's worker capacity. */
	error_code = WorldChunkAsyncSubmitter::submit_dirty_remeshes(streamer);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	/* A priority request may still be in immutable snapshot capture even after
	 * this pass has submitted it. Do not let a new generation request overtake
	 * that edit/light work before it reaches the worker pipeline. */
	interactive_pending = false;
	for (const WorldChunkStreamer::RemeshPriority &priority
		: streamer.priority_remeshes_)
	{
		if (priority.interactive)
		{
			interactive_pending = true;
			break ;
		}
	}
	if (!interactive_pending)
	{
		std::lock_guard<std::mutex> lock(streamer.remesh_capture_mutex_);
		for (const WorldChunkStreamer::RemeshCaptureTask &task
			: streamer.remesh_capture_tasks_)
		{
			if (task.interactive != FT_FALSE)
			{
				interactive_pending = true;
				break ;
			}
		}
	}
	if (interactive_pending)
		return (FT_ERR_SUCCESS);
	submitted = 0;
	scanned = 0;
	candidate_count = static_cast<int32_t>(streamer.stream_candidates_.size());
	while (scanned < candidate_count)
	{
		WorldChunkStreamer::StreamCandidate &candidate = streamer.stream_candidates_[streamer.stream_candidate_cursor_];

		streamer.stream_candidate_cursor_ = (streamer.stream_candidate_cursor_
				+ 1U) % streamer.stream_candidates_.size();
		scanned += 1;
		if (WorldChunkAsyncSubmitter::process_async_candidate(streamer,
				candidate, &submitted, budget))
			break ;
	}
	return (FT_ERR_SUCCESS);
}
