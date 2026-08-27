#include "../../src/world/WorldGenerationResultCommitter.hpp"

WorldGenerationResultCommitter::WorldGenerationResultCommitter()
{
}

WorldGenerationResultCommitter::WorldGenerationResultCommitter(const WorldGenerationResultCommitter &other)
{
	(void)other;
}

WorldGenerationResultCommitter::~WorldGenerationResultCommitter()
{
}

WorldGenerationResultCommitter &WorldGenerationResultCommitter::operator=(const WorldGenerationResultCommitter &other)
{
	(void)other;
	return (*this);
}

int32_t WorldGenerationResultCommitter::move_mesh(chunk_mesh &destination,
	chunk_mesh &source) noexcept
{
	int32_t error_code;

	error_code = destination.vertices.move(source.vertices);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = destination.indices.move(source.indices);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	destination.bounds = source.bounds;
	destination.occupied_bounds = source.occupied_bounds;
	destination.has_occupied_bounds = source.has_occupied_bounds;
	return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationResultCommitter::commit_remesh_result(World &world,
	WorldGenerationPipeline::Result &result) noexcept
{
	WorldChunk *chunk;

	chunk = world.find_chunk_mutable(result.chunk_x, result.chunk_z);
	if (chunk == nullptr || !chunk->initialized
		|| chunk->pending_mesh_request_id != result.request_id
		|| chunk->voxel_revision != result.voxel_revision)
		return (FT_ERR_SUCCESS);
	chunk->pending_mesh_request_id = 0U;
	if (result.error_code != FT_ERR_SUCCESS || result.mesh == nullptr)
		return (FT_ERR_SUCCESS);
	(void)chunk_mesh_destroy(chunk->mesh);
	if (chunk_mesh_initialize(chunk->mesh) != FT_ERR_SUCCESS
		|| WorldGenerationResultCommitter::move_mesh(chunk->mesh,
			*result.mesh) != FT_ERR_SUCCESS)
		return (FT_ERR_NO_MEMORY);
	chunk->mesh_revision += 1U;
	chunk->mesh_dirty = false;
	return (FT_ERR_SUCCESS);
}

void WorldGenerationResultCommitter::populate_chunk_slot(WorldChunk &slot,
	const WorldGenerationPipeline::Result &result) noexcept
{
	slot.chunk_x = result.chunk_x;
	slot.chunk_z = result.chunk_z;
	slot.world_x = result.chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	slot.world_z = result.chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	slot.initialized = true;
	slot.mesh_revision = 1U;
	slot.voxel_revision = 1U;
	slot.pending_mesh_request_id = 0U;
	slot.mesh_dirty = true;
}

int32_t WorldGenerationResultCommitter::create_chunk_from_stream_result(WorldChunkStreamer &streamer,
	World &world, WorldGenerationPipeline::Result &result,
	WorldChunkStreamer::StreamCandidate &candidate) noexcept
{
	WorldChunk *slot;

	slot = WorldChunkStore::find_free_chunk_slot(world.chunks,
			world.chunk_count);
	if (slot == nullptr)
	{
		candidate.state = WorldChunkStreamer::CANDIDATE_FAILED_RETRYABLE;
		candidate.last_error = FT_ERR_NO_MEMORY;
		candidate.retry_frames = 1;
		return (FT_ERR_SUCCESS);
	}
	if (slot->chunk.move(result.chunk->chunk) != FT_ERR_SUCCESS
		|| chunk_mesh_initialize(slot->mesh) != FT_ERR_SUCCESS
		|| WorldGenerationResultCommitter::move_mesh(slot->mesh,
			result.chunk->mesh) != FT_ERR_SUCCESS)
	{
		slot->destroy();
		candidate.state = WorldChunkStreamer::CANDIDATE_FAILED_RETRYABLE;
		candidate.last_error = FT_ERR_NO_MEMORY;
		candidate.retry_frames = 1;
		return (FT_ERR_SUCCESS);
	}
	WorldGenerationResultCommitter::populate_chunk_slot(*slot, result);
	world.loaded_chunk_count += 1;
	world.register_chunk_index(*slot);
	candidate.state = WorldChunkStreamer::CANDIDATE_READY;
	candidate.retry_count = 0U;
	candidate.retry_frames = 0;
	candidate.last_error = FT_ERR_SUCCESS;
	candidate.queued_frame = 0U;
	streamer.stream_progress_frame_ = streamer.stream_frame_;
	streamer.deferred_edits_.insert(streamer.deferred_edits_.end(),
		result.deferred_edits.begin(), result.deferred_edits.end());
	(void)streamer.queue_neighbor_remeshes(result.chunk_x, result.chunk_z);
	return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationResultCommitter::commit_stream_result(WorldChunkStreamer &streamer,
	World &world, WorldGenerationPipeline::Result &result) noexcept
{
	WorldChunkStreamer::StreamCandidate *candidate;

	candidate = WorldChunkCandidateScanner::find_stream_candidate(streamer,
			result.chunk_x, result.chunk_z);
	if (candidate == nullptr || candidate->request_id != result.request_id
		|| candidate->relevance_epoch != result.relevance_epoch
		|| candidate->generation_revision != result.generation_revision)
		return (FT_ERR_SUCCESS);
	if (result.error_code != FT_ERR_SUCCESS || result.chunk == nullptr)
	{
		candidate->state = WorldChunkStreamer::CANDIDATE_FAILED_RETRYABLE;
		candidate->retry_count += 1U;
		candidate->last_error = result.error_code;
		candidate->retry_frames = 1 << std::min(candidate->retry_count, 6U);
		streamer.stream_last_error_ = result.error_code;
		streamer.stream_retryable_count_ += 1;
		return (FT_ERR_SUCCESS);
	}
	if (world.find_chunk(result.chunk_x, result.chunk_z) != nullptr)
	{
		candidate->state = WorldChunkStreamer::CANDIDATE_READY;
		return (FT_ERR_SUCCESS);
	}
	return (WorldGenerationResultCommitter::create_chunk_from_stream_result(streamer,
			world, result, *candidate));
}

int32_t WorldGenerationResultCommitter::commit(WorldChunkStreamer &streamer,
	World &world, WorldGenerationPipeline::Result &result) noexcept
{
	if (result.operation == WorldGenerationPipeline::WorldGenerationOperation::REGENERATE)
		return (WorldRegenerationResultApplier::commit(streamer, world,
				result));
	if (result.operation == WorldGenerationPipeline::WorldGenerationOperation::REMESH)
		return (WorldGenerationResultCommitter::commit_remesh_result(world,
				result));
	return (WorldGenerationResultCommitter::commit_stream_result(streamer,
			world, result));
}

int32_t WorldGenerationResultCommitter::drain(WorldChunkStreamer &streamer,
	World &world) noexcept
{
	std::unique_ptr<WorldGenerationPipeline::Result> result;
	int32_t processed;
	std::chrono::steady_clock::time_point deadline;
	int32_t error_code;

	processed = 0;
	deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
	while (processed < 2 && std::chrono::steady_clock::now() < deadline
		&& streamer.generation_pipeline_.poll(result) == FT_ERR_SUCCESS)
	{
		error_code = WorldGenerationResultCommitter::commit(streamer, world,
				*result);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		result.reset();
		processed += 1;
	}
	return (WorldDeferredEditApplier::apply(streamer, world));
}
