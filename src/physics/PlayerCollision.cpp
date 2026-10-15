#include "../../src/physics/PlayerCollision.hpp"

static int32_t player_collision_floor_div(int32_t value,
	int32_t divisor) noexcept
{
	int32_t quotient = value / divisor;
	if (value < 0 && value % divisor != 0)
		--quotient;
	return (quotient);
}

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
			max_z)
		|| player_box_intersects_revision_barrier(world, x, eye_y, z));
}

bool PlayerCollision::player_box_intersects_revision_barrier(
	const World &world, double x, double eye_y, double z)
{
	int32_t min_x;
	int32_t max_x;
	int32_t min_y;
	int32_t max_y;
	int32_t min_z;
	int32_t max_z;
	const int32_t chunk_width = GAME_VOXEL_CHUNK_WIDTH;
	const int32_t chunk_depth = GAME_VOXEL_CHUNK_DEPTH;
	if (!world.revision_barriers_active()
		|| chunk_width <= 0 || chunk_depth <= 0)
		return (false);
	PlayerGeometry::player_block_range(x, eye_y, z, &min_x, &max_x,
		&min_y, &max_y, &min_z, &max_z);
	for (int32_t block_z = min_z; block_z <= max_z; ++block_z)
	{
		const int32_t chunk_z = player_collision_floor_div(block_z,
			chunk_depth);
		for (int32_t block_x = min_x; block_x <= max_x; ++block_x)
			if (world.revision_barrier_blocks_chunk(
					player_collision_floor_div(block_x, chunk_width), chunk_z))
				return (true);
	}
	return (false);
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
	double &next_z, bool ignore_revision_barriers, bool &collided)
{
	for (int32_t by = min_y; by <= max_y; by++)
		for (int32_t bz = min_z; bz <= max_z; bz++)
			for (int32_t bx = min_x; bx <= max_x; bx++)
				if (world.solid_block_at(bx, by, bz)
					|| (!ignore_revision_barriers
						&& world.revision_barrier_blocks_chunk(
							player_collision_floor_div(bx,
								GAME_VOXEL_CHUNK_WIDTH),
							player_collision_floor_div(bz,
								GAME_VOXEL_CHUNK_DEPTH))))
					resolve_block_collision(axis, delta, bx, by, bz, next_x,
						next_y, next_z, collided);
}

void PlayerCollision::resolve_revision_barrier_collision(const World &world,
	int axis, double delta, double old_x, double old_z, double &next_x,
	double &next_z, bool &collided)
{
	const double half_width = PlayerGeometry::PLAYER_HALF_WIDTH;
	const double epsilon = 0.001;
	const int32_t chunk_width = GAME_VOXEL_CHUNK_WIDTH;
	const int32_t chunk_depth = GAME_VOXEL_CHUNK_DEPTH;
	int32_t first_chunk;
	int32_t last_chunk;
	int32_t other_first;
	int32_t other_last;
	if (axis == 0 && chunk_width > 0 && chunk_depth > 0)
	{
		first_chunk = player_collision_floor_div(static_cast<int32_t>(
			std::floor(std::min(old_x, next_x) - half_width)), chunk_width);
		last_chunk = player_collision_floor_div(static_cast<int32_t>(
			std::floor(std::max(old_x, next_x) + half_width)), chunk_width);
		other_first = player_collision_floor_div(static_cast<int32_t>(
			std::floor(old_z - half_width)), chunk_depth);
		other_last = player_collision_floor_div(static_cast<int32_t>(
			std::floor(old_z + half_width)), chunk_depth);
		for (int32_t chunk_z = other_first; chunk_z <= other_last; ++chunk_z)
			for (int32_t chunk_x = first_chunk; chunk_x <= last_chunk; ++chunk_x)
				if (world.revision_barrier_blocks_chunk(chunk_x, chunk_z))
				{
					if (delta > 0.0)
					{
						const double edge = static_cast<double>(chunk_x)
							* static_cast<double>(chunk_width);
						if (old_x + half_width <= edge + epsilon
							&& next_x + half_width > edge)
						{
							next_x = std::min(next_x,
								edge - half_width - epsilon);
							collided = true;
						}
					}
					else
					{
						const double edge = (static_cast<double>(chunk_x) + 1.0)
							* static_cast<double>(chunk_width);
						if (old_x - half_width >= edge - epsilon
							&& next_x - half_width < edge)
						{
							next_x = std::max(next_x,
								edge + half_width + epsilon);
							collided = true;
						}
					}
				}
	}
	else if (axis == 2 && chunk_width > 0 && chunk_depth > 0)
	{
		first_chunk = player_collision_floor_div(static_cast<int32_t>(
			std::floor(std::min(old_z, next_z) - half_width)), chunk_depth);
		last_chunk = player_collision_floor_div(static_cast<int32_t>(
			std::floor(std::max(old_z, next_z) + half_width)), chunk_depth);
		other_first = player_collision_floor_div(static_cast<int32_t>(
			std::floor(old_x - half_width)), chunk_width);
		other_last = player_collision_floor_div(static_cast<int32_t>(
			std::floor(old_x + half_width)), chunk_width);
		for (int32_t chunk_x = other_first; chunk_x <= other_last; ++chunk_x)
			for (int32_t chunk_z = first_chunk; chunk_z <= last_chunk; ++chunk_z)
				if (world.revision_barrier_blocks_chunk(chunk_x, chunk_z))
				{
					if (delta > 0.0)
					{
						const double edge = static_cast<double>(chunk_z)
							* static_cast<double>(chunk_depth);
						if (old_z + half_width <= edge + epsilon
							&& next_z + half_width > edge)
						{
							next_z = std::min(next_z,
								edge - half_width - epsilon);
							collided = true;
						}
					}
					else
					{
						const double edge = (static_cast<double>(chunk_z) + 1.0)
							* static_cast<double>(chunk_depth);
						if (old_z - half_width >= edge - epsilon
							&& next_z - half_width < edge)
						{
							next_z = std::max(next_z,
								edge + half_width + epsilon);
							collided = true;
						}
					}
				}
	}
}

bool PlayerCollision::move_player_axis(Camera *camera, const World &world,
	int axis, double delta)
{
	double	next_x;
	double	next_y;
	double	next_z;
	bool	collided;
	bool	ignore_revision_barriers;

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
	ignore_revision_barriers =
		player_box_intersects_revision_barrier(world, camera->x, camera->y,
			camera->z);
	int32_t min_x, max_x, min_y, max_y, min_z, max_z;
	PlayerGeometry::player_block_range(next_x, next_y, next_z, &min_x, &max_x,
		&min_y, &max_y, &min_z, &max_z);
	collided = false;
	scan_and_resolve(world, axis, delta, min_x, max_x, min_y, max_y, min_z,
		max_z, next_x, next_y, next_z, ignore_revision_barriers, collided);
	if (!ignore_revision_barriers)
		resolve_revision_barrier_collision(world, axis, delta, camera->x,
			camera->z, next_x, next_z, collided);
	camera->x = next_x;
	camera->y = next_y;
	camera->z = next_z;
	return (collided);
}
