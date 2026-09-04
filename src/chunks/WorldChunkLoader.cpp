#include "../../src/chunks/WorldChunkLoader.hpp"

namespace
{
	int32_t lookup_local_light_block(void *user_data, int32_t world_x,
		int32_t world_y, int32_t world_z, uint32_t *block_id) noexcept
	{
		game_voxel_chunk *chunk = static_cast<game_voxel_chunk *>(user_data);
		if (chunk == nullptr || block_id == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		world_x %= GAME_VOXEL_CHUNK_WIDTH;
		world_z %= GAME_VOXEL_CHUNK_DEPTH;
		if (world_x < 0)
			world_x += GAME_VOXEL_CHUNK_WIDTH;
		if (world_z < 0)
			world_z += GAME_VOXEL_CHUNK_DEPTH;
		return (chunk->read_block(world_x, world_y, world_z, block_id));
	}
}

WorldChunkLoader::WorldChunkLoader()
{
}

WorldChunkLoader::WorldChunkLoader(const WorldChunkLoader &other)
{
	(void)other;
}

WorldChunkLoader::~WorldChunkLoader()
{
}

WorldChunkLoader &WorldChunkLoader::operator=(const WorldChunkLoader &other)
{
	(void)other;
	return (*this);
}

int32_t WorldChunkLoader::setup_chunk_coordinates(WorldChunk *world_chunk,
	int32_t chunk_x, int32_t chunk_z)
{
	if (!world_chunk)
		return (FT_ERR_INVALID_ARGUMENT);
	world_chunk->chunk_x = chunk_x;
	world_chunk->chunk_z = chunk_z;
	world_chunk->world_x = chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	world_chunk->world_z = chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	world_chunk->initialized = false;
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkLoader::init_chunk_data(WorldChunk *world_chunk,
	const char *seed, const voxel_generation_config *config)
{
	int32_t	err;

	err = world_chunk->chunk.initialize();
	if (err != FT_ERR_SUCCESS)
		return (err);
	err = chunk_mesh_initialize(world_chunk->mesh);
	if (err != FT_ERR_SUCCESS)
	{
		(void)world_chunk->chunk.destroy();
		return (err);
	}
	if (config != nullptr)
		err = voxel_generate_chunk(world_chunk->chunk, world_chunk->world_x,
				world_chunk->world_z, seed, *config);
	else
		err = voxel_generate_chunk(world_chunk->chunk, world_chunk->world_x,
				world_chunk->world_z, seed);
	if (err != FT_ERR_SUCCESS)
	{
		(void)chunk_mesh_destroy(world_chunk->mesh);
		(void)world_chunk->chunk.destroy();
	}
	return (err);
}

int32_t WorldChunkLoader::build_mesh_with_neighbors(WorldChunk *world_chunk,
	int32_t chunk_x, int32_t chunk_z, WorldChunk *chunks, int32_t chunk_count)
{
	int32_t	err;

	err = voxel_light_build_chunk_local(world_chunk->light,
		world_chunk->world_x, world_chunk->world_z, lookup_local_light_block,
		&world_chunk->chunk);
	if (err != FT_ERR_SUCCESS)
		return (err);
	err = ChunkNeighborMesher::generate_with_neighbors(world_chunk->mesh,
			world_chunk->chunk, chunk_x, chunk_z, chunks, chunk_count,
			&world_chunk->light);
	if (err != FT_ERR_SUCCESS)
	{
		(void)chunk_mesh_destroy(world_chunk->mesh);
		(void)world_chunk->chunk.destroy();
	}
	return (err);
}

int32_t WorldChunkLoader::initialize_chunk(WorldChunk *world_chunk,
	int32_t chunk_x, int32_t chunk_z, const char *seed)
{
	int32_t	err;

	err = setup_chunk_coordinates(world_chunk, chunk_x, chunk_z);
	if (err != FT_ERR_SUCCESS)
		return (err);
	err = init_chunk_data(world_chunk, seed);
	if (err != FT_ERR_SUCCESS)
		return (err);
	err = voxel_light_build_chunk_local(world_chunk->light,
		world_chunk->world_x, world_chunk->world_z, lookup_local_light_block,
		&world_chunk->chunk);
	if (err == FT_ERR_SUCCESS)
		err = chunk_mesh_generate_from_chunk_with_light(world_chunk->mesh,
			world_chunk->chunk, world_chunk->light);
	if (err != FT_ERR_SUCCESS)
	{
		(void)chunk_mesh_destroy(world_chunk->mesh);
		(void)world_chunk->chunk.destroy();
		return (err);
	}
	world_chunk->initialized = true;
	world_chunk->mesh_revision += 1U;
	world_chunk->voxel_revision += 1U;
	world_chunk->pending_mesh_request_id = 0U;
	world_chunk->mesh_dirty = false;
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkLoader::initialize_chunk(WorldChunk *world_chunk,
	int32_t chunk_x, int32_t chunk_z, const char *seed, WorldChunk *chunks,
	int32_t chunk_count)
{
	int32_t	err;

	err = setup_chunk_coordinates(world_chunk, chunk_x, chunk_z);
	if (err != FT_ERR_SUCCESS)
		return (err);
	err = init_chunk_data(world_chunk, seed);
	if (err != FT_ERR_SUCCESS)
		return (err);
	err = build_mesh_with_neighbors(world_chunk, chunk_x, chunk_z, chunks,
			chunk_count);
	if (err != FT_ERR_SUCCESS)
		return (err);
	world_chunk->initialized = true;
	world_chunk->mesh_revision += 1U;
	world_chunk->voxel_revision += 1U;
	world_chunk->pending_mesh_request_id = 0U;
	world_chunk->mesh_dirty = false;
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkLoader::initialize_chunk(WorldChunk *world_chunk,
	int32_t chunk_x, int32_t chunk_z, const char *seed, WorldChunk *chunks,
	int32_t chunk_count, const voxel_generation_config &config)
{
	int32_t	err;

	err = setup_chunk_coordinates(world_chunk, chunk_x, chunk_z);
	if (err != FT_ERR_SUCCESS)
		return (err);
	err = init_chunk_data(world_chunk, seed, &config);
	if (err != FT_ERR_SUCCESS)
		return (err);
	err = build_mesh_with_neighbors(world_chunk, chunk_x, chunk_z, chunks,
			chunk_count);
	if (err != FT_ERR_SUCCESS)
		return (err);
	world_chunk->initialized = true;
	world_chunk->mesh_revision += 1U;
	world_chunk->voxel_revision += 1U;
	world_chunk->pending_mesh_request_id = 0U;
	world_chunk->mesh_dirty = false;
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkLoader::generate_missing_chunk(WorldChunk *chunks,
	int32_t chunk_count, int32_t *loaded_chunk_count, int32_t chunk_x,
	int32_t chunk_z, const char *seed)
{
	WorldChunk	*slot;
	int32_t		err;

	if (WorldChunkStore::find_chunk(chunks, chunk_count, chunk_x, chunk_z))
		return (FT_ERR_SUCCESS);
	slot = WorldChunkStore::find_free_chunk_slot(chunks, chunk_count);
	if (!slot)
		return (FT_ERR_NO_MEMORY);
	err = initialize_chunk(slot, chunk_x, chunk_z, seed);
	if (err == FT_ERR_SUCCESS && loaded_chunk_count)
		*loaded_chunk_count += 1;
	return (err);
}

int32_t WorldChunkLoader::remesh_chunk(WorldChunk *chunks, int32_t chunk_count,
	int32_t chunk_x, int32_t chunk_z)
{
	WorldChunk	*wc;
	int32_t		err;

	wc = WorldChunkStore::find_chunk_mutable(chunks, chunk_count, chunk_x,
			chunk_z);
	if (!wc)
		return (FT_ERR_SUCCESS);
	wc->pending_mesh_request_id = 0U;
	wc->mesh_dirty = true;
	err = voxel_light_build_chunk_local(wc->light, wc->world_x, wc->world_z,
		lookup_local_light_block, &wc->chunk);
	if (err != FT_ERR_SUCCESS)
		return (err);
	err = chunk_mesh_clear(wc->mesh);
	if (err == FT_ERR_SUCCESS)
		err = chunk_mesh_generate_from_chunk_with_light(wc->mesh, wc->chunk,
			wc->light);
	if (err == FT_ERR_SUCCESS)
	{
		wc->mesh_revision += 1U;
		wc->mesh_dirty = false;
	}
	return (err);
}

int32_t WorldChunkLoader::remesh_chunk(WorldChunk *chunks, int32_t chunk_count,
	int32_t chunk_x, int32_t chunk_z, bool use_neighbors)
{
	WorldChunk	*wc;
	int32_t		err;

	wc = WorldChunkStore::find_chunk_mutable(chunks, chunk_count, chunk_x,
			chunk_z);
	if (!wc)
		return (FT_ERR_SUCCESS);
	wc->pending_mesh_request_id = 0U;
	wc->mesh_dirty = true;
	err = voxel_light_build_chunk_local(wc->light, wc->world_x, wc->world_z,
		lookup_local_light_block, &wc->chunk);
	if (err != FT_ERR_SUCCESS)
		return (err);
	err = chunk_mesh_clear(wc->mesh);
	if (err != FT_ERR_SUCCESS)
		return (err);
	if (!use_neighbors)
	{
		err = chunk_mesh_generate_from_chunk_with_light(wc->mesh, wc->chunk,
			wc->light);
		if (err == FT_ERR_SUCCESS)
		{
			wc->mesh_revision += 1U;
			wc->mesh_dirty = false;
		}
		return (err);
	}
	err = ChunkNeighborMesher::generate_with_neighbors(wc->mesh, wc->chunk,
			chunk_x, chunk_z, chunks, chunk_count, &wc->light);
	if (err == FT_ERR_SUCCESS)
	{
		wc->mesh_revision += 1U;
		wc->mesh_dirty = false;
	}
	return (err);
}

int32_t WorldChunkLoader::remesh_edited_chunk_border(WorldChunk *chunks,
	int32_t chunk_count, int32_t chunk_x, int32_t chunk_z, int32_t local_x,
	int32_t local_z)
{
	int32_t	err;

	err = remesh_chunk(chunks, chunk_count, chunk_x, chunk_z, true);
	if (err != FT_ERR_SUCCESS)
		return (err);
	if (local_x == 0)
		err = remesh_chunk(chunks, chunk_count, chunk_x - 1, chunk_z, true);
	if (err == FT_ERR_SUCCESS && local_x == GAME_VOXEL_CHUNK_WIDTH - 1)
		err = remesh_chunk(chunks, chunk_count, chunk_x + 1, chunk_z, true);
	if (err == FT_ERR_SUCCESS && local_z == 0)
		err = remesh_chunk(chunks, chunk_count, chunk_x, chunk_z - 1, true);
	if (err == FT_ERR_SUCCESS && local_z == GAME_VOXEL_CHUNK_DEPTH - 1)
		err = remesh_chunk(chunks, chunk_count, chunk_x, chunk_z + 1, true);
	return (err);
}
