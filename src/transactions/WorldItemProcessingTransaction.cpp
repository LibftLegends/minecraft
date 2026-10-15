#include "WorldItemProcessingTransaction.hpp"

#include "../crafting/MinecraftRecipeCatalog.hpp"
#include "../entities/WorldItemStore.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <limits>
#include <new>
#include <vector>

namespace
{
	static const uint64_t PROCESSING_SCOPE = 0x574F524C44505243ULL;
	static const uint32_t PROCESSING_TYPE = 3U;
	static const uint32_t PROCESSING_VERSION = 2U;
	static const std::size_t PROCESSING_V1_PAYLOAD_SIZE = 32U;
	static const std::size_t PROCESSING_PAYLOAD_SIZE = 52U;

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

	static bool read_u32(const std::vector<uint8_t> &bytes,
		std::size_t *offset, uint32_t *value) noexcept
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

	static bool read_u64(const std::vector<uint8_t> &bytes,
		std::size_t *offset, uint64_t *value) noexcept
	{
		uint32_t low;
		uint32_t high;

		if (!read_u32(bytes, offset, &low)
			|| !read_u32(bytes, offset, &high))
			return (false);
		*value = static_cast<uint64_t>(low)
			| (static_cast<uint64_t>(high) << 32U);
		return (true);
	}

	static int32_t decode(const GameTransactionJournal::Record &record,
		uint32_t *recipe_id, int32_t *batch_count,
		uint64_t *expected_revision, int32_t *station_x,
		int32_t *station_y, int32_t *station_z,
		uint64_t *expected_station_revision) noexcept
	{
		std::size_t offset = 4U;
		uint32_t version;
		uint32_t raw_batch;
		uint64_t request_id;
		uint32_t raw_station_x = 0U;
		uint32_t raw_station_y = 0U;
		uint32_t raw_station_z = 0U;
		uint64_t station_revision = 0U;

		if (recipe_id == nullptr || batch_count == nullptr
			|| expected_revision == nullptr || station_x == nullptr
			|| station_y == nullptr || station_z == nullptr
			|| expected_station_revision == nullptr
			|| record.scope_id != PROCESSING_SCOPE
			|| record.transaction_type != PROCESSING_TYPE
			|| (record.payload.size() != PROCESSING_V1_PAYLOAD_SIZE
				&& record.payload.size() != PROCESSING_PAYLOAD_SIZE)
			|| record.payload[0] != 'G' || record.payload[1] != 'T'
			|| record.payload[2] != 'P' || record.payload[3] != 'R'
			|| !read_u32(record.payload, &offset, &version)
			|| (version != 1U && version != PROCESSING_VERSION)
			|| (version == 1U
				&& record.payload.size() != PROCESSING_V1_PAYLOAD_SIZE)
			|| (version == PROCESSING_VERSION
				&& record.payload.size() != PROCESSING_PAYLOAD_SIZE)
			|| !read_u64(record.payload, &offset, &request_id)
			|| !read_u32(record.payload, &offset, recipe_id)
			|| !read_u32(record.payload, &offset, &raw_batch)
			|| !read_u64(record.payload, &offset, expected_revision)
			|| (version == PROCESSING_VERSION
				&& (!read_u32(record.payload, &offset, &raw_station_x)
					|| !read_u32(record.payload, &offset, &raw_station_y)
					|| !read_u32(record.payload, &offset, &raw_station_z)
					|| !read_u64(record.payload, &offset, &station_revision)
					|| station_revision == 0U))
			|| offset != record.payload.size()
			|| request_id != record.request_id || *recipe_id == 0U
			|| raw_batch == 0U || raw_batch > static_cast<uint32_t>(
				std::numeric_limits<int32_t>::max()))
			return (FT_ERR_INVALID_ARGUMENT);
		*batch_count = static_cast<int32_t>(raw_batch);
		*station_x = static_cast<int32_t>(raw_station_x);
		*station_y = static_cast<int32_t>(raw_station_y);
		*station_z = static_cast<int32_t>(raw_station_z);
		*expected_station_revision = station_revision;
		return (FT_ERR_SUCCESS);
	}

	static int32_t apply_prepared(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal,
		const GameTransactionJournal::Record &record,
		MinecraftProcessingActionResult *result_out,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept
	{
		uint32_t recipe_id;
		int32_t batch_count;
		uint64_t expected_revision;
		int32_t station_x;
		int32_t station_y;
		int32_t station_z;
		uint64_t expected_station_revision;
		int32_t result;
		int32_t journal_result;

		if (snapshot_path == nullptr || snapshot_path[0] == '\0'
			|| result_out == nullptr
			|| record.state != GameTransactionJournal::PREPARED
			|| decode(record, &recipe_id, &batch_count, &expected_revision,
				&station_x, &station_y, &station_z,
				&expected_station_revision) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_ARGUMENT);
		result = recipe_catalog.start_processing(
			character.get_inventory(), record.request_id, recipe_id, batch_count,
			expected_revision, station_x, station_y, station_z,
			expected_station_revision, result_out);
		if (result == FT_ERR_SUCCESS)
			journal_result = journal.mark_committed(record.scope_id,
				record.request_id);
		else
			journal_result = journal.mark_aborted(record.scope_id,
				record.request_id);
		if (journal_result != FT_ERR_SUCCESS)
			return (journal_result);
		if (snapshot_writer != nullptr)
			journal_result = snapshot_writer->save();
		else
			journal_result = GameSessionPersistence::save_file(snapshot_path,
				character, world_items, recipe_catalog, journal);
		return (journal_result == FT_ERR_SUCCESS ? result : journal_result);
	}
}

uint64_t WorldItemProcessingTransaction::journal_scope() noexcept
{
	return (PROCESSING_SCOPE);
}

int32_t WorldItemProcessingTransaction::prepare(const char *snapshot_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal, uint64_t request_id,
	uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision, int32_t station_x,
	int32_t station_y, int32_t station_z,
	uint64_t expected_station_revision,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	std::vector<uint8_t> payload;
	uint64_t sequence;
	bool replayed;
	int32_t error_code;

	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| request_id == 0U || recipe_id == 0U || batch_count <= 0
		|| expected_station_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		payload.reserve(PROCESSING_PAYLOAD_SIZE);
		payload.push_back('G');
		payload.push_back('T');
		payload.push_back('P');
		payload.push_back('R');
		append_u32(payload, PROCESSING_VERSION);
		append_u64(payload, request_id);
		append_u32(payload, recipe_id);
		append_u32(payload, static_cast<uint32_t>(batch_count));
		append_u64(payload, expected_inventory_revision);
		append_u32(payload, static_cast<uint32_t>(station_x));
		append_u32(payload, static_cast<uint32_t>(station_y));
		append_u32(payload, static_cast<uint32_t>(station_z));
		append_u64(payload, expected_station_revision);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (payload.size() != PROCESSING_PAYLOAD_SIZE)
		return (FT_ERR_INVALID_STATE);
	error_code = journal.prepare(PROCESSING_SCOPE, request_id,
		PROCESSING_TYPE, payload.data(),
		static_cast<uint32_t>(payload.size()), &sequence, &replayed);
	(void)sequence;
	if (error_code != FT_ERR_SUCCESS || replayed)
		return (error_code);
	if (snapshot_writer != nullptr)
		error_code = snapshot_writer->save();
	else
		error_code = GameSessionPersistence::save_file(snapshot_path,
			character, world_items, recipe_catalog, journal);
	if (error_code != FT_ERR_SUCCESS)
		(void)journal.mark_aborted(PROCESSING_SCOPE, request_id);
	return (error_code);
}

int32_t WorldItemProcessingTransaction::execute(const char *snapshot_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog, GameTransactionJournal &journal,
	uint64_t request_id, uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision, int32_t station_x,
	int32_t station_y, int32_t station_z,
	uint64_t expected_station_revision,
	MinecraftProcessingActionResult *result_out,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	GameTransactionJournal::Record record = {};
	int32_t error_code;

	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| request_id == 0U || recipe_id == 0U || batch_count <= 0
		|| result_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*result_out = {};
	error_code = WorldItemProcessingTransaction::prepare(snapshot_path,
		character, world_items, recipe_catalog, journal, request_id,
		recipe_id, batch_count, expected_inventory_revision, station_x,
		station_y, station_z, expected_station_revision, snapshot_writer);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = journal.find(PROCESSING_SCOPE, request_id, &record);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (record.state == GameTransactionJournal::PREPARED)
		return (apply_prepared(snapshot_path, character, world_items,
			recipe_catalog, journal, record, result_out, snapshot_writer));
	if (record.state != GameTransactionJournal::COMMITTED
		&& record.state != GameTransactionJournal::ABORTED)
		return (FT_ERR_INVALID_STATE);
	return (recipe_catalog.start_processing(character.get_inventory(),
		request_id, recipe_id, batch_count, expected_inventory_revision,
		station_x, station_y, station_z, expected_station_revision,
		result_out));
}

int32_t WorldItemProcessingTransaction::recover_pending(
	const char *snapshot_path, game_character &character,
	WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	std::vector<GameTransactionJournal::Record> pending;
	int32_t error_code = journal.pending_records(pending);

	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	for (const GameTransactionJournal::Record &record : pending)
	{
		MinecraftProcessingActionResult result = {};

		if (record.scope_id != PROCESSING_SCOPE)
			continue ;
		error_code = apply_prepared(snapshot_path, character, world_items,
			recipe_catalog, journal, record, &result, snapshot_writer);
		if (error_code != FT_ERR_SUCCESS
			&& error_code != FT_ERR_INVALID_STATE
			&& error_code != FT_ERR_NOT_FOUND
			&& error_code != FT_ERR_FULL)
			return (error_code);
	}
	return (FT_ERR_SUCCESS);
}
