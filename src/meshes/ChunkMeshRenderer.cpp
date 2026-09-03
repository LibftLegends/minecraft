#include "../../src/meshes/ChunkMeshRenderer.hpp"

const double ChunkMeshRenderer::NEAR_PLANE = 0.08;

ChunkMeshRenderer::ChunkMeshRenderer() : default_rasterizer(),
	rasterizer(&this->default_rasterizer)
{
}

ChunkMeshRenderer::ChunkMeshRenderer(const ChunkMeshRenderer &other) : default_rasterizer(),
	rasterizer(&this->default_rasterizer)
{
	(void)other;
}

ChunkMeshRenderer::~ChunkMeshRenderer()
{
}

ChunkMeshRenderer &ChunkMeshRenderer::operator=(const ChunkMeshRenderer &other)
{
	(void)other;
	this->rasterizer = &this->default_rasterizer;
	return (*this);
}

bool ChunkMeshRenderer::transform_triangle(const Camera &camera,
	const RenderCache &render_cache, const WorldChunk &world_chunk,
	const chunk_mesh_vertex source_vertices[3],
	ClipVertex transformed_vertices[3]) const
{
	const int32_t wx = world_chunk.world_x;
	const int32_t wz = world_chunk.world_z;

	bool v0 = MeshProjector::transform_vertex(camera, source_vertices[0], wx,
			wz, render_cache, &transformed_vertices[0]);
	bool v1 = MeshProjector::transform_vertex(camera, source_vertices[1], wx,
			wz, render_cache, &transformed_vertices[1]);
	bool v2 = MeshProjector::transform_vertex(camera, source_vertices[2], wx,
			wz, render_cache, &transformed_vertices[2]);
	return (v0 && v1 && v2);
}

bool ChunkMeshRenderer::project_and_draw(ft_render_framebuffer &framebuffer,
	std::vector<double> &depth_buffer, const RenderCache &render_cache,
	const chunk_mesh_vertex source_vertices[3],
	const ClipVertex clipped_vertices[4], size_t n) const
{
	ScreenVertex sv[4];
	for (size_t i = 0; i < n; ++i)
		if (!MeshProjector::project_vertex(render_cache, clipped_vertices[i],
				&sv[i]))
			return (false);
	rasterizer->draw_triangle(framebuffer, depth_buffer, sv,
		source_vertices[0].block_id, source_vertices[0].face);
	return (true);
}

bool ChunkMeshRenderer::draw_triangle_pair(ft_render_framebuffer &framebuffer,
	std::vector<double> &depth_buffer, const RenderCache &render_cache,
	const chunk_mesh_vertex source_vertices[3],
	const ClipVertex clipped_vertices[4], size_t clipped_count) const
{
	if (clipped_count == 3U)
		return (project_and_draw(framebuffer, depth_buffer, render_cache,
				source_vertices, clipped_vertices, 3U));
	if (clipped_count == 4U)
	{
		ScreenVertex sv[4];
		for (size_t i = 0; i < 4U; ++i)
			if (!MeshProjector::project_vertex(render_cache,
					clipped_vertices[i], &sv[i]))
				return (false);
		rasterizer->draw_triangle(framebuffer, depth_buffer, sv,
			source_vertices[0].block_id, source_vertices[0].face);
		sv[1] = sv[2];
		sv[2] = sv[3];
		rasterizer->draw_triangle(framebuffer, depth_buffer, sv,
			source_vertices[0].block_id, source_vertices[0].face);
		return (true);
	}
	return (false);
}

void ChunkMeshRenderer::process_triangle(ft_render_framebuffer &framebuffer,
	std::vector<double> &depth_buffer, const Camera &camera,
	const RenderCache &render_cache, const WorldChunk &world_chunk,
	const chunk_mesh_vertex source_vertices[3],
	ClipVertex transformed_vertices[3]) const
{
	if (!transform_triangle(camera, render_cache, world_chunk, source_vertices,
			transformed_vertices))
		return ;

	bool v0_front = transformed_vertices[0].view_z >= NEAR_PLANE;
	bool v1_front = transformed_vertices[1].view_z >= NEAR_PLANE;
	bool v2_front = transformed_vertices[2].view_z >= NEAR_PLANE;
	bool all_in_front = v0_front && v1_front && v2_front;

	if (all_in_front)
	{
		ScreenVertex sv[3];
		bool p0 = MeshProjector::project_vertex(render_cache,
				transformed_vertices[0], &sv[0]);
		bool p1 = MeshProjector::project_vertex(render_cache,
				transformed_vertices[1], &sv[1]);
		bool p2 = MeshProjector::project_vertex(render_cache,
				transformed_vertices[2], &sv[2]);
		if (p0 && p1 && p2)
			rasterizer->draw_triangle(framebuffer, depth_buffer, sv,
				source_vertices[0].block_id, source_vertices[0].face);
	}
	else
	{
		ClipVertex clipped[4];
		size_t n = MeshClipper::clip_near(transformed_vertices, clipped);
		draw_triangle_pair(framebuffer, depth_buffer, render_cache,
			source_vertices, clipped, n);
	}
}

bool ChunkMeshRenderer::draw_mesh(ft_render_framebuffer &framebuffer,
	std::vector<double> &depth_buffer, const Camera &camera,
	const RenderCache &render_cache, const WorldChunk &world_chunk) const
{
	if (!world_chunk.initialized)
		return (false);
	if (!MeshCuller::chunk_is_visible(camera, world_chunk, render_cache))
		return (false);
	return (this->draw_mesh_visible(framebuffer, depth_buffer, camera,
			render_cache, world_chunk));
}

bool ChunkMeshRenderer::draw_mesh_visible(ft_render_framebuffer &framebuffer,
	std::vector<double> &depth_buffer, const Camera &camera,
	const RenderCache &render_cache, const WorldChunk &world_chunk) const
{
	if (!world_chunk.initialized)
		return (false);
	ClipVertex transformed[3];
	size_t idx = 0U;
	while (idx + 2U < world_chunk.mesh.indices.size())
	{
		chunk_mesh_vertex sv[3];
		sv[0] = world_chunk.mesh.vertices[world_chunk.mesh.indices[idx]];
		sv[1] = world_chunk.mesh.vertices[world_chunk.mesh.indices[idx + 1U]];
		sv[2] = world_chunk.mesh.vertices[world_chunk.mesh.indices[idx + 2U]];
		if (!MeshCuller::triangle_faces_camera(camera, world_chunk, sv))
		{
			idx += 3U;
			continue;
		}
		process_triangle(framebuffer, depth_buffer, camera, render_cache,
			world_chunk, sv, transformed);
		idx += 3U;
	}
	return (true);
}
