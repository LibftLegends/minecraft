#ifndef WORLD_EQUIPMENT_REPAIR_TRANSACTION_HPP
# define WORLD_EQUIPMENT_REPAIR_TRANSACTION_HPP

# include "GameTransactionJournal.hpp"
# include "../app/GameSessionSnapshotWriter.hpp"
# include "../inventory/MinecraftItemCatalog.hpp"
# include "../../Libft/Modules/Game/game_character.hpp"

class WorldEquipmentRepairTransaction
{
  public:
	static uint64_t journal_scope() noexcept;
	static uint32_t transaction_type() noexcept;
	static int32_t execute(const char *snapshot_path, game_character &character,
		MinecraftItemCatalog &catalog, GameTransactionJournal &journal,
		uint64_t request_id, int32_t equipment_slot,
		uint64_t expected_inventory_revision,
		int32_t station_x, int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept;
	static int32_t recover_pending(const char *snapshot_path,
		game_character &character, MinecraftItemCatalog &catalog,
		GameTransactionJournal &journal,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept;
};

#endif
