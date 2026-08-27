#include "../../src/player/PlayerController.hpp"

PlayerController::PlayerController()
{
}

PlayerController::PlayerController(const PlayerController &other)
{
	*this = other;
}

PlayerController::~PlayerController()
{
}

PlayerController &PlayerController::operator=(const PlayerController &other)
{
	(void)other;
	return (*this);
}

void PlayerController::camera_forward(const Camera &camera, double *direction_x,
	double *direction_y, double *direction_z)
{
	PlayerGeometry::camera_forward(camera, direction_x, direction_y,
		direction_z);
}

bool PlayerController::block_overlaps_player(const Camera &camera,
	int32_t block_x, int32_t block_y, int32_t block_z)
{
	return (PlayerGeometry::block_overlaps_player(camera, block_x, block_y,
			block_z));
}

bool PlayerController::ground_eye_y_at(const World &world, double x, double z,
	double *ground_eye_y)
{
	return (PlayerGround::ground_eye_y_at(world, x, z, ground_eye_y));
}

void PlayerController::drop_camera_to_ground(Camera *camera, const World &world)
{
	PlayerGround::drop_camera_to_ground(camera, world);
}

bool PlayerController::player_box_intersects_world(const World &world, double x,
	double eye_y, double z)
{
	return (PlayerCollision::player_box_intersects_world(world, x, eye_y, z));
}

void PlayerController::spawn_player_on_ground(Camera *camera,
	const World &world)
{
	PlayerSpawner::spawn_player_on_ground(camera, world);
}

void PlayerController::update_player_horizontal_motion(Camera *camera,
	const CameraInput &input, const World &world, double delta_seconds)
{
	PlayerMovement::update_player_horizontal_motion(camera, input, world,
		delta_seconds);
}

void PlayerController::update_player_vertical_motion(Camera *camera,
	const CameraInput &input, const World &world, double delta_seconds)
{
	PlayerMovement::update_player_vertical_motion(camera, input, world,
		delta_seconds);
}
