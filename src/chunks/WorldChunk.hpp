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
	uint64_t mesh_revision;
	uint64_t voxel_revision;
	uint64_t pending_mesh_request_id;
	bool mesh_dirty;
	bool initialized;
	/* Keep frequently scanned render/stream metadata contiguous. The voxel
	 * storage and mesh payloads are cold during slot discovery and culling. */
	game_voxel_chunk chunk;
	chunk_mesh mesh;

	WorldChunk();
	WorldChunk(const WorldChunk &other);
	~WorldChunk();
	WorldChunk &operator=(const WorldChunk &other);

	void reset_coordinates();
	void destroy();
	static bool mesh_is_drawable(const chunk_mesh &mesh) noexcept;
};

#endif
