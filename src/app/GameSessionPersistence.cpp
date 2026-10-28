#include "GameSessionPersistence.hpp"
#include "GameSession.hpp"
#include "GameSessionEntityState.hpp"
#include "../entities/WorldEntityStore.hpp"
#include "../world/World.hpp"

#include "../../Libft/Modules/Game/game_world.hpp"
#include "../../Libft/Modules/Game/game_voxel_chunk.hpp"
#include "../../Libft/Modules/Buffer/byte_buffer.hpp"
#include "../../Libft/Modules/Filesystem/filesystem.hpp"
#include "../../Libft/Modules/File/file_utils.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <set>
#include <string>
#include <utility>
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
	static const uint32_t SESSION_SNAPSHOT_VERSION_WITH_HOST_CONFIG = 19U;

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

	static bool is_valid_mcsp_snapshot(
		const std::vector<uint8_t> &bytes) noexcept
	{
		if (bytes.size() < 32U || bytes.size() > SESSION_SNAPSHOT_MAX_BYTES
			|| bytes[0] != 'M' || bytes[1] != 'C'
			|| bytes[2] != 'S' || bytes[3] != 'P')
			return (false);
		const uint32_t version = static_cast<uint32_t>(bytes[4U])
			| (static_cast<uint32_t>(bytes[5U]) << 8U)
			| (static_cast<uint32_t>(bytes[6U]) << 16U)
			| (static_cast<uint32_t>(bytes[7U]) << 24U);
		if (version == 0U
			|| version > SESSION_SNAPSHOT_VERSION_WITH_HOST_CONFIG)
			return (false);
		uint64_t stored_checksum = 0U;
		for (uint32_t index = 0U; index < 8U; ++index)
			stored_checksum |= static_cast<uint64_t>(
				bytes[bytes.size() - 8U + index]) << (index * 8U);
		return (checksum(bytes.data(), bytes.size() - 8U)
			== stored_checksum);
	}

	static bool valid_host_config_size(uint64_t size) noexcept
	{
		return (size == 24U || size == 56U || size == 88U || size == 124U);
	}

	static int32_t mcsp_snapshot_world_id(
		const std::vector<uint8_t> &bytes, uint8_t world_id_out[32],
		bool *world_id_present_out) noexcept
	{
		Reader reader = {};
		uint32_t version = 0U;
		uint64_t character_size = 0U;
		uint64_t item_size = 0U;
		uint64_t recipe_size = 0U;
		uint64_t craft_size = 0U;
		uint64_t processing_size = 0U;
		uint64_t journal_size = 0U;
		uint64_t cards_size = 0U;
		uint64_t rules_size = 0U;
		uint64_t instability_size = 0U;
		uint64_t metadata_size = 0U;
		uint64_t fluid_size = 0U;
		uint64_t entities_size = 0U;
		uint64_t crop_size = 0U;
		uint64_t revision_server_size = 0U;
		uint64_t host_config_size = 0U;
		uint64_t ignored_u64 = 0U;
		if (world_id_out == nullptr || world_id_present_out == nullptr
			|| !is_valid_mcsp_snapshot(bytes))
			return (FT_ERR_INVALID_ARGUMENT);
		reader = {bytes.data(), bytes.size() - 8U, 4U};
		if (!reader.read_u32(&version) || !reader.read_u64(&character_size)
			|| !reader.read_u64(&item_size)
			|| (version >= 2U && !reader.read_u64(&recipe_size))
			|| (version >= 3U && !reader.read_u64(&ignored_u64))
			|| (version >= 4U && !reader.read_u64(&craft_size))
			|| (version >= 5U && !reader.read_u64(&processing_size))
			|| (version >= 6U && !reader.read_u64(&journal_size))
			|| (version >= 7U && !reader.read_u64(&cards_size))
			|| (version >= 8U && !reader.read_u64(&rules_size))
			|| (version >= 9U && !reader.read_u64(&instability_size))
			|| (version >= 10U && !reader.read_u64(&metadata_size))
			|| (version >= 11U && !reader.read_u64(&fluid_size))
			|| (version >= 12U && !reader.read_u64(&entities_size))
			|| (version >= 14U && !reader.read_u64(&crop_size))
			|| (version >= 18U && !reader.read_u64(&revision_server_size))
			|| (version >= 19U && !reader.read_u64(&host_config_size)))
			return (FT_ERR_INVALID_ARGUMENT);
		auto skip_payload = [&reader](uint64_t size) -> bool
		{
			if (reader.offset > reader.size
				|| size > reader.size - reader.offset)
				return (false);
			reader.offset += static_cast<std::size_t>(size);
			return (true);
		};
		if (!skip_payload(character_size) || !skip_payload(item_size)
			|| !skip_payload(recipe_size) || !skip_payload(craft_size)
			|| !skip_payload(processing_size) || !skip_payload(journal_size)
			|| !skip_payload(cards_size) || !skip_payload(rules_size)
			|| !skip_payload(instability_size) || !skip_payload(metadata_size)
			|| !skip_payload(fluid_size) || !skip_payload(entities_size)
			|| !skip_payload(crop_size)
			|| (version >= 17U && !skip_payload(8U))
			|| !skip_payload(revision_server_size))
			return (FT_ERR_INVALID_ARGUMENT);
		*world_id_present_out = false;
		std::memset(world_id_out, 0, 32U);
		if (version < 19U)
			return (reader.offset == reader.size
				? FT_ERR_SUCCESS : FT_ERR_INVALID_ARGUMENT);
		if (!valid_host_config_size(host_config_size)
			|| host_config_size != reader.size - reader.offset)
			return (FT_ERR_INVALID_ARGUMENT);
		const uint8_t *config = reader.bytes + reader.offset;
		Reader config_reader = {config,
			static_cast<std::size_t>(host_config_size), 4U};
		Reader config_checksum_reader = {config,
			static_cast<std::size_t>(host_config_size),
			static_cast<std::size_t>(host_config_size - 8U)};
		uint32_t config_version = 0U;
		uint64_t stored_config_checksum = 0U;
		if (host_config_size < 24U || std::memcmp(config, "RBH1", 4U) != 0
			|| !config_reader.read_u32(&config_version)
			|| !config_checksum_reader.read_u64(&stored_config_checksum)
			|| (config_version == 1U && host_config_size != 24U)
			|| (config_version == 2U && host_config_size != 56U)
			|| (config_version == 3U && host_config_size != 88U)
			|| (config_version == 4U && host_config_size != 124U)
			|| config_version < 1U || config_version > 4U
			|| checksum(config, static_cast<std::size_t>(host_config_size - 8U))
				!= stored_config_checksum)
			return (FT_ERR_INVALID_ARGUMENT);
		if (config_version >= 2U)
		{
			std::memcpy(world_id_out, config + 16U, 32U);
			*world_id_present_out = true;
			bool nonzero = false;
			for (std::size_t index = 0U; index < 32U; ++index)
				if (world_id_out[index] != 0U)
					nonzero = true;
			if (!nonzero)
				return (FT_ERR_INVALID_ARGUMENT);
		}
		return (FT_ERR_SUCCESS);
	}

	static int32_t make_backup_path(const char *file_path,
		std::string &backup_path) noexcept
	{
		try
		{
			backup_path.assign(file_path);
			backup_path.append(".bak");
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		return (FT_ERR_SUCCESS);
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
				bytes.resize(FT_UINT64_TO_NATIVE_SIZE_CAST(file_size));
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
		const std::vector<uint8_t> *revision_server_state = nullptr,
		const std::vector<uint8_t> *host_config_state = nullptr) noexcept
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
	if (result == FT_ERR_SUCCESS
		&& snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_HOST_CONFIG
		&& (host_config_state == nullptr
			|| !valid_host_config_size(host_config_state->size())))
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
				: revision_server_state->size())
			+ (host_config_state == nullptr ? 0U
				: host_config_state->size()));
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
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_HOST_CONFIG)
			append_u64(output, host_config_state->size());
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
		if (snapshot_version >= SESSION_SNAPSHOT_VERSION_WITH_HOST_CONFIG)
			output.insert(output.end(), host_config_state->begin(),
				host_config_state->end());
		if (output.size() + sizeof(uint64_t) > SESSION_SNAPSHOT_MAX_BYTES)
			return (FT_ERR_OUT_OF_RANGE);
		append_u64(output, checksum(output.data(), output.size()));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	std::vector<uint8_t> previous_snapshot;
	std::string backup_path;
	int32_t backup_result = read_file(file_path, previous_snapshot);
	if (backup_result == FT_ERR_SUCCESS
		&& is_valid_mcsp_snapshot(previous_snapshot))
	{
		backup_result = make_backup_path(file_path, backup_path);
		if (backup_result == FT_ERR_SUCCESS)
			backup_result = file_replace_safe(backup_path.c_str(),
				reinterpret_cast<const char *>(previous_snapshot.data()),
				previous_snapshot.size());
		if (backup_result != FT_ERR_SUCCESS)
			return (backup_result);
	}
	else if (backup_result == FT_ERR_IO)
	{
		errno = 0;
		FILE *existing_file = std::fopen(file_path, "rb");
		if (existing_file != nullptr)
		{
			(void)std::fclose(existing_file);
			return (FT_ERR_IO);
		}
		if (errno != ENOENT)
			return (FT_ERR_IO);
	}
	else if (backup_result != FT_ERR_SUCCESS
		&& backup_result != FT_ERR_OUT_OF_RANGE)
		return (backup_result);
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

int32_t GameSessionPersistence::save_file_with_revision_server_checkpoint_and_host_config(
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
	const std::vector<uint8_t> &host_config_state) noexcept
{
	return (game_session_persistence_save_file(file_path, character,
		world_items, &recipe_catalog.recipe_book(),
		recipe_catalog.next_output_instance_id(), &recipe_catalog,
		&transaction_journal, &card_session_state, &regeneration_rules_state,
		&chunk_instability_state,
		SESSION_SNAPSHOT_VERSION_WITH_HOST_CONFIG,
		&revision_metadata_state, &fluid_state, &entity_state,
		&crop_growth_state, output_session_id, &revision_server_state,
		&host_config_state));
}

static int32_t game_session_persistence_load_file_once(const char *file_path,
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
		std::vector<uint8_t> *revision_server_state = nullptr,
		std::vector<uint8_t> *host_config_state = nullptr,
		bool *state_commit_started_out = nullptr) noexcept
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
	std::vector<uint8_t> staged_host_config_state;
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
	uint64_t host_config_state_size = 0U;
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
	if (state_commit_started_out != nullptr)
		*state_commit_started_out = false;
	if (transaction_journal != nullptr
		&& !transaction_journal->is_initialized())
		return (FT_ERR_INVALID_OPERATION);
	result = read_file(file_path, bytes);
	if (result != FT_ERR_SUCCESS)
		return (result);
	const bool is_legacy_world_items = bytes.size() >= 4U
		&& bytes[0] == 'W' && bytes[1] == 'I'
		&& bytes[2] == 'S' && bytes[3] == 'P';
	if (is_legacy_world_items)
	{
		result = staged_items.initialize();
		if (result == FT_ERR_SUCCESS)
			result = load_legacy_world_items(bytes, staged_items);
		if (result == FT_ERR_SUCCESS && transaction_journal != nullptr)
		{
			result = staged_transaction_journal.initialize();
		}
		if (result == FT_ERR_SUCCESS)
		{
			if (state_commit_started_out != nullptr)
				*state_commit_started_out = true;
			result = world_items.replace_from(staged_items);
		}
		/* replace_from on an initialized journal is a no-allocation swap. */
		if (result == FT_ERR_SUCCESS && transaction_journal != nullptr)
			result = transaction_journal->replace_from(
				staged_transaction_journal);
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
		if (result == FT_ERR_SUCCESS && host_config_state != nullptr)
			host_config_state->clear();
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
		|| version > SESSION_SNAPSHOT_VERSION_WITH_HOST_CONFIG)
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
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_HOST_CONFIG
			&& !reader.read_u64(&host_config_state_size))
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
		|| host_config_state_size > 1024U * 1024U
		|| (version >= SESSION_SNAPSHOT_VERSION_WITH_HOST_CONFIG
			&& !valid_host_config_size(host_config_state_size))
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
			- FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
		|| recipe_size > reader.size - reader.offset
			- FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
		|| craft_action_cache_size > reader.size - reader.offset
			- FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
		|| processing_state_size > reader.size - reader.offset
			- FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size)
		|| transaction_journal_size > reader.size - reader.offset
			- FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(processing_state_size)
		|| card_session_size > reader.size - reader.offset
			- FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(processing_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(transaction_journal_size)
		|| regeneration_rules_size > reader.size - reader.offset
			- FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(processing_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(transaction_journal_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(card_session_size)
		|| chunk_instability_size > reader.size - reader.offset
			- FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(processing_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(transaction_journal_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(card_session_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(regeneration_rules_size)
		|| revision_metadata_size > reader.size - reader.offset
			- FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(processing_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(transaction_journal_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(card_session_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(regeneration_rules_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(chunk_instability_size)
		|| fluid_state_size > reader.size - reader.offset
			- FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(processing_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(transaction_journal_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(card_session_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(regeneration_rules_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(chunk_instability_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(revision_metadata_size)
		|| entity_state_size > reader.size - reader.offset
			- FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(processing_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(transaction_journal_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(card_session_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(regeneration_rules_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(chunk_instability_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(revision_metadata_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(fluid_state_size)
		|| crop_growth_policy_size > reader.size - reader.offset
			- FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(processing_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(transaction_journal_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(card_session_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(regeneration_rules_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(chunk_instability_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(revision_metadata_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(fluid_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(entity_state_size)
		|| revision_server_state_size > reader.size - reader.offset
			- FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(processing_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(transaction_journal_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(card_session_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(regeneration_rules_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(chunk_instability_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(revision_metadata_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(fluid_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(entity_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(crop_growth_policy_size)
			- (version >= SESSION_SNAPSHOT_VERSION_WITH_OUTPUT_SESSION_ID
				? 8U : 0U))
		|| host_config_state_size > reader.size - reader.offset
			- FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(processing_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(transaction_journal_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(card_session_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(regeneration_rules_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(chunk_instability_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(revision_metadata_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(fluid_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(entity_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(crop_growth_policy_size)
			- (version >= SESSION_SNAPSHOT_VERSION_WITH_OUTPUT_SESSION_ID
				? 8U : 0U)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(revision_server_state_size))
		return (FT_ERR_INVALID_ARGUMENT);
	payload_end = reader.offset + FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
		+ FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
		+ FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
		+ FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size)
		+ FT_UINT64_TO_NATIVE_SIZE_CAST(processing_state_size)
		+ FT_UINT64_TO_NATIVE_SIZE_CAST(transaction_journal_size)
		+ FT_UINT64_TO_NATIVE_SIZE_CAST(card_session_size)
		+ FT_UINT64_TO_NATIVE_SIZE_CAST(regeneration_rules_size)
		+ FT_UINT64_TO_NATIVE_SIZE_CAST(chunk_instability_size);
	payload_end += FT_UINT64_TO_NATIVE_SIZE_CAST(revision_metadata_size);
	payload_end += FT_UINT64_TO_NATIVE_SIZE_CAST(fluid_state_size);
	if (payload_end + FT_UINT64_TO_NATIVE_SIZE_CAST(entity_state_size)
		+ FT_UINT64_TO_NATIVE_SIZE_CAST(crop_growth_policy_size)
		+ (version >= SESSION_SNAPSHOT_VERSION_WITH_OUTPUT_SESSION_ID
			? 8U : 0U)
		+ FT_UINT64_TO_NATIVE_SIZE_CAST(revision_server_state_size)
		+ FT_UINT64_TO_NATIVE_SIZE_CAST(host_config_state_size)
		!= reader.size)
		return (FT_ERR_INVALID_ARGUMENT);
	if (version >= SESSION_SNAPSHOT_VERSION_WITH_OUTPUT_SESSION_ID)
	{
		session_id_reader.bytes = bytes.data();
		session_id_reader.size = reader.size;
		session_id_reader.offset = payload_end
			+ FT_UINT64_TO_NATIVE_SIZE_CAST(entity_state_size)
			+ FT_UINT64_TO_NATIVE_SIZE_CAST(crop_growth_policy_size);
		if (!session_id_reader.read_u64(&restored_output_session_id))
			return (FT_ERR_INVALID_ARGUMENT);
	}
	if (entity_state_size > 0U && entity_state != nullptr)
	{
		try
		{
			staged_entity_state.assign(bytes.begin() + payload_end,
				bytes.begin() + payload_end
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(entity_state_size));
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
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(entity_state_size),
				bytes.begin() + payload_end
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(entity_state_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(crop_growth_policy_size));
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
					- FT_UINT64_TO_NATIVE_SIZE_CAST(host_config_state_size)
					- FT_UINT64_TO_NATIVE_SIZE_CAST(revision_server_state_size),
				bytes.begin() + reader.size
					- FT_UINT64_TO_NATIVE_SIZE_CAST(host_config_state_size));
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	if (host_config_state_size > 0U && host_config_state != nullptr)
	{
		try
		{
			staged_host_config_state.assign(
				bytes.begin() + reader.size
					- FT_UINT64_TO_NATIVE_SIZE_CAST(host_config_state_size),
				bytes.begin() + reader.size);
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	if (version >= SESSION_SNAPSHOT_VERSION_WITH_SPAWNERS)
		result = GameSessionEntityState::validate(bytes.data() + payload_end,
			FT_UINT64_TO_NATIVE_SIZE_CAST(entity_state_size));
	else if (version >= SESSION_SNAPSHOT_VERSION_WITH_ENTITIES)
	{
		result = staged_entities.initialize();
		if (result == FT_ERR_SUCCESS)
			result = staged_entities.deserialize(bytes.data() + payload_end,
				FT_UINT64_TO_NATIVE_SIZE_CAST(entity_state_size));
	}
	if (result != FT_ERR_SUCCESS)
		return (result);
	try
	{
		character_json.assign(
			reinterpret_cast<const char *>(bytes.data() + reader.offset),
			FT_UINT64_TO_NATIVE_SIZE_CAST(character_size));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (character_json.find('\0') != std::string::npos)
		return (FT_ERR_INVALID_ARGUMENT);
	reader.offset += FT_UINT64_TO_NATIVE_SIZE_CAST(character_size);
	result = staged_items.initialize();
	if (result == FT_ERR_SUCCESS && transaction_journal != nullptr)
		result = staged_transaction_journal.initialize();
	if (result == FT_ERR_SUCCESS)
	{
		result = staged_items.deserialize(bytes.data() + reader.offset,
			FT_UINT64_TO_NATIVE_SIZE_CAST(item_size));
	}
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (transaction_journal != nullptr
		&& version >= SESSION_SNAPSHOT_VERSION_WITH_JOURNAL)
	{
		const std::size_t journal_offset = payload_end
			- FT_UINT64_TO_NATIVE_SIZE_CAST(fluid_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(revision_metadata_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(chunk_instability_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(regeneration_rules_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(card_session_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(transaction_journal_size);

		result = staged_transaction_journal.deserialize(
			bytes.data() + journal_offset,
			FT_UINT64_TO_NATIVE_SIZE_CAST(transaction_journal_size));
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	if (card_session_size > 0U)
	{
		const std::size_t card_offset = payload_end
			- FT_UINT64_TO_NATIVE_SIZE_CAST(fluid_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(revision_metadata_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(chunk_instability_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(regeneration_rules_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(card_session_size);

		try
		{
			staged_card_session_state.assign(bytes.begin() + card_offset,
				bytes.begin() + card_offset
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(card_session_size));
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	if (regeneration_rules_size > 0U)
	{
		const std::size_t rules_offset = payload_end
			- FT_UINT64_TO_NATIVE_SIZE_CAST(fluid_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(revision_metadata_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(chunk_instability_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(regeneration_rules_size);

		try
		{
			staged_regeneration_rules_state.assign(bytes.begin() + rules_offset,
				bytes.begin() + rules_offset
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(regeneration_rules_size));
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	if (chunk_instability_size > 0U && chunk_instability_state != nullptr)
	{
		const std::size_t instability_offset = payload_end
			- FT_UINT64_TO_NATIVE_SIZE_CAST(fluid_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(revision_metadata_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(chunk_instability_size);
		try
		{
			staged_chunk_instability_state.assign(
				bytes.begin() + instability_offset,
				bytes.begin() + instability_offset
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(chunk_instability_size));
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	if (revision_metadata_size > 0U && revision_metadata_state != nullptr)
	{
		const std::size_t metadata_offset = payload_end
			- FT_UINT64_TO_NATIVE_SIZE_CAST(fluid_state_size)
			- FT_UINT64_TO_NATIVE_SIZE_CAST(revision_metadata_size);
		const std::size_t metadata_end = metadata_offset
			+ FT_UINT64_TO_NATIVE_SIZE_CAST(revision_metadata_size);
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
			- FT_UINT64_TO_NATIVE_SIZE_CAST(fluid_state_size);

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
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size),
				FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size));
		if (result == FT_ERR_SUCCESS && version >= 5U)
			result = recipe_catalog->validate_processing_state(
				bytes.data() + reader.offset
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size),
				FT_UINT64_TO_NATIVE_SIZE_CAST(processing_state_size));
		if (result == FT_ERR_SUCCESS)
			result = recipe_buffer.initialize();
		if (result == FT_ERR_SUCCESS)
			result = recipe_buffer.append(
				bytes.data() + reader.offset + FT_UINT64_TO_NATIVE_SIZE_CAST(item_size),
				recipe_size);
		if (result == FT_ERR_SUCCESS)
		{
			if (state_commit_started_out != nullptr)
				*state_commit_started_out = true;
			result = recipe_catalog->import_book(recipe_buffer);
		}
		if (result == FT_ERR_SUCCESS && version >= 3U)
			result = recipe_catalog->restore_next_output_instance_id(
				next_instance_id);
		if (result == FT_ERR_SUCCESS && version >= 4U)
			result = recipe_catalog->restore_craft_action_cache(
				bytes.data() + reader.offset
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size),
				FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size));
		if (result == FT_ERR_SUCCESS && version >= 5U)
			result = recipe_catalog->restore_processing_state(
				bytes.data() + reader.offset
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(craft_action_cache_size),
				FT_UINT64_TO_NATIVE_SIZE_CAST(processing_state_size));
	}
	if (result == FT_ERR_SUCCESS && recipe_catalog != nullptr
		&& version < 4U)
	{
		if (state_commit_started_out != nullptr)
			*state_commit_started_out = true;
		result = recipe_catalog->clear_craft_action_cache();
	}
	if (result == FT_ERR_SUCCESS && recipe_catalog != nullptr
		&& version < 5U)
	{
		if (state_commit_started_out != nullptr)
			*state_commit_started_out = true;
		result = recipe_catalog->clear_processing_state();
	}
	if (result == FT_ERR_SUCCESS)
	{
		if (state_commit_started_out != nullptr)
			*state_commit_started_out = true;
		result = loader.load_from_buffer(character_json.c_str(), character,
			character.get_inventory());
	}
	if (result == FT_ERR_SUCCESS)
		result = world_items.replace_from(staged_items);
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
	if (result == FT_ERR_SUCCESS && host_config_state != nullptr)
		host_config_state->swap(staged_host_config_state);
	if (result == FT_ERR_SUCCESS && output_session_id_out != nullptr)
		*output_session_id_out = restored_output_session_id;
	return (result);
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
	std::vector<uint8_t> *revision_server_state = nullptr,
	std::vector<uint8_t> *host_config_state = nullptr) noexcept
{
	if (file_path == nullptr || file_path[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	std::vector<uint8_t> primary_probe;
	const int32_t probe_result = read_file(file_path, primary_probe);
	const bool primary_is_mcsp = probe_result == FT_ERR_SUCCESS
		&& primary_probe.size() >= 4U && primary_probe[0] == 'M'
		&& primary_probe[1] == 'C' && primary_probe[2] == 'S'
		&& primary_probe[3] == 'P';
	const bool primary_is_wisp = probe_result == FT_ERR_SUCCESS
		&& primary_probe.size() >= 4U && primary_probe[0] == 'W'
		&& primary_probe[1] == 'I' && primary_probe[2] == 'S'
		&& primary_probe[3] == 'P';
	bool recover_from_backup = probe_result == FT_ERR_IO
		|| probe_result == FT_ERR_OUT_OF_RANGE
		|| (primary_is_mcsp && !is_valid_mcsp_snapshot(primary_probe))
		|| (probe_result == FT_ERR_SUCCESS
			&& !primary_is_mcsp && !primary_is_wisp);
	bool primary_commit_started = false;
	int32_t primary_load_result = probe_result;
	if (probe_result == FT_ERR_SUCCESS && primary_is_mcsp
		&& !is_valid_mcsp_snapshot(primary_probe))
		primary_load_result = FT_ERR_INVALID_ARGUMENT;
	else if (probe_result == FT_ERR_SUCCESS && !primary_is_mcsp
		&& !primary_is_wisp)
		primary_load_result = FT_ERR_INVALID_ARGUMENT;
	if (probe_result == FT_ERR_SUCCESS
		&& (primary_is_wisp || (primary_is_mcsp
			&& is_valid_mcsp_snapshot(primary_probe))))
	{
		primary_load_result = game_session_persistence_load_file_once(file_path,
			character, world_items, recipe_catalog, transaction_journal,
			card_session_state, regeneration_rules_state,
			chunk_instability_state, revision_metadata_state, fluid_state,
			entity_state, crop_growth_policy_state, output_session_id_out,
			revision_server_state, host_config_state, &primary_commit_started);
		if (primary_load_result == FT_ERR_SUCCESS || primary_commit_started)
		{
			std::vector<uint8_t>().swap(primary_probe);
			return (primary_load_result);
		}
		recover_from_backup = true;
	}
	std::vector<uint8_t>().swap(primary_probe);
	if (!recover_from_backup)
		return (primary_load_result);
	std::string backup_path;
	const int32_t path_result = make_backup_path(file_path, backup_path);
	if (path_result != FT_ERR_SUCCESS)
		return (probe_result == FT_ERR_SUCCESS
			? primary_load_result : probe_result);
	std::vector<uint8_t> backup_bytes;
	const int32_t backup_read_result = read_file(backup_path.c_str(),
		backup_bytes);
	if (backup_read_result != FT_ERR_SUCCESS
		|| !is_valid_mcsp_snapshot(backup_bytes))
		return (probe_result == FT_ERR_SUCCESS
			? primary_load_result : probe_result);
	const int32_t repair_result = file_replace_safe(file_path,
		reinterpret_cast<const char *>(backup_bytes.data()),
		backup_bytes.size());
	if (repair_result != FT_ERR_SUCCESS)
		return (repair_result);
	return (game_session_persistence_load_file_once(file_path, character,
		world_items, recipe_catalog, transaction_journal, card_session_state,
		regeneration_rules_state, chunk_instability_state,
		revision_metadata_state, fluid_state, entity_state,
		crop_growth_policy_state, output_session_id_out, revision_server_state,
		host_config_state));
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

int32_t GameSessionPersistence::load_file_with_revision_server_checkpoint_and_host_config(
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
	std::vector<uint8_t> &host_config_state) noexcept
{
	if (output_session_id_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (game_session_persistence_load_file(file_path, character,
		world_items, &recipe_catalog, &transaction_journal,
		&card_session_state, &regeneration_rules_state,
		&chunk_instability_state, &revision_metadata_state, &fluid_state,
		&entity_state, &crop_growth_policy_state, output_session_id_out,
		&revision_server_state, &host_config_state));
}

static int32_t game_session_bundle_validate_store_paths(
	const char *snapshot_path, const char *revision_path) noexcept
{
	std::string expected_revision_path;
	const std::size_t snapshot_length = snapshot_path == nullptr ? 0U
		: std::strlen(snapshot_path);
	const std::size_t revision_length = revision_path == nullptr ? 0U
		: std::strlen(revision_path);
	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| revision_path == nullptr || revision_path[0] == '\0'
		|| snapshot_length < 5U || revision_length < 5U
		|| std::strcmp(snapshot_path + snapshot_length - 5U, ".wisp") != 0
		|| std::strcmp(revision_path + revision_length - 4U, ".wrv") != 0
		|| file_validate_path_inside_root("saves/world_items", snapshot_path)
			!= FT_ERR_SUCCESS
		|| file_validate_path_inside_root("saves/world_items", revision_path)
			!= FT_ERR_SUCCESS
		|| file_validate_path_resolution_inside_root("saves/world_items", snapshot_path)
			!= FT_ERR_SUCCESS
		|| file_validate_path_resolution_inside_root("saves/world_items", revision_path)
			!= FT_ERR_SUCCESS
		|| file_path_equal(snapshot_path, revision_path) != FT_FALSE)
		return (FT_ERR_INVALID_PATH);
	try
	{
		expected_revision_path.assign(snapshot_path,
			snapshot_length - 5U);
		expected_revision_path.append(".wrv");
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (file_path_equal(expected_revision_path.c_str(), revision_path)
		!= FT_TRUE)
		return (FT_ERR_INVALID_PATH);
	return (FT_ERR_SUCCESS);
}

static int32_t game_session_bundle_validate_paths(const char *bundle_path,
	const char *snapshot_path, const char *revision_path) noexcept
{
	if (bundle_path == nullptr || bundle_path[0] == '\0'
		|| std::strlen(bundle_path) < 4U
		|| std::strcmp(bundle_path + std::strlen(bundle_path) - 4U,
			".wbk") != 0
		|| file_validate_path_inside_root("saves/backups", bundle_path)
			!= FT_ERR_SUCCESS
		|| file_validate_path_resolution_inside_root("saves/backups",
			bundle_path) != FT_ERR_SUCCESS
		|| game_session_bundle_validate_store_paths(snapshot_path,
			revision_path) != FT_ERR_SUCCESS
		|| file_path_equal(bundle_path, snapshot_path) != FT_FALSE
		|| file_path_equal(bundle_path, revision_path) != FT_FALSE)
		return (FT_ERR_INVALID_PATH);
	return (FT_ERR_SUCCESS);
}

static bool game_session_bundle_read_world_id(const char *snapshot_path,
	uint8_t world_id_out[32]) noexcept
{
	if (snapshot_path == nullptr || world_id_out == nullptr)
		return (false);
	const char *basename = snapshot_path;
	for (const char *cursor = snapshot_path; *cursor != '\0'; ++cursor)
		if (*cursor == '/' || *cursor == '\\')
			basename = cursor + 1;
	if (std::strlen(basename) != 69U
		|| std::strcmp(basename + 64U, ".wisp") != 0)
		return (false);
	for (std::size_t index = 0U; index < 32U; ++index)
	{
		const char high = basename[index * 2U];
		const char low = basename[index * 2U + 1U];
		const auto hex_value = [](char value) -> int
		{
			if (value >= '0' && value <= '9')
				return (value - '0');
			if (value >= 'a' && value <= 'f')
				return (value - 'a' + 10);
			return (-1);
		};
		const int high_value = hex_value(high);
		const int low_value = hex_value(low);
		if (high_value < 0 || low_value < 0)
			return (false);
		world_id_out[index] = static_cast<uint8_t>(
			(high_value << 4) | low_value);
	}
	bool nonzero = false;
	for (std::size_t index = 0U; index < 32U; ++index)
		if (world_id_out[index] != 0U)
			nonzero = true;
	return (nonzero);
}

static int32_t game_session_bundle_world_paths(const uint8_t world_id[32],
	std::string &snapshot_path, std::string &revision_path) noexcept
{
	if (world_id == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		snapshot_path.assign("saves/world_items/");
		for (std::size_t index = 0U; index < 32U; ++index)
		{
			char encoded[3];
			const int written = std::snprintf(encoded, sizeof(encoded), "%02x",
				static_cast<unsigned int>(world_id[index]));
			if (written != 2)
				return (FT_ERR_IO);
			snapshot_path.append(encoded, 2U);
		}
		snapshot_path.append(".wisp");
		revision_path.assign(snapshot_path, 0U, snapshot_path.size() - 5U);
		revision_path.append(".wrv");
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

static int32_t game_session_bundle_terrain_path(const uint8_t world_id[32],
	std::string &terrain_path) noexcept
{
	if (world_id == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		terrain_path.assign("saves/terrain/");
		for (std::size_t index = 0U; index < 32U; ++index)
		{
			char encoded[3];
			const int written = std::snprintf(encoded, sizeof(encoded), "%02x",
				static_cast<unsigned int>(world_id[index]));
			if (written != 2)
				return (FT_ERR_IO);
			terrain_path.append(encoded, 2U);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (file_validate_path_resolution_inside_root("saves/terrain",
		terrain_path.c_str()));
}

static int32_t game_session_bundle_remove_terrain(const uint8_t world_id[32])
	noexcept
{
	std::string terrain_path;
	const file_type root_type = file_get_type("saves/terrain");
	if (root_type == FILE_TYPE_MISSING)
		return (FT_ERR_SUCCESS);
	if (root_type != FILE_TYPE_DIRECTORY)
		return (FT_ERR_INVALID_PATH);
	int32_t result = game_session_bundle_terrain_path(world_id, terrain_path);
	if (result != FT_ERR_SUCCESS)
		return (result);
	const file_type type = file_get_type(terrain_path.c_str());
	if (type == FILE_TYPE_MISSING)
		return (FT_ERR_SUCCESS);
	if (type != FILE_TYPE_DIRECTORY)
		return (FT_ERR_INVALID_PATH);
	result = file_validate_path_resolution_inside_root("saves/terrain",
		terrain_path.c_str());
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (file_delete_recursive(terrain_path.c_str()));
}

static bool game_session_bundle_valid_chunk_filename(const char *name,
	int32_t *chunk_x_out = nullptr, int32_t *chunk_z_out = nullptr) noexcept
{
	if (name == nullptr)
		return (false);
	const char *separator = std::strchr(name, '_');
	const char *extension = std::strstr(name, ".vxc");
	if (separator == nullptr || extension == nullptr
		|| std::strcmp(extension, ".vxc") != 0 || separator >= extension)
		return (false);
	char x_text[32];
	const std::size_t x_length = static_cast<std::size_t>(separator - name);
	if (x_length == 0U || x_length >= sizeof(x_text))
		return (false);
	std::memcpy(x_text, name, x_length);
	x_text[x_length] = '\0';
	errno = 0;
	char *end = nullptr;
	const long x_value = std::strtol(x_text, &end, 10);
	if (errno != 0 || end == x_text || *end != '\0'
		|| x_value < std::numeric_limits<int32_t>::min()
		|| x_value > std::numeric_limits<int32_t>::max())
		return (false);
	errno = 0;
	const long z_value = std::strtol(separator + 1, &end, 10);
	if (errno != 0 || end == separator + 1 || end != extension
		|| z_value < std::numeric_limits<int32_t>::min()
		|| z_value > std::numeric_limits<int32_t>::max())
		return (false);
	char canonical[64];
	const int written = std::snprintf(canonical, sizeof(canonical), "%ld_%ld.vxc",
		x_value, z_value);
	if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(canonical)
		|| std::strcmp(canonical, name) != 0)
		return (false);
	if (chunk_x_out != nullptr)
		*chunk_x_out = static_cast<int32_t>(x_value);
	if (chunk_z_out != nullptr)
		*chunk_z_out = static_cast<int32_t>(z_value);
	return (true);
}

static uint32_t game_session_bundle_read_u32(const uint8_t *bytes) noexcept
{
	return (static_cast<uint32_t>(bytes[0U])
		| (static_cast<uint32_t>(bytes[1U]) << 8U)
		| (static_cast<uint32_t>(bytes[2U]) << 16U)
		| (static_cast<uint32_t>(bytes[3U]) << 24U));
}

static uint64_t game_session_bundle_read_u64(const uint8_t *bytes) noexcept
{
	uint64_t value = 0U;
	for (uint32_t index = 0U; index < 8U; ++index)
		value |= static_cast<uint64_t>(bytes[index]) << (index * 8U);
	return (value);
}

static int32_t game_session_bundle_validate_chunk_record(const char *name,
	const std::vector<uint8_t> &bytes) noexcept
{
	int32_t chunk_x = 0;
	int32_t chunk_z = 0;
	if (!game_session_bundle_valid_chunk_filename(name, &chunk_x, &chunk_z)
		|| bytes.size() < 32U
		|| std::memcmp(bytes.data(), "WVC1", 4U) != 0
		|| game_session_bundle_read_u32(bytes.data() + 4U) != 1U
		|| game_session_bundle_read_u32(bytes.data() + 8U)
			!= static_cast<uint32_t>(chunk_x)
		|| game_session_bundle_read_u32(bytes.data() + 12U)
			!= static_cast<uint32_t>(chunk_z))
		return (FT_ERR_INVALID_ARGUMENT);
	const uint64_t payload_size = game_session_bundle_read_u64(
		bytes.data() + 16U);
	const uint64_t record_checksum = game_session_bundle_read_u64(
		bytes.data() + bytes.size() - 8U);
	if (payload_size != bytes.size() - 32U
		|| record_checksum != checksum(bytes.data(), bytes.size() - 8U))
		return (FT_ERR_INVALID_ARGUMENT);
	ft_byte_buffer payload;
	int32_t result = payload.initialize(static_cast<ft_size_t>(payload_size),
		FT_TRUE);
	const bool payload_initialized = result == FT_ERR_SUCCESS;
	if (result == FT_ERR_SUCCESS)
		result = payload.append(bytes.data() + 24U,
			static_cast<ft_size_t>(payload_size));
	if (result == FT_ERR_SUCCESS)
		result = payload.reset_read_position();
	game_voxel_chunk chunk;
	bool chunk_initialized = false;
	if (result == FT_ERR_SUCCESS)
	{
		result = chunk.initialize();
		chunk_initialized = result == FT_ERR_SUCCESS;
	}
	if (result == FT_ERR_SUCCESS)
		result = chunk.deserialize(payload);
	if (result == FT_ERR_SUCCESS && payload.remaining() != 0U)
		result = FT_ERR_INVALID_ARGUMENT;
	if (chunk_initialized)
	{
		const int32_t destroy_result = chunk.destroy();
		if (result == FT_ERR_SUCCESS)
			result = destroy_result;
	}
	if (payload_initialized)
	{
		const int32_t destroy_result = payload.destroy();
		if (result == FT_ERR_SUCCESS)
			result = destroy_result;
	}
	return (result);
}

static int32_t game_session_bundle_serialize_terrain(
	const uint8_t world_id[32], std::vector<uint8_t> &archive_out) noexcept
{
	std::string directory;
	std::vector<uint8_t> archive;
	int32_t result = game_session_bundle_terrain_path(world_id, directory);
	if (result != FT_ERR_SUCCESS)
		return (result);
	const file_type root_type = file_get_type("saves/terrain");
	const file_type directory_type = file_get_type(directory.c_str());
	if (root_type == FILE_TYPE_MISSING)
	{
		try
		{
			archive.insert(archive.end(), {'W', 'T', 'A', '1'});
			append_u32(archive, 1U);
			append_u32(archive, 0U);
			append_u64(archive, checksum(archive.data(), archive.size()));
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		archive_out.swap(archive);
		return (FT_ERR_SUCCESS);
	}
	if (root_type != FILE_TYPE_DIRECTORY)
		return (FT_ERR_INVALID_PATH);
	if (directory_type == FILE_TYPE_MISSING)
	{
		try
		{
			archive.insert(archive.end(), {'W', 'T', 'A', '1'});
			append_u32(archive, 1U);
			append_u32(archive, 0U);
			append_u64(archive, checksum(archive.data(), archive.size()));
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		archive_out.swap(archive);
		return (FT_ERR_SUCCESS);
	}
	if (directory_type != FILE_TYPE_DIRECTORY)
		return (FT_ERR_INVALID_PATH);
	ft_vector<ft_string> entries;
	result = entries.initialize();
	if (result == FT_ERR_SUCCESS)
		result = file_list_directory(directory.c_str(), entries,
			FILE_LIST_INCLUDE_FILES | FILE_LIST_INCLUDE_HIDDEN);
	if (result != FT_ERR_SUCCESS)
	{
		(void)entries.destroy();
		return (result);
	}
	if (entries.size() > 65536U)
	{
		(void)entries.destroy();
		return (FT_ERR_OUT_OF_RANGE);
	}
	try
	{
		archive.insert(archive.end(), {'W', 'T', 'A', '1'});
		append_u32(archive, 1U);
		append_u32(archive, static_cast<uint32_t>(entries.size()));
		for (ft_size_t index = 0U; index < entries.size(); ++index)
		{
			const char *path = entries[index].c_str();
			const char *basename = std::strrchr(path, '/');
			const char *backslash = std::strrchr(path, '\\');
			if (backslash != nullptr && (basename == nullptr || backslash > basename))
				basename = backslash;
			basename = basename == nullptr ? path : basename + 1;
			if (!game_session_bundle_valid_chunk_filename(basename)
				|| file_validate_regular_file_inside_root("saves/terrain", path)
				!= FT_ERR_SUCCESS)
			{
				result = FT_ERR_INVALID_PATH;
				break;
			}
			std::vector<uint8_t> file_bytes;
			result = read_file(path, file_bytes);
			if (result != FT_ERR_SUCCESS)
				break;
			result = game_session_bundle_validate_chunk_record(basename,
				file_bytes);
			if (result != FT_ERR_SUCCESS)
			{
				break;
			}
			const std::size_t name_length = std::strlen(basename);
			if (name_length > 63U
				|| file_bytes.size() > 16U * 1024U * 1024U
				|| archive.size() > SESSION_SNAPSHOT_MAX_BYTES
					- name_length - file_bytes.size() - 24U)
			{
				result = FT_ERR_OUT_OF_RANGE;
				break;
			}
			append_u32(archive, static_cast<uint32_t>(name_length));
			archive.insert(archive.end(), basename, basename + name_length);
			append_u64(archive, file_bytes.size());
			append_u64(archive, checksum(file_bytes.data(), file_bytes.size()));
			archive.insert(archive.end(), file_bytes.begin(), file_bytes.end());
		}
		if (result == FT_ERR_SUCCESS)
			append_u64(archive, checksum(archive.data(), archive.size()));
	}
	catch (const std::bad_alloc &)
	{
		result = FT_ERR_NO_MEMORY;
	}
	const int32_t destroy_result = entries.destroy();
	if (result == FT_ERR_SUCCESS)
		result = destroy_result;
	if (result == FT_ERR_SUCCESS)
		archive_out.swap(archive);
	return (result);
}

static int32_t game_session_bundle_parse_terrain(const uint8_t *bytes,
	std::size_t byte_count,
	std::vector<std::pair<std::string, std::vector<uint8_t> > > &files_out)
	noexcept
{
	if (bytes == nullptr || byte_count < 20U
		|| byte_count > SESSION_SNAPSHOT_MAX_BYTES
		|| std::memcmp(bytes, "WTA1", 4U) != 0)
		return (FT_ERR_INVALID_ARGUMENT);
	uint64_t stored_checksum = 0U;
	for (uint32_t index = 0U; index < 8U; ++index)
		stored_checksum |= static_cast<uint64_t>(bytes[byte_count - 8U + index])
			<< (index * 8U);
	if (stored_checksum != checksum(bytes, byte_count - 8U))
		return (FT_ERR_INVALID_ARGUMENT);
	Reader reader = {bytes, byte_count - 8U, 4U};
	uint32_t version = 0U;
	uint32_t count = 0U;
	if (!reader.read_u32(&version) || version != 1U
		|| !reader.read_u32(&count) || count > 65536U)
		return (FT_ERR_INVALID_ARGUMENT);
	std::vector<std::pair<std::string, std::vector<uint8_t> > > staged;
	std::set<std::string> seen_names;
	try
	{
		staged.reserve(count);
		for (uint32_t index = 0U; index < count; ++index)
		{
			uint32_t name_length = 0U;
			uint64_t file_size = 0U;
			uint64_t file_checksum = 0U;
			if (!reader.read_u32(&name_length) || name_length == 0U
				|| name_length > 63U || reader.offset > reader.size
				|| name_length > reader.size - reader.offset)
				return (FT_ERR_INVALID_ARGUMENT);
			std::string name(reinterpret_cast<const char *>(
				reader.bytes + reader.offset), name_length);
			reader.offset += name_length;
			if (!game_session_bundle_valid_chunk_filename(name.c_str())
				|| !seen_names.insert(name).second
				|| !reader.read_u64(&file_size)
				|| !reader.read_u64(&file_checksum)
				|| file_size > 16U * 1024U * 1024U
				|| reader.offset > reader.size
				|| file_size > reader.size - reader.offset)
				return (FT_ERR_INVALID_ARGUMENT);
			std::vector<uint8_t> file_bytes(
				reader.bytes + reader.offset,
				reader.bytes + reader.offset
					+ static_cast<std::size_t>(file_size));
			reader.offset += static_cast<std::size_t>(file_size);
			if (checksum(file_bytes.data(), file_bytes.size()) != file_checksum)
				return (FT_ERR_INVALID_ARGUMENT);
			const int32_t chunk_result =
				game_session_bundle_validate_chunk_record(name.c_str(),
					file_bytes);
			if (chunk_result != FT_ERR_SUCCESS)
				return (chunk_result);
			staged.push_back(std::make_pair(std::move(name),
				std::move(file_bytes)));
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (reader.offset != reader.size)
		return (FT_ERR_INVALID_ARGUMENT);
	files_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

static int32_t game_session_bundle_install_terrain(const uint8_t world_id[32],
	const uint8_t *archive, std::size_t archive_size) noexcept
{
	std::string directory;
	std::vector<std::pair<std::string, std::vector<uint8_t> > > files;
	int32_t result = game_session_bundle_parse_terrain(archive, archive_size,
		files);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = game_session_bundle_terrain_path(world_id, directory);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = file_create_directories("saves/terrain", 0700);
	if (result != FT_ERR_SUCCESS)
		return (result);
	const file_type current_type = file_get_type(directory.c_str());
	if (current_type != FILE_TYPE_MISSING
		&& current_type != FILE_TYPE_DIRECTORY)
		return (FT_ERR_INVALID_PATH);
	if (current_type == FILE_TYPE_DIRECTORY)
	{
		result = file_validate_path_resolution_inside_root("saves/terrain",
			directory.c_str());
		if (result == FT_ERR_SUCCESS)
			result = file_delete_recursive(directory.c_str());
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	if (files.empty())
		return (FT_ERR_SUCCESS);
	result = file_create_directories(directory.c_str(), 0700);
	if (result != FT_ERR_SUCCESS)
		return (result);
	for (const auto &entry : files)
	{
		std::string path;
		try
		{
			path.assign(directory);
			path.push_back('/');
			path.append(entry.first);
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		result = file_validate_path_resolution_inside_root("saves/terrain",
			path.c_str());
		if (result == FT_ERR_SUCCESS)
			result = file_write_all_atomic(path.c_str(),
				reinterpret_cast<const char *>(entry.second.data()),
				static_cast<ft_size_t>(entry.second.size()));
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	return (FT_ERR_SUCCESS);
}

static int32_t game_session_bundle_terrain_backup_path(
	const char *snapshot_path, std::string &backup_path) noexcept
{
	if (snapshot_path == nullptr || snapshot_path[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		backup_path.assign(snapshot_path);
		backup_path.append(".wbj-terrain");
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (file_validate_path_resolution_inside_root("saves/world_items",
		backup_path.c_str()));
}

static int32_t game_session_bundle_restore_file(const char *path,
	const std::vector<uint8_t> &bytes, bool existed) noexcept
{
	const file_type type = file_get_type(path);
	if (existed)
	{
		if (type != FILE_TYPE_REGULAR && type != FILE_TYPE_MISSING)
			return (FT_ERR_INVALID_PATH);
		return (file_write_all_atomic(path,
			reinterpret_cast<const char *>(bytes.data()),
			static_cast<ft_size_t>(bytes.size())));
	}
	if (type == FILE_TYPE_MISSING)
		return (FT_ERR_SUCCESS);
	if (type != FILE_TYPE_REGULAR)
		return (FT_ERR_INVALID_PATH);
	return (file_delete(path));
}

static int32_t game_session_bundle_remove_file(const char *path) noexcept
{
	const file_type type = file_get_type(path);
	if (type == FILE_TYPE_MISSING)
		return (FT_ERR_SUCCESS);
	if (type != FILE_TYPE_REGULAR)
		return (FT_ERR_INVALID_PATH);
	return (file_delete(path));
}

static int32_t game_session_bundle_journal_paths(const char *snapshot_path,
	std::string &journal_path, std::string &snapshot_backup_path,
	std::string &revision_backup_path) noexcept
{
	try
	{
		journal_path.assign(snapshot_path);
		journal_path.append(".wbj");
		snapshot_backup_path.assign(snapshot_path);
		snapshot_backup_path.append(".wbj-main");
		revision_backup_path.assign(snapshot_path);
		revision_backup_path.append(".wbj-revision");
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (file_validate_path_resolution_inside_root("saves/world_items",
			journal_path.c_str()) != FT_ERR_SUCCESS
		|| file_validate_path_resolution_inside_root("saves/world_items",
			snapshot_backup_path.c_str()) != FT_ERR_SUCCESS
		|| file_validate_path_resolution_inside_root("saves/world_items",
			revision_backup_path.c_str()) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_PATH);
	return (FT_ERR_SUCCESS);
}

int32_t GameSessionPersistence::recover_world_bundle_import(
	const char *snapshot_path, const char *revision_metadata_path) noexcept
{
	std::string journal_path;
	std::string snapshot_backup_path;
	std::string revision_backup_path;
	std::string terrain_backup_path;
	std::vector<uint8_t> journal;
	std::vector<uint8_t> old_snapshot;
	std::vector<uint8_t> old_revision;
	std::vector<uint8_t> old_terrain;
	std::vector<std::pair<std::string, std::vector<uint8_t> > >
		validated_old_terrain;
	uint8_t world_id[32] = {};
	if (snapshot_path == nullptr || revision_metadata_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	int32_t result = game_session_bundle_validate_store_paths(snapshot_path,
		revision_metadata_path);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = game_session_bundle_journal_paths(snapshot_path, journal_path,
		snapshot_backup_path, revision_backup_path);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = game_session_bundle_terrain_backup_path(snapshot_path,
		terrain_backup_path);
	if (result != FT_ERR_SUCCESS
		|| !game_session_bundle_read_world_id(snapshot_path, world_id))
		return (result == FT_ERR_SUCCESS ? FT_ERR_INVALID_PATH : result);
	const file_type journal_type = file_get_type(journal_path.c_str());
	if (journal_type == FILE_TYPE_MISSING)
	{
		(void)game_session_bundle_remove_file(snapshot_backup_path.c_str());
		(void)game_session_bundle_remove_file(revision_backup_path.c_str());
		(void)game_session_bundle_remove_file(terrain_backup_path.c_str());
		return (FT_ERR_SUCCESS);
	}
	if (journal_type != FILE_TYPE_REGULAR)
		return (FT_ERR_INVALID_PATH);
	result = read_file(journal_path.c_str(), journal);
	if (result != FT_ERR_SUCCESS)
		return (result);
	uint64_t journal_checksum = 0U;
	if (journal.size() < 8U)
		return (FT_ERR_INVALID_ARGUMENT);
	for (uint32_t index = 0U; index < 8U; ++index)
		journal_checksum |= static_cast<uint64_t>(
			journal[journal.size() - 8U + index]) << (index * 8U);
	if ((journal.size() != 36U && journal.size() != 44U)
		|| std::memcmp(journal.data(), "WBJ1", 4U) != 0
		|| checksum(journal.data(), journal.size() - 8U) != journal_checksum)
		return (FT_ERR_INVALID_ARGUMENT);
	Reader reader = {journal.data(), journal.size() - 8U, 4U};
	uint32_t version = 0U;
	uint32_t flags = 0U;
	uint64_t snapshot_checksum = 0U;
	uint64_t revision_checksum = 0U;
	uint64_t terrain_checksum = 0U;
	if (!reader.read_u32(&version) || (version != 1U && version != 2U)
		|| (version == 1U && journal.size() != 36U)
		|| (version == 2U && journal.size() != 44U)
		|| !reader.read_u32(&flags) || flags > (version == 1U ? 3U : 7U)
		|| !reader.read_u64(&snapshot_checksum)
		|| !reader.read_u64(&revision_checksum)
		|| (version == 2U && !reader.read_u64(&terrain_checksum))
		|| reader.offset != reader.size)
		return (FT_ERR_INVALID_ARGUMENT);
	const bool had_snapshot = (flags & 1U) != 0U;
	const bool had_revision = (flags & 2U) != 0U;
	const bool had_terrain = version == 2U && (flags & 4U) != 0U;
	if (had_snapshot)
	{
		if (file_validate_regular_file_inside_root("saves/world_items",
				snapshot_backup_path.c_str()) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
		result = read_file(snapshot_backup_path.c_str(), old_snapshot);
		if (result != FT_ERR_SUCCESS
			|| checksum(old_snapshot.data(), old_snapshot.size())
				!= snapshot_checksum)
			return (result == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE : result);
	}
	else if (snapshot_checksum != 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (had_revision)
	{
		if (file_validate_regular_file_inside_root("saves/world_items",
				revision_backup_path.c_str()) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
		result = read_file(revision_backup_path.c_str(), old_revision);
		if (result != FT_ERR_SUCCESS
			|| checksum(old_revision.data(), old_revision.size())
				!= revision_checksum)
			return (result == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE : result);
	}
	else if (revision_checksum != 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (had_terrain)
	{
		if (file_validate_regular_file_inside_root("saves/world_items",
				terrain_backup_path.c_str()) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
		result = read_file(terrain_backup_path.c_str(), old_terrain);
		if (result != FT_ERR_SUCCESS
			|| checksum(old_terrain.data(), old_terrain.size())
				!= terrain_checksum
			|| game_session_bundle_parse_terrain(old_terrain.data(),
				old_terrain.size(), validated_old_terrain) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
	}
	else if (terrain_checksum != 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	result = game_session_bundle_restore_file(snapshot_path, old_snapshot,
		had_snapshot);
	if (result == FT_ERR_SUCCESS)
		result = game_session_bundle_restore_file(revision_metadata_path,
			old_revision, had_revision);
	if (result == FT_ERR_SUCCESS && version == 2U)
	{
		if (had_terrain)
			result = game_session_bundle_install_terrain(world_id,
				old_terrain.data(), old_terrain.size());
		else
			result = game_session_bundle_remove_terrain(world_id);
	}
	if (result != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	result = file_delete(journal_path.c_str());
	if (result != FT_ERR_SUCCESS)
		return (result);
	(void)game_session_bundle_remove_file(snapshot_backup_path.c_str());
	(void)game_session_bundle_remove_file(revision_backup_path.c_str());
	(void)game_session_bundle_remove_file(terrain_backup_path.c_str());
	return (FT_ERR_SUCCESS);
}

int32_t GameSessionPersistence::export_world_bundle(const char *snapshot_path,
	const char *revision_metadata_path, const char *bundle_path) noexcept
{
	std::vector<uint8_t> snapshot;
	std::vector<uint8_t> revision_metadata;
	std::vector<uint8_t> terrain_archive;
	std::vector<uint8_t> bundle;
	uint8_t world_id[32] = {};
	uint8_t snapshot_world_id[32] = {};
	bool snapshot_world_id_present = false;
	if (bundle_path == nullptr || snapshot_path == nullptr
		|| revision_metadata_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	int32_t result = file_create_directories("saves/backups", 0700);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = game_session_bundle_validate_paths(bundle_path,
		snapshot_path, revision_metadata_path);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = GameSessionPersistence::recover_world_bundle_import(snapshot_path,
		revision_metadata_path);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (file_validate_regular_file_inside_root("saves/world_items", snapshot_path)
		!= FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_PATH);
	if (!game_session_bundle_read_world_id(snapshot_path, world_id))
		return (FT_ERR_INVALID_PATH);
	result = read_file(snapshot_path, snapshot);
	if (result != FT_ERR_SUCCESS || !is_valid_mcsp_snapshot(snapshot))
		return (result == FT_ERR_SUCCESS ? FT_ERR_INVALID_ARGUMENT : result);
	result = mcsp_snapshot_world_id(snapshot, snapshot_world_id,
		&snapshot_world_id_present);
	if (result != FT_ERR_SUCCESS || (snapshot_world_id_present
		&& std::memcmp(world_id, snapshot_world_id, 32U) != 0))
		return (result == FT_ERR_SUCCESS ? FT_ERR_INVALID_ARGUMENT : result);
	const file_type revision_type = file_get_type(revision_metadata_path);
	if (revision_type == FILE_TYPE_REGULAR)
	{
		ft_size_t revision_size = 0U;
		result = file_get_size(revision_metadata_path, &revision_size);
		if (result != FT_ERR_SUCCESS || revision_size > 1024U * 1024U)
			return (result == FT_ERR_SUCCESS ? FT_ERR_OUT_OF_RANGE : result);
		result = read_file(revision_metadata_path, revision_metadata);
		if (result != FT_ERR_SUCCESS)
			return (result);
		try
		{
			World metadata_probe;
			result = metadata_probe.deserialize_revision_metadata(
				revision_metadata.data(), revision_metadata.size());
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	else if (revision_type != FILE_TYPE_MISSING)
		return (FT_ERR_INVALID_PATH);
	result = game_session_bundle_serialize_terrain(world_id, terrain_archive);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (snapshot.size() > SESSION_SNAPSHOT_MAX_BYTES - 96U
		|| revision_metadata.size() > 1024U * 1024U
		|| snapshot.size() + revision_metadata.size() + terrain_archive.size()
			> SESSION_SNAPSHOT_MAX_BYTES - 96U)
		return (FT_ERR_OUT_OF_RANGE);
	try
	{
		bundle.reserve(96U + snapshot.size() + revision_metadata.size()
			+ terrain_archive.size());
		bundle.insert(bundle.end(), {'W', 'B', 'K', '1'});
		append_u32(bundle, 3U);
		bundle.insert(bundle.end(), world_id, world_id + 32U);
		append_u64(bundle, snapshot.size());
		append_u64(bundle, revision_metadata.size());
		append_u64(bundle, terrain_archive.size());
		append_u64(bundle, checksum(snapshot.data(), snapshot.size()));
		append_u64(bundle, revision_metadata.empty() ? 0U
			: checksum(revision_metadata.data(), revision_metadata.size()));
		append_u64(bundle, checksum(terrain_archive.data(),
			terrain_archive.size()));
		bundle.insert(bundle.end(), snapshot.begin(), snapshot.end());
		bundle.insert(bundle.end(), revision_metadata.begin(),
			revision_metadata.end());
		bundle.insert(bundle.end(), terrain_archive.begin(),
			terrain_archive.end());
		append_u64(bundle, checksum(bundle.data(), bundle.size()));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (file_write_all_atomic(bundle_path,
		reinterpret_cast<const char *>(bundle.data()),
		static_cast<ft_size_t>(bundle.size())));
}

int32_t GameSessionPersistence::backup_catalog_world(
	WorldSaveCatalog &catalog, const uint8_t world_id[32],
	uint64_t actor_id, const char *bundle_path) noexcept
{
	WorldSaveDefinition definition = {};
	std::string snapshot_path;
	std::string revision_path;
	if (world_id == nullptr || bundle_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	int32_t result = catalog.find(world_id, &definition);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = catalog.begin_world_maintenance(world_id, actor_id,
		definition.name);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = GameSession::acquire_world_maintenance(world_id);
	if (result != FT_ERR_SUCCESS)
	{
		(void)catalog.end_world_maintenance(world_id, actor_id);
		return (result);
	}
	result = game_session_bundle_world_paths(world_id, snapshot_path,
		revision_path);
	if (result == FT_ERR_SUCCESS)
		result = GameSessionPersistence::export_world_bundle(
			snapshot_path.c_str(), revision_path.c_str(), bundle_path);
	const int32_t session_end_result =
		GameSession::release_world_maintenance(world_id);
	const int32_t catalog_end_result = catalog.end_world_maintenance(world_id,
		actor_id);
	if (session_end_result != FT_ERR_SUCCESS
		|| catalog_end_result != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	return (result);
}

int32_t GameSessionPersistence::restore_catalog_world(
	WorldSaveCatalog &catalog, const uint8_t world_id[32],
	uint64_t actor_id, const std::string &typed_world_name,
	const char *bundle_path) noexcept
{
	std::string snapshot_path;
	std::string revision_path;
	if (world_id == nullptr || bundle_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	int32_t result = catalog.begin_world_maintenance(world_id, actor_id,
		typed_world_name);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = GameSession::acquire_world_maintenance(world_id);
	if (result != FT_ERR_SUCCESS)
	{
		(void)catalog.end_world_maintenance(world_id, actor_id);
		return (result);
	}
	result = game_session_bundle_world_paths(world_id, snapshot_path,
		revision_path);
	if (result == FT_ERR_SUCCESS)
		result = GameSessionPersistence::import_world_bundle(bundle_path,
			snapshot_path.c_str(), revision_path.c_str());
	const int32_t session_end_result =
		GameSession::release_world_maintenance(world_id);
	const int32_t catalog_end_result = catalog.end_world_maintenance(world_id,
		actor_id);
	if (session_end_result != FT_ERR_SUCCESS
		|| catalog_end_result != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	return (result);
}

static int32_t game_session_bundle_prepare_delete_journal(
	const char *snapshot_path, const char *revision_metadata_path,
	std::string &journal_path, std::string &snapshot_backup_path,
	std::string &revision_backup_path) noexcept
{
	std::vector<uint8_t> snapshot;
	std::vector<uint8_t> revision;
	std::vector<uint8_t> journal;
	const file_type snapshot_type = file_get_type(snapshot_path);
	const file_type revision_type = file_get_type(revision_metadata_path);
	if ((snapshot_type != FILE_TYPE_REGULAR
			&& snapshot_type != FILE_TYPE_MISSING)
		|| (revision_type != FILE_TYPE_REGULAR
			&& revision_type != FILE_TYPE_MISSING))
		return (FT_ERR_INVALID_PATH);
	if (snapshot_type == FILE_TYPE_REGULAR)
	{
		ft_size_t file_size = 0U;
		int32_t result = file_get_size(snapshot_path, &file_size);
		if (result != FT_ERR_SUCCESS || file_size > SESSION_SNAPSHOT_MAX_BYTES)
			return (result == FT_ERR_SUCCESS ? FT_ERR_OUT_OF_RANGE : result);
		result = read_file(snapshot_path, snapshot);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	if (revision_type == FILE_TYPE_REGULAR)
	{
		ft_size_t file_size = 0U;
		int32_t result = file_get_size(revision_metadata_path, &file_size);
		if (result != FT_ERR_SUCCESS || file_size > 1024U * 1024U)
			return (result == FT_ERR_SUCCESS ? FT_ERR_OUT_OF_RANGE : result);
		result = read_file(revision_metadata_path, revision);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	int32_t result = game_session_bundle_journal_paths(snapshot_path,
		journal_path, snapshot_backup_path, revision_backup_path);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = snapshot_type == FILE_TYPE_REGULAR
		? file_write_all_atomic(snapshot_backup_path.c_str(),
			reinterpret_cast<const char *>(snapshot.data()),
			static_cast<ft_size_t>(snapshot.size()))
		: game_session_bundle_remove_file(snapshot_backup_path.c_str());
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = revision_type == FILE_TYPE_REGULAR
		? file_write_all_atomic(revision_backup_path.c_str(),
			reinterpret_cast<const char *>(revision.data()),
			static_cast<ft_size_t>(revision.size()))
		: game_session_bundle_remove_file(revision_backup_path.c_str());
	if (result != FT_ERR_SUCCESS)
	{
		(void)game_session_bundle_remove_file(snapshot_backup_path.c_str());
		return (result);
	}
	try
	{
		journal.reserve(36U);
		journal.insert(journal.end(), {'W', 'B', 'J', '1'});
		append_u32(journal, 1U);
		append_u32(journal,
			(snapshot_type == FILE_TYPE_REGULAR ? 1U : 0U)
			| (revision_type == FILE_TYPE_REGULAR ? 2U : 0U));
		append_u64(journal, snapshot_type == FILE_TYPE_REGULAR
			? checksum(snapshot.data(), snapshot.size()) : 0U);
		append_u64(journal, revision_type == FILE_TYPE_REGULAR
			? checksum(revision.data(), revision.size()) : 0U);
		append_u64(journal, checksum(journal.data(), journal.size()));
	}
	catch (const std::bad_alloc &)
	{
		(void)game_session_bundle_remove_file(snapshot_backup_path.c_str());
		(void)game_session_bundle_remove_file(revision_backup_path.c_str());
		return (FT_ERR_NO_MEMORY);
	}
	result = file_write_all_atomic(journal_path.c_str(),
		reinterpret_cast<const char *>(journal.data()),
		static_cast<ft_size_t>(journal.size()));
	if (result != FT_ERR_SUCCESS)
	{
		(void)game_session_bundle_remove_file(snapshot_backup_path.c_str());
		(void)game_session_bundle_remove_file(revision_backup_path.c_str());
	}
	return (result);
}

static int32_t game_session_bundle_deletion_marker_path(
	const char *snapshot_path, std::string &marker_path) noexcept
{
	try
	{
		marker_path.assign(snapshot_path);
		marker_path.append(".wdel");
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (file_validate_path_resolution_inside_root("saves/world_items",
		marker_path.c_str()));
}

static int32_t game_session_bundle_write_deletion_marker(
	const uint8_t world_id[32], const char *snapshot_path,
	std::string &marker_path) noexcept
{
	std::vector<uint8_t> marker;
	if (world_id == nullptr || snapshot_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	int32_t result = game_session_bundle_deletion_marker_path(snapshot_path,
		marker_path);
	if (result != FT_ERR_SUCCESS)
		return (result);
	try
	{
		marker.reserve(48U);
		marker.insert(marker.end(), {'W', 'D', 'L', '1'});
		append_u32(marker, 1U);
		marker.insert(marker.end(), world_id, world_id + 32U);
		append_u64(marker, checksum(marker.data(), marker.size()));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (file_write_all_atomic(marker_path.c_str(),
		reinterpret_cast<const char *>(marker.data()),
		static_cast<ft_size_t>(marker.size())));
}

static int32_t game_session_bundle_parse_deletion_marker(
	const char *path, uint8_t world_id_out[32]) noexcept
{
	if (path == nullptr || world_id_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	const char *basename = std::strrchr(path, '/');
	const char *backslash = std::strrchr(path, '\\');
	if (backslash != nullptr && (basename == nullptr || backslash > basename))
		basename = backslash;
	basename = basename == nullptr ? path : basename + 1;
	if (std::strlen(basename) != 69U
		|| std::strcmp(basename + 64U, ".wdel") != 0)
		return (FT_ERR_INVALID_ARGUMENT);
	for (std::size_t index = 0U; index < 32U; ++index)
	{
		uint8_t decoded = 0U;
		for (std::size_t nibble = 0U; nibble < 2U; ++nibble)
		{
			const char value = basename[index * 2U + nibble];
			uint8_t digit = 0U;
			if (value >= '0' && value <= '9')
				digit = static_cast<uint8_t>(value - '0');
			else if (value >= 'a' && value <= 'f')
				digit = static_cast<uint8_t>(value - 'a' + 10);
			else
				return (FT_ERR_INVALID_ARGUMENT);
			decoded = static_cast<uint8_t>((decoded << 4U) | digit);
		}
		world_id_out[index] = decoded;
	}
	return (FT_ERR_SUCCESS);
}

int32_t GameSessionPersistence::recover_catalog_world_deletions(
	WorldSaveCatalog &catalog, const uint8_t world_id_filter[32]) noexcept
{
	if (file_get_type("saves/world_items") == FILE_TYPE_MISSING)
		return (FT_ERR_SUCCESS);
	if (file_get_type("saves/world_items") != FILE_TYPE_DIRECTORY)
		return (FT_ERR_INVALID_PATH);
	ft_vector<ft_string> entries;
	int32_t result = entries.initialize();
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = file_list_directory("saves/world_items", entries,
		FILE_LIST_INCLUDE_FILES);
	for (ft_size_t index = 0U;
		result == FT_ERR_SUCCESS && index < entries.size(); ++index)
	{
		const char *path = entries[index].c_str();
		const char *basename = std::strrchr(path, '/');
		const char *backslash = std::strrchr(path, '\\');
		if (backslash != nullptr && (basename == nullptr || backslash > basename))
			basename = backslash;
		basename = basename == nullptr ? path : basename + 1;
		const std::size_t path_length = std::strlen(basename);
		if (path_length < 5U
			|| std::strcmp(basename + path_length - 5U, ".wdel") != 0)
			continue;
		result = file_validate_regular_file_inside_root("saves/world_items",
			path);
		uint8_t world_id[32] = {};
		if (result == FT_ERR_SUCCESS)
			result = game_session_bundle_parse_deletion_marker(path, world_id);
		if (result == FT_ERR_SUCCESS && world_id_filter != nullptr
			&& std::memcmp(world_id, world_id_filter, 32U) != 0)
			continue;
		ft_size_t marker_size = 0U;
		if (result == FT_ERR_SUCCESS)
			result = file_get_size(path, &marker_size);
		if (result == FT_ERR_SUCCESS && marker_size != 48U)
			result = FT_ERR_INVALID_ARGUMENT;
		std::vector<uint8_t> marker;
		if (result == FT_ERR_SUCCESS)
			result = read_file(path, marker);
		if (result != FT_ERR_SUCCESS)
			break ;
		if (marker.size() != 48U
			|| std::memcmp(marker.data(), "WDL1", 4U) != 0
			|| read_u32(marker.data(), 4U) != 1U
			|| std::memcmp(marker.data() + 8U, world_id, 32U) != 0
			|| checksum(marker.data(), marker.size() - 8U)
			!= read_u64(marker.data(), marker.size() - 8U))
		{
			result = FT_ERR_INVALID_ARGUMENT;
			break ;
		}
		WorldSaveDefinition definition = {};
		const int32_t find_result = catalog.find(world_id, &definition);
		std::string snapshot_path;
		std::string revision_path;
		std::string journal_path;
		std::string snapshot_backup_path;
		std::string revision_backup_path;
		if (find_result != FT_ERR_SUCCESS && find_result != FT_ERR_NOT_FOUND)
		{
			result = find_result;
			break ;
		}
		bool catalog_maintenance = false;
		bool session_maintenance = false;
		if (find_result == FT_ERR_SUCCESS)
		{
			result = catalog.begin_world_maintenance(world_id,
				definition.owner_actor_id, definition.name);
			if (result != FT_ERR_SUCCESS)
				break ;
			catalog_maintenance = true;
		}
		result = GameSession::acquire_world_maintenance(world_id);
		if (result == FT_ERR_SUCCESS)
			session_maintenance = true;
		if (session_maintenance)
		{
			result = game_session_bundle_world_paths(world_id, snapshot_path,
				revision_path);
			if (result == FT_ERR_SUCCESS)
				result = game_session_bundle_journal_paths(
					snapshot_path.c_str(), journal_path,
					snapshot_backup_path, revision_backup_path);
			if (result == FT_ERR_SUCCESS)
			{
				if (find_result == FT_ERR_SUCCESS)
					result = GameSessionPersistence::recover_world_bundle_import(
						snapshot_path.c_str(), revision_path.c_str());
				else
				{
					result = game_session_bundle_remove_file(
						snapshot_path.c_str());
					if (result == FT_ERR_SUCCESS)
						result = game_session_bundle_remove_file(
							revision_path.c_str());
					if (result == FT_ERR_SUCCESS)
						result = game_session_bundle_remove_file(
							journal_path.c_str());
					if (result == FT_ERR_SUCCESS)
						result = game_session_bundle_remove_file(
							snapshot_backup_path.c_str());
					if (result == FT_ERR_SUCCESS)
						result = game_session_bundle_remove_file(
							revision_backup_path.c_str());
					if (result == FT_ERR_SUCCESS)
						result = game_session_bundle_remove_terrain(world_id);
				}
			}
			if (result == FT_ERR_SUCCESS)
				result = game_session_bundle_remove_file(path);
		}
		if (session_maintenance)
		{
			const int32_t release_result =
				GameSession::release_world_maintenance(world_id);
			if (result == FT_ERR_SUCCESS && release_result != FT_ERR_SUCCESS)
				result = FT_ERR_INVALID_STATE;
		}
		if (catalog_maintenance)
		{
			const int32_t release_result = catalog.end_world_maintenance(
				world_id, definition.owner_actor_id);
			if (result == FT_ERR_SUCCESS && release_result != FT_ERR_SUCCESS)
				result = FT_ERR_INVALID_STATE;
		}
		if (result != FT_ERR_SUCCESS)
			break ;
	}
	const int32_t destroy_result = entries.destroy();
	if (result == FT_ERR_SUCCESS)
		result = destroy_result;
	return (result);
}

int32_t GameSessionPersistence::initialize_catalog(
	WorldSaveCatalog &catalog, const char *catalog_path) noexcept
{
	int32_t result = catalog.initialize(catalog_path);
	if (result == FT_ERR_SUCCESS)
		result = GameSessionPersistence::recover_catalog_world_deletions(catalog);
	return (result);
}

int32_t GameSessionPersistence::delete_catalog_world(
	WorldSaveCatalog &catalog, const uint8_t world_id[32], uint64_t actor_id,
	const std::string &typed_world_name) noexcept
{
	WorldSaveDefinition definition = {};
	std::string snapshot_path;
	std::string revision_path;
	std::string journal_path;
	std::string marker_path;
	std::string snapshot_backup_path;
	std::string revision_backup_path;
	if (world_id == nullptr || actor_id == 0U || typed_world_name.empty())
		return (FT_ERR_INVALID_ARGUMENT);
	int32_t result = GameSessionPersistence::recover_catalog_world_deletions(
		catalog, world_id);
	if (result == FT_ERR_SUCCESS)
		result = catalog.find(world_id, &definition);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = catalog.begin_world_maintenance(world_id, actor_id,
		typed_world_name);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = GameSession::acquire_world_maintenance(world_id);
	if (result != FT_ERR_SUCCESS)
	{
		(void)catalog.end_world_maintenance(world_id, actor_id);
		return (result);
	}
	result = file_create_directories("saves/world_items", 0700);
	if (result == FT_ERR_SUCCESS)
		result = game_session_bundle_world_paths(world_id, snapshot_path,
			revision_path);
	if (result == FT_ERR_SUCCESS)
		result = GameSessionPersistence::recover_world_bundle_import(
			snapshot_path.c_str(), revision_path.c_str());
	if (result == FT_ERR_SUCCESS)
		result = game_session_bundle_write_deletion_marker(world_id,
			snapshot_path.c_str(), marker_path);
	if (result == FT_ERR_SUCCESS)
		result = game_session_bundle_prepare_delete_journal(
			snapshot_path.c_str(), revision_path.c_str(), journal_path,
			snapshot_backup_path, revision_backup_path);
	if (result == FT_ERR_SUCCESS)
		result = game_session_bundle_remove_file(snapshot_path.c_str());
	if (result == FT_ERR_SUCCESS)
		result = game_session_bundle_remove_file(revision_path.c_str());
	if (result == FT_ERR_SUCCESS)
		result = catalog.delete_world(world_id, actor_id, typed_world_name);
	if (result != FT_ERR_SUCCESS)
	{
		const int32_t recovery_result = snapshot_path.empty()
			|| revision_path.empty() ? FT_ERR_SUCCESS
			: GameSessionPersistence::recover_world_bundle_import(
				snapshot_path.c_str(), revision_path.c_str());
		const int32_t marker_remove_result =
			recovery_result != FT_ERR_SUCCESS ? recovery_result
			: (marker_path.empty() ? FT_ERR_SUCCESS
				: game_session_bundle_remove_file(marker_path.c_str()));
		const int32_t session_end_result =
			GameSession::release_world_maintenance(world_id);
		const int32_t catalog_end_result = catalog.end_world_maintenance(
			world_id, actor_id);
		if (recovery_result != FT_ERR_SUCCESS
			|| marker_remove_result != FT_ERR_SUCCESS
			|| session_end_result != FT_ERR_SUCCESS
			|| catalog_end_result != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
		return (result);
	}
	const int32_t session_end_result =
		GameSession::release_world_maintenance(world_id);
	if (session_end_result != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	result = game_session_bundle_remove_terrain(world_id);
	if (result != FT_ERR_SUCCESS)
		return (result);
	const int32_t journal_remove_result =
		game_session_bundle_remove_file(journal_path.c_str());
	const int32_t snapshot_backup_remove_result =
		game_session_bundle_remove_file(snapshot_backup_path.c_str());
	const int32_t revision_backup_remove_result =
		game_session_bundle_remove_file(revision_backup_path.c_str());
	const int32_t marker_remove_result =
		game_session_bundle_remove_file(marker_path.c_str());
	if (journal_remove_result != FT_ERR_SUCCESS
		|| snapshot_backup_remove_result != FT_ERR_SUCCESS
		|| revision_backup_remove_result != FT_ERR_SUCCESS
		|| marker_remove_result != FT_ERR_SUCCESS)
		return (FT_ERR_IO);
	return (FT_ERR_SUCCESS);
}

int32_t GameSessionPersistence::import_world_bundle(const char *bundle_path,
	const char *snapshot_path, const char *revision_metadata_path) noexcept
{
	std::vector<uint8_t> bundle;
	std::vector<uint8_t> snapshot;
	std::vector<uint8_t> revision_metadata;
	std::vector<uint8_t> bundle_terrain;
	std::vector<uint8_t> old_snapshot;
	std::vector<uint8_t> old_revision_metadata;
	std::vector<uint8_t> old_terrain_archive;
	std::vector<uint8_t> journal;
	std::vector<std::pair<std::string, std::vector<uint8_t> > >
		validated_bundle_terrain;
	std::string journal_path;
	std::string snapshot_backup_path;
	std::string revision_backup_path;
	std::string terrain_backup_path;
	std::string terrain_directory;
	uint8_t target_world_id[32] = {};
	uint8_t snapshot_world_id[32] = {};
	bool snapshot_world_id_present = false;
	bool had_terrain = false;
	if (bundle_path == nullptr || snapshot_path == nullptr
		|| revision_metadata_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	int32_t result = file_create_directories("saves/world_items", 0700);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = game_session_bundle_validate_paths(bundle_path,
		snapshot_path, revision_metadata_path);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (!game_session_bundle_read_world_id(snapshot_path, target_world_id))
		return (FT_ERR_INVALID_PATH);
	result = GameSessionPersistence::recover_world_bundle_import(snapshot_path,
		revision_metadata_path);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (file_validate_regular_file_inside_root("saves/backups", bundle_path)
		!= FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_PATH);
	result = read_file(bundle_path, bundle);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (bundle.size() < 80U || bundle.size() > SESSION_SNAPSHOT_MAX_BYTES
		|| std::memcmp(bundle.data(), "WBK1", 4U) != 0
		|| checksum(bundle.data(), bundle.size() - 8U)
			!= (static_cast<uint64_t>(bundle[bundle.size() - 8U])
				| (static_cast<uint64_t>(bundle[bundle.size() - 7U]) << 8U)
				| (static_cast<uint64_t>(bundle[bundle.size() - 6U]) << 16U)
				| (static_cast<uint64_t>(bundle[bundle.size() - 5U]) << 24U)
				| (static_cast<uint64_t>(bundle[bundle.size() - 4U]) << 32U)
				| (static_cast<uint64_t>(bundle[bundle.size() - 3U]) << 40U)
				| (static_cast<uint64_t>(bundle[bundle.size() - 2U]) << 48U)
				| (static_cast<uint64_t>(bundle[bundle.size() - 1U]) << 56U)))
		return (FT_ERR_INVALID_ARGUMENT);
	Reader reader = {bundle.data(), bundle.size() - 8U, 4U};
	uint32_t version = 0U;
	uint8_t bundled_world_id[32] = {};
	uint64_t snapshot_size = 0U;
	uint64_t revision_size = 0U;
	uint64_t terrain_size = 0U;
	uint64_t snapshot_checksum = 0U;
	uint64_t revision_checksum = 0U;
	uint64_t terrain_checksum = 0U;
	if (!reader.read_u32(&version) || (version != 2U && version != 3U)
		|| reader.offset > reader.size
		|| reader.size - reader.offset < sizeof(bundled_world_id))
		return (FT_ERR_INVALID_ARGUMENT);
	std::memcpy(bundled_world_id, reader.bytes + reader.offset,
		sizeof(bundled_world_id));
	reader.offset += sizeof(bundled_world_id);
	if (std::memcmp(bundled_world_id, target_world_id,
			sizeof(target_world_id)) != 0
		|| !reader.read_u64(&snapshot_size)
		|| !reader.read_u64(&revision_size)
		|| !reader.read_u64(&snapshot_checksum)
		|| !reader.read_u64(&revision_checksum)
		|| (version == 3U && (!reader.read_u64(&terrain_size)
			|| !reader.read_u64(&terrain_checksum)))
		|| snapshot_size < 32U
		|| snapshot_size > SESSION_SNAPSHOT_MAX_BYTES
		|| revision_size > 1024U * 1024U
		|| terrain_size > SESSION_SNAPSHOT_MAX_BYTES
		|| snapshot_size + revision_size > reader.size - reader.offset
		|| (version == 2U && snapshot_size + revision_size
			!= reader.size - reader.offset)
		|| (version == 3U && terrain_size != reader.size - reader.offset
			- snapshot_size - revision_size))
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		const uint8_t *snapshot_begin = bundle.data() + reader.offset;
		const uint8_t *revision_begin = snapshot_begin + snapshot_size;
		snapshot.assign(snapshot_begin, revision_begin);
		revision_metadata.assign(revision_begin,
			revision_begin + revision_size);
		if (version == 3U)
			bundle_terrain.assign(revision_begin + revision_size,
				bundle.data() + reader.size);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (checksum(snapshot.data(), snapshot.size()) != snapshot_checksum
		|| !is_valid_mcsp_snapshot(snapshot)
		|| (revision_metadata.empty()
			? revision_checksum != 0U
			: checksum(revision_metadata.data(), revision_metadata.size())
				!= revision_checksum))
		return (FT_ERR_INVALID_ARGUMENT);
	if (version == 3U
		&& (checksum(bundle_terrain.data(), bundle_terrain.size())
			!= terrain_checksum
			|| game_session_bundle_parse_terrain(bundle_terrain.data(),
				bundle_terrain.size(), validated_bundle_terrain)
				!= FT_ERR_SUCCESS))
		return (FT_ERR_INVALID_ARGUMENT);
	result = mcsp_snapshot_world_id(snapshot, snapshot_world_id,
		&snapshot_world_id_present);
	if (result != FT_ERR_SUCCESS || (snapshot_world_id_present
		&& std::memcmp(snapshot_world_id, target_world_id, 32U) != 0))
		return (result == FT_ERR_SUCCESS ? FT_ERR_INVALID_ARGUMENT : result);
	if (!revision_metadata.empty())
	{
		try
		{
			World metadata_probe;
			result = metadata_probe.deserialize_revision_metadata(
				revision_metadata.data(), revision_metadata.size());
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	const file_type old_snapshot_type = file_get_type(snapshot_path);
	const file_type old_revision_type = file_get_type(revision_metadata_path);
	if ((old_snapshot_type != FILE_TYPE_REGULAR
			&& old_snapshot_type != FILE_TYPE_MISSING)
		|| (old_revision_type != FILE_TYPE_REGULAR
			&& old_revision_type != FILE_TYPE_MISSING))
		return (FT_ERR_INVALID_PATH);
	const bool had_snapshot = old_snapshot_type == FILE_TYPE_REGULAR;
	const bool had_revision = old_revision_type == FILE_TYPE_REGULAR;
	if (had_revision)
	{
		ft_size_t old_revision_size = 0U;
		result = file_get_size(revision_metadata_path, &old_revision_size);
		if (result != FT_ERR_SUCCESS || old_revision_size > 1024U * 1024U)
			return (result == FT_ERR_SUCCESS ? FT_ERR_OUT_OF_RANGE : result);
	}
	if (had_snapshot && (result = read_file(snapshot_path, old_snapshot))
		!= FT_ERR_SUCCESS)
		return (result);
	if (had_revision && (result = read_file(revision_metadata_path,
			old_revision_metadata)) != FT_ERR_SUCCESS)
		return (result);
	result = game_session_bundle_journal_paths(snapshot_path, journal_path,
		snapshot_backup_path, revision_backup_path);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = game_session_bundle_terrain_backup_path(snapshot_path,
		terrain_backup_path);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (version == 3U)
	{
		result = game_session_bundle_terrain_path(target_world_id,
			terrain_directory);
		if (result != FT_ERR_SUCCESS)
			return (result);
		const file_type terrain_type = file_get_type(terrain_directory.c_str());
		if (terrain_type != FILE_TYPE_MISSING
			&& terrain_type != FILE_TYPE_DIRECTORY)
			return (FT_ERR_INVALID_PATH);
		had_terrain = terrain_type == FILE_TYPE_DIRECTORY;
		result = game_session_bundle_serialize_terrain(target_world_id,
			old_terrain_archive);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	if (had_snapshot)
		result = file_write_all_atomic(snapshot_backup_path.c_str(),
			reinterpret_cast<const char *>(old_snapshot.data()),
			static_cast<ft_size_t>(old_snapshot.size()));
	else
		result = game_session_bundle_remove_file(snapshot_backup_path.c_str());
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (had_revision)
		result = file_write_all_atomic(revision_backup_path.c_str(),
			reinterpret_cast<const char *>(old_revision_metadata.data()),
			static_cast<ft_size_t>(old_revision_metadata.size()));
	else
		result = game_session_bundle_remove_file(revision_backup_path.c_str());
	if (result != FT_ERR_SUCCESS)
	{
		(void)game_session_bundle_remove_file(snapshot_backup_path.c_str());
		return (result);
	}
	if (version == 3U)
	{
		if (had_terrain)
			result = file_write_all_atomic(terrain_backup_path.c_str(),
				reinterpret_cast<const char *>(old_terrain_archive.data()),
				static_cast<ft_size_t>(old_terrain_archive.size()));
		else
			result = game_session_bundle_remove_file(
				terrain_backup_path.c_str());
		if (result != FT_ERR_SUCCESS)
		{
			(void)game_session_bundle_remove_file(snapshot_backup_path.c_str());
			(void)game_session_bundle_remove_file(revision_backup_path.c_str());
			return (result);
		}
	}
	try
	{
		journal.reserve(version == 3U ? 44U : 36U);
		journal.insert(journal.end(), {'W', 'B', 'J', '1'});
		append_u32(journal, version == 3U ? 2U : 1U);
		append_u32(journal, (had_snapshot ? 1U : 0U)
			| (had_revision ? 2U : 0U)
			| (version == 3U && had_terrain ? 4U : 0U));
		append_u64(journal, had_snapshot
			? checksum(old_snapshot.data(), old_snapshot.size()) : 0U);
		append_u64(journal, had_revision
			? checksum(old_revision_metadata.data(),
				old_revision_metadata.size()) : 0U);
		if (version == 3U)
			append_u64(journal, had_terrain
				? checksum(old_terrain_archive.data(),
					old_terrain_archive.size()) : 0U);
		append_u64(journal, checksum(journal.data(), journal.size()));
	}
	catch (const std::bad_alloc &)
	{
		(void)game_session_bundle_remove_file(snapshot_backup_path.c_str());
		(void)game_session_bundle_remove_file(revision_backup_path.c_str());
		(void)game_session_bundle_remove_file(terrain_backup_path.c_str());
		return (FT_ERR_NO_MEMORY);
	}
	result = file_write_all_atomic(journal_path.c_str(),
		reinterpret_cast<const char *>(journal.data()),
		static_cast<ft_size_t>(journal.size()));
	if (result != FT_ERR_SUCCESS)
	{
		(void)game_session_bundle_remove_file(snapshot_backup_path.c_str());
		(void)game_session_bundle_remove_file(revision_backup_path.c_str());
		(void)game_session_bundle_remove_file(terrain_backup_path.c_str());
		return (result);
	}
	result = file_write_all_atomic(snapshot_path,
		reinterpret_cast<const char *>(snapshot.data()),
		static_cast<ft_size_t>(snapshot.size()));
	if (result != FT_ERR_SUCCESS)
	{
		const int32_t rollback_result =
			GameSessionPersistence::recover_world_bundle_import(snapshot_path,
				revision_metadata_path);
		return (rollback_result == FT_ERR_SUCCESS ? result
			: FT_ERR_INVALID_STATE);
	}
	if (revision_metadata.empty())
	{
		if (had_revision)
			result = file_delete(revision_metadata_path);
	}
	else
		result = file_write_all_atomic(revision_metadata_path,
			reinterpret_cast<const char *>(revision_metadata.data()),
			static_cast<ft_size_t>(revision_metadata.size()));
	if (result == FT_ERR_SUCCESS && version == 3U)
		result = game_session_bundle_install_terrain(target_world_id,
			bundle_terrain.data(), bundle_terrain.size());
	if (result == FT_ERR_SUCCESS)
		result = file_delete(journal_path.c_str());
	if (result != FT_ERR_SUCCESS)
	{
		const int32_t rollback_result =
			GameSessionPersistence::recover_world_bundle_import(snapshot_path,
				revision_metadata_path);
		return (rollback_result == FT_ERR_SUCCESS ? result
			: FT_ERR_INVALID_STATE);
	}
	(void)game_session_bundle_remove_file(snapshot_backup_path.c_str());
	(void)game_session_bundle_remove_file(revision_backup_path.c_str());
	(void)game_session_bundle_remove_file(terrain_backup_path.c_str());
	return (FT_ERR_SUCCESS);
}
