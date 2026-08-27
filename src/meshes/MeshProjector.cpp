#include "../../src/meshes/MeshProjector.hpp"

MeshProjector::MeshProjector()
{
}

MeshProjector::MeshProjector(const MeshProjector &other)
{
	*this = other;
}

MeshProjector::~MeshProjector()
{
}

MeshProjector &MeshProjector::operator=(const MeshProjector &other)
{
	(void)other;
	return (*this);
}

bool MeshProjector::transform_vertex(const Camera &camera,
	const chunk_mesh_vertex &vertex, int32_t origin_x, int32_t origin_z,
	const RenderCache &render_cache, ClipVertex *clip_vertex)
{
	double	world_x;
	double	world_y;
	double	world_z;
	double	translated_x;
	double	translated_y;
	double	translated_z;
	double	view_x;
	double	view_y;
	double	view_z;
	double	pitched_y;
	double	pitched_z;

	if (clip_vertex == nullptr)
		return (false);
	world_x = static_cast<double>(origin_x)
		+ static_cast<double>(vertex.coordinate_x);
	world_y = static_cast<double>(vertex.coordinate_y);
	world_z = static_cast<double>(origin_z)
		+ static_cast<double>(vertex.coordinate_z);
	translated_x = world_x - camera.x;
	translated_y = world_y - camera.y;
	translated_z = world_z - camera.z;
	view_x = (translated_x * render_cache.yaw_cos) - (translated_z
			* render_cache.yaw_sin);
	view_z = (translated_x * render_cache.yaw_sin) + (translated_z
			* render_cache.yaw_cos);
	view_y = translated_y;
	pitched_y = (view_y * render_cache.pitch_cos) - (view_z
			* render_cache.pitch_sin);
	pitched_z = (view_y * render_cache.pitch_sin) + (view_z
			* render_cache.pitch_cos);
	clip_vertex->view_x = view_x;
	clip_vertex->view_y = pitched_y;
	clip_vertex->view_z = pitched_z;
	clip_vertex->texture_u = static_cast<double>(vertex.texture_u);
	clip_vertex->texture_v = static_cast<double>(vertex.texture_v);
	return (true);
}

bool MeshProjector::project_vertex(const RenderCache &render_cache,
	const ClipVertex &clip_vertex, ScreenVertex *screen_vertex)
{
	double	inv_view_z;

	if (screen_vertex == nullptr || clip_vertex.view_z < NEAR_PLANE)
		return (false);
	inv_view_z = 1.0 / clip_vertex.view_z;
	screen_vertex->x = render_cache.half_width + ((clip_vertex.view_x
				* inv_view_z) * render_cache.focal_length);
	screen_vertex->y = render_cache.half_height - ((clip_vertex.view_y
				* inv_view_z) * render_cache.focal_length);
	screen_vertex->depth = clip_vertex.view_z;
	screen_vertex->texture_u = clip_vertex.texture_u;
	screen_vertex->texture_v = clip_vertex.texture_v;
	return (true);
}
