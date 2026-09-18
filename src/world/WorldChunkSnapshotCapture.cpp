#include "../../src/world/WorldChunkSnapshotCapture.hpp"
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
# include <cstdio>
#endif
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

int32_t WorldChunkSnapshotCapture::capture_read_states(
	const std::shared_ptr<const WorldChunkReadState> &target,
	const std::shared_ptr<const WorldChunkReadState> &west,
	const std::shared_ptr<const WorldChunkReadState> &east,
	const std::shared_ptr<const WorldChunkReadState> &north,
	const std::shared_ptr<const WorldChunkReadState> &south,
	const std::shared_ptr<const WorldChunkReadState> &northwest,
	const std::shared_ptr<const WorldChunkReadState> &northeast,
	const std::shared_ptr<const WorldChunkReadState> &southwest,
	const std::shared_ptr<const WorldChunkReadState> &southeast,
	WorldGenerationPipeline::WorldChunkSnapshot &snapshot) noexcept
{
	const std::shared_ptr<const WorldChunkReadState> sources[9] = {target,
		west, east, north, south, northwest, northeast, southwest, southeast};
	std::vector<uint32_t> source_blocks[9];
	const int32_t halo = WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO;
	const int32_t edge = GAME_VOXEL_CHUNK_WIDTH + halo * 2;
	std::size_t full_size = static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
		* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
		* static_cast<std::size_t>(GAME_VOXEL_CHUNK_DEPTH);
	uint32_t ring_columns = 0U;
	int32_t x;
	int32_t z;

	if (target == nullptr || target->initialized == FT_FALSE
		|| target->light.size() != full_size)
		return (FT_ERR_NOT_FOUND);
	for (std::size_t source_index = 0U; source_index < 9U; ++source_index)
	{
		if (sources[source_index] != nullptr
			&& sources[source_index]->initialized != FT_FALSE
			&& sources[source_index]->materialize_blocks(
				source_blocks[source_index]) != FT_ERR_SUCCESS)
			return (FT_ERR_NO_MEMORY);
	}
	if (source_blocks[0].size() != full_size)
		return (FT_ERR_INVALID_OPERATION);
	snapshot.chunk_x = target->chunk_x;
	snapshot.chunk_z = target->chunk_z;
	snapshot.generation_metadata = target->generation_metadata;
	snapshot.west_border_valid = west != nullptr
		&& west->initialized != FT_FALSE ? FT_TRUE : FT_FALSE;
	snapshot.east_border_valid = east != nullptr
		&& east->initialized != FT_FALSE ? FT_TRUE : FT_FALSE;
	snapshot.north_border_valid = north != nullptr
		&& north->initialized != FT_FALSE ? FT_TRUE : FT_FALSE;
	snapshot.south_border_valid = south != nullptr
		&& south->initialized != FT_FALSE ? FT_TRUE : FT_FALSE;
	snapshot.existing_light_valid = target->light_valid;
	snapshot.lighting_halo_blocked = FT_FALSE;
	snapshot.lighting_cardinal_halo_valid = FT_TRUE;
	snapshot.dependency_valid = FT_FALSE;
	snapshot.dependency_chunk_x = 0;
	snapshot.dependency_chunk_z = 0;
	snapshot.dependency_voxel_revision = 0U;
	snapshot.dependency_light_revision = 0U;
	snapshot.dependency_content_version = 0U;
	snapshot.dependency_light_input_version = 0U;
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (target->light_valid == FT_FALSE)
		std::fprintf(stderr,
			"[WorldGen] remesh capture target light invalid chunk=(%d,%d) "
			"content=%u light_input=%u light_version=%u computed=%u\n",
			target->chunk_x, target->chunk_z,
			target->content_version, target->light_input_version,
			target->light_version, target->computed_light_input_version);
	#endif
	snapshot.lighting_halo_valid = FT_TRUE;
	for (std::size_t source_index = 0U; source_index < 9U; ++source_index)
	{
		if (source_index != 0U && sources[source_index] != nullptr
			&& sources[source_index]->initialized != FT_FALSE
			&& (sources[source_index]->light_valid == FT_FALSE
				|| !world_light_version::matches(
					sources[source_index]->light_version,
					sources[source_index]->content_version)
				|| !world_light_version::matches(
					sources[source_index]->computed_light_input_version,
					sources[source_index]->light_input_version)))
			snapshot.lighting_halo_blocked = FT_TRUE;
		if (sources[source_index] == nullptr
			|| sources[source_index]->initialized == FT_FALSE
			|| sources[source_index]->light_valid == FT_FALSE
			|| (source_index != 0U
				&& (!world_light_version::matches(sources[source_index]->light_version,
					sources[source_index]->content_version)
					|| !world_light_version::matches(
						sources[source_index]->computed_light_input_version,
						sources[source_index]->light_input_version))))
			snapshot.lighting_halo_valid = FT_FALSE;
		if (source_index <= 4U
			&& (sources[source_index] == nullptr
				|| sources[source_index]->initialized == FT_FALSE
				|| sources[source_index]->light_valid == FT_FALSE
				|| (source_index != 0U
					&& (!world_light_version::matches(
						sources[source_index]->light_version,
						sources[source_index]->content_version)
						|| !world_light_version::matches(
							sources[source_index]->computed_light_input_version,
							sources[source_index]->light_input_version)))))
			snapshot.lighting_cardinal_halo_valid = FT_FALSE;
	}
	try
	{
		snapshot.blocks = source_blocks[0];
		snapshot.existing_light = target->light;
		snapshot.west_border.assign(static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
			* GAME_VOXEL_CHUNK_DEPTH, GAME_VOXEL_AIR_BLOCK);
		snapshot.east_border = snapshot.west_border;
		snapshot.north_border.assign(static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
			* GAME_VOXEL_CHUNK_WIDTH, GAME_VOXEL_AIR_BLOCK);
		snapshot.south_border = snapshot.north_border;
		snapshot.lighting_ring_offsets.assign(static_cast<std::size_t>(edge) * edge,
			UINT32_MAX);
		for (z = 0; z < edge; ++z)
			for (x = 0; x < edge; ++x)
				if (x < halo || x >= GAME_VOXEL_CHUNK_WIDTH + halo
					|| z < halo || z >= GAME_VOXEL_CHUNK_DEPTH + halo)
					snapshot.lighting_ring_offsets[static_cast<std::size_t>(z) * edge + x]
						= ring_columns++ * static_cast<uint32_t>(GAME_VOXEL_CHUNK_HEIGHT);
		/*
		 * Keep the same boundary convention as the live capture path:
		 * an unavailable neighbouring chunk is treated as solid stone for
		 * lighting.  This prevents an absent neighbour from becoming an
		 * artificial sunlight/propagation opening.  The immutable handoff
		 * must be observationally equivalent to capture(), including this
		 * fallback, even though the halo is marked invalid until the
		 * neighbour becomes available.
		 */
		snapshot.lighting_blocks.assign(static_cast<std::size_t>(ring_columns)
			* GAME_VOXEL_CHUNK_HEIGHT, VOXEL_GENERATOR_STONE_BLOCK);
		snapshot.lighting_existing_light.assign(snapshot.lighting_blocks.size(), 0U);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
	{
		for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
		{
			snapshot.west_border[static_cast<std::size_t>(y)
				* GAME_VOXEL_CHUNK_DEPTH + z] = west != nullptr
				&& west->initialized != FT_FALSE ? source_blocks[1U][
				(static_cast<std::size_t>(z) * GAME_VOXEL_CHUNK_HEIGHT + y)
				* GAME_VOXEL_CHUNK_WIDTH + GAME_VOXEL_CHUNK_WIDTH - 1]
				: GAME_VOXEL_AIR_BLOCK;
			snapshot.east_border[static_cast<std::size_t>(y)
				* GAME_VOXEL_CHUNK_DEPTH + z] = east != nullptr
				&& east->initialized != FT_FALSE ? source_blocks[2U][
				(static_cast<std::size_t>(z) * GAME_VOXEL_CHUNK_HEIGHT + y)
				* GAME_VOXEL_CHUNK_WIDTH]
				: GAME_VOXEL_AIR_BLOCK;
		}
	}
	for (x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
	{
		for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
		{
			snapshot.north_border[static_cast<std::size_t>(y)
				* GAME_VOXEL_CHUNK_WIDTH + x] = north != nullptr
				&& north->initialized != FT_FALSE ? source_blocks[3U][
				(static_cast<std::size_t>(GAME_VOXEL_CHUNK_DEPTH - 1)
				* GAME_VOXEL_CHUNK_HEIGHT + y) * GAME_VOXEL_CHUNK_WIDTH + x]
				: GAME_VOXEL_AIR_BLOCK;
			snapshot.south_border[static_cast<std::size_t>(y)
				* GAME_VOXEL_CHUNK_WIDTH + x] = south != nullptr
				&& south->initialized != FT_FALSE ? source_blocks[4U][
				(static_cast<std::size_t>(y) * GAME_VOXEL_CHUNK_WIDTH) + x]
				: GAME_VOXEL_AIR_BLOCK;
		}
	}
	for (z = 0; z < edge; ++z)
	{
		for (x = 0; x < edge; ++x)
		{
			int32_t local_x = x - halo;
			int32_t local_z = z - halo;
			int32_t side_x = local_x < 0 ? -1 : local_x >= GAME_VOXEL_CHUNK_WIDTH ? 1 : 0;
			int32_t side_z = local_z < 0 ? -1 : local_z >= GAME_VOXEL_CHUNK_DEPTH ? 1 : 0;
			std::size_t source_index = 0U;
			if (side_x < 0 && side_z < 0) source_index = 5U;
			else if (side_x > 0 && side_z < 0) source_index = 6U;
			else if (side_x < 0 && side_z > 0) source_index = 7U;
			else if (side_x > 0 && side_z > 0) source_index = 8U;
			else if (side_x < 0) source_index = 1U;
			else if (side_x > 0) source_index = 2U;
			else if (side_z < 0) source_index = 3U;
			else if (side_z > 0) source_index = 4U;
			if (source_index == 0U)
				continue ;
			if (sources[source_index] == nullptr
				|| sources[source_index]->initialized == FT_FALSE)
			{
				snapshot.lighting_halo_valid = FT_FALSE;
				continue ;
			}
			if (side_x < 0) local_x += GAME_VOXEL_CHUNK_WIDTH;
			else if (side_x > 0) local_x -= GAME_VOXEL_CHUNK_WIDTH;
			if (side_z < 0) local_z += GAME_VOXEL_CHUNK_DEPTH;
			else if (side_z > 0) local_z -= GAME_VOXEL_CHUNK_DEPTH;
			std::size_t ring = snapshot.lighting_ring_offsets[
				static_cast<std::size_t>(z) * edge + x];
			for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
			{
				std::size_t source_offset = (static_cast<std::size_t>(local_z)
					* GAME_VOXEL_CHUNK_HEIGHT + y) * GAME_VOXEL_CHUNK_WIDTH
					+ static_cast<std::size_t>(local_x);
				snapshot.lighting_blocks[ring + y] = source_blocks[source_index][source_offset];
				snapshot.lighting_existing_light[ring + y] = sources[source_index]->light[source_offset];
			}
		}
	}
	return (FT_ERR_SUCCESS);
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
	int32_t source_check;
	uint32_t ring_offset;

	snapshot.lighting_halo_valid = FT_TRUE;
	snapshot.lighting_cardinal_halo_valid = FT_TRUE;
	source_check = 0;
	while (source_check < 9)
	{
		if (source_check != 0 && sources[source_check] != nullptr
			&& sources[source_check]->initialized != false
			&& (sources[source_check]->light_buffer_is_valid() == false
				|| sources[source_check]->light_is_current() == false))
			snapshot.lighting_halo_blocked = FT_TRUE;
		if (sources[source_check] == nullptr
			|| sources[source_check]->initialized == false
			|| sources[source_check]->light_buffer_is_valid() == false
			|| (source_check != 0
				&& sources[source_check]->light_is_current() == false))
			snapshot.lighting_halo_valid = FT_FALSE;
		if (source_check <= 4
			&& (sources[source_check] == nullptr
				|| sources[source_check]->initialized == false
				|| sources[source_check]->light_buffer_is_valid() == false
				|| (source_check != 0
					&& sources[source_check]->light_is_current() == false)))
			snapshot.lighting_cardinal_halo_valid = FT_FALSE;
		source_check += 1;
	}

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
			if (source_index == 0U)
			{
				halo_x += 1;
				continue ;
			}
			ring_offset = snapshot.lighting_ring_offsets[
				static_cast<std::size_t>(halo_z)
				* static_cast<std::size_t>(edge)
				+ static_cast<std::size_t>(halo_x)];
			index = static_cast<std::size_t>(ring_offset);
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
				if (source == nullptr || source->initialized == false)
					snapshot.lighting_existing_light[index
						+ static_cast<std::size_t>(y)] = 0U;
				else
					snapshot.lighting_existing_light[index
						+ static_cast<std::size_t>(y)] = source->light.get(
						local_x, y, local_z);
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
	snapshot.west_border_valid = west != nullptr
		&& west->initialized ? FT_TRUE : FT_FALSE;
	snapshot.east_border_valid = east != nullptr
		&& east->initialized ? FT_TRUE : FT_FALSE;
	snapshot.north_border_valid = north != nullptr
		&& north->initialized ? FT_TRUE : FT_FALSE;
	snapshot.south_border_valid = south != nullptr
		&& south->initialized ? FT_TRUE : FT_FALSE;
	snapshot.blocks.clear();
	snapshot.west_border.clear();
	snapshot.east_border.clear();
	snapshot.north_border.clear();
	snapshot.south_border.clear();
	snapshot.lighting_blocks.clear();
	snapshot.lighting_existing_light.clear();
	snapshot.lighting_ring_offsets.clear();
	snapshot.existing_light.clear();
	snapshot.existing_light_valid = FT_FALSE;
	snapshot.lighting_halo_valid = FT_FALSE;
	snapshot.lighting_cardinal_halo_valid = FT_FALSE;
	snapshot.lighting_halo_blocked = FT_FALSE;
	snapshot.dependency_valid = FT_FALSE;
	snapshot.dependency_chunk_x = 0;
	snapshot.dependency_chunk_z = 0;
	snapshot.dependency_voxel_revision = 0U;
	snapshot.dependency_light_revision = 0U;
	snapshot.dependency_content_version = 0U;
	snapshot.dependency_light_input_version = 0U;
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
		const std::size_t halo_edge = static_cast<std::size_t>(
			GAME_VOXEL_CHUNK_WIDTH + WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO * 2);
		uint32_t ring_column = 0U;
		snapshot.lighting_ring_offsets.resize(halo_edge * halo_edge,
			UINT32_MAX);
		int32_t ring_z = 0;
		while (ring_z < static_cast<int32_t>(halo_edge))
		{
			int32_t ring_x = 0;
			while (ring_x < static_cast<int32_t>(halo_edge))
			{
				if (ring_x < WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO
					|| ring_x >= GAME_VOXEL_CHUNK_WIDTH
						+ WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO
					|| ring_z < WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO
					|| ring_z >= GAME_VOXEL_CHUNK_DEPTH
						+ WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO)
				{
					snapshot.lighting_ring_offsets[
						static_cast<std::size_t>(ring_z) * halo_edge
						+ static_cast<std::size_t>(ring_x)] = ring_column
						* static_cast<uint32_t>(GAME_VOXEL_CHUNK_HEIGHT);
					ring_column += 1U;
				}
				ring_x += 1;
			}
			ring_z += 1;
		}
		snapshot.lighting_blocks.resize(static_cast<std::size_t>(ring_column)
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT),
			GAME_VOXEL_AIR_BLOCK);
		snapshot.lighting_existing_light.resize(snapshot.lighting_blocks.size(), 0U);
		snapshot.existing_light.resize(static_cast<std::size_t>(
			GAME_VOXEL_CHUNK_WIDTH) * static_cast<std::size_t>(
			GAME_VOXEL_CHUNK_DEPTH) * static_cast<std::size_t>(
			GAME_VOXEL_CHUNK_HEIGHT), 0U);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (WorldChunkSnapshotCapture::capture_blocks(target,
		snapshot) != FT_ERR_SUCCESS)
	{
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		std::fprintf(stderr,
			"[WorldGen] remesh snapshot block capture failed chunk=(%d,%d)\n",
			target.chunk_x, target.chunk_z);
	#endif
		return (FT_ERR_INVALID_OPERATION);
	}
	if (target.light_buffer_is_valid())
		snapshot.existing_light_valid = FT_TRUE;
	{
		int32_t local_z = 0;
		while (local_z < GAME_VOXEL_CHUNK_DEPTH)
		{
			int32_t local_y = 0;
			while (local_y < GAME_VOXEL_CHUNK_HEIGHT)
			{
				int32_t local_x = 0;
				while (local_x < GAME_VOXEL_CHUNK_WIDTH)
				{
					std::size_t index = (static_cast<std::size_t>(local_z)
						* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
						+ static_cast<std::size_t>(local_y))
						* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
						+ static_cast<std::size_t>(local_x);
					snapshot.existing_light[index] = target.light.get(
						local_x, local_y, local_z);
					local_x += 1;
				}
				local_y += 1;
			}
			local_z += 1;
		}
	}
	if (WorldChunkSnapshotCapture::capture_lighting_halo(target, west, east,
		north, south, northwest, northeast, southwest, southeast,
		snapshot) != FT_ERR_SUCCESS)
	{
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		std::fprintf(stderr,
			"[WorldGen] remesh snapshot lighting capture failed chunk=(%d,%d)\n",
			target.chunk_x, target.chunk_z);
	#endif
		return (FT_ERR_INVALID_OPERATION);
	}
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
