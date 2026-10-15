#ifndef WORLD_ITEM_PROCESSING_TRANSACTION_HPP
# define WORLD_ITEM_PROCESSING_TRANSACTION_HPP

# include "GameTransactionJournal.hpp"
# include "../app/GameSessionPersistence.hpp"
# include "../app/GameSessionSnapshotWriter.hpp"

class WorldItemProcessingTransaction
{
  public:
	static uint64_t journal_scope() noexcept;
	static int32_t prepare(const char *snapshot_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal, uint64_t request_id,
		uint32_t recipe_id, int32_t batch_count,
		uint64_t expected_inventory_revision, int32_t station_x,
		int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
	static int32_t execute(const char *snapshot_path, game_character &character,
		WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal, uint64_t request_id,
		uint32_t recipe_id, int32_t batch_count,
		uint64_t expected_inventory_revision, int32_t station_x,
		int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision,
		MinecraftProcessingActionResult *result_out,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
	static int32_t recover_pending(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
};

#endif
