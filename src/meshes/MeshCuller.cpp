#include "../../src/meshes/MeshCuller.hpp"
#include <cmath>

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
    for (int i = 0; i < RenderCache::FRUSTUM_PLANE_COUNT; ++i)
    {
        const RenderCache::FrustumPlane &plane = cache.frustum_planes[i];
        const double positive_x = plane.x >= 0.0
            ? static_cast<double>(world_chunk.world_x + GAME_VOXEL_CHUNK_WIDTH)
            : static_cast<double>(world_chunk.world_x);
        const double positive_y = plane.y >= 0.0
            ? static_cast<double>(GAME_VOXEL_CHUNK_HEIGHT) : 0.0;
        const double positive_z = plane.z >= 0.0
            ? static_cast<double>(world_chunk.world_z + GAME_VOXEL_CHUNK_DEPTH)
            : static_cast<double>(world_chunk.world_z);
        const double furthest_point = plane.x * positive_x
            + plane.y * positive_y + plane.z * positive_z + plane.offset;
        if (furthest_point < 0.0)
            return false;
    }
    (void)camera;
    return true;
}

bool MeshCuller::chunk_is_visible(const Camera &camera,
	const WorldChunk &world_chunk, const RenderCache &cache)
{
	double	closest_x;
	double	closest_z;
	double	distance_squared;
	double	fallback_distance;

	if (world_chunk.mesh.has_occupied_bounds == FT_FALSE)
		return (false);
	if (chunk_aabb_is_visible(camera, world_chunk, cache))
		return (true);
	/* Streaming must not make a newly generated nearby chunk disappear just
	 * because a frustum plane is numerically conservative at a boundary. The
	 * horizontal distance test is deliberately less selective, but it is a
	 * safe fallback: loaded chunks near the player remain drawable and the
	 * regular AABB test still handles the common path. */
	closest_x = camera.x;
	if (closest_x < static_cast<double>(world_chunk.world_x))
		closest_x = static_cast<double>(world_chunk.world_x);
	else if (closest_x > static_cast<double>(world_chunk.world_x
		+ GAME_VOXEL_CHUNK_WIDTH))
		closest_x = static_cast<double>(world_chunk.world_x
			+ GAME_VOXEL_CHUNK_WIDTH);
	closest_z = camera.z;
	if (closest_z < static_cast<double>(world_chunk.world_z))
		closest_z = static_cast<double>(world_chunk.world_z);
	else if (closest_z > static_cast<double>(world_chunk.world_z
		+ GAME_VOXEL_CHUNK_DEPTH))
		closest_z = static_cast<double>(world_chunk.world_z
			+ GAME_VOXEL_CHUNK_DEPTH);
	distance_squared = (closest_x - camera.x) * (closest_x - camera.x)
		+ (closest_z - camera.z) * (closest_z - camera.z);
	fallback_distance = cache.render_distance
		+ std::sqrt(static_cast<double>(GAME_VOXEL_CHUNK_WIDTH
			* GAME_VOXEL_CHUNK_WIDTH + GAME_VOXEL_CHUNK_DEPTH
			* GAME_VOXEL_CHUNK_DEPTH));
	return (distance_squared <= fallback_distance * fallback_distance);
}
