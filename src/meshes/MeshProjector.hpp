#ifndef MESH_PROJECTOR_HPP
# define MESH_PROJECTOR_HPP

# include "../../src/camera/Camera.hpp"
# include "../../src/frame/RenderCache.hpp"
# include "../../src/geometry/ClipVertex.hpp"
# include "../../src/geometry/ScreenVertex.hpp"
# include "../ft_vox.hpp"

class MeshProjector
{
  public:
	MeshProjector();
	MeshProjector(const MeshProjector &other);
	~MeshProjector();
	MeshProjector &operator=(const MeshProjector &other);

	static bool transform_vertex(const Camera &camera,
		const chunk_mesh_vertex &vertex, int32_t origin_x, int32_t origin_z,
		const RenderCache &render_cache, ClipVertex *clip_vertex);
	static bool project_vertex(const RenderCache &render_cache,
		const ClipVertex &clip_vertex, ScreenVertex *screen_vertex);

  private:
	static constexpr double NEAR_PLANE = 0.08;
};

#endif
