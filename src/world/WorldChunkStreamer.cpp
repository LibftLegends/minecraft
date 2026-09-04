#include "../../src/world/WorldChunkStreamer.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"
#include <cstdio>

const uint8_t WorldChunkStreamer::CANDIDATE_ABSENT = 0U;
const uint8_t WorldChunkStreamer::CANDIDATE_QUEUED = 1U;
const uint8_t WorldChunkStreamer::CANDIDATE_GENERATING = 2U;
const uint8_t WorldChunkStreamer::CANDIDATE_GENERATED = 3U;
const uint8_t WorldChunkStreamer::CANDIDATE_MESHING = 4U;
const uint8_t WorldChunkStreamer::CANDIDATE_READY = 5U;
const uint8_t WorldChunkStreamer::CANDIDATE_FAILED_RETRYABLE = 6U;

WorldChunkStreamer::WorldChunkStreamer(World &world) : world_(world)
{
	voxel_light_update_config_defaults(this->light_update_config_);
}

WorldChunkStreamer::WorldChunkStreamer(const WorldChunkStreamer &other)
	: world_(other.world_)
{
	(void)other;
}

WorldChunkStreamer::~WorldChunkStreamer()
{
}

WorldChunkStreamer &WorldChunkStreamer::operator=(const WorldChunkStreamer &other)
{
	(void)other;
	return (*this);
}

int32_t WorldChunkStreamer::initialize_pipeline() noexcept
{
	return (this->generation_pipeline_.initialize(0U, 0U));
}

void WorldChunkStreamer::reset() noexcept
{
	this->world_epoch_ += 1U;
	(void)this->generation_pipeline_.destroy();
	this->stream_candidates_radius_ = -1;
	this->stream_candidate_cursor_ = 0U;
	this->stream_candidate_lookup_.clear();
	this->dirty_remesh_cursor_ = 0;
	this->generation_credit_ = 0;
	this->stream_last_error_ = FT_ERR_SUCCESS;
	this->stream_retryable_count_ = 0;
	this->deferred_edits_.clear();
	this->deferred_edits_sorted_ = false;
	this->deferred_apply_cursor_ = 0U;
	this->deferred_sorted_end_ = 0U;
	this->deferred_pending_edits_.clear();
	this->deferred_touched_chunks_.clear();
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
	this->generation_pipeline_.cancel_queued();
	this->stream_relevance_epoch_ += 1U;
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

void WorldChunkStreamer::bump_generation_revision() noexcept
{
	this->generation_revision_ += 1U;
}

WorldGenerationPipeline &WorldChunkStreamer::pipeline() noexcept
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
	WorldGenerationPipeline::WorldChunkSnapshot snapshot;
	int32_t error_code;
	uint64_t request_id;

	if (!chunk.initialized || !chunk.mesh_dirty
		|| chunk.pending_mesh_request_id != 0U)
		return (FT_ERR_SUCCESS);
	if (this->generation_pipeline_.remesh_in_flight_count() >= 1U)
		return (FT_ERR_FULL);
	if (this->generation_pipeline_.queued_count() >= 8U)
		return (FT_ERR_FULL);
	error_code = this->generation_pipeline_.capture_snapshot(chunk,
			this->world_.find_chunk(chunk.chunk_x - 1, chunk.chunk_z),
			this->world_.find_chunk(chunk.chunk_x + 1, chunk.chunk_z),
			this->world_.find_chunk(chunk.chunk_x, chunk.chunk_z - 1),
			this->world_.find_chunk(chunk.chunk_x, chunk.chunk_z + 1),
			snapshot);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	request_id = this->next_request_id_++;
	error_code = this->generation_pipeline_.submit_remesh(request_id,
			this->world_epoch_, this->stream_relevance_epoch_,
			this->generation_revision_, chunk.chunk_x, chunk.chunk_z,
			chunk.voxel_revision, snapshot);
	if (error_code == FT_ERR_SUCCESS)
		chunk.pending_mesh_request_id = request_id;
	return (error_code);
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
			chunk->mesh_dirty = true;
		index += 1;
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
	int32_t error_code;

	index = 0;
	while (index < 5)
	{
		chunk = this->world_.find_chunk_mutable(coordinates[index][0],
				coordinates[index][1]);
		if (chunk != nullptr)
		{
			chunk->mesh_dirty = true;
			error_code = this->queue_chunk_remesh(*chunk);
			if (error_code != FT_ERR_SUCCESS && error_code != FT_ERR_FULL)
				return (error_code);
		}
		index += 1;
	}
	return (FT_ERR_SUCCESS);
}
