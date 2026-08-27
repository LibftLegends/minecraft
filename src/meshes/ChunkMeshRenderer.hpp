#ifndef CHUNK_MESH_RENDERER_HPP
# define CHUNK_MESH_RENDERER_HPP

# include "../../src/camera/Camera.hpp"
# include "../../src/chunks/WorldChunk.hpp"
# include "../../src/frame/RenderCache.hpp"
# include "../../src/geometry/ClipVertex.hpp"
# include "../../src/geometry/ScreenVertex.hpp"
# include "../../src/meshes/MeshClipper.hpp"
# include "../../src/meshes/MeshCuller.hpp"
# include "../../src/meshes/MeshProjector.hpp"
# include "../../src/meshes/PerspectiveTriangleRasterizer.hpp"
# include "../../src/meshes/TriangleRasterizer.hpp"
# include "../ft_vox.hpp"

class ChunkMeshRenderer
{
  public:
	ChunkMeshRenderer();
	ChunkMeshRenderer(const ChunkMeshRenderer &other);
	~ChunkMeshRenderer();
	ChunkMeshRenderer &operator=(const ChunkMeshRenderer &other);

	bool draw_mesh(ft_render_framebuffer &framebuffer,
		std::vector<double> &depth_buffer, const Camera &camera,
		const RenderCache &render_cache, const WorldChunk &world_chunk) const;

  private:
	PerspectiveTriangleRasterizer default_rasterizer;
	const TriangleRasterizer *rasterizer;

	static const double NEAR_PLANE;

	bool transform_triangle(const Camera &camera,
		const RenderCache &render_cache, const WorldChunk &world_chunk,
		const chunk_mesh_vertex source_vertices[3],
		ClipVertex transformed_vertices[3]) const;
	bool project_and_draw(ft_render_framebuffer &framebuffer,
		std::vector<double> &depth_buffer, const RenderCache &render_cache,
		const chunk_mesh_vertex source_vertices[3],
		const ClipVertex clipped_vertices[4], size_t n) const;
	bool draw_triangle_pair(ft_render_framebuffer &framebuffer,
		std::vector<double> &depth_buffer, const RenderCache &render_cache,
		const chunk_mesh_vertex source_vertices[3],
		const ClipVertex clipped_vertices[4], size_t clipped_count) const;
	void process_triangle(ft_render_framebuffer &framebuffer,
		std::vector<double> &depth_buffer, const Camera &camera,
		const RenderCache &render_cache, const WorldChunk &world_chunk,
		const chunk_mesh_vertex source_vertices[3],
		ClipVertex transformed_vertices[3]) const;
};

#endif
