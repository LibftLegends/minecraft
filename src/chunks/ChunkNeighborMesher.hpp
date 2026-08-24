#ifndef CHUNK_NEIGHBOR_MESHER_HPP
# define CHUNK_NEIGHBOR_MESHER_HPP

# include "../../src/chunks/WorldChunk.hpp"
# include "../../src/chunks/WorldChunkStore.hpp"
# include "../ft_vox.hpp"

class ChunkNeighborMesher
{
  public:
	ChunkNeighborMesher();
	ChunkNeighborMesher(const ChunkNeighborMesher &other);
	~ChunkNeighborMesher();
	ChunkNeighborMesher &operator=(const ChunkNeighborMesher &other);

	static int32_t generate_with_neighbors(chunk_mesh &mesh,
		const game_voxel_chunk &chunk, int32_t chunk_x, int32_t chunk_z,
		WorldChunk *chunks, int32_t chunk_count);

  private:
	struct					NeighborContext
	{
		const WorldChunk	*west;
		const WorldChunk	*east;
		const WorldChunk	*north;
		const WorldChunk	*south;
		int32_t				world_origin_x;
		int32_t				world_origin_z;
	};

	static NeighborContext build_context(int32_t chunk_x, int32_t chunk_z,
		WorldChunk *chunks, int32_t chunk_count);
	static void resolve_neighbor_read(const NeighborContext &ctx,
		int32_t local_x, int32_t local_z, const WorldChunk **out_chunk,
		int32_t *out_read_x, int32_t *out_read_z);
	static int32_t lookup_block(void *user_data, int32_t world_x,
		int32_t world_y, int32_t world_z, uint32_t *block_id);
};

#endif
