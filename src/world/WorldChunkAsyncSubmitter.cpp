#include "../../src/world/WorldChunkAsyncSubmitter.hpp"

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
	const int32_t scan_budget = 16;

	if (streamer.world_.chunk_count <= 0)
		return (FT_ERR_SUCCESS);
	dirty_index = streamer.dirty_remesh_cursor_;
	scanned_count = 0;
	while (scanned_count < scan_budget
		&& scanned_count < streamer.world_.chunk_count
		&& streamer.generation_pipeline_.queued_count() < 8U)
	{
		if (streamer.world_.chunks[dirty_index].initialized
			&& streamer.world_.chunks[dirty_index].mesh_dirty)
		{
			error_code = streamer.queue_chunk_remesh(
				streamer.world_.chunks[dirty_index]);
			if (error_code != FT_ERR_SUCCESS && error_code != FT_ERR_FULL)
				return (error_code);
		}
		dirty_index = (dirty_index + 1) % streamer.world_.chunk_count;
		scanned_count += 1;
	}
	streamer.dirty_remesh_cursor_ = dirty_index;
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkAsyncSubmitter::stream_chunks_async(WorldChunkStreamer &streamer,
	int32_t stream_radius, int32_t budget, int32_t *generated) noexcept
{
	int32_t submitted;
	int32_t scanned;
	int32_t candidate_count;

	(void)generated;
	(void)stream_radius;
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
	return (WorldChunkAsyncSubmitter::submit_dirty_remeshes(streamer));
}
