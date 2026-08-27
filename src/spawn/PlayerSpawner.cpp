#include "../../src/spawn/PlayerSpawner.hpp"

PlayerSpawner::PlayerSpawner()
{
}

PlayerSpawner::PlayerSpawner(const PlayerSpawner &other)
{
	*this = other;
}

PlayerSpawner::~PlayerSpawner()
{
}

PlayerSpawner &PlayerSpawner::operator=(const PlayerSpawner &other)
{
	(void)other;
	return (*this);
}

bool PlayerSpawner::try_spawn_at(Camera *camera, const World &world,
	double candidate_x, double candidate_z)
{
	double	ground_eye_y;

	if (PlayerGround::ground_eye_y_at(world, candidate_x, candidate_z,
			&ground_eye_y) == false)
		return (false);
	if (PlayerCollision::player_box_intersects_world(world, candidate_x,
			ground_eye_y, candidate_z) == true)
		return (false);
	camera->x = candidate_x;
	camera->z = candidate_z;
	camera->y = ground_eye_y;
	camera->vertical_velocity = 0.0;
	camera->on_ground = true;
	return (true);
}

bool PlayerSpawner::scan_spawn_ring(Camera *camera, const World &world,
	int32_t origin_x, int32_t origin_z, int32_t radius)
{
	int32_t	offset_x;
	int32_t	offset_z;
	double	candidate_x;
	double	candidate_z;

	offset_z = -radius;
	while (offset_z <= radius)
	{
		offset_x = -radius;
		while (offset_x <= radius)
		{
			if (offset_x == -radius || offset_x == radius || offset_z == -radius
				|| offset_z == radius)
			{
				candidate_x = static_cast<double>(origin_x + offset_x) + 0.5;
				candidate_z = static_cast<double>(origin_z + offset_z) + 0.5;
				if (try_spawn_at(camera, world, candidate_x, candidate_z))
					return (true);
			}
			offset_x = offset_x + 1;
		}
		offset_z = offset_z + 1;
	}
	return (false);
}

void PlayerSpawner::spawn_player_on_ground(Camera *camera, const World &world)
{
	int32_t	origin_x;
	int32_t	origin_z;
	int32_t	radius;

	if (camera == nullptr)
		return ;
	origin_x = static_cast<int32_t>(std::floor(camera->x));
	origin_z = static_cast<int32_t>(std::floor(camera->z));
	radius = 0;
	while (radius <= 8)
	{
		if (scan_spawn_ring(camera, world, origin_x, origin_z, radius))
			return ;
		radius = radius + 1;
	}
	PlayerGround::drop_camera_to_ground(camera, world);
}
