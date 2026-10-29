#include "../../src/world/WorldSaveCatalog.hpp"
#include "../../Libft/Modules/File/file_utils.hpp"
#include "../../Libft/Modules/RNG/rng.hpp"
#include "../../Libft/Modules/Voxel/voxel_types.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <new>
#include <utility>

namespace
{
	static const uint32_t WORLD_SAVE_CATALOG_VERSION = 7U;
	static const uint32_t WORLD_SAVE_CATALOG_OWNER_VERSION = 2U;
	static const uint32_t WORLD_SAVE_CATALOG_LEGACY_VERSION = 1U;
	static const uint32_t WORLD_SAVE_CATALOG_MAX_ACTORS = 4096U;
	static const uint32_t WORLD_SAVE_CATALOG_MAX_BANNED_CHARACTERS = 4096U;
	static const uint32_t WORLD_SAVE_CATALOG_MAX_APPROVED_CHARACTERS = 4096U;
	static const uint32_t WORLD_SAVE_CATALOG_MAX_PLAYERS = 256U;
	static const std::size_t WORLD_SAVE_CATALOG_MAX_BYTES = 1024U * 1024U;
	static const uint64_t WORLD_SAVE_CATALOG_FNV_OFFSET =
		14695981039346656037ULL;
	static const uint64_t WORLD_SAVE_CATALOG_FNV_PRIME = 1099511628211ULL;

	static void append_u16(std::vector<uint8_t> &bytes, uint16_t value)
	{
		bytes.push_back(static_cast<uint8_t>(value));
		bytes.push_back(static_cast<uint8_t>(value >> 8U));
	}

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

	static uint16_t read_u16(const uint8_t *bytes, std::size_t offset)
	{
		return (static_cast<uint16_t>(bytes[offset])
			| static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1U])
				<< 8U));
	}

	static uint32_t read_u32(const uint8_t *bytes, std::size_t offset)
	{
		return (static_cast<uint32_t>(bytes[offset])
			| (static_cast<uint32_t>(bytes[offset + 1U]) << 8U)
			| (static_cast<uint32_t>(bytes[offset + 2U]) << 16U)
			| (static_cast<uint32_t>(bytes[offset + 3U]) << 24U));
	}

	static uint64_t read_u64(const uint8_t *bytes, std::size_t offset)
	{
		uint64_t value = 0U;
		for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			value |= static_cast<uint64_t>(bytes[offset + shift / 8U]) << shift;
		return (value);
	}

	static uint64_t checksum(const uint8_t *bytes, std::size_t size)
	{
		uint64_t hash = WORLD_SAVE_CATALOG_FNV_OFFSET;
		for (std::size_t index = 0U; index < size; ++index)
		{
			hash ^= bytes[index];
			hash *= WORLD_SAVE_CATALOG_FNV_PRIME;
		}
		return (hash);
	}

	static bool valid_name(const std::string &value) noexcept
	{
		if (value.empty() || value.size() > 64U)
			return (false);
		for (std::size_t index = 0U; index < value.size();)
		{
			const uint8_t lead = static_cast<uint8_t>(value[index]);
			uint32_t code_point;
			uint32_t continuation_count;
			uint32_t minimum_code_point;
			if (lead <= 0x7FU)
			{
				if (lead < 0x20U || lead == 0x7FU)
					return (false);
				++index;
				continue ;
			}
			if (lead >= 0xC2U && lead <= 0xDFU)
			{
				code_point = lead & 0x1FU;
				continuation_count = 1U;
				minimum_code_point = 0x80U;
			}
			else if (lead >= 0xE0U && lead <= 0xEFU)
			{
				code_point = lead & 0x0FU;
				continuation_count = 2U;
				minimum_code_point = 0x800U;
			}
			else if (lead >= 0xF0U && lead <= 0xF4U)
			{
				code_point = lead & 0x07U;
				continuation_count = 3U;
				minimum_code_point = 0x10000U;
			}
			else
				return (false);
			if (value.size() - index <= continuation_count)
				return (false);
			for (uint32_t part = 0U; part < continuation_count; ++part)
			{
				const uint8_t continuation = static_cast<uint8_t>(
					value[index + 1U + part]);
				if ((continuation & 0xC0U) != 0x80U)
					return (false);
				code_point = (code_point << 6U) | (continuation & 0x3FU);
			}
			if (code_point < minimum_code_point || code_point > 0x10FFFFU
				|| (code_point >= 0xD800U && code_point <= 0xDFFFU)
				|| (code_point >= 0x80U && code_point <= 0x9FU))
				return (false);
			index += continuation_count + 1U;
		}
		return (true);
	}

	static bool valid_seed(const std::string &value) noexcept
	{
		return (value.size() <= 127U
			&& value.find('\0') == std::string::npos);
	}

	static bool valid_policy(uint32_t difficulty, uint32_t ownership,
		uint32_t access, uint32_t spawn) noexcept
	{
		return (difficulty <= 3U
			&& (ownership == WORLD_SAVE_OWNER_LOCAL
				|| ownership == WORLD_SAVE_OWNER_SHARED)
			&& access >= WORLD_SAVE_ACCESS_PRIVATE
			&& access <= WORLD_SAVE_ACCESS_PUBLIC
			&& spawn >= WORLD_SAVE_SPAWN_DEFAULT
			&& spawn <= WORLD_SAVE_SPAWN_BED_BOUND);
	}

	static bool valid_character_admission(uint32_t admission,
		uint32_t maximum_players) noexcept
	{
		return (admission >= WORLD_SAVE_CHARACTER_WORLD_BOUND_ONLY
			&& admission <= WORLD_SAVE_CHARACTER_FRESH_ONLY
			&& maximum_players > 0U
			&& maximum_players <= WORLD_SAVE_CATALOG_MAX_PLAYERS);
	}

	static bool id_is_nonzero(const uint8_t id[32]) noexcept
	{
		if (id == nullptr)
			return (false);
		for (std::size_t index = 0U; index < 32U; ++index)
			if (id[index] != 0U)
				return (true);
		return (false);
	}

	static bool same_id(const uint8_t left[32], const uint8_t right[32])
		noexcept
	{
		return (std::memcmp(left, right, 32U) == 0);
	}

	static bool character_id_is_listed(
		const std::vector<std::array<uint8_t, 32> > &character_ids,
		const uint8_t character_id[32]) noexcept
	{
		for (const std::array<uint8_t, 32> &entry : character_ids)
			if (same_id(entry.data(), character_id))
				return (true);
		return (false);
	}

	static bool access_is_allowed(const WorldSaveDefinition &entry,
		uint64_t actor_id, bool host_reports_friend) noexcept
	{
		return (entry.owner_actor_id == actor_id
			|| entry.access_policy == WORLD_SAVE_ACCESS_PUBLIC
			|| (entry.access_policy == WORLD_SAVE_ACCESS_FRIENDS_ONLY
				&& host_reports_friend)
			|| ((entry.access_policy == WORLD_SAVE_ACCESS_INVITE_ONLY
					|| entry.access_policy == WORLD_SAVE_ACCESS_ALLOWLIST)
				&& std::binary_search(entry.allowed_actor_ids.begin(),
					entry.allowed_actor_ids.end(), actor_id)));
	}

	static bool approval_is_allowed(const WorldSaveDefinition &entry,
		uint64_t actor_id, const uint8_t character_id[32]) noexcept
	{
		return (entry.character_admission
			!= WORLD_SAVE_CHARACTER_APPROVAL_REQUIRED
			|| actor_id == entry.owner_actor_id
			|| std::binary_search(entry.allowed_actor_ids.begin(),
				entry.allowed_actor_ids.end(), actor_id)
			|| character_id_is_listed(entry.approved_character_ids,
				character_id));
	}

	static bool same_world_id(const uint8_t left[32],
		const uint8_t right[32]) noexcept
	{
		return (left != nullptr && right != nullptr
			&& std::memcmp(left, right, 32U) == 0);
	}

	static bool valid_entry(const WorldSaveDefinition &entry) noexcept
	{
		if (!id_is_nonzero(entry.world_id)
			|| !valid_name(entry.name) || !valid_seed(entry.seed)
			|| entry.generator_version == 0U
			|| entry.ruleset_version == 0U
			|| !valid_policy(entry.difficulty, entry.ownership_mode,
				entry.access_policy, entry.spawn_policy)
			|| entry.created_epoch_ms == 0U
			|| entry.modified_epoch_ms < entry.created_epoch_ms
			|| entry.minimum_generator_version == 0U
			|| entry.minimum_generator_version
				> entry.maximum_generator_version
			|| entry.minimum_ruleset_version == 0U
			|| entry.minimum_ruleset_version > entry.maximum_ruleset_version
			|| (entry.character_import_mask & ~WORLD_SAVE_IMPORT_ALL) != 0U
			|| entry.maximum_progression_level > 1000U
			|| entry.maximum_imported_item_instances > 65536U
			|| entry.maximum_imported_item_value > UINT64_C(1000000000000)
			|| entry.allowed_actor_ids.size()
				> WORLD_SAVE_CATALOG_MAX_ACTORS
			|| !valid_character_admission(entry.character_admission,
				entry.maximum_players))
			return (false);
		if (!std::is_sorted(entry.allowed_actor_ids.begin(),
			entry.allowed_actor_ids.end())
			|| std::adjacent_find(entry.allowed_actor_ids.begin(),
				entry.allowed_actor_ids.end()) != entry.allowed_actor_ids.end())
			return (false);
		for (uint64_t actor_id : entry.allowed_actor_ids)
			if (actor_id == 0U || actor_id == entry.owner_actor_id)
				return (false);
		if (entry.banned_character_ids.size()
			> WORLD_SAVE_CATALOG_MAX_BANNED_CHARACTERS
			|| !std::is_sorted(entry.banned_character_ids.begin(),
				entry.banned_character_ids.end())
			|| std::adjacent_find(entry.banned_character_ids.begin(),
				entry.banned_character_ids.end())
				!= entry.banned_character_ids.end())
			return (false);
		for (const std::array<uint8_t, 32> &character_id
			: entry.banned_character_ids)
			if (!id_is_nonzero(character_id.data()))
				return (false);
		if (entry.approved_character_ids.size()
			> WORLD_SAVE_CATALOG_MAX_APPROVED_CHARACTERS
			|| !std::is_sorted(entry.approved_character_ids.begin(),
				entry.approved_character_ids.end())
			|| std::adjacent_find(entry.approved_character_ids.begin(),
				entry.approved_character_ids.end())
				!= entry.approved_character_ids.end())
			return (false);
		for (const std::array<uint8_t, 32> &character_id
			: entry.approved_character_ids)
			if (!id_is_nonzero(character_id.data()))
				return (false);
		return (true);
	}

	static uint64_t now_epoch_ms() noexcept
	{
		const auto now = std::chrono::system_clock::now().time_since_epoch();
		return (static_cast<uint64_t>(std::chrono::duration_cast<
			std::chrono::milliseconds>(now).count()));
	}

	static int32_t serialize(const std::vector<WorldSaveDefinition> &entries,
		std::vector<uint8_t> &bytes_out) noexcept
	{
		std::vector<uint8_t> bytes;
		if (entries.size() > WorldSaveCatalog::MAX_WORLDS)
			return (FT_ERR_FULL);
		try
		{
			bytes.reserve(16U + entries.size() * 96U);
			bytes.insert(bytes.end(), {'W', 'C', 'T', '1'});
			append_u32(bytes, WORLD_SAVE_CATALOG_VERSION);
			append_u32(bytes, static_cast<uint32_t>(entries.size()));
			for (std::size_t index = 0U; index < entries.size(); ++index)
			{
				const WorldSaveDefinition &entry = entries[index];
				if (!valid_entry(entry))
					return (FT_ERR_INVALID_STATE);
				for (std::size_t previous = 0U; previous < index; ++previous)
					if (same_id(entries[previous].world_id, entry.world_id))
						return (FT_ERR_ALREADY_EXISTS);
				bytes.insert(bytes.end(), entry.world_id, entry.world_id + 32U);
				append_u16(bytes, static_cast<uint16_t>(entry.name.size()));
				append_u16(bytes, static_cast<uint16_t>(entry.seed.size()));
				append_u32(bytes, entry.generator_version);
				append_u32(bytes, entry.ruleset_version);
				append_u32(bytes, entry.difficulty);
				append_u32(bytes, entry.ownership_mode);
				append_u32(bytes, entry.access_policy);
				append_u32(bytes, entry.spawn_policy);
				append_u64(bytes, entry.created_epoch_ms);
				append_u64(bytes, entry.modified_epoch_ms);
				append_u64(bytes, entry.owner_actor_id);
				append_u32(bytes,
					static_cast<uint32_t>(entry.allowed_actor_ids.size()));
				for (uint64_t actor_id : entry.allowed_actor_ids)
					append_u64(bytes, actor_id);
				append_u32(bytes, entry.character_admission);
				append_u32(bytes, entry.maximum_players);
				append_u32(bytes, static_cast<uint32_t>(
					entry.banned_character_ids.size()));
				for (const std::array<uint8_t, 32> &character_id
					: entry.banned_character_ids)
					bytes.insert(bytes.end(), character_id.begin(),
						character_id.end());
				append_u32(bytes, entry.minimum_generator_version);
				append_u32(bytes, entry.maximum_generator_version);
				append_u32(bytes, entry.minimum_ruleset_version);
				append_u32(bytes, entry.maximum_ruleset_version);
				append_u32(bytes, entry.character_import_mask);
				append_u32(bytes, entry.maximum_progression_level);
				append_u32(bytes, entry.maximum_imported_item_instances);
				append_u64(bytes, entry.maximum_imported_item_value);
				append_u32(bytes, static_cast<uint32_t>(
					entry.approved_character_ids.size()));
				for (const std::array<uint8_t, 32> &character_id
					: entry.approved_character_ids)
					bytes.insert(bytes.end(), character_id.begin(),
						character_id.end());
				bytes.insert(bytes.end(), entry.name.begin(), entry.name.end());
				bytes.insert(bytes.end(), entry.seed.begin(), entry.seed.end());
				if (bytes.size() > WORLD_SAVE_CATALOG_MAX_BYTES - 8U)
					return (FT_ERR_FULL);
			}
			append_u64(bytes, checksum(bytes.data(), bytes.size()));
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		bytes_out.swap(bytes);
		return (FT_ERR_SUCCESS);
	}

	static int32_t deserialize(const uint8_t *bytes, std::size_t byte_count,
		std::vector<WorldSaveDefinition> &entries_out) noexcept
	{
		std::vector<WorldSaveDefinition> entries;
		std::size_t offset = 12U;
		if (bytes == nullptr || byte_count < 20U
			|| byte_count > WORLD_SAVE_CATALOG_MAX_BYTES
			|| std::memcmp(bytes, "WCT1", 4U) != 0
			|| (read_u32(bytes, 4U) != WORLD_SAVE_CATALOG_VERSION
				&& read_u32(bytes, 4U) != 6U
				&& read_u32(bytes, 4U) != 5U
				&& read_u32(bytes, 4U) != 4U
				&& read_u32(bytes, 4U) != 3U
				&& read_u32(bytes, 4U) != WORLD_SAVE_CATALOG_OWNER_VERSION
				&& read_u32(bytes, 4U) != WORLD_SAVE_CATALOG_LEGACY_VERSION))
			return (FT_ERR_INVALID_ARGUMENT);
		const uint32_t version = read_u32(bytes, 4U);
		const uint32_t count = read_u32(bytes, 8U);
		if (count > WorldSaveCatalog::MAX_WORLDS
			|| read_u64(bytes, byte_count - 8U)
				!= checksum(bytes, byte_count - 8U))
			return (FT_ERR_INVALID_ARGUMENT);
		try
		{
			entries.reserve(count);
			for (uint32_t index = 0U; index < count; ++index)
			{
				const std::size_t fixed_record_size = version >= 7U ? 140U
					: (version >= 6U ? 136U
					: (version >= 5U ? 116U
					: (version >= 4U ? 100U
					: (version >= 3U ? 96U
						: (version >= 2U ? 88U : 76U)))));
				if (offset > byte_count - 8U
					|| byte_count - 8U - offset < fixed_record_size)
					return (FT_ERR_INVALID_ARGUMENT);
				WorldSaveDefinition entry = {};
				std::memcpy(entry.world_id, bytes + offset, 32U);
				offset += 32U;
				const uint16_t name_length = read_u16(bytes, offset);
				const uint16_t seed_length = read_u16(bytes, offset + 2U);
				offset += 4U;
				entry.generator_version = read_u32(bytes, offset);
				entry.ruleset_version = read_u32(bytes, offset + 4U);
				entry.difficulty = read_u32(bytes, offset + 8U);
				entry.ownership_mode = read_u32(bytes, offset + 12U);
				entry.access_policy = read_u32(bytes, offset + 16U);
				entry.spawn_policy = read_u32(bytes, offset + 20U);
				entry.created_epoch_ms = read_u64(bytes, offset + 24U);
				entry.modified_epoch_ms = read_u64(bytes, offset + 32U);
				offset += 40U;
				uint32_t allowed_count = 0U;
				if (version >= 2U)
				{
					entry.owner_actor_id = read_u64(bytes, offset);
					allowed_count = read_u32(bytes, offset + 8U);
					offset += 12U;
					const std::size_t trailing_fields =
						(version >= 3U ? 8U : 0U)
						+ (version >= 4U ? 4U : 0U)
						+ (version >= 5U ? 16U : 0U)
						+ (version >= 6U ? 20U : 0U)
						+ (version >= 7U ? 4U : 0U)
						+ static_cast<std::size_t>(name_length)
						+ static_cast<std::size_t>(seed_length);
					if (offset > byte_count - 8U
						|| byte_count - 8U - offset < trailing_fields)
						return (FT_ERR_INVALID_ARGUMENT);
					if (allowed_count > WORLD_SAVE_CATALOG_MAX_ACTORS
						|| static_cast<std::size_t>(allowed_count)
							> (byte_count - 8U - offset - trailing_fields) / 8U)
						return (FT_ERR_INVALID_ARGUMENT);
					entry.allowed_actor_ids.reserve(allowed_count);
					for (uint32_t actor_index = 0U;
						actor_index < allowed_count; ++actor_index)
					{
						entry.allowed_actor_ids.push_back(read_u64(bytes, offset));
						offset += 8U;
					}
				}
				else
				{
					entry.character_admission =
						WORLD_SAVE_CHARACTER_WORLD_BOUND_ONLY;
					entry.maximum_players = 1U;
				}
				if (version >= 3U)
				{
					entry.character_admission = read_u32(bytes, offset);
					entry.maximum_players = read_u32(bytes, offset + 4U);
					offset += 8U;
					if (version >= 4U)
					{
						const std::size_t trailing_fields =
							(version >= 5U ? 16U : 0U)
							+ (version >= 6U ? 20U : 0U)
							+ (version >= 7U ? 4U : 0U)
							+ static_cast<std::size_t>(name_length)
							+ static_cast<std::size_t>(seed_length);
						if (offset > byte_count - 8U
							|| byte_count - 8U - offset
								< 4U + trailing_fields)
							return (FT_ERR_INVALID_ARGUMENT);
						const uint32_t banned_count = read_u32(bytes, offset);
						offset += 4U;
						if (offset > byte_count - 8U
							|| byte_count - 8U - offset < trailing_fields)
							return (FT_ERR_INVALID_ARGUMENT);
						if (banned_count
							> WORLD_SAVE_CATALOG_MAX_BANNED_CHARACTERS
							|| static_cast<std::size_t>(banned_count)
								> (byte_count - 8U - offset
									- trailing_fields) / 32U)
							return (FT_ERR_INVALID_ARGUMENT);
						entry.banned_character_ids.reserve(banned_count);
						for (uint32_t banned_index = 0U;
							banned_index < banned_count; ++banned_index)
						{
							std::array<uint8_t, 32> banned_id;
							std::memcpy(banned_id.data(), bytes + offset, 32U);
							entry.banned_character_ids.push_back(banned_id);
							offset += 32U;
						}
					}
				}
				else if (version == 2U)
			{
				entry.character_admission =
					WORLD_SAVE_CHARACTER_WORLD_BOUND_ONLY;
				entry.maximum_players = 1U;
				}
				if (version >= 5U)
				{
					entry.minimum_generator_version = read_u32(bytes, offset);
					entry.maximum_generator_version = read_u32(bytes, offset + 4U);
					entry.minimum_ruleset_version = read_u32(bytes, offset + 8U);
					entry.maximum_ruleset_version = read_u32(bytes, offset + 12U);
					offset += 16U;
				}
				else
				{
					entry.minimum_generator_version = entry.generator_version;
					entry.maximum_generator_version = entry.generator_version;
					entry.minimum_ruleset_version = entry.ruleset_version;
					entry.maximum_ruleset_version = entry.ruleset_version;
				}
				if (version >= 6U)
				{
					entry.character_import_mask = read_u32(bytes, offset);
					entry.maximum_progression_level = read_u32(bytes, offset + 4U);
					entry.maximum_imported_item_instances = read_u32(bytes,
						offset + 8U);
					entry.maximum_imported_item_value = read_u64(bytes,
						offset + 12U);
					offset += 20U;
				}
				if (version >= 7U)
				{
					if (offset > byte_count - 8U
						|| byte_count - 8U - offset
							< 4U + static_cast<std::size_t>(name_length)
								+ static_cast<std::size_t>(seed_length))
						return (FT_ERR_INVALID_ARGUMENT);
					const uint32_t approved_count = read_u32(bytes, offset);
					offset += 4U;
					const std::size_t trailing_strings =
						static_cast<std::size_t>(name_length)
						+ static_cast<std::size_t>(seed_length);
					if (offset > byte_count - 8U
						|| byte_count - 8U - offset < trailing_strings
						|| approved_count
							> WORLD_SAVE_CATALOG_MAX_APPROVED_CHARACTERS
						|| static_cast<std::size_t>(approved_count)
							> (byte_count - 8U - offset - trailing_strings) / 32U)
						return (FT_ERR_INVALID_ARGUMENT);
					entry.approved_character_ids.reserve(approved_count);
					for (uint32_t approved_index = 0U;
						approved_index < approved_count; ++approved_index)
					{
						std::array<uint8_t, 32> approved_id;
						std::memcpy(approved_id.data(), bytes + offset, 32U);
						entry.approved_character_ids.push_back(approved_id);
						offset += 32U;
					}
				}
				if (name_length == 0U || name_length > 64U || seed_length > 127U
					|| offset > byte_count - 8U
					|| static_cast<std::size_t>(name_length) + seed_length
						> byte_count - 8U - offset)
					return (FT_ERR_INVALID_ARGUMENT);
				entry.name.assign(reinterpret_cast<const char *>(bytes + offset),
					name_length);
				offset += name_length;
				entry.seed.assign(reinterpret_cast<const char *>(bytes + offset),
					seed_length);
				offset += seed_length;
				if (!valid_entry(entry))
					return (FT_ERR_INVALID_ARGUMENT);
				for (const WorldSaveDefinition &previous : entries)
					if (same_id(previous.world_id, entry.world_id))
						return (FT_ERR_ALREADY_EXISTS);
				entries.push_back(std::move(entry));
			}
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		if (offset != byte_count - 8U)
			return (FT_ERR_INVALID_ARGUMENT);
		entries_out.swap(entries);
		return (FT_ERR_SUCCESS);
	}

	static std::mutex world_save_catalog_registry_mutex;
	static std::vector<std::string> world_save_catalog_paths;

	static int32_t register_world_save_catalog_path(
		const std::string &catalog_path) noexcept
	{
		std::lock_guard<std::mutex> lock(world_save_catalog_registry_mutex);
		for (const std::string &registered_path : world_save_catalog_paths)
			if (file_path_equal(registered_path.c_str(), catalog_path.c_str())
				== FT_TRUE)
				return (FT_ERR_ALREADY_EXISTS);
		try
		{
			world_save_catalog_paths.push_back(catalog_path);
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		return (FT_ERR_SUCCESS);
	}

	static void unregister_world_save_catalog_path(
		const std::string &catalog_path) noexcept
	{
		std::lock_guard<std::mutex> lock(world_save_catalog_registry_mutex);
		for (std::vector<std::string>::iterator it =
			world_save_catalog_paths.begin();
			it != world_save_catalog_paths.end(); ++it)
			if (file_path_equal(it->c_str(), catalog_path.c_str()) == FT_TRUE)
			{
				world_save_catalog_paths.erase(it);
				return ;
			}
	}
}

const uint32_t WorldSaveCatalog::MAX_WORLDS;

WorldSaveCatalog::WorldSaveCatalog() : catalog_path_(), entries_(),
	active_world_maintenance_(), active_join_leases_(),
	catalog_path_registered_(false), initialized_(false)
{
}

WorldSaveCatalog::WorldSaveCatalog(const WorldSaveCatalog &other)
	: catalog_path_(), entries_(), active_world_maintenance_(),
	active_join_leases_(), catalog_path_registered_(false), initialized_(false)
{
	(void)other;
}

WorldSaveCatalog::~WorldSaveCatalog()
{
	this->unregister_catalog_path();
}

void WorldSaveCatalog::unregister_catalog_path() noexcept
{
	if (this->catalog_path_registered_)
	{
		unregister_world_save_catalog_path(this->catalog_path_);
		this->catalog_path_registered_ = false;
	}
}

WorldSaveCatalog &WorldSaveCatalog::operator=(const WorldSaveCatalog &other)
{
	(void)other;
	return (*this);
}

int32_t WorldSaveCatalog::initialize(const char *catalog_path) noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (catalog_path == nullptr || catalog_path[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->initialized_)
		return (FT_ERR_ALREADY_EXISTS);
	try
	{
		this->catalog_path_ = catalog_path;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	int32_t path_result = file_create_directories("saves", 0700);
	if (path_result != FT_ERR_SUCCESS)
	{
		this->catalog_path_.clear();
		return (path_result);
	}
	if (file_validate_path_resolution_inside_root("saves", catalog_path)
		!= FT_ERR_SUCCESS)
	{
		this->catalog_path_.clear();
		return (FT_ERR_INVALID_PATH);
	}
	const int32_t registration_result = register_world_save_catalog_path(
		this->catalog_path_);
	if (registration_result != FT_ERR_SUCCESS)
	{
		this->catalog_path_.clear();
		return (registration_result);
	}
	this->catalog_path_registered_ = true;
	const file_type type = file_get_type(catalog_path);
	if (type == FILE_TYPE_REGULAR)
	{
		ft_string contents;
		ft_size_t file_size = 0U;
		int32_t result = contents.initialize();
		if (result == FT_ERR_SUCCESS)
			result = file_get_size(catalog_path, &file_size);
		if (result == FT_ERR_SUCCESS
			&& file_size > WORLD_SAVE_CATALOG_MAX_BYTES)
			result = FT_ERR_OUT_OF_RANGE;
		if (result == FT_ERR_SUCCESS)
			result = file_read_all(catalog_path, contents);
		std::vector<WorldSaveDefinition> loaded;
		if (result == FT_ERR_SUCCESS)
			result = deserialize(reinterpret_cast<const uint8_t *>(
				contents.data()), contents.size(), loaded);
		const int32_t destroy_result = contents.destroy();
		if (result == FT_ERR_SUCCESS)
			result = destroy_result;
		if (result != FT_ERR_SUCCESS)
		{
			this->unregister_catalog_path();
			this->catalog_path_.clear();
			return (result);
		}
		this->entries_.swap(loaded);
	}
	else if (type == FILE_TYPE_MISSING)
	{
		const int32_t result = this->save_locked(this->entries_);
		if (result != FT_ERR_SUCCESS)
		{
			this->unregister_catalog_path();
			this->catalog_path_.clear();
			return (result);
		}
	}
	else
	{
		this->unregister_catalog_path();
		this->catalog_path_.clear();
		return (FT_ERR_INVALID_PATH);
	}
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldSaveCatalog::list(
	std::vector<WorldSaveDefinition> &entries_out) const noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	try
	{
		std::vector<WorldSaveDefinition> staged = this->entries_;
		entries_out.swap(staged);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldSaveCatalog::find(const uint8_t world_id[32],
	WorldSaveDefinition *entry_out) const noexcept
{
	if (!id_is_nonzero(world_id) || entry_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (const WorldSaveDefinition &entry : this->entries_)
		if (same_id(entry.world_id, world_id))
		{
			try
			{
				WorldSaveDefinition staged = entry;
				*entry_out = std::move(staged);
			}
			catch (const std::bad_alloc &)
			{
				return (FT_ERR_NO_MEMORY);
			}
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::begin_world_maintenance(
	const uint8_t world_id[32], uint64_t actor_id,
	const std::string &typed_world_name) noexcept
{
	if (!id_is_nonzero(world_id) || actor_id == 0U
		|| typed_world_name.empty())
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (const std::array<uint8_t, 32> &maintenance
		: this->active_world_maintenance_)
		if (same_id(maintenance.data(), world_id))
			return (FT_ERR_INVALID_STATE);
	for (const WorldSaveDefinition &entry : this->entries_)
		if (same_id(entry.world_id, world_id))
		{
			if (entry.owner_actor_id != actor_id)
				return (FT_ERR_PERMISSION_DENIED);
			if (entry.name != typed_world_name)
				return (FT_ERR_INVALID_ARGUMENT);
			for (const ActiveJoinLease &lease : this->active_join_leases_)
				if (same_id(lease.world_id, world_id))
					return (FT_ERR_INVALID_STATE);
			std::array<uint8_t, 32> maintenance_id;
			std::memcpy(maintenance_id.data(), world_id, 32U);
			try
			{
				this->active_world_maintenance_.push_back(maintenance_id);
			}
			catch (const std::bad_alloc &)
			{
				return (FT_ERR_NO_MEMORY);
			}
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::end_world_maintenance(
	const uint8_t world_id[32], uint64_t actor_id) noexcept
{
	if (!id_is_nonzero(world_id) || actor_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	const WorldSaveDefinition *entry = nullptr;
	for (const WorldSaveDefinition &candidate : this->entries_)
		if (same_id(candidate.world_id, world_id))
		{
			entry = &candidate;
			break ;
		}
	if (entry == nullptr)
		return (FT_ERR_NOT_FOUND);
	if (entry->owner_actor_id != actor_id)
		return (FT_ERR_PERMISSION_DENIED);
	for (std::vector<std::array<uint8_t, 32> >::iterator it =
		this->active_world_maintenance_.begin();
		it != this->active_world_maintenance_.end(); ++it)
		if (same_id(it->data(), world_id))
		{
			this->active_world_maintenance_.erase(it);
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::delete_world(const uint8_t world_id[32],
	uint64_t actor_id, const std::string &typed_world_name) noexcept
{
	if (!id_is_nonzero(world_id) || actor_id == 0U
		|| typed_world_name.empty())
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	bool maintenance_active = false;
	for (const std::array<uint8_t, 32> &maintenance
		: this->active_world_maintenance_)
		if (same_id(maintenance.data(), world_id))
			maintenance_active = true;
	if (!maintenance_active)
		return (FT_ERR_INVALID_STATE);
	for (const ActiveJoinLease &lease : this->active_join_leases_)
		if (same_id(lease.world_id, world_id))
			return (FT_ERR_INVALID_STATE);
	for (const WorldSaveDefinition &entry : this->entries_)
		if (same_id(entry.world_id, world_id))
		{
			if (entry.owner_actor_id != actor_id)
				return (FT_ERR_PERMISSION_DENIED);
			if (entry.name != typed_world_name)
				return (FT_ERR_INVALID_ARGUMENT);
			std::vector<WorldSaveDefinition> staged;
			std::vector<uint8_t> encoded;
			try
			{
				staged = this->entries_;
			}
			catch (const std::bad_alloc &)
			{
				return (FT_ERR_NO_MEMORY);
			}
			staged.erase(std::remove_if(staged.begin(), staged.end(),
				[world_id](const WorldSaveDefinition &candidate)
				{
					return (same_id(candidate.world_id, world_id));
				}), staged.end());
			const int32_t result = serialize(staged, encoded);
			if (result != FT_ERR_SUCCESS)
				return (result);
			const int32_t save_result = file_replace_safe(
				this->catalog_path_.c_str(),
				reinterpret_cast<const char *>(encoded.data()), encoded.size());
			if (save_result != FT_ERR_SUCCESS)
				return (save_result);
			this->entries_.swap(staged);
			this->active_world_maintenance_.erase(std::remove_if(
				this->active_world_maintenance_.begin(),
				this->active_world_maintenance_.end(),
				[world_id](const std::array<uint8_t, 32> &maintenance)
				{
					return (same_id(maintenance.data(), world_id));
				}), this->active_world_maintenance_.end());
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::create(const std::string &name,
	const std::string &seed,
	uint64_t owner_actor_id, uint32_t difficulty, uint32_t ownership_mode,
	uint32_t access_policy, uint32_t spawn_policy,
	uint32_t character_admission, uint32_t maximum_players,
	WorldSaveDefinition *entry_out) noexcept
{
	if (!valid_name(name) || !valid_seed(seed)
		|| owner_actor_id == 0U
		|| !valid_policy(difficulty, ownership_mode, access_policy, spawn_policy)
		|| !valid_character_admission(character_admission, maximum_players)
		|| entry_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (this->entries_.size() >= MAX_WORLDS)
		return (FT_ERR_FULL);
	WorldSaveDefinition entry = {};
	WorldSaveDefinition output_staged = {};
	try
	{
		entry.name = name;
		entry.seed = seed;
		entry.generator_version = VOXEL_GENERATOR_VERSION;
		entry.ruleset_version = WORLD_SAVE_CURRENT_RULESET_VERSION;
		entry.minimum_generator_version = entry.generator_version;
		entry.maximum_generator_version = entry.generator_version;
		entry.minimum_ruleset_version = entry.ruleset_version;
		entry.maximum_ruleset_version = entry.ruleset_version;
		entry.difficulty = difficulty;
		entry.ownership_mode = ownership_mode;
		entry.access_policy = access_policy;
		entry.spawn_policy = spawn_policy;
		entry.character_admission = character_admission;
		entry.maximum_players = maximum_players;
		entry.owner_actor_id = owner_actor_id;
		entry.created_epoch_ms = now_epoch_ms();
		entry.modified_epoch_ms = entry.created_epoch_ms;
		if (entry.created_epoch_ms == 0U)
			return (FT_ERR_IO);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	bool unique_id = false;
	for (uint32_t attempt = 0U; attempt < 4U && !unique_id; ++attempt)
	{
		const int32_t random_result = rng_secure_bytes(entry.world_id, 32U);
		if (random_result != FT_ERR_SUCCESS)
			return (random_result);
		unique_id = id_is_nonzero(entry.world_id);
		for (const WorldSaveDefinition &existing : this->entries_)
			if (same_id(existing.world_id, entry.world_id))
				unique_id = false;
	}
	if (!unique_id)
		return (FT_ERR_ALREADY_EXISTS);
	try
	{
		output_staged = entry;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	std::vector<WorldSaveDefinition> staged;
	std::vector<uint8_t> encoded;
	try
	{
		staged = this->entries_;
		staged.push_back(entry);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	int32_t result = serialize(staged, encoded);
	if (result == FT_ERR_SUCCESS)
		result = file_replace_safe(this->catalog_path_.c_str(),
			reinterpret_cast<const char *>(encoded.data()), encoded.size());
	if (result != FT_ERR_SUCCESS)
		return (result);
	this->entries_.swap(staged);
	*entry_out = std::move(output_staged);
	return (FT_ERR_SUCCESS);
}

int32_t WorldSaveCatalog::rename(const uint8_t world_id[32],
	uint64_t actor_id, const std::string &new_name) noexcept
{
	if (!id_is_nonzero(world_id) || actor_id == 0U || !valid_name(new_name))
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (const std::array<uint8_t, 32> &maintenance
		: this->active_world_maintenance_)
		if (same_id(maintenance.data(), world_id))
			return (FT_ERR_INVALID_STATE);
	std::vector<WorldSaveDefinition> staged;
	std::vector<uint8_t> encoded;
	try
	{
		staged = this->entries_;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (WorldSaveDefinition &entry : staged)
		if (same_id(entry.world_id, world_id))
		{
			if (entry.owner_actor_id != actor_id)
				return (FT_ERR_PERMISSION_DENIED);
			try
			{
				entry.name = new_name;
			}
			catch (const std::bad_alloc &)
			{
				return (FT_ERR_NO_MEMORY);
			}
			entry.modified_epoch_ms = std::max(now_epoch_ms(),
				entry.created_epoch_ms);
			const int32_t result = serialize(staged, encoded);
			if (result != FT_ERR_SUCCESS)
				return (result);
			const int32_t save_result = file_replace_safe(
				this->catalog_path_.c_str(),
				reinterpret_cast<const char *>(encoded.data()), encoded.size());
			if (save_result != FT_ERR_SUCCESS)
				return (save_result);
			this->entries_.swap(staged);
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::update_policy(const uint8_t world_id[32],
	uint64_t actor_id, uint32_t difficulty, uint32_t ownership_mode,
	uint32_t access_policy, uint32_t spawn_policy) noexcept
{
	if (!id_is_nonzero(world_id) || actor_id == 0U
		|| !valid_policy(difficulty, ownership_mode, access_policy, spawn_policy))
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (const std::array<uint8_t, 32> &maintenance
		: this->active_world_maintenance_)
		if (same_id(maintenance.data(), world_id))
			return (FT_ERR_INVALID_STATE);
	for (const ActiveJoinLease &lease : this->active_join_leases_)
		if (same_id(lease.world_id, world_id))
			return (FT_ERR_INVALID_STATE);
	std::vector<WorldSaveDefinition> staged;
	std::vector<uint8_t> encoded;
	try
	{
		staged = this->entries_;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (WorldSaveDefinition &entry : staged)
		if (same_id(entry.world_id, world_id))
		{
			if (entry.owner_actor_id != actor_id)
				return (FT_ERR_PERMISSION_DENIED);
			entry.difficulty = difficulty;
			entry.ownership_mode = ownership_mode;
			entry.access_policy = access_policy;
			entry.spawn_policy = spawn_policy;
			entry.modified_epoch_ms = std::max(now_epoch_ms(),
				entry.created_epoch_ms);
			const int32_t result = serialize(staged, encoded);
			if (result != FT_ERR_SUCCESS)
				return (result);
			const int32_t save_result = file_replace_safe(
				this->catalog_path_.c_str(),
				reinterpret_cast<const char *>(encoded.data()), encoded.size());
			if (save_result != FT_ERR_SUCCESS)
				return (save_result);
			this->entries_.swap(staged);
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::set_allowed_actors(const uint8_t world_id[32],
	uint64_t actor_id, const std::vector<uint64_t> &actor_ids) noexcept
{
	if (!id_is_nonzero(world_id) || actor_id == 0U
		|| actor_ids.size() > WORLD_SAVE_CATALOG_MAX_ACTORS)
		return (FT_ERR_INVALID_ARGUMENT);
	for (uint64_t actor_id : actor_ids)
		if (actor_id == 0U)
			return (FT_ERR_INVALID_ARGUMENT);
	if (!std::is_sorted(actor_ids.begin(), actor_ids.end())
		|| std::adjacent_find(actor_ids.begin(), actor_ids.end())
			!= actor_ids.end())
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (const std::array<uint8_t, 32> &maintenance
		: this->active_world_maintenance_)
		if (same_id(maintenance.data(), world_id))
			return (FT_ERR_INVALID_STATE);
	for (const ActiveJoinLease &lease : this->active_join_leases_)
		if (same_id(lease.world_id, world_id))
			return (FT_ERR_INVALID_STATE);
	std::vector<WorldSaveDefinition> staged;
	std::vector<uint8_t> encoded;
	try
	{
		staged = this->entries_;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (WorldSaveDefinition &entry : staged)
		if (same_id(entry.world_id, world_id))
		{
			if (entry.owner_actor_id != actor_id)
				return (FT_ERR_PERMISSION_DENIED);
			if (entry.owner_actor_id == 0U
				|| std::binary_search(actor_ids.begin(), actor_ids.end(),
					entry.owner_actor_id))
				return (FT_ERR_INVALID_ARGUMENT);
			try
			{
				entry.allowed_actor_ids = actor_ids;
			}
			catch (const std::bad_alloc &)
			{
				return (FT_ERR_NO_MEMORY);
			}
			entry.modified_epoch_ms = std::max(now_epoch_ms(),
				entry.created_epoch_ms);
			const int32_t result = serialize(staged, encoded);
			if (result != FT_ERR_SUCCESS)
				return (result);
			const int32_t save_result = file_replace_safe(
				this->catalog_path_.c_str(),
				reinterpret_cast<const char *>(encoded.data()), encoded.size());
			if (save_result != FT_ERR_SUCCESS)
				return (save_result);
			this->entries_.swap(staged);
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::set_banned_characters(const uint8_t world_id[32],
	uint64_t actor_id,
	const std::vector<std::array<uint8_t, 32> > &character_ids) noexcept
{
	if (!id_is_nonzero(world_id) || actor_id == 0U
		|| character_ids.size() > WORLD_SAVE_CATALOG_MAX_BANNED_CHARACTERS
		|| !std::is_sorted(character_ids.begin(), character_ids.end())
		|| std::adjacent_find(character_ids.begin(), character_ids.end())
			!= character_ids.end())
		return (FT_ERR_INVALID_ARGUMENT);
	for (const std::array<uint8_t, 32> &character_id : character_ids)
		if (!id_is_nonzero(character_id.data()))
			return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (const std::array<uint8_t, 32> &maintenance
		: this->active_world_maintenance_)
		if (same_id(maintenance.data(), world_id))
			return (FT_ERR_INVALID_STATE);
	for (const ActiveJoinLease &lease : this->active_join_leases_)
		if (same_id(lease.world_id, world_id))
			return (FT_ERR_INVALID_STATE);
	std::vector<WorldSaveDefinition> staged;
	std::vector<uint8_t> encoded;
	try
	{
		staged = this->entries_;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (WorldSaveDefinition &entry : staged)
		if (same_id(entry.world_id, world_id))
		{
			if (entry.owner_actor_id != actor_id)
				return (FT_ERR_PERMISSION_DENIED);
			try
			{
				entry.banned_character_ids = character_ids;
			}
			catch (const std::bad_alloc &)
			{
				return (FT_ERR_NO_MEMORY);
			}
			entry.modified_epoch_ms = std::max(now_epoch_ms(),
				entry.created_epoch_ms);
			const int32_t result = serialize(staged, encoded);
			if (result != FT_ERR_SUCCESS)
				return (result);
			const int32_t save_result = file_replace_safe(
				this->catalog_path_.c_str(),
				reinterpret_cast<const char *>(encoded.data()), encoded.size());
			if (save_result != FT_ERR_SUCCESS)
				return (save_result);
			this->entries_.swap(staged);
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::set_approved_characters(const uint8_t world_id[32],
	uint64_t actor_id,
	const std::vector<std::array<uint8_t, 32> > &character_ids) noexcept
{
	if (!id_is_nonzero(world_id) || actor_id == 0U
		|| character_ids.size() > WORLD_SAVE_CATALOG_MAX_APPROVED_CHARACTERS
		|| !std::is_sorted(character_ids.begin(), character_ids.end())
		|| std::adjacent_find(character_ids.begin(), character_ids.end())
			!= character_ids.end())
		return (FT_ERR_INVALID_ARGUMENT);
	for (const std::array<uint8_t, 32> &character_id : character_ids)
		if (!id_is_nonzero(character_id.data()))
			return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (const std::array<uint8_t, 32> &maintenance
		: this->active_world_maintenance_)
		if (same_id(maintenance.data(), world_id))
			return (FT_ERR_INVALID_STATE);
	for (const ActiveJoinLease &lease : this->active_join_leases_)
		if (same_id(lease.world_id, world_id))
			return (FT_ERR_INVALID_STATE);
	std::vector<WorldSaveDefinition> staged;
	std::vector<uint8_t> encoded;
	try
	{
		staged = this->entries_;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (WorldSaveDefinition &entry : staged)
		if (same_id(entry.world_id, world_id))
		{
			if (entry.owner_actor_id != actor_id)
				return (FT_ERR_PERMISSION_DENIED);
			try
			{
				entry.approved_character_ids = character_ids;
			}
			catch (const std::bad_alloc &)
			{
				return (FT_ERR_NO_MEMORY);
			}
			entry.modified_epoch_ms = std::max(now_epoch_ms(),
				entry.created_epoch_ms);
			const int32_t result = serialize(staged, encoded);
			if (result != FT_ERR_SUCCESS)
				return (result);
			const int32_t save_result = file_replace_safe(
				this->catalog_path_.c_str(),
				reinterpret_cast<const char *>(encoded.data()), encoded.size());
			if (save_result != FT_ERR_SUCCESS)
				return (save_result);
			this->entries_.swap(staged);
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::authorize_access(const uint8_t world_id[32],
	uint64_t actor_id, bool host_reports_friend) const noexcept
{
	if (!id_is_nonzero(world_id) || actor_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (const std::array<uint8_t, 32> &maintenance
		: this->active_world_maintenance_)
		if (same_id(maintenance.data(), world_id))
			return (FT_ERR_INVALID_STATE);
	for (const WorldSaveDefinition &entry : this->entries_)
		if (same_id(entry.world_id, world_id))
		{
			if (access_is_allowed(entry, actor_id, host_reports_friend))
				return (FT_ERR_SUCCESS);
			return (FT_ERR_PERMISSION_DENIED);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::update_admission_policy(
	const uint8_t world_id[32], uint64_t actor_id,
	uint32_t character_admission, uint32_t maximum_players) noexcept
{
	if (!id_is_nonzero(world_id) || actor_id == 0U
		|| !valid_character_admission(character_admission, maximum_players))
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (const std::array<uint8_t, 32> &maintenance
		: this->active_world_maintenance_)
		if (same_id(maintenance.data(), world_id))
			return (FT_ERR_INVALID_STATE);
	std::vector<WorldSaveDefinition> staged;
	std::vector<uint8_t> encoded;
	try
	{
		staged = this->entries_;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (WorldSaveDefinition &entry : staged)
		if (same_id(entry.world_id, world_id))
		{
			if (entry.owner_actor_id != actor_id)
				return (FT_ERR_PERMISSION_DENIED);
			uint32_t active_players = 0U;
			for (const ActiveJoinLease &lease : this->active_join_leases_)
				if (same_id(lease.world_id, world_id))
					++active_players;
			if (active_players > maximum_players
				|| (active_players != 0U
					&& character_admission != entry.character_admission))
				return (FT_ERR_INVALID_STATE);
			entry.character_admission = character_admission;
			entry.maximum_players = maximum_players;
			entry.modified_epoch_ms = std::max(now_epoch_ms(),
				entry.created_epoch_ms);
			const int32_t result = serialize(staged, encoded);
			if (result != FT_ERR_SUCCESS)
				return (result);
			const int32_t save_result = file_replace_safe(
				this->catalog_path_.c_str(),
				reinterpret_cast<const char *>(encoded.data()), encoded.size());
			if (save_result != FT_ERR_SUCCESS)
				return (save_result);
			this->entries_.swap(staged);
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::update_compatibility_policy(
	const uint8_t world_id[32], uint64_t actor_id,
	uint32_t minimum_generator_version, uint32_t maximum_generator_version,
	uint32_t minimum_ruleset_version,
	uint32_t maximum_ruleset_version) noexcept
{
	if (!id_is_nonzero(world_id) || actor_id == 0U
		|| minimum_generator_version == 0U
		|| minimum_generator_version > maximum_generator_version
		|| minimum_ruleset_version == 0U
		|| minimum_ruleset_version > maximum_ruleset_version)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (const std::array<uint8_t, 32> &maintenance
		: this->active_world_maintenance_)
		if (same_id(maintenance.data(), world_id))
			return (FT_ERR_INVALID_STATE);
	for (const ActiveJoinLease &lease : this->active_join_leases_)
		if (same_id(lease.world_id, world_id))
			return (FT_ERR_INVALID_STATE);
	std::vector<WorldSaveDefinition> staged;
	std::vector<uint8_t> encoded;
	try
	{
		staged = this->entries_;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (WorldSaveDefinition &entry : staged)
		if (same_id(entry.world_id, world_id))
		{
			if (entry.owner_actor_id != actor_id)
				return (FT_ERR_PERMISSION_DENIED);
			if (entry.generator_version < minimum_generator_version
				|| entry.generator_version > maximum_generator_version
				|| entry.ruleset_version < minimum_ruleset_version
				|| entry.ruleset_version > maximum_ruleset_version)
				return (FT_ERR_CONFIGURATION);
			entry.minimum_generator_version = minimum_generator_version;
			entry.maximum_generator_version = maximum_generator_version;
			entry.minimum_ruleset_version = minimum_ruleset_version;
			entry.maximum_ruleset_version = maximum_ruleset_version;
			entry.modified_epoch_ms = std::max(now_epoch_ms(),
				entry.created_epoch_ms);
			const int32_t result = serialize(staged, encoded);
			if (result != FT_ERR_SUCCESS)
				return (result);
			const int32_t save_result = file_replace_safe(
				this->catalog_path_.c_str(),
				reinterpret_cast<const char *>(encoded.data()), encoded.size());
			if (save_result != FT_ERR_SUCCESS)
				return (save_result);
			this->entries_.swap(staged);
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::update_character_import_policy(
	const uint8_t world_id[32], uint64_t actor_id, uint32_t import_mask,
	uint32_t maximum_progression_level, uint32_t maximum_item_instances,
	uint64_t maximum_item_value) noexcept
{
	if (!id_is_nonzero(world_id) || actor_id == 0U
		|| (import_mask & ~WORLD_SAVE_IMPORT_ALL) != 0U
		|| maximum_progression_level > 1000U
		|| maximum_item_instances > 65536U
		|| maximum_item_value > UINT64_C(1000000000000))
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (const std::array<uint8_t, 32> &maintenance
		: this->active_world_maintenance_)
		if (same_id(maintenance.data(), world_id))
			return (FT_ERR_INVALID_STATE);
	for (const ActiveJoinLease &lease : this->active_join_leases_)
		if (same_id(lease.world_id, world_id))
			return (FT_ERR_INVALID_STATE);
	std::vector<WorldSaveDefinition> staged;
	std::vector<uint8_t> encoded;
	try
	{
		staged = this->entries_;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (WorldSaveDefinition &entry : staged)
		if (same_id(entry.world_id, world_id))
		{
			if (entry.owner_actor_id != actor_id)
				return (FT_ERR_PERMISSION_DENIED);
			entry.character_import_mask = import_mask;
			entry.maximum_progression_level = maximum_progression_level;
			entry.maximum_imported_item_instances = maximum_item_instances;
			entry.maximum_imported_item_value = maximum_item_value;
			entry.modified_epoch_ms = std::max(now_epoch_ms(),
				entry.created_epoch_ms);
			const int32_t result = serialize(staged, encoded);
			if (result != FT_ERR_SUCCESS)
				return (result);
			const int32_t save_result = file_replace_safe(
				this->catalog_path_.c_str(),
				reinterpret_cast<const char *>(encoded.data()), encoded.size());
			if (save_result != FT_ERR_SUCCESS)
				return (save_result);
			this->entries_.swap(staged);
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::authorize_join(const uint8_t world_id[32],
	uint64_t actor_id, const uint8_t character_id[32],
	bool host_reports_friend, uint32_t character_mode,
	const uint8_t source_world_id[32], bool character_is_new,
	uint32_t generator_version, uint32_t ruleset_version,
	uint32_t active_player_count) const noexcept
{
	if (!id_is_nonzero(world_id) || actor_id == 0U
		|| !id_is_nonzero(character_id)
		|| (character_mode != WORLD_SAVE_CHARACTER_WORLD_BOUND
			&& character_mode != WORLD_SAVE_CHARACTER_PORTABLE))
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (const std::array<uint8_t, 32> &maintenance
		: this->active_world_maintenance_)
		if (same_id(maintenance.data(), world_id))
			return (FT_ERR_INVALID_STATE);
	for (const WorldSaveDefinition &entry : this->entries_)
		if (same_id(entry.world_id, world_id))
		{
			for (const std::array<uint8_t, 32> &banned_id
				: entry.banned_character_ids)
				if (same_id(banned_id.data(), character_id))
					return (FT_ERR_PERMISSION_DENIED);
			if (generator_version < entry.minimum_generator_version
				|| generator_version > entry.maximum_generator_version
				|| ruleset_version < entry.minimum_ruleset_version
				|| ruleset_version > entry.maximum_ruleset_version)
				return (FT_ERR_CONFIGURATION);
			if (!access_is_allowed(entry, actor_id, host_reports_friend))
				return (FT_ERR_PERMISSION_DENIED);
			if (active_player_count >= entry.maximum_players)
				return (FT_ERR_FULL);
			const bool source_is_this_world = same_world_id(world_id,
				source_world_id);
			if (entry.character_admission
				== WORLD_SAVE_CHARACTER_WORLD_BOUND_ONLY
				&& (character_mode != WORLD_SAVE_CHARACTER_WORLD_BOUND
					|| (!character_is_new && !source_is_this_world)))
				return (FT_ERR_PERMISSION_DENIED);
			if (entry.character_admission
				== WORLD_SAVE_CHARACTER_PORTABLE_ALLOWED
				&& character_mode == WORLD_SAVE_CHARACTER_WORLD_BOUND
				&& !character_is_new && !source_is_this_world)
				return (FT_ERR_PERMISSION_DENIED);
			if (!approval_is_allowed(entry, actor_id, character_id))
				return (FT_ERR_PERMISSION_DENIED);
			if (entry.character_admission == WORLD_SAVE_CHARACTER_FRESH_ONLY
				&& !character_is_new)
				return (FT_ERR_PERMISSION_DENIED);
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::authorize_character_import(
	const uint8_t world_id[32], uint64_t actor_id,
	const uint8_t character_id[32], bool host_reports_friend,
	const WorldSaveCharacterImportSummary &summary) const noexcept
{
	if (!id_is_nonzero(world_id) || actor_id == 0U
		|| !id_is_nonzero(character_id)
		|| (summary.import_mask & ~WORLD_SAVE_IMPORT_ALL) != 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (const std::array<uint8_t, 32> &maintenance
		: this->active_world_maintenance_)
		if (same_id(maintenance.data(), world_id))
			return (FT_ERR_INVALID_STATE);
	for (const WorldSaveDefinition &entry : this->entries_)
		if (same_id(entry.world_id, world_id))
		{
			for (const std::array<uint8_t, 32> &banned_id
				: entry.banned_character_ids)
				if (same_id(banned_id.data(), character_id))
					return (FT_ERR_PERMISSION_DENIED);
			if (!access_is_allowed(entry, actor_id, host_reports_friend))
				return (FT_ERR_PERMISSION_DENIED);
			if (!approval_is_allowed(entry, actor_id, character_id))
				return (FT_ERR_PERMISSION_DENIED);
			if (entry.character_admission
				== WORLD_SAVE_CHARACTER_FRESH_ONLY
				&& (summary.import_mask != 0U
					|| summary.progression_level != 0U
					|| summary.item_instance_count != 0U
					|| summary.item_value != 0U))
				return (FT_ERR_PERMISSION_DENIED);
			if ((summary.import_mask & ~entry.character_import_mask) != 0U
				|| (summary.progression_level != 0U
					&& (summary.import_mask
						& WORLD_SAVE_IMPORT_PROGRESSION) == 0U)
				|| (summary.item_instance_count != 0U
					&& (summary.import_mask
						& (WORLD_SAVE_IMPORT_INVENTORY
							| WORLD_SAVE_IMPORT_EQUIPMENT)) == 0U)
				|| (summary.item_value != 0U
					&& (summary.import_mask
						& (WORLD_SAVE_IMPORT_INVENTORY
							| WORLD_SAVE_IMPORT_EQUIPMENT)) == 0U)
				|| summary.progression_level
					> entry.maximum_progression_level
				|| summary.item_instance_count
				> entry.maximum_imported_item_instances
				|| summary.item_value > entry.maximum_imported_item_value)
				return (FT_ERR_PERMISSION_DENIED);
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::reserve_join(const uint8_t world_id[32],
	uint64_t actor_id, uint64_t request_id, bool host_reports_friend,
	const uint8_t character_id[32], uint32_t character_mode,
	const uint8_t source_world_id[32], bool character_is_new,
	uint32_t generator_version, uint32_t ruleset_version,
	uint8_t lease_id_out[32]) noexcept
{
	if (!id_is_nonzero(world_id) || actor_id == 0U || request_id == 0U
		|| !id_is_nonzero(character_id) || lease_id_out == nullptr
		|| (character_mode != WORLD_SAVE_CHARACTER_WORLD_BOUND
			&& character_mode != WORLD_SAVE_CHARACTER_PORTABLE))
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (const std::array<uint8_t, 32> &maintenance
		: this->active_world_maintenance_)
		if (same_id(maintenance.data(), world_id))
			return (FT_ERR_INVALID_STATE);
	uint8_t normalized_source_world_id[32] = {};
	if (source_world_id != nullptr)
		std::memcpy(normalized_source_world_id, source_world_id, 32U);
	for (const ActiveJoinLease &lease : this->active_join_leases_)
	{
		if (lease.actor_id == actor_id && lease.request_id == request_id)
		{
			if (!same_id(lease.world_id, world_id)
				|| !same_id(lease.character_id, character_id)
				|| !same_id(lease.source_world_id,
					normalized_source_world_id)
				|| lease.character_mode != character_mode
				|| lease.generator_version != generator_version
				|| lease.ruleset_version != ruleset_version
				|| lease.host_reports_friend != host_reports_friend
				|| lease.character_is_new != character_is_new)
				return (FT_ERR_ALREADY_EXISTS);
			std::memcpy(lease_id_out, lease.lease_id, 32U);
			return (FT_ERR_SUCCESS);
		}
		if (same_id(lease.character_id, character_id))
			return (FT_ERR_ALREADY_EXISTS);
	}
	const WorldSaveDefinition *entry = nullptr;
	for (const WorldSaveDefinition &candidate : this->entries_)
		if (same_id(candidate.world_id, world_id))
		{
			entry = &candidate;
			break ;
		}
	if (entry == nullptr)
		return (FT_ERR_NOT_FOUND);
	for (const std::array<uint8_t, 32> &banned_id
		: entry->banned_character_ids)
		if (same_id(banned_id.data(), character_id))
			return (FT_ERR_PERMISSION_DENIED);
	if (generator_version < entry->minimum_generator_version
		|| generator_version > entry->maximum_generator_version
		|| ruleset_version < entry->minimum_ruleset_version
		|| ruleset_version > entry->maximum_ruleset_version)
		return (FT_ERR_CONFIGURATION);
	if (!access_is_allowed(*entry, actor_id, host_reports_friend))
		return (FT_ERR_PERMISSION_DENIED);
	uint32_t active_players = 0U;
	for (const ActiveJoinLease &lease : this->active_join_leases_)
		if (same_id(lease.world_id, world_id))
			++active_players;
	if (active_players >= entry->maximum_players)
		return (FT_ERR_FULL);
	const bool source_is_this_world = same_world_id(world_id,
		source_world_id);
	if (entry->character_admission == WORLD_SAVE_CHARACTER_WORLD_BOUND_ONLY
		&& (character_mode != WORLD_SAVE_CHARACTER_WORLD_BOUND
			|| (!character_is_new && !source_is_this_world)))
		return (FT_ERR_PERMISSION_DENIED);
	if (entry->character_admission == WORLD_SAVE_CHARACTER_PORTABLE_ALLOWED
		&& character_mode == WORLD_SAVE_CHARACTER_WORLD_BOUND
		&& !character_is_new && !source_is_this_world)
		return (FT_ERR_PERMISSION_DENIED);
	if (!approval_is_allowed(*entry, actor_id, character_id))
		return (FT_ERR_PERMISSION_DENIED);
	if (entry->character_admission == WORLD_SAVE_CHARACTER_FRESH_ONLY
		&& !character_is_new)
		return (FT_ERR_PERMISSION_DENIED);
	ActiveJoinLease lease = {};
	bool unique_id = false;
	for (uint32_t attempt = 0U; attempt < 4U && !unique_id; ++attempt)
	{
		int32_t result = rng_secure_bytes(lease.lease_id, 32U);
		if (result != FT_ERR_SUCCESS)
			return (result);
		unique_id = id_is_nonzero(lease.lease_id);
		for (const ActiveJoinLease &existing : this->active_join_leases_)
			if (same_id(existing.lease_id, lease.lease_id))
				unique_id = false;
	}
	if (!unique_id)
		return (FT_ERR_ALREADY_EXISTS);
	std::memcpy(lease.world_id, world_id, 32U);
	std::memcpy(lease.character_id, character_id, 32U);
	std::memcpy(lease.source_world_id, normalized_source_world_id, 32U);
	lease.actor_id = actor_id;
	lease.request_id = request_id;
	lease.character_mode = character_mode;
	lease.generator_version = generator_version;
	lease.ruleset_version = ruleset_version;
	lease.host_reports_friend = host_reports_friend;
	lease.character_is_new = character_is_new;
	try
	{
		this->active_join_leases_.push_back(lease);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	std::memcpy(lease_id_out, lease.lease_id, 32U);
	return (FT_ERR_SUCCESS);
}

int32_t WorldSaveCatalog::release_join(const uint8_t lease_id[32]) noexcept
{
	if (!id_is_nonzero(lease_id))
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (std::vector<ActiveJoinLease>::iterator it =
		this->active_join_leases_.begin();
		it != this->active_join_leases_.end(); ++it)
		if (same_id(it->lease_id, lease_id))
		{
			this->active_join_leases_.erase(it);
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldSaveCatalog::validate_join_lease(
	const uint8_t lease_id[32]) const noexcept
{
	if (!id_is_nonzero(lease_id))
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	const ActiveJoinLease *lease = nullptr;
	for (const ActiveJoinLease &candidate : this->active_join_leases_)
		if (same_id(candidate.lease_id, lease_id))
		{
			lease = &candidate;
			break ;
		}
	if (lease == nullptr)
		return (FT_ERR_NOT_FOUND);
	const WorldSaveDefinition *entry = nullptr;
	for (const WorldSaveDefinition &candidate : this->entries_)
		if (same_id(candidate.world_id, lease->world_id))
		{
			entry = &candidate;
			break ;
		}
	if (entry == nullptr)
		return (FT_ERR_NOT_FOUND);
	for (const std::array<uint8_t, 32> &banned_id
		: entry->banned_character_ids)
		if (same_id(banned_id.data(), lease->character_id))
			return (FT_ERR_PERMISSION_DENIED);
	if (lease->generator_version < entry->minimum_generator_version
		|| lease->generator_version > entry->maximum_generator_version
		|| lease->ruleset_version < entry->minimum_ruleset_version
		|| lease->ruleset_version > entry->maximum_ruleset_version)
		return (FT_ERR_CONFIGURATION);
	if (!access_is_allowed(*entry, lease->actor_id,
			lease->host_reports_friend)
		|| !approval_is_allowed(*entry, lease->actor_id, lease->character_id))
		return (FT_ERR_PERMISSION_DENIED);
	uint32_t reserved_player_count = 0U;
	for (const ActiveJoinLease &candidate : this->active_join_leases_)
		if (same_id(candidate.world_id, lease->world_id))
			++reserved_player_count;
	if (reserved_player_count > entry->maximum_players)
		return (FT_ERR_FULL);
	const bool source_is_this_world = same_world_id(lease->world_id,
		lease->source_world_id);
	if (entry->character_admission == WORLD_SAVE_CHARACTER_WORLD_BOUND_ONLY
		&& (lease->character_mode != WORLD_SAVE_CHARACTER_WORLD_BOUND
			|| (!lease->character_is_new && !source_is_this_world)))
		return (FT_ERR_PERMISSION_DENIED);
	if (entry->character_admission == WORLD_SAVE_CHARACTER_PORTABLE_ALLOWED
		&& lease->character_mode == WORLD_SAVE_CHARACTER_WORLD_BOUND
		&& !lease->character_is_new && !source_is_this_world)
		return (FT_ERR_PERMISSION_DENIED);
	if (entry->character_admission == WORLD_SAVE_CHARACTER_FRESH_ONLY
		&& !lease->character_is_new)
		return (FT_ERR_PERMISSION_DENIED);
	return (FT_ERR_SUCCESS);
}

int32_t WorldSaveCatalog::save_locked(
	const std::vector<WorldSaveDefinition> &entries) const noexcept
{
	std::vector<uint8_t> encoded;
	if (this->catalog_path_.empty())
		return (FT_ERR_NOT_INITIALISED);
	const int32_t result = serialize(entries, encoded);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (file_replace_safe(this->catalog_path_.c_str(),
		reinterpret_cast<const char *>(encoded.data()), encoded.size()));
}
