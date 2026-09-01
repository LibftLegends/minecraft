#include "../../src/world/WorldChunkCandidateScanner.hpp"

WorldChunkCandidateScanner::WorldChunkCandidateScanner()
{
}

WorldChunkCandidateScanner::WorldChunkCandidateScanner(const WorldChunkCandidateScanner &other)
{
	(void)other;
}

WorldChunkCandidateScanner::~WorldChunkCandidateScanner()
{
}

WorldChunkCandidateScanner &WorldChunkCandidateScanner::operator=(const WorldChunkCandidateScanner &other)
{
	(void)other;
	return (*this);
}

bool WorldChunkCandidateScanner::candidate_less(const WorldChunkStreamer::StreamCandidate &a,
	const WorldChunkStreamer::StreamCandidate &b) noexcept
{
	int32_t a_manhattan;
	int32_t b_manhattan;

	if (a.dist_sq != b.dist_sq)
		return (a.dist_sq < b.dist_sq);
	a_manhattan = std::abs(a.offset_x) + std::abs(a.offset_z);
	b_manhattan = std::abs(b.offset_x) + std::abs(b.offset_z);
	if (a_manhattan != b_manhattan)
		return (a_manhattan < b_manhattan);
	if (a.offset_z != b.offset_z)
		return (a.offset_z < b.offset_z);
	return (a.offset_x < b.offset_x);
}

void WorldChunkCandidateScanner::prepare_stream_candidates(WorldChunkStreamer &streamer,
	int32_t stream_radius) noexcept
{
	int32_t radius_sq;
	int32_t dist_sq;

	if (streamer.stream_candidates_radius_ == stream_radius)
		return ;
	streamer.stream_candidates_.clear();
	streamer.stream_candidate_cursor_ = 0U;
	streamer.stream_candidates_.reserve(static_cast<size_t>((stream_radius * 2
				+ 1) * (stream_radius * 2 + 1)));
	radius_sq = stream_radius * stream_radius;
	for (int32_t z = -stream_radius; z <= stream_radius; ++z)
	{
		for (int32_t x = -stream_radius; x <= stream_radius; ++x)
		{
			dist_sq = (x * x) + (z * z);
			if (dist_sq <= radius_sq)
				streamer.stream_candidates_.push_back({x, z, dist_sq,
					WorldChunkStreamer::CANDIDATE_ABSENT, 0U, 0, FT_ERR_SUCCESS,
					streamer.stream_relevance_epoch_,
					streamer.generation_revision_, 0U, 0U});
		}
	}
	std::sort(streamer.stream_candidates_.begin(),
		streamer.stream_candidates_.end(),
		&WorldChunkCandidateScanner::candidate_less);
	streamer.stream_candidates_radius_ = stream_radius;
	streamer.stream_retryable_count_ = 0;
	streamer.stream_relevance_epoch_ += 1U;
	streamer.generation_revision_ += 1U;
}

WorldChunkStreamer::StreamCandidate *WorldChunkCandidateScanner::find_stream_candidate(WorldChunkStreamer &streamer,
	int32_t chunk_x, int32_t chunk_z) noexcept
{
	for (WorldChunkStreamer::StreamCandidate &candidate : streamer.stream_candidates_)
	{
		if (streamer.world_.center_chunk_x + candidate.offset_x == chunk_x
			&& streamer.world_.center_chunk_z + candidate.offset_z == chunk_z)
			return (&candidate);
	}
	return (nullptr);
}

void WorldChunkCandidateScanner::remesh_loaded_neighbor(WorldChunkStreamer &streamer,
	int32_t chunk_x, int32_t chunk_z) noexcept
{
	if (streamer.world_.find_chunk(chunk_x, chunk_z) == nullptr)
		return ;
	(void)WorldChunkLoader::remesh_chunk(streamer.world_.chunks,
		streamer.world_.chunk_count, chunk_x, chunk_z, true);
}

int32_t WorldChunkCandidateScanner::try_load_chunk_at(WorldChunkStreamer &streamer,
	int32_t chunk_x, int32_t chunk_z) noexcept
{
	WorldChunk *slot;
	int32_t error_code;

	if (streamer.world_.find_chunk(chunk_x, chunk_z) != nullptr)
		return (0);
	slot = WorldChunkStore::find_free_chunk_slot(streamer.world_.chunks,
			streamer.world_.chunk_count);
	if (slot == nullptr)
		return (FT_ERR_NO_MEMORY);
	error_code = WorldChunkLoader::initialize_chunk(slot, chunk_x, chunk_z,
			streamer.world_.seed, streamer.world_.chunks,
			streamer.world_.chunk_count,
			streamer.world_.voxel_context.config());
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	streamer.world_.loaded_chunk_count = streamer.world_.loaded_chunk_count + 1;
	streamer.world_.register_chunk_index(*slot);
	WorldChunkCandidateScanner::remesh_loaded_neighbor(streamer, chunk_x - 1,
		chunk_z);
	WorldChunkCandidateScanner::remesh_loaded_neighbor(streamer, chunk_x + 1,
		chunk_z);
	WorldChunkCandidateScanner::remesh_loaded_neighbor(streamer, chunk_x,
		chunk_z - 1);
	WorldChunkCandidateScanner::remesh_loaded_neighbor(streamer, chunk_x,
		chunk_z + 1);
	return (1);
}

void WorldChunkCandidateScanner::refresh_stale_candidate(WorldChunkStreamer &streamer,
	WorldChunkStreamer::StreamCandidate &candidate) noexcept
{
	if (candidate.relevance_epoch == streamer.stream_relevance_epoch_
		&& candidate.generation_revision == streamer.generation_revision_)
		return ;
	candidate.state = WorldChunkStreamer::CANDIDATE_ABSENT;
	candidate.retry_count = 0U;
	candidate.retry_frames = 0;
	candidate.last_error = FT_ERR_SUCCESS;
	candidate.relevance_epoch = streamer.stream_relevance_epoch_;
	candidate.generation_revision = streamer.generation_revision_;
	candidate.queued_frame = 0U;
	candidate.request_id = 0U;
}

bool WorldChunkCandidateScanner::handle_sync_success(WorldChunkStreamer &streamer,
	WorldChunkStreamer::StreamCandidate &candidate, int32_t budget,
	int32_t *generated) noexcept
{
	candidate.state = WorldChunkStreamer::CANDIDATE_READY;
	candidate.retry_count = 0U;
	candidate.retry_frames = 0;
	candidate.last_error = FT_ERR_SUCCESS;
	candidate.queued_frame = 0U;
	streamer.stream_progress_frame_ = streamer.stream_frame_;
	*generated = *generated + 1;
	if (budget > 0 && *generated >= budget)
		return (true);
	return (false);
}

bool WorldChunkCandidateScanner::process_sync_candidate(WorldChunkStreamer &streamer,
	WorldChunkStreamer::StreamCandidate &candidate, int32_t budget,
	int32_t *generated) noexcept
{
	int32_t result;
	int32_t chunk_x;
	int32_t chunk_z;

	if (candidate.retry_frames > 0)
	{
		candidate.retry_frames -= 1;
		return (false);
	}
	if (candidate.state == WorldChunkStreamer::CANDIDATE_READY)
		return (false);
	WorldChunkCandidateScanner::refresh_stale_candidate(streamer, candidate);
	candidate.state = WorldChunkStreamer::CANDIDATE_QUEUED;
	if (candidate.queued_frame == 0U)
		candidate.queued_frame = streamer.stream_frame_;
	chunk_x = streamer.world_.center_chunk_x + candidate.offset_x;
	chunk_z = streamer.world_.center_chunk_z + candidate.offset_z;
	if (streamer.world_.find_chunk(chunk_x, chunk_z) != nullptr)
	{
		candidate.state = WorldChunkStreamer::CANDIDATE_READY;
		candidate.request_id = 0U;
		return (false);
	}
	candidate.state = WorldChunkStreamer::CANDIDATE_GENERATING;
	result = WorldChunkCandidateScanner::try_load_chunk_at(streamer, chunk_x,
			chunk_z);
	if (result < 0)
	{
		candidate.state = WorldChunkStreamer::CANDIDATE_FAILED_RETRYABLE;
		candidate.retry_count += 1U;
		candidate.last_error = result;
		candidate.retry_frames = 1 << std::min(candidate.retry_count, 6U);
		streamer.stream_last_error_ = result;
		streamer.stream_retryable_count_ += 1;
		return (false);
	}
	if (result > 0)
		return (WorldChunkCandidateScanner::handle_sync_success(streamer,
				candidate, budget, generated));
	return (false);
}

int32_t WorldChunkCandidateScanner::stream_chunks_sync(WorldChunkStreamer &streamer,
	int32_t stream_radius, int32_t budget, int32_t *generated) noexcept
{
	std::size_t candidate_count;
	std::size_t scanned_count;

	WorldChunkCandidateScanner::prepare_stream_candidates(streamer,
		stream_radius);
	candidate_count = streamer.stream_candidates_.size();
	scanned_count = 0U;
	while (scanned_count < candidate_count)
	{
		WorldChunkStreamer::StreamCandidate &candidate = streamer.stream_candidates_[streamer.stream_candidate_cursor_];

		streamer.stream_candidate_cursor_ = (streamer.stream_candidate_cursor_
				+ 1U) % candidate_count;
		scanned_count += 1U;
		if (WorldChunkCandidateScanner::process_sync_candidate(streamer,
				candidate, budget, generated))
			return (FT_ERR_SUCCESS);
	}
	return (FT_ERR_SUCCESS);
}
