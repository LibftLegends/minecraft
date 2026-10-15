#include "WorldBlockMiningTransaction.hpp"
#include "../../Libft/Modules/Game/game_inventory.hpp"
#include "../../Libft/Modules/Game/game_character.hpp"

#include "../coordinates/WorldCoordinates.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <vector>
#include <algorithm>
#include <climits>

namespace
{
	static const uint64_t MINING_SCOPE = 0x574F524C444D494ELL;
	static const uint32_t MINING_TYPE = 10U;
	static const uint32_t MINING_VERSION = 1U;
	static const uint32_t MINING_TOOL_VERSION = 2U;
	static const uint32_t MINING_EQUIPPED_TOOL_VERSION = 3U;
	static const uint32_t MINING_EQUIPPED_TOOL_SENTINEL = UINT32_MAX - 1U;
	static const std::size_t MINING_PAYLOAD_SIZE = 80U;
	static const std::size_t MINING_TOOL_PAYLOAD_SIZE = 104U;
	static const uint32_t MAX_MINING_DROPS = 2U;

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
	static int32_t checkpoint(const char *path,
		const GameSessionSnapshotWriter *writer) noexcept
	{
		if (path == nullptr || path[0] == '\0' || writer == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		return (writer->save());
	}
	static int32_t encode(uint64_t request_id, uint64_t owner_id,
		uint64_t tick, int32_t x, int32_t y, int32_t z, uint32_t original,
		uint32_t replacement, uint64_t voxel_revision,
		const WorldMiningDrop *drops, uint32_t drop_count,
		int32_t tool_slot, int32_t tool_item_id, uint64_t tool_instance_id,
		uint64_t inventory_revision,
		std::vector<uint8_t> &payload) noexcept
	{
		const bool equipped_tool = tool_slot == -2;
		const bool has_tool = tool_slot != -1 || tool_item_id != 0
			|| tool_instance_id != 0U || inventory_revision != 0U;
		if (request_id == 0U || voxel_revision == 0U || y < 0
			|| y >= GAME_VOXEL_CHUNK_HEIGHT || drop_count > MAX_MINING_DROPS
			|| (drop_count > 0U && drops == nullptr)
			|| voxel_block_is_known(original) != FT_TRUE
			|| voxel_block_is_known(replacement) != FT_TRUE
			|| original == replacement
			|| (has_tool && ((tool_slot < 0 && !equipped_tool)
				|| tool_item_id <= 0
				|| tool_instance_id == 0U || inventory_revision == 0U)))
			return (FT_ERR_INVALID_ARGUMENT);
		try
		{
			payload.reserve(has_tool ? MINING_TOOL_PAYLOAD_SIZE
				: MINING_PAYLOAD_SIZE);
			payload.insert(payload.end(), {'G', 'M', 'I', 'N'});
			append_u32(payload, !has_tool ? MINING_VERSION
				: equipped_tool ? MINING_EQUIPPED_TOOL_VERSION
				: MINING_TOOL_VERSION);
			append_u64(payload, request_id);
			append_u32(payload, static_cast<uint32_t>(x));
			append_u32(payload, static_cast<uint32_t>(y));
			append_u32(payload, static_cast<uint32_t>(z));
			append_u32(payload, original);
			append_u32(payload, replacement);
			append_u64(payload, voxel_revision);
			append_u64(payload, tick);
			append_u64(payload, owner_id);
			append_u32(payload, drop_count);
			for (uint32_t index = 0U; index < MAX_MINING_DROPS; ++index)
			{
				const int32_t item_id = index < drop_count
					? drops[index].item_id : 0;
				const int32_t quantity = index < drop_count
					? drops[index].quantity : 0;
				if (index < drop_count && (item_id <= 0 || quantity <= 0))
					return (FT_ERR_INVALID_ARGUMENT);
				append_u32(payload, static_cast<uint32_t>(item_id));
				append_u32(payload, static_cast<uint32_t>(quantity));
			}
			if (has_tool)
			{
				append_u64(payload, inventory_revision);
				append_u32(payload, equipped_tool
					? MINING_EQUIPPED_TOOL_SENTINEL
					: static_cast<uint32_t>(tool_slot));
				append_u32(payload, static_cast<uint32_t>(tool_item_id));
				append_u64(payload, tool_instance_id);
			}
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		return (payload.size() == (has_tool ? MINING_TOOL_PAYLOAD_SIZE
			: MINING_PAYLOAD_SIZE)
			? FT_ERR_SUCCESS : FT_ERR_INVALID_STATE);
	}
	static int32_t validate(const GameTransactionJournal::Record &record)
	{
		if (record.scope_id != MINING_SCOPE || record.transaction_type != MINING_TYPE
			|| record.request_id == 0U
			|| (record.payload.size() != MINING_PAYLOAD_SIZE
				&& record.payload.size() != MINING_TOOL_PAYLOAD_SIZE)
			|| record.payload[0] != 'G' || record.payload[1] != 'M'
			|| record.payload[2] != 'I' || record.payload[3] != 'N'
			|| (read_u32(record.payload, 4U) != MINING_VERSION
				&& read_u32(record.payload, 4U) != MINING_TOOL_VERSION
				&& read_u32(record.payload, 4U)
					!= MINING_EQUIPPED_TOOL_VERSION)
			|| ((read_u32(record.payload, 4U) == MINING_VERSION)
				!= (record.payload.size() == MINING_PAYLOAD_SIZE))
			|| read_u64(record.payload, 8U) != record.request_id
			|| read_u32(record.payload, 20U) >= GAME_VOXEL_CHUNK_HEIGHT
			|| voxel_block_is_known(read_u32(record.payload, 28U)) != FT_TRUE
			|| voxel_block_is_known(read_u32(record.payload, 32U)) != FT_TRUE
			|| read_u32(record.payload, 28U) == read_u32(record.payload, 32U)
			|| read_u64(record.payload, 36U) == 0U
			|| read_u32(record.payload, 60U) > MAX_MINING_DROPS)
			return (FT_ERR_INVALID_ARGUMENT);
		for (uint32_t index = 0U; index < MAX_MINING_DROPS; ++index)
		{
			const uint32_t item_id = read_u32(record.payload, 64U + index * 8U);
			const uint32_t quantity = read_u32(record.payload, 68U + index * 8U);
			if ((index < read_u32(record.payload, 60U)
				&& (item_id == 0U || quantity == 0U))
				|| (index >= read_u32(record.payload, 60U)
					&& (item_id != 0U || quantity != 0U)))
				return (FT_ERR_INVALID_ARGUMENT);
		}
		if (record.payload.size() == MINING_TOOL_PAYLOAD_SIZE
			&& (read_u64(record.payload, 80U) == 0U
				|| (read_u32(record.payload, 4U) == MINING_EQUIPPED_TOOL_VERSION
					? read_u32(record.payload, 88U)
						!= MINING_EQUIPPED_TOOL_SENTINEL
					: read_u32(record.payload, 88U) > INT32_MAX)
				|| read_u32(record.payload, 92U) == 0U
				|| read_u64(record.payload, 96U) == 0U))
			return (FT_ERR_INVALID_ARGUMENT);
		return (FT_ERR_SUCCESS);
	}
	static int32_t rollback_applied_mining(World &world,
		WorldItemStore &items, int32_t x, int32_t y, int32_t z,
		uint32_t original, uint32_t replacement, bool world_changed,
		bool override_changed, const uint64_t *spawned_ids,
		uint32_t spawned_count) noexcept
	{
		int32_t rollback_result = FT_ERR_SUCCESS;

		for (uint32_t index = 0U; index < spawned_count; ++index)
		{
			const int32_t despawn_result = items.despawn(spawned_ids[index], 1U);
			if (despawn_result != FT_ERR_SUCCESS
				&& despawn_result != FT_ERR_NOT_FOUND
				&& rollback_result == FT_ERR_SUCCESS)
				rollback_result = despawn_result;
		}
		if (world_changed)
		{
			const int32_t chunk_x = WorldCoordinates::floor_divide(x,
				GAME_VOXEL_CHUNK_WIDTH);
			const int32_t chunk_z = WorldCoordinates::floor_divide(z,
				GAME_VOXEL_CHUNK_DEPTH);
			const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
			const int32_t world_result = chunk == nullptr ? FT_ERR_NOT_FOUND
				: world.replace_block_state_at(x, y, z, replacement, original,
					chunk->voxel_revision);
			if (world_result != FT_ERR_SUCCESS
				&& rollback_result == FT_ERR_SUCCESS)
				rollback_result = world_result;
		}
		else if (override_changed)
		{
			const int32_t override_result = world.set_block_state_override(x, y,
				z, original);
			if (override_result != FT_ERR_SUCCESS
				&& rollback_result == FT_ERR_SUCCESS)
				rollback_result = override_result;
		}
		return (rollback_result);
	}
	static int32_t apply(const GameTransactionJournal::Record &record,
		World &world, MinecraftItemCatalog &catalog, WorldItemStore &items,
		game_inventory *inventory, game_character *character)
	{
		const int32_t x = static_cast<int32_t>(read_u32(record.payload, 16U));
		const int32_t y = static_cast<int32_t>(read_u32(record.payload, 20U));
		const int32_t z = static_cast<int32_t>(read_u32(record.payload, 24U));
		const uint32_t original = read_u32(record.payload, 28U);
		const uint32_t replacement = read_u32(record.payload, 32U);
		uint32_t current = GAME_VOXEL_AIR_BLOCK;
		bool world_changed = false;
		bool override_changed = false;
		if (!world.block_id_at(x, y, z, &current))
		{
			const int32_t result = world.set_block_state_override(x, y, z,
				replacement);
			if (result != FT_ERR_SUCCESS)
				return (result);
			override_changed = true;
			current = replacement;
			/* The override is already the durable participant for an unloaded
			 * location. Drops are still applied below. */
		}
		if (current == original)
		{
			const int32_t chunk_x = WorldCoordinates::floor_divide(x,
				GAME_VOXEL_CHUNK_WIDTH);
			const int32_t chunk_z = WorldCoordinates::floor_divide(z,
				GAME_VOXEL_CHUNK_DEPTH);
			const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
			if (chunk == nullptr)
				return (FT_ERR_NOT_FOUND);
			const int32_t result = world.replace_block_state_at(x, y, z,
				original, replacement, chunk->voxel_revision);
			if (result != FT_ERR_SUCCESS)
				return (result);
			world_changed = true;
		}
		else if (current != replacement)
			return (FT_ERR_INVALID_STATE);
		const uint64_t tick = read_u64(record.payload, 44U);
		const uint64_t owner = read_u64(record.payload, 52U);
		const uint32_t count = read_u32(record.payload, 60U);
		uint64_t spawned_ids[MAX_MINING_DROPS] = {0U, 0U};
		uint32_t spawned_count = 0U;
		for (uint32_t index = 0U; index < count; ++index)
		{
			const int32_t item_id = static_cast<int32_t>(
				read_u32(record.payload, 64U + index * 8U));
			const int32_t quantity = static_cast<int32_t>(
				read_u32(record.payload, 68U + index * 8U));
			ft_sharedptr<game_item> item;
			int32_t result = catalog.create_item(item_id, quantity, &item);
			if (result == FT_ERR_SUCCESS)
			{
				result = items.spawn(item, static_cast<double>(x) + 0.5,
					static_cast<double>(y) + 0.5, static_cast<double>(z) + 0.5,
					tick, owner, 0U, &spawned_ids[spawned_count]);
				if (result == FT_ERR_SUCCESS)
					++spawned_count;
			}
			if (result != FT_ERR_SUCCESS)
			{
				const int32_t rollback_result = rollback_applied_mining(world,
					items, x, y, z, original, replacement, world_changed,
					override_changed, spawned_ids, spawned_count);
				return (rollback_result == FT_ERR_SUCCESS ? result
					: rollback_result);
			}
		}
		if (record.payload.size() == MINING_TOOL_PAYLOAD_SIZE)
		{
			game_inventory_snapshot before = {};
			bool broken = false;
			int32_t result = FT_ERR_INVALID_STATE;
			game_inventory *tool_inventory = inventory;
			if (tool_inventory == nullptr && character != nullptr)
				tool_inventory = &character->get_inventory();
			if (tool_inventory != nullptr
				&& tool_inventory->get_snapshot(before) == FT_ERR_SUCCESS
				&& before.revision == read_u64(record.payload, 80U))
			{
				if (read_u32(record.payload, 4U)
					== MINING_EQUIPPED_TOOL_VERSION)
				{
					ft_sharedptr<game_item> *equipped_tool = character == nullptr
						? nullptr : character->get_equipped_item(EQUIP_WEAPON);
					if (equipped_tool == nullptr)
						result = character == nullptr ? FT_ERR_INVALID_STATE
							: character->get_error();
					else
					{
						if (*equipped_tool
							&& (*equipped_tool)->get_id()
								== static_cast<int32_t>(read_u32(
									record.payload, 92U))
							&& (*equipped_tool)->get_instance_id()
								== read_u64(record.payload, 96U))
						{
							game_inventory_item_snapshot tool = {};
							tool.item_id = (*equipped_tool)->get_id();
							tool.instance_id = (*equipped_tool)->get_instance_id();
							tool.stack_size = 1;
							tool.max_stack = 1;
							tool.modifier1_id =
								(*equipped_tool)->get_modifier1_id();
							tool.modifier1_value =
								(*equipped_tool)->get_modifier1_value();
							uint32_t durability = 0U;
							uint32_t maximum = 0U;
							MinecraftMiningToolClass tool_class =
								MINECRAFT_MINING_TOOL_NONE;
							uint32_t required_power = 0U;
							MinecraftEquipmentRule equipment_rule = {};
							result = catalog.resolve_block_mining_requirement(
								original, &tool_class, &required_power);
							if (result == FT_ERR_SUCCESS)
								result = catalog.resolve_equipment_rule(
									tool.item_id, &equipment_rule);
							const bool correct_class =
								(tool_class == MINECRAFT_MINING_TOOL_PICKAXE
									&& tool.item_id
										>= MinecraftItemCatalog::WOOD_PICKAXE_ITEM_ID
									&& tool.item_id
										<= MinecraftItemCatalog::DIAMOND_PICKAXE_ITEM_ID)
								|| (tool_class == MINECRAFT_MINING_TOOL_AXE
									&& tool.item_id
										>= MinecraftItemCatalog::WOOD_AXE_ITEM_ID
									&& tool.item_id
										<= MinecraftItemCatalog::DIAMOND_AXE_ITEM_ID);
							if (result == FT_ERR_SUCCESS
								&& (!correct_class
									|| equipment_rule.slot
										!= MINECRAFT_EQUIPMENT_MAIN_HAND
									|| equipment_rule.mining_power < required_power))
								result = FT_ERR_PERMISSION_DENIED;
							if (result == FT_ERR_SUCCESS)
								result = catalog.query_equipment_durability(tool,
									&durability, &maximum);
							if (result == FT_ERR_SUCCESS && durability == 0U)
								result = FT_ERR_INVALID_OPERATION;
							if (result == FT_ERR_SUCCESS)
							{
								(*equipped_tool)->set_modifier1_id(
									MinecraftItemCatalog::EQUIPMENT_DURABILITY_MODIFIER_ID);
								(*equipped_tool)->set_modifier1_value(
									durability > 1U
										? static_cast<int32_t>(durability - 1U) : 0);
							}
							(void)maximum;
							(void)broken;
						}
						(void)equipped_tool->destroy();
						delete equipped_tool;
					}
				}
				else
				{
					const int32_t slot = static_cast<int32_t>(
						read_u32(record.payload, 88U));
					std::vector<game_inventory_item_snapshot>::const_iterator tool =
						std::find_if(before.items.begin(), before.items.end(),
							[slot](const game_inventory_item_snapshot &entry)
							{ return (entry.slot == slot); });
					game_inventory_snapshot after = {};
					if (tool != before.items.end()
						&& tool->item_id == static_cast<int32_t>(
							read_u32(record.payload, 92U))
						&& tool->instance_id == read_u64(record.payload, 96U))
					{
						result = catalog.stage_equipment_damage(before, slot,
							before.revision, 1U, after, &broken);
						if (result == FT_ERR_SUCCESS)
							result = tool_inventory->restore_snapshot(after);
					}
				}
			}
			if (result != FT_ERR_SUCCESS)
			{
				const int32_t rollback_result = rollback_applied_mining(world,
					items, x, y, z, original, replacement, world_changed,
					override_changed, spawned_ids, spawned_count);
				return (rollback_result == FT_ERR_SUCCESS ? result
					: rollback_result);
			}
		}
		for (uint32_t index = 0U; index < spawned_count; ++index)
		{
			int32_t merged_quantity = 0;
			(void)items.merge_into_nearby(spawned_ids[index], 1U,
				static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5,
				static_cast<double>(z) + 0.5, owner, tick, &merged_quantity);
		}
		return (FT_ERR_SUCCESS);
	}
}

uint64_t WorldBlockMiningTransaction::journal_scope() noexcept
{
	return (MINING_SCOPE);
}
uint32_t WorldBlockMiningTransaction::transaction_type() noexcept
{
	return (MINING_TYPE);
}

int32_t WorldBlockMiningTransaction::execute(const char *snapshot_path,
	World &world, MinecraftItemCatalog &catalog, WorldItemStore &items,
	GameTransactionJournal &journal, uint64_t request_id, uint64_t owner_id,
	uint64_t server_tick, int32_t x, int32_t y, int32_t z,
	uint32_t expected_block_id, uint32_t replacement_block_id,
	uint64_t expected_voxel_revision, const WorldMiningDrop *drops,
	uint32_t drop_count, const GameSessionSnapshotWriter *snapshot_writer,
	game_inventory *inventory, int32_t tool_slot, int32_t tool_item_id,
	uint64_t tool_instance_id, uint64_t expected_inventory_revision,
	game_character *character)
	noexcept
{
	std::vector<uint8_t> payload;
	std::vector<uint8_t> journal_before;
	GameTransactionJournal::Record existing = {};
	uint64_t sequence = 0U;
	bool replayed = false;
	int32_t result = encode(request_id, owner_id, server_tick, x, y, z,
		expected_block_id, replacement_block_id, expected_voxel_revision,
		drops, drop_count, tool_slot, tool_item_id, tool_instance_id,
		expected_inventory_revision, payload);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = journal.find(MINING_SCOPE, request_id, &existing);
	if (result == FT_ERR_SUCCESS)
	{
		if (existing.transaction_type != MINING_TYPE || existing.payload != payload)
			return (FT_ERR_INVALID_ARGUMENT);
		if (existing.state == GameTransactionJournal::COMMITTED)
			return (FT_ERR_SUCCESS);
		if (existing.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_STATE);
	}
	else if (result != FT_ERR_NOT_FOUND)
		return (result);
	result = journal.serialize(journal_before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (existing.state != GameTransactionJournal::PREPARED)
	{
		result = journal.prepare(MINING_SCOPE, request_id, MINING_TYPE,
			payload.data(), static_cast<uint32_t>(payload.size()), &sequence,
			&replayed);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	result = checkpoint(snapshot_path, snapshot_writer);
	if (result != FT_ERR_SUCCESS)
	{
		(void)journal.deserialize(journal_before.data(), journal_before.size());
		return (result);
	}
	GameTransactionJournal::Record prepared_record = {};
	prepared_record.sequence = sequence;
	prepared_record.scope_id = MINING_SCOPE;
	prepared_record.request_id = request_id;
	prepared_record.transaction_type = MINING_TYPE;
	prepared_record.state = GameTransactionJournal::PREPARED;
	try
	{
		prepared_record.payload = payload;
	}
	catch (...)
	{
		(void)journal.deserialize(journal_before.data(), journal_before.size());
		(void)checkpoint(snapshot_path, snapshot_writer);
		return (FT_ERR_NO_MEMORY);
	}
	result = apply(existing.state == GameTransactionJournal::PREPARED
		? existing : prepared_record, world, catalog, items, inventory,
		character);
	if (result != FT_ERR_SUCCESS)
	{
		/* Keep the prepared intent durable. If a participant could not roll back,
		 * startup recovery can finish the block and drops as one transaction. */
		(void)checkpoint(snapshot_path, snapshot_writer);
		return (result);
	}
	result = journal.mark_committed(MINING_SCOPE, request_id);
	if (result == FT_ERR_SUCCESS)
		result = journal.prune_terminal_scope(MINING_SCOPE, request_id);
	if (result == FT_ERR_SUCCESS)
		result = checkpoint(snapshot_path, snapshot_writer);
	return (result);
}

int32_t WorldBlockMiningTransaction::recover_pending(const char *snapshot_path,
	World &world, MinecraftItemCatalog &catalog, WorldItemStore &items,
	GameTransactionJournal &journal,
	const GameSessionSnapshotWriter *snapshot_writer,
	game_inventory *inventory, game_character *character) noexcept
{
	std::vector<GameTransactionJournal::Record> pending;
	uint64_t highest_request_id = 0U;
	int32_t result = journal.pending_records(pending);
	if (result != FT_ERR_SUCCESS)
		return (result);
	for (const GameTransactionJournal::Record &record : pending)
	{
		if (record.scope_id != MINING_SCOPE)
			continue ;
		result = validate(record);
		if (result != FT_ERR_SUCCESS
			|| record.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_ARGUMENT);
		result = apply(record, world, catalog, items, inventory, character);
		if (result == FT_ERR_SUCCESS)
			result = journal.mark_committed(MINING_SCOPE, record.request_id);
		if (result == FT_ERR_SUCCESS)
			result = checkpoint(snapshot_path, snapshot_writer);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (record.request_id > highest_request_id)
			highest_request_id = record.request_id;
	}
	if (highest_request_id != 0U)
	{
		result = journal.prune_terminal_scope(MINING_SCOPE, highest_request_id);
		if (result == FT_ERR_SUCCESS)
			result = checkpoint(snapshot_path, snapshot_writer);
	}
	return (result);
}
