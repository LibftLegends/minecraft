#include "../../src/world/WorldRegenerationResultApplier.hpp"

WorldRegenerationResultApplier::WorldRegenerationResultApplier()
{
}

WorldRegenerationResultApplier::WorldRegenerationResultApplier(const WorldRegenerationResultApplier &other)
{
	(void)other;
}

WorldRegenerationResultApplier::~WorldRegenerationResultApplier()
{
}

WorldRegenerationResultApplier &WorldRegenerationResultApplier::operator=(const WorldRegenerationResultApplier &other)
{
	(void)other;
	return (*this);
}

void WorldRegenerationResultApplier::apply_chunk(WorldChunkStreamer &streamer,
	World &world, WorldGenerationPipeline::Result &result) noexcept
{
	WorldChunk *chunk;

	chunk = world.find_chunk_mutable(result.chunk_x, result.chunk_z);
	if (chunk == nullptr || !chunk->initialized)
	{
		world.revision_manager.record_regeneration_skipped();
		return ;
	}
	chunk->destroy();
	if (chunk->chunk.move(result.chunk->chunk) != FT_ERR_SUCCESS
		|| chunk_mesh_initialize(chunk->mesh) != FT_ERR_SUCCESS
		|| WorldGenerationResultCommitter::move_mesh(chunk->mesh,
			result.chunk->mesh) != FT_ERR_SUCCESS)
	{
		chunk->destroy();
		world.revision_manager.record_regeneration_error(FT_ERR_NO_MEMORY);
		return ;
	}
	chunk->chunk_x = result.chunk_x;
	chunk->chunk_z = result.chunk_z;
	chunk->world_x = result.chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	chunk->world_z = result.chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	chunk->initialized = true;
	chunk->mesh_revision += 1U;
	chunk->voxel_revision += 1U;
	chunk->pending_mesh_request_id = 0U;
	chunk->mesh_dirty = true;
	world.revision_manager.record_regeneration_success();
	(void)streamer.queue_neighbor_remeshes(result.chunk_x, result.chunk_z);
	streamer.deferred_edits_.insert(streamer.deferred_edits_.end(),
		result.deferred_edits.begin(), result.deferred_edits.end());
}

int32_t WorldRegenerationResultApplier::commit(WorldChunkStreamer &streamer,
	World &world, WorldGenerationPipeline::Result &result) noexcept
{
	if (!world.revision_manager.is_regenerating_for(result.relevance_epoch))
		return (FT_ERR_SUCCESS);
	world.revision_manager.record_regeneration_completed();
	if (result.error_code != FT_ERR_SUCCESS || result.chunk == nullptr)
		world.revision_manager.record_regeneration_error(result.error_code);
	else
		WorldRegenerationResultApplier::apply_chunk(streamer, world, result);
	if (world.revision_manager.all_regeneration_jobs_done())
		return (world.revision_manager.finish_regeneration());
	return (FT_ERR_SUCCESS);
}
