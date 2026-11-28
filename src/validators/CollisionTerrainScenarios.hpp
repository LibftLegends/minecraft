#ifndef COLLISION_VOXEL_SCENARIOS_HPP
# define COLLISION_VOXEL_SCENARIOS_HPP

# include "../../src/validators/CollisionValidator.hpp"

class CollisionTerrainScenarios
{
  public:
	CollisionTerrainScenarios();
	CollisionTerrainScenarios(const CollisionTerrainScenarios &other);
	~CollisionTerrainScenarios();
	CollisionTerrainScenarios &operator=(const CollisionTerrainScenarios &other);

	static int test_step_climb(World &world, Camera &camera,
		CollisionValidator::Metrics &m);
	static int test_tunnel(World &world, Camera &camera,
		CollisionValidator::Metrics &m);
	static int test_drop(World &world, Camera &camera,
		CollisionValidator::Metrics &m);

  private:
	static int setup_step_blocks(World &world, Camera &camera, int32_t &step_x,
		int32_t &step_y, int32_t &step_z);
	static bool run_climb_loop(Camera &camera, World &world, double start_y,
		double &climb_delta_y);
	static void setup_tunnel(World &world, Camera &camera, int32_t &tx,
		int32_t &ty, int32_t &tz);
	static void setup_drop(World &world, Camera &camera, double &drop_start_y);
};

#endif
