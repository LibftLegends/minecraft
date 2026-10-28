#include "WorldEquipmentRepairTransaction.hpp"

#include "../crafting/MinecraftRecipeCatalog.hpp"
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
	static const uint32_t REPAIR_VERSION = 2U;
	static const std::size_t REPAIR_V1_PAYLOAD_SIZE = 68U;
	static const std::size_t REPAIR_PAYLOAD_SIZE = 76U;

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
			|| (record.payload.size() != REPAIR_V1_PAYLOAD_SIZE
				&& record.payload.size() != REPAIR_PAYLOAD_SIZE)
			|| record.payload[0] != 'G' || record.payload[1] != 'R'
			|| record.payload[2] != 'P' || record.payload[3] != 'R'
			|| (read_u32(record.payload, 4U) != 1U
				&& read_u32(record.payload, 4U) != REPAIR_VERSION)
			|| (read_u32(record.payload, 4U) == 1U
				&& record.payload.size() != REPAIR_V1_PAYLOAD_SIZE)
			|| (read_u32(record.payload, 4U) == REPAIR_VERSION
				&& (record.payload.size() != REPAIR_PAYLOAD_SIZE
					|| read_u64(record.payload, 68U) == 0U))
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
	static int32_t make_payload(uint64_t request_id,
		uint64_t authenticated_actor_id, int32_t slot,
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
			payload.reserve(authenticated_actor_id == 0U
				? REPAIR_V1_PAYLOAD_SIZE : REPAIR_PAYLOAD_SIZE);
			payload.insert(payload.end(), {'G', 'R', 'P', 'R'});
			append_u32(payload, authenticated_actor_id == 0U ? 1U
				: REPAIR_VERSION);
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
			if (authenticated_actor_id != 0U)
				append_u64(payload, authenticated_actor_id);
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		return (payload.size() == (authenticated_actor_id == 0U
			? REPAIR_V1_PAYLOAD_SIZE : REPAIR_PAYLOAD_SIZE)
			? FT_ERR_SUCCESS : FT_ERR_INVALID_STATE);
	}
	struct PreparedRecoveryAuthorization
	{
		bool (*station_validator)(uint32_t, int32_t, int32_t, int32_t,
			uint64_t, void *) noexcept;
		void *station_user_data;
		bool (*actor_authorizer)(uint64_t, void *) noexcept;
		void *actor_user_data;
	};
	static bool authorize_prepared_recovery(uint64_t authenticated_actor_id,
		int32_t station_x, int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision, void *user_data) noexcept
	{
		if (user_data == nullptr)
			return (false);
		PreparedRecoveryAuthorization *authorization =
			static_cast<PreparedRecoveryAuthorization *>(user_data);
		return (authenticated_actor_id != 0U
			&& authorization->actor_authorizer != nullptr
			&& authorization->actor_authorizer(authenticated_actor_id,
				authorization->actor_user_data)
			&& authorization->station_validator != nullptr
			&& authorization->station_validator(
				MinecraftRecipeCatalog::WORKBENCH_STATION_ID,
				station_x, station_y, station_z, expected_station_revision,
				authorization->station_user_data));
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
	return (WorldEquipmentRepairTransaction::execute(snapshot_path, character,
		catalog, journal, request_id, 0U, slot, expected_revision, station_x,
		station_y, station_z, expected_station_revision, snapshot_writer,
		nullptr, nullptr));
}

int32_t WorldEquipmentRepairTransaction::execute(const char *snapshot_path,
	game_character &character, MinecraftItemCatalog &catalog,
	GameTransactionJournal &journal, uint64_t request_id,
	uint64_t authenticated_actor_id, int32_t slot,
	uint64_t expected_revision, int32_t station_x, int32_t station_y,
	int32_t station_z, uint64_t expected_station_revision,
	const GameSessionSnapshotWriter *snapshot_writer,
	bool (*precommit_authorizer)(uint64_t authenticated_actor_id,
		int32_t station_x, int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision, void *user_data) noexcept,
	void *precommit_authorizer_user_data) noexcept
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
	if (request_id == 0U || slot < 0 || expected_revision == 0U
		|| (authenticated_actor_id != 0U && precommit_authorizer == nullptr))
		return (FT_ERR_INVALID_ARGUMENT);
	int32_t result = journal.find(REPAIR_SCOPE, request_id, &existing);
	if (result == FT_ERR_SUCCESS)
	{
		if (validate(existing) != FT_ERR_SUCCESS
			|| (read_u32(existing.payload, 4U) == REPAIR_VERSION
				? read_u64(existing.payload, 68U) != authenticated_actor_id
				: authenticated_actor_id != 0U)
			|| static_cast<int32_t>(read_u32(existing.payload, 16U)) != slot
			|| read_u64(existing.payload, 32U) != expected_revision
			|| static_cast<int32_t>(read_u32(existing.payload, 48U)) != station_x
			|| static_cast<int32_t>(read_u32(existing.payload, 52U)) != station_y
			|| static_cast<int32_t>(read_u32(existing.payload, 56U)) != station_z
			|| read_u64(existing.payload, 60U) != expected_station_revision)
			return (FT_ERR_INVALID_ARGUMENT);
		if (existing.state == GameTransactionJournal::COMMITTED)
		{
			if (authenticated_actor_id != 0U
				&& !precommit_authorizer(authenticated_actor_id, station_x,
					station_y, station_z, expected_station_revision,
					precommit_authorizer_user_data))
				return (FT_ERR_PERMISSION_DENIED);
			return (FT_ERR_SUCCESS);
		}
		if (existing.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_STATE);
		item_id = static_cast<int32_t>(read_u32(existing.payload, 20U));
		instance_id = read_u64(existing.payload, 24U);
		material_id = static_cast<int32_t>(read_u32(existing.payload, 40U));
		material_quantity = static_cast<int32_t>(read_u32(existing.payload, 44U));
	}
	else if (result != FT_ERR_NOT_FOUND)
		return (result);
	else
	{
		GameTransactionJournal::Record latest = {};
		result = journal.latest(REPAIR_SCOPE, REPAIR_TYPE, &latest);
		if (result == FT_ERR_SUCCESS && request_id <= latest.request_id)
			return (FT_ERR_NOT_FOUND);
		if (result != FT_ERR_SUCCESS && result != FT_ERR_NOT_FOUND)
			return (result);
	}
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
		result = make_payload(request_id, authenticated_actor_id, slot,
			item_id, instance_id,
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
		const int32_t rollback_result = journal.deserialize(
			journal_before.data(), journal_before.size());
		return (rollback_result == FT_ERR_SUCCESS
			? result : FT_ERR_INVALID_STATE);
	}
	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| snapshot_writer == nullptr)
		result = FT_ERR_INVALID_ARGUMENT;
	else
		result = snapshot_writer->save();
	if (result == FT_ERR_SUCCESS && authenticated_actor_id != 0U
		&& !precommit_authorizer(authenticated_actor_id, station_x,
			station_y, station_z, expected_station_revision,
			precommit_authorizer_user_data))
	{
		const int32_t abort_result = journal.mark_aborted(REPAIR_SCOPE,
			request_id);
		const int32_t checkpoint_result = abort_result == FT_ERR_SUCCESS
			? snapshot_writer->save() : abort_result;
		return (checkpoint_result == FT_ERR_SUCCESS
			? FT_ERR_PERMISSION_DENIED : checkpoint_result);
	}
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
	const GameSessionSnapshotWriter *snapshot_writer,
	bool (*station_continuity_validator)(uint32_t station_id,
		int32_t station_x, int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision, void *user_data) noexcept,
		void *station_continuity_user_data,
		bool (*actor_authorizer)(uint64_t authenticated_actor_id,
			void *user_data) noexcept,
		void *actor_authorizer_user_data) noexcept
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
		const uint64_t actor_id = read_u32(record.payload, 4U)
			== REPAIR_VERSION ? read_u64(record.payload, 68U) : 0U;
		if (actor_id != 0U && (actor_authorizer == nullptr
			|| !actor_authorizer(actor_id, actor_authorizer_user_data)))
		{
			if (snapshot_writer == nullptr)
				return (FT_ERR_INVALID_ARGUMENT);
			result = journal.mark_aborted(REPAIR_SCOPE, record.request_id);
			if (result == FT_ERR_SUCCESS)
				result = snapshot_writer->save();
			if (result != FT_ERR_SUCCESS)
				return (result);
			continue ;
		}
		if (station_continuity_validator == nullptr
			|| !station_continuity_validator(static_cast<uint32_t>(
				MinecraftRecipeCatalog::WORKBENCH_STATION_ID),
				static_cast<int32_t>(read_u32(record.payload, 48U)),
				static_cast<int32_t>(read_u32(record.payload, 52U)),
				static_cast<int32_t>(read_u32(record.payload, 56U)),
				read_u64(record.payload, 60U),
				station_continuity_user_data))
			continue ;
		PreparedRecoveryAuthorization authorization = {
			station_continuity_validator, station_continuity_user_data,
			actor_authorizer, actor_authorizer_user_data};
		result = WorldEquipmentRepairTransaction::execute(snapshot_path,
			character, catalog, journal, record.request_id,
			read_u32(record.payload, 4U) == REPAIR_VERSION
				? read_u64(record.payload, 68U) : 0U,
			static_cast<int32_t>(read_u32(record.payload, 16U)),
			read_u64(record.payload, 32U),
			static_cast<int32_t>(read_u32(record.payload, 48U)),
			static_cast<int32_t>(read_u32(record.payload, 52U)),
			static_cast<int32_t>(read_u32(record.payload, 56U)),
			read_u64(record.payload, 60U), snapshot_writer,
			&authorize_prepared_recovery, &authorization);
		if (result != FT_ERR_SUCCESS
			&& result != FT_ERR_PERMISSION_DENIED)
			return (result);
	}
	return (FT_ERR_SUCCESS);
}
