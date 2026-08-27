#ifndef PLAYER_GROUND_HPP
# define PLAYER_GROUND_HPP

# include "../../src/physics/PlayerGeometry.hpp"
# include "../../src/world/World.hpp"
# include "../ft_vox.hpp"

class PlayerGround
{
  public:
	PlayerGround();
	PlayerGround(const PlayerGround &other);
	~PlayerGround();
	PlayerGround &operator=(const PlayerGround &other);

	static bool ground_eye_y_at(const World &world, double x, double z,
		double *ground_eye_y);
	static void drop_camera_to_ground(Camera *camera, const World &world);
};

#endif
