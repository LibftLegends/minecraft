#include "../../src/queries/WorldRaycaster.hpp"

WorldRaycaster::WorldRaycaster()
{
}

WorldRaycaster::WorldRaycaster(const WorldRaycaster &other)
{
	*this = other;
}

WorldRaycaster::~WorldRaycaster()
{
}

WorldRaycaster &WorldRaycaster::operator=(const WorldRaycaster &other)
{
	(void)other;
	return (*this);
}

void WorldRaycaster::compute_cell(double ox, double oy, double oz, double dx,
	double dy, double dz, double dist, int32_t &tx, int32_t &ty, int32_t &tz)
{
	tx = static_cast<int32_t>(std::floor(ox + dx * dist));
	ty = static_cast<int32_t>(std::floor(oy + dy * dist));
	tz = static_cast<int32_t>(std::floor(oz + dz * dist));
}

int32_t WorldRaycaster::raycast_solid(const World &world, double origin_x,
	double origin_y, double origin_z, double direction_x, double direction_y,
	double direction_z, double max_distance, int32_t *block_x, int32_t *block_y,
	int32_t *block_z)
{
	uint32_t	block_id;
	int32_t		tx;
	int32_t		ty;
	int32_t		tz;

	if (block_x == nullptr || block_y == nullptr || block_z == nullptr
		|| max_distance <= 0.0)
		return (FT_ERR_INVALID_ARGUMENT);
	for (double dist = STEP_DISTANCE; dist <= max_distance; dist
		+= STEP_DISTANCE)
	{
		compute_cell(origin_x, origin_y, origin_z, direction_x, direction_y,
			direction_z, dist, tx, ty, tz);
		if (WorldBlockQuery::block_id_at(world, tx, ty, tz, &block_id)
			&& block_id != GAME_VOXEL_AIR_BLOCK)
		{
			*block_x = tx;
			*block_y = ty;
			*block_z = tz;
			return (FT_ERR_SUCCESS);
		}
	}
	return (FT_ERR_INVALID_ARGUMENT);
}

int32_t WorldRaycaster::raycast_edit_target(const World &world, double origin_x,
	double origin_y, double origin_z, double direction_x, double direction_y,
	double direction_z, double max_distance, int32_t *hit_x, int32_t *hit_y,
	int32_t *hit_z, int32_t *place_x, int32_t *place_y, int32_t *place_z,
	uint32_t *hit_id)
{
	int32_t		prev_x;
	int32_t		prev_y;
	int32_t		prev_z;
	uint32_t	block_id;
	int32_t		tx;
	int32_t		ty;
	int32_t		tz;

	prev_x = static_cast<int32_t>(std::floor(origin_x));
	prev_y = static_cast<int32_t>(std::floor(origin_y));
	prev_z = static_cast<int32_t>(std::floor(origin_z));
	if (!hit_x || !hit_y || !hit_z || !place_x || !place_y || !place_z
		|| !hit_id || max_distance <= 0.0)
		return (FT_ERR_INVALID_ARGUMENT);
	for (double dist = STEP_DISTANCE; dist <= max_distance; dist
		+= STEP_DISTANCE)
	{
		compute_cell(origin_x, origin_y, origin_z, direction_x, direction_y,
			direction_z, dist, tx, ty, tz);
		if (tx == prev_x && ty == prev_y && tz == prev_z)
			continue ;
		if (WorldBlockQuery::block_id_at(world, tx, ty, tz, &block_id)
			&& block_id != GAME_VOXEL_AIR_BLOCK)
		{
			*hit_x = tx;
			*hit_y = ty;
			*hit_z = tz;
			*place_x = prev_x;
			*place_y = prev_y;
			*place_z = prev_z;
			*hit_id = block_id;
			return (FT_ERR_SUCCESS);
		}
		prev_x = tx;
		prev_y = ty;
		prev_z = tz;
	}
	return (FT_ERR_NOT_FOUND);
}
