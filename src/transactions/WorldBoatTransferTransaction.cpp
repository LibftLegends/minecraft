#include "WorldBoatTransferTransaction.hpp"

#include "../../Libft/Modules/Errno/errno.hpp"
#include <algorithm>
#include <cstring>
#include <new>

namespace
{
	static const uint64_t BOAT_TRANSFER_SCOPE = 0x574F524C44425452ULL;
	static const uint32_t BOAT_TRANSFER_TYPE = 8U;
	static const uint32_t BOAT_TRANSFER_VERSION = 2U;
	static const uint32_t LEGACY_BOAT_TRANSFER_VERSION = 1U;
	static const std::size_t LEGACY_BOAT_TRANSFER_SIZE = 140U;
	static const std::size_t BOAT_TRANSFER_SIZE = 148U;
	static const uint32_t BOAT_PLACE = 1U;
	static const uint32_t BOAT_RETRIEVE = 2U;
	static const uint32_t BOAT_MOVE = 3U;

	static bool same_double_bits(double left, double right) noexcept
	{
		return (std::memcmp(&left, &right, sizeof(double)) == 0);
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

	static void append_double(std::vector<uint8_t> &bytes, double value)
	{
		uint64_t bits = 0U;
		static_assert(sizeof(bits) == sizeof(value), "double width mismatch");
		std::memcpy(&bits, &value, sizeof(bits));
		append_u64(bytes, bits);
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

	static bool is_boat_payload(const GameTransactionJournal::Record &record,
		uint32_t operation, uint64_t request_id,
		uint64_t inventory_revision, int32_t inventory_slot,
		uint32_t entity_id, uint64_t entity_revision,
		uint64_t owner_actor_id) noexcept
	{
		const uint32_t version = record.payload.size() >= 8U
			? read_u32(record.payload, 4U) : 0U;
		const bool valid_version =
			(version == LEGACY_BOAT_TRANSFER_VERSION
				&& record.payload.size() == LEGACY_BOAT_TRANSFER_SIZE
				&& owner_actor_id == 0U)
			|| (version == BOAT_TRANSFER_VERSION
				&& record.payload.size() == BOAT_TRANSFER_SIZE
				&& read_u64(record.payload, LEGACY_BOAT_TRANSFER_SIZE)
					== owner_actor_id);
		return (record.scope_id == BOAT_TRANSFER_SCOPE
			&& record.transaction_type == BOAT_TRANSFER_TYPE
			&& valid_version
			&& record.payload[0] == 'W' && record.payload[1] == 'B'
			&& record.payload[2] == 'T' && record.payload[3] == 'R'
			&& read_u32(record.payload, 8U) == operation
			&& read_u64(record.payload, 12U) == request_id
			&& read_u64(record.payload, 20U) == inventory_revision
			&& read_u32(record.payload, 28U)
				== static_cast<uint32_t>(inventory_slot)
			&& read_u32(record.payload, 32U) == entity_id
			&& read_u64(record.payload, 36U) == entity_revision
			&& read_u64(record.payload, 44U) != 0U
			&& read_u32(record.payload, 52U)
				== static_cast<uint32_t>(MinecraftItemCatalog::BOAT_ITEM_ID)
			&& read_u32(record.payload, 56U)
				== static_cast<uint32_t>(EntityState::Kind::BOAT));
	}

	static std::vector<uint8_t> make_payload(uint32_t operation,
		uint64_t request_id, uint64_t inventory_revision, int32_t inventory_slot,
		uint32_t entity_id, uint64_t entity_revision, uint64_t item_instance_id,
		int32_t item_id, const EntityState &state, uint64_t owner_actor_id,
		uint32_t version = BOAT_TRANSFER_VERSION)
	{
		std::vector<uint8_t> bytes;
		bytes.reserve(version == LEGACY_BOAT_TRANSFER_VERSION
			? LEGACY_BOAT_TRANSFER_SIZE : BOAT_TRANSFER_SIZE);
		bytes.insert(bytes.end(), {'W', 'B', 'T', 'R'});
		append_u32(bytes, version);
		append_u32(bytes, operation);
		append_u64(bytes, request_id);
		append_u64(bytes, inventory_revision);
		append_u32(bytes, static_cast<uint32_t>(inventory_slot));
		append_u32(bytes, entity_id);
		append_u64(bytes, entity_revision);
		append_u64(bytes, item_instance_id);
		append_u32(bytes, static_cast<uint32_t>(item_id));
		append_u32(bytes, state.type);
		append_double(bytes, state.position_x);
		append_double(bytes, state.position_y);
		append_double(bytes, state.position_z);
		append_double(bytes, state.velocity_x);
		append_double(bytes, state.velocity_y);
		append_double(bytes, state.velocity_z);
		append_double(bytes, state.orientation_yaw);
		append_double(bytes, state.orientation_pitch);
		append_u32(bytes, static_cast<uint32_t>(state.health));
		append_u32(bytes, state.action_state);
		append_u64(bytes, state.tick);
		if (version == BOAT_TRANSFER_VERSION)
			append_u64(bytes, owner_actor_id);
		return (bytes);
	}

	static bool payload_matches_state(
		const GameTransactionJournal::Record &record, uint32_t operation,
		uint64_t request_id, uint64_t inventory_revision,
		int32_t inventory_slot, uint32_t entity_id, uint64_t entity_revision,
		uint64_t item_instance_id, int32_t item_id, const EntityState &state,
		uint64_t owner_actor_id)
	{
		if (record.payload == make_payload(operation, request_id,
			inventory_revision, inventory_slot, entity_id, entity_revision,
			item_instance_id, item_id, state, owner_actor_id))
			return (true);
		return (owner_actor_id == 0U
			&& record.payload == make_payload(operation, request_id,
				inventory_revision, inventory_slot, entity_id, entity_revision,
				item_instance_id, item_id, state, 0U,
				LEGACY_BOAT_TRANSFER_VERSION));
	}

	static int32_t save_checkpoint(const char *snapshot_path,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept
	{
		if (snapshot_path == nullptr || snapshot_path[0] == '\0'
			|| snapshot_writer == nullptr || snapshot_writer->callback == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		return (snapshot_writer->save());
	}

	static int32_t prepare_journal(const char *snapshot_path,
		GameTransactionJournal &journal, const std::vector<uint8_t> &payload,
		uint64_t request_id, const std::vector<uint8_t> &journal_before,
		const GameSessionSnapshotWriter *snapshot_writer,
		GameTransactionJournal::Record *record_out) noexcept
	{
		GameTransactionJournal::Record record = {};
		int32_t result = journal.find(BOAT_TRANSFER_SCOPE, request_id, &record);
		if (result == FT_ERR_SUCCESS)
		{
			if (record.transaction_type != BOAT_TRANSFER_TYPE
				|| record.payload != payload)
				return (FT_ERR_INVALID_ARGUMENT);
			if (record.state == GameTransactionJournal::COMMITTED)
			{
				*record_out = record;
				return (FT_ERR_SUCCESS);
			}
			if (record.state == GameTransactionJournal::ABORTED)
				return (FT_ERR_INVALID_STATE);
			if (record.state != GameTransactionJournal::PREPARED)
				return (FT_ERR_INVALID_STATE);
			*record_out = record;
			return (FT_ERR_SUCCESS);
		}
		if (result != FT_ERR_NOT_FOUND)
			return (result);
		uint64_t sequence = 0U;
		bool replayed = false;
		result = journal.prepare(BOAT_TRANSFER_SCOPE, request_id,
			BOAT_TRANSFER_TYPE, payload.data(),
			static_cast<uint32_t>(payload.size()), &sequence, &replayed);
		(void)sequence;
		if (result == FT_ERR_SUCCESS)
			result = save_checkpoint(snapshot_path, snapshot_writer);
		if (result != FT_ERR_SUCCESS)
		{
			const int32_t rollback = journal.deserialize(journal_before.data(),
				journal_before.size());
			return (rollback == FT_ERR_SUCCESS ? result : rollback);
		}
		return (journal.find(BOAT_TRANSFER_SCOPE, request_id, record_out));
	}

	static int32_t rollback(const game_inventory_snapshot &inventory_before,
		game_inventory &inventory, const std::vector<uint8_t> &entities_before,
		WorldEntityStore &entities, GameTransactionJournal &journal,
		const std::vector<uint8_t> &journal_before, const char *snapshot_path,
		const GameSessionSnapshotWriter *snapshot_writer,
		int32_t original_error) noexcept
	{
		int32_t result = inventory.restore_snapshot(inventory_before);
		if (result == FT_ERR_SUCCESS)
			result = entities.deserialize(entities_before.data(),
				entities_before.size());
		if (result == FT_ERR_SUCCESS)
			result = journal.deserialize(journal_before.data(),
				journal_before.size());
		if (result == FT_ERR_SUCCESS)
			result = save_checkpoint(snapshot_path, snapshot_writer);
		return (result == FT_ERR_SUCCESS ? original_error : result);
	}

	static int32_t locate_entity_by_item(WorldEntityStore &entities,
		uint64_t item_instance_id, uint32_t *entity_id_out) noexcept
	{
		std::vector<WorldEntitySnapshot> snapshots;
		if (entity_id_out == nullptr || item_instance_id == 0U)
			return (FT_ERR_INVALID_ARGUMENT);
		int32_t result = entities.snapshot(snapshots);
		if (result != FT_ERR_SUCCESS)
			return (result);
		for (const WorldEntitySnapshot &snapshot : snapshots)
		{
			if (snapshot.item_instance_id == item_instance_id)
			{
				*entity_id_out = snapshot.state.id;
				return (FT_ERR_SUCCESS);
			}
		}
		return (FT_ERR_NOT_FOUND);
	}

	static int32_t locate_inventory_item(game_inventory &inventory,
		uint64_t item_instance_id, int32_t *slot_out) noexcept
	{
		game_inventory_snapshot snapshot = {};
		if (item_instance_id == 0U || slot_out == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		int32_t result = inventory.get_snapshot(snapshot);
		if (result != FT_ERR_SUCCESS)
			return (result);
		for (const game_inventory_item_snapshot &item : snapshot.items)
		{
			if (item.instance_id == item_instance_id)
			{
				*slot_out = item.slot;
				return (FT_ERR_SUCCESS);
			}
		}
		return (FT_ERR_NOT_FOUND);
	}
}

uint64_t WorldBoatTransferTransaction::journal_scope() noexcept
{
	return (BOAT_TRANSFER_SCOPE);
}

int32_t WorldBoatTransferTransaction::place(const char *snapshot_path,
	game_character &character, WorldItemStore &world_items,
	WorldEntityStore &world_entities, GameTransactionJournal &journal,
	uint64_t request_id, int32_t inventory_slot,
	uint64_t expected_inventory_revision, const EntityState &boat_state,
	const GameSessionSnapshotWriter *snapshot_writer,
	Result *result_out) noexcept
{
	return (WorldBoatTransferTransaction::place_for_actor(snapshot_path,
		character, world_items, world_entities, journal, request_id, 0U,
		inventory_slot, expected_inventory_revision, boat_state,
		snapshot_writer, result_out));
}

int32_t WorldBoatTransferTransaction::place_for_actor(
	const char *snapshot_path, game_character &character,
	WorldItemStore &world_items, WorldEntityStore &world_entities,
	GameTransactionJournal &journal, uint64_t request_id,
	uint64_t authenticated_actor_id, int32_t inventory_slot,
	uint64_t expected_inventory_revision, const EntityState &boat_state,
	const GameSessionSnapshotWriter *snapshot_writer,
	Result *result_out) noexcept
{
	game_inventory &inventory = character.get_inventory();
	game_inventory_snapshot inventory_before = {};
	game_inventory_snapshot inventory_after = {};
	std::vector<uint8_t> entities_before;
	std::vector<uint8_t> journal_before;
	GameTransactionJournal::Record record = {};
	uint64_t item_instance_id = 0U;
	int32_t result = FT_ERR_SUCCESS;
	if (result_out != nullptr)
		*result_out = {};
	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| request_id == 0U || inventory_slot < 0
		|| expected_inventory_revision == 0U || result_out == nullptr
		|| boat_state.id != 0U
		|| boat_state.type != static_cast<uint32_t>(EntityState::Kind::BOAT))
		return (FT_ERR_INVALID_ARGUMENT);
	{
		GameTransactionJournal::Record existing = {};
		const int32_t find_result = journal.find(BOAT_TRANSFER_SCOPE,
			request_id, &existing);
		if (find_result == FT_ERR_SUCCESS
			&& existing.state == GameTransactionJournal::COMMITTED)
		{
			if (!is_boat_payload(existing, BOAT_PLACE, request_id,
				expected_inventory_revision, inventory_slot, 0U, 0U,
				authenticated_actor_id))
				return (FT_ERR_INVALID_ARGUMENT);
			uint64_t item_id = read_u64(existing.payload, 44U);
			try
			{
				if (!payload_matches_state(existing, BOAT_PLACE, request_id,
					expected_inventory_revision, inventory_slot, 0U, 0U,
					item_id, MinecraftItemCatalog::BOAT_ITEM_ID, boat_state,
					authenticated_actor_id))
					return (FT_ERR_INVALID_ARGUMENT);
			}
			catch (const std::bad_alloc &)
			{
				return (FT_ERR_NO_MEMORY);
			}
			result_out->request_id = request_id;
			result_out->item_instance_id = item_id;
			result_out->inventory_slot = inventory_slot;
			result_out->replayed = true;
			return (locate_entity_by_item(world_entities, item_id,
				&result_out->entity_id));
		}
		if (find_result == FT_ERR_SUCCESS
			&& existing.state == GameTransactionJournal::ABORTED)
			return (FT_ERR_INVALID_STATE);
		if (find_result != FT_ERR_SUCCESS && find_result != FT_ERR_NOT_FOUND)
			return (find_result);
	}
	result = inventory.get_snapshot(inventory_before);
	if (result == FT_ERR_SUCCESS
		&& inventory_before.revision != expected_inventory_revision)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		auto item = std::find_if(inventory_before.items.begin(),
			inventory_before.items.end(), [inventory_slot](
				const game_inventory_item_snapshot &entry)
			{
				return (entry.slot == inventory_slot);
			});
		if (item == inventory_before.items.end())
			result = FT_ERR_NOT_FOUND;
		else if (item->item_id != MinecraftItemCatalog::BOAT_ITEM_ID
			|| item->instance_id == 0U || item->stack_size != 1
			|| item->max_stack != 1)
			result = FT_ERR_INVALID_STATE;
		else
		{
			item_instance_id = item->instance_id;
			const uint64_t area = static_cast<uint64_t>(item->width)
				* static_cast<uint64_t>(item->height);
			if (area > inventory_before.used_slots
				|| inventory_before.current_weight < item->stack_size
				|| inventory_before.revision == UINT64_MAX)
				result = FT_ERR_INVALID_STATE;
			else
			{
				inventory_after = inventory_before;
				auto staged_item = std::find_if(inventory_after.items.begin(),
					inventory_after.items.end(), [inventory_slot](
						const game_inventory_item_snapshot &entry)
					{
						return (entry.slot == inventory_slot);
					});
				if (staged_item == inventory_after.items.end())
					result = FT_ERR_INVALID_STATE;
				else
				{
					inventory_after.used_slots -= static_cast<ft_size_t>(area);
					inventory_after.current_weight -= staged_item->stack_size;
					inventory_after.items.erase(staged_item);
					++inventory_after.revision;
				}
			}
		}
	}
	if (result == FT_ERR_SUCCESS)
		result = world_entities.serialize(entities_before);
	if (result == FT_ERR_SUCCESS)
		result = journal.serialize(journal_before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	std::vector<uint8_t> payload;
	try
	{
		payload = make_payload(BOAT_PLACE, request_id,
			expected_inventory_revision, inventory_slot, 0U, 0U,
			item_instance_id, MinecraftItemCatalog::BOAT_ITEM_ID, boat_state,
			authenticated_actor_id);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	result = prepare_journal(snapshot_path, journal, payload, request_id,
		journal_before, snapshot_writer, &record);
	if (result != FT_ERR_SUCCESS)
	{
		return (result);
	}
	if (record.state == GameTransactionJournal::COMMITTED)
	{
		result_out->request_id = request_id;
		result_out->item_instance_id = item_instance_id;
		result_out->inventory_slot = inventory_slot;
		result_out->replayed = true;
		return (locate_entity_by_item(world_entities, item_instance_id,
			&result_out->entity_id));
	}
	result = inventory.restore_snapshot(inventory_after);
	if (result == FT_ERR_SUCCESS)
		result = world_entities.spawn_item(boat_state, item_instance_id,
			authenticated_actor_id, &result_out->entity_id);
	if (result == FT_ERR_SUCCESS)
		result = world_items.validate_instance_id_uniqueness(inventory,
			&world_entities);
	if (result == FT_ERR_SUCCESS)
		result = journal.mark_committed(BOAT_TRANSFER_SCOPE, request_id);
	if (result == FT_ERR_SUCCESS)
		result = save_checkpoint(snapshot_path, snapshot_writer);
	if (result != FT_ERR_SUCCESS)
		return (rollback(inventory_before, inventory, entities_before,
			world_entities, journal, journal_before, snapshot_path,
			snapshot_writer, result));
	result_out->request_id = request_id;
	result_out->item_instance_id = item_instance_id;
	result_out->inventory_slot = -1;
	result_out->replayed = false;
	return (FT_ERR_SUCCESS);
}

int32_t WorldBoatTransferTransaction::retrieve(const char *snapshot_path,
	game_character &character, WorldItemStore &world_items,
	WorldEntityStore &world_entities, MinecraftItemCatalog &item_catalog,
	GameTransactionJournal &journal, uint64_t request_id,
	uint32_t entity_id, uint64_t expected_entity_revision,
	uint64_t expected_inventory_revision,
	const GameSessionSnapshotWriter *snapshot_writer,
	Result *result_out) noexcept
{
	return (WorldBoatTransferTransaction::retrieve_for_actor(snapshot_path,
		character, world_items, world_entities, item_catalog, journal,
		request_id, 0U, entity_id, expected_entity_revision,
		expected_inventory_revision, snapshot_writer, result_out));
}

int32_t WorldBoatTransferTransaction::retrieve_for_actor(
	const char *snapshot_path, game_character &character,
	WorldItemStore &world_items, WorldEntityStore &world_entities,
	MinecraftItemCatalog &item_catalog, GameTransactionJournal &journal,
	uint64_t request_id, uint64_t authenticated_actor_id, uint32_t entity_id,
	uint64_t expected_entity_revision, uint64_t expected_inventory_revision,
	const GameSessionSnapshotWriter *snapshot_writer,
	Result *result_out) noexcept
{
	game_inventory &inventory = character.get_inventory();
	game_inventory_snapshot inventory_before = {};
	game_inventory_snapshot inventory_after = {};
	WorldEntitySnapshot boat = {};
	std::vector<uint8_t> entities_before;
	std::vector<uint8_t> journal_before;
	GameTransactionJournal::Record record = {};
	ft_sharedptr<game_item> item;
	int32_t output_slot = -1;
	int32_t result = FT_ERR_SUCCESS;
	if (result_out != nullptr)
		*result_out = {};
	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| request_id == 0U || entity_id == 0U
		|| expected_entity_revision == 0U || expected_inventory_revision == 0U
		|| result_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	{
		GameTransactionJournal::Record existing = {};
		const int32_t find_result = journal.find(BOAT_TRANSFER_SCOPE,
			request_id, &existing);
		if (find_result == FT_ERR_SUCCESS
			&& existing.state == GameTransactionJournal::COMMITTED)
		{
			if (!is_boat_payload(existing, BOAT_RETRIEVE, request_id,
				expected_inventory_revision, -1, entity_id,
				expected_entity_revision, authenticated_actor_id))
				return (FT_ERR_INVALID_ARGUMENT);
			result_out->request_id = request_id;
			result_out->item_instance_id = read_u64(existing.payload, 44U);
			result_out->entity_id = entity_id;
			result_out->replayed = true;
			return (locate_inventory_item(inventory,
				result_out->item_instance_id, &result_out->inventory_slot));
		}
		if (find_result == FT_ERR_SUCCESS
			&& existing.state == GameTransactionJournal::ABORTED)
			return (FT_ERR_INVALID_STATE);
		if (find_result != FT_ERR_SUCCESS && find_result != FT_ERR_NOT_FOUND)
			return (find_result);
	}
	result = world_entities.get(entity_id, &boat);
	if (result == FT_ERR_SUCCESS
		&& (boat.revision != expected_entity_revision
			|| boat.state.type != static_cast<uint32_t>(EntityState::Kind::BOAT)
			|| boat.item_instance_id == 0U))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS
		&& boat.owner_actor_id != authenticated_actor_id)
		result = FT_ERR_PERMISSION_DENIED;
	if (result == FT_ERR_SUCCESS)
	{
		result = inventory.get_snapshot(inventory_before);
	}
	if (result == FT_ERR_SUCCESS
		&& inventory_before.revision != expected_inventory_revision)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		result = item_catalog.create_item(MinecraftItemCatalog::BOAT_ITEM_ID,
			1, &item);
	}
	if (result == FT_ERR_SUCCESS)
	{
		item->set_instance_id(boat.item_instance_id);
		game_inventory staged_inventory;
		result = staged_inventory.initialize(inventory);
		if (result == FT_ERR_SUCCESS)
			result = staged_inventory.restore_snapshot(inventory_before);
		if (result == FT_ERR_SUCCESS)
		{
			result = staged_inventory.add_item_revisioned(item,
				expected_inventory_revision, &output_slot);
		}
		if (result == FT_ERR_SUCCESS)
		{
			result = staged_inventory.get_snapshot(inventory_after);
		}
	}
	if (result == FT_ERR_SUCCESS)
	{
		result = world_entities.serialize(entities_before);
	}
	if (result == FT_ERR_SUCCESS)
	{
		result = journal.serialize(journal_before);
	}
	if (result != FT_ERR_SUCCESS)
		return (result);
	std::vector<uint8_t> payload;
	try
	{
		payload = make_payload(BOAT_RETRIEVE, request_id,
			expected_inventory_revision, -1, entity_id, expected_entity_revision,
			boat.item_instance_id, MinecraftItemCatalog::BOAT_ITEM_ID,
			boat.state, authenticated_actor_id);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	result = prepare_journal(snapshot_path, journal, payload, request_id,
		journal_before, snapshot_writer, &record);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (record.state == GameTransactionJournal::COMMITTED)
	{
		result_out->request_id = request_id;
		result_out->item_instance_id = boat.item_instance_id;
		result_out->entity_id = entity_id;
		result_out->replayed = true;
		return (locate_inventory_item(inventory, boat.item_instance_id,
			&result_out->inventory_slot));
	}
	result = inventory.restore_snapshot(inventory_after);
	if (result == FT_ERR_SUCCESS)
		result = world_entities.despawn(entity_id, expected_entity_revision);
	if (result == FT_ERR_SUCCESS)
		result = world_items.validate_instance_id_uniqueness(inventory,
			&world_entities);
	if (result == FT_ERR_SUCCESS)
		result = journal.mark_committed(BOAT_TRANSFER_SCOPE, request_id);
	if (result == FT_ERR_SUCCESS)
		result = save_checkpoint(snapshot_path, snapshot_writer);
	if (result != FT_ERR_SUCCESS)
		return (rollback(inventory_before, inventory, entities_before,
			world_entities, journal, journal_before, snapshot_path,
			snapshot_writer, result));
	result_out->request_id = request_id;
	result_out->item_instance_id = boat.item_instance_id;
	result_out->entity_id = entity_id;
	result_out->inventory_slot = output_slot;
	result_out->replayed = false;
	return (FT_ERR_SUCCESS);
}

int32_t WorldBoatTransferTransaction::move_for_actor(
	const char *snapshot_path, WorldEntityStore &world_entities,
	GameTransactionJournal &journal, uint64_t request_id,
	uint64_t authenticated_actor_id, uint32_t entity_id,
	uint64_t expected_entity_revision, const EntityState &next_state,
	const GameSessionSnapshotWriter *snapshot_writer,
	uint64_t *new_revision_out) noexcept
{
	WorldEntitySnapshot before = {};
	std::vector<uint8_t> entities_before;
	std::vector<uint8_t> journal_before;
	GameTransactionJournal::Record existing = {};
	GameTransactionJournal::Record record = {};
	std::vector<uint8_t> payload;
	EntityState request_state = next_state;
	int32_t result;
	if (new_revision_out != nullptr)
		*new_revision_out = 0U;
	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| request_id == 0U || authenticated_actor_id == 0U || entity_id == 0U
		|| expected_entity_revision == 0U || new_revision_out == nullptr
		|| next_state.id != entity_id
		|| next_state.type != static_cast<uint32_t>(EntityState::Kind::BOAT))
		return (FT_ERR_INVALID_ARGUMENT);
	result = world_entities.get(entity_id, &before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (before.state.type != static_cast<uint32_t>(EntityState::Kind::BOAT)
		|| before.item_instance_id == 0U)
		return (FT_ERR_INVALID_STATE);
	if (before.owner_actor_id != authenticated_actor_id)
		return (FT_ERR_PERMISSION_DENIED);
	request_state.velocity_x = 0.0;
	request_state.velocity_y = 0.0;
	request_state.velocity_z = 0.0;
	request_state.orientation_pitch = 0.0;
	request_state.health = 1;
	request_state.action_state = static_cast<uint32_t>(
		EntityState::ActionState::IDLE);
	request_state.tick = 0U;
	try
	{
		payload = make_payload(BOAT_MOVE, request_id, 0U, -1, entity_id,
			expected_entity_revision, before.item_instance_id,
			MinecraftItemCatalog::BOAT_ITEM_ID, request_state,
			authenticated_actor_id);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	result = journal.find(BOAT_TRANSFER_SCOPE, request_id, &existing);
	if (result == FT_ERR_SUCCESS)
	{
		if (existing.transaction_type != BOAT_TRANSFER_TYPE
			|| existing.payload != payload)
			return (FT_ERR_INVALID_ARGUMENT);
		if (existing.state == GameTransactionJournal::ABORTED)
			return (FT_ERR_INVALID_STATE);
		if (existing.state == GameTransactionJournal::COMMITTED)
		{
			if (expected_entity_revision == UINT64_MAX
				|| before.revision != expected_entity_revision + 1U
				|| !same_double_bits(before.state.position_x,
					next_state.position_x)
				|| !same_double_bits(before.state.position_y,
					next_state.position_y)
				|| !same_double_bits(before.state.position_z,
					next_state.position_z)
				|| !same_double_bits(before.state.orientation_yaw,
					next_state.orientation_yaw)
				|| before.owner_actor_id != authenticated_actor_id
				|| before.item_instance_id
					!= read_u64(existing.payload, 44U))
				return (FT_ERR_INVALID_STATE);
			*new_revision_out = before.revision;
			return (FT_ERR_SUCCESS);
		}
		if (existing.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_STATE);
	}
	else if (result != FT_ERR_NOT_FOUND)
		return (result);
	if (before.revision != expected_entity_revision
		|| before.state.type != static_cast<uint32_t>(EntityState::Kind::BOAT))
		return (FT_ERR_INVALID_STATE);
	if (expected_entity_revision == UINT64_MAX)
		return (FT_ERR_OUT_OF_RANGE);
	result = world_entities.serialize(entities_before);
	if (result == FT_ERR_SUCCESS)
		result = journal.serialize(journal_before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = prepare_journal(snapshot_path, journal, payload, request_id,
		journal_before, snapshot_writer, &record);
	if (result != FT_ERR_SUCCESS)
		return (result);
	uint64_t next_revision = 0U;
	result = world_entities.update(entity_id, expected_entity_revision,
		next_state, &next_revision);
	if (result == FT_ERR_SUCCESS)
		result = journal.mark_committed(BOAT_TRANSFER_SCOPE, request_id);
	if (result == FT_ERR_SUCCESS)
		result = save_checkpoint(snapshot_path, snapshot_writer);
	if (result != FT_ERR_SUCCESS)
	{
		int32_t rollback_result = world_entities.deserialize(
			entities_before.data(), entities_before.size());
		if (rollback_result == FT_ERR_SUCCESS)
			rollback_result = journal.deserialize(journal_before.data(),
				journal_before.size());
		if (rollback_result == FT_ERR_SUCCESS)
			rollback_result = save_checkpoint(snapshot_path, snapshot_writer);
		return (rollback_result == FT_ERR_SUCCESS ? result : rollback_result);
	}
	*new_revision_out = next_revision;
	return (FT_ERR_SUCCESS);
}

int32_t WorldBoatTransferTransaction::recover_pending(
	const char *snapshot_path, game_character &character,
	WorldItemStore &world_items, WorldEntityStore &world_entities,
	GameTransactionJournal &journal,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	std::vector<GameTransactionJournal::Record> pending;
	int32_t result = journal.pending_records(pending);
	if (result != FT_ERR_SUCCESS)
		return (result);
	bool changed = false;
	for (const GameTransactionJournal::Record &record : pending)
	{
		if (record.scope_id != BOAT_TRANSFER_SCOPE)
			continue ;
		if ((record.payload.size() != LEGACY_BOAT_TRANSFER_SIZE
				&& record.payload.size() != BOAT_TRANSFER_SIZE)
			|| record.payload[0] != 'W' || record.payload[1] != 'B'
			|| record.payload[2] != 'T' || record.payload[3] != 'R')
			return (FT_ERR_INVALID_ARGUMENT);
		const uint32_t operation = read_u32(record.payload, 8U);
		const uint64_t inventory_revision = read_u64(record.payload, 20U);
		const int32_t inventory_slot = static_cast<int32_t>(
			read_u32(record.payload, 28U));
		const uint32_t entity_id = read_u32(record.payload, 32U);
		const uint64_t entity_revision = read_u64(record.payload, 36U);
		const uint64_t owner_actor_id = record.payload.size()
			== BOAT_TRANSFER_SIZE
			? read_u64(record.payload, LEGACY_BOAT_TRANSFER_SIZE) : 0U;
		if ((operation != BOAT_PLACE && operation != BOAT_RETRIEVE
				&& operation != BOAT_MOVE)
			|| !is_boat_payload(record, operation, record.request_id,
				inventory_revision, inventory_slot, entity_id, entity_revision,
				owner_actor_id)
			|| (operation == BOAT_PLACE
				&& (inventory_slot < 0 || entity_id != 0U
					|| entity_revision != 0U))
			|| (operation == BOAT_RETRIEVE
				&& (inventory_slot != -1 || entity_id == 0U
					|| entity_revision == 0U))
			|| (operation == BOAT_MOVE
				&& (inventory_revision != 0U || inventory_slot != -1
					|| entity_id == 0U || entity_revision == 0U
					|| owner_actor_id == 0U)))
			return (FT_ERR_INVALID_ARGUMENT);
		result = journal.mark_aborted(record.scope_id, record.request_id);
		if (result != FT_ERR_SUCCESS)
			return (result);
		changed = true;
	}
	if (!changed)
		return (FT_ERR_SUCCESS);
	(void)character;
	(void)world_items;
	(void)world_entities;
	return (save_checkpoint(snapshot_path, snapshot_writer));
}
