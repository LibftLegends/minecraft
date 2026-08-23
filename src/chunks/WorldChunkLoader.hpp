#ifndef WORLD_CHUNK_LOADER_HPP
#define WORLD_CHUNK_LOADER_HPP

#ifndef GAME_USE_VOXEL_REGION_BACKEND
#define GAME_USE_VOXEL_REGION_BACKEND
#endif
#include "../ft_vox.hpp"
#include "../../src/chunks/WorldChunkStore.hpp"
#include "../../src/coordinates/WorldCoordinates.hpp"
#include "../../Libft/Modules/Voxel/terrain_api.hpp"

class WorldChunkLoader
{
  public:
    WorldChunkLoader();
    WorldChunkLoader(const WorldChunkLoader &other);
    ~WorldChunkLoader();
    WorldChunkLoader &operator=(const WorldChunkLoader &other);

    static int32_t initialize_chunk(WorldChunk *world_chunk, int32_t chunk_x, int32_t chunk_z,
                                    const char *seed);
    static int32_t initialize_chunk(WorldChunk *world_chunk, int32_t chunk_x, int32_t chunk_z,
                                    const char *seed, WorldChunk *chunks, int32_t chunk_count);
    static int32_t initialize_chunk(WorldChunk *world_chunk, int32_t chunk_x, int32_t chunk_z,
                                    const char *seed, WorldChunk *chunks, int32_t chunk_count,
                                    const terrain_generation_config &config);
    static int32_t generate_missing_chunk(WorldChunk *chunks, int32_t chunk_count,
                                          int32_t *loaded_chunk_count, int32_t chunk_x,
                                          int32_t chunk_z, const char *seed);
    static int32_t remesh_chunk(WorldChunk *chunks, int32_t chunk_count, int32_t chunk_x,
                                int32_t chunk_z);
    static int32_t remesh_chunk(WorldChunk *chunks, int32_t chunk_count, int32_t chunk_x,
                                int32_t chunk_z, bool use_neighbors);
    static int32_t remesh_edited_chunk_border(WorldChunk *chunks, int32_t chunk_count,
                                              int32_t chunk_x, int32_t chunk_z, int32_t local_x,
                                              int32_t local_z);

  private:
    struct NeighborContext
    {
        const WorldChunk *west;
        const WorldChunk *east;
        const WorldChunk *north;
        const WorldChunk *south;
        int32_t world_origin_x;
        int32_t world_origin_z;
    };

    static int32_t lookup_block(void *user_data, int32_t world_x, int32_t world_y, int32_t world_z,
                                uint32_t *block_id);
    static int32_t setup_chunk_coordinates(WorldChunk *world_chunk, int32_t chunk_x,
                                           int32_t chunk_z);
    static int32_t init_chunk_data(WorldChunk *world_chunk, const char *seed,
                                   const terrain_generation_config *config = nullptr);
    static int32_t build_mesh_with_neighbors(WorldChunk *world_chunk, int32_t chunk_x,
                                             int32_t chunk_z, WorldChunk *chunks,
                                             int32_t chunk_count);
};

#endif
