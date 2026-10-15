#include "WorldItemCraftTransaction.hpp"

#include "../crafting/MinecraftRecipeCatalog.hpp"
#include "../entities/WorldItemStore.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <limits>
#include <new>
#include <vector>

namespace
{
	static const uint64_t WORLD_ITEM_CRAFT_SCOPE = 0x574F524C44435246ULL;
	static const uint32_t WORLD_ITEM_CRAFT_TYPE = 2U;
	static const uint32_t WORLD_ITEM_CRAFT_VERSION = 2U;
	static const std::size_t WORLD_ITEM_CRAFT_V1_PAYLOAD_SIZE = 32U;
	static const std::size_t WORLD_ITEM_CRAFT_PAYLOAD_SIZE = 56U;

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
		uint64_t *expected_revision, uint32_t *station_id,
		int32_t *station_x, int32_t *station_y, int32_t *station_z,
		uint64_t *expected_station_revision) noexcept
	{
		std::size_t offset = 4U;
		uint32_t version;
		uint32_t raw_batch;
		uint32_t raw_x = 0U, raw_y = 0U, raw_z = 0U;
		uint64_t request_id;

		if (recipe_id == nullptr || batch_count == nullptr
			|| expected_revision == nullptr || station_id == nullptr
			|| station_x == nullptr || station_y == nullptr || station_z == nullptr
			|| expected_station_revision == nullptr
			|| record.scope_id != WORLD_ITEM_CRAFT_SCOPE
			|| record.transaction_type != WORLD_ITEM_CRAFT_TYPE
			|| (record.payload.size() != WORLD_ITEM_CRAFT_V1_PAYLOAD_SIZE
				&& record.payload.size() != WORLD_ITEM_CRAFT_PAYLOAD_SIZE)
			|| record.payload[0] != 'G' || record.payload[1] != 'T'
			|| record.payload[2] != 'C' || record.payload[3] != 'R'
			|| !read_u32(record.payload, &offset, &version)
			|| (version != 1U && version != WORLD_ITEM_CRAFT_VERSION)
			|| !read_u64(record.payload, &offset, &request_id)
			|| !read_u32(record.payload, &offset, recipe_id)
			|| !read_u32(record.payload, &offset, &raw_batch)
			|| !read_u64(record.payload, &offset, expected_revision)
			|| request_id != record.request_id || *recipe_id == 0U
			|| raw_batch == 0U || raw_batch > static_cast<uint32_t>(
				std::numeric_limits<int32_t>::max()))
			return (FT_ERR_INVALID_ARGUMENT);
		*station_id = 0U;
		*station_x = 0;
		*station_y = 0;
		*station_z = 0;
		*expected_station_revision = 0U;
		if (version == WORLD_ITEM_CRAFT_VERSION
			&& (!read_u32(record.payload, &offset, station_id)
				|| !read_u32(record.payload, &offset, &raw_x)
				|| !read_u32(record.payload, &offset, &raw_y)
				|| !read_u32(record.payload, &offset, &raw_z)
				|| !read_u64(record.payload, &offset,
					expected_station_revision)
				|| *station_id > static_cast<uint32_t>(
					MinecraftRecipeCatalog::FURNACE_STATION_ID)
				|| (*station_id != static_cast<uint32_t>(
					MinecraftRecipeCatalog::HAND_STATION_ID)
					&& *expected_station_revision == 0U)))
			return (FT_ERR_INVALID_ARGUMENT);
		*station_x = static_cast<int32_t>(raw_x);
		*station_y = static_cast<int32_t>(raw_y);
		*station_z = static_cast<int32_t>(raw_z);
		if (offset != record.payload.size())
			return (FT_ERR_INVALID_ARGUMENT);
		*batch_count = static_cast<int32_t>(raw_batch);
		return (FT_ERR_SUCCESS);
	}

	static int32_t synchronize_output_instance_allocator(
		const game_character &character, const WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog) noexcept
	{
		game_inventory_snapshot inventory = {};
		uint64_t maximum_instance_id = 0U;
		int32_t error_code = world_items.get_max_item_instance_id(
			&maximum_instance_id);

		if (error_code == FT_ERR_SUCCESS)
			error_code = character.get_inventory().get_snapshot(inventory);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		for (const game_inventory_item_snapshot &item : inventory.items)
		{
			if (item.instance_id > maximum_instance_id)
				maximum_instance_id = item.instance_id;
		}
		return (recipe_catalog.ensure_next_output_instance_id_above(
			maximum_instance_id));
	}

	static int32_t apply_prepared(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal,
		const GameTransactionJournal::Record &record,
		MinecraftCraftActionResult *result_out,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept
	{
		uint32_t recipe_id;
		int32_t batch_count;
		uint64_t expected_revision;
		uint32_t station_id;
		int32_t station_x, station_y, station_z;
		uint64_t expected_station_revision;
		int32_t result;
		int32_t error_code;

		if (snapshot_path == nullptr || snapshot_path[0] == '\0'
			|| result_out == nullptr
			|| record.state != GameTransactionJournal::PREPARED
			|| decode(record, &recipe_id, &batch_count,
				&expected_revision, &station_id, &station_x, &station_y,
				&station_z, &expected_station_revision) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_ARGUMENT);
		result = synchronize_output_instance_allocator(character, world_items,
			recipe_catalog);
		if (result != FT_ERR_SUCCESS && result != FT_ERR_OUT_OF_RANGE)
			return (result);
		result = recipe_catalog.craft_recipe_at_station(
			character.get_inventory(), record.request_id, recipe_id, batch_count,
			expected_revision,
			static_cast<MinecraftRecipeCatalog::StationId>(station_id),
			station_x, station_y, station_z, expected_station_revision,
			result_out);
		if (result == FT_ERR_SUCCESS)
			error_code = journal.mark_committed(record.scope_id,
				record.request_id);
		else
			error_code = journal.mark_aborted(record.scope_id,
				record.request_id);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (snapshot_writer != nullptr)
			error_code = snapshot_writer->save();
		else
			error_code = GameSessionPersistence::save_file(snapshot_path,
				character, world_items, recipe_catalog, journal);
		return (error_code == FT_ERR_SUCCESS ? result : error_code);
	}
}

uint64_t WorldItemCraftTransaction::journal_scope() noexcept
{
	return (WORLD_ITEM_CRAFT_SCOPE);
}

int32_t WorldItemCraftTransaction::prepare(const char *snapshot_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal, uint64_t request_id,
	uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision,
	uint32_t station_id, int32_t station_x, int32_t station_y,
	int32_t station_z, uint64_t expected_station_revision,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	std::vector<uint8_t> payload;
	uint64_t sequence;
	bool replayed;
	int32_t error_code;

	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| request_id == 0U || recipe_id == 0U || batch_count <= 0
		|| station_id > static_cast<uint32_t>(
			MinecraftRecipeCatalog::FURNACE_STATION_ID)
		|| (station_id == static_cast<uint32_t>(
			MinecraftRecipeCatalog::HAND_STATION_ID)
			&& (station_x != 0 || station_y != 0 || station_z != 0
				|| expected_station_revision != 0U))
		|| (station_id != static_cast<uint32_t>(
			MinecraftRecipeCatalog::HAND_STATION_ID)
			&& expected_station_revision == 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		payload.reserve(WORLD_ITEM_CRAFT_PAYLOAD_SIZE);
		payload.push_back('G');
		payload.push_back('T');
		payload.push_back('C');
		payload.push_back('R');
		append_u32(payload, WORLD_ITEM_CRAFT_VERSION);
		append_u64(payload, request_id);
		append_u32(payload, recipe_id);
		append_u32(payload, static_cast<uint32_t>(batch_count));
		append_u64(payload, expected_inventory_revision);
		append_u32(payload, station_id);
		append_u32(payload, static_cast<uint32_t>(station_x));
		append_u32(payload, static_cast<uint32_t>(station_y));
		append_u32(payload, static_cast<uint32_t>(station_z));
		append_u64(payload, expected_station_revision);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (payload.size() != WORLD_ITEM_CRAFT_PAYLOAD_SIZE)
		return (FT_ERR_INVALID_STATE);
	error_code = journal.prepare(WORLD_ITEM_CRAFT_SCOPE, request_id,
		WORLD_ITEM_CRAFT_TYPE, payload.data(),
		static_cast<uint32_t>(payload.size()), &sequence, &replayed);
	(void)sequence;
	if (error_code != FT_ERR_SUCCESS || replayed)
		return (error_code);
	if (snapshot_writer != nullptr)
		error_code = snapshot_writer->save();
	else
		error_code = GameSessionPersistence::save_file(snapshot_path, character,
			world_items, recipe_catalog, journal);
	if (error_code != FT_ERR_SUCCESS)
		(void)journal.mark_aborted(WORLD_ITEM_CRAFT_SCOPE, request_id);
	return (error_code);
}

int32_t WorldItemCraftTransaction::execute(const char *snapshot_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog, GameTransactionJournal &journal,
	uint64_t request_id, uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision,
	uint32_t station_id, int32_t station_x, int32_t station_y,
	int32_t station_z, uint64_t expected_station_revision,
	MinecraftCraftActionResult *result_out,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	GameTransactionJournal::Record record = {};
	int32_t error_code;

	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| request_id == 0U || recipe_id == 0U || batch_count <= 0
		|| result_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*result_out = {};
	error_code = WorldItemCraftTransaction::prepare(snapshot_path, character,
		world_items, recipe_catalog, journal, request_id, recipe_id,
		batch_count, expected_inventory_revision, station_id, station_x,
		station_y, station_z, expected_station_revision, snapshot_writer);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = journal.find(WORLD_ITEM_CRAFT_SCOPE, request_id, &record);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (record.state == GameTransactionJournal::PREPARED)
		return (apply_prepared(snapshot_path, character, world_items,
			recipe_catalog, journal, record, result_out, snapshot_writer));
	if (record.state != GameTransactionJournal::COMMITTED
		&& record.state != GameTransactionJournal::ABORTED)
		return (FT_ERR_INVALID_STATE);
	return (recipe_catalog.craft_recipe_at_station(character.get_inventory(),
		request_id, recipe_id, batch_count, expected_inventory_revision,
		static_cast<MinecraftRecipeCatalog::StationId>(station_id),
		station_x, station_y, station_z, expected_station_revision, result_out));
}

int32_t WorldItemCraftTransaction::recover_pending(
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
		MinecraftCraftActionResult result = {};

		if (record.scope_id != WORLD_ITEM_CRAFT_SCOPE)
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
