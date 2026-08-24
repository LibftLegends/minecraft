#include "../../src/chunks/WorldChunkLoader.hpp"

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
	const char *seed, const terrain_generation_config *config)
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
		err = terrain_generate_chunk(world_chunk->chunk, world_chunk->world_x,
				world_chunk->world_z, seed, *config);
	else
		err = terrain_generate_chunk(world_chunk->chunk, world_chunk->world_x,
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

	err = ChunkNeighborMesher::generate_with_neighbors(world_chunk->mesh,
			world_chunk->chunk, chunk_x, chunk_z, chunks, chunk_count);
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
	err = chunk_mesh_generate_from_chunk(world_chunk->mesh, world_chunk->chunk);
	if (err != FT_ERR_SUCCESS)
	{
		(void)chunk_mesh_destroy(world_chunk->mesh);
		(void)world_chunk->chunk.destroy();
		return (err);
	}
	world_chunk->initialized = true;
	world_chunk->mesh_revision += 1U;
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
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkLoader::initialize_chunk(WorldChunk *world_chunk,
	int32_t chunk_x, int32_t chunk_z, const char *seed, WorldChunk *chunks,
	int32_t chunk_count, const terrain_generation_config &config)
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
	err = chunk_mesh_clear(wc->mesh);
	if (err == FT_ERR_SUCCESS)
		err = chunk_mesh_generate_from_chunk(wc->mesh, wc->chunk);
	if (err == FT_ERR_SUCCESS)
		wc->mesh_revision += 1U;
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
	err = chunk_mesh_clear(wc->mesh);
	if (err != FT_ERR_SUCCESS)
		return (err);
	if (!use_neighbors)
	{
		err = chunk_mesh_generate_from_chunk(wc->mesh, wc->chunk);
		if (err == FT_ERR_SUCCESS)
			wc->mesh_revision += 1U;
		return (err);
	}
	err = ChunkNeighborMesher::generate_with_neighbors(wc->mesh, wc->chunk,
			chunk_x, chunk_z, chunks, chunk_count);
	if (err == FT_ERR_SUCCESS)
		wc->mesh_revision += 1U;
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
