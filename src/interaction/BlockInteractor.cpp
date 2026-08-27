#include "../../src/interaction/BlockInteractor.hpp"

const int32_t BlockInteractor::WATER_HOPS_MAX = 7;
const int32_t BlockInteractor::WATER_FILL_MAX = 150;
const int32_t BlockInteractor::WATER_BUF = WATER_FILL_MAX + 16;

BlockInteractor::BlockInteractor()
{
}
BlockInteractor::BlockInteractor(const BlockInteractor &other)
{
	(void)other;
}
BlockInteractor::~BlockInteractor()
{
}
BlockInteractor &BlockInteractor::operator=(const BlockInteractor &other)
{
	(void)other;
	return (*this);
}

bool BlockInteractor::is_visited(int32_t x, int32_t y, int32_t z,
	const WaterState &s)
{
	for (int32_t i = 0; i < s.v_count; ++i)
		if (s.vx[i] == x && s.vy[i] == y && s.vz[i] == z)
			return (true);
	return (false);
}

void BlockInteractor::mark(int32_t x, int32_t y, int32_t z, WaterState &s)
{
	if (s.v_count < WATER_BUF - 1)
	{
		s.vx[s.v_count] = x;
		s.vy[s.v_count] = y;
		s.vz[s.v_count] = z;
		++s.v_count;
	}
}

void BlockInteractor::enqueue(int32_t x, int32_t y, int32_t z, int32_t hd,
	WaterState &s)
{
	if (s.q_back < WATER_BUF - 1)
	{
		s.qx[s.q_back] = x;
		s.qy[s.q_back] = y;
		s.qz[s.q_back] = z;
		s.qhd[s.q_back] = hd;
		++s.q_back;
	}
}

void BlockInteractor::try_spread_down(World *world, int32_t x, int32_t y,
	int32_t z, WaterState &s)
{
	uint32_t	b;

	if (world->solid_block_at(x, y - 1, z))
		return ;
	b = 0;
	world->block_id_at(x, y - 1, z, &b);
	if (b != GAME_VOXEL_AIR_BLOCK || is_visited(x, y - 1, z, s))
		return ;
	if (world->place_block_at(x, y - 1, z,
			TERRAIN_GENERATOR_WATER_BLOCK) == FT_ERR_SUCCESS)
	{
		++s.fills;
		mark(x, y - 1, z, s);
		enqueue(x, y - 1, z, 0, s);
	}
}

void BlockInteractor::try_spread_horizontal(World *world, int32_t x, int32_t y,
	int32_t z, int32_t hd, WaterState &s)
{
	static const int32_t	DX[4] = {1, -1, 0, 0};
	static const int32_t	DZ[4] = {0, 0, 1, -1};
	int32_t					nx;
	int32_t					nz;
	uint32_t				b;

	for (int d = 0; d < 4; ++d)
	{
		nx = x + DX[d];
		nz = z + DZ[d];
		if (is_visited(nx, y, nz, s))
			continue ;
		b = 0;
		world->block_id_at(nx, y, nz, &b);
		if (b == GAME_VOXEL_AIR_BLOCK)
		{
			if (world->place_block_at(nx, y, nz,
					TERRAIN_GENERATOR_WATER_BLOCK) == FT_ERR_SUCCESS)
			{
				++s.fills;
				mark(nx, y, nz, s);
				enqueue(nx, y, nz, hd + 1, s);
			}
		}
	}
}

void BlockInteractor::spread_water(World *world, int32_t sx, int32_t sy,
	int32_t sz)
{
	WaterState	s;
	int32_t		x;
	int32_t		y;
	int32_t		z;
	int32_t		hd;

	s = {};
	mark(sx, sy, sz, s);
	enqueue(sx, sy, sz, 0, s);
	while (s.q_front < s.q_back && s.fills < WATER_FILL_MAX)
	{
		x = s.qx[s.q_front];
		y = s.qy[s.q_front];
		z = s.qz[s.q_front];
		hd = s.qhd[s.q_front];
		++s.q_front;
		try_spread_down(world, x, y, z, s);
		if (hd < WATER_HOPS_MAX)
			try_spread_horizontal(world, x, y, z, hd, s);
	}
}

void BlockInteractor::get_camera_direction(const Camera &camera, double &dx,
	double &dy, double &dz)
{
	PlayerController::camera_forward(camera, &dx, &dy, &dz);
}

int32_t BlockInteractor::try_delete_target_block(World *world,
	const Camera &camera)
{
	if (!world)
		return (FT_ERR_INVALID_ARGUMENT);
	double dx, dy, dz;
	get_camera_direction(camera, dx, dy, dz);
	int32_t bx, by, bz;
	if (world->raycast_solid(camera.x, camera.y, camera.z, dx, dy, dz, 10.0,
			&bx, &by, &bz) != FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	DeleteBlockCommand cmd(bx, by, bz);
	return (cmd.execute(*world));
}

int32_t BlockInteractor::try_pick_target_block(const World *world,
	const Camera &camera, uint32_t *selected_block_id)
{
	uint32_t	hit_id;

	if (!world || !selected_block_id)
		return (FT_ERR_INVALID_ARGUMENT);
	double dx, dy, dz;
	get_camera_direction(camera, dx, dy, dz);
	int32_t hx, hy, hz, px, py, pz;
	if (world->raycast_edit_target(camera.x, camera.y, camera.z, dx, dy, dz,
			10.0, &hx, &hy, &hz, &px, &py, &pz, &hit_id) != FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	if (hit_id != GAME_VOXEL_AIR_BLOCK
		&& terrain_block_is_known(hit_id) == FT_TRUE
		&& terrain_block_is_breakable(hit_id) == FT_TRUE)
		*selected_block_id = hit_id;
	return (FT_ERR_SUCCESS);
}

int32_t BlockInteractor::try_place_selected_block(World *world,
	const Camera &camera, uint32_t selected_block_id)
{
	uint32_t	hit_id;
	int32_t		err;

	if (!world || selected_block_id == GAME_VOXEL_AIR_BLOCK)
		return (FT_ERR_INVALID_ARGUMENT);
	double dx, dy, dz;
	get_camera_direction(camera, dx, dy, dz);
	int32_t hx, hy, hz, px, py, pz;
	if (world->raycast_edit_target(camera.x, camera.y, camera.z, dx, dy, dz,
			10.0, &hx, &hy, &hz, &px, &py, &pz, &hit_id) != FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	if (PlayerController::block_overlaps_player(camera, px, py, pz))
		return (FT_ERR_SUCCESS);
	PlaceBlockCommand cmd(px, py, pz, selected_block_id);
	err = cmd.execute(*world);
	if (err == FT_ERR_ALREADY_EXISTS || err == FT_ERR_NOT_FOUND)
		return (FT_ERR_SUCCESS);
	if (err == FT_ERR_SUCCESS
		&& selected_block_id == TERRAIN_GENERATOR_WATER_BLOCK)
		spread_water(world, px, py, pz);
	return (err);
}
