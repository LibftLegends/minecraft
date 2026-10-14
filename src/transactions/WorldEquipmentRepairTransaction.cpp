#include "WorldEquipmentRepairTransaction.hpp"

#include "../../Libft/Modules/Game/game_inventory.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <algorithm>
#include <climits>
#include <new>
#include <vector>

namespace
{
	static const uint64_t REPAIR_SCOPE = 0x4551554950524550ULL;
	static const uint32_t REPAIR_TYPE = 11U;
	static const uint32_t REPAIR_VERSION = 1U;
	static const std::size_t REPAIR_PAYLOAD_SIZE = 68U;

	static void append_u32(std::vector<uint8_t> &bytes, uint32_t value)
	{
		for (uint32_t shift = 0U; shift < 32U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}
	static void append_u64(std::vector<uint8_t> &bytes, uint64_t value)
	{
		append_u32(bytes, static_cast<uint32_t>(value));
		append_u32(bytes, static_cast<uint32_t>(value >> 32U));
	}
	static uint32_t read_u32(const std::vector<uint8_t> &bytes,
		std::size_t offset) noexcept
	{
		return (static_cast<uint32_t>(bytes[offset])
			| (static_cast<uint32_t>(bytes[offset + 1U]) << 8U)
			| (static_cast<uint32_t>(bytes[offset + 2U]) << 16U)
			| (static_cast<uint32_t>(bytes[offset + 3U]) << 24U));
	}
	static uint64_t read_u64(const std::vector<uint8_t> &bytes,
		std::size_t offset) noexcept
	{
		return (static_cast<uint64_t>(read_u32(bytes, offset))
			| (static_cast<uint64_t>(read_u32(bytes, offset + 4U)) << 32U));
	}
	static int32_t validate(const GameTransactionJournal::Record &record)
	{
		if (record.scope_id != REPAIR_SCOPE
			|| record.transaction_type != REPAIR_TYPE || record.request_id == 0U
			|| record.payload.size() != REPAIR_PAYLOAD_SIZE
			|| record.payload[0] != 'G' || record.payload[1] != 'R'
			|| record.payload[2] != 'P' || record.payload[3] != 'R'
			|| read_u32(record.payload, 4U) != REPAIR_VERSION
			|| read_u64(record.payload, 8U) != record.request_id
			|| read_u32(record.payload, 16U) > INT32_MAX
			|| read_u32(record.payload, 20U) == 0U
			|| read_u32(record.payload, 20U) > INT32_MAX
			|| read_u64(record.payload, 24U) == 0U
			|| read_u64(record.payload, 32U) == 0U
			|| read_u32(record.payload, 40U) == 0U
			|| read_u32(record.payload, 40U) > INT32_MAX
			|| read_u32(record.payload, 44U) == 0U
			|| read_u32(record.payload, 44U) > INT32_MAX
			|| read_u32(record.payload, 52U) >= GAME_VOXEL_CHUNK_HEIGHT
			|| read_u64(record.payload, 60U) == 0U)
			return (FT_ERR_INVALID_ARGUMENT);
		return (FT_ERR_SUCCESS);
	}
	static int32_t make_payload(uint64_t request_id, int32_t slot,
		int32_t item_id, uint64_t instance_id, uint64_t inventory_revision,
		int32_t material_id, int32_t material_quantity, int32_t station_x,
		int32_t station_y, int32_t station_z,
		uint64_t station_revision,
		std::vector<uint8_t> &payload) noexcept
	{
		if (request_id == 0U || slot < 0 || item_id <= 0 || instance_id == 0U
			|| inventory_revision == 0U || material_id <= 0
			|| material_quantity <= 0 || station_revision == 0U
			|| station_y < 0 || station_y >= GAME_VOXEL_CHUNK_HEIGHT)
			return (FT_ERR_INVALID_ARGUMENT);
		try
		{
			payload.reserve(REPAIR_PAYLOAD_SIZE);
			payload.insert(payload.end(), {'G', 'R', 'P', 'R'});
			append_u32(payload, REPAIR_VERSION);
			append_u64(payload, request_id);
			append_u32(payload, static_cast<uint32_t>(slot));
			append_u32(payload, static_cast<uint32_t>(item_id));
			append_u64(payload, instance_id);
			append_u64(payload, inventory_revision);
			append_u32(payload, static_cast<uint32_t>(material_id));
			append_u32(payload, static_cast<uint32_t>(material_quantity));
			append_u32(payload, static_cast<uint32_t>(station_x));
			append_u32(payload, static_cast<uint32_t>(station_y));
			append_u32(payload, static_cast<uint32_t>(station_z));
			append_u64(payload, station_revision);
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		return (payload.size() == REPAIR_PAYLOAD_SIZE
			? FT_ERR_SUCCESS : FT_ERR_INVALID_STATE);
	}
}

uint64_t WorldEquipmentRepairTransaction::journal_scope() noexcept
{
	return (REPAIR_SCOPE);
}

uint32_t WorldEquipmentRepairTransaction::transaction_type() noexcept
{
	return (REPAIR_TYPE);
}

int32_t WorldEquipmentRepairTransaction::execute(const char *snapshot_path,
	game_character &character, MinecraftItemCatalog &catalog,
	GameTransactionJournal &journal, uint64_t request_id, int32_t slot,
	uint64_t expected_revision, int32_t station_x, int32_t station_y,
	int32_t station_z, uint64_t expected_station_revision,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	game_inventory_snapshot before = {};
	game_inventory_snapshot after = {};
	GameTransactionJournal::Record existing = {};
	std::vector<uint8_t> payload;
	std::vector<uint8_t> journal_before;
	std::vector<uint8_t> journal_prepared;
	uint64_t sequence = 0U;
	bool replayed = false;
	int32_t material_id = 0;
	int32_t material_quantity = 0;
	int32_t item_id = 0;
	uint64_t instance_id = 0U;
	if (request_id == 0U || slot < 0 || expected_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	int32_t result = journal.find(REPAIR_SCOPE, request_id, &existing);
	if (result == FT_ERR_SUCCESS)
	{
		if (validate(existing) != FT_ERR_SUCCESS
			|| static_cast<int32_t>(read_u32(existing.payload, 16U)) != slot
			|| read_u64(existing.payload, 32U) != expected_revision
			|| static_cast<int32_t>(read_u32(existing.payload, 48U)) != station_x
			|| static_cast<int32_t>(read_u32(existing.payload, 52U)) != station_y
			|| static_cast<int32_t>(read_u32(existing.payload, 56U)) != station_z
			|| read_u64(existing.payload, 60U) != expected_station_revision)
			return (FT_ERR_INVALID_ARGUMENT);
		if (existing.state == GameTransactionJournal::COMMITTED)
			return (FT_ERR_SUCCESS);
		if (existing.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_STATE);
		item_id = static_cast<int32_t>(read_u32(existing.payload, 20U));
		instance_id = read_u64(existing.payload, 24U);
		material_id = static_cast<int32_t>(read_u32(existing.payload, 40U));
		material_quantity = static_cast<int32_t>(read_u32(existing.payload, 44U));
	}
	else if (result != FT_ERR_NOT_FOUND)
		return (result);
	result = character.get_inventory().get_snapshot(before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (before.revision != expected_revision)
		return (FT_ERR_INVALID_STATE);
	auto equipment = std::find_if(before.items.begin(), before.items.end(),
		[slot](const game_inventory_item_snapshot &item)
		{ return (item.slot == slot); });
	if (equipment == before.items.end())
		return (FT_ERR_NOT_FOUND);
	if (existing.sequence == 0U)
	{
		item_id = equipment->item_id;
		instance_id = equipment->instance_id;
		result = catalog.resolve_equipment_repair_cost(item_id, &material_id,
			&material_quantity);
		if (result != FT_ERR_SUCCESS)
			return (result);
		result = make_payload(request_id, slot, item_id, instance_id,
			expected_revision, material_id, material_quantity, station_x,
			station_y, station_z, expected_station_revision, payload);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	else
	{
		try
		{
			payload = existing.payload;
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	if (equipment->item_id != item_id || equipment->instance_id != instance_id)
		return (FT_ERR_INVALID_STATE);
	result = catalog.stage_equipment_repair(before, slot, expected_revision,
		material_id, material_quantity, after);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = journal.serialize(journal_before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (existing.sequence == 0U)
	{
		result = journal.prepare(REPAIR_SCOPE, request_id, REPAIR_TYPE,
			payload.data(), static_cast<uint32_t>(payload.size()), &sequence,
			&replayed);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	result = journal.serialize(journal_prepared);
	if (result != FT_ERR_SUCCESS)
	{
		(void)journal.deserialize(journal_before.data(), journal_before.size());
		return (result);
	}
	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| snapshot_writer == nullptr)
		result = FT_ERR_INVALID_ARGUMENT;
	else
		result = snapshot_writer->save();
	if (result == FT_ERR_SUCCESS)
		result = character.get_inventory().restore_snapshot(after);
	if (result == FT_ERR_SUCCESS)
		result = journal.mark_committed(REPAIR_SCOPE, request_id);
	if (result == FT_ERR_SUCCESS)
		result = journal.prune_terminal_scope(REPAIR_SCOPE, request_id);
	if (result == FT_ERR_SUCCESS)
		result = snapshot_writer->save();
	if (result == FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	const int32_t inventory_rollback = character.get_inventory()
		.restore_snapshot(before);
	const int32_t journal_rollback = journal.deserialize(
		inventory_rollback == FT_ERR_SUCCESS ? journal_before.data()
			: journal_prepared.data(),
		inventory_rollback == FT_ERR_SUCCESS ? journal_before.size()
			: journal_prepared.size());
	int32_t checkpoint_rollback = FT_ERR_SUCCESS;
	if (snapshot_writer != nullptr)
		checkpoint_rollback = snapshot_writer->save();
	if (inventory_rollback != FT_ERR_SUCCESS)
		return (inventory_rollback);
	if (journal_rollback != FT_ERR_SUCCESS)
		return (journal_rollback);
	if (checkpoint_rollback != FT_ERR_SUCCESS)
		return (checkpoint_rollback);
	return (result);
}

int32_t WorldEquipmentRepairTransaction::recover_pending(
	const char *snapshot_path, game_character &character,
	MinecraftItemCatalog &catalog, GameTransactionJournal &journal,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	std::vector<GameTransactionJournal::Record> pending;
	int32_t result = journal.pending_records(pending);
	if (result != FT_ERR_SUCCESS)
		return (result);
	for (const GameTransactionJournal::Record &record : pending)
	{
		if (record.scope_id != REPAIR_SCOPE)
			continue ;
		result = validate(record);
		if (result != FT_ERR_SUCCESS
			|| record.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_ARGUMENT);
		result = WorldEquipmentRepairTransaction::execute(snapshot_path,
			character, catalog, journal, record.request_id,
			static_cast<int32_t>(read_u32(record.payload, 16U)),
			read_u64(record.payload, 32U),
			static_cast<int32_t>(read_u32(record.payload, 48U)),
			static_cast<int32_t>(read_u32(record.payload, 52U)),
			static_cast<int32_t>(read_u32(record.payload, 56U)),
			read_u64(record.payload, 60U), snapshot_writer);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	return (FT_ERR_SUCCESS);
}
