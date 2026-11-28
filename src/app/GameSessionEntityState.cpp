#include "GameSessionEntityState.hpp"

#include "../../Libft/Modules/Errno/errno.hpp"
#include <cstring>
#include <new>

namespace
{
	static const uint32_t GAME_SESSION_ENTITY_STATE_VERSION = 1U;

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

	static uint32_t read_u32(const uint8_t *bytes, std::size_t offset) noexcept
	{
		return (static_cast<uint32_t>(bytes[offset])
			| (static_cast<uint32_t>(bytes[offset + 1U]) << 8U)
			| (static_cast<uint32_t>(bytes[offset + 2U]) << 16U)
			| (static_cast<uint32_t>(bytes[offset + 3U]) << 24U));
	}

	static uint64_t read_u64(const uint8_t *bytes, std::size_t offset) noexcept
	{
		return (static_cast<uint64_t>(read_u32(bytes, offset))
			| (static_cast<uint64_t>(read_u32(bytes, offset + 4U)) << 32U));
	}

	static uint64_t checksum(const uint8_t *bytes, std::size_t byte_count) noexcept
	{
		uint64_t hash = 14695981039346656037ULL;
		for (std::size_t index = 0U; index < byte_count; ++index)
		{
			hash ^= bytes[index];
			hash *= 1099511628211ULL;
		}
		return (hash);
	}

	static int32_t validate_composite(const uint8_t *bytes,
		std::size_t byte_count) noexcept
	{
		if (bytes == nullptr || byte_count < 24U
			|| std::memcmp(bytes, "WEC1", 4U) != 0
			|| read_u32(bytes, 4U) != GAME_SESSION_ENTITY_STATE_VERSION
			|| read_u64(bytes, byte_count - 8U)
				!= checksum(bytes, byte_count - 8U))
			return (FT_ERR_INVALID_ARGUMENT);
		const uint32_t entity_size = read_u32(bytes, 8U);
		const uint32_t spawner_size = read_u32(bytes, 12U);
		if (static_cast<uint64_t>(entity_size) + spawner_size
			!= byte_count - 24U)
			return (FT_ERR_INVALID_ARGUMENT);
		WorldEntityStore entities;
		WorldSpawnerStore spawners;
		int32_t result = entities.initialize();
		if (result == FT_ERR_SUCCESS)
			result = spawners.initialize();
		if (result == FT_ERR_SUCCESS)
			result = entities.deserialize(bytes + 16U, entity_size);
		if (result == FT_ERR_SUCCESS)
			result = spawners.deserialize(bytes + 16U + entity_size,
				spawner_size);
		return (result);
	}
}

int32_t GameSessionEntityState::serialize(const WorldEntityStore &entities,
	const WorldSpawnerStore &spawners,
	std::vector<uint8_t> &bytes_out) noexcept
{
	std::vector<uint8_t> entity_bytes;
	std::vector<uint8_t> spawner_bytes;
	std::vector<uint8_t> staged;
	int32_t result = entities.serialize(entity_bytes);
	if (result == FT_ERR_SUCCESS)
		result = spawners.serialize(spawner_bytes);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (entity_bytes.size() > UINT32_MAX || spawner_bytes.size() > UINT32_MAX)
		return (FT_ERR_OUT_OF_RANGE);
	try
	{
		staged.insert(staged.end(), {'W', 'E', 'C', '1'});
		append_u32(staged, GAME_SESSION_ENTITY_STATE_VERSION);
		append_u32(staged, static_cast<uint32_t>(entity_bytes.size()));
		append_u32(staged, static_cast<uint32_t>(spawner_bytes.size()));
		staged.insert(staged.end(), entity_bytes.begin(), entity_bytes.end());
		staged.insert(staged.end(), spawner_bytes.begin(), spawner_bytes.end());
		append_u64(staged, checksum(staged.data(), staged.size()));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	bytes_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t GameSessionEntityState::restore(const uint8_t *bytes,
	std::size_t byte_count, WorldEntityStore &entities,
	WorldSpawnerStore &spawners) noexcept
{
	int32_t result;
	if (bytes == nullptr || byte_count < 4U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (std::memcmp(bytes, "WENS", 4U) == 0)
		return (entities.deserialize(bytes, byte_count));
	result = validate_composite(bytes, byte_count);
	if (result != FT_ERR_SUCCESS)
		return (result);
	const uint32_t entity_size = read_u32(bytes, 8U);
	const uint32_t spawner_size = read_u32(bytes, 12U);
	result = entities.deserialize(bytes + 16U, entity_size);
	if (result == FT_ERR_SUCCESS)
		result = spawners.deserialize(bytes + 16U + entity_size, spawner_size);
	return (result);
}

int32_t GameSessionEntityState::validate(const uint8_t *bytes,
	std::size_t byte_count) noexcept
{
	if (bytes == nullptr || byte_count < 4U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (std::memcmp(bytes, "WENS", 4U) == 0)
	{
		WorldEntityStore entities;
		int32_t result = entities.initialize();
		return (result == FT_ERR_SUCCESS
			? entities.deserialize(bytes, byte_count) : result);
	}
	return (validate_composite(bytes, byte_count));
}
