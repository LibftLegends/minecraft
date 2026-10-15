#ifndef BLOCK_INTERACTOR_HPP
# define BLOCK_INTERACTOR_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../../src/camera/Camera.hpp"
# include "../../src/edits/DeleteBlockCommand.hpp"
# include "../../src/edits/PlaceBlockCommand.hpp"
# include "../../src/entities/WorldItemStore.hpp"
# include "../../src/inventory/MinecraftItemCatalog.hpp"
# include "../../src/player/PlayerController.hpp"
# include "../../src/world/World.hpp"
# include "../../src/transactions/WorldBlockMiningTransaction.hpp"
# include "../ft_vox.hpp"

class game_character;

class BlockInteractor
{
  public:
	BlockInteractor();
	BlockInteractor(const BlockInteractor &other);
	~BlockInteractor();
	BlockInteractor &operator=(const BlockInteractor &other);

	static int32_t try_delete_target_block(World *world, const Camera &camera);
	static int32_t try_delete_target_block(World *world, const Camera &camera,
		MinecraftItemCatalog &catalog, WorldItemStore &world_items,
		uint64_t owner_id, uint64_t server_tick);
	static int32_t try_delete_target_block(World *world, const Camera &camera,
		MinecraftItemCatalog &catalog, WorldItemStore &world_items,
		GameTransactionJournal &journal,
		const GameSessionSnapshotWriter *snapshot_writer,
		const char *snapshot_path, uint64_t owner_id, uint64_t server_tick,
		game_inventory *inventory = nullptr,
		game_character *character = nullptr);
	static int32_t try_pick_target_block(const World *world,
		const Camera &camera, uint32_t *selected_block_id);
	static int32_t try_place_selected_block(World *world, const Camera &camera,
		uint32_t selected_block_id);

  private:
	static void get_camera_direction(const Camera &camera, double &dx,
		double &dy, double &dz);
};

#endif
