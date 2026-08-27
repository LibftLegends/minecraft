#ifndef MESH_CULLER_HPP
# define MESH_CULLER_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../../src/camera/Camera.hpp"
# include "../../src/chunks/WorldChunk.hpp"
# include "../../src/frame/RenderCache.hpp"
# include "../ft_vox.hpp"

class MeshCuller
{
  public:
	MeshCuller();
	MeshCuller(const MeshCuller &other);
	~MeshCuller();
	MeshCuller &operator=(const MeshCuller &other);

	static bool chunk_is_visible(const Camera &camera,
		const WorldChunk &world_chunk, const RenderCache &cache);
	static bool triangle_faces_camera(const Camera &camera,
		const WorldChunk &world_chunk,
		const chunk_mesh_vertex triangle_vertices[3]);

  private:
	static void face_normal(uint8_t face, double &nx, double &ny, double &nz);
	static void chunk_corner_in_view(const Camera &camera, const WorldChunk &wc,
		const RenderCache &cache, double local_x, double local_y,
		double local_z, double &view_x, double &pitched_y, double &pitched_z);
	static bool chunk_aabb_is_visible(const Camera &camera,
		const WorldChunk &world_chunk, const RenderCache &cache);
};

#endif
