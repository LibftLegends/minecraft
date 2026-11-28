#ifndef WORLD_FARMLAND_TRANSACTION_HPP
# define WORLD_FARMLAND_TRANSACTION_HPP

# include "GameTransactionJournal.hpp"
# include "../app/GameSessionSnapshotWriter.hpp"
# include "../world/World.hpp"
# include "../inventory/MinecraftItemCatalog.hpp"
# include "../../Libft/Modules/Game/game_character.hpp"

class WorldFarmlandTransaction
{
  public:
	static uint64_t journal_scope() noexcept;
	static uint32_t transaction_type() noexcept;
	static int32_t execute(const char *snapshot_path,
		game_character &character, World &world, GameTransactionJournal &journal,
		uint64_t request_id, int32_t inventory_slot,
		uint64_t expected_inventory_revision, int32_t world_x, int32_t world_y,
		int32_t world_z, uint64_t expected_voxel_revision,
		const GameSessionSnapshotWriter *snapshot_writer,
		MinecraftItemCatalog *catalog = nullptr) noexcept;
	static int32_t recover_pending(const char *snapshot_path, World &world,
		GameTransactionJournal &journal,
		const GameSessionSnapshotWriter *snapshot_writer,
		game_character *character = nullptr,
		MinecraftItemCatalog *catalog = nullptr) noexcept;
};

#endif
