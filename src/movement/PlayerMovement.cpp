#include "../../src/movement/PlayerMovement.hpp"

PlayerMovement::PlayerMovement()
{
}

PlayerMovement::PlayerMovement(const PlayerMovement &other)
{
	*this = other;
}

PlayerMovement::~PlayerMovement()
{
}

PlayerMovement &PlayerMovement::operator=(const PlayerMovement &other)
{
	(void)other;
	return (*this);
}

bool PlayerMovement::player_in_water(const Camera &camera, const World &world)
{
	uint32_t	blk;
	int32_t		ex;
	int32_t		ey;
	int32_t		ez;
	int32_t		fy;

	ex = static_cast<int32_t>(std::floor(camera.x));
	ey = static_cast<int32_t>(std::floor(camera.y - 0.1));
	ez = static_cast<int32_t>(std::floor(camera.z));
	blk = 0U;
	if (world.block_id_at(ex, ey, ez, &blk)
		&& blk == VOXEL_GENERATOR_WATER_BLOCK)
		return (true);
	fy = static_cast<int32_t>(std::floor(camera.y - 1.5));
	blk = 0U;
	return (world.block_id_at(ex, fy, ez, &blk)
		&& blk == VOXEL_GENERATOR_WATER_BLOCK);
}

PlayerMovement::LocalDirection PlayerMovement::local_direction_from_input(
	const CameraInput &input)
{
	LocalDirection	direction;
	double			move_length;

	direction.x = 0.0;
	direction.z = 0.0;
	if (input.move_forward == true)
		direction.z = direction.z + 1.0;
	if (input.move_backward == true)
		direction.z = direction.z - 1.0;
	if (input.move_right == true)
		direction.x = direction.x + 1.0;
	if (input.move_left == true)
		direction.x = direction.x - 1.0;
	move_length = (direction.x * direction.x) + (direction.z * direction.z);
	if (move_length > 1.0)
	{
		direction.x = direction.x * 0.7071067811865475;
		direction.z = direction.z * 0.7071067811865475;
	}
	return (direction);
}

PlayerMovement::MovementDelta PlayerMovement::camera_horizontal_delta(
	const Camera &camera, const CameraInput &input, double delta_seconds,
	double speed_scale)
{
	LocalDirection	direction;
	MovementDelta	delta;
	double			distance;
	double			yaw_cos;
	double			yaw_sin;
	double			speed;

	delta.x = 0.0;
	delta.z = 0.0;
	if (delta_seconds <= 0.0)
		return (delta);
	direction = local_direction_from_input(input);
	speed = camera.speed * speed_scale;
	if (input.speed_boost == true)
		speed = speed * 20.0;
	distance = speed * delta_seconds;
	yaw_cos = std::cos(camera.yaw);
	yaw_sin = std::sin(camera.yaw);
	delta.x = ((direction.x * yaw_cos) + (direction.z * yaw_sin)) * distance;
	delta.z = ((direction.z * yaw_cos) - (direction.x * yaw_sin)) * distance;
	return (delta);
}

PlayerMovement::CameraSnapshot PlayerMovement::snapshot_camera(
	const Camera &camera)
{
	CameraSnapshot	snapshot;

	snapshot.x = camera.x;
	snapshot.y = camera.y;
	snapshot.z = camera.z;
	return (snapshot);
}

void PlayerMovement::restore_camera(Camera *camera,
	const CameraSnapshot &snapshot)
{
	if (camera == nullptr)
		return ;
	camera->x = snapshot.x;
	camera->y = snapshot.y;
	camera->z = snapshot.z;
}

bool PlayerMovement::move_axis_with_water_step(Camera *camera,
	const World &world, int axis, double delta)
{
	CameraSnapshot	snapshot;

	snapshot = snapshot_camera(*camera);
	if (PlayerCollision::move_player_axis(camera, world, axis, delta) == false)
		return (false);
	restore_camera(camera, snapshot);
	PlayerCollision::move_player_axis(camera, world, 1, 1.05);
	if (PlayerCollision::move_player_axis(camera, world, axis, delta) == false)
		return (true);
	restore_camera(camera, snapshot);
	return (false);
}

void PlayerMovement::update_player_horizontal_motion(Camera *camera,
	const CameraInput &input, const World &world, double delta_seconds)
{
	MovementDelta	delta;
	bool			in_water;
	bool			can_step;
	bool			stepped;

	if (camera == nullptr || delta_seconds <= 0.0)
		return ;
	camera->update_orientation(input, delta_seconds);
	in_water = player_in_water(*camera, world);
	delta = camera_horizontal_delta(*camera, input, delta_seconds,
			in_water ? 0.5 : 1.0);
	can_step = in_water && !input.speed_boost;
	stepped = false;
	if (can_step == true)
		stepped = move_axis_with_water_step(camera, world, 0, delta.x);
	else
		PlayerCollision::move_player_axis(camera, world, 0, delta.x);
	if (can_step == true && stepped == false)
		move_axis_with_water_step(camera, world, 2, delta.z);
	else
		PlayerCollision::move_player_axis(camera, world, 2, delta.z);
}

void PlayerMovement::update_water_velocity(Camera *camera,
	const CameraInput &input, double delta_seconds)
{
	const double	water_gravity = 3.0;
	const double	water_drag = 4.0;
	const double	water_swim = 6.0;
	double			accel;

	accel = -water_gravity;
	if (input.jump_held == true)
		accel = accel + water_swim;
	accel = accel - (water_drag * camera->vertical_velocity);
	camera->vertical_velocity = camera->vertical_velocity + (accel
			* delta_seconds);
	if (camera->vertical_velocity > 2.0)
		camera->vertical_velocity = 2.0;
	if (camera->vertical_velocity < -2.0)
		camera->vertical_velocity = -2.0;
	camera->on_ground = false;
}

void PlayerMovement::update_air_velocity(Camera *camera,
	const CameraInput &input, double delta_seconds)
{
	const double	jump_speed = 9.9;
	const double	gravity = 32.0;
	const double	max_fall = -60.0;

	if (camera->on_ground == true && input.jump_pressed == true)
	{
		camera->vertical_velocity = jump_speed;
		camera->on_ground = false;
	}
	camera->vertical_velocity = camera->vertical_velocity - (gravity
			* delta_seconds);
	if (camera->vertical_velocity < max_fall)
		camera->vertical_velocity = max_fall;
}

void PlayerMovement::apply_vertical_motion(Camera *camera, const World &world,
	double delta_seconds, bool in_water)
{
	double	delta_y;
	double	previous_y;
	bool	collided;

	delta_y = camera->vertical_velocity * delta_seconds;
	previous_y = camera->y;
	collided = PlayerCollision::move_player_axis(camera, world, 1, delta_y);
	if (collided == false)
	{
		camera->on_ground = false;
		return ;
	}
	camera->on_ground = !in_water && camera->vertical_velocity <= 0.0
		&& camera->y >= previous_y + delta_y;
	camera->vertical_velocity = 0.0;
}

void PlayerMovement::update_player_vertical_motion(Camera *camera,
	const CameraInput &input, const World &world, double delta_seconds)
{
	bool	in_water;

	if (camera == nullptr || delta_seconds <= 0.0)
		return ;
	in_water = player_in_water(*camera, world);
	if (in_water == true)
		update_water_velocity(camera, input, delta_seconds);
	else
		update_air_velocity(camera, input, delta_seconds);
	apply_vertical_motion(camera, world, delta_seconds, in_water);
}
