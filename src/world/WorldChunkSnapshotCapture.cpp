#include "../../src/world/WorldChunkSnapshotCapture.hpp"
#include <vector>

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
	if (source == nullptr || !source->initialized)
		return (FT_ERR_SUCCESS);
	if (border_local_x < 0 || border_local_x >= GAME_VOXEL_CHUNK_WIDTH)
		return (source->chunk.copy_x_border(&border[0],
			static_cast<uint32_t>(border.size()), border_local_x < 0
				? GAME_VOXEL_CHUNK_WIDTH - 1 : 0));
	return (source->chunk.copy_z_border(&border[0],
		static_cast<uint32_t>(border.size()), border_local_z < 0
			? GAME_VOXEL_CHUNK_DEPTH - 1 : 0));
}

int32_t WorldChunkSnapshotCapture::capture_blocks(const WorldChunk &target,
	WorldGenerationPipeline::WorldChunkSnapshot &snapshot) noexcept
{
	return (target.chunk.copy_blocks(&snapshot.blocks[0],
			static_cast<uint32_t>(snapshot.blocks.size())));
}

int32_t WorldChunkSnapshotCapture::capture_lighting_halo(
	const WorldChunk &target, const WorldChunk *west, const WorldChunk *east,
	const WorldChunk *north, const WorldChunk *south,
	const WorldChunk *northwest, const WorldChunk *northeast,
	const WorldChunk *southwest, const WorldChunk *southeast,
	WorldGenerationPipeline::WorldChunkSnapshot &snapshot) noexcept
{
	const WorldChunk *sources[9] = {&target, west, east, north, south,
		northwest, northeast, southwest, southeast};
	const int32_t halo = WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO;
	const int32_t edge = GAME_VOXEL_CHUNK_WIDTH + halo * 2;
	struct SourceRegion
	{
		uint32_t first_x;
		uint32_t first_z;
		uint32_t width;
		uint32_t depth;
		std::vector<uint32_t> blocks;
	};
	SourceRegion source_regions[9];
	int32_t halo_z;
	int32_t halo_x;
	int32_t local_x;
	int32_t local_z;
	const WorldChunk *source;
	int32_t x_side;
	int32_t z_side;
	uint32_t block_id;
	std::size_t index;
	std::size_t source_index = 0U;
	std::size_t source_block_index;
	int32_t source_slot;

	try
	{
		for (source_slot = 1; source_slot < 9; ++source_slot)
		{
			source_regions[source_slot].first_x = 0U;
			source_regions[source_slot].first_z = 0U;
			source_regions[source_slot].width = GAME_VOXEL_CHUNK_WIDTH;
			source_regions[source_slot].depth = GAME_VOXEL_CHUNK_DEPTH;
		}
		source_regions[1].first_x = GAME_VOXEL_CHUNK_WIDTH - halo;
		source_regions[1].width = halo;
		source_regions[2].width = halo;
		source_regions[3].first_z = GAME_VOXEL_CHUNK_DEPTH - halo;
		source_regions[3].depth = halo;
		source_regions[4].depth = halo;
		source_regions[5].first_x = GAME_VOXEL_CHUNK_WIDTH - halo;
		source_regions[5].first_z = GAME_VOXEL_CHUNK_DEPTH - halo;
		source_regions[5].width = halo;
		source_regions[5].depth = halo;
		source_regions[6].first_z = GAME_VOXEL_CHUNK_DEPTH - halo;
		source_regions[6].width = halo;
		source_regions[6].depth = halo;
		source_regions[7].first_x = GAME_VOXEL_CHUNK_WIDTH - halo;
		source_regions[7].width = halo;
		source_regions[7].depth = halo;
		source_regions[8].width = halo;
		source_regions[8].depth = halo;
		for (source_slot = 1; source_slot < 9; ++source_slot)
		{
			if (sources[source_slot] != nullptr
				&& sources[source_slot]->initialized)
			{
				source_regions[source_slot].blocks.resize(
					static_cast<std::size_t>(source_regions[source_slot].width)
					* static_cast<std::size_t>(source_regions[source_slot].depth)
					* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT));
				if (sources[source_slot]->chunk.copy_region(
						&source_regions[source_slot].blocks[0],
						static_cast<uint32_t>(source_regions[source_slot].blocks.size()),
						source_regions[source_slot].first_x,
						source_regions[source_slot].first_z,
						source_regions[source_slot].width,
						source_regions[source_slot].depth) != FT_ERR_SUCCESS)
					return (FT_ERR_INVALID_OPERATION);
			}
		}
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}

	halo_z = 0;
	while (halo_z < edge)
	{
		halo_x = 0;
		while (halo_x < edge)
		{
			local_x = halo_x - halo;
			local_z = halo_z - halo;
			source_index = 0U;
			x_side = local_x < 0 ? -1 : (local_x >= GAME_VOXEL_CHUNK_WIDTH ? 1 : 0);
			z_side = local_z < 0 ? -1 : (local_z >= GAME_VOXEL_CHUNK_DEPTH ? 1 : 0);
			if (x_side < 0 && z_side < 0)
				source_index = 5U;
			else if (x_side > 0 && z_side < 0)
				source_index = 6U;
			else if (x_side < 0 && z_side > 0)
				source_index = 7U;
			else if (x_side > 0 && z_side > 0)
				source_index = 8U;
			else if (x_side < 0)
				source_index = 1U;
			else if (x_side > 0)
				source_index = 2U;
			else if (z_side < 0)
				source_index = 3U;
			else if (z_side > 0)
				source_index = 4U;
			source = sources[source_index];
			if (source == nullptr || source->initialized == false)
				block_id = VOXEL_GENERATOR_STONE_BLOCK;
			else
			{
				if (x_side < 0)
					local_x += GAME_VOXEL_CHUNK_WIDTH;
				else if (x_side > 0)
					local_x -= GAME_VOXEL_CHUNK_WIDTH;
				if (z_side < 0)
					local_z += GAME_VOXEL_CHUNK_DEPTH;
				else if (z_side > 0)
					local_z -= GAME_VOXEL_CHUNK_DEPTH;
				if (source_index != 0U
					&& source_regions[source_index].blocks.empty())
					 return (FT_ERR_INVALID_OPERATION);
			}
			index = (static_cast<std::size_t>(halo_z)
				* static_cast<std::size_t>(edge)
				+ static_cast<std::size_t>(halo_x))
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT);
			for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
			{
				if (source == nullptr || source->initialized == false)
					block_id = VOXEL_GENERATOR_STONE_BLOCK;
				else
				{
					if (source_index == 0U)
					{
						source_block_index = (
							static_cast<std::size_t>(local_z)
							* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
							+ static_cast<std::size_t>(y))
							* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
							+ static_cast<std::size_t>(local_x);
						block_id = snapshot.blocks[source_block_index];
					}
					else
					{
						source_block_index = (
							static_cast<std::size_t>(local_z
								- static_cast<int32_t>(source_regions[source_index].first_z))
							* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
							+ static_cast<std::size_t>(y))
							* static_cast<std::size_t>(source_regions[source_index].width)
							+ static_cast<std::size_t>(local_x
								- static_cast<int32_t>(source_regions[source_index].first_x));
						block_id = source_regions[source_index].blocks[
							source_block_index];
					}
				}
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
	const WorldChunk *south, const WorldChunk *northwest,
	const WorldChunk *northeast, const WorldChunk *southwest,
	const WorldChunk *southeast,
	WorldGenerationPipeline::WorldChunkSnapshot &snapshot) noexcept
{
	snapshot.chunk_x = target.chunk_x;
	snapshot.chunk_z = target.chunk_z;
	snapshot.generation_metadata = target.chunk.get_generation_metadata();
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
		north, south, northwest, northeast, southwest, southeast,
		snapshot) != FT_ERR_SUCCESS)
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
