#include "WorldProjectileLaunchTransaction.hpp"

#include "../entities/EntityState.hpp"
#include "../entities/WorldProjectileSimulation.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
	static const uint64_t PROJECTILE_SCOPE_SALT = 0x4152524F574C4E43ULL;
	static const uint32_t PROJECTILE_TRANSACTION_TYPE = 17U;
	static const uint32_t PROJECTILE_PAYLOAD_VERSION = 2U;
	static const std::size_t PROJECTILE_PAYLOAD_V1_SIZE = 92U;
	static const std::size_t PROJECTILE_PAYLOAD_SIZE = 100U;
	static const double PROJECTILE_SPEED = 1.25;
	static const uint64_t PROJECTILE_COOLDOWN_TICKS = 10U;

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
	static void append_double(std::vector<uint8_t> &bytes, double value)
	{
		uint64_t bits = 0U;
		std::memcpy(&bits, &value, sizeof(bits));
		append_u64(bytes, bits);
	}
	static double read_double(const std::vector<uint8_t> &bytes,
		std::size_t offset) noexcept
	{
		const uint64_t bits = read_u64(bytes, offset);
		double value = 0.0;
		std::memcpy(&value, &bits, sizeof(value));
		return (value);
	}
	static bool payload_is_valid(const GameTransactionJournal::Record &record)
	{
		const uint32_t version = record.payload.size() >= 8U
			? read_u32(record.payload, 4U) : 0U;
		uint64_t expected_scope = 0U;
		const bool valid_size =
			(version == 1U && record.payload.size() == PROJECTILE_PAYLOAD_V1_SIZE)
			|| (version == PROJECTILE_PAYLOAD_VERSION
				&& record.payload.size() == PROJECTILE_PAYLOAD_SIZE);
		if (version == PROJECTILE_PAYLOAD_VERSION && valid_size)
		{
			expected_scope = PROJECTILE_SCOPE_SALT
				^ read_u64(record.payload, 92U);
			if (expected_scope == 0U)
				expected_scope = PROJECTILE_SCOPE_SALT;
		}
		return (record.transaction_type == PROJECTILE_TRANSACTION_TYPE
			&& valid_size
			&& (version != PROJECTILE_PAYLOAD_VERSION
				|| record.scope_id == expected_scope)
			&& record.payload[0] == 'G' && record.payload[1] == 'P'
			&& record.payload[2] == 'R' && record.payload[3] == 'J'
			&& read_u64(record.payload, 8U) == record.request_id
			&& record.request_id != 0U && read_u64(record.payload, 16U) != 0U
			&& read_u32(record.payload, 32U)
				== static_cast<uint32_t>(MinecraftItemCatalog::BOW_ITEM_ID)
			&& read_u64(record.payload, 36U) != 0U
			&& std::isfinite(read_double(record.payload, 44U))
			&& std::isfinite(read_double(record.payload, 52U))
			&& std::isfinite(read_double(record.payload, 60U))
			&& std::isfinite(read_double(record.payload, 68U))
			&& std::isfinite(read_double(record.payload, 76U))
			&& std::isfinite(read_double(record.payload, 84U)));
	}
	static int32_t apply_prepared(const char *snapshot_path,
		game_character &character, WorldEntityStore &entities,
		MinecraftItemCatalog &catalog, GameTransactionJournal &journal,
		const GameTransactionJournal::Record &record,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept
	{
		game_inventory &inventory = character.get_inventory();
		game_inventory_snapshot inventory_before = {};
		std::vector<uint8_t> entities_before;
		std::vector<uint8_t> journal_before;
		uint64_t inventory_revision = 0U;
		uint32_t active_projectiles = 0U;
		uint32_t spawned_id = 0U;
		int32_t result = FT_ERR_SUCCESS;
		if (snapshot_path == nullptr || snapshot_path[0] == '\0'
			|| snapshot_writer == nullptr || !payload_is_valid(record)
			|| record.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_ARGUMENT);
		result = inventory.get_snapshot(inventory_before);
		if (result == FT_ERR_SUCCESS)
			result = inventory.get_revision(&inventory_revision);
		if (result == FT_ERR_SUCCESS
			&& inventory_revision != read_u64(record.payload, 16U))
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
			result = entities.serialize(entities_before);
		std::vector<WorldEntitySnapshot> existing_entities;
		if (result == FT_ERR_SUCCESS)
			result = entities.snapshot(existing_entities);
		if (result == FT_ERR_SUCCESS)
		{
			for (const WorldEntitySnapshot &entity : existing_entities)
				if (entity.state.type == static_cast<uint32_t>(
						EntityState::Kind::PROJECTILE))
					++active_projectiles;
			if (active_projectiles >= WorldProjectileSimulation::MAXIMUM_ACTIVE_PROJECTILES)
				result = FT_ERR_FULL;
		}
		ft_sharedptr<game_item> *bow = character.get_equipped_item(EQUIP_WEAPON);
		if (result == FT_ERR_SUCCESS && bow == nullptr)
			result = character.get_error();
		int32_t old_modifier_id = 0;
		int32_t old_modifier_value = 0;
		uint32_t durability = 0U;
		MinecraftEquipmentRule bow_rule = {};
		if (result == FT_ERR_SUCCESS && !(*bow))
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
		{
			old_modifier_id = (*bow)->get_modifier1_id();
			old_modifier_value = (*bow)->get_modifier1_value();
		if ((*bow)->get_item_id() != MinecraftItemCatalog::BOW_ITEM_ID
				|| (*bow)->get_instance_id() != read_u64(record.payload, 36U))
				result = FT_ERR_INVALID_STATE;
		}
		if (result == FT_ERR_SUCCESS)
			result = catalog.resolve_equipment_rule(
				MinecraftItemCatalog::BOW_ITEM_ID, &bow_rule);
		if (result == FT_ERR_SUCCESS
			&& (bow_rule.slot != MINECRAFT_EQUIPMENT_MAIN_HAND
				|| bow_rule.durability == 0U
				|| bow_rule.attack_power == 0U
				|| bow_rule.attack_power > static_cast<uint32_t>(
					std::numeric_limits<int32_t>::max())))
			result = FT_ERR_INVALID_STATE;
		durability = bow_rule.durability;
		if (result == FT_ERR_SUCCESS && old_modifier_id != 0)
		{
			if (old_modifier_id
				!= MinecraftItemCatalog::EQUIPMENT_DURABILITY_MODIFIER_ID
				|| old_modifier_value < 0
				|| static_cast<uint32_t>(old_modifier_value) > bow_rule.durability)
				result = FT_ERR_INVALID_STATE;
			else
				durability = static_cast<uint32_t>(old_modifier_value);
		}
		if (result == FT_ERR_SUCCESS && durability == 0U)
			result = FT_ERR_INVALID_OPERATION;
		if (result == FT_ERR_SUCCESS)
			result = journal.serialize(journal_before);
		game_inventory_quantity_result consumed = {};
		if (result == FT_ERR_SUCCESS)
			result = inventory.remove_item_quantity(
				MinecraftItemCatalog::ARROW_ITEM_ID, 1,
				read_u64(record.payload, 16U), record.scope_id,
				record.request_id, &consumed);
		if (result == FT_ERR_SUCCESS)
		{
			EntityState projectile;
			projectile.type = static_cast<uint32_t>(EntityState::Kind::PROJECTILE);
			projectile.position_x = read_double(record.payload, 44U)
				+ read_double(record.payload, 68U) * 0.5;
			projectile.position_y = read_double(record.payload, 52U)
				+ read_double(record.payload, 76U) * 0.5;
			projectile.position_z = read_double(record.payload, 60U)
				+ read_double(record.payload, 84U) * 0.5;
			projectile.velocity_x = read_double(record.payload, 68U)
				* PROJECTILE_SPEED;
			projectile.velocity_y = read_double(record.payload, 76U)
				* PROJECTILE_SPEED;
			projectile.velocity_z = read_double(record.payload, 84U)
				* PROJECTILE_SPEED;
			projectile.orientation_yaw = 0.0;
			projectile.orientation_pitch = 0.0;
			projectile.health = static_cast<int32_t>(bow_rule.attack_power);
			projectile.tick = read_u64(record.payload, 24U);
			const uint64_t owner_actor_id = read_u32(record.payload, 4U) >= 2U
				? read_u64(record.payload, 92U) : 0U;
			result = entities.spawn(projectile, owner_actor_id, &spawned_id);
		}
		if (result == FT_ERR_SUCCESS)
		{
			(*bow)->set_modifier1_id(
				MinecraftItemCatalog::EQUIPMENT_DURABILITY_MODIFIER_ID);
			(*bow)->set_modifier1_value(durability > 1U
				? static_cast<int32_t>(durability - 1U) : 0);
			result = journal.mark_committed(record.scope_id, record.request_id);
		}
		if (result == FT_ERR_SUCCESS)
			result = snapshot_writer->save();
		if (result != FT_ERR_SUCCESS && journal_before.empty())
		{
			const int32_t operation_error = result;
			int32_t abort_result = journal.mark_aborted(record.scope_id,
				record.request_id);
			if (abort_result == FT_ERR_SUCCESS)
				abort_result = snapshot_writer->save();
			if (bow != nullptr)
			{
				(void)bow->destroy();
				delete bow;
			}
			return (abort_result == FT_ERR_SUCCESS
				? operation_error : abort_result);
		}
		if (result != FT_ERR_SUCCESS)
		{
			int32_t rollback_result = inventory.restore_snapshot(inventory_before);
			if (spawned_id != 0U)
			{
				const int32_t entity_restore_result = entities.deserialize(
					entities_before.data(), entities_before.size());
				if (rollback_result == FT_ERR_SUCCESS
					&& entity_restore_result != FT_ERR_SUCCESS)
					rollback_result = entity_restore_result;
			}
			if (bow != nullptr && *bow)
			{
				(*bow)->set_modifier1_id(old_modifier_id);
				(*bow)->set_modifier1_value(old_modifier_value);
			}
			const int32_t journal_result = journal.deserialize(
				journal_before.data(), journal_before.size());
			if (rollback_result == FT_ERR_SUCCESS
				&& journal_result != FT_ERR_SUCCESS)
				rollback_result = journal_result;
			const int32_t checkpoint_result = snapshot_writer->save();
			if (rollback_result == FT_ERR_SUCCESS
				&& checkpoint_result != FT_ERR_SUCCESS)
				rollback_result = checkpoint_result;
			result = rollback_result == FT_ERR_SUCCESS ? result : rollback_result;
		}
		if (bow != nullptr)
		{
			(void)bow->destroy();
			delete bow;
		}
		return (result);
	}
	static int32_t make_payload(uint64_t request_id,
		uint64_t inventory_revision, uint64_t server_tick,
		uint64_t bow_instance_id, uint64_t actor_id,
		double origin_x, double origin_y, double origin_z, double direction_x,
		double direction_y, double direction_z,
		std::vector<uint8_t> &payload) noexcept
	{
		if (request_id == 0U || inventory_revision == 0U
			|| bow_instance_id == 0U || !std::isfinite(origin_x)
			|| !std::isfinite(origin_y) || !std::isfinite(origin_z)
			|| !std::isfinite(direction_x) || !std::isfinite(direction_y)
			|| !std::isfinite(direction_z))
			return (FT_ERR_INVALID_ARGUMENT);
		try
		{
			payload.reserve(PROJECTILE_PAYLOAD_SIZE);
			payload.insert(payload.end(), {'G', 'P', 'R', 'J'});
			append_u32(payload, PROJECTILE_PAYLOAD_VERSION);
			append_u64(payload, request_id);
			append_u64(payload, inventory_revision);
			append_u64(payload, server_tick);
			append_u32(payload,
				static_cast<uint32_t>(MinecraftItemCatalog::BOW_ITEM_ID));
			append_u64(payload, bow_instance_id);
			append_double(payload, origin_x);
			append_double(payload, origin_y);
			append_double(payload, origin_z);
			append_double(payload, direction_x);
			append_double(payload, direction_y);
			append_double(payload, direction_z);
			append_u64(payload, actor_id);
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		return (payload.size() == PROJECTILE_PAYLOAD_SIZE
			? FT_ERR_SUCCESS : FT_ERR_INVALID_STATE);
	}
}

uint64_t WorldProjectileLaunchTransaction::journal_scope(uint64_t actor_id) noexcept
{
	const uint64_t scope = PROJECTILE_SCOPE_SALT ^ actor_id;
	return (scope == 0U ? PROJECTILE_SCOPE_SALT : scope);
}

uint32_t WorldProjectileLaunchTransaction::transaction_type() noexcept
{
	return (PROJECTILE_TRANSACTION_TYPE);
}

int32_t WorldProjectileLaunchTransaction::launch(const char *snapshot_path,
	game_character &character, WorldEntityStore &entities,
	MinecraftItemCatalog &catalog, GameTransactionJournal &journal,
	uint64_t actor_id, uint64_t request_id,
	uint64_t expected_inventory_revision, uint64_t server_tick,
	double origin_x, double origin_y, double origin_z, double direction_x,
	double direction_y, double direction_z,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	const uint64_t scope = WorldProjectileLaunchTransaction::journal_scope(actor_id);
	GameTransactionJournal::Record existing = {};
	std::vector<uint8_t> payload;
	std::vector<uint8_t> journal_before;
	int32_t result = FT_ERR_SUCCESS;
	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| snapshot_writer == nullptr || request_id == 0U
		|| expected_inventory_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	result = journal.find(scope, request_id, &existing);
	if (result == FT_ERR_SUCCESS)
	{
		if (!payload_is_valid(existing)
			|| (existing.state != GameTransactionJournal::PREPARED
				&& existing.state != GameTransactionJournal::COMMITTED)
			|| read_u64(existing.payload, 16U) != expected_inventory_revision)
			return (FT_ERR_INVALID_ARGUMENT);
		if (existing.state == GameTransactionJournal::COMMITTED)
			return (FT_ERR_SUCCESS);
		return (apply_prepared(snapshot_path, character, entities, catalog,
			journal, existing, snapshot_writer));
	}
	if (result != FT_ERR_NOT_FOUND)
		return (result);
	GameTransactionJournal::Record latest = {};
	result = journal.latest(scope, PROJECTILE_TRANSACTION_TYPE, &latest);
	if (result == FT_ERR_SUCCESS)
	{
		if (!payload_is_valid(latest))
			return (FT_ERR_INVALID_STATE);
		if (latest.state == GameTransactionJournal::COMMITTED)
		{
			const uint64_t last_tick = read_u64(latest.payload, 24U);
			const uint64_t next_allowed_tick = last_tick
				> UINT64_MAX - PROJECTILE_COOLDOWN_TICKS ? UINT64_MAX
				: last_tick + PROJECTILE_COOLDOWN_TICKS;
			if (server_tick < next_allowed_tick)
				return (FT_ERR_INVALID_OPERATION);
		}
		else if (latest.state != GameTransactionJournal::ABORTED)
			return (FT_ERR_INVALID_STATE);
	}
	else if (result != FT_ERR_NOT_FOUND)
		return (result);
	game_inventory &inventory = character.get_inventory();
	uint64_t inventory_revision = 0U;
	result = inventory.get_revision(&inventory_revision);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (inventory_revision != expected_inventory_revision)
		return (FT_ERR_INVALID_STATE);
	ft_sharedptr<game_item> *bow = character.get_equipped_item(EQUIP_WEAPON);
	if (bow == nullptr)
		return (character.get_error());
	if (!(*bow) || (*bow)->get_item_id() != MinecraftItemCatalog::BOW_ITEM_ID
		|| (*bow)->get_instance_id() == 0U)
	{
		(void)bow->destroy();
		delete bow;
		return (FT_ERR_PERMISSION_DENIED);
	}
	const uint64_t bow_instance_id = (*bow)->get_instance_id();
	(void)bow->destroy();
	delete bow;
	result = make_payload(request_id,
		expected_inventory_revision, server_tick, bow_instance_id, actor_id, origin_x,
		origin_y, origin_z, direction_x, direction_y, direction_z, payload);
	if (result == FT_ERR_SUCCESS)
		result = journal.serialize(journal_before);
	uint64_t sequence = 0U;
	bool replayed = false;
	if (result == FT_ERR_SUCCESS)
		result = journal.prepare(scope, request_id, PROJECTILE_TRANSACTION_TYPE,
			payload.data(), static_cast<uint32_t>(payload.size()), &sequence,
			&replayed);
	if (result == FT_ERR_SUCCESS)
		result = snapshot_writer->save();
	if (result != FT_ERR_SUCCESS)
	{
		if (!journal_before.empty())
		{
			int32_t restore_result = journal.deserialize(
				journal_before.data(), journal_before.size());
			if (restore_result != FT_ERR_SUCCESS)
				result = restore_result;
			else
			{
				restore_result = snapshot_writer->save();
				if (restore_result != FT_ERR_SUCCESS)
					result = restore_result;
			}
		}
		return (result);
	}
	GameTransactionJournal::Record prepared = {};
	result = journal.find(scope, request_id, &prepared);
	if (result == FT_ERR_SUCCESS)
		result = apply_prepared(snapshot_path, character, entities, catalog,
			journal, prepared, snapshot_writer);
	(void)sequence;
	(void)replayed;
	return (result);
}

int32_t WorldProjectileLaunchTransaction::recover_pending(
	const char *snapshot_path, game_character &character,
	WorldEntityStore &entities, MinecraftItemCatalog &catalog,
	GameTransactionJournal &journal, uint64_t server_tick,
	double origin_x, double origin_y, double origin_z, double direction_x,
	double direction_y, double direction_z,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	std::vector<GameTransactionJournal::Record> pending;
	int32_t result = journal.pending_records(pending);
	if (result != FT_ERR_SUCCESS)
		return (result);
	(void)server_tick;
	(void)origin_x;
	(void)origin_y;
	(void)origin_z;
	(void)direction_x;
	(void)direction_y;
	(void)direction_z;
	for (const GameTransactionJournal::Record &record : pending)
	{
		if (record.transaction_type != PROJECTILE_TRANSACTION_TYPE)
			continue ;
		if (!payload_is_valid(record))
			return (FT_ERR_INVALID_STATE);
		result = apply_prepared(snapshot_path, character, entities, catalog,
			journal, record, snapshot_writer);
		if (result != FT_ERR_SUCCESS)
		{
			GameTransactionJournal::Record resolved = {};
			const int32_t lookup_result = journal.find(record.scope_id,
				record.request_id, &resolved);
			if (lookup_result != FT_ERR_SUCCESS
				|| resolved.state != GameTransactionJournal::ABORTED)
				return (result);
		}
	}
	return (FT_ERR_SUCCESS);
}
