#include "../../src/world/WorldChunkStreamDiagnosticsBuilder.hpp"

WorldChunkStreamDiagnosticsBuilder::WorldChunkStreamDiagnosticsBuilder()
{
}

WorldChunkStreamDiagnosticsBuilder::WorldChunkStreamDiagnosticsBuilder(const WorldChunkStreamDiagnosticsBuilder &other)
{
	(void)other;
}

WorldChunkStreamDiagnosticsBuilder::~WorldChunkStreamDiagnosticsBuilder()
{
}

WorldChunkStreamDiagnosticsBuilder &WorldChunkStreamDiagnosticsBuilder::operator=(const WorldChunkStreamDiagnosticsBuilder &other)
{
	(void)other;
	return (*this);
}

void WorldChunkStreamDiagnosticsBuilder::accumulate_candidate(const WorldChunkStreamer::StreamCandidate &candidate,
	uint64_t current_frame,
	WorldChunkStreamer::Diagnostics &diagnostics) noexcept
{
	uint64_t age;

	if (candidate.state == WorldChunkStreamer::CANDIDATE_READY)
		diagnostics.ready_count++;
	else if (candidate.state == WorldChunkStreamer::CANDIDATE_FAILED_RETRYABLE)
	{
		diagnostics.retryable_count++;
		diagnostics.pending_count++;
	}
	else if (candidate.state != WorldChunkStreamer::CANDIDATE_ABSENT)
		diagnostics.pending_count++;
	if (candidate.queued_frame != 0U && current_frame >= candidate.queued_frame)
	{
		age = current_frame - candidate.queued_frame;
		if (age > diagnostics.oldest_pending_age)
			diagnostics.oldest_pending_age = age;
	}
	if (candidate.last_error != FT_ERR_SUCCESS
		&& candidate.state == WorldChunkStreamer::CANDIDATE_FAILED_RETRYABLE)
		diagnostics.failed_count++;
}

WorldChunkStreamer::Diagnostics WorldChunkStreamDiagnosticsBuilder::build(const WorldChunkStreamer &streamer) noexcept
{
	WorldChunkStreamer::Diagnostics diagnostics;

	diagnostics.frame = streamer.stream_frame_;
	diagnostics.progress_frame = streamer.stream_progress_frame_;
	diagnostics.candidate_count = streamer.stream_candidates_.size();
	diagnostics.ready_count = 0U;
	diagnostics.pending_count = 0U;
	diagnostics.retryable_count = 0U;
	diagnostics.failed_count = 0U;
	diagnostics.playable_failed_count = 0U;
	diagnostics.playable_required_count = 0U;
	diagnostics.playable_drawable_count = 0U;
	diagnostics.active_generation_count = streamer.generation_pipeline_.active_count();
	diagnostics.oldest_result_age_nanoseconds = streamer.generation_pipeline_
		.oldest_completed_result_age_nanoseconds();
	diagnostics.oldest_pending_age = 0U;
	diagnostics.deferred_edit_count = streamer.deferred_edits_.size();
	diagnostics.deferred_edit_cursor = streamer.deferred_apply_cursor_;
	diagnostics.last_error = streamer.stream_last_error_;
	const int32_t playable_radius = WorldCoordinates::render_distance_to_chunk_radius(
		WorldCoordinates::MIN_RENDER_DISTANCE);
	const int32_t playable_radius_sq = playable_radius * playable_radius;
	for (const WorldChunkStreamer::StreamCandidate &candidate : streamer.stream_candidates_)
	{
		WorldChunkStreamDiagnosticsBuilder::accumulate_candidate(candidate,
			streamer.stream_frame_, diagnostics);
		if (candidate.state == WorldChunkStreamer::CANDIDATE_FAILED_RETRYABLE
			&& candidate.last_error != FT_ERR_SUCCESS
			&& candidate.dist_sq <= playable_radius_sq)
			diagnostics.playable_failed_count++;
	}
	{
		const int32_t playable_check_radius =
			WorldCoordinates::render_distance_to_chunk_radius(
				WorldCoordinates::MIN_RENDER_DISTANCE);
		const int32_t playable_check_radius_sq = playable_check_radius
			* playable_check_radius;
		int32_t offset_z = -playable_check_radius;
		while (offset_z <= playable_check_radius)
		{
			int32_t offset_x = -playable_check_radius;
			while (offset_x <= playable_check_radius)
			{
				if (offset_x * offset_x + offset_z * offset_z
					<= playable_check_radius_sq)
				{
					const WorldChunk *chunk = streamer.world_.find_chunk(
						streamer.world_.center_chunk_x + offset_x,
						streamer.world_.center_chunk_z + offset_z);
					diagnostics.playable_required_count += 1U;
					if (chunk != nullptr
						&& WorldChunk::mesh_is_drawable(chunk->mesh))
						diagnostics.playable_drawable_count += 1U;
				}
				offset_x += 1;
			}
			offset_z += 1;
		}
	}
	return (diagnostics);
}
