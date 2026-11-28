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
	constexpr std::size_t plane = static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
		* GAME_VOXEL_CHUNK_HEIGHT;
	constexpr std::size_t section_plane = static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
		* GAME_VOXEL_CHUNK_DEPTH;
	if (snapshot.blocks.size() > plane * GAME_VOXEL_CHUNK_DEPTH)
		return (FT_ERR_OUT_OF_RANGE);
	int32_t error_code = chunk.initialize();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	/* This chunk is private to the worker. Mark a nonempty snapshot dirty once,
	 * then fill its sections without taking the chunk lock for each voxel. */
	if (!snapshot.blocks.empty())
		error_code = chunk.write_generated_block(0, 0, 0, GAME_VOXEL_AIR_BLOCK);
	for (std::size_t index = 0U;
		error_code == FT_ERR_SUCCESS && index < snapshot.blocks.size(); ++index)
	{
		const uint32_t block_id = snapshot.blocks[index];
		if (block_id == GAME_VOXEL_AIR_BLOCK)
			continue ;
		const std::size_t z = index / plane;
		const std::size_t y = (index % plane) / GAME_VOXEL_CHUNK_WIDTH;
		const std::size_t x = index % GAME_VOXEL_CHUNK_WIDTH;
		const auto section_index = static_cast<uint8_t>(y / GAME_VOXEL_SECTION_EDGE);
		const auto section_cell = static_cast<uint16_t>(x + z * GAME_VOXEL_CHUNK_WIDTH
			+ (y % GAME_VOXEL_SECTION_EDGE) * section_plane);
		if (section_cell != 0U)
			error_code = chunk.get_section(section_index).set_block(section_cell, block_id);
	}
	/* Leave the first cell as air until the end. In dense sections this keeps
	 * the palette's uniformity check from scanning an increasingly long run
	 * of identical voxels after each write. Uniform sections still compact. */
	for (uint8_t section_index = 0U;
		error_code == FT_ERR_SUCCESS && section_index < GAME_VOXEL_CHUNK_SECTION_COUNT;
		++section_index)
	{
		const std::size_t first_cell = static_cast<std::size_t>(section_index)
			* GAME_VOXEL_SECTION_EDGE * GAME_VOXEL_CHUNK_WIDTH;
		if (first_cell >= snapshot.blocks.size())
			break ;
		const uint32_t block_id = snapshot.blocks[first_cell];
		if (block_id != GAME_VOXEL_AIR_BLOCK)
			error_code = chunk.get_section(section_index).set_block(0U, block_id);
	}
	if (error_code == FT_ERR_SUCCESS && snapshot.generation_metadata.valid != FT_FALSE)
		error_code = chunk.set_generation_metadata(snapshot.generation_metadata);
	if (error_code != FT_ERR_SUCCESS)
		(void)chunk.destroy();
	return (error_code);
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
