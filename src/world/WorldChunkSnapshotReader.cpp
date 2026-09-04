#include "../../src/world/WorldChunkSnapshotReader.hpp"

WorldChunkSnapshotReader::WorldChunkSnapshotReader()
{
}

WorldChunkSnapshotReader::WorldChunkSnapshotReader(const WorldChunkSnapshotReader &other)
{
	(void)other;
}

WorldChunkSnapshotReader::~WorldChunkSnapshotReader()
{
}

WorldChunkSnapshotReader &WorldChunkSnapshotReader::operator=(const WorldChunkSnapshotReader &other)
{
	(void)other;
	return (*this);
}

int32_t WorldChunkSnapshotReader::initialize_snapshot_chunk(game_voxel_chunk &chunk,
	const WorldGenerationPipeline::WorldChunkSnapshot &snapshot) noexcept
{
	int32_t error_code;
	std::size_t index;
	std::size_t plane;
	std::size_t local_z;
	std::size_t remainder;
	std::size_t local_y;
	std::size_t local_x;

	error_code = chunk.initialize();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	index = 0U;
	while (index < snapshot.blocks.size())
	{
		plane = static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT);
		local_z = index / plane;
		remainder = index % plane;
		local_y = remainder / GAME_VOXEL_CHUNK_WIDTH;
		local_x = remainder % GAME_VOXEL_CHUNK_WIDTH;
		error_code = chunk.write_generated_block(static_cast<int32_t>(local_x),
				static_cast<int32_t>(local_y), static_cast<int32_t>(local_z),
				snapshot.blocks[index]);
		if (error_code != FT_ERR_SUCCESS)
		{
			(void)chunk.destroy();
			return (error_code);
		}
		index += 1U;
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkSnapshotReader::lookup_snapshot_block(void *user_data,
	int32_t world_x, int32_t world_y, int32_t world_z,
	uint32_t *block_id) noexcept
{
	const WorldGenerationPipeline::WorldChunkSnapshot *snapshot;
	int32_t local_x;
	int32_t local_z;
	std::size_t index;
	const int32_t halo = WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO;
	const int32_t edge = GAME_VOXEL_CHUNK_WIDTH + halo * 2;

	if (user_data == nullptr || block_id == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	snapshot = static_cast<const WorldGenerationPipeline::WorldChunkSnapshot *>(user_data);
	if (world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT)
	{
		*block_id = GAME_VOXEL_AIR_BLOCK;
		return (FT_ERR_SUCCESS);
	}
	local_x = world_x - snapshot->chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	local_z = world_z - snapshot->chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	if (local_x >= 0 && local_x < GAME_VOXEL_CHUNK_WIDTH && local_z >= 0
		&& local_z < GAME_VOXEL_CHUNK_DEPTH)
	{
		index = (static_cast<std::size_t>(local_z)
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
				+ static_cast<std::size_t>(world_y))
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
			+ static_cast<std::size_t>(local_x);
		*block_id = snapshot->blocks[index];
		return (FT_ERR_SUCCESS);
	}
	if (local_x >= -halo && local_x < GAME_VOXEL_CHUNK_WIDTH + halo
		&& local_z >= -halo && local_z < GAME_VOXEL_CHUNK_DEPTH + halo)
	{
		if (snapshot->lighting_blocks.size() < static_cast<std::size_t>(edge)
			* static_cast<std::size_t>(edge)
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT))
			return (FT_ERR_INVALID_OPERATION);
		index = (static_cast<std::size_t>(local_z + halo)
				* static_cast<std::size_t>(edge)
				+ static_cast<std::size_t>(local_x + halo))
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
			+ static_cast<std::size_t>(world_y);
		*block_id = snapshot->lighting_blocks[index];
		return (FT_ERR_SUCCESS);
	}
	return (WorldChunkSnapshotReader::lookup_border_block(*snapshot, local_x,
			local_z, world_y, block_id));
}

int32_t WorldChunkSnapshotReader::lookup_border_block(const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
	int32_t local_x, int32_t local_z, int32_t world_y,
	uint32_t *block_id) noexcept
{
	if (local_x == -1 && local_z >= 0 && local_z < GAME_VOXEL_CHUNK_DEPTH)
		*block_id = snapshot.west_border[static_cast<std::size_t>(world_y)
			* GAME_VOXEL_CHUNK_DEPTH + static_cast<std::size_t>(local_z)];
	else if (local_x == GAME_VOXEL_CHUNK_WIDTH && local_z >= 0
		&& local_z < GAME_VOXEL_CHUNK_DEPTH)
		*block_id = snapshot.east_border[static_cast<std::size_t>(world_y)
			* GAME_VOXEL_CHUNK_DEPTH + static_cast<std::size_t>(local_z)];
	else if (local_z == -1 && local_x >= 0 && local_x < GAME_VOXEL_CHUNK_WIDTH)
		*block_id = snapshot.north_border[static_cast<std::size_t>(world_y)
			* GAME_VOXEL_CHUNK_WIDTH + static_cast<std::size_t>(local_x)];
	else if (local_z == GAME_VOXEL_CHUNK_DEPTH && local_x >= 0
		&& local_x < GAME_VOXEL_CHUNK_WIDTH)
		*block_id = snapshot.south_border[static_cast<std::size_t>(world_y)
			* GAME_VOXEL_CHUNK_WIDTH + static_cast<std::size_t>(local_x)];
	else
		*block_id = GAME_VOXEL_AIR_BLOCK;
	return (FT_ERR_SUCCESS);
}
