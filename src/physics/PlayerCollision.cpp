#include "../../src/physics/PlayerCollision.hpp"

PlayerCollision::PlayerCollision()
{
}

PlayerCollision::PlayerCollision(const PlayerCollision &other)
{
	*this = other;
}

PlayerCollision::~PlayerCollision()
{
}

PlayerCollision &PlayerCollision::operator=(const PlayerCollision &other)
{
	(void)other;
	return (*this);
}

bool PlayerCollision::any_solid_in_range(const World &world, int32_t min_x,
	int32_t max_x, int32_t min_y, int32_t max_y, int32_t min_z, int32_t max_z)
{
	for (int32_t by = min_y; by <= max_y; by++)
		for (int32_t bz = min_z; bz <= max_z; bz++)
			for (int32_t bx = min_x; bx <= max_x; bx++)
				if (world.solid_block_at(bx, by, bz))
					return (true);
	return (false);
}

bool PlayerCollision::player_box_intersects_world(const World &world, double x,
	double eye_y, double z)
{
	const double	foot_clearance = 0.05;
	const double	head_clearance = 0.05;
	const double	side_clearance = 0.02;
	const double	feet_y = eye_y - PlayerGeometry::PLAYER_EYE_HEIGHT;
	const int32_t	min_x = static_cast<int32_t>(std::floor(x
					- PlayerGeometry::PLAYER_HALF_WIDTH + side_clearance));
	const int32_t	max_x = static_cast<int32_t>(std::floor(x
					+ PlayerGeometry::PLAYER_HALF_WIDTH - side_clearance));
	const int32_t	min_y = static_cast<int32_t>(std::floor(feet_y
					+ foot_clearance));
	const int32_t	max_y = static_cast<int32_t>(std::floor(feet_y
					+ PlayerGeometry::PLAYER_HEIGHT - head_clearance));
	const int32_t	min_z = static_cast<int32_t>(std::floor(z
					- PlayerGeometry::PLAYER_HALF_WIDTH + side_clearance));
	const int32_t	max_z = static_cast<int32_t>(std::floor(z
					+ PlayerGeometry::PLAYER_HALF_WIDTH - side_clearance));

	return (any_solid_in_range(world, min_x, max_x, min_y, max_y, min_z,
			max_z));
}

void PlayerCollision::resolve_block_collision(int axis, double delta,
	int32_t bx, int32_t by, int32_t bz, double &next_x, double &next_y,
	double &next_z, bool &collided)
{
	const double	eps = 0.001;
	const double	hw = PlayerGeometry::PLAYER_HALF_WIDTH;
	const double	eh = PlayerGeometry::PLAYER_EYE_HEIGHT;
	const double	ph = PlayerGeometry::PLAYER_HEIGHT;

	collided = true;
	if (axis == 0 && delta > 0.0)
		next_x = std::min(next_x, static_cast<double>(bx) - hw - eps);
	else if (axis == 0)
		next_x = std::max(next_x, static_cast<double>(bx + 1) + hw + eps);
	else if (axis == 1 && delta > 0.0)
		next_y = std::min(next_y, static_cast<double>(by) - ph + eh - eps);
	else if (axis == 1)
		next_y = std::max(next_y, static_cast<double>(by + 1) + eh + eps);
	else if (axis == 2 && delta > 0.0)
		next_z = std::min(next_z, static_cast<double>(bz) - hw - eps);
	else
		next_z = std::max(next_z, static_cast<double>(bz + 1) + hw + eps);
}

void PlayerCollision::scan_and_resolve(const World &world, int axis,
	double delta, int32_t min_x, int32_t max_x, int32_t min_y, int32_t max_y,
	int32_t min_z, int32_t max_z, double &next_x, double &next_y,
	double &next_z, bool &collided)
{
	for (int32_t by = min_y; by <= max_y; by++)
		for (int32_t bz = min_z; bz <= max_z; bz++)
			for (int32_t bx = min_x; bx <= max_x; bx++)
				if (world.solid_block_at(bx, by, bz))
					resolve_block_collision(axis, delta, bx, by, bz, next_x,
						next_y, next_z, collided);
}

bool PlayerCollision::move_player_axis(Camera *camera, const World &world,
	int axis, double delta)
{
	double	next_x;
	double	next_y;
	double	next_z;
	bool	collided;

	if (camera == nullptr || std::fabs(delta) < 0.000001)
		return (false);
	next_x = camera->x;
	next_y = camera->y;
	next_z = camera->z;
	if (axis == 0)
		next_x += delta;
	else if (axis == 1)
		next_y += delta;
	else
		next_z += delta;
	int32_t min_x, max_x, min_y, max_y, min_z, max_z;
	PlayerGeometry::player_block_range(next_x, next_y, next_z, &min_x, &max_x,
		&min_y, &max_y, &min_z, &max_z);
	collided = false;
	scan_and_resolve(world, axis, delta, min_x, max_x, min_y, max_y, min_z,
		max_z, next_x, next_y, next_z, collided);
	camera->x = next_x;
	camera->y = next_y;
	camera->z = next_z;
	return (collided);
}
