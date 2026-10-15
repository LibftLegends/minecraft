#include "GameSessionPersistence.hpp"
#include "GameSessionEntityState.hpp"
#include "../entities/WorldEntityStore.hpp"

#include "../../Libft/Modules/Game/game_world.hpp"
#include "../../Libft/Modules/Filesystem/filesystem.hpp"
#include "../../Libft/Modules/File/file_utils.hpp"
#include <algorithm>
#include <cstdio>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace
{
	static const std::size_t SESSION_SNAPSHOT_MAX_BYTES =
		128U * 1024U * 1024U;
	static const uint32_t SESSION_SNAPSHOT_VERSION = 5U;
	static const uint32_t SESSION_SNAPSHOT_VERSION_WITH_JOURNAL = 6U;
	static const uint32_t SESSION_SNAPSHOT_VERSION_WITH_CARDS = 7U;
	static const uint32_t SESSION_SNAPSHOT_VERSION_WITH_REGENERATION_RULES = 8U;
	static const uint32_t SESSION_SNAPSHOT_VERSION_WITH_CHUNK_INSTABILITY = 9U;
	static const uint32_t SESSION_SNAPSHOT_VERSION_WITH_REVISION_METADATA = 10U;
	static const uint32_t SESSION_SNAPSHOT_VERSION_WITH_FLUID_STATE = 11U;
	static const uint32_t SESSION_SNAPSHOT_VERSION_WITH_ENTITIES = 12U;
	static const uint32_t SESSION_SNAPSHOT_VERSION_WITH_SPAWNERS = 13U;
	static const uint32_t SESSION_SNAPSHOT_VERSION_WITH_CROP_POLICY = 14U;
	static const uint32_t SESSION_SNAPSHOT_VERSION_WITH_CROP_HYDRATION = 15U;
	static const uint32_t SESSION_SNAPSHOT_VERSION_WITH_CROP_DEADLINES = 16U;
	static const uint32_t SESSION_SNAPSHOT_VERSION_WITH_OUTPUT_SESSION_ID = 17U;
	static const uint32_t SESSION_SNAPSHOT_VERSION_WITH_REVISION_SERVER_STATE = 18U;

	static void append_u32(std::vector<uint8_t> &bytes, uint32_t value)
	{
		for (uint32_t shift = 0U; shift < 32U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	static void append_u64(std::vector<uint8_t> &bytes, uint64_t value)
	{
		for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	static uint64_t checksum(const uint8_t *bytes, std::size_t size) noexcept
	{
		uint64_t hash = 14695981039346656037ULL;
		for (std::size_t index = 0U; index < size; ++index)
		{
			hash ^= bytes[index];
			hash *= 1099511628211ULL;
		}
		return (hash);
	}

	struct Reader
	{
		const uint8_t *bytes;
		std::size_t size;
		std::size_t offset;

		bool read_u32(uint32_t *value) noexcept
		{
			if (value == nullptr || offset > size || size - offset < 4U)
				return (false);
			*value = static_cast<uint32_t>(bytes[offset])
				| (static_cast<uint32_t>(bytes[offset + 1U]) << 8U)
				| (static_cast<uint32_t>(bytes[offset + 2U]) << 16U)
				| (static_cast<uint32_t>(bytes[offset + 3U]) << 24U);
			offset += 4U;
			return (true);
		}

		bool read_u64(uint64_t *value) noexcept
		{
			uint32_t low;
			uint32_t high;

			if (!read_u32(&low) || !read_u32(&high))
				return (false);
			*value = static_cast<uint64_t>(low)
				| (static_cast<uint64_t>(high) << 32U);
			return (true);
		}
	};

	static int32_t read_file(const char *path,
		std::vector<uint8_t> &bytes) noexcept
	{
		FILE *file;
		long file_size;
		int32_t result;
		int extra_byte;

		file = std::fopen(path, "rb");
		if (file == nullptr)
			return (FT_ERR_IO);
		result = FT_ERR_SUCCESS;
		file_size = 0;
		if (std::fseek(file, 0, SEEK_END) != 0)
			result = FT_ERR_IO;
		if (result == FT_ERR_SUCCESS)
		{
			file_size = std::ftell(file);
			if (file_size < 0)
				result = FT_ERR_IO;
			else if (static_cast<uint64_t>(file_size)
				> SESSION_SNAPSHOT_MAX_BYTES)
				result = FT_ERR_OUT_OF_RANGE;
		}
		if (result == FT_ERR_SUCCESS && std::fseek(file, 0, SEEK_SET) != 0)
			result = FT_ERR_IO;
		if (result == FT_ERR_SUCCESS)
		{
			try
			{
				bytes.resize(static_cast<std::size_t>(file_size));
			}
			catch (const std::bad_alloc &)
			{
				result = FT_ERR_NO_MEMORY;
			}
		}
		if (result == FT_ERR_SUCCESS
			&& std::fread(bytes.data(), 1U, bytes.size(), file) != bytes.size())
			result = FT_ERR_IO;
		if (result == FT_ERR_SUCCESS)
		{
			extra_byte = std::fgetc(file);
			if (extra_byte != EOF || std::ferror(file))
				result = FT_ERR_IO;
		}
		if (std::fclose(file) != 0 && result == FT_ERR_SUCCESS)
			result = FT_ERR_IO;
		return (result);
	}

	static int32_t load_legacy_world_items(const std::vector<uint8_t> &bytes,
		WorldItemStore &world_items) noexcept
	{
		if (bytes.size() < 4U || bytes[0] != 'W' || bytes[1] != 'I'
			|| bytes[2] != 'S' || bytes[3] != 'P')
			return (FT_ERR_NOT_FOUND);
		return (world_items.deserialize(bytes.data(), bytes.size()));
	}
}

static int32_t game_session_persistence_save_file(const char *file_path,
	const game_character &character,
	const WorldItemStore &world_items,
	const game_recipe_book *recipe_book, uint64_t next_instance_id,
	const MinecraftRecipeCatalog *recipe_catalog,
	const GameTransactionJournal *transaction_journal,
	const std::vector<uint8_t> *card_session_state,
	const std::vector<uint8_t> *regeneration_rules_state,
	const std::vector<uint8_t> *chunk_instability_state,
	uint32_t snapshot_version,
	const std::vector<uint8_t> *revision_metadata_state = nullptr,
	const std::vector<uint8_t> *fluid_state = nullptr,
		const std::vector<uint8_t> *entity_state = nullptr,
		const std::vector<uint8_t> *crop_growth_policy_state = nullptr,
		uint64_t output_session_id = 0U,
		const std::vector<uint8_t> *revision_server_state = nullptr) noexcept
{
	game_world serializer;
	ft_string character_buffer;
	ft_byte_buffer recipe_buffer;
	std::vector<uint8_t> item_bytes;
	std::vector<uint8_t> craft_action_cache_bytes;
	std::vector<uint8_t> processing_state_bytes;
	std::vector<uint8_t> transaction_journal_bytes;
	std::vector<uint8_t> output;
	int32_t result;

	if (file_path == nullptr || file_path[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	result = world_items.validate_instance_id_uniqueness(
		character.get_inventory());
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = serializer.initialize();
	if (result == FT_ERR_SUCCESS)
		result = character_buffer.initialize();
	if (result == FT_ERR_SUCCESS && recipe_book != nullptr)
		if (result == FT_ERR_SUCCESS)
			result = recipe_buffer.initialize();
	if (result == FT_ERR_SUCCESS)
		result = serializer.save_to_buffer(character_buffer, character,
			character.get_inventory());
	if (result == FT_ERR_SUCCESS)
		result = world_items.serialize(item_bytes);
	if (result == FT_ERR_SUCCESS && recipe_book != nullptr)
		result = recipe_book->export_book(recipe_buffer);
	if (result == FT_ERR_SUCCESS && snapshot_version >= 4U
		&& recipe_catalog != nullptr)
		result = recipe_catalog->serialize_craft_action_cache(
			craft_action_cache_bytes);
	if (result == FT_ERR_SUCCESS && snapshot_version >= 5U
		&& recipe_catalog != nullptr)
		result = recipe_catalog->serialize_processing_state(
			processing_state_bytes);
	if (result == FT_ERR_SUCCESS
		&& snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_JOURNAL)
	{
		if (transaction_journal == nullptr)
			result = FT_ERR_INVALID_ARGUMENT;
		else
			result = transaction_journal->serialize(transaction_journal_bytes);
	}
	if (result == FT_ERR_SUCCESS
		&& snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_CARDS)
	{
		if (card_session_state == nullptr || card_session_state->empty()
			|| card_session_state->size() > SESSION_SNAPSHOT_MAX_BYTES)
			result = FT_ERR_INVALID_ARGUMENT;
	}
	if (result == FT_ERR_SUCCESS
		&& snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_REGENERATION_RULES
		&& (regeneration_rules_state == nullptr
			|| regeneration_rules_state->empty()
			|| regeneration_rules_state->size() > SESSION_SNAPSHOT_MAX_BYTES))
		result = FT_ERR_INVALID_ARGUMENT;
	if (result == FT_ERR_SUCCESS
		&& snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_CHUNK_INSTABILITY
		&& (chunk_instability_state == nullptr
			|| chunk_instability_state->size() > SESSION_SNAPSHOT_MAX_BYTES))
		result = FT_ERR_INVALID_ARGUMENT;
	if (result == FT_ERR_SUCCESS
		&& snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_REVISION_METADATA
		&& (revision_metadata_state == nullptr
			|| revision_metadata_state->empty()
			|| revision_metadata_state->size() > 1024U * 1024U))
		result = FT_ERR_INVALID_ARGUMENT;
	if (result == FT_ERR_SUCCESS
		&& snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_FLUID_STATE
		&& (fluid_state == nullptr
			|| fluid_state->size() > 1024U * 1024U))
		result = FT_ERR_INVALID_ARGUMENT;
	if (result == FT_ERR_SUCCESS
		&& snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_ENTITIES
		&& (entity_state == nullptr || entity_state->empty()
			|| entity_state->size() > 16U * 1024U * 1024U))
		result = FT_ERR_INVALID_ARGUMENT;
	if (result == FT_ERR_SUCCESS
		&& snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_SPAWNERS)
		result = GameSessionEntityState::validate(entity_state->data(),
			entity_state->size());
	if (result == FT_ERR_SUCCESS
		&& snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_CROP_POLICY
		&& (crop_growth_policy_state == nullptr
			|| (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_CROP_DEADLINES
				? (crop_growth_policy_state->size() < 40U
					|| crop_growth_policy_state->size() > 1024U * 1024U)
				: crop_growth_policy_state->size()
					!= (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_CROP_HYDRATION
						? 20U : 16U))))
		result = FT_ERR_INVALID_ARGUMENT;
	if (result == FT_ERR_SUCCESS
		&& snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_REVISION_SERVER_STATE
		&& (revision_server_state == nullptr
			|| revision_server_state->size() > 64U * 1024U * 1024U))
		result = FT_ERR_INVALID_ARGUMENT;
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (character_buffer.size() == 0U
		|| character_buffer.size() > SESSION_SNAPSHOT_MAX_BYTES
		|| item_bytes.size() > SESSION_SNAPSHOT_MAX_BYTES)
		return (FT_ERR_OUT_OF_RANGE);
	try
	{
		output.reserve(40U + character_buffer.size() + item_bytes.size()
			+ (recipe_book == nullptr ? 0U : recipe_buffer.size())
			+ craft_action_cache_bytes.size() + processing_state_bytes.size()
			+ transaction_journal_bytes.size()
			+ (card_session_state == nullptr ? 0U : card_session_state->size())
			+ (regeneration_rules_state == nullptr ? 0U
				: regeneration_rules_state->size())
			+ (chunk_instability_state == nullptr ? 0U
				: chunk_instability_state->size())
			+ (revision_metadata_state == nullptr ? 0U
				: revision_metadata_state->size())
			+ (fluid_state == nullptr ? 0U : fluid_state->size())
			+ (entity_state == nullptr ? 0U : entity_state->size())
			+ (crop_growth_policy_state == nullptr ? 0U
				: crop_growth_policy_state->size())
			+ (revision_server_state == nullptr ? 0U
				: revision_server_state->size()));
		output.push_back('M');
		output.push_back('C');
		output.push_back('S');
		output.push_back('P');
		append_u32(output, snapshot_version);
		append_u64(output, character_buffer.size());
		append_u64(output, item_bytes.size());
		if (recipe_book != nullptr)
			append_u64(output, recipe_buffer.size());
		if (snapshot_version >= 3U)
			append_u64(output, next_instance_id);
		if (snapshot_version >= 4U)
			append_u64(output, craft_action_cache_bytes.size());
		if (snapshot_version >= 5U)
			append_u64(output, processing_state_bytes.size());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_JOURNAL)
			append_u64(output, transaction_journal_bytes.size());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_CARDS)
			append_u64(output, card_session_state->size());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_REGENERATION_RULES)
			append_u64(output, regeneration_rules_state->size());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_CHUNK_INSTABILITY)
			append_u64(output, chunk_instability_state->size());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_REVISION_METADATA)
			append_u64(output, revision_metadata_state->size());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_FLUID_STATE)
			append_u64(output, fluid_state->size());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_ENTITIES)
			append_u64(output, entity_state->size());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_CROP_POLICY)
			append_u64(output, crop_growth_policy_state->size());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_REVISION_SERVER_STATE)
			append_u64(output, revision_server_state->size());
		output.insert(output.end(),
			reinterpret_cast<const uint8_t *>(character_buffer.data()),
			reinterpret_cast<const uint8_t *>(character_buffer.data())
				+ character_buffer.size());
		output.insert(output.end(), item_bytes.begin(), item_bytes.end());
		if (recipe_book != nullptr && recipe_buffer.size() > 0U)
			output.insert(output.end(), recipe_buffer.data(),
				recipe_buffer.data() + recipe_buffer.size());
		output.insert(output.end(), craft_action_cache_bytes.begin(),
			craft_action_cache_bytes.end());
		output.insert(output.end(), processing_state_bytes.begin(),
			processing_state_bytes.end());
		output.insert(output.end(), transaction_journal_bytes.begin(),
			transaction_journal_bytes.end());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_CARDS)
			output.insert(output.end(), card_session_state->begin(),
				card_session_state->end());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_REGENERATION_RULES)
			output.insert(output.end(), regeneration_rules_state->begin(),
				regeneration_rules_state->end());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_CHUNK_INSTABILITY)
			output.insert(output.end(), chunk_instability_state->begin(),
				chunk_instability_state->end());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_REVISION_METADATA)
			output.insert(output.end(), revision_metadata_state->begin(),
				revision_metadata_state->end());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_FLUID_STATE)
			output.insert(output.end(), fluid_state->begin(), fluid_state->end());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_ENTITIES)
			output.insert(output.end(), entity_state->begin(), entity_state->end());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_CROP_POLICY)
			output.insert(output.end(), crop_growth_policy_state->begin(),
				crop_growth_policy_state->end());
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_OUTPUT_SESSION_ID)
			append_u64(output, output_session_id);
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_REVISION_SERVER_STATE)
			output.insert(output.end(), revision_server_state->begin(),
				revision_server_state->end());
		if (output.size() + sizeof(uint64_t) > SESSION_SNAPSHOT_MAX_BYTES)
			return (FT_ERR_OUT_OF_RANGE);
		append_u64(output, checksum(output.data(), output.size()));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (file_replace_safe(file_path,
		reinterpret_cast<const char *>(output.data()), output.size()));
}

int32_t GameSessionPersistence::save_file(const char *file_path,
	const game_character &character, const WorldItemStore &world_items) noexcept
{
	return (game_session_persistence_save_file(file_path, character,
		world_items, nullptr, 0U, nullptr, nullptr, nullptr, nullptr, nullptr, 1U));
}

int32_t GameSessionPersistence::save_file(const char *file_path,
	const game_character &character, const WorldItemStore &world_items,
	const game_recipe_book &recipe_book) noexcept
{
	return (game_session_persistence_save_file(file_path, character,
		world_items, &recipe_book, 0U, nullptr, nullptr, nullptr, nullptr, nullptr, 2U));
}

int32_t GameSessionPersistence::save_file(const char *file_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog) noexcept
{
	return (game_session_persistence_save_file(file_path, character,
		world_items, &recipe_catalog.recipe_book(),
		recipe_catalog.next_output_instance_id(), &recipe_catalog, nullptr,
			nullptr, nullptr, nullptr, 5U));
}

int32_t GameSessionPersistence::save_file(const char *file_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	const GameTransactionJournal &transaction_journal) noexcept
{
	return (game_session_persistence_save_file(file_path, character,
		world_items, &recipe_catalog.recipe_book(),
		recipe_catalog.next_output_instance_id(), &recipe_catalog,
		&transaction_journal, nullptr, nullptr,
			nullptr, SESSION_SNAPSHOT_VERSION_WITH_JOURNAL));
}

int32_t GameSessionPersistence::save_file(const char *file_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	const GameTransactionJournal &transaction_journal,
	const std::vector<uint8_t> &card_session_state) noexcept
{
	return (game_session_persistence_save_file(file_path, character,
		world_items, &recipe_catalog.recipe_book(),
		recipe_catalog.next_output_instance_id(), &recipe_catalog,
		&transaction_journal, &card_session_state,
		nullptr, nullptr, SESSION_SNAPSHOT_VERSION_WITH_CARDS));
}

int32_t GameSessionPersistence::save_file(const char *file_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	const GameTransactionJournal &transaction_journal,
	const std::vector<uint8_t> &card_session_state,
	const std::vector<uint8_t> &regeneration_rules_state) noexcept
{
	return (game_session_persistence_save_file(file_path, character,
		world_items, &recipe_catalog.recipe_book(),
		recipe_catalog.next_output_instance_id(), &recipe_catalog,
		&transaction_journal, &card_session_state, &regeneration_rules_state,
		nullptr,
		SESSION_SNAPSHOT_VERSION_WITH_REGENERATION_RULES));
}

int32_t GameSessionPersistence::save_file(const char *file_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	const GameTransactionJournal &transaction_journal,
	const std::vector<uint8_t> &card_session_state,
	const std::vector<uint8_t> &regeneration_rules_state,
	const std::vector<uint8_t> &chunk_instability_state) noexcept
{
	return (game_session_persistence_save_file(file_path, character,
		world_items, &recipe_catalog.recipe_book(),
		recipe_catalog.next_output_instance_id(), &recipe_catalog,
		&transaction_journal, &card_session_state, &regeneration_rules_state,
		&chunk_instability_state,
		SESSION_SNAPSHOT_VERSION_WITH_CHUNK_INSTABILITY));
}

int32_t GameSessionPersistence::save_file(const char *file_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	const GameTransactionJournal &transaction_journal,
	const std::vector<uint8_t> &card_session_state,
	const std::vector<uint8_t> &regeneration_rules_state,
	const std::vector<uint8_t> &chunk_instability_state,
	const std::vector<uint8_t> &revision_metadata_state) noexcept
{
	return (game_session_persistence_save_file(file_path, character,
		world_items, &recipe_catalog.recipe_book(),
		recipe_catalog.next_output_instance_id(), &recipe_catalog,
		&transaction_journal, &card_session_state, &regeneration_rules_state,
		&chunk_instability_state, SESSION_SNAPSHOT_VERSION_WITH_REVISION_METADATA,
		&revision_metadata_state));
}

int32_t GameSessionPersistence::save_file(const char *file_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	const GameTransactionJournal &transaction_journal,
	const std::vector<uint8_t> &card_session_state,
	const std::vector<uint8_t> &regeneration_rules_state,
	const std::vector<uint8_t> &chunk_instability_state,
	const std::vector<uint8_t> &revision_metadata_state,
	const std::vector<uint8_t> &fluid_state) noexcept
{
	return (game_session_persistence_save_file(file_path, character,
		world_items, &recipe_catalog.recipe_book(),
		recipe_catalog.next_output_instance_id(), &recipe_catalog,
		&transaction_journal, &card_session_state, &regeneration_rules_state,
		&chunk_instability_state, SESSION_SNAPSHOT_VERSION_WITH_FLUID_STATE,
		&revision_metadata_state, &fluid_state));
}

int32_t GameSessionPersistence::save_file(const char *file_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	const GameTransactionJournal &transaction_journal,
	const std::vector<uint8_t> &card_session_state,
	const std::vector<uint8_t> &regeneration_rules_state,
	const std::vector<uint8_t> &chunk_instability_state,
	const std::vector<uint8_t> &revision_metadata_state,
	const std::vector<uint8_t> &fluid_state,
	const std::vector<uint8_t> &entity_state) noexcept
{
	return (game_session_persistence_save_file(file_path, character,
		world_items, &recipe_catalog.recipe_book(),
		recipe_catalog.next_output_instance_id(), &recipe_catalog,
		&transaction_journal, &card_session_state, &regeneration_rules_state,
		&chunk_instability_state, SESSION_SNAPSHOT_VERSION_WITH_SPAWNERS,
		&revision_metadata_state, &fluid_state, &entity_state));
}

int32_t GameSessionPersistence::save_file(const char *file_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	const GameTransactionJournal &transaction_journal,
	const std::vector<uint8_t> &card_session_state,
	const std::vector<uint8_t> &regeneration_rules_state,
	const std::vector<uint8_t> &chunk_instability_state,
	const std::vector<uint8_t> &revision_metadata_state,
	const std::vector<uint8_t> &fluid_state,
	const std::vector<uint8_t> &entity_state,
	const std::vector<uint8_t> &crop_growth_policy_state) noexcept
{
	return (game_session_persistence_save_file(file_path, character,
		world_items, &recipe_catalog.recipe_book(),
		recipe_catalog.next_output_instance_id(), &recipe_catalog,
		&transaction_journal, &card_session_state, &regeneration_rules_state,
		&chunk_instability_state, SESSION_SNAPSHOT_VERSION_WITH_CROP_HYDRATION,
		&revision_metadata_state, &fluid_state, &entity_state,
		&crop_growth_policy_state));
}

int32_t GameSessionPersistence::save_file_with_crop_growth_state(
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
	const std::vector<uint8_t> &crop_growth_state) noexcept
{
	return (game_session_persistence_save_file(file_path, character,
		world_items, &recipe_catalog.recipe_book(),
		recipe_catalog.next_output_instance_id(), &recipe_catalog,
		&transaction_journal, &card_session_state, &regeneration_rules_state,
		&chunk_instability_state, SESSION_SNAPSHOT_VERSION_WITH_CROP_DEADLINES,
		&revision_metadata_state, &fluid_state, &entity_state,
		&crop_growth_state));
}

int32_t GameSessionPersistence::save_file_with_crop_growth_state_and_output_session_id(
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
	uint64_t output_session_id) noexcept
{
	return (game_session_persistence_save_file(file_path, character,
		world_items, &recipe_catalog.recipe_book(),
		recipe_catalog.next_output_instance_id(), &recipe_catalog,
		&transaction_journal, &card_session_state, &regeneration_rules_state,
		&chunk_instability_state, SESSION_SNAPSHOT_VERSION_WITH_OUTPUT_SESSION_ID,
		&revision_metadata_state, &fluid_state, &entity_state,
		&crop_growth_state, output_session_id));
}

int32_t GameSessionPersistence::save_file_with_revision_server_checkpoint(
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
	const std::vector<uint8_t> &revision_server_state) noexcept
{
	return (game_session_persistence_save_file(file_path, character,
		world_items, &recipe_catalog.recipe_book(),
		recipe_catalog.next_output_instance_id(), &recipe_catalog,
		&transaction_journal, &card_session_state, &regeneration_rules_state,
		&chunk_instability_state,
		SESSION_SNAPSHOT_VERSION_WITH_REVISION_SERVER_STATE,
		&revision_metadata_state, &fluid_state, &entity_state,
		&crop_growth_state, output_session_id, &revision_server_state));
}

static int32_t game_session_persistence_load_file(const char *file_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog *recipe_catalog,
	GameTransactionJournal *transaction_journal,
	std::vector<uint8_t> *card_session_state,
	std::vector<uint8_t> *regeneration_rules_state,
	std::vector<uint8_t> *chunk_instability_state,
	std::vector<uint8_t> *revision_metadata_state = nullptr,
	std::vector<uint8_t> *fluid_state = nullptr,
		std::vector<uint8_t> *entity_state = nullptr,
		std::vector<uint8_t> *crop_growth_policy_state = nullptr,
		uint64_t *output_session_id_out = nullptr,
		std::vector<uint8_t> *revision_server_state = nullptr) noexcept
{
	std::vector<uint8_t> bytes;
	std::vector<uint8_t> staged_card_session_state;
	std::vector<uint8_t> staged_regeneration_rules_state;
	std::vector<uint8_t> staged_chunk_instability_state;
	std::vector<uint8_t> staged_revision_metadata_state;
	std::vector<uint8_t> staged_fluid_state;
	std::vector<uint8_t> staged_entity_state;
	std::vector<uint8_t> staged_crop_growth_policy_state;
	std::vector<uint8_t> staged_revision_server_state;
	WorldItemStore staged_items;
	WorldEntityStore staged_entities;
	game_character staged_character;
	GameTransactionJournal staged_transaction_journal;
	game_world loader;
	ft_byte_buffer recipe_buffer;
	Reader reader = {};
	Reader session_id_reader = {};
	uint64_t character_size;
	uint64_t item_size;
	uint64_t recipe_size = 0U;
	uint64_t craft_action_cache_size = 0U;
	uint64_t processing_state_size = 0U;
	uint64_t transaction_journal_size = 0U;
	uint64_t card_session_size = 0U;
	uint64_t regeneration_rules_size = 0U;
	uint64_t chunk_instability_size = 0U;
	uint64_t revision_metadata_size = 0U;
	uint64_t fluid_state_size = 0U;
	uint64_t entity_state_size = 0U;
	uint64_t crop_growth_policy_size = 0U;
	uint64_t revision_server_state_size = 0U;
	uint64_t restored_output_session_id = 0U;
	uint64_t next_instance_id = 1U;
	uint64_t maximum_instance_id = 0U;
	uint64_t stored_checksum;
	uint32_t version;
	std::string character_json;
	std::size_t payload_end;
	int32_t result;

	if (file_path == nullptr || file_path[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	if (transaction_journal != nullptr
		&& !transaction_journal->is_initialized())
		return (FT_ERR_INVALID_OPERATION);
	result = read_file(file_path, bytes);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = load_legacy_world_items(bytes, world_items);
	if (result != FT_ERR_NOT_FOUND)
	{
		if (result == FT_ERR_SUCCESS && transaction_journal != nullptr)
		{
			result = staged_transaction_journal.initialize();
			if (result == FT_ERR_SUCCESS)
				result = transaction_journal->replace_from(
					staged_transaction_journal);
		}
		if (result == FT_ERR_SUCCESS && card_session_state != nullptr)
			card_session_state->clear();
		if (result == FT_ERR_SUCCESS && regeneration_rules_state != nullptr)
			regeneration_rules_state->clear();
		if (result == FT_ERR_SUCCESS && chunk_instability_state != nullptr)
			chunk_instability_state->clear();
		if (result == FT_ERR_SUCCESS && revision_metadata_state != nullptr)
			revision_metadata_state->clear();
		if (result == FT_ERR_SUCCESS && fluid_state != nullptr)
			fluid_state->clear();
		if (result == FT_ERR_SUCCESS && entity_state != nullptr)
			entity_state->clear();
		if (result == FT_ERR_SUCCESS && crop_growth_policy_state != nullptr)
			crop_growth_policy_state->clear();
		if (result == FT_ERR_SUCCESS && revision_server_state != nullptr)
			revision_server_state->clear();
		if (result == FT_ERR_SUCCESS && output_session_id_out != nullptr)
			*output_session_id_out = 0U;
		return (result);
	}
	result = FT_ERR_SUCCESS;
	if (bytes.size() < 32U || bytes[0] != 'M' || bytes[1] != 'C'
		|| bytes[2] != 'S' || bytes[3] != 'P')
		return (FT_ERR_INVALID_ARGUMENT);
	stored_checksum = 0U;
	for (uint32_t index = 0U; index < 8U; ++index)
		stored_checksum |= static_cast<uint64_t>(
			bytes[bytes.size() - 8U + index]) << (index * 8U);
	if (checksum(bytes.data(), bytes.size() - 8U) != stored_checksum)
		return (FT_ERR_INVALID_ARGUMENT);
	reader.bytes = bytes.data();
	reader.size = bytes.size() - 8U;
	reader.offset = 4U;
	if (!reader.read_u32(&version) || (version < 1U
		|| version > SESSION_SNAPSHOT_VERSION_WITH_REVISION_SERVER_STATE)
		|| !reader.read_u64(&character_size)
		|| !reader.read_u64(&item_size)
		|| (version >= 2U
			&& !reader.read_u64(&recipe_size))
		|| (version >= 3U && !reader.read_u64(&next_instance_id))
		|| (version >= 4U && !reader.read_u64(&craft_action_cache_size))
		|| (version >= 5U && !reader.read_u64(&processing_state_size))
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_JOURNAL
			&& !reader.read_u64(&transaction_journal_size))
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_CARDS
			&& !reader.read_u64(&card_session_size))
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_REGENERATION_RULES
			&& !reader.read_u64(&regeneration_rules_size))
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_CHUNK_INSTABILITY
			&& !reader.read_u64(&chunk_instability_size))
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_REVISION_METADATA
			&& !reader.read_u64(&revision_metadata_size))
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_FLUID_STATE
			&& !reader.read_u64(&fluid_state_size))
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_ENTITIES
			&& !reader.read_u64(&entity_state_size))
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_CROP_POLICY
			&& !reader.read_u64(&crop_growth_policy_size))
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_REVISION_SERVER_STATE
			&& !reader.read_u64(&revision_server_state_size))
		|| character_size == 0U
		|| character_size > SESSION_SNAPSHOT_MAX_BYTES
		|| item_size > SESSION_SNAPSHOT_MAX_BYTES
		|| recipe_size > SESSION_SNAPSHOT_MAX_BYTES
		|| craft_action_cache_size > SESSION_SNAPSHOT_MAX_BYTES
		|| processing_state_size > SESSION_SNAPSHOT_MAX_BYTES
		|| transaction_journal_size > SESSION_SNAPSHOT_MAX_BYTES
		|| card_session_size > SESSION_SNAPSHOT_MAX_BYTES
		|| regeneration_rules_size > SESSION_SNAPSHOT_MAX_BYTES
		|| chunk_instability_size > SESSION_SNAPSHOT_MAX_BYTES
		|| revision_metadata_size > 1024U * 1024U
		|| fluid_state_size > 1024U * 1024U
		|| entity_state_size > 16U * 1024U * 1024U
		|| crop_growth_policy_size > 1024U * 1024U
		|| revision_server_state_size > 64U * 1024U * 1024U
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_CROP_POLICY
			&& (crop_growth_policy_state == nullptr
				|| (version >= SESSION_SNAPSHOT_VERSION_WITH_CROP_DEADLINES
					? crop_growth_policy_size < 40U
					: crop_growth_policy_size
						!= (version >= SESSION_SNAPSHOT_VERSION_WITH_CROP_HYDRATION
							? 20U : 16U))))
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_ENTITIES
			&& entity_state_size < 24U)
		|| (version >= 2U && recipe_size == 0U)
		|| (version >= 4U && craft_action_cache_size == 0U)
		|| (version >= 5U && processing_state_size == 0U)
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_JOURNAL
			&& (transaction_journal_size == 0U
				|| transaction_journal == nullptr))
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_CARDS
			&& (card_session_size == 0U || card_session_state == nullptr))
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_REGENERATION_RULES
			&& (regeneration_rules_size == 0U
				|| regeneration_rules_state == nullptr))
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_REVISION_METADATA
			&& (revision_metadata_size == 0U
				|| revision_metadata_state == nullptr))
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_FLUID_STATE
			&& fluid_state == nullptr)
		|| character_size > reader.size - reader.offset
		|| item_size > reader.size - reader.offset
			- static_cast<std::size_t>(character_size)
		|| recipe_size > reader.size - reader.offset
			- static_cast<std::size_t>(character_size)
			- static_cast<std::size_t>(item_size)
		|| craft_action_cache_size > reader.size - reader.offset
			- static_cast<std::size_t>(character_size)
			- static_cast<std::size_t>(item_size)
			- static_cast<std::size_t>(recipe_size)
		|| processing_state_size > reader.size - reader.offset
			- static_cast<std::size_t>(character_size)
			- static_cast<std::size_t>(item_size)
			- static_cast<std::size_t>(recipe_size)
			- static_cast<std::size_t>(craft_action_cache_size)
		|| transaction_journal_size > reader.size - reader.offset
			- static_cast<std::size_t>(character_size)
			- static_cast<std::size_t>(item_size)
			- static_cast<std::size_t>(recipe_size)
			- static_cast<std::size_t>(craft_action_cache_size)
			- static_cast<std::size_t>(processing_state_size)
		|| card_session_size > reader.size - reader.offset
			- static_cast<std::size_t>(character_size)
			- static_cast<std::size_t>(item_size)
			- static_cast<std::size_t>(recipe_size)
			- static_cast<std::size_t>(craft_action_cache_size)
			- static_cast<std::size_t>(processing_state_size)
			- static_cast<std::size_t>(transaction_journal_size)
		|| regeneration_rules_size > reader.size - reader.offset
			- static_cast<std::size_t>(character_size)
			- static_cast<std::size_t>(item_size)
			- static_cast<std::size_t>(recipe_size)
			- static_cast<std::size_t>(craft_action_cache_size)
			- static_cast<std::size_t>(processing_state_size)
			- static_cast<std::size_t>(transaction_journal_size)
			- static_cast<std::size_t>(card_session_size)
		|| chunk_instability_size > reader.size - reader.offset
			- static_cast<std::size_t>(character_size)
			- static_cast<std::size_t>(item_size)
			- static_cast<std::size_t>(recipe_size)
			- static_cast<std::size_t>(craft_action_cache_size)
			- static_cast<std::size_t>(processing_state_size)
			- static_cast<std::size_t>(transaction_journal_size)
			- static_cast<std::size_t>(card_session_size)
			- static_cast<std::size_t>(regeneration_rules_size)
		|| revision_metadata_size > reader.size - reader.offset
			- static_cast<std::size_t>(character_size)
			- static_cast<std::size_t>(item_size)
			- static_cast<std::size_t>(recipe_size)
			- static_cast<std::size_t>(craft_action_cache_size)
			- static_cast<std::size_t>(processing_state_size)
			- static_cast<std::size_t>(transaction_journal_size)
			- static_cast<std::size_t>(card_session_size)
			- static_cast<std::size_t>(regeneration_rules_size)
			- static_cast<std::size_t>(chunk_instability_size)
		|| fluid_state_size > reader.size - reader.offset
			- static_cast<std::size_t>(character_size)
			- static_cast<std::size_t>(item_size)
			- static_cast<std::size_t>(recipe_size)
			- static_cast<std::size_t>(craft_action_cache_size)
			- static_cast<std::size_t>(processing_state_size)
			- static_cast<std::size_t>(transaction_journal_size)
			- static_cast<std::size_t>(card_session_size)
			- static_cast<std::size_t>(regeneration_rules_size)
			- static_cast<std::size_t>(chunk_instability_size)
			- static_cast<std::size_t>(revision_metadata_size)
		|| entity_state_size > reader.size - reader.offset
			- static_cast<std::size_t>(character_size)
			- static_cast<std::size_t>(item_size)
			- static_cast<std::size_t>(recipe_size)
			- static_cast<std::size_t>(craft_action_cache_size)
			- static_cast<std::size_t>(processing_state_size)
			- static_cast<std::size_t>(transaction_journal_size)
			- static_cast<std::size_t>(card_session_size)
			- static_cast<std::size_t>(regeneration_rules_size)
			- static_cast<std::size_t>(chunk_instability_size)
			- static_cast<std::size_t>(revision_metadata_size)
			- static_cast<std::size_t>(fluid_state_size)
		|| crop_growth_policy_size > reader.size - reader.offset
			- static_cast<std::size_t>(character_size)
			- static_cast<std::size_t>(item_size)
			- static_cast<std::size_t>(recipe_size)
			- static_cast<std::size_t>(craft_action_cache_size)
			- static_cast<std::size_t>(processing_state_size)
			- static_cast<std::size_t>(transaction_journal_size)
			- static_cast<std::size_t>(card_session_size)
			- static_cast<std::size_t>(regeneration_rules_size)
			- static_cast<std::size_t>(chunk_instability_size)
			- static_cast<std::size_t>(revision_metadata_size)
			- static_cast<std::size_t>(fluid_state_size)
			- static_cast<std::size_t>(entity_state_size)
		|| revision_server_state_size > reader.size - reader.offset
			- static_cast<std::size_t>(character_size)
			- static_cast<std::size_t>(item_size)
			- static_cast<std::size_t>(recipe_size)
			- static_cast<std::size_t>(craft_action_cache_size)
			- static_cast<std::size_t>(processing_state_size)
			- static_cast<std::size_t>(transaction_journal_size)
			- static_cast<std::size_t>(card_session_size)
			- static_cast<std::size_t>(regeneration_rules_size)
			- static_cast<std::size_t>(chunk_instability_size)
			- static_cast<std::size_t>(revision_metadata_size)
			- static_cast<std::size_t>(fluid_state_size)
			- static_cast<std::size_t>(entity_state_size)
			- static_cast<std::size_t>(crop_growth_policy_size)
			- (version >= SESSION_SNAPSHOT_VERSION_WITH_OUTPUT_SESSION_ID
				? 8U : 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	payload_end = reader.offset + static_cast<std::size_t>(character_size)
		+ static_cast<std::size_t>(item_size)
		+ static_cast<std::size_t>(recipe_size)
		+ static_cast<std::size_t>(craft_action_cache_size)
		+ static_cast<std::size_t>(processing_state_size)
		+ static_cast<std::size_t>(transaction_journal_size)
		+ static_cast<std::size_t>(card_session_size)
		+ static_cast<std::size_t>(regeneration_rules_size)
		+ static_cast<std::size_t>(chunk_instability_size);
	payload_end += static_cast<std::size_t>(revision_metadata_size);
	payload_end += static_cast<std::size_t>(fluid_state_size);
	if (payload_end + static_cast<std::size_t>(entity_state_size)
		+ static_cast<std::size_t>(crop_growth_policy_size)
		+ (version >= SESSION_SNAPSHOT_VERSION_WITH_OUTPUT_SESSION_ID
			? 8U : 0U)
		+ static_cast<std::size_t>(revision_server_state_size)
		!= reader.size)
		return (FT_ERR_INVALID_ARGUMENT);
	if (version >= SESSION_SNAPSHOT_VERSION_WITH_OUTPUT_SESSION_ID)
	{
		session_id_reader.bytes = bytes.data();
		session_id_reader.size = reader.size;
		session_id_reader.offset = payload_end
			+ static_cast<std::size_t>(entity_state_size)
			+ static_cast<std::size_t>(crop_growth_policy_size);
		if (!session_id_reader.read_u64(&restored_output_session_id))
			return (FT_ERR_INVALID_ARGUMENT);
	}
	if (entity_state_size > 0U && entity_state != nullptr)
	{
		try
		{
			staged_entity_state.assign(bytes.begin() + payload_end,
				bytes.begin() + payload_end
					+ static_cast<std::size_t>(entity_state_size));
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	if (crop_growth_policy_size > 0U && crop_growth_policy_state != nullptr)
	{
		try
		{
			staged_crop_growth_policy_state.assign(
				bytes.begin() + payload_end
					+ static_cast<std::size_t>(entity_state_size),
				bytes.begin() + payload_end
					+ static_cast<std::size_t>(entity_state_size)
					+ static_cast<std::size_t>(crop_growth_policy_size));
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	if (revision_server_state_size > 0U && revision_server_state != nullptr)
	{
		try
		{
			staged_revision_server_state.assign(
				bytes.begin() + reader.size
					- static_cast<std::size_t>(revision_server_state_size),
				bytes.begin() + reader.size);
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	if (version >= SESSION_SNAPSHOT_VERSION_WITH_SPAWNERS)
		result = GameSessionEntityState::validate(bytes.data() + payload_end,
			static_cast<std::size_t>(entity_state_size));
	else if (version >= SESSION_SNAPSHOT_VERSION_WITH_ENTITIES)
	{
		result = staged_entities.initialize();
		if (result == FT_ERR_SUCCESS)
			result = staged_entities.deserialize(bytes.data() + payload_end,
				static_cast<std::size_t>(entity_state_size));
	}
	if (result != FT_ERR_SUCCESS)
		return (result);
	try
	{
		character_json.assign(
			reinterpret_cast<const char *>(bytes.data() + reader.offset),
			static_cast<std::size_t>(character_size));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (character_json.find('\0') != std::string::npos)
		return (FT_ERR_INVALID_ARGUMENT);
	reader.offset += static_cast<std::size_t>(character_size);
	result = staged_items.initialize();
	if (result == FT_ERR_SUCCESS && transaction_journal != nullptr)
		result = staged_transaction_journal.initialize();
	if (result == FT_ERR_SUCCESS)
	{
		result = staged_items.deserialize(bytes.data() + reader.offset,
			static_cast<std::size_t>(item_size));
	}
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (transaction_journal != nullptr
		&& version >= SESSION_SNAPSHOT_VERSION_WITH_JOURNAL)
	{
		const std::size_t journal_offset = payload_end
			- static_cast<std::size_t>(fluid_state_size)
			- static_cast<std::size_t>(revision_metadata_size)
			- static_cast<std::size_t>(chunk_instability_size)
			- static_cast<std::size_t>(regeneration_rules_size)
			- static_cast<std::size_t>(card_session_size)
			- static_cast<std::size_t>(transaction_journal_size);

		result = staged_transaction_journal.deserialize(
			bytes.data() + journal_offset,
			static_cast<std::size_t>(transaction_journal_size));
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	if (card_session_size > 0U)
	{
		const std::size_t card_offset = payload_end
			- static_cast<std::size_t>(fluid_state_size)
			- static_cast<std::size_t>(revision_metadata_size)
			- static_cast<std::size_t>(chunk_instability_size)
			- static_cast<std::size_t>(regeneration_rules_size)
			- static_cast<std::size_t>(card_session_size);

		try
		{
			staged_card_session_state.assign(bytes.begin() + card_offset,
				bytes.begin() + card_offset
					+ static_cast<std::size_t>(card_session_size));
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	if (regeneration_rules_size > 0U)
	{
		const std::size_t rules_offset = payload_end
			- static_cast<std::size_t>(fluid_state_size)
			- static_cast<std::size_t>(revision_metadata_size)
			- static_cast<std::size_t>(chunk_instability_size)
			- static_cast<std::size_t>(regeneration_rules_size);

		try
		{
			staged_regeneration_rules_state.assign(bytes.begin() + rules_offset,
				bytes.begin() + rules_offset
					+ static_cast<std::size_t>(regeneration_rules_size));
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	if (chunk_instability_size > 0U && chunk_instability_state != nullptr)
	{
		const std::size_t instability_offset = payload_end
			- static_cast<std::size_t>(fluid_state_size)
			- static_cast<std::size_t>(revision_metadata_size)
			- static_cast<std::size_t>(chunk_instability_size);
		try
		{
			staged_chunk_instability_state.assign(
				bytes.begin() + instability_offset,
				bytes.begin() + instability_offset
					+ static_cast<std::size_t>(chunk_instability_size));
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	if (revision_metadata_size > 0U && revision_metadata_state != nullptr)
	{
		const std::size_t metadata_offset = payload_end
			- static_cast<std::size_t>(fluid_state_size)
			- static_cast<std::size_t>(revision_metadata_size);
		const std::size_t metadata_end = metadata_offset
			+ static_cast<std::size_t>(revision_metadata_size);
		try
		{
			staged_revision_metadata_state.assign(
				bytes.begin() + metadata_offset, bytes.begin() + metadata_end);
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	if (fluid_state_size > 0U && fluid_state != nullptr)
	{
		const std::size_t fluid_offset = payload_end
			- static_cast<std::size_t>(fluid_state_size);

		try
		{
			staged_fluid_state.assign(bytes.begin() + fluid_offset,
				bytes.begin() + payload_end);
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	result = loader.initialize();
	if (result == FT_ERR_SUCCESS)
		result = staged_character.initialize();
	if (result == FT_ERR_SUCCESS)
	{
		result = loader.load_from_buffer(character_json.c_str(), staged_character,
			staged_character.get_inventory());
	}
	if (result == FT_ERR_SUCCESS)
	{
		result = staged_items.validate_instance_id_uniqueness(
			staged_character.get_inventory());
	}
	if (result == FT_ERR_SUCCESS && recipe_catalog != nullptr
		&& version >= 3U)
	{
		game_inventory_snapshot inventory_snapshot = {};
		uint64_t world_maximum_instance_id = 0U;

		result = staged_character.get_inventory().get_snapshot(
			inventory_snapshot);
		if (result == FT_ERR_SUCCESS)
			result = staged_items.get_max_item_instance_id(
				&world_maximum_instance_id);
		if (result == FT_ERR_SUCCESS)
		{
			maximum_instance_id = world_maximum_instance_id;
			for (const game_inventory_item_snapshot &item
				: inventory_snapshot.items)
			{
				if (item.instance_id > maximum_instance_id)
					maximum_instance_id = item.instance_id;
			}
			if (next_instance_id != 0U
				&& next_instance_id <= maximum_instance_id)
				next_instance_id = maximum_instance_id
					== std::numeric_limits<uint64_t>::max()
					? 0U : maximum_instance_id + 1U;
		}
	}
	if (result == FT_ERR_SUCCESS && recipe_catalog != nullptr
		&& recipe_size > 0U)
	{
		if (version >= 4U)
			result = recipe_catalog->validate_craft_action_cache(
				bytes.data() + reader.offset
					+ static_cast<std::size_t>(item_size)
					+ static_cast<std::size_t>(recipe_size),
				static_cast<std::size_t>(craft_action_cache_size));
		if (result == FT_ERR_SUCCESS && version >= 5U)
			result = recipe_catalog->validate_processing_state(
				bytes.data() + reader.offset
					+ static_cast<std::size_t>(item_size)
					+ static_cast<std::size_t>(recipe_size)
					+ static_cast<std::size_t>(craft_action_cache_size),
				static_cast<std::size_t>(processing_state_size));
		if (result == FT_ERR_SUCCESS)
			result = recipe_buffer.initialize();
		if (result == FT_ERR_SUCCESS)
			result = recipe_buffer.append(
				bytes.data() + reader.offset + static_cast<std::size_t>(item_size),
				static_cast<ft_size_t>(recipe_size));
		if (result == FT_ERR_SUCCESS)
		{
			result = recipe_catalog->import_book(recipe_buffer);
		}
		if (result == FT_ERR_SUCCESS && version >= 3U)
			result = recipe_catalog->restore_next_output_instance_id(
				next_instance_id);
		if (result == FT_ERR_SUCCESS && version >= 4U)
			result = recipe_catalog->restore_craft_action_cache(
				bytes.data() + reader.offset
					+ static_cast<std::size_t>(item_size)
					+ static_cast<std::size_t>(recipe_size),
				static_cast<std::size_t>(craft_action_cache_size));
		if (result == FT_ERR_SUCCESS && version >= 5U)
			result = recipe_catalog->restore_processing_state(
				bytes.data() + reader.offset
					+ static_cast<std::size_t>(item_size)
					+ static_cast<std::size_t>(recipe_size)
					+ static_cast<std::size_t>(craft_action_cache_size),
				static_cast<std::size_t>(processing_state_size));
	}
	if (result == FT_ERR_SUCCESS && recipe_catalog != nullptr
		&& version < 4U)
		result = recipe_catalog->clear_craft_action_cache();
	if (result == FT_ERR_SUCCESS && recipe_catalog != nullptr
		&& version < 5U)
		result = recipe_catalog->clear_processing_state();
	if (result == FT_ERR_SUCCESS)
	{
		result = loader.load_from_buffer(character_json.c_str(), character,
			character.get_inventory());
	}
	if (result == FT_ERR_SUCCESS)
	{
		result = world_items.deserialize(bytes.data() + reader.offset,
			static_cast<std::size_t>(item_size));
	}
	if (result == FT_ERR_SUCCESS && transaction_journal != nullptr)
		result = transaction_journal->replace_from(staged_transaction_journal);
	if (result == FT_ERR_SUCCESS && card_session_state != nullptr)
		card_session_state->swap(staged_card_session_state);
	if (result == FT_ERR_SUCCESS && regeneration_rules_state != nullptr)
		regeneration_rules_state->swap(staged_regeneration_rules_state);
	if (result == FT_ERR_SUCCESS && chunk_instability_state != nullptr)
		chunk_instability_state->swap(staged_chunk_instability_state);
	if (result == FT_ERR_SUCCESS && revision_metadata_state != nullptr)
		revision_metadata_state->swap(staged_revision_metadata_state);
	if (result == FT_ERR_SUCCESS && fluid_state != nullptr)
		fluid_state->swap(staged_fluid_state);
	if (result == FT_ERR_SUCCESS && entity_state != nullptr)
		entity_state->swap(staged_entity_state);
	if (result == FT_ERR_SUCCESS && crop_growth_policy_state != nullptr)
		crop_growth_policy_state->swap(staged_crop_growth_policy_state);
	if (result == FT_ERR_SUCCESS && revision_server_state != nullptr)
		revision_server_state->swap(staged_revision_server_state);
	if (result == FT_ERR_SUCCESS && output_session_id_out != nullptr)
		*output_session_id_out = restored_output_session_id;
	return (result);
}

int32_t GameSessionPersistence::load_file(const char *file_path,
	game_character &character, WorldItemStore &world_items) noexcept
{
	return (game_session_persistence_load_file(file_path, character,
		world_items, nullptr, nullptr, nullptr, nullptr, nullptr));
}

int32_t GameSessionPersistence::load_file(const char *file_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog) noexcept
{
	return (game_session_persistence_load_file(file_path, character,
		world_items, &recipe_catalog, nullptr, nullptr, nullptr, nullptr));
}

int32_t GameSessionPersistence::load_file(const char *file_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &transaction_journal) noexcept
{
	return (game_session_persistence_load_file(file_path, character,
		world_items, &recipe_catalog, &transaction_journal, nullptr, nullptr,
		nullptr));
}

int32_t GameSessionPersistence::load_file(const char *file_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &transaction_journal,
	std::vector<uint8_t> &card_session_state) noexcept
{
	return (game_session_persistence_load_file(file_path, character,
		world_items, &recipe_catalog, &transaction_journal,
		&card_session_state, nullptr, nullptr));
}

int32_t GameSessionPersistence::load_file(const char *file_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &transaction_journal,
	std::vector<uint8_t> &card_session_state,
	std::vector<uint8_t> &regeneration_rules_state) noexcept
{
	return (game_session_persistence_load_file(file_path, character,
		world_items, &recipe_catalog, &transaction_journal,
		&card_session_state, &regeneration_rules_state, nullptr));
}

int32_t GameSessionPersistence::load_file(const char *file_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &transaction_journal,
	std::vector<uint8_t> &card_session_state,
	std::vector<uint8_t> &regeneration_rules_state,
	std::vector<uint8_t> &chunk_instability_state) noexcept
{
	return (game_session_persistence_load_file(file_path, character,
		world_items, &recipe_catalog, &transaction_journal,
		&card_session_state, &regeneration_rules_state,
		&chunk_instability_state));
}

int32_t GameSessionPersistence::load_file(const char *file_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &transaction_journal,
	std::vector<uint8_t> &card_session_state,
	std::vector<uint8_t> &regeneration_rules_state,
	std::vector<uint8_t> &chunk_instability_state,
	std::vector<uint8_t> &revision_metadata_state) noexcept
{
	return (game_session_persistence_load_file(file_path, character,
		world_items, &recipe_catalog, &transaction_journal,
		&card_session_state, &regeneration_rules_state,
		&chunk_instability_state, &revision_metadata_state));
}

int32_t GameSessionPersistence::load_file(const char *file_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &transaction_journal,
	std::vector<uint8_t> &card_session_state,
	std::vector<uint8_t> &regeneration_rules_state,
	std::vector<uint8_t> &chunk_instability_state,
	std::vector<uint8_t> &revision_metadata_state,
	std::vector<uint8_t> &fluid_state) noexcept
{
	return (game_session_persistence_load_file(file_path, character,
		world_items, &recipe_catalog, &transaction_journal,
		&card_session_state, &regeneration_rules_state,
		&chunk_instability_state, &revision_metadata_state, &fluid_state));
}

int32_t GameSessionPersistence::load_file(const char *file_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &transaction_journal,
	std::vector<uint8_t> &card_session_state,
	std::vector<uint8_t> &regeneration_rules_state,
	std::vector<uint8_t> &chunk_instability_state,
	std::vector<uint8_t> &revision_metadata_state,
	std::vector<uint8_t> &fluid_state,
	std::vector<uint8_t> &entity_state) noexcept
{
	return (game_session_persistence_load_file(file_path, character,
		world_items, &recipe_catalog, &transaction_journal,
		&card_session_state, &regeneration_rules_state,
		&chunk_instability_state, &revision_metadata_state, &fluid_state,
		&entity_state));
}

int32_t GameSessionPersistence::load_file(const char *file_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &transaction_journal,
	std::vector<uint8_t> &card_session_state,
	std::vector<uint8_t> &regeneration_rules_state,
	std::vector<uint8_t> &chunk_instability_state,
	std::vector<uint8_t> &revision_metadata_state,
	std::vector<uint8_t> &fluid_state,
	std::vector<uint8_t> &entity_state,
	std::vector<uint8_t> &crop_growth_policy_state) noexcept
{
	return (game_session_persistence_load_file(file_path, character,
		world_items, &recipe_catalog, &transaction_journal,
		&card_session_state, &regeneration_rules_state,
		&chunk_instability_state, &revision_metadata_state, &fluid_state,
		&entity_state, &crop_growth_policy_state));
}

int32_t GameSessionPersistence::load_file_with_output_session_id(
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
	uint64_t *output_session_id_out) noexcept
{
	if (output_session_id_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (game_session_persistence_load_file(file_path, character,
		world_items, &recipe_catalog, &transaction_journal,
		&card_session_state, &regeneration_rules_state,
		&chunk_instability_state, &revision_metadata_state, &fluid_state,
		&entity_state, &crop_growth_policy_state, output_session_id_out));
}

int32_t GameSessionPersistence::load_file_with_revision_server_checkpoint(
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
	std::vector<uint8_t> &revision_server_state) noexcept
{
	if (output_session_id_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (game_session_persistence_load_file(file_path, character,
		world_items, &recipe_catalog, &transaction_journal,
		&card_session_state, &regeneration_rules_state,
		&chunk_instability_state, &revision_metadata_state, &fluid_state,
		&entity_state, &crop_growth_policy_state, output_session_id_out,
		&revision_server_state));
}
