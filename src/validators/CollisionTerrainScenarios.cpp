#include "../../src/validators/CollisionTerrainScenarios.hpp"

CollisionTerrainScenarios::CollisionTerrainScenarios()
{
}

CollisionTerrainScenarios::CollisionTerrainScenarios(const CollisionTerrainScenarios &other)
{
	(void)other;
}

CollisionTerrainScenarios::~CollisionTerrainScenarios()
{
}

CollisionTerrainScenarios &CollisionTerrainScenarios::operator=(const CollisionTerrainScenarios &other)
{
	(void)other;
	return (*this);
}

int CollisionTerrainScenarios::setup_step_blocks(World &world, Camera &camera,
	int32_t &step_x, int32_t &step_y, int32_t &step_z)
{
	int32_t	frame;
	int32_t	error_code;

	camera.initialize();
	PlayerController::spawn_player_on_ground(&camera, world);
	step_x = static_cast<int32_t>(std::floor(camera.x));
	step_z = static_cast<int32_t>(std::floor(camera.z + 1.0));
	step_y = static_cast<int32_t>(std::floor(camera.y
				- PlayerController::PLAYER_EYE_HEIGHT));
	frame = 0;
	while (frame < 4)
	{
		error_code = world.place_block_at(step_x, step_y, step_z + frame,
				VOXEL_GENERATOR_STONE_BLOCK);
		if (error_code != FT_ERR_SUCCESS && error_code != FT_ERR_ALREADY_EXISTS)
			return (ApplicationError::fail("collision step setup", error_code));
		frame = frame + 1;
	}
	return (0);
}

bool CollisionTerrainScenarios::run_climb_loop(Camera &camera, World &world,
	double start_y, double &climb_delta_y)
{
	CameraInput	input;
	int32_t		frame;
	bool		climbed;

	input = InputReader::empty_camera_input();
	input.move_forward = true;
	input.jump_pressed = true;
	frame = 0;
	climbed = false;
	climb_delta_y = 0.0;
	while (frame < 120)
	{
		PlayerController::update_player_vertical_motion(&camera, input, world,
			1.0 / 60.0);
		PlayerController::update_player_horizontal_motion(&camera, input, world,
			1.0 / 60.0);
		if (camera.y - start_y > climb_delta_y)
			climb_delta_y = camera.y - start_y;
		if (camera.y >= start_y + 0.75 && camera.on_ground == true)
			climbed = true;
		input.jump_pressed = false;
		frame = frame + 1;
	}
	return (climbed);
}

int CollisionTerrainScenarios::test_step_climb(World &world, Camera &camera,
	CollisionValidator::Metrics &m)
{
	int32_t	step_x;
	int32_t	step_y;
	int32_t	step_z;
	double	climb_start_y;

	if (setup_step_blocks(world, camera, step_x, step_y, step_z) != 0)
		return (1);
	climb_start_y = camera.y;
	if (run_climb_loop(camera, world, climb_start_y, m.climb_delta_y) == false)
	{
		std::fprintf(stderr,
						"collision: failed one-block climb check"
						" y=%.2f start=%.2f ground=%d\n",
						camera.y,
						climb_start_y,
						camera.on_ground == true ? 1 : 0);
		return (1);
	}
	return (0);
}

void CollisionTerrainScenarios::setup_tunnel(World &world, Camera &camera,
	int32_t &tx, int32_t &ty, int32_t &tz)
{
	int32_t	frame;

	camera.initialize();
	PlayerController::spawn_player_on_ground(&camera, world);
	tx = static_cast<int32_t>(std::floor(camera.x));
	ty = static_cast<int32_t>(std::floor(camera.y
				- PlayerController::PLAYER_EYE_HEIGHT));
	tz = static_cast<int32_t>(std::floor(camera.z));
	frame = 0;
	while (frame < 6)
	{
		(void)world.delete_block_at(tx, ty, tz + frame);
		(void)world.delete_block_at(tx, ty + 1, tz + frame);
		(void)world.place_block_at(tx - 1, ty, tz + frame,
			VOXEL_GENERATOR_STONE_BLOCK);
		(void)world.place_block_at(tx - 1, ty + 1, tz + frame,
			VOXEL_GENERATOR_STONE_BLOCK);
		(void)world.place_block_at(tx + 1, ty, tz + frame,
			VOXEL_GENERATOR_STONE_BLOCK);
		(void)world.place_block_at(tx + 1, ty + 1, tz + frame,
			VOXEL_GENERATOR_STONE_BLOCK);
		frame = frame + 1;
	}
}

int CollisionTerrainScenarios::test_tunnel(World &world, Camera &camera,
	CollisionValidator::Metrics &m)
{
	CameraInput	input;
	int32_t		tx;
	int32_t		ty;
	int32_t		tz;
	double		tunnel_start_z;
	int32_t		frame;

	setup_tunnel(world, camera, tx, ty, tz);
	tunnel_start_z = camera.z;
	input = InputReader::empty_camera_input();
	input.move_forward = true;
	frame = 0;
	while (frame < 90)
	{
		PlayerController::update_player_vertical_motion(&camera, input, world,
			1.0 / 60.0);
		PlayerController::update_player_horizontal_motion(&camera, input, world,
			1.0 / 60.0);
		frame = frame + 1;
	}
	m.tunnel_delta_z = camera.z - tunnel_start_z;
	if (m.tunnel_delta_z < 1.0)
	{
		std::fprintf(stderr,
						"collision: failed one-block-wide tunnel check"
						" start=%.2f end=%.2f\n",
						tunnel_start_z,
						camera.z);
		return (1);
	}
	return (0);
}

void CollisionTerrainScenarios::setup_drop(World &world, Camera &camera,
	double &drop_start_y)
{
	int32_t	drop_x;
	int32_t	drop_y;
	int32_t	drop_z;
	int32_t	frame;

	camera.initialize();
	PlayerController::spawn_player_on_ground(&camera, world);
	drop_start_y = camera.y;
	drop_x = static_cast<int32_t>(std::floor(camera.x));
	drop_y = static_cast<int32_t>(std::floor(camera.y
				- PlayerController::PLAYER_EYE_HEIGHT)) - 1;
	drop_z = static_cast<int32_t>(std::floor(camera.z));
	frame = 0;
	while (frame < 5)
	{
		(void)world.delete_block_at(drop_x, drop_y - frame, drop_z);
		frame = frame + 1;
	}
	camera.on_ground = false;
	camera.vertical_velocity = 0.0;
}

int CollisionTerrainScenarios::test_drop(World &world, Camera &camera,
	CollisionValidator::Metrics &m)
{
	CameraInput	input;
	double		drop_start_y;
	int32_t		frame;

	setup_drop(world, camera, drop_start_y);
	input = InputReader::empty_camera_input();
	frame = 0;
	while (frame < 90)
	{
		PlayerController::update_player_vertical_motion(&camera, input, world,
			1.0 / 60.0);
		PlayerController::update_player_horizontal_motion(&camera, input, world,
			1.0 / 60.0);
		frame = frame + 1;
	}
	m.drop_delta_y = drop_start_y - camera.y;
	if (camera.y > drop_start_y - 1.0)
	{
		std::fprintf(stderr,
						"collision: failed one-block drop shaft check"
						" start_y=%.2f end_y=%.2f\n",
						drop_start_y,
						camera.y);
		return (1);
	}
	return (0);
}
