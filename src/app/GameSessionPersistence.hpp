#ifndef GAME_SESSION_PERSISTENCE_HPP
# define GAME_SESSION_PERSISTENCE_HPP

#include "../entities/WorldItemStore.hpp"
#include "../../Libft/Modules/Game/game_character.hpp"
#include "../../Libft/Modules/Game/game_recipe_book.hpp"
#include "../crafting/MinecraftRecipeCatalog.hpp"
#include "../transactions/GameTransactionJournal.hpp"
#include <cstdint>
#include <vector>

class GameSessionPersistence
{
  public:
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
};

#endif
