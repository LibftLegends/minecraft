#include "../../src/edits/WorldEditCommand.hpp"
#include "../../src/world/World.hpp"

WorldEditCommand::WorldEditCommand()
{
}
WorldEditCommand::WorldEditCommand(const WorldEditCommand &other)
{
	(void)other;
}
WorldEditCommand::~WorldEditCommand()
{
}
WorldEditCommand &WorldEditCommand::operator=(const WorldEditCommand &other)
{
	(void)other;
	return (*this);
}

WorldChunk *WorldEditCommand::resolve_chunk(World &world, int32_t world_x,
	int32_t world_y, int32_t world_z, int32_t &chunk_x, int32_t &chunk_z,
	int32_t &local_x, int32_t &local_z)
{
	WorldChunk	*wc;

	if (world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT)
		return (nullptr);
	chunk_x = WorldCoordinates::floor_divide(world_x, GAME_VOXEL_CHUNK_WIDTH);
	chunk_z = WorldCoordinates::floor_divide(world_z, GAME_VOXEL_CHUNK_DEPTH);
	wc = world.find_chunk_mutable(chunk_x, chunk_z);
	if (!wc)
		return (nullptr);
	local_x = WorldCoordinates::positive_modulo(world_x,
			GAME_VOXEL_CHUNK_WIDTH);
	local_z = WorldCoordinates::positive_modulo(world_z,
			GAME_VOXEL_CHUNK_DEPTH);
	return (wc);
}
