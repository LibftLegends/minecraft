#include "WorldEquipmentLoadoutTransaction.hpp"

#include "../inventory/MinecraftItemCatalog.hpp"
#include "../../Libft/Modules/Game/game_inventory.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <algorithm>
#include <climits>
#include <new>
#include <vector>

namespace
{
	static const uint64_t LOADOUT_SCOPE = 0x45515549504C4F41ULL;
	static const uint32_t LOADOUT_TYPE = 15U;
	static const uint32_t LOADOUT_VERSION = 1U;
	static const std::size_t LOADOUT_PAYLOAD_SIZE = 44U;

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
	static int32_t character_slot(uint32_t equipment_slot,
		int32_t *character_slot_out) noexcept
	{
		if (character_slot_out == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		switch (equipment_slot)
		{
			case MINECRAFT_EQUIPMENT_MAIN_HAND:
				*character_slot_out = EQUIP_WEAPON;
				break ;
			case MINECRAFT_EQUIPMENT_OFF_HAND:
				*character_slot_out = EQUIP_OFF_HAND;
				break ;
			case MINECRAFT_EQUIPMENT_HEAD:
				*character_slot_out = EQUIP_HEAD;
				break ;
			case MINECRAFT_EQUIPMENT_CHEST:
				*character_slot_out = EQUIP_CHEST;
				break ;
			case MINECRAFT_EQUIPMENT_LEGS:
				*character_slot_out = EQUIP_LEGS;
				break ;
			case MINECRAFT_EQUIPMENT_FEET:
				*character_slot_out = EQUIP_FEET;
				break ;
			default:
				return (FT_ERR_INVALID_ARGUMENT);
		}
	return (FT_ERR_SUCCESS);
	}
	static int32_t make_payload(uint64_t request_id, int32_t inventory_slot,
		uint64_t inventory_revision, uint32_t equipment_slot, int32_t item_id,
		uint64_t instance_id, std::vector<uint8_t> &payload) noexcept
	{
		int32_t character_equipment_slot = 0;
		if (request_id == 0U || inventory_slot < -1
			|| inventory_revision == 0U
			|| (inventory_slot == -1 && (item_id != 0 || instance_id != 0U))
			|| (inventory_slot >= 0 && (item_id <= 0 || instance_id == 0U))
			|| character_slot(equipment_slot, &character_equipment_slot)
				!= FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_ARGUMENT);
		try
		{
			payload.reserve(LOADOUT_PAYLOAD_SIZE);
			payload.insert(payload.end(), {'G', 'L', 'E', 'Q'});
			append_u32(payload, LOADOUT_VERSION);
			append_u64(payload, request_id);
			append_u32(payload, static_cast<uint32_t>(inventory_slot));
			append_u64(payload, inventory_revision);
			append_u32(payload, equipment_slot);
			append_u32(payload, static_cast<uint32_t>(item_id));
			append_u64(payload, instance_id);
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		return (payload.size() == LOADOUT_PAYLOAD_SIZE
			? FT_ERR_SUCCESS : FT_ERR_INVALID_STATE);
	}
	static int32_t validate_record(const GameTransactionJournal::Record &record)
	{
		int32_t character_equipment_slot = 0;
		if (record.scope_id != LOADOUT_SCOPE
			|| record.transaction_type != LOADOUT_TYPE || record.request_id == 0U
			|| record.payload.size() != LOADOUT_PAYLOAD_SIZE
			|| record.payload[0] != 'G' || record.payload[1] != 'L'
			|| record.payload[2] != 'E' || record.payload[3] != 'Q'
			|| read_u32(record.payload, 4U) != LOADOUT_VERSION
			|| read_u64(record.payload, 8U) != record.request_id
			|| (read_u32(record.payload, 16U) > INT32_MAX
				&& read_u32(record.payload, 16U) != UINT32_MAX)
			|| read_u64(record.payload, 20U) == 0U
			|| character_slot(read_u32(record.payload, 28U),
				&character_equipment_slot) != FT_ERR_SUCCESS
			|| ((read_u32(record.payload, 16U) == UINT32_MAX)
				!= (read_u32(record.payload, 32U) == 0U))
			|| ((read_u32(record.payload, 16U) == UINT32_MAX)
				!= (read_u64(record.payload, 36U) == 0U))
			|| (read_u32(record.payload, 32U) > INT32_MAX))
			return (FT_ERR_INVALID_ARGUMENT);
		return (FT_ERR_SUCCESS);
	}
	static void release_item_handle(ft_sharedptr<game_item> *&item) noexcept
	{
		if (item == nullptr)
			return ;
		(void)item->destroy();
		delete item;
		item = nullptr;
		return ;
	}
}

uint64_t WorldEquipmentLoadoutTransaction::journal_scope() noexcept
{
	return (LOADOUT_SCOPE);
}

uint32_t WorldEquipmentLoadoutTransaction::transaction_type() noexcept
{
	return (LOADOUT_TYPE);
}

int32_t WorldEquipmentLoadoutTransaction::equip(const char *snapshot_path,
	game_character &character, MinecraftItemCatalog &catalog,
	GameTransactionJournal &journal, uint64_t request_id,
		int32_t inventory_slot, uint64_t expected_inventory_revision,
	uint32_t equipment_slot,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	game_inventory &inventory = character.get_inventory();
	game_inventory_snapshot inventory_before = {};
	GameTransactionJournal::Record existing = {};
	std::vector<uint8_t> payload;
	std::vector<uint8_t> journal_before;
	std::vector<uint8_t> journal_prepared;
	ft_sharedptr<game_item> *new_item = nullptr;
	ft_sharedptr<game_item> *old_item = nullptr;
	std::vector<game_inventory_item_snapshot>::const_iterator selected;
	MinecraftEquipmentRule rule = {};
	uint64_t sequence = 0U;
	uint64_t current_revision = 0U;
	uint32_t current_durability = 0U;
	uint32_t maximum_durability = 0U;
	int32_t game_slot = 0;
	int32_t inserted_slot = -1;
	int32_t requested_item_id = 0;
	uint64_t requested_instance_id = 0U;
	ft_bool old_item_present = FT_FALSE;
	ft_bool equipment_mutated = FT_FALSE;
	bool replayed = false;
	int32_t action_error = FT_ERR_SUCCESS;
	int32_t result;

	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| snapshot_writer == nullptr || request_id == 0U || inventory_slot < -1
		|| expected_inventory_revision == 0U
		|| character_slot(equipment_slot, &game_slot) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_ARGUMENT);
	result = journal.find(LOADOUT_SCOPE, request_id, &existing);
	if (result == FT_ERR_SUCCESS)
	{
		if (validate_record(existing) != FT_ERR_SUCCESS
			|| read_u32(existing.payload, 16U)
				!= static_cast<uint32_t>(inventory_slot)
			|| read_u64(existing.payload, 20U) != expected_inventory_revision
			|| read_u32(existing.payload, 28U) != equipment_slot)
			return (FT_ERR_INVALID_ARGUMENT);
		if (existing.state == GameTransactionJournal::COMMITTED)
			return (FT_ERR_SUCCESS);
		if (existing.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_STATE);
		try
		{
			payload = existing.payload;
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	else if (result != FT_ERR_NOT_FOUND)
		return (result);
	result = inventory.get_snapshot(inventory_before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (inventory_before.revision != expected_inventory_revision)
		return (FT_ERR_INVALID_STATE);
	if (inventory_slot >= 0)
	{
		selected = std::find_if(inventory_before.items.begin(),
			inventory_before.items.end(), [inventory_slot](
				const game_inventory_item_snapshot &item)
			{ return (item.slot == inventory_slot); });
		if (selected == inventory_before.items.end()
			|| selected->instance_id == 0U || selected->stack_size != 1
			|| selected->max_stack != 1)
			return (FT_ERR_INVALID_STATE);
		result = catalog.resolve_equipment_rule(selected->item_id, &rule);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (static_cast<uint32_t>(rule.slot) != equipment_slot)
			return (FT_ERR_INVALID_ARGUMENT);
		result = catalog.query_equipment_durability(*selected,
			&current_durability, &maximum_durability);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (current_durability == 0U)
			return (FT_ERR_INVALID_STATE);
		requested_item_id = selected->item_id;
		requested_instance_id = selected->instance_id;
	}
	if (existing.sequence == 0U)
	{
		result = make_payload(request_id, inventory_slot,
			expected_inventory_revision, equipment_slot, requested_item_id,
			requested_instance_id, payload);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	else if (read_u32(payload, 32U)
			!= static_cast<uint32_t>(requested_item_id)
		|| read_u64(payload, 36U) != requested_instance_id)
		return (FT_ERR_INVALID_STATE);
	if (inventory_slot >= 0)
	{
		result = inventory.copy_item_at_revision(inventory_slot,
			expected_inventory_revision, &new_item);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	old_item = character.get_equipped_item(game_slot);
	if (old_item == nullptr)
	{
		release_item_handle(new_item);
		return (character.get_error());
	}
	if (*old_item)
		old_item_present = FT_TRUE;
	if (inventory_slot == -1 && old_item_present == FT_FALSE)
	{
		release_item_handle(new_item);
		release_item_handle(old_item);
		return (FT_ERR_NOT_FOUND);
	}
	if (inventory_slot >= 0 && old_item_present == FT_TRUE
		&& (*old_item)->get_instance_id() == requested_instance_id)
	{
		release_item_handle(new_item);
		release_item_handle(old_item);
		return (FT_ERR_INVALID_STATE);
	}
	result = journal.serialize(journal_before);
	if (result == FT_ERR_SUCCESS && existing.sequence == 0U)
		result = journal.prepare(LOADOUT_SCOPE, request_id, LOADOUT_TYPE,
			payload.data(), static_cast<uint32_t>(payload.size()), &sequence,
			&replayed);
	if (result == FT_ERR_SUCCESS)
		result = journal.serialize(journal_prepared);
	if (result == FT_ERR_SUCCESS)
		result = snapshot_writer->save();
	if (result != FT_ERR_SUCCESS)
	{
		if (!journal_before.empty())
			(void)journal.deserialize(journal_before.data(), journal_before.size());
		release_item_handle(new_item);
		release_item_handle(old_item);
		return (result);
	}
	if (result == FT_ERR_SUCCESS && inventory_slot >= 0)
		result = inventory.remove_item_instance(inventory_slot,
			expected_inventory_revision, requested_instance_id);
	if (result == FT_ERR_SUCCESS && inventory_slot >= 0)
		result = character.equip_item(game_slot, *new_item);
	if (result == FT_ERR_SUCCESS && inventory_slot == -1)
	{
		character.unequip_item(game_slot);
		result = character.get_error();
	}
	if (result == FT_ERR_SUCCESS)
		equipment_mutated = FT_TRUE;
	if (result == FT_ERR_SUCCESS && old_item_present == FT_TRUE)
	{
		result = inventory.get_revision(&current_revision);
		if (result == FT_ERR_SUCCESS)
			result = inventory.add_item_revisioned(*old_item,
				current_revision, &inserted_slot);
	}
	if (result == FT_ERR_SUCCESS)
		result = journal.mark_committed(LOADOUT_SCOPE, request_id);
	if (result == FT_ERR_SUCCESS)
		result = snapshot_writer->save();
	if (result == FT_ERR_SUCCESS)
	{
		release_item_handle(new_item);
		release_item_handle(old_item);
		return (FT_ERR_SUCCESS);
	}
	action_error = result;
	int32_t equipment_rollback = FT_ERR_SUCCESS;
	if (equipment_mutated == FT_TRUE)
	{
		if (old_item_present == FT_TRUE)
			equipment_rollback = character.equip_item(game_slot, *old_item);
		else
		{
			character.unequip_item(game_slot);
			equipment_rollback = character.get_error();
		}
	}
	int32_t inventory_rollback = inventory.restore_snapshot(inventory_before);
	int32_t journal_rollback = journal.deserialize(
		inventory_rollback == FT_ERR_SUCCESS
			&& equipment_rollback == FT_ERR_SUCCESS
			? journal_before.data() : journal_prepared.data(),
		inventory_rollback == FT_ERR_SUCCESS
			&& equipment_rollback == FT_ERR_SUCCESS
			? journal_before.size() : journal_prepared.size());
	release_item_handle(new_item);
	release_item_handle(old_item);
	if (equipment_rollback != FT_ERR_SUCCESS)
		return (equipment_rollback);
	if (inventory_rollback != FT_ERR_SUCCESS)
		return (inventory_rollback);
	if (journal_rollback != FT_ERR_SUCCESS)
		return (journal_rollback);
	int32_t checkpoint_rollback = snapshot_writer->save();
	if (checkpoint_rollback != FT_ERR_SUCCESS)
		return (checkpoint_rollback);
	return (action_error);
}

int32_t WorldEquipmentLoadoutTransaction::unequip(const char *snapshot_path,
	game_character &character, MinecraftItemCatalog &catalog,
	GameTransactionJournal &journal, uint64_t request_id,
	uint64_t expected_inventory_revision, uint32_t equipment_slot,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	return (WorldEquipmentLoadoutTransaction::equip(snapshot_path, character,
		catalog, journal, request_id, -1, expected_inventory_revision,
		equipment_slot, snapshot_writer));
}

int32_t WorldEquipmentLoadoutTransaction::recover_pending(
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
		if (record.scope_id != LOADOUT_SCOPE)
			continue ;
		result = validate_record(record);
		if (result != FT_ERR_SUCCESS
			|| record.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_ARGUMENT);
		int32_t inventory_slot = -1;
		if (read_u32(record.payload, 16U) != UINT32_MAX)
			inventory_slot = static_cast<int32_t>(
				read_u32(record.payload, 16U));
		result = WorldEquipmentLoadoutTransaction::equip(snapshot_path,
			character, catalog, journal, record.request_id,
			inventory_slot,
			read_u64(record.payload, 20U), read_u32(record.payload, 28U),
			snapshot_writer);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	return (FT_ERR_SUCCESS);
}
