#include "../../src/queries/WorldBlockQuery.hpp"

WorldBlockQuery::WorldBlockQuery()
{
}

WorldBlockQuery::WorldBlockQuery(const WorldBlockQuery &other)
{
	*this = other;
}

WorldBlockQuery::~WorldBlockQuery()
{
}

WorldBlockQuery &WorldBlockQuery::operator=(const WorldBlockQuery &other)
{
	(void)other;
	return (*this);
}

bool WorldBlockQuery::find_chunk_local(const World &world, int32_t wx,
	int32_t wz, const WorldChunk **chunk, int32_t &lx, int32_t &lz)
{
	int32_t	chunk_x;
	int32_t	chunk_z;

	chunk_x = WorldCoordinates::floor_divide(wx, GAME_VOXEL_CHUNK_WIDTH);
	chunk_z = WorldCoordinates::floor_divide(wz, GAME_VOXEL_CHUNK_DEPTH);
	*chunk = world.find_chunk(chunk_x, chunk_z);
	if (*chunk == nullptr)
		return (false);
	lx = WorldCoordinates::positive_modulo(wx, GAME_VOXEL_CHUNK_WIDTH);
	lz = WorldCoordinates::positive_modulo(wz, GAME_VOXEL_CHUNK_DEPTH);
	return (true);
}

bool WorldBlockQuery::surface_top_at(const World &world, int32_t world_x,
	int32_t world_z, double *surface_top)
{
	const WorldChunk	*world_chunk;
	uint32_t			block_id;
	int32_t				local_x;
	int32_t				local_z;

	if (surface_top == nullptr)
		return (false);
	if (!find_chunk_local(world, world_x, world_z, &world_chunk, local_x,
			local_z))
		return (false);
	for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1; y >= 0; y--)
	{
		if (world_chunk->chunk.read_block(local_x, y, local_z,
				&block_id) == FT_ERR_SUCCESS && block_id != GAME_VOXEL_AIR_BLOCK
			&& voxel_block_is_solid(block_id) == FT_TRUE)
		{
			*surface_top = static_cast<double>(y + 1);
			return (true);
		}
	}
	return (false);
}

bool WorldBlockQuery::solid_block_at(const World &world, int32_t world_x,
	int32_t world_y, int32_t world_z)
{
	const WorldChunk	*world_chunk;
	uint32_t			block_id;
	int32_t				local_x;
	int32_t				local_z;

	if (world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT)
		return (false);
	if (!find_chunk_local(world, world_x, world_z, &world_chunk, local_x,
			local_z))
		return (false);
	if (world_chunk->chunk.read_block(local_x, world_y, local_z,
			&block_id) != FT_ERR_SUCCESS)
		return (false);
	return (block_id != GAME_VOXEL_AIR_BLOCK
		&& voxel_block_is_solid(block_id) == FT_TRUE);
}

bool WorldBlockQuery::block_id_at(const World &world, int32_t world_x,
	int32_t world_y, int32_t world_z, uint32_t *block_id)
{
	const WorldChunk	*world_chunk;
	int32_t				local_x;
	int32_t				local_z;

	if (block_id == nullptr || world_y < 0
		|| world_y >= GAME_VOXEL_CHUNK_HEIGHT)
		return (false);
	if (!find_chunk_local(world, world_x, world_z, &world_chunk, local_x,
			local_z))
		return (false);
	return (world_chunk->chunk.read_block(local_x, world_y, local_z,
			block_id) == FT_ERR_SUCCESS);
}
