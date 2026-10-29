#ifndef WORLD_ITEM_CARD_TRANSFER_TRANSACTION_HPP
# define WORLD_ITEM_CARD_TRANSFER_TRANSACTION_HPP

# include "GameTransactionJournal.hpp"
# include "../app/GameSessionPersistence.hpp"
# include "../app/GameSessionSnapshotWriter.hpp"
# include "../cards/RegenerationCardSession.hpp"
# include "../entities/WorldItemStore.hpp"
# include "../crafting/MinecraftRecipeCatalog.hpp"

class WorldItemCardTransferTransaction
{
  public:
	static uint64_t journal_scope() noexcept;
	static int32_t prepare_import(const char *snapshot_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal, int32_t inventory_slot,
		uint64_t expected_inventory_revision, uint64_t request_id,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
	static int32_t prepare_export(const char *snapshot_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal, uint32_t card_instance_id,
		uint64_t expected_inventory_revision, uint64_t request_id,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
	static int32_t import_to_deck(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog,
		RegenerationCardSession &card_session,
		GameTransactionJournal &journal, int32_t inventory_slot,
		uint64_t expected_inventory_revision, uint64_t request_id,
		card_game_deck_card *card_out,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
	static int32_t export_to_inventory(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog,
		RegenerationCardSession &card_session,
		GameTransactionJournal &journal, uint32_t card_instance_id,
		uint64_t expected_inventory_revision, uint64_t request_id,
		RegenerationCardTransferResult *result_out,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
	static int32_t recover_pending(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog,
		RegenerationCardSession &card_session,
		GameTransactionJournal &journal,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
};

#endif
