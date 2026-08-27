#include "../../src/meshes/MeshCuller.hpp"

MeshCuller::MeshCuller()
{
}
MeshCuller::MeshCuller(const MeshCuller &other)
{
	(void)other;
}
MeshCuller::~MeshCuller()
{
}
MeshCuller &MeshCuller::operator=(const MeshCuller &other)
{
	(void)other;
	return (*this);
}

void MeshCuller::face_normal(uint8_t face, double &nx, double &ny, double &nz)
{
	nx = 0.0;
	ny = 0.0;
	nz = 0.0;
	if (face == CHUNK_MESH_FACE_WEST)
		nx = -1.0;
	else if (face == CHUNK_MESH_FACE_EAST)
		nx = 1.0;
	else if (face == CHUNK_MESH_FACE_DOWN)
		ny = -1.0;
	else if (face == CHUNK_MESH_FACE_UP)
		ny = 1.0;
	else if (face == CHUNK_MESH_FACE_NORTH)
		nz = -1.0;
	else
		nz = 1.0;
}

bool MeshCuller::triangle_faces_camera(const Camera &camera,
	const WorldChunk &world_chunk, const chunk_mesh_vertex triangle_vertices[3])
{
	const chunk_mesh_vertex &vertex = triangle_vertices[0];
	if (vertex.face == CHUNK_MESH_FACE_WEST)
		return (camera.x < static_cast<double>(world_chunk.world_x
				+ vertex.coordinate_x));
	if (vertex.face == CHUNK_MESH_FACE_EAST)
		return (camera.x > static_cast<double>(world_chunk.world_x
				+ vertex.coordinate_x));
	if (vertex.face == CHUNK_MESH_FACE_DOWN)
		return (camera.y < static_cast<double>(vertex.coordinate_y));
	if (vertex.face == CHUNK_MESH_FACE_UP)
		return (camera.y > static_cast<double>(vertex.coordinate_y));
	if (vertex.face == CHUNK_MESH_FACE_NORTH)
		return (camera.z < static_cast<double>(world_chunk.world_z
				+ vertex.coordinate_z));
	return (camera.z > static_cast<double>(world_chunk.world_z
			+ vertex.coordinate_z));
}

void MeshCuller::chunk_corner_in_view(const Camera &camera,
	const WorldChunk &wc, const RenderCache &cache, double local_x,
	double local_y, double local_z, double &view_x, double &pitched_y,
	double &pitched_z)
{
	double	cx;
	double	cy;
	double	cz;
	double	vz;

	cx = static_cast<double>(wc.world_x) + local_x - camera.x;
	cy = local_y - camera.y;
	cz = static_cast<double>(wc.world_z) + local_z - camera.z;
	view_x = cx * cache.yaw_cos - cz * cache.yaw_sin;
	vz = cx * cache.yaw_sin + cz * cache.yaw_cos;
	pitched_y = cy * cache.pitch_cos - vz * cache.pitch_sin;
	pitched_z = cy * cache.pitch_sin + vz * cache.pitch_cos;
}

bool MeshCuller::chunk_aabb_is_visible(const Camera &camera,
	const WorldChunk &world_chunk, const RenderCache &cache)
{
	const double	xs[2] = {0.0, static_cast<double>(GAME_VOXEL_CHUNK_WIDTH)};
	const double	ys[2] = {0.0, static_cast<double>(GAME_VOXEL_CHUNK_HEIGHT)};
	const double	zs[2] = {0.0, static_cast<double>(GAME_VOXEL_CHUNK_DEPTH)};
	const double	horizontal_tangent = cache.fov_tangent * cache.aspect_ratio;
	bool			outside_near;
	bool			outside_far;
	bool			outside_left;
	bool			outside_right;
	bool			outside_bottom;
	bool			outside_top;
	double			view_x;
	double			pitched_y;
	double			pitched_z;

	outside_near = true;
	outside_far = true;
	outside_left = true;
	outside_right = true;
	outside_bottom = true;
	outside_top = true;
	for (int xi = 0; xi < 2; ++xi)
	{
		for (int yi = 0; yi < 2; ++yi)
		{
			for (int zi = 0; zi < 2; ++zi)
			{
				chunk_corner_in_view(camera, world_chunk, cache, xs[xi], ys[yi],
					zs[zi], view_x, pitched_y, pitched_z);
				if (pitched_z >= 0.0)
					outside_near = false;
				if (pitched_z <= cache.render_distance)
					outside_far = false;
				if (view_x >= -pitched_z * horizontal_tangent)
					outside_left = false;
				if (view_x <= pitched_z * horizontal_tangent)
					outside_right = false;
				if (pitched_y >= -pitched_z * cache.fov_tangent)
					outside_bottom = false;
				if (pitched_y <= pitched_z * cache.fov_tangent)
					outside_top = false;
			}
		}
	}
	return (!(outside_near || outside_far || outside_left || outside_right
			|| outside_bottom || outside_top));
}

bool MeshCuller::chunk_is_visible(const Camera &camera,
	const WorldChunk &world_chunk, const RenderCache &cache)
{
	if (world_chunk.mesh.vertices.empty() == FT_TRUE)
		return (false);
	return (chunk_aabb_is_visible(camera, world_chunk, cache));
}
