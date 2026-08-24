#ifndef WORLD_CHUNK_STORE_HPP
# define WORLD_CHUNK_STORE_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../../src/chunks/WorldChunk.hpp"
# include "../../src/coordinates/WorldCoordinates.hpp"
# include "../ft_vox.hpp"

class WorldChunkStore
{
  public:
	WorldChunkStore();
	WorldChunkStore(const WorldChunkStore &other);
	~WorldChunkStore();
	WorldChunkStore &operator=(const WorldChunkStore &other);

	static const WorldChunk *find_chunk(const WorldChunk *chunks,
		int32_t chunk_count, int32_t chunk_x, int32_t chunk_z);
	static WorldChunk *find_chunk_mutable(WorldChunk *chunks,
		int32_t chunk_count, int32_t chunk_x, int32_t chunk_z);
	static WorldChunk *find_free_chunk_slot(WorldChunk *chunks,
		int32_t chunk_count);
	static void evict_far_chunks(WorldChunk *chunks, int32_t chunk_count,
		int32_t *loaded_chunk_count, int32_t target_chunk_x,
		int32_t target_chunk_z);
};

#endif
