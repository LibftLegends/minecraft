#include "../../src/physics/PlayerGeometry.hpp"

PlayerGeometry::PlayerGeometry()
{
}

PlayerGeometry::PlayerGeometry(const PlayerGeometry &other)
{
	*this = other;
}

PlayerGeometry::~PlayerGeometry()
{
}

PlayerGeometry &PlayerGeometry::operator=(const PlayerGeometry &other)
{
	(void)other;
	return (*this);
}

void PlayerGeometry::camera_forward(const Camera &camera, double *direction_x,
	double *direction_y, double *direction_z)
{
	double	pitch_cos;

	if (direction_x == nullptr || direction_y == nullptr
		|| direction_z == nullptr)
		return ;
	pitch_cos = std::cos(camera.pitch);
	*direction_x = std::sin(camera.yaw) * pitch_cos;
	*direction_y = std::sin(camera.pitch);
	*direction_z = std::cos(camera.yaw) * pitch_cos;
}

bool PlayerGeometry::block_overlaps_player(const Camera &camera,
	int32_t block_x, int32_t block_y, int32_t block_z)
{
	double	player_min_x;
	double	player_max_x;
	double	player_min_y;
	double	player_max_y;
	double	player_min_z;
	double	player_max_z;

	player_min_x = camera.x - PLAYER_HALF_WIDTH;
	player_max_x = camera.x + PLAYER_HALF_WIDTH;
	player_min_y = camera.y - PLAYER_EYE_HEIGHT;
	player_max_y = player_min_y + PLAYER_HEIGHT;
	player_min_z = camera.z - PLAYER_HALF_WIDTH;
	player_max_z = camera.z + PLAYER_HALF_WIDTH;
	if (static_cast<double>(block_x + 1) <= player_min_x
		|| static_cast<double>(block_x) >= player_max_x)
		return (false);
	if (static_cast<double>(block_y + 1) <= player_min_y
		|| static_cast<double>(block_y) >= player_max_y)
		return (false);
	if (static_cast<double>(block_z + 1) <= player_min_z
		|| static_cast<double>(block_z) >= player_max_z)
		return (false);
	return (true);
}

void PlayerGeometry::player_block_range(double x, double eye_y, double z,
	int32_t *min_x, int32_t *max_x, int32_t *min_y, int32_t *max_y,
	int32_t *min_z, int32_t *max_z)
{
	const double	collision_epsilon = 0.001;
	double			feet_y;

	if (min_x == nullptr || max_x == nullptr || min_y == nullptr
		|| max_y == nullptr || min_z == nullptr || max_z == nullptr)
		return ;
	feet_y = eye_y - PLAYER_EYE_HEIGHT;
	*min_x = static_cast<int32_t>(std::floor(x - PLAYER_HALF_WIDTH
				+ collision_epsilon));
	*max_x = static_cast<int32_t>(std::floor(x + PLAYER_HALF_WIDTH
				- collision_epsilon));
	*min_y = static_cast<int32_t>(std::floor(feet_y + collision_epsilon));
	*max_y = static_cast<int32_t>(std::floor(feet_y + PLAYER_HEIGHT
				- collision_epsilon));
	*min_z = static_cast<int32_t>(std::floor(z - PLAYER_HALF_WIDTH
				+ collision_epsilon));
	*max_z = static_cast<int32_t>(std::floor(z + PLAYER_HALF_WIDTH
				- collision_epsilon));
}
