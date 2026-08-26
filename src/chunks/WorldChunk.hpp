#ifndef WORLD_CHUNK_HPP
# define WORLD_CHUNK_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../ft_vox.hpp"

class WorldChunk
{
  public:
	int32_t chunk_x;
	int32_t chunk_z;
	int32_t world_x;
	int32_t world_z;
	game_voxel_chunk chunk;
	chunk_mesh mesh;
	uint64_t mesh_revision;
	uint64_t voxel_revision;
	uint64_t pending_mesh_request_id;
	bool mesh_dirty;
	bool initialized;

	WorldChunk();
	WorldChunk(const WorldChunk &other);
	~WorldChunk();
	WorldChunk &operator=(const WorldChunk &other);

	void reset_coordinates();
	void destroy();
};

#endif
