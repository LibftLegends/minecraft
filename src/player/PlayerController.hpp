#ifndef PLAYER_CONTROLLER_HPP
# define PLAYER_CONTROLLER_HPP

# include "../../src/movement/PlayerMovement.hpp"
# include "../../src/physics/PlayerCollision.hpp"
# include "../../src/physics/PlayerGeometry.hpp"
# include "../../src/physics/PlayerGround.hpp"
# include "../../src/spawn/PlayerSpawner.hpp"
# include "../ft_vox.hpp"

class PlayerController
{
  public:
	static constexpr double PLAYER_HALF_WIDTH = PlayerGeometry::PLAYER_HALF_WIDTH;
	static constexpr double PLAYER_EYE_HEIGHT = PlayerGeometry::PLAYER_EYE_HEIGHT;
	static constexpr double PLAYER_HEIGHT = PlayerGeometry::PLAYER_HEIGHT;

	PlayerController();
	PlayerController(const PlayerController &other);
	~PlayerController();
	PlayerController &operator=(const PlayerController &other);

	static void camera_forward(const Camera &camera, double *direction_x,
		double *direction_y, double *direction_z);
	static bool block_overlaps_player(const Camera &camera, int32_t block_x,
		int32_t block_y, int32_t block_z);
	static bool ground_eye_y_at(const World &world, double x, double z,
		double *ground_eye_y);
	static void drop_camera_to_ground(Camera *camera, const World &world);
	static bool player_box_intersects_world(const World &world, double x,
		double eye_y, double z);
	static void spawn_player_on_ground(Camera *camera, const World &world);
	static void update_player_horizontal_motion(Camera *camera,
		const CameraInput &input, const World &world, double delta_seconds);
	static void update_player_vertical_motion(Camera *camera,
		const CameraInput &input, const World &world, double delta_seconds);
};

#endif
