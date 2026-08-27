#ifndef PLAYER_GEOMETRY_HPP
# define PLAYER_GEOMETRY_HPP

# include "../../src/camera/Camera.hpp"
# include "../ft_vox.hpp"

class PlayerGeometry
{
  public:
	static constexpr double PLAYER_HALF_WIDTH = 0.30;
	static constexpr double PLAYER_EYE_HEIGHT = 1.62;
	static constexpr double PLAYER_HEIGHT = 1.80;

	PlayerGeometry();
	PlayerGeometry(const PlayerGeometry &other);
	~PlayerGeometry();
	PlayerGeometry &operator=(const PlayerGeometry &other);

	static void camera_forward(const Camera &camera, double *direction_x,
		double *direction_y, double *direction_z);
	static bool block_overlaps_player(const Camera &camera, int32_t block_x,
		int32_t block_y, int32_t block_z);
	static void player_block_range(double x, double eye_y, double z,
		int32_t *min_x, int32_t *max_x, int32_t *min_y, int32_t *max_y,
		int32_t *min_z, int32_t *max_z);
};

#endif
