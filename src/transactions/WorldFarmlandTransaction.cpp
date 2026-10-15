#include "WorldFarmlandTransaction.hpp"

#include "../inventory/MinecraftItemCatalog.hpp"
#include "../coordinates/WorldCoordinates.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <algorithm>
#include <vector>

namespace
{
	static const uint64_t FARMLAND_TILL_SCOPE = 0x574F524C4454494CLL;
	static const uint32_t FARMLAND_TILL_TYPE = 9U;
	static const uint32_t FARMLAND_TILL_VERSION = 1U;
	static const uint32_t FARMLAND_TILL_DURABILITY_VERSION = 2U;
	static const std::size_t FARMLAND_TILL_PAYLOAD_SIZE = 56U;
	static const std::size_t FARMLAND_TILL_DURABILITY_PAYLOAD_SIZE = 64U;

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

	static int32_t save_checkpoint(const char *snapshot_path,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept
	{
		if (snapshot_path == nullptr || snapshot_path[0] == '\0')
			return (FT_ERR_INVALID_ARGUMENT);
		if (snapshot_writer == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		return (snapshot_writer->save());
	}

	static int32_t make_payload(uint64_t request_id, int32_t inventory_slot,
		uint64_t inventory_revision, int32_t world_x, int32_t world_y,
		int32_t world_z, uint64_t voxel_revision, uint64_t tool_instance_id,
		std::vector<uint8_t> &payload) noexcept
	{
		try
		{
			payload.reserve(tool_instance_id != 0U
				? FARMLAND_TILL_DURABILITY_PAYLOAD_SIZE
				: FARMLAND_TILL_PAYLOAD_SIZE);
			payload.insert(payload.end(), {'G', 'T', 'I', 'L'});
			append_u32(payload, tool_instance_id != 0U
				? FARMLAND_TILL_DURABILITY_VERSION : FARMLAND_TILL_VERSION);
			append_u64(payload, request_id);
			append_u32(payload, static_cast<uint32_t>(inventory_slot));
			append_u32(payload, static_cast<uint32_t>(
				MinecraftItemCatalog::WOOD_HOE_ITEM_ID));
			append_u32(payload, 0U);
			append_u64(payload, inventory_revision);
			append_u32(payload, static_cast<uint32_t>(world_x));
			append_u32(payload, static_cast<uint32_t>(world_y));
			append_u32(payload, static_cast<uint32_t>(world_z));
			append_u64(payload, voxel_revision);
			if (tool_instance_id != 0U)
				append_u64(payload, tool_instance_id);
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		return (payload.size() == (tool_instance_id != 0U
			? FARMLAND_TILL_DURABILITY_PAYLOAD_SIZE
			: FARMLAND_TILL_PAYLOAD_SIZE)
			? FT_ERR_SUCCESS : FT_ERR_INVALID_STATE);
	}

	static int32_t validate_record(const GameTransactionJournal::Record &record)
	{
		if (record.scope_id != FARMLAND_TILL_SCOPE
			|| record.transaction_type != FARMLAND_TILL_TYPE
			|| record.request_id == 0U
			|| (record.payload.size() != FARMLAND_TILL_PAYLOAD_SIZE
				&& record.payload.size() != FARMLAND_TILL_DURABILITY_PAYLOAD_SIZE)
			|| record.payload[0] != 'G' || record.payload[1] != 'T'
			|| record.payload[2] != 'I' || record.payload[3] != 'L'
			|| (read_u32(record.payload, 4U) != FARMLAND_TILL_VERSION
				&& read_u32(record.payload, 4U) != FARMLAND_TILL_DURABILITY_VERSION)
			|| ((read_u32(record.payload, 4U) == FARMLAND_TILL_VERSION)
				!= (record.payload.size() == FARMLAND_TILL_PAYLOAD_SIZE))
			|| read_u64(record.payload, 8U) != record.request_id
			|| static_cast<int32_t>(read_u32(record.payload, 16U)) < 0
			|| read_u32(record.payload, 20U)
				!= static_cast<uint32_t>(MinecraftItemCatalog::WOOD_HOE_ITEM_ID)
			|| read_u32(record.payload, 24U) != 0U
			|| read_u64(record.payload, 28U) == 0U
			|| read_u64(record.payload, 48U) == 0U)
			return (FT_ERR_INVALID_ARGUMENT);
		if (record.payload.size() == FARMLAND_TILL_DURABILITY_PAYLOAD_SIZE
			&& read_u64(record.payload, 56U) == 0U)
			return (FT_ERR_INVALID_ARGUMENT);
		return (FT_ERR_SUCCESS);
	}

	static int32_t damage_recorded_hoe(const GameTransactionJournal::Record &record,
		game_character *character, MinecraftItemCatalog *catalog) noexcept
	{
		if (record.payload.size() == FARMLAND_TILL_PAYLOAD_SIZE)
			return (FT_ERR_SUCCESS);
		if (character == nullptr || catalog == nullptr)
			return (FT_ERR_INVALID_STATE);
		game_inventory_snapshot before = {};
		game_inventory_snapshot after = {};
		int32_t result = character->get_inventory().get_snapshot(before);
		if (result != FT_ERR_SUCCESS)
			return (result);
		const uint64_t expected_revision = read_u64(record.payload, 28U);
		if (before.revision != expected_revision)
			return (FT_ERR_INVALID_STATE);
		const int32_t slot = static_cast<int32_t>(read_u32(record.payload, 16U));
		std::vector<game_inventory_item_snapshot>::const_iterator hoe =
			std::find_if(before.items.begin(), before.items.end(),
				[slot](const game_inventory_item_snapshot &item)
				{ return (item.slot == slot); });
		if (hoe == before.items.end()
			|| hoe->item_id != MinecraftItemCatalog::WOOD_HOE_ITEM_ID
			|| hoe->instance_id != read_u64(record.payload, 56U))
			return (FT_ERR_INVALID_STATE);
		bool broken = false;
		result = catalog->stage_equipment_damage(before, slot,
			expected_revision, 1U, after, &broken);
		if (result == FT_ERR_SUCCESS)
			result = character->get_inventory().restore_snapshot(after);
		(void)broken;
		return (result);
	}
}

uint64_t WorldFarmlandTransaction::journal_scope() noexcept
{
	return (FARMLAND_TILL_SCOPE);
}

uint32_t WorldFarmlandTransaction::transaction_type() noexcept
{
	return (FARMLAND_TILL_TYPE);
}

int32_t WorldFarmlandTransaction::execute(const char *snapshot_path,
	game_character &character, World &world, GameTransactionJournal &journal,
	uint64_t request_id, int32_t inventory_slot,
	uint64_t expected_inventory_revision, int32_t world_x, int32_t world_y,
	int32_t world_z, uint64_t expected_voxel_revision,
	const GameSessionSnapshotWriter *snapshot_writer,
	MinecraftItemCatalog *catalog) noexcept
{
	game_inventory_snapshot inventory_snapshot = {};
	std::vector<uint8_t> payload;
	std::vector<uint8_t> journal_before;
	GameTransactionJournal::Record existing = {};
	uint64_t sequence = 0U;
	bool replayed = false;
	uint32_t current_block_id = GAME_VOXEL_AIR_BLOCK;
	uint32_t original_soil_id = GAME_VOXEL_AIR_BLOCK;
	int32_t result;
	bool world_changed = false;
	bool inventory_changed = false;
	uint64_t tool_instance_id = 0U;

	if (request_id == 0U || inventory_slot < 0
		|| expected_inventory_revision == 0U || expected_voxel_revision == 0U
		|| world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT)
		return (FT_ERR_INVALID_ARGUMENT);
	result = journal.find(FARMLAND_TILL_SCOPE, request_id, &existing);
	if (result == FT_ERR_SUCCESS)
	{
		if (validate_record(existing) != FT_ERR_SUCCESS
			|| static_cast<int32_t>(read_u32(existing.payload, 16U))
				!= inventory_slot
			|| read_u64(existing.payload, 28U) != expected_inventory_revision
			|| static_cast<int32_t>(read_u32(existing.payload, 36U)) != world_x
			|| static_cast<int32_t>(read_u32(existing.payload, 40U)) != world_y
			|| static_cast<int32_t>(read_u32(existing.payload, 44U)) != world_z
			|| read_u64(existing.payload, 48U) != expected_voxel_revision)
			return (FT_ERR_INVALID_ARGUMENT);
		if (existing.state == GameTransactionJournal::COMMITTED)
			return (FT_ERR_SUCCESS);
		if (existing.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_STATE);
		if (existing.payload.size() == FARMLAND_TILL_DURABILITY_PAYLOAD_SIZE)
			tool_instance_id = read_u64(existing.payload, 56U);
	}
	else if (result != FT_ERR_NOT_FOUND)
		return (result);
	result = character.get_inventory().get_snapshot(inventory_snapshot);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (inventory_snapshot.revision != expected_inventory_revision)
		return (FT_ERR_INVALID_STATE);
	auto hoe = std::find_if(inventory_snapshot.items.begin(),
		inventory_snapshot.items.end(), [inventory_slot](
			const game_inventory_item_snapshot &item)
		{
			return (item.slot == inventory_slot
				&& item.item_id == MinecraftItemCatalog::WOOD_HOE_ITEM_ID
				&& item.stack_size == 1 && item.max_stack == 1);
		});
	if (hoe == inventory_snapshot.items.end())
		return (FT_ERR_PERMISSION_DENIED);
	if (catalog != nullptr
		&& (existing.sequence == 0U || tool_instance_id != 0U))
	{
		uint32_t durability = 0U;
		uint32_t maximum = 0U;
		if (hoe->instance_id == 0U
			|| catalog->query_equipment_durability(*hoe, &durability,
				&maximum) != FT_ERR_SUCCESS || durability == 0U)
			return (FT_ERR_INVALID_STATE);
		(void)maximum;
		if (existing.sequence != 0U
			&& hoe->instance_id != tool_instance_id)
			return (FT_ERR_INVALID_STATE);
		if (existing.sequence == 0U)
			tool_instance_id = hoe->instance_id;
	}
	result = make_payload(request_id, inventory_slot,
		expected_inventory_revision, world_x, world_y, world_z,
		expected_voxel_revision, tool_instance_id, payload);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (existing.sequence != 0U)
	{
		if (existing.payload != payload)
			return (FT_ERR_INVALID_ARGUMENT);
	}
	if (existing.state == GameTransactionJournal::PREPARED
		&& world.block_id_at(world_x, world_y, world_z, &current_block_id)
		&& current_block_id == VOXEL_GENERATOR_FARMLAND_BLOCK_ID)
	{
		result = damage_recorded_hoe(existing, &character, catalog);
		if (result == FT_ERR_SUCCESS)
			result = journal.mark_committed(FARMLAND_TILL_SCOPE, request_id);
		if (result == FT_ERR_SUCCESS)
			result = save_checkpoint(snapshot_path, snapshot_writer);
		return (result);
	}
	if (!world.block_id_at(world_x, world_y, world_z, &original_soil_id)
		|| (original_soil_id != VOXEL_GENERATOR_DIRT_BLOCK_ID
			&& original_soil_id != VOXEL_GENERATOR_GRASS_BLOCK_ID))
		return (FT_ERR_PERMISSION_DENIED);
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
	if (chunk == nullptr)
		return (FT_ERR_NOT_FOUND);
	if (chunk->voxel_revision != expected_voxel_revision)
		return (FT_ERR_INVALID_STATE);
	result = journal.serialize(journal_before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (existing.state != GameTransactionJournal::PREPARED)
	{
		result = journal.prepare(FARMLAND_TILL_SCOPE, request_id,
			FARMLAND_TILL_TYPE, payload.data(),
			static_cast<uint32_t>(payload.size()), &sequence, &replayed);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	result = save_checkpoint(snapshot_path, snapshot_writer);
	if (result == FT_ERR_SUCCESS)
	{
		result = world.till_farmland_at(world_x, world_y, world_z,
			expected_voxel_revision);
		world_changed = result == FT_ERR_SUCCESS;
	}
	if (result == FT_ERR_SUCCESS && tool_instance_id != 0U)
	{
		GameTransactionJournal::Record prepared = {};
		result = journal.find(FARMLAND_TILL_SCOPE, request_id, &prepared);
		if (result == FT_ERR_SUCCESS)
		{
			result = damage_recorded_hoe(prepared, &character, catalog);
			inventory_changed = result == FT_ERR_SUCCESS;
		}
	}
	if (result == FT_ERR_SUCCESS)
		result = journal.mark_committed(FARMLAND_TILL_SCOPE, request_id);
	if (result == FT_ERR_SUCCESS)
		result = save_checkpoint(snapshot_path, snapshot_writer);
	if (result == FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	int32_t rollback_result = FT_ERR_SUCCESS;
	if (world_changed)
	{
		const WorldChunk *changed = world.find_chunk(chunk_x, chunk_z);
		rollback_result = changed == nullptr ? FT_ERR_NOT_FOUND
			: world.replace_block_state_at(world_x, world_y, world_z,
				VOXEL_GENERATOR_FARMLAND_BLOCK_ID, original_soil_id,
				changed->voxel_revision);
	}
	if (inventory_changed)
	{
		const int32_t inventory_rollback = character.get_inventory()
			.restore_snapshot(inventory_snapshot);
		if (rollback_result == FT_ERR_SUCCESS
			&& inventory_rollback != FT_ERR_SUCCESS)
			rollback_result = inventory_rollback;
	}
	if (rollback_result != FT_ERR_SUCCESS)
		return (rollback_result);
	(void)journal.deserialize(journal_before.data(), journal_before.size());
	(void)save_checkpoint(snapshot_path, snapshot_writer);
	return (result);
}

int32_t WorldFarmlandTransaction::recover_pending(const char *snapshot_path,
	World &world, GameTransactionJournal &journal,
	const GameSessionSnapshotWriter *snapshot_writer,
	game_character *character, MinecraftItemCatalog *catalog) noexcept
{
	std::vector<GameTransactionJournal::Record> pending;
	int32_t result = journal.pending_records(pending);
	if (result != FT_ERR_SUCCESS)
		return (result);
	for (const GameTransactionJournal::Record &record : pending)
	{
		if (record.scope_id != FARMLAND_TILL_SCOPE)
			continue ;
		result = validate_record(record);
		if (result != FT_ERR_SUCCESS
			|| record.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_ARGUMENT);
		const int32_t world_x = static_cast<int32_t>(
			read_u32(record.payload, 36U));
		const int32_t world_y = static_cast<int32_t>(
			read_u32(record.payload, 40U));
		const int32_t world_z = static_cast<int32_t>(
			read_u32(record.payload, 44U));
		if (world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT)
			return (FT_ERR_INVALID_ARGUMENT);
		uint32_t current = GAME_VOXEL_AIR_BLOCK;
		const bool block_loaded = world.block_id_at(world_x, world_y, world_z,
			&current);
		if (block_loaded && current != VOXEL_GENERATOR_DIRT_BLOCK_ID
			&& current != VOXEL_GENERATOR_GRASS_BLOCK_ID
			&& current != VOXEL_GENERATOR_FARMLAND_BLOCK_ID)
			return (FT_ERR_INVALID_STATE);
		if (block_loaded && current != VOXEL_GENERATOR_FARMLAND_BLOCK_ID)
		{
			const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
				GAME_VOXEL_CHUNK_WIDTH);
			const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
				GAME_VOXEL_CHUNK_DEPTH);
			const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
			result = chunk == nullptr ? FT_ERR_NOT_FOUND
				: world.till_farmland_at(world_x, world_y, world_z,
					chunk->voxel_revision);
		}
		else if (block_loaded)
			result = FT_ERR_SUCCESS;
		else
			result = world.set_block_state_override(world_x, world_y, world_z,
				VOXEL_GENERATOR_FARMLAND_BLOCK_ID);
		if (result == FT_ERR_SUCCESS)
			result = damage_recorded_hoe(record, character, catalog);
		if (result == FT_ERR_SUCCESS)
			result = journal.mark_committed(FARMLAND_TILL_SCOPE,
				record.request_id);
		if (result == FT_ERR_SUCCESS)
			result = save_checkpoint(snapshot_path, snapshot_writer);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	return (FT_ERR_SUCCESS);
}
