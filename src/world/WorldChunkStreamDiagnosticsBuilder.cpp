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
	diagnostics.oldest_pending_age = 0U;
	diagnostics.last_error = streamer.stream_last_error_;
	for (const WorldChunkStreamer::StreamCandidate &candidate : streamer.stream_candidates_)
		WorldChunkStreamDiagnosticsBuilder::accumulate_candidate(candidate,
			streamer.stream_frame_, diagnostics);
	return (diagnostics);
}
