#ifndef GAME_SESSION_PERSISTENCE_HPP
# define GAME_SESSION_PERSISTENCE_HPP

#include "../entities/WorldItemStore.hpp"
#include "../../Libft/Modules/Game/game_character.hpp"
#include "../../Libft/Modules/Game/game_recipe_book.hpp"
#include "../crafting/MinecraftRecipeCatalog.hpp"
#include "../transactions/GameTransactionJournal.hpp"
#include "../world/WorldSaveCatalog.hpp"
#include <cstdint>
#include <string>
#include <vector>

class GameSessionPersistence
{
  public:
	/* Initializes the catalog and reconciles interrupted world deletions before
	 * exposing it to session admission. */
	static int32_t initialize_catalog(WorldSaveCatalog &catalog,
		const char *catalog_path) noexcept;
	/* Bundles the MCSP snapshot, optional revision metadata, and WVC1 terrain
	 * records into a checksummed archive. All paths must resolve within saves/. */
	static int32_t export_world_bundle(const char *snapshot_path,
		const char *revision_metadata_path, const char *bundle_path) noexcept;
	/* Catalog wrappers require the owner and block admitted joins plus
	 * same-process GameSession starts for the maintenance interval. */
	static int32_t backup_catalog_world(WorldSaveCatalog &catalog,
		const uint8_t world_id[32], uint64_t actor_id,
		const char *bundle_path) noexcept;
	/* Installs a validated bundle with a recoverable rollback journal covering
	 * the snapshot, revision metadata, and terrain records. */
	static int32_t import_world_bundle(const char *bundle_path,
		const char *snapshot_path,
		const char *revision_metadata_path) noexcept;
	static int32_t restore_catalog_world(WorldSaveCatalog &catalog,
		const uint8_t world_id[32], uint64_t actor_id,
		const std::string &typed_world_name,
		const char *bundle_path) noexcept;
	/* Owner-confirmed deletion removes the catalog entry and its authoritative
	 * snapshot/revision files under maintenance locks, with WBJ1 rollback. */
	static int32_t delete_catalog_world(WorldSaveCatalog &catalog,
		const uint8_t world_id[32], uint64_t actor_id,
		const std::string &typed_world_name) noexcept;
	/* Reconciles WDL1 deletion markers before a catalog world is opened. */
	static int32_t recover_catalog_world_deletions(
		WorldSaveCatalog &catalog,
		const uint8_t world_id_filter[32] = nullptr) noexcept;
	/* Completes rollback for an interrupted bundle import before a world is
	 * opened. A missing recovery journal is success. */
	static int32_t recover_world_bundle_import(const char *snapshot_path,
		const char *revision_metadata_path) noexcept;
	static int32_t save_file(const char *file_path,
		const game_character &character,
		const WorldItemStore &world_items) noexcept;
	static int32_t save_file(const char *file_path,
		const game_character &character, const WorldItemStore &world_items,
		const game_recipe_book &recipe_book) noexcept;
	static int32_t save_file(const char *file_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog) noexcept;
	static int32_t save_file(const char *file_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		const GameTransactionJournal &transaction_journal) noexcept;
	static int32_t save_file(const char *file_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		const GameTransactionJournal &transaction_journal,
		const std::vector<uint8_t> &card_session_state) noexcept;
	static int32_t save_file(const char *file_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		const GameTransactionJournal &transaction_journal,
		const std::vector<uint8_t> &card_session_state,
		const std::vector<uint8_t> &regeneration_rules_state) noexcept;
	static int32_t save_file(const char *file_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		const GameTransactionJournal &transaction_journal,
		const std::vector<uint8_t> &card_session_state,
		const std::vector<uint8_t> &regeneration_rules_state,
		const std::vector<uint8_t> &chunk_instability_state) noexcept;
	static int32_t save_file(const char *file_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		const GameTransactionJournal &transaction_journal,
		const std::vector<uint8_t> &card_session_state,
		const std::vector<uint8_t> &regeneration_rules_state,
		const std::vector<uint8_t> &chunk_instability_state,
		const std::vector<uint8_t> &revision_metadata_state) noexcept;
	static int32_t save_file(const char *file_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		const GameTransactionJournal &transaction_journal,
		const std::vector<uint8_t> &card_session_state,
		const std::vector<uint8_t> &regeneration_rules_state,
		const std::vector<uint8_t> &chunk_instability_state,
		const std::vector<uint8_t> &revision_metadata_state,
		const std::vector<uint8_t> &fluid_state) noexcept;
	static int32_t save_file(const char *file_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		const GameTransactionJournal &transaction_journal,
		const std::vector<uint8_t> &card_session_state,
		const std::vector<uint8_t> &regeneration_rules_state,
		const std::vector<uint8_t> &chunk_instability_state,
		const std::vector<uint8_t> &revision_metadata_state,
		const std::vector<uint8_t> &fluid_state,
		const std::vector<uint8_t> &entity_state) noexcept;
	static int32_t save_file(const char *file_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		const GameTransactionJournal &transaction_journal,
		const std::vector<uint8_t> &card_session_state,
		const std::vector<uint8_t> &regeneration_rules_state,
		const std::vector<uint8_t> &chunk_instability_state,
		const std::vector<uint8_t> &revision_metadata_state,
		const std::vector<uint8_t> &fluid_state,
		const std::vector<uint8_t> &entity_state,
		const std::vector<uint8_t> &crop_growth_policy_state) noexcept;
	static int32_t save_file_with_crop_growth_state(const char *file_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		const GameTransactionJournal &transaction_journal,
		const std::vector<uint8_t> &card_session_state,
		const std::vector<uint8_t> &regeneration_rules_state,
		const std::vector<uint8_t> &chunk_instability_state,
		const std::vector<uint8_t> &revision_metadata_state,
		const std::vector<uint8_t> &fluid_state,
		const std::vector<uint8_t> &entity_state,
		const std::vector<uint8_t> &crop_growth_state) noexcept;
	static int32_t save_file_with_crop_growth_state_and_output_session_id(
		const char *file_path, const game_character &character,
		const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		const GameTransactionJournal &transaction_journal,
		const std::vector<uint8_t> &card_session_state,
		const std::vector<uint8_t> &regeneration_rules_state,
		const std::vector<uint8_t> &chunk_instability_state,
		const std::vector<uint8_t> &revision_metadata_state,
		const std::vector<uint8_t> &fluid_state,
		const std::vector<uint8_t> &entity_state,
		const std::vector<uint8_t> &crop_growth_state,
		uint64_t output_session_id) noexcept;
	static int32_t save_file_with_revision_server_checkpoint(
		const char *file_path, const game_character &character,
		const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		const GameTransactionJournal &transaction_journal,
		const std::vector<uint8_t> &card_session_state,
		const std::vector<uint8_t> &regeneration_rules_state,
		const std::vector<uint8_t> &chunk_instability_state,
		const std::vector<uint8_t> &revision_metadata_state,
		const std::vector<uint8_t> &fluid_state,
		const std::vector<uint8_t> &entity_state,
		const std::vector<uint8_t> &crop_growth_state,
		uint64_t output_session_id,
		const std::vector<uint8_t> &revision_server_state) noexcept;
	static int32_t save_file_with_revision_server_checkpoint_and_host_config(
		const char *file_path, const game_character &character,
		const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		const GameTransactionJournal &transaction_journal,
		const std::vector<uint8_t> &card_session_state,
		const std::vector<uint8_t> &regeneration_rules_state,
		const std::vector<uint8_t> &chunk_instability_state,
		const std::vector<uint8_t> &revision_metadata_state,
		const std::vector<uint8_t> &fluid_state,
		const std::vector<uint8_t> &entity_state,
		const std::vector<uint8_t> &crop_growth_state,
		uint64_t output_session_id,
		const std::vector<uint8_t> &revision_server_state,
		const std::vector<uint8_t> &host_config_state) noexcept;
	static int32_t load_file(const char *file_path,
		game_character &character, WorldItemStore &world_items) noexcept;
	static int32_t load_file(const char *file_path, game_character &character,
		WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog) noexcept;
	static int32_t load_file(const char *file_path, game_character &character,
		WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &transaction_journal) noexcept;
	static int32_t load_file(const char *file_path, game_character &character,
		WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &transaction_journal,
		std::vector<uint8_t> &card_session_state) noexcept;
	static int32_t load_file(const char *file_path, game_character &character,
		WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &transaction_journal,
		std::vector<uint8_t> &card_session_state,
		std::vector<uint8_t> &regeneration_rules_state) noexcept;
	static int32_t load_file(const char *file_path, game_character &character,
		WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &transaction_journal,
		std::vector<uint8_t> &card_session_state,
		std::vector<uint8_t> &regeneration_rules_state,
		std::vector<uint8_t> &chunk_instability_state) noexcept;
	static int32_t load_file(const char *file_path, game_character &character,
		WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &transaction_journal,
		std::vector<uint8_t> &card_session_state,
		std::vector<uint8_t> &regeneration_rules_state,
		std::vector<uint8_t> &chunk_instability_state,
		std::vector<uint8_t> &revision_metadata_state) noexcept;
	static int32_t load_file(const char *file_path, game_character &character,
		WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &transaction_journal,
		std::vector<uint8_t> &card_session_state,
		std::vector<uint8_t> &regeneration_rules_state,
		std::vector<uint8_t> &chunk_instability_state,
		std::vector<uint8_t> &revision_metadata_state,
		std::vector<uint8_t> &fluid_state) noexcept;
	static int32_t load_file(const char *file_path, game_character &character,
		WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &transaction_journal,
		std::vector<uint8_t> &card_session_state,
		std::vector<uint8_t> &regeneration_rules_state,
		std::vector<uint8_t> &chunk_instability_state,
		std::vector<uint8_t> &revision_metadata_state,
		std::vector<uint8_t> &fluid_state,
		std::vector<uint8_t> &entity_state) noexcept;
	static int32_t load_file(const char *file_path, game_character &character,
		WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &transaction_journal,
		std::vector<uint8_t> &card_session_state,
		std::vector<uint8_t> &regeneration_rules_state,
		std::vector<uint8_t> &chunk_instability_state,
		std::vector<uint8_t> &revision_metadata_state,
		std::vector<uint8_t> &fluid_state,
		std::vector<uint8_t> &entity_state,
		std::vector<uint8_t> &crop_growth_policy_state) noexcept;
	static int32_t load_file_with_output_session_id(const char *file_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &transaction_journal,
		std::vector<uint8_t> &card_session_state,
		std::vector<uint8_t> &regeneration_rules_state,
		std::vector<uint8_t> &chunk_instability_state,
		std::vector<uint8_t> &revision_metadata_state,
		std::vector<uint8_t> &fluid_state,
		std::vector<uint8_t> &entity_state,
		std::vector<uint8_t> &crop_growth_policy_state,
		uint64_t *output_session_id_out) noexcept;
	static int32_t load_file_with_revision_server_checkpoint(
		const char *file_path, game_character &character,
		WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &transaction_journal,
		std::vector<uint8_t> &card_session_state,
		std::vector<uint8_t> &regeneration_rules_state,
		std::vector<uint8_t> &chunk_instability_state,
		std::vector<uint8_t> &revision_metadata_state,
		std::vector<uint8_t> &fluid_state,
		std::vector<uint8_t> &entity_state,
		std::vector<uint8_t> &crop_growth_policy_state,
		uint64_t *output_session_id_out,
		std::vector<uint8_t> &revision_server_state) noexcept;
	static int32_t load_file_with_revision_server_checkpoint_and_host_config(
		const char *file_path, game_character &character,
		WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &transaction_journal,
		std::vector<uint8_t> &card_session_state,
		std::vector<uint8_t> &regeneration_rules_state,
		std::vector<uint8_t> &chunk_instability_state,
		std::vector<uint8_t> &revision_metadata_state,
		std::vector<uint8_t> &fluid_state,
		std::vector<uint8_t> &entity_state,
		std::vector<uint8_t> &crop_growth_policy_state,
		uint64_t *output_session_id_out,
		std::vector<uint8_t> &revision_server_state,
		std::vector<uint8_t> &host_config_state) noexcept;
};

#endif
