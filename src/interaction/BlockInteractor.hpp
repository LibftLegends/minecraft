#ifndef BLOCK_INTERACTOR_HPP
# define BLOCK_INTERACTOR_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../../src/camera/Camera.hpp"
# include "../../src/edits/DeleteBlockCommand.hpp"
# include "../../src/edits/PlaceBlockCommand.hpp"
# include "../../src/player/PlayerController.hpp"
# include "../../src/world/World.hpp"
# include "../ft_vox.hpp"

class BlockInteractor
{
  public:
	BlockInteractor();
	BlockInteractor(const BlockInteractor &other);
	~BlockInteractor();
	BlockInteractor &operator=(const BlockInteractor &other);

	static int32_t try_delete_target_block(World *world, const Camera &camera);
	static int32_t try_pick_target_block(const World *world,
		const Camera &camera, uint32_t *selected_block_id);
	static int32_t try_place_selected_block(World *world, const Camera &camera,
		uint32_t selected_block_id);

  private:
	static const int32_t WATER_HOPS_MAX;
	static const int32_t WATER_FILL_MAX;
	static const int32_t WATER_BUF;

	struct		WaterState
	{
		int32_t	qx[166], qy[166], qz[166], qhd[166];
		int32_t	vx[166], vy[166], vz[166];
		int32_t q_front, q_back, v_count, fills;
	};

	static bool is_visited(int32_t x, int32_t y, int32_t z,
		const WaterState &s);
	static void mark(int32_t x, int32_t y, int32_t z, WaterState &s);
	static void enqueue(int32_t x, int32_t y, int32_t z, int32_t hd,
		WaterState &s);
	static void try_spread_down(World *world, int32_t x, int32_t y, int32_t z,
		WaterState &s);
	static void try_spread_horizontal(World *world, int32_t x, int32_t y,
		int32_t z, int32_t hd, WaterState &s);
	static void spread_water(World *world, int32_t sx, int32_t sy, int32_t sz);
	static void get_camera_direction(const Camera &camera, double &dx,
		double &dy, double &dz);
};

#endif
