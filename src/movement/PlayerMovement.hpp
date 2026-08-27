#ifndef PLAYER_MOVEMENT_HPP
# define PLAYER_MOVEMENT_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../../src/camera/CameraInput.hpp"
# include "../../src/physics/PlayerCollision.hpp"
# include "../ft_vox.hpp"

class PlayerMovement
{
  private:
	struct		CameraSnapshot
	{
		double	x;
		double	y;
		double	z;
	};

	struct		MovementDelta
	{
		double	x;
		double	z;
	};

	struct		LocalDirection
	{
		double	x;
		double	z;
	};

	static LocalDirection local_direction_from_input(const CameraInput &input);
	static MovementDelta camera_horizontal_delta(const Camera &camera,
		const CameraInput &input, double delta_seconds, double speed_scale);
	static CameraSnapshot snapshot_camera(const Camera &camera);
	static void restore_camera(Camera *camera, const CameraSnapshot &snapshot);
	static bool move_axis_with_water_step(Camera *camera, const World &world,
		int axis, double delta);
	static bool player_in_water(const Camera &camera, const World &world);
	static void update_air_velocity(Camera *camera, const CameraInput &input,
		double delta_seconds);
	static void update_water_velocity(Camera *camera, const CameraInput &input,
		double delta_seconds);
	static void apply_vertical_motion(Camera *camera, const World &world,
		double delta_seconds, bool in_water);

  public:
	PlayerMovement();
	PlayerMovement(const PlayerMovement &other);
	~PlayerMovement();
	PlayerMovement &operator=(const PlayerMovement &other);

	static void update_player_horizontal_motion(Camera *camera,
		const CameraInput &input, const World &world, double delta_seconds);
	static void update_player_vertical_motion(Camera *camera,
		const CameraInput &input, const World &world, double delta_seconds);
};

#endif
