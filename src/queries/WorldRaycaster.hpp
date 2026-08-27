#ifndef WORLD_RAYCASTER_HPP
# define WORLD_RAYCASTER_HPP

# include "../../src/queries/WorldBlockQuery.hpp"
# include "../ft_vox.hpp"

class	World;

class WorldRaycaster
{
  public:
	WorldRaycaster();
	WorldRaycaster(const WorldRaycaster &other);
	~WorldRaycaster();
	WorldRaycaster &operator=(const WorldRaycaster &other);

	static int32_t raycast_solid(const World &world, double origin_x,
		double origin_y, double origin_z, double direction_x,
		double direction_y, double direction_z, double max_distance,
		int32_t *block_x, int32_t *block_y, int32_t *block_z);
	static int32_t raycast_edit_target(const World &world, double origin_x,
		double origin_y, double origin_z, double direction_x,
		double direction_y, double direction_z, double max_distance,
		int32_t *hit_x, int32_t *hit_y, int32_t *hit_z, int32_t *place_x,
		int32_t *place_y, int32_t *place_z, uint32_t *hit_id);

  private:
	static constexpr double STEP_DISTANCE = 0.0625;

	static void compute_cell(double ox, double oy, double oz, double dx,
		double dy, double dz, double dist, int32_t &tx, int32_t &ty,
		int32_t &tz);
};

# include "../../src/world/World.hpp"

#endif
