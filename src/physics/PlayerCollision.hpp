#ifndef PLAYER_COLLISION_HPP
# define PLAYER_COLLISION_HPP

# include "../../src/camera/Camera.hpp"
# include "../../src/physics/PlayerGround.hpp"
# include "../../src/world/World.hpp"
# include "../ft_vox.hpp"

class PlayerCollision
{
  public:
	PlayerCollision();
	PlayerCollision(const PlayerCollision &other);
	~PlayerCollision();
	PlayerCollision &operator=(const PlayerCollision &other);

	static bool player_box_intersects_world(const World &world, double x,
		double eye_y, double z);
	static bool move_player_axis(Camera *camera, const World &world, int axis,
		double delta);

  private:
	static bool any_solid_in_range(const World &world, int32_t min_x,
		int32_t max_x, int32_t min_y, int32_t max_y, int32_t min_z,
		int32_t max_z);
	static void resolve_block_collision(int axis, double delta, int32_t bx,
		int32_t by, int32_t bz, double &next_x, double &next_y, double &next_z,
		bool &collided);
	static void scan_and_resolve(const World &world, int axis, double delta,
		int32_t min_x, int32_t max_x, int32_t min_y, int32_t max_y,
		int32_t min_z, int32_t max_z, double &next_x, double &next_y,
		double &next_z, bool &collided);
};

#endif
