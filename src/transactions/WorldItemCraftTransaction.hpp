#ifndef WORLD_ITEM_CRAFT_TRANSACTION_HPP
# define WORLD_ITEM_CRAFT_TRANSACTION_HPP

# include "GameTransactionJournal.hpp"
# include "../app/GameSessionPersistence.hpp"
# include "../app/GameSessionSnapshotWriter.hpp"

class WorldItemCraftTransaction
{
  public:
	using StationContinuityValidator = bool (*)(uint32_t station_id,
		int32_t station_x, int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision, void *user_data) noexcept;
	using CharacterStateRevisionProvider = bool (*)(
		const game_character &character, uint64_t *revision_out,
		void *user_data) noexcept;
	using CraftPrecommitAuthorizer = bool (*)(uint64_t authenticated_actor_id,
		uint32_t station_id, int32_t station_x, int32_t station_y,
		int32_t station_z, uint64_t expected_station_revision,
		uint64_t expected_session_id, uint64_t expected_session_revision,
		void *user_data) noexcept;
	static uint64_t journal_scope() noexcept;
	static int32_t prepare(const char *snapshot_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal, uint64_t request_id,
		uint32_t recipe_id, int32_t batch_count,
		uint64_t expected_inventory_revision,
		uint32_t station_id, int32_t station_x, int32_t station_y,
		int32_t station_z, uint64_t expected_station_revision,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
	static int32_t prepare(const char *snapshot_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal, uint64_t request_id,
		uint32_t recipe_id, int32_t batch_count,
		uint64_t expected_inventory_revision,
		uint64_t expected_catalog_revision,
		uint64_t expected_recipe_book_revision,
		uint32_t station_id, int32_t station_x, int32_t station_y,
		int32_t station_z, uint64_t expected_station_revision,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
	static int32_t prepare(const char *snapshot_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal, uint64_t request_id,
		uint32_t recipe_id, int32_t batch_count,
		uint64_t expected_inventory_revision,
		uint64_t expected_catalog_revision,
		uint64_t expected_recipe_book_revision,
		uint64_t expected_character_state_revision,
		uint64_t authenticated_actor_id,
		uint32_t station_id, int32_t station_x, int32_t station_y,
		int32_t station_z, uint64_t expected_station_revision,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr,
		uint64_t expected_session_id = 0U,
		uint64_t expected_session_revision = 0U) noexcept;
	static int32_t execute(const char *snapshot_path, game_character &character,
		WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal, uint64_t request_id,
		uint32_t recipe_id, int32_t batch_count,
		uint64_t expected_inventory_revision,
		uint32_t station_id, int32_t station_x, int32_t station_y,
		int32_t station_z, uint64_t expected_station_revision,
		MinecraftCraftActionResult *result_out,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
	static int32_t execute(const char *snapshot_path, game_character &character,
		WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal, uint64_t request_id,
		uint32_t recipe_id, int32_t batch_count,
		uint64_t expected_inventory_revision,
		uint64_t expected_catalog_revision,
		uint64_t expected_recipe_book_revision,
		uint32_t station_id, int32_t station_x, int32_t station_y,
		int32_t station_z, uint64_t expected_station_revision,
		MinecraftCraftActionResult *result_out,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr) noexcept;
	static int32_t execute(const char *snapshot_path, game_character &character,
		WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal, uint64_t request_id,
		uint32_t recipe_id, int32_t batch_count,
		uint64_t expected_inventory_revision,
		uint64_t expected_catalog_revision,
		uint64_t expected_recipe_book_revision,
		uint64_t authenticated_actor_id,
		uint64_t expected_character_state_revision,
		CharacterStateRevisionProvider character_revision_provider,
		void *character_revision_user_data,
		CraftPrecommitAuthorizer precommit_authorizer,
		void *precommit_authorizer_user_data,
		uint32_t station_id, int32_t station_x, int32_t station_y,
		int32_t station_z, uint64_t expected_station_revision,
		MinecraftCraftActionResult *result_out,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr,
		uint64_t expected_session_id = 0U,
		uint64_t expected_session_revision = 0U) noexcept;
	static int32_t recover_pending(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal,
		const GameSessionSnapshotWriter *snapshot_writer = nullptr,
		StationContinuityValidator station_validator = nullptr,
		void *station_validator_user_data = nullptr,
		CharacterStateRevisionProvider character_revision_provider = nullptr,
		void *character_revision_user_data = nullptr,
		CraftPrecommitAuthorizer precommit_authorizer = nullptr,
		void *precommit_authorizer_user_data = nullptr) noexcept;
};

#endif
