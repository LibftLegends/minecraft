#include "../../src/world/WorldChunkSnapshotReader.hpp"

namespace
{
	static void apply_snapshot_sky_fallback(
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		int32_t world_x, int32_t world_y, int32_t world_z,
		uint8_t *packed_light) noexcept
	{
		if (packed_light == nullptr || *packed_light != 0U
			|| world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT)
			return ;
		for (int32_t y = world_y; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
		{
			uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
			const voxel_block_metadata *metadata;
			if (WorldChunkSnapshotReader::lookup_snapshot_block(
				const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
				world_x, y, world_z, &block_id) != FT_ERR_SUCCESS)
				return ;
			metadata = &voxel_get_block_metadata(block_id);
			if (metadata->light_attenuation >= 15U
				|| metadata->transparent == FT_FALSE)
				return ;
		}
		*packed_light = voxel_light_pack(15U, 0U);
	}
}

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
	if (snapshot.generation_metadata.valid != FT_FALSE)
	{
		error_code = chunk.set_generation_metadata(
			snapshot.generation_metadata);
		if (error_code != FT_ERR_SUCCESS)
		{
			(void)chunk.destroy();
			return (error_code);
		}
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
	/* Mesh generation queries the immediately adjacent cell first.  Resolve
	 * that one-cell border through the explicit captured-border contract before
	 * consulting the wider lighting ring; otherwise an absent ring entry would
	 * silently look like air and publish a hole at an unloaded chunk boundary. */
	if ((local_x == -1 || local_x == GAME_VOXEL_CHUNK_WIDTH)
		&& local_z >= 0 && local_z < GAME_VOXEL_CHUNK_DEPTH)
		return (WorldChunkSnapshotReader::lookup_border_block(*snapshot,
			local_x, local_z, world_y, block_id));
	if ((local_z == -1 || local_z == GAME_VOXEL_CHUNK_DEPTH)
		&& local_x >= 0 && local_x < GAME_VOXEL_CHUNK_WIDTH)
		return (WorldChunkSnapshotReader::lookup_border_block(*snapshot,
			local_x, local_z, world_y, block_id));
	if (local_x >= -halo && local_x < GAME_VOXEL_CHUNK_WIDTH + halo
		&& local_z >= -halo && local_z < GAME_VOXEL_CHUNK_DEPTH + halo)
	{
		const std::size_t offset_index = (static_cast<std::size_t>(local_z + halo)
			* static_cast<std::size_t>(edge))
			+ static_cast<std::size_t>(local_x + halo);
		if (snapshot->lighting_ring_offsets.size() <= offset_index
			|| snapshot->lighting_ring_offsets[offset_index] == UINT32_MAX)
			return (FT_ERR_INVALID_OPERATION);
		index = static_cast<std::size_t>(snapshot->lighting_ring_offsets[
			offset_index]) + static_cast<std::size_t>(world_y);
		if (snapshot->lighting_blocks.size() <= index)
			return (FT_ERR_INVALID_OPERATION);
		*block_id = snapshot->lighting_blocks[index];
		return (FT_ERR_SUCCESS);
	}
	return (WorldChunkSnapshotReader::lookup_border_block(*snapshot, local_x,
		local_z, world_y, block_id));
}

int32_t WorldChunkSnapshotReader::lookup_snapshot_light(void *user_data,
	int32_t world_x, int32_t world_y, int32_t world_z,
	uint8_t *packed_light) noexcept
{
	const WorldGenerationPipeline::WorldChunkSnapshot *snapshot;
	int32_t local_x;
	int32_t local_z;
	int32_t halo_local_x;
	int32_t halo_local_z;
	int32_t halo_edge;
	std::size_t index;

	if (user_data == nullptr || packed_light == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	snapshot = static_cast<const WorldGenerationPipeline::WorldChunkSnapshot *>(
		user_data);
	local_x = world_x - snapshot->chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	local_z = world_z - snapshot->chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	halo_edge = GAME_VOXEL_CHUNK_WIDTH
		+ WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO * 2;
	halo_local_x = local_x + WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO;
	halo_local_z = local_z + WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO;
	if (world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT
		|| halo_local_x < 0 || halo_local_x >= halo_edge
		|| halo_local_z < 0 || halo_local_z >= halo_edge)
	{
		*packed_light = 0U;
		return (FT_ERR_SUCCESS);
	}
	if (local_x >= 0 && local_x < GAME_VOXEL_CHUNK_WIDTH
		&& local_z >= 0 && local_z < GAME_VOXEL_CHUNK_DEPTH)
	{
		index = (static_cast<std::size_t>(local_z)
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
			+ static_cast<std::size_t>(world_y))
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
			+ static_cast<std::size_t>(local_x);
		if (snapshot->existing_light.size() <= index)
			return (FT_ERR_INVALID_OPERATION);
		*packed_light = snapshot->existing_light[index];
		apply_snapshot_sky_fallback(*snapshot, world_x, world_y, world_z,
			packed_light);
		return (FT_ERR_SUCCESS);
	}
	const std::size_t offset_index = static_cast<std::size_t>(halo_local_z)
		* static_cast<std::size_t>(halo_edge)
		+ static_cast<std::size_t>(halo_local_x);
	if (snapshot->lighting_ring_offsets.size() <= offset_index
		|| snapshot->lighting_ring_offsets[offset_index] == UINT32_MAX)
		return (FT_ERR_INVALID_OPERATION);
	index = static_cast<std::size_t>(snapshot->lighting_ring_offsets[
		offset_index]) + static_cast<std::size_t>(world_y);
	if (snapshot->lighting_existing_light.size() <= index)
		return (FT_ERR_INVALID_OPERATION);
	*packed_light = snapshot->lighting_existing_light[index];
	apply_snapshot_sky_fallback(*snapshot, world_x, world_y, world_z,
		packed_light);
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkSnapshotReader::lookup_snapshot_mesh_block(void *user_data,
	int32_t world_x, int32_t world_y, int32_t world_z,
	uint32_t *block_id) noexcept
{
	const WorldGenerationPipeline::WorldChunkSnapshot *snapshot;
	int32_t local_x;
	int32_t local_z;
	bool cardinal_border;

	if (user_data == nullptr || block_id == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	snapshot = static_cast<const WorldGenerationPipeline::WorldChunkSnapshot *>(
		user_data);
	if (world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT)
	{
		*block_id = GAME_VOXEL_AIR_BLOCK;
		return (FT_ERR_SUCCESS);
	}
	local_x = world_x - snapshot->chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	local_z = world_z - snapshot->chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	if (local_x >= 0 && local_x < GAME_VOXEL_CHUNK_WIDTH
		&& local_z >= 0 && local_z < GAME_VOXEL_CHUNK_DEPTH)
		return (WorldChunkSnapshotReader::lookup_snapshot_block(user_data,
			world_x, world_y, world_z, block_id));
	cardinal_border = (local_x == -1 || local_x == GAME_VOXEL_CHUNK_WIDTH)
		&& local_z >= 0 && local_z < GAME_VOXEL_CHUNK_DEPTH;
	cardinal_border = cardinal_border || ((local_z == -1
		|| local_z == GAME_VOXEL_CHUNK_DEPTH) && local_x >= 0
		&& local_x < GAME_VOXEL_CHUNK_WIDTH);
	if (!cardinal_border)
	{
		*block_id = GAME_VOXEL_AIR_BLOCK;
		return (FT_ERR_SUCCESS);
	}
	if ((local_x == -1 && snapshot->west_border_valid == FT_FALSE)
		|| (local_x == GAME_VOXEL_CHUNK_WIDTH
			&& snapshot->east_border_valid == FT_FALSE)
		|| (local_z == -1 && snapshot->north_border_valid == FT_FALSE)
		|| (local_z == GAME_VOXEL_CHUNK_DEPTH
			&& snapshot->south_border_valid == FT_FALSE))
	{
		*block_id = GAME_VOXEL_AIR_BLOCK;
		return (FT_ERR_SUCCESS);
	}
	return (WorldChunkSnapshotReader::lookup_snapshot_block(user_data,
		world_x, world_y, world_z, block_id));
}

int32_t WorldChunkSnapshotReader::lookup_border_block(const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
	int32_t local_x, int32_t local_z, int32_t world_y,
	uint32_t *block_id) noexcept
{
	if (local_x == -1 && local_z >= 0 && local_z < GAME_VOXEL_CHUNK_DEPTH)
	{
		if (snapshot.west_border_valid == FT_FALSE)
		{
			*block_id = VOXEL_GENERATOR_STONE_BLOCK;
			return (FT_ERR_SUCCESS);
		}
		*block_id = snapshot.west_border[static_cast<std::size_t>(world_y)
			* GAME_VOXEL_CHUNK_DEPTH + static_cast<std::size_t>(local_z)];
	}
	else if (local_x == GAME_VOXEL_CHUNK_WIDTH && local_z >= 0
		&& local_z < GAME_VOXEL_CHUNK_DEPTH)
	{
		if (snapshot.east_border_valid == FT_FALSE)
		{
			*block_id = VOXEL_GENERATOR_STONE_BLOCK;
			return (FT_ERR_SUCCESS);
		}
		*block_id = snapshot.east_border[static_cast<std::size_t>(world_y)
			* GAME_VOXEL_CHUNK_DEPTH + static_cast<std::size_t>(local_z)];
	}
	else if (local_z == -1 && local_x >= 0 && local_x < GAME_VOXEL_CHUNK_WIDTH)
	{
		if (snapshot.north_border_valid == FT_FALSE)
		{
			*block_id = VOXEL_GENERATOR_STONE_BLOCK;
			return (FT_ERR_SUCCESS);
		}
		*block_id = snapshot.north_border[static_cast<std::size_t>(world_y)
			* GAME_VOXEL_CHUNK_WIDTH + static_cast<std::size_t>(local_x)];
	}
	else if (local_z == GAME_VOXEL_CHUNK_DEPTH && local_x >= 0
		&& local_x < GAME_VOXEL_CHUNK_WIDTH)
	{
		if (snapshot.south_border_valid == FT_FALSE)
		{
			*block_id = VOXEL_GENERATOR_STONE_BLOCK;
			return (FT_ERR_SUCCESS);
		}
		*block_id = snapshot.south_border[static_cast<std::size_t>(world_y)
			* GAME_VOXEL_CHUNK_WIDTH + static_cast<std::size_t>(local_x)];
	}
	else
		*block_id = GAME_VOXEL_AIR_BLOCK;
	return (FT_ERR_SUCCESS);
}
