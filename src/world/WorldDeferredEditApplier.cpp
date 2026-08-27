#include "../../src/world/WorldDeferredEditApplier.hpp"

WorldDeferredEditApplier::WorldDeferredEditApplier()
{
}

WorldDeferredEditApplier::WorldDeferredEditApplier(const WorldDeferredEditApplier &other)
{
	(void)other;
}

WorldDeferredEditApplier::~WorldDeferredEditApplier()
{
}

WorldDeferredEditApplier &WorldDeferredEditApplier::operator=(const WorldDeferredEditApplier &other)
{
	(void)other;
	return (*this);
}

bool WorldDeferredEditApplier::deferred_edit_less(const WorldGenerationPipeline::WorldDeferredBlockEdit &left,
	const WorldGenerationPipeline::WorldDeferredBlockEdit &right) noexcept
{
	if (left.world_x != right.world_x)
		return (left.world_x < right.world_x);
	if (left.world_y != right.world_y)
		return (left.world_y < right.world_y);
	if (left.world_z != right.world_z)
		return (left.world_z < right.world_z);
	if (left.request_id != right.request_id)
		return (left.request_id < right.request_id);
	return (left.sequence < right.sequence);
}

int32_t WorldDeferredEditApplier::apply_single_edit(WorldChunkStreamer &streamer,
	World &world, const WorldGenerationPipeline::WorldDeferredBlockEdit &edit,
	std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> &pending) noexcept
{
	int32_t chunk_x;
	int32_t chunk_z;
	int32_t local_x;
	int32_t local_z;
	WorldChunk *chunk;

	chunk_x = WorldCoordinates::floor_divide(edit.world_x,
			GAME_VOXEL_CHUNK_WIDTH);
	chunk_z = WorldCoordinates::floor_divide(edit.world_z,
			GAME_VOXEL_CHUNK_DEPTH);
	chunk = world.find_chunk_mutable(chunk_x, chunk_z);
	if (chunk == nullptr || !chunk->initialized)
	{
		pending.push_back(edit);
		return (FT_ERR_SUCCESS);
	}
	local_x = WorldCoordinates::positive_modulo(edit.world_x,
			GAME_VOXEL_CHUNK_WIDTH);
	local_z = WorldCoordinates::positive_modulo(edit.world_z,
			GAME_VOXEL_CHUNK_DEPTH);
	if (chunk->chunk.write_generated_block(local_x, edit.world_y, local_z,
			edit.block_id) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_OPERATION);
	chunk->voxel_revision += 1U;
	chunk->mesh_dirty = true;
	(void)streamer.queue_chunk_remesh(*chunk);
	return (FT_ERR_SUCCESS);
}

int32_t WorldDeferredEditApplier::apply(WorldChunkStreamer &streamer,
	World &world) noexcept
{
	std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> pending;
	int32_t error_code;

	std::sort(streamer.deferred_edits_.begin(), streamer.deferred_edits_.end(),
		&WorldDeferredEditApplier::deferred_edit_less);
	for (const WorldGenerationPipeline::WorldDeferredBlockEdit &edit : streamer.deferred_edits_)
	{
		error_code = WorldDeferredEditApplier::apply_single_edit(streamer,
				world, edit, pending);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
	}
	streamer.deferred_edits_.swap(pending);
	return (FT_ERR_SUCCESS);
}
