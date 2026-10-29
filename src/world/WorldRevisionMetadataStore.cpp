#include "../../src/world/WorldRevisionMetadataStore.hpp"
#include "../../Libft/Modules/File/file_utils.hpp"

#include <cstdio>
#include <limits>
#include <new>

static const uint32_t WORLD_REVISION_METADATA_MAGIC = 0x57525631U;
static const uint32_t WORLD_REVISION_METADATA_VERSION = 4U;
static const uint32_t WORLD_REVISION_METADATA_MAX_PROTECTED = 100000U;
static const uint64_t WORLD_REVISION_METADATA_FNV_OFFSET =
	14695981039346656037ULL;
static const uint64_t WORLD_REVISION_METADATA_FNV_PRIME =
	1099511628211ULL;

static void world_revision_metadata_append_u32(
	std::vector<uint8_t> &bytes, uint32_t value)
{
	for (uint32_t shift = 0U; shift < 32U; shift += 8U)
		bytes.push_back(static_cast<uint8_t>(value >> shift));
}

static void world_revision_metadata_append_u64(
	std::vector<uint8_t> &bytes, uint64_t value)
{
	for (uint32_t shift = 0U; shift < 64U; shift += 8U)
		bytes.push_back(static_cast<uint8_t>(value >> shift));
}

static uint64_t world_revision_metadata_hash(const uint8_t *bytes,
	std::size_t size)
{
	uint64_t hash = WORLD_REVISION_METADATA_FNV_OFFSET;
	for (std::size_t index = 0U; index < size; ++index)
	{
		hash ^= bytes[index];
		hash *= WORLD_REVISION_METADATA_FNV_PRIME;
	}
	return (hash);
}

static bool world_revision_metadata_read_u32(const std::vector<uint8_t> &bytes,
	std::size_t *offset, uint32_t *value)
{
	if (offset == nullptr || value == nullptr || *offset > bytes.size()
		|| bytes.size() - *offset < 4U)
		return (false);
	*value = static_cast<uint32_t>(bytes[*offset])
		| (static_cast<uint32_t>(bytes[*offset + 1U]) << 8U)
		| (static_cast<uint32_t>(bytes[*offset + 2U]) << 16U)
		| (static_cast<uint32_t>(bytes[*offset + 3U]) << 24U);
	*offset += 4U;
	return (true);
}

static bool world_revision_metadata_read_u64(const std::vector<uint8_t> &bytes,
	std::size_t *offset, uint64_t *value)
{
	uint32_t low;
	uint32_t high;

	if (!world_revision_metadata_read_u32(bytes, offset, &low)
		|| !world_revision_metadata_read_u32(bytes, offset, &high))
		return (false);
	*value = static_cast<uint64_t>(low)
		| (static_cast<uint64_t>(high) << 32U);
	return (true);
}

static bool world_revision_metadata_policy_is_valid(
	const WorldRevisionCostPolicy &policy, uint64_t policy_revision,
	bool configured)
{
	if (!configured)
		return (policy_revision == 0U
			&& policy.maximum_selected_chunks == 0U
			&& policy.currency_item_id == 0U
			&& policy.currency_units_per_chunk == 0U
			&& policy.four_card_opening_cost == 0U
			&& policy.maximum_total_currency_cost == 0U);
	return (policy_revision > 0U && policy.maximum_selected_chunks > 0U
		&& policy.maximum_selected_chunks
			<= WorldRevisionManager::HARD_MAX_SELECTED_CHUNKS
		&& policy.currency_item_id != 0U
		&& policy.maximum_total_currency_cost != 0U);
}

static int32_t world_revision_metadata_floor_div(int32_t value,
	int32_t divisor)
{
	int32_t quotient = value / divisor;

	if (value < 0 && value % divisor != 0)
		--quotient;
	return (quotient);
}

WorldRevisionMetadataStore::WorldRevisionMetadataStore()
{
}

WorldRevisionMetadataStore::WorldRevisionMetadataStore(
	const WorldRevisionMetadataStore &other)
{
	(void)other;
}

WorldRevisionMetadataStore::~WorldRevisionMetadataStore()
{
}

WorldRevisionMetadataStore &WorldRevisionMetadataStore::operator=(
	const WorldRevisionMetadataStore &other)
{
	(void)other;
	return (*this);
}

int32_t WorldRevisionMetadataStore::serialize(uint32_t revision_id,
	const std::vector<WorldRevisionManager::RevisionChunk> &manual_protected,
	const WorldRevisionCostPolicy &cost_policy,
	uint64_t cost_policy_revision, bool cost_policy_configured,
	bool station_configured, int32_t station_chunk_x,
	int32_t station_chunk_z, bool station_block_configured,
	int32_t station_block_x, int32_t station_block_y,
	int32_t station_block_z, uint32_t station_block_id,
	std::vector<uint8_t> &bytes_out) noexcept
{
	std::vector<uint8_t> bytes;
	uint64_t checksum;

	if (manual_protected.size() > WORLD_REVISION_METADATA_MAX_PROTECTED
		|| (!station_configured
			&& (station_chunk_x != 0 || station_chunk_z != 0))
		|| (!station_block_configured
			&& (station_block_x != 0 || station_block_y != 0
				|| station_block_z != 0 || station_block_id != 0U))
		|| (station_block_configured
			&& (!station_configured || station_block_id == 0U
				|| station_block_y < 0
				|| station_block_y >= GAME_VOXEL_CHUNK_HEIGHT
				|| station_chunk_x != world_revision_metadata_floor_div(
					station_block_x, GAME_VOXEL_CHUNK_WIDTH)
				|| station_chunk_z != world_revision_metadata_floor_div(
					station_block_z, GAME_VOXEL_CHUNK_DEPTH)))
		|| !world_revision_metadata_policy_is_valid(cost_policy,
			cost_policy_revision, cost_policy_configured))
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		world_revision_metadata_append_u32(bytes,
			WORLD_REVISION_METADATA_MAGIC);
		world_revision_metadata_append_u32(bytes,
			WORLD_REVISION_METADATA_VERSION);
		world_revision_metadata_append_u32(bytes, revision_id);
		world_revision_metadata_append_u32(bytes,
			cost_policy_configured ? 1U : 0U);
		world_revision_metadata_append_u64(bytes, cost_policy_revision);
		world_revision_metadata_append_u32(bytes,
			cost_policy.maximum_selected_chunks);
		world_revision_metadata_append_u32(bytes, cost_policy.currency_item_id);
		world_revision_metadata_append_u64(bytes,
			cost_policy.currency_units_per_chunk);
		world_revision_metadata_append_u64(bytes,
			cost_policy.four_card_opening_cost);
		world_revision_metadata_append_u64(bytes,
			cost_policy.maximum_total_currency_cost);
		world_revision_metadata_append_u32(bytes,
			station_configured ? 1U : 0U);
		world_revision_metadata_append_u32(bytes,
			static_cast<uint32_t>(station_chunk_x));
		world_revision_metadata_append_u32(bytes,
			static_cast<uint32_t>(station_chunk_z));
		world_revision_metadata_append_u32(bytes,
			station_block_configured ? 1U : 0U);
		world_revision_metadata_append_u32(bytes,
			static_cast<uint32_t>(station_block_x));
		world_revision_metadata_append_u32(bytes,
			static_cast<uint32_t>(station_block_y));
		world_revision_metadata_append_u32(bytes,
			static_cast<uint32_t>(station_block_z));
		world_revision_metadata_append_u32(bytes, station_block_id);
		world_revision_metadata_append_u32(bytes,
			static_cast<uint32_t>(manual_protected.size()));
		for (const WorldRevisionManager::RevisionChunk &entry : manual_protected)
		{
			world_revision_metadata_append_u32(bytes,
				static_cast<uint32_t>(entry.chunk_x));
			world_revision_metadata_append_u32(bytes,
				static_cast<uint32_t>(entry.chunk_z));
		}
		checksum = world_revision_metadata_hash(bytes.data(), bytes.size());
		world_revision_metadata_append_u64(bytes, checksum);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	bytes_out.swap(bytes);
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionMetadataStore::save(const char *file_path,
	uint32_t revision_id,
	const std::vector<WorldRevisionManager::RevisionChunk> &manual_protected,
	const WorldRevisionCostPolicy &cost_policy,
	uint64_t cost_policy_revision, bool cost_policy_configured,
	bool station_configured, int32_t station_chunk_x,
	int32_t station_chunk_z, bool station_block_configured,
	int32_t station_block_x, int32_t station_block_y,
	int32_t station_block_z, uint32_t station_block_id) noexcept
{
	std::vector<uint8_t> bytes;
	int32_t result;

	if (file_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	result = serialize(revision_id, manual_protected, cost_policy,
		cost_policy_revision, cost_policy_configured, station_configured,
		station_chunk_x, station_chunk_z, station_block_configured,
		station_block_x, station_block_y, station_block_z, station_block_id,
		bytes);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (file_replace_safe(file_path,
		reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}

int32_t WorldRevisionMetadataStore::load(const char *file_path,
	uint32_t &revision_id,
	std::vector<WorldRevisionManager::RevisionChunk> &manual_protected,
	WorldRevisionCostPolicy &cost_policy, uint64_t &cost_policy_revision,
	bool &cost_policy_configured, bool &cost_policy_stored,
	bool &station_configured, int32_t &station_chunk_x,
	int32_t &station_chunk_z, bool &station_stored,
	bool &station_block_configured, int32_t &station_block_x,
	int32_t &station_block_y, int32_t &station_block_z,
	uint32_t &station_block_id) noexcept
{
	FILE *file;
	long file_size;
	std::vector<uint8_t> bytes;

	if (file_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	file = std::fopen(file_path, "rb");
	if (file == nullptr)
		return (FT_ERR_IO);
	if (std::fseek(file, 0, SEEK_END) != 0
		|| (file_size = std::ftell(file)) < 0
		|| file_size > 1024L * 1024L
		|| std::fseek(file, 0, SEEK_SET) != 0)
	{
		std::fclose(file);
		return (FT_ERR_IO);
	}
	try
	{
		bytes.resize(static_cast<std::size_t>(file_size));
		if (std::fread(bytes.data(), 1U, bytes.size(), file) != bytes.size())
		{
			std::fclose(file);
			return (FT_ERR_IO);
		}
	}
	catch (...)
	{
		std::fclose(file);
		return (FT_ERR_NO_MEMORY);
	}
	if (std::fclose(file) != 0)
		return (FT_ERR_IO);
	return (deserialize(bytes, revision_id, manual_protected, cost_policy,
		cost_policy_revision, cost_policy_configured, cost_policy_stored,
		station_configured, station_chunk_x, station_chunk_z, station_stored,
		station_block_configured, station_block_x, station_block_y,
		station_block_z, station_block_id));
}

int32_t WorldRevisionMetadataStore::deserialize(
	const std::vector<uint8_t> &bytes,
	uint32_t &revision_id,
	std::vector<WorldRevisionManager::RevisionChunk> &manual_protected,
	WorldRevisionCostPolicy &cost_policy, uint64_t &cost_policy_revision,
	bool &cost_policy_configured, bool &cost_policy_stored,
	bool &station_configured, int32_t &station_chunk_x,
	int32_t &station_chunk_z, bool &station_stored,
	bool &station_block_configured, int32_t &station_block_x,
	int32_t &station_block_y, int32_t &station_block_z,
	uint32_t &station_block_id) noexcept
{
	std::vector<WorldRevisionManager::RevisionChunk> staged_protected;
	WorldRevisionCostPolicy staged_policy = {};
	uint64_t staged_policy_revision = 0U;
	uint32_t staged_revision_id = 0U;
	uint32_t magic;
	uint32_t version;
	uint32_t manual_count;
	uint32_t configured_value = 0U;
	uint32_t station_configured_value = 0U;
	uint32_t station_x_value = 0U;
	uint32_t station_z_value = 0U;
	uint32_t station_block_configured_value = 0U;
	uint32_t station_block_x_value = 0U;
	uint32_t station_block_y_value = 0U;
	uint32_t station_block_z_value = 0U;
	uint32_t station_block_id_value = 0U;
	std::size_t offset = 0U;
	std::size_t expected_payload_size;
	uint64_t stored_checksum;
	WorldRevisionManager::RevisionChunk entry = {};
	bool staged_policy_configured = false;
	bool staged_policy_stored = false;
	bool staged_station_configured = false;
	bool staged_station_stored = false;

	if (bytes.size() > 1024U * 1024U)
		return (FT_ERR_OUT_OF_RANGE);
	if (!world_revision_metadata_read_u32(bytes, &offset, &magic)
		|| !world_revision_metadata_read_u32(bytes, &offset, &version)
		|| !world_revision_metadata_read_u32(bytes, &offset,
			&staged_revision_id)
		|| magic != WORLD_REVISION_METADATA_MAGIC)
		return (FT_ERR_INVALID_ARGUMENT);
	if (version == 1U)
	{
		if (!world_revision_metadata_read_u32(bytes, &offset, &manual_count)
			|| manual_count > WORLD_REVISION_METADATA_MAX_PROTECTED
			|| bytes.size() - offset
				!= static_cast<std::size_t>(manual_count) * 8U)
			return (FT_ERR_INVALID_ARGUMENT);
		staged_policy_stored = false;
	}
	else if (version >= 2U && version <= WORLD_REVISION_METADATA_VERSION)
	{
		if (!world_revision_metadata_read_u32(bytes, &offset,
				&configured_value)
			|| configured_value > 1U
			|| !world_revision_metadata_read_u64(bytes, &offset,
				&staged_policy_revision)
			|| !world_revision_metadata_read_u32(bytes, &offset,
				&staged_policy.maximum_selected_chunks)
			|| !world_revision_metadata_read_u32(bytes, &offset,
				&staged_policy.currency_item_id)
			|| !world_revision_metadata_read_u64(bytes, &offset,
				&staged_policy.currency_units_per_chunk)
			|| !world_revision_metadata_read_u64(bytes, &offset,
				&staged_policy.four_card_opening_cost)
			|| !world_revision_metadata_read_u64(bytes, &offset,
				&staged_policy.maximum_total_currency_cost)
			|| (version >= 3U
				&& (!world_revision_metadata_read_u32(bytes, &offset,
					&station_configured_value)
					|| station_configured_value > 1U
					|| !world_revision_metadata_read_u32(bytes, &offset,
						&station_x_value)
					|| !world_revision_metadata_read_u32(bytes, &offset,
						&station_z_value)))
			|| (version >= 4U
				&& (!world_revision_metadata_read_u32(bytes, &offset,
					&station_block_configured_value)
					|| station_block_configured_value > 1U
					|| !world_revision_metadata_read_u32(bytes, &offset,
						&station_block_x_value)
					|| !world_revision_metadata_read_u32(bytes, &offset,
						&station_block_y_value)
					|| !world_revision_metadata_read_u32(bytes, &offset,
						&station_block_z_value)
					|| !world_revision_metadata_read_u32(bytes, &offset,
						&station_block_id_value)))
			|| !world_revision_metadata_read_u32(bytes, &offset, &manual_count)
			|| manual_count > WORLD_REVISION_METADATA_MAX_PROTECTED)
			return (FT_ERR_INVALID_ARGUMENT);
		staged_policy_configured = configured_value == 1U;
		staged_policy_stored = true;
		staged_station_configured = station_configured_value == 1U;
		staged_station_stored = version >= 3U;
		if ((station_block_configured_value == 0U
				&& (station_block_x_value != 0U || station_block_y_value != 0U
					|| station_block_z_value != 0U
					|| station_block_id_value != 0U))
			|| (station_block_configured_value == 1U
				&& (station_configured_value != 1U
					|| station_block_id_value == 0U
					|| station_block_y_value >= GAME_VOXEL_CHUNK_HEIGHT
					|| static_cast<int32_t>(station_x_value)
						!= world_revision_metadata_floor_div(
							static_cast<int32_t>(station_block_x_value),
							GAME_VOXEL_CHUNK_WIDTH)
					|| static_cast<int32_t>(station_z_value)
						!= world_revision_metadata_floor_div(
							static_cast<int32_t>(station_block_z_value),
							GAME_VOXEL_CHUNK_DEPTH))))
			return (FT_ERR_INVALID_ARGUMENT);
		if (!staged_station_configured
			&& (station_x_value != 0U || station_z_value != 0U))
			return (FT_ERR_INVALID_ARGUMENT);
		expected_payload_size = offset
			+ static_cast<std::size_t>(manual_count) * 8U + 8U;
		if (expected_payload_size != bytes.size()
			|| !world_revision_metadata_policy_is_valid(staged_policy,
				staged_policy_revision, staged_policy_configured))
			return (FT_ERR_INVALID_ARGUMENT);
	}
	else
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		staged_protected.reserve(manual_count);
		for (uint32_t index = 0U; index < manual_count; ++index)
		{
			uint32_t x_value;
			uint32_t z_value;

			if (!world_revision_metadata_read_u32(bytes, &offset, &x_value)
				|| !world_revision_metadata_read_u32(bytes, &offset, &z_value))
				return (FT_ERR_INVALID_ARGUMENT);
			entry.chunk_x = static_cast<int32_t>(x_value);
			entry.chunk_z = static_cast<int32_t>(z_value);
			staged_protected.push_back(entry);
		}
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (version >= 2U)
	{
		if (!world_revision_metadata_read_u64(bytes, &offset, &stored_checksum)
			|| offset != bytes.size()
			|| stored_checksum != world_revision_metadata_hash(bytes.data(),
				bytes.size() - 8U))
			return (FT_ERR_INVALID_ARGUMENT);
	}
	else if (offset != bytes.size())
		return (FT_ERR_INVALID_ARGUMENT);
	revision_id = staged_revision_id;
	manual_protected.swap(staged_protected);
	cost_policy = staged_policy;
	cost_policy_revision = staged_policy_revision;
	cost_policy_configured = staged_policy_configured;
	cost_policy_stored = staged_policy_stored;
	station_configured = staged_station_configured;
	station_chunk_x = static_cast<int32_t>(station_x_value);
	station_chunk_z = static_cast<int32_t>(station_z_value);
	station_stored = staged_station_stored;
	station_block_configured = station_block_configured_value == 1U;
	station_block_x = static_cast<int32_t>(station_block_x_value);
	station_block_y = static_cast<int32_t>(station_block_y_value);
	station_block_z = static_cast<int32_t>(station_block_z_value);
	station_block_id = station_block_id_value;
	return (FT_ERR_SUCCESS);
}
