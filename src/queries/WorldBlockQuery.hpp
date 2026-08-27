#ifndef WORLD_BLOCK_QUERY_HPP
# define WORLD_BLOCK_QUERY_HPP

# include "../../src/chunks/WorldChunkStore.hpp"
# include "../../src/coordinates/WorldCoordinates.hpp"
# include "../ft_vox.hpp"

class	World;

class WorldBlockQuery
{
  public:
	WorldBlockQuery();
	WorldBlockQuery(const WorldBlockQuery &other);
	~WorldBlockQuery();
	WorldBlockQuery &operator=(const WorldBlockQuery &other);

	static bool surface_top_at(const World &world, int32_t world_x,
		int32_t world_z, double *surface_top);
	static bool solid_block_at(const World &world, int32_t world_x,
		int32_t world_y, int32_t world_z);
	static bool block_id_at(const World &world, int32_t world_x,
		int32_t world_y, int32_t world_z, uint32_t *block_id);

  private:
	static bool find_chunk_local(const World &world, int32_t wx, int32_t wz,
		const WorldChunk **chunk, int32_t &lx, int32_t &lz);
};

# include "../../src/world/World.hpp"

#endif
