#include "../../src/physics/PlayerGround.hpp"

PlayerGround::PlayerGround()
{
}

PlayerGround::PlayerGround(const PlayerGround &other)
{
	*this = other;
}

PlayerGround::~PlayerGround()
{
}

PlayerGround &PlayerGround::operator=(const PlayerGround &other)
{
	(void)other;
	return (*this);
}

bool PlayerGround::ground_eye_y_at(const World &world, double x, double z,
	double *ground_eye_y)
{
	double	surface_top;
	int32_t	block_x;
	int32_t	block_z;

	if (ground_eye_y == nullptr)
		return (false);
	block_x = static_cast<int32_t>(std::floor(x));
	block_z = static_cast<int32_t>(std::floor(z));
	if (world.surface_top_at(block_x, block_z, &surface_top) == true)
	{
		*ground_eye_y = surface_top + PlayerGeometry::PLAYER_EYE_HEIGHT;
		return (true);
	}
	return (false);
}

void PlayerGround::drop_camera_to_ground(Camera *camera, const World &world)
{
	double	ground_eye_y;

	if (camera == nullptr)
		return ;
	if (PlayerGround::ground_eye_y_at(world, camera->x, camera->z,
			&ground_eye_y) == true)
	{
		camera->y = ground_eye_y;
		camera->vertical_velocity = 0.0;
		camera->on_ground = true;
	}
}
