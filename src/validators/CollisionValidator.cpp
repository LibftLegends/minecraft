#include "../../src/validators/CollisionValidator.hpp"

CollisionValidator::CollisionValidator()
{
}

CollisionValidator::CollisionValidator(const CollisionValidator &other)
{
	*this = other;
}

CollisionValidator::~CollisionValidator()
{
}

CollisionValidator &CollisionValidator::operator=(const CollisionValidator &other)
{
	(void)other;
	return (*this);
}

int CollisionValidator::test_movement(World &world, Camera &camera, Metrics &m)
{
	CameraInput	input;

	camera.initialize();
	PlayerController::spawn_player_on_ground(&camera, world);
	m.start_x = camera.x;
	m.start_z = camera.z;
	input = InputReader::empty_camera_input();
	input.move_forward = true;
	PlayerController::update_player_horizontal_motion(&camera, input, world,
		1.0);
	m.movement_delta_z = camera.z - m.start_z;
	if (std::fabs(camera.z - m.start_z) < 0.25)
	{
		std::fprintf(stderr, "collision: failed movement sanity check\n");
		return (1);
	}
	return (0);
}

int CollisionValidator::test_wall(World &world, Camera &camera, Metrics &m)
{
	CameraInput	input;
	double		before_wall_x;

	before_wall_x = camera.x;
	(void)world.place_block_at(static_cast<int32_t>(std::floor(camera.x + 1.0)),
		static_cast<int32_t>(std::floor(camera.y - 1.0)),
		static_cast<int32_t>(std::floor(camera.z)),
		TERRAIN_GENERATOR_STONE_BLOCK);
	input = InputReader::empty_camera_input();
	input.move_right = true;
	PlayerController::update_player_horizontal_motion(&camera, input, world,
		0.25);
	m.wall_delta_x = camera.x - before_wall_x;
	if (camera.x > before_wall_x + 0.75)
	{
		std::fprintf(stderr, "collision: failed wall rejection check\n");
		return (1);
	}
	return (0);
}

int CollisionValidator::validate() const
{
	World world;
	Camera camera;
	Metrics m;
	int32_t error_code;

	error_code = world.initialize("integration-seed");
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("collision world initialization",
				error_code));
	if (test_movement(world, camera, m) != 0 || test_wall(world, camera, m) != 0
		|| CollisionTerrainScenarios::test_step_climb(world, camera, m) != 0
		|| CollisionTerrainScenarios::test_tunnel(world, camera, m) != 0
		|| CollisionTerrainScenarios::test_drop(world, camera, m) != 0)
	{
		world.destroy();
		return (1);
	}
	std::printf("collision: ok start=(%.2f,%.2f) moved_z=%.2f wall_x=%.2f"
				" climb_y=%.2f tunnel_z=%.2f drop_y=%.2f\n",
				m.start_x,
				m.start_z,
				m.movement_delta_z,
				m.wall_delta_x,
				m.climb_delta_y,
				m.tunnel_delta_z,
				m.drop_delta_y);
	world.destroy();
	return (0);
}
