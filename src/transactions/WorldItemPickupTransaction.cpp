#include "WorldItemPickupTransaction.hpp"

#include "../app/GameSessionPersistence.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace
{
	static const uint64_t WORLD_ITEM_PICKUP_SCOPE =
		0x574F524C44504943ULL;
	static const uint32_t WORLD_ITEM_PICKUP_TYPE = 1U;
	static const uint32_t WORLD_ITEM_PICKUP_PAYLOAD_VERSION = 1U;
	static const std::size_t WORLD_ITEM_PICKUP_PAYLOAD_SIZE = 84U;

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

	static bool decode(const GameTransactionJournal::Record &record,
		uint64_t *entity_id, uint64_t *entity_revision, uint64_t *actor_id,
		uint64_t *request_id, uint64_t *inventory_revision,
		int32_t *quantity, double *actor_x, double *actor_y, double *actor_z,
		uint64_t *server_tick) noexcept
	{
		std::size_t offset = 4U;
		uint32_t version;
		uint32_t raw_quantity;
		uint64_t x_bits;
		uint64_t y_bits;
		uint64_t z_bits;

		if (entity_id == nullptr || entity_revision == nullptr
			|| actor_id == nullptr || request_id == nullptr
			|| inventory_revision == nullptr || quantity == nullptr
			|| actor_x == nullptr || actor_y == nullptr || actor_z == nullptr
			|| server_tick == nullptr || record.scope_id != WORLD_ITEM_PICKUP_SCOPE
			|| record.transaction_type != WORLD_ITEM_PICKUP_TYPE
			|| record.payload.size() != WORLD_ITEM_PICKUP_PAYLOAD_SIZE
			|| record.payload[0] != 'G' || record.payload[1] != 'T'
			|| record.payload[2] != 'P' || record.payload[3] != 'K'
			|| !read_u32(record.payload, &offset, &version)
			|| version != WORLD_ITEM_PICKUP_PAYLOAD_VERSION
			|| !read_u64(record.payload, &offset, entity_id)
			|| !read_u64(record.payload, &offset, entity_revision)
			|| !read_u64(record.payload, &offset, actor_id)
			|| !read_u64(record.payload, &offset, request_id)
			|| !read_u64(record.payload, &offset, inventory_revision)
			|| !read_u32(record.payload, &offset, &raw_quantity)
			|| !read_u64(record.payload, &offset, &x_bits)
			|| !read_u64(record.payload, &offset, &y_bits)
			|| !read_u64(record.payload, &offset, &z_bits)
			|| !read_u64(record.payload, &offset, server_tick)
			|| offset != record.payload.size()
			|| *entity_id == 0U || *entity_revision == 0U || *actor_id == 0U
			|| *request_id != record.request_id || raw_quantity == 0U
			|| raw_quantity > static_cast<uint32_t>(
				std::numeric_limits<int32_t>::max()))
			return (false);
		*quantity = static_cast<int32_t>(raw_quantity);
		std::memcpy(actor_x, &x_bits, sizeof(x_bits));
		std::memcpy(actor_y, &y_bits, sizeof(y_bits));
		std::memcpy(actor_z, &z_bits, sizeof(z_bits));
		return (std::isfinite(*actor_x) && std::isfinite(*actor_y)
			&& std::isfinite(*actor_z));
	}

	static bool retryable_pickup_result(int32_t result) noexcept
	{
		return (result == FT_ERR_FULL || result == FT_ERR_INVALID_STATE
			|| result == FT_ERR_NOT_FOUND);
	}

	static int32_t apply_record(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal,
		const GameTransactionJournal::Record &record,
		WorldItemPickupResult *result_out,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept
	{
		uint64_t entity_id;
		uint64_t entity_revision;
		uint64_t actor_id;
		uint64_t request_id;
		uint64_t inventory_revision;
		uint64_t server_tick;
		int32_t quantity;
		double actor_x;
		double actor_y;
		double actor_z;
		int32_t pickup_result;
		int32_t error_code;

		if (snapshot_path == nullptr || snapshot_path[0] == '\0'
			|| result_out == nullptr || record.state
				!= GameTransactionJournal::PREPARED
			|| !decode(record, &entity_id, &entity_revision, &actor_id,
				&request_id, &inventory_revision, &quantity, &actor_x, &actor_y,
				&actor_z, &server_tick))
			return (FT_ERR_INVALID_ARGUMENT);
		*result_out = {};
		pickup_result = world_items.pickup(entity_id, entity_revision,
			actor_id, request_id, inventory_revision, quantity, actor_x, actor_y,
			actor_z, server_tick, character.get_inventory(), result_out);
		if (pickup_result == FT_ERR_SUCCESS)
			error_code = journal.mark_committed(record.scope_id, request_id);
		else
			error_code = journal.mark_aborted(record.scope_id, request_id);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (snapshot_writer != nullptr)
			error_code = snapshot_writer->save();
		else
			error_code = GameSessionPersistence::save_file(snapshot_path,
				character, world_items, recipe_catalog, journal);
		return (error_code == FT_ERR_SUCCESS ? pickup_result : error_code);
	}
}

uint64_t WorldItemPickupTransaction::journal_scope() noexcept
{
	return (WORLD_ITEM_PICKUP_SCOPE);
}

int32_t WorldItemPickupTransaction::next_request_id(
	const GameTransactionJournal &journal, uint64_t *request_id_out) noexcept
{
	return (journal.next_request_id(WORLD_ITEM_PICKUP_SCOPE,
		request_id_out));
}

int32_t WorldItemPickupTransaction::prepare(const char *snapshot_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal, const WorldItemSnapshot &item,
	uint64_t actor_id, uint64_t request_id,
	uint64_t expected_inventory_revision, double actor_x, double actor_y,
	double actor_z, uint64_t server_tick,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	std::vector<uint8_t> payload;
	uint64_t x_bits;
	uint64_t y_bits;
	uint64_t z_bits;
	uint64_t sequence;
	bool replayed;
	int32_t error_code;

	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| actor_id == 0U || request_id == 0U || item.entity_id == 0U
		|| item.entity_revision == 0U || item.quantity <= 0
		|| !std::isfinite(actor_x) || !std::isfinite(actor_y)
		|| !std::isfinite(actor_z))
		return (FT_ERR_INVALID_ARGUMENT);
	std::memcpy(&x_bits, &actor_x, sizeof(x_bits));
	std::memcpy(&y_bits, &actor_y, sizeof(y_bits));
	std::memcpy(&z_bits, &actor_z, sizeof(z_bits));
	try
	{
		payload.reserve(WORLD_ITEM_PICKUP_PAYLOAD_SIZE);
		payload.push_back('G');
		payload.push_back('T');
		payload.push_back('P');
		payload.push_back('K');
		append_u32(payload, WORLD_ITEM_PICKUP_PAYLOAD_VERSION);
		append_u64(payload, item.entity_id);
		append_u64(payload, item.entity_revision);
		append_u64(payload, actor_id);
		append_u64(payload, request_id);
		append_u64(payload, expected_inventory_revision);
		append_u32(payload, static_cast<uint32_t>(item.quantity));
		append_u64(payload, x_bits);
		append_u64(payload, y_bits);
		append_u64(payload, z_bits);
		append_u64(payload, server_tick);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (payload.size() != WORLD_ITEM_PICKUP_PAYLOAD_SIZE)
		return (FT_ERR_INVALID_STATE);
	error_code = journal.prepare(WORLD_ITEM_PICKUP_SCOPE, request_id,
		WORLD_ITEM_PICKUP_TYPE, payload.data(),
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
		(void)journal.mark_aborted(WORLD_ITEM_PICKUP_SCOPE, request_id);
	return (error_code);
}

int32_t WorldItemPickupTransaction::apply_prepared(
	const char *snapshot_path, game_character &character,
	WorldItemStore &world_items, const MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal,
	const GameTransactionJournal::Record &record,
	WorldItemPickupResult *result_out,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	return (apply_record(snapshot_path, character, world_items, recipe_catalog,
		journal, record, result_out, snapshot_writer));
}

int32_t WorldItemPickupTransaction::execute(const char *snapshot_path,
	game_character &character, WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal, const WorldItemSnapshot &item,
	uint64_t actor_id, uint64_t request_id,
	uint64_t expected_inventory_revision, double actor_x, double actor_y,
	double actor_z, uint64_t server_tick,
	WorldItemPickupResult *result_out,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	GameTransactionJournal::Record record = {};
	int32_t result;

	if (result_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*result_out = {};
	result = WorldItemPickupTransaction::prepare(snapshot_path, character,
		world_items, recipe_catalog, journal, item, actor_id, request_id,
		expected_inventory_revision, actor_x, actor_y, actor_z, server_tick,
		snapshot_writer);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = journal.find(WORLD_ITEM_PICKUP_SCOPE, request_id, &record);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (record.state == GameTransactionJournal::PREPARED)
		return (WorldItemPickupTransaction::apply_prepared(snapshot_path,
			character, world_items, recipe_catalog, journal, record, result_out,
			snapshot_writer));
	if (record.state != GameTransactionJournal::COMMITTED
		&& record.state != GameTransactionJournal::ABORTED)
		return (FT_ERR_INVALID_STATE);
	return (world_items.pickup(item.entity_id, item.entity_revision, actor_id,
		request_id, expected_inventory_revision, item.quantity, actor_x, actor_y,
		actor_z, server_tick, character.get_inventory(), result_out));
}

int32_t WorldItemPickupTransaction::recover_pending(
	const char *snapshot_path, game_character &character,
	WorldItemStore &world_items, const MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	std::vector<GameTransactionJournal::Record> pending;
	int32_t error_code = journal.pending_records(pending);

	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	for (const GameTransactionJournal::Record &record : pending)
	{
		WorldItemPickupResult result = {};
		int32_t pickup_result;

		if (record.scope_id != WORLD_ITEM_PICKUP_SCOPE)
			continue ;
		pickup_result = WorldItemPickupTransaction::apply_prepared(
			snapshot_path, character, world_items, recipe_catalog, journal,
			record, &result, snapshot_writer);

		if (pickup_result != FT_ERR_SUCCESS
			&& !retryable_pickup_result(pickup_result))
			return (pickup_result);
	}
	return (FT_ERR_SUCCESS);
}
