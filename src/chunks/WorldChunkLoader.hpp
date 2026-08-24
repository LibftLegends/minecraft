#ifndef WORLD_CHUNK_LOADER_HPP
# define WORLD_CHUNK_LOADER_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../../Libft/Modules/Voxel/voxel.hpp"
# include "../../src/chunks/ChunkNeighborMesher.hpp"
# include "../../src/chunks/WorldChunkStore.hpp"
# include "../../src/coordinates/WorldCoordinates.hpp"
# include "../ft_vox.hpp"

class WorldChunkLoader
{
  public:
	WorldChunkLoader();
	WorldChunkLoader(const WorldChunkLoader &other);
	~WorldChunkLoader();
	WorldChunkLoader &operator=(const WorldChunkLoader &other);

	static int32_t initialize_chunk(WorldChunk *world_chunk, int32_t chunk_x,
		int32_t chunk_z, const char *seed);
	static int32_t initialize_chunk(WorldChunk *world_chunk, int32_t chunk_x,
		int32_t chunk_z, const char *seed, WorldChunk *chunks,
		int32_t chunk_count);
	static int32_t initialize_chunk(WorldChunk *world_chunk, int32_t chunk_x,
		int32_t chunk_z, const char *seed, WorldChunk *chunks,
		int32_t chunk_count, const terrain_generation_config &config);
	static int32_t generate_missing_chunk(WorldChunk *chunks,
		int32_t chunk_count, int32_t *loaded_chunk_count, int32_t chunk_x,
		int32_t chunk_z, const char *seed);
	static int32_t remesh_chunk(WorldChunk *chunks, int32_t chunk_count,
		int32_t chunk_x, int32_t chunk_z);
	static int32_t remesh_chunk(WorldChunk *chunks, int32_t chunk_count,
		int32_t chunk_x, int32_t chunk_z, bool use_neighbors);
	static int32_t remesh_edited_chunk_border(WorldChunk *chunks,
		int32_t chunk_count, int32_t chunk_x, int32_t chunk_z, int32_t local_x,
		int32_t local_z);

  private:
	static int32_t setup_chunk_coordinates(WorldChunk *world_chunk,
		int32_t chunk_x, int32_t chunk_z);
	static int32_t init_chunk_data(WorldChunk *world_chunk, const char *seed,
		const terrain_generation_config *config = nullptr);
	static int32_t build_mesh_with_neighbors(WorldChunk *world_chunk,
		int32_t chunk_x, int32_t chunk_z, WorldChunk *chunks,
		int32_t chunk_count);
};

#endif
