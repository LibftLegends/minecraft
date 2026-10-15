#ifndef WORLD_BLOCK_MINING_TRANSACTION_HPP
# define WORLD_BLOCK_MINING_TRANSACTION_HPP

# include "GameTransactionJournal.hpp"
# include "../app/GameSessionSnapshotWriter.hpp"
# include "../entities/WorldItemStore.hpp"
# include "../inventory/MinecraftItemCatalog.hpp"
# include "../world/World.hpp"

class game_inventory;
class game_character;

struct WorldMiningDrop
{
	int32_t item_id;
	int32_t quantity;
};

class WorldBlockMiningTransaction
{
  public:
	static uint64_t journal_scope() noexcept;
	static uint32_t transaction_type() noexcept;
	static int32_t execute(const char *snapshot_path, World &world,
		MinecraftItemCatalog &catalog, WorldItemStore &items,
		GameTransactionJournal &journal, uint64_t request_id, uint64_t owner_id,
		uint64_t server_tick, int32_t world_x, int32_t world_y, int32_t world_z,
		uint32_t expected_block_id, uint32_t replacement_block_id,
		uint64_t expected_voxel_revision, const WorldMiningDrop *drops,
		uint32_t drop_count, const GameSessionSnapshotWriter *snapshot_writer,
		game_inventory *inventory = nullptr, int32_t tool_slot = -1,
		int32_t tool_item_id = 0, uint64_t tool_instance_id = 0U,
		uint64_t expected_inventory_revision = 0U,
		game_character *character = nullptr)
		noexcept;
	static int32_t recover_pending(const char *snapshot_path, World &world,
		MinecraftItemCatalog &catalog, WorldItemStore &items,
		GameTransactionJournal &journal,
		const GameSessionSnapshotWriter *snapshot_writer,
		game_inventory *inventory = nullptr,
		game_character *character = nullptr) noexcept;
};

#endif
