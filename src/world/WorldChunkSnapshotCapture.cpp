#include "../../src/world/WorldChunkSnapshotCapture.hpp"

WorldChunkSnapshotCapture::WorldChunkSnapshotCapture()
{
}

WorldChunkSnapshotCapture::WorldChunkSnapshotCapture(const WorldChunkSnapshotCapture &other)
{
	(void)other;
}

WorldChunkSnapshotCapture::~WorldChunkSnapshotCapture()
{
}

WorldChunkSnapshotCapture &WorldChunkSnapshotCapture::operator=(const WorldChunkSnapshotCapture &other)
{
	(void)other;
	return (*this);
}

int32_t WorldChunkSnapshotCapture::capture_border_column(const WorldChunk *source,
	std::vector<uint32_t> &border, int32_t border_local_x,
	int32_t border_local_z) noexcept
{
	int32_t read_x;
	int32_t read_z;
	uint32_t block_id;

	if (source == nullptr || !source->initialized)
		return (FT_ERR_SUCCESS);
	for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
	{
		for (int32_t axis = 0; axis < GAME_VOXEL_CHUNK_WIDTH; ++axis)
		{
			if (border_local_x < 0)
				read_x = GAME_VOXEL_CHUNK_WIDTH - 1;
			else if (border_local_x >= GAME_VOXEL_CHUNK_WIDTH)
				read_x = 0;
			else
				read_x = axis;
			if (border_local_z < 0)
				read_z = GAME_VOXEL_CHUNK_DEPTH - 1;
			else if (border_local_z >= GAME_VOXEL_CHUNK_DEPTH)
				read_z = 0;
			else
				read_z = axis;
			if (source->chunk.read_block(read_x, y, read_z,
					&block_id) != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_OPERATION);
			if (border_local_x < 0 || border_local_x >= GAME_VOXEL_CHUNK_WIDTH)
				border[static_cast<std::size_t>(y) * GAME_VOXEL_CHUNK_DEPTH
					+ static_cast<std::size_t>(axis)] = block_id;
			else
				border[static_cast<std::size_t>(y) * GAME_VOXEL_CHUNK_WIDTH
					+ static_cast<std::size_t>(axis)] = block_id;
		}
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkSnapshotCapture::capture_blocks(const WorldChunk &target,
	WorldGenerationPipeline::WorldChunkSnapshot &snapshot) noexcept
{
	uint32_t block_id;
	std::size_t index;

	for (int32_t local_z = 0; local_z < GAME_VOXEL_CHUNK_DEPTH; ++local_z)
	{
		for (int32_t local_y = 0; local_y < GAME_VOXEL_CHUNK_HEIGHT; ++local_y)
		{
			for (int32_t local_x = 0; local_x < GAME_VOXEL_CHUNK_WIDTH; ++local_x)
			{
				if (target.chunk.read_block(local_x, local_y, local_z,
						&block_id) != FT_ERR_SUCCESS)
					return (FT_ERR_INVALID_OPERATION);
				index = (static_cast<std::size_t>(local_z)
						* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
						+ static_cast<std::size_t>(local_y))
					* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
					+ static_cast<std::size_t>(local_x);
				snapshot.blocks[index] = block_id;
			}
		}
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkSnapshotCapture::capture_lighting_halo(
	const WorldChunk &target, const WorldChunk *west, const WorldChunk *east,
	const WorldChunk *north, const WorldChunk *south,
	WorldGenerationPipeline::WorldChunkSnapshot &snapshot) noexcept
{
	const int32_t halo = WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO;
	const int32_t edge = GAME_VOXEL_CHUNK_WIDTH + halo * 2;
	int32_t halo_z;
	int32_t halo_x;
	int32_t local_x;
	int32_t local_z;
	const WorldChunk *source;
	uint32_t block_id;
	std::size_t index;

	halo_z = 0;
	while (halo_z < edge)
	{
		halo_x = 0;
		while (halo_x < edge)
		{
			local_x = halo_x - halo;
			local_z = halo_z - halo;
			source = &target;
			if (local_x < 0 && local_z >= 0 && local_z < GAME_VOXEL_CHUNK_DEPTH)
				source = west;
			else if (local_x >= GAME_VOXEL_CHUNK_WIDTH && local_z >= 0
				&& local_z < GAME_VOXEL_CHUNK_DEPTH)
				source = east;
			else if (local_z < 0 && local_x >= 0
				&& local_x < GAME_VOXEL_CHUNK_WIDTH)
				source = north;
			else if (local_z >= GAME_VOXEL_CHUNK_DEPTH && local_x >= 0
				&& local_x < GAME_VOXEL_CHUNK_WIDTH)
				source = south;
			else if (local_x < 0 || local_x >= GAME_VOXEL_CHUNK_WIDTH
				|| local_z < 0 || local_z >= GAME_VOXEL_CHUNK_DEPTH)
				source = nullptr;
			if (source == nullptr || source->initialized == false)
				block_id = VOXEL_GENERATOR_STONE_BLOCK;
			else
			{
				if (source == &target)
				{
					local_x = halo_x - halo;
					local_z = halo_z - halo;
				}
				else if (source == west)
					local_x = GAME_VOXEL_CHUNK_WIDTH + local_x;
				else if (source == east)
					local_x -= GAME_VOXEL_CHUNK_WIDTH;
				else if (source == north)
					local_z = GAME_VOXEL_CHUNK_DEPTH + local_z;
				else if (source == south)
					local_z -= GAME_VOXEL_CHUNK_DEPTH;
			}
			index = (static_cast<std::size_t>(halo_z)
				* static_cast<std::size_t>(edge)
				+ static_cast<std::size_t>(halo_x))
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT);
			for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
			{
				if (source == nullptr || source->initialized == false)
					block_id = VOXEL_GENERATOR_STONE_BLOCK;
				else if (source->chunk.read_block(local_x, y, local_z,
						&block_id) != FT_ERR_SUCCESS)
					return (FT_ERR_INVALID_OPERATION);
				snapshot.lighting_blocks[index + static_cast<std::size_t>(y)] = block_id;
			}
			halo_x += 1;
		}
		halo_z += 1;
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkSnapshotCapture::capture(const WorldChunk &target,
	const WorldChunk *west, const WorldChunk *east, const WorldChunk *north,
	const WorldChunk *south,
	WorldGenerationPipeline::WorldChunkSnapshot &snapshot) noexcept
{
	snapshot.chunk_x = target.chunk_x;
	snapshot.chunk_z = target.chunk_z;
	snapshot.blocks.clear();
	snapshot.west_border.clear();
	snapshot.east_border.clear();
	snapshot.north_border.clear();
	snapshot.south_border.clear();
	snapshot.lighting_blocks.clear();
	try
	{
		snapshot.blocks.resize(static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_DEPTH)
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT));
		snapshot.west_border.resize(static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_DEPTH),
			GAME_VOXEL_AIR_BLOCK);
		snapshot.east_border = snapshot.west_border;
		snapshot.north_border.resize(static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH),
			GAME_VOXEL_AIR_BLOCK);
		snapshot.south_border = snapshot.north_border;
		snapshot.lighting_blocks.resize(static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH
			+ WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO * 2)
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_DEPTH
				+ WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO * 2)
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT),
			GAME_VOXEL_AIR_BLOCK);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (WorldChunkSnapshotCapture::capture_blocks(target,
			snapshot) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_OPERATION);
	if (WorldChunkSnapshotCapture::capture_lighting_halo(target, west, east,
			north, south, snapshot) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_OPERATION);
	if (WorldChunkSnapshotCapture::capture_border_column(west,
			snapshot.west_border, -1, 0) != FT_ERR_SUCCESS
		|| WorldChunkSnapshotCapture::capture_border_column(east,
			snapshot.east_border, GAME_VOXEL_CHUNK_WIDTH, 0) != FT_ERR_SUCCESS
		|| WorldChunkSnapshotCapture::capture_border_column(north,
			snapshot.north_border, 0, -1) != FT_ERR_SUCCESS
		|| WorldChunkSnapshotCapture::capture_border_column(south,
			snapshot.south_border, 0, GAME_VOXEL_CHUNK_DEPTH) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_OPERATION);
	return (FT_ERR_SUCCESS);
}
