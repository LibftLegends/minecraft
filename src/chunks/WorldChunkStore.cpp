#include "../../src/chunks/WorldChunkStore.hpp"

WorldChunkStore::WorldChunkStore()
{
}

WorldChunkStore::WorldChunkStore(const WorldChunkStore &other)
{
	*this = other;
}

WorldChunkStore::~WorldChunkStore()
{
}

WorldChunkStore &WorldChunkStore::operator=(const WorldChunkStore &other)
{
	(void)other;
	return (*this);
}

const WorldChunk *WorldChunkStore::find_chunk(const WorldChunk *chunks,
	int32_t chunk_count, int32_t chunk_x, int32_t chunk_z)
{
	int32_t	index;

	if (chunks == nullptr)
		return (nullptr);
	index = 0;
	while (index < chunk_count)
	{
		if (chunks[index].initialized == true
			&& chunks[index].chunk_x == chunk_x
			&& chunks[index].chunk_z == chunk_z)
			return (&chunks[index]);
		index = index + 1;
	}
	return (nullptr);
}

WorldChunk *WorldChunkStore::find_chunk_mutable(WorldChunk *chunks,
	int32_t chunk_count, int32_t chunk_x, int32_t chunk_z)
{
	int32_t	index;

	if (chunks == nullptr)
		return (nullptr);
	index = 0;
	while (index < chunk_count)
	{
		if (chunks[index].initialized == true
			&& chunks[index].chunk_x == chunk_x
			&& chunks[index].chunk_z == chunk_z)
			return (&chunks[index]);
		index = index + 1;
	}
	return (nullptr);
}

WorldChunk *WorldChunkStore::find_free_chunk_slot(WorldChunk *chunks,
	int32_t chunk_count)
{
	int32_t	index;

	if (chunks == nullptr)
		return (nullptr);
	index = 0;
	while (index < chunk_count)
	{
		if (chunks[index].initialized == false)
			return (&chunks[index]);
		index = index + 1;
	}
	return (nullptr);
}

void WorldChunkStore::evict_far_chunks(WorldChunk *chunks, int32_t chunk_count,
	int32_t *loaded_chunk_count, int32_t target_chunk_x, int32_t target_chunk_z)
{
	int32_t	index;
	int32_t	cache_radius_squared;

	if (chunks == nullptr || loaded_chunk_count == nullptr)
		return ;
	cache_radius_squared = WorldCoordinates::CACHE_CHUNK_RADIUS
		* WorldCoordinates::CACHE_CHUNK_RADIUS;
	index = 0;
	while (index < chunk_count)
	{
		if (chunks[index].initialized == true
			&& WorldCoordinates::chunk_distance_squared(chunks[index].chunk_x,
				chunks[index].chunk_z, target_chunk_x,
				target_chunk_z) > cache_radius_squared)
		{
			chunks[index].destroy();
			if (*loaded_chunk_count > 0)
				*loaded_chunk_count = *loaded_chunk_count - 1;
		}
		index = index + 1;
	}
}
