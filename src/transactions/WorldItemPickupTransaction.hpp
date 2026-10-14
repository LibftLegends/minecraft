#ifndef WORLD_ITEM_PICKUP_TRANSACTION_HPP
# define WORLD_ITEM_PICKUP_TRANSACTION_HPP

# include "GameTransactionJournal.hpp"
# include "../app/GameSessionSnapshotWriter.hpp"
# include "../crafting/MinecraftRecipeCatalog.hpp"
# include "../entities/WorldItemStore.hpp"
# include "../../Libft/Modules/Game/game_character.hpp"
# include <cstdint>

class WorldItemPickupTransaction
{
  public:
	static uint64_t journal_scope() noexcept;
	static int32_t next_request_id(const GameTransactionJournal &journal,
		uint64_t *request_id_out) noexcept;
	static int32_t prepare(const char *snapshot_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal, const WorldItemSnapshot &item,
		uint64_t actor_id, uint64_t request_id,
		uint64_t expected_inventory_revision, double actor_x, double actor_y,
		double actor_z, uint64_t server_tick,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
	static int32_t apply_prepared(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal,
		const GameTransactionJournal::Record &record,
		WorldItemPickupResult *result_out,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
	static int32_t execute(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal, const WorldItemSnapshot &item,
		uint64_t actor_id, uint64_t request_id,
		uint64_t expected_inventory_revision, double actor_x, double actor_y,
		double actor_z, uint64_t server_tick,
		WorldItemPickupResult *result_out,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
	static int32_t recover_pending(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
};

#endif
