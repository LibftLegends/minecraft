#ifndef WORLD_ITEM_PROCESSING_ADVANCE_TRANSACTION_HPP
# define WORLD_ITEM_PROCESSING_ADVANCE_TRANSACTION_HPP

# include "GameTransactionJournal.hpp"
# include "../app/GameSessionSnapshotWriter.hpp"

class MinecraftRecipeCatalog;
class WorldItemStore;
class game_character;

class WorldItemProcessingAdvanceTransaction
{
  public:
	static uint64_t journal_scope() noexcept;
	static int32_t execute(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog, GameTransactionJournal &journal,
		uint64_t request_id, uint32_t server_ticks, uint32_t job_budget,
		uint32_t *completed_out, uint32_t *pending_output_out,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
	static int32_t execute(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog, GameTransactionJournal &journal,
		uint64_t request_id,
		const std::vector<uint64_t> &authorized_request_ids,
		uint32_t server_ticks, uint32_t job_budget,
		uint32_t *completed_out, uint32_t *pending_output_out,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
	static int32_t recover_pending(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog, GameTransactionJournal &journal,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
};

#endif
