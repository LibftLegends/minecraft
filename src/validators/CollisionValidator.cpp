#include "../../src/validators/CollisionValidator.hpp"
#include <cmath>

CollisionValidator::CollisionValidator()
{
}

CollisionValidator::CollisionValidator(const CollisionValidator &other)
	: IValidator(other)
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
		VOXEL_GENERATOR_STONE_BLOCK);
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

int CollisionValidator::test_raycast_tied_case(World &world, int32_t edge_x,
	int32_t edge_y, int32_t edge_z, int32_t target_x, int32_t target_y,
	int32_t target_z, double direction_x, double direction_y,
	double direction_z, double origin_y, double max_distance)
{
	int32_t hit_x;
	int32_t hit_y;
	int32_t hit_z;
	int32_t error_code;

	hit_x = 0;
	hit_y = 0;
	hit_z = 0;
	error_code = world.place_block_at(edge_x, edge_y, edge_z,
		VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code != FT_ERR_SUCCESS)
		return (1);
	error_code = world.raycast_solid(0.1, origin_y,
		0.1, direction_x, direction_y, direction_z, max_distance,
		&hit_x, &hit_y, &hit_z);
	if (error_code != FT_ERR_INVALID_ARGUMENT)
	{
		std::fprintf(stderr,
			"collision: tied-boundary ray incorrectly hit edge (%d,%d,%d) "
			"error=%d\n", hit_x, hit_y, hit_z, error_code);
		if (world.delete_block_at(edge_x, edge_y, edge_z) != FT_ERR_SUCCESS)
			return (1);
		return (1);
	}
	if (world.delete_block_at(edge_x, edge_y, edge_z) != FT_ERR_SUCCESS)
		return (1);
	error_code = world.place_block_at(target_x, target_y, target_z,
		VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code != FT_ERR_SUCCESS)
		return (1);
	error_code = world.raycast_solid(0.1, origin_y,
		0.1, direction_x, direction_y, direction_z, max_distance,
		&hit_x, &hit_y, &hit_z);
	if (error_code != FT_ERR_SUCCESS || hit_x != target_x
		|| hit_y != target_y || hit_z != target_z)
	{
		std::fprintf(stderr,
			"collision: tied-boundary ray missed target error=%d "
			"hit=(%d,%d,%d)\n", error_code, hit_x, hit_y, hit_z);
		if (world.delete_block_at(target_x, target_y, target_z)
			!= FT_ERR_SUCCESS)
			return (1);
		return (1);
	}
	if (world.delete_block_at(target_x, target_y, target_z) != FT_ERR_SUCCESS)
		return (1);
	return (0);
}

int CollisionValidator::test_raycast_tied_boundary(World &world)
{
	const double diagonal = 0.7071067811865475;
	const double corner = 0.5773502691896258;

	if (test_raycast_tied_case(world, 1, 250, 0, 1, 251, 0,
			diagonal, diagonal, 0.0, 250.1, 2.0) != 0)
		return (1);
	if (test_raycast_tied_case(world, 1, 250, 0, 1, 250, 1,
			diagonal, 0.0, diagonal, 250.1, 2.0) != 0)
		return (1);
	if (test_raycast_tied_case(world, 0, 251, 0, 0, 251, 1,
			0.0, diagonal, diagonal, 250.1, 2.0) != 0)
		return (1);
	return (test_raycast_tied_case(world, 1, 250, 0, 1, 251, 1,
			corner, corner, corner, 250.1, 2.5));
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
		|| test_raycast_tied_boundary(world) != 0
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
