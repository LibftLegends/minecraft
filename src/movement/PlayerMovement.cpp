#include "../../src/movement/PlayerMovement.hpp"
#include "../../src/physics/PlayerGeometry.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

static bool player_movement_crosses_revision_barrier(const Camera &camera,
	const World &world, int axis, double delta)
{
	int32_t start_min_chunk_x;
	int32_t start_max_chunk_x;
	int32_t start_min_chunk_z;
	int32_t start_max_chunk_z;
	const double half_width = PlayerGeometry::PLAYER_HALF_WIDTH;
	const auto get_chunk_coordinate = [](double position,
		double chunk_size, int32_t *chunk_out) -> bool
	{
		const double chunk_value = std::floor(position / chunk_size);
		if (chunk_out == nullptr
			|| chunk_value < std::numeric_limits<int32_t>::min()
			|| chunk_value > std::numeric_limits<int32_t>::max())
			return (false);
		*chunk_out = static_cast<int32_t>(chunk_value);
		return (true);
	};
	const auto overlaps_start_footprint = [&](int32_t chunk_x,
		int32_t chunk_z) -> bool
	{
		return (chunk_x >= start_min_chunk_x && chunk_x <= start_max_chunk_x
			&& chunk_z >= start_min_chunk_z && chunk_z <= start_max_chunk_z);
	};
	const auto footprint_hits_new_barrier = [&](double x, double z) -> bool
	{
		int32_t min_chunk_x;
		int32_t max_chunk_x;
		int32_t min_chunk_z;
		int32_t max_chunk_z;
		if (!get_chunk_coordinate(x - half_width,
				GAME_VOXEL_CHUNK_WIDTH, &min_chunk_x)
			|| !get_chunk_coordinate(x + half_width,
				GAME_VOXEL_CHUNK_WIDTH, &max_chunk_x)
			|| !get_chunk_coordinate(z - half_width,
				GAME_VOXEL_CHUNK_DEPTH, &min_chunk_z)
			|| !get_chunk_coordinate(z + half_width,
				GAME_VOXEL_CHUNK_DEPTH, &max_chunk_z))
			return (true);
		for (int64_t chunk_x = min_chunk_x; chunk_x <= max_chunk_x; ++chunk_x)
		{
			for (int64_t chunk_z = min_chunk_z;
				chunk_z <= max_chunk_z; ++chunk_z)
			{
				const int32_t candidate_chunk_x =
					static_cast<int32_t>(chunk_x);
				const int32_t candidate_chunk_z =
					static_cast<int32_t>(chunk_z);
				if (!overlaps_start_footprint(candidate_chunk_x,
						candidate_chunk_z)
					&& world.revision_barrier_blocks_chunk(candidate_chunk_x,
						candidate_chunk_z))
					return (true);
			}
		}
		return (false);
	};
	if (!world.revision_barriers_active() || delta == 0.0)
		return (false);
	if (!std::isfinite(delta) || !std::isfinite(camera.x)
		|| !std::isfinite(camera.z))
		return (true);
	if (!get_chunk_coordinate(camera.x - half_width,
			GAME_VOXEL_CHUNK_WIDTH, &start_min_chunk_x)
		|| !get_chunk_coordinate(camera.x + half_width,
			GAME_VOXEL_CHUNK_WIDTH, &start_max_chunk_x)
		|| !get_chunk_coordinate(camera.z - half_width,
			GAME_VOXEL_CHUNK_DEPTH, &start_min_chunk_z)
		|| !get_chunk_coordinate(camera.z + half_width,
			GAME_VOXEL_CHUNK_DEPTH, &start_max_chunk_z))
		return (true);
	const double steps_value = std::ceil(std::abs(delta) / 0.5);
	if (steps_value > 4096.0)
		return (true);
	const uint32_t steps = std::max(1U,
		static_cast<uint32_t>(steps_value));
	for (uint32_t index = 1U; index <= steps; ++index)
	{
		const double fraction = static_cast<double>(index) / steps;
		const double x = axis == 0 ? camera.x + delta * fraction : camera.x;
		const double z = axis == 2 ? camera.z + delta * fraction : camera.z;
		if (footprint_hits_new_barrier(x, z))
			return (true);
	}
	return (false);
}

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

bool PlayerMovement::player_on_ladder(const Camera &camera, const World &world)
{
	const int32_t x = static_cast<int32_t>(std::floor(camera.x));
	const int32_t z = static_cast<int32_t>(std::floor(camera.z));
	const int32_t feet_y = static_cast<int32_t>(std::floor(camera.y - 1.5));
	for (int32_t y = feet_y; y <= feet_y + 2; ++y)
	{
		uint32_t block = 0U;
		if (world.block_id_at(x, y, z, &block)
			&& block == VOXEL_GENERATOR_LADDER_BLOCK)
			return (true);
	}
	return (false);
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

	if (player_movement_crosses_revision_barrier(*camera, world, axis, delta))
		return (false);
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
	else if (!player_movement_crosses_revision_barrier(*camera, world,
			0, delta.x))
		PlayerCollision::move_player_axis(camera, world, 0, delta.x);
	if (can_step == true && stepped == false)
		move_axis_with_water_step(camera, world, 2, delta.z);
	else if (!player_movement_crosses_revision_barrier(*camera, world,
			2, delta.z))
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
	if (player_on_ladder(*camera, world))
	{
		camera->vertical_velocity = input.jump_held ? 4.0
			: (input.move_backward ? -4.0 : 0.0);
		camera->on_ground = false;
	}
	else if (in_water == true)
		update_water_velocity(camera, input, delta_seconds);
	else
		update_air_velocity(camera, input, delta_seconds);
	apply_vertical_motion(camera, world, delta_seconds, in_water);
}
