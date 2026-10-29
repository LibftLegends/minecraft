#include "../../src/validators/WorldItemStoreValidator.hpp"
#include "../../src/entities/WorldItemStore.hpp"
#include "../../src/entities/WorldEntityStore.hpp"
#include "../../src/entities/WorldSpawnerStore.hpp"
#include "../../src/entities/WorldAnimalSimulation.hpp"
#include "../../src/app/GameSessionPersistence.hpp"
#include "../../src/app/GameSessionEntityState.hpp"
#include "../../src/transactions/WorldItemPickupTransaction.hpp"
#include "../../src/transactions/WorldItemCraftTransaction.hpp"
#include "../../src/transactions/WorldItemProcessingTransaction.hpp"
#include "../../src/transactions/WorldItemProcessingAdvanceTransaction.hpp"
#include "../../src/transactions/WorldItemCardTransferTransaction.hpp"
#include "../../src/transactions/WorldBoatTransferTransaction.hpp"
#include "../../src/inventory/MinecraftItemCatalog.hpp"
#include "../../src/inventory/MinecraftLootCatalog.hpp"
#include "../../src/crafting/MinecraftRecipeCatalog.hpp"
#include "../../src/cards/RegenerationCardSession.hpp"
#include "../../src/world/WorldRevisionMetadataStore.hpp"
#include "../../Libft/Modules/Game/game_item.hpp"
#include "../../Libft/Modules/Filesystem/filesystem.hpp"
#include <cstdio>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>

static int32_t world_item_validator_entity_store_roundtrip() noexcept
{
	WorldEntityStore source;
	WorldEntityStore restored;
	WorldEntityStore legacy_restored;
	EntityState entity;
	WorldEntitySnapshot snapshot = {};
	std::vector<uint8_t> bytes;
	std::vector<uint8_t> before_corruption;
	std::vector<uint8_t> after_corruption;
	std::vector<uint8_t> legacy_bytes;
	std::vector<uint8_t> invalid_identity_bytes;
	std::vector<WorldEntitySnapshot> entities;
	std::vector<EntityState> batch_states(2U);
	std::vector<EntityState> invalid_batch_states(2U);
	std::vector<uint32_t> batch_ids;
	std::vector<uint32_t> ids_sentinel(1U, 777U);
	uint32_t entity_id = 0U;
	uint32_t boat_entity_id = 0U;
	uint64_t new_revision = 0U;
	uint64_t rejected_revision = 0U;
	uint64_t maximum_entity_item_instance_id = 0U;
	int32_t result = source.initialize();
	if (result == FT_ERR_SUCCESS)
		result = restored.initialize();
	if (result == FT_ERR_SUCCESS)
		result = legacy_restored.initialize();
	entity.type = static_cast<uint32_t>(EntityState::Kind::ZOMBIE);
	entity.position_x = 12.5;
	entity.position_y = 70.0;
	entity.position_z = -4.25;
	entity.health = 20;
	if (result == FT_ERR_SUCCESS)
		result = source.spawn(entity, &entity_id);
	if (result == FT_ERR_SUCCESS
		&& source.spawn_item(entity, 0xA11ULL, &boat_entity_id)
			!= FT_ERR_INVALID_ARGUMENT)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = source.get(entity_id, &snapshot);
	if (result == FT_ERR_SUCCESS && (snapshot.revision != 1U
		|| snapshot.state.health != 20))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		snapshot.state.health = 17;
		result = source.update(entity_id, snapshot.revision, snapshot.state,
			&new_revision);
	}
	if (result == FT_ERR_SUCCESS
		&& (new_revision != 2U
			|| source.update(entity_id, 1U, snapshot.state,
				&new_revision) != FT_ERR_INVALID_STATE))
		result = FT_ERR_INVALID_STATE;
	for (std::size_t index = 0U; index < batch_states.size(); ++index)
	{
		batch_states[index].type = static_cast<uint32_t>(
			EntityState::Kind::ZOMBIE);
		batch_states[index].position_x = 20.0
			+ static_cast<double>(index);
		batch_states[index].position_y = 64.0;
		batch_states[index].health = 20;
	}
	if (result == FT_ERR_SUCCESS)
		result = source.spawn_batch(batch_states, batch_ids);
	if (result == FT_ERR_SUCCESS
		&& (batch_ids.size() != 2U || batch_ids[0] != 2U
			|| batch_ids[1] != 3U))
		result = FT_ERR_INVALID_STATE;
	EntityState boat;
	boat.type = static_cast<uint32_t>(EntityState::Kind::BOAT);
	boat.position_x = 25.5;
	boat.position_y = 64.0;
	boat.position_z = -8.5;
	if (result == FT_ERR_SUCCESS)
		result = source.spawn_item(boat, 0xB047ULL, &boat_entity_id);
	if (result == FT_ERR_SUCCESS && boat_entity_id != 4U)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = source.get_max_item_instance_id(
			&maximum_entity_item_instance_id);
	if (result == FT_ERR_SUCCESS
		&& maximum_entity_item_instance_id != 0xB047ULL)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		WorldEntitySnapshot boat_snapshot = {};
		result = source.get(boat_entity_id, &boat_snapshot);
		if (result == FT_ERR_SUCCESS)
		{
			boat_snapshot.state.type = static_cast<uint32_t>(
				EntityState::Kind::ZOMBIE);
			if (source.update(boat_entity_id, boat_snapshot.revision,
					boat_snapshot.state, &rejected_revision)
				!= FT_ERR_INVALID_ARGUMENT)
				result = FT_ERR_INVALID_STATE;
		}
	}
	if (result == FT_ERR_SUCCESS
		&& (source.spawn(boat, &entity_id) != FT_ERR_INVALID_ARGUMENT
			|| source.spawn_item(boat, 0xB047ULL, &entity_id)
				!= FT_ERR_ALREADY_EXISTS))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = source.query_radius(20.5, 64.0, 0.0, 1.5, 2U, entities);
	if (result == FT_ERR_SUCCESS
		&& (entities.size() != 2U || entities[0].state.id != 2U
			|| entities[1].state.id != 3U))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS
		&& (source.query_radius(20.5, 64.0, 0.0, 1.5, 1U, entities)
			!= FT_ERR_FULL
			|| entities.size() != 2U || entities[0].state.id != 2U
			|| entities[1].state.id != 3U))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS
		&& (source.query_radius(20.5, 64.0, 0.0,
			std::numeric_limits<double>::infinity(), 2U, entities)
			!= FT_ERR_INVALID_ARGUMENT
			|| entities.size() != 2U || entities[0].state.id != 2U
			|| entities[1].state.id != 3U))
		result = FT_ERR_INVALID_STATE;
	invalid_batch_states[0] = batch_states[0];
	invalid_batch_states[1].position_x = 2.0;
	invalid_batch_states[1].health = 20;
	if (result == FT_ERR_SUCCESS
		&& (source.spawn_batch(invalid_batch_states, ids_sentinel)
			!= FT_ERR_INVALID_ARGUMENT
			|| ids_sentinel.size() != 1U || ids_sentinel[0] != 777U))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = source.serialize(bytes);
	if (result == FT_ERR_SUCCESS)
		result = restored.deserialize(bytes.data(), bytes.size());
	if (result == FT_ERR_SUCCESS)
		result = restored.snapshot(entities);
	if (result == FT_ERR_SUCCESS
		&& (entities.size() != 4U || entities[0].state.id != entity_id
			|| entities[0].state.health != 17
			|| entities[0].revision != 2U
			|| entities[3].state.id != boat_entity_id
			|| entities[3].item_instance_id != 0xB047ULL))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		uint64_t checksum = 14695981039346656037ULL;
		try
		{
			invalid_identity_bytes = bytes;
		}
		catch (const std::bad_alloc &)
		{
			result = FT_ERR_NO_MEMORY;
		}
		if (result == FT_ERR_SUCCESS)
		{
			for (uint32_t shift = 0U; shift < 64U; shift += 8U)
				invalid_identity_bytes[16U + 96U + shift / 8U] =
					static_cast<uint8_t>(0xB048ULL >> shift);
			for (std::size_t index = 0U;
				index < invalid_identity_bytes.size() - 8U; ++index)
			{
				checksum ^= invalid_identity_bytes[index];
				checksum *= 1099511628211ULL;
			}
			for (uint32_t shift = 0U; shift < 64U; shift += 8U)
				invalid_identity_bytes[invalid_identity_bytes.size() - 8U
					+ shift / 8U] = static_cast<uint8_t>(checksum >> shift);
			before_corruption = bytes;
			if (restored.deserialize(invalid_identity_bytes.data(),
					invalid_identity_bytes.size()) != FT_ERR_INVALID_ARGUMENT)
				result = FT_ERR_INVALID_STATE;
		}
	}
	if (result == FT_ERR_SUCCESS)
		result = restored.serialize(after_corruption);
	if (result == FT_ERR_SUCCESS && before_corruption != after_corruption)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		try
		{
			legacy_bytes.insert(legacy_bytes.end(), bytes.begin(),
				bytes.begin() + 16);
			legacy_bytes[4] = 1U;
			legacy_bytes[8] = 4U;
			legacy_bytes[9] = 0U;
			legacy_bytes[10] = 0U;
			legacy_bytes[11] = 0U;
			legacy_bytes[12] = 3U;
			legacy_bytes[13] = 0U;
			legacy_bytes[14] = 0U;
			legacy_bytes[15] = 0U;
			for (std::size_t index = 0U; index < 3U; ++index)
			{
				const std::size_t offset = 16U + index * 112U;
				legacy_bytes.insert(legacy_bytes.end(), bytes.begin() + offset,
					bytes.begin() + offset + 96U);
			}
			uint64_t checksum = 14695981039346656037ULL;
			for (uint8_t value : legacy_bytes)
			{
				checksum ^= value;
				checksum *= 1099511628211ULL;
			}
			for (uint32_t shift = 0U; shift < 64U; shift += 8U)
				legacy_bytes.push_back(static_cast<uint8_t>(checksum >> shift));
		}
		catch (const std::bad_alloc &)
		{
			result = FT_ERR_NO_MEMORY;
		}
	}
	if (result == FT_ERR_SUCCESS)
		result = legacy_restored.deserialize(legacy_bytes.data(),
			legacy_bytes.size());
	if (result == FT_ERR_SUCCESS)
	{
		std::vector<WorldEntitySnapshot> legacy_entities;
		result = legacy_restored.snapshot(legacy_entities);
		if (result == FT_ERR_SUCCESS
			&& (legacy_entities.size() != 3U
				|| legacy_entities[0].item_instance_id != 0U
				|| legacy_entities[1].item_instance_id != 0U
				|| legacy_entities[2].item_instance_id != 0U))
			result = FT_ERR_INVALID_STATE;
	}
	if (result == FT_ERR_SUCCESS)
		result = restored.serialize(before_corruption);
	if (result == FT_ERR_SUCCESS)
		result = restored.serialize(bytes);
	if (result == FT_ERR_SUCCESS)
	{
		bytes.back() ^= 1U;
		if (restored.deserialize(bytes.data(), bytes.size())
			!= FT_ERR_INVALID_ARGUMENT)
			result = FT_ERR_INVALID_STATE;
	}
	if (result == FT_ERR_SUCCESS)
		result = restored.serialize(after_corruption);
	if (result == FT_ERR_SUCCESS && before_corruption != after_corruption)
		result = FT_ERR_INVALID_STATE;
	(void)source.destroy();
	(void)restored.destroy();
	(void)legacy_restored.destroy();
	return (result);
}

static int32_t world_item_validator_animal_simulation() noexcept
{
	World world;
	WorldEntityStore first_store;
	WorldEntityStore second_store;
	WorldEntityStore restored_store;
	WorldEntityStore chicken_store;
	WorldEntityStore chicken_restored_store;
	EntityState initial_state;
	WorldEntitySnapshot first = {};
	WorldEntitySnapshot second = {};
	WorldEntitySnapshot restored = {};
	std::vector<uint8_t> serialized;
	std::vector<WorldAnimalProductionEvent> production_events;
	int32_t animal_x = -1;
	int32_t animal_y = -1;
	int32_t animal_z = -1;
	bool changed = false;
	int32_t result = world.initialize("animal-wander-validation");
	if (result == FT_ERR_SUCCESS)
		result = first_store.initialize();
	if (result == FT_ERR_SUCCESS)
		result = second_store.initialize();
	if (result == FT_ERR_SUCCESS)
		result = restored_store.initialize();
	if (result == FT_ERR_SUCCESS)
		result = chicken_store.initialize();
	if (result == FT_ERR_SUCCESS)
		result = chicken_restored_store.initialize();
	for (int32_t z = 3; result == FT_ERR_SUCCESS && z < 13
		&& animal_x < 0; ++z)
	{
		for (int32_t x = 3; x < 13 && animal_x < 0; ++x)
		{
			for (int32_t y = 2; y < GAME_VOXEL_CHUNK_HEIGHT - 2; ++y)
			{
				if (world.solid_block_at(x, y - 1, z)
					&& !world.solid_block_at(x, y, z)
					&& !world.solid_block_at(x, y + 1, z))
				{
					animal_x = x;
					animal_y = y;
					animal_z = z;
					break;
				}
			}
		}
	}
	if (result == FT_ERR_SUCCESS && animal_x < 0)
		result = FT_ERR_NOT_FOUND;
	initial_state.type = static_cast<uint32_t>(EntityState::Kind::COW);
	initial_state.position_x = static_cast<double>(animal_x) + 0.5;
	initial_state.position_y = static_cast<double>(animal_y);
	initial_state.position_z = static_cast<double>(animal_z) + 0.5;
	initial_state.health = 20;
	uint32_t first_id = 0U;
	uint32_t second_id = 0U;
	if (result == FT_ERR_SUCCESS)
		result = first_store.spawn(initial_state, &first_id);
	if (result == FT_ERR_SUCCESS)
		result = second_store.spawn(initial_state, &second_id);
	if (result == FT_ERR_SUCCESS && first_id != second_id)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = WorldAnimalSimulation::tick(world, first_store,
			initial_state.position_x, initial_state.position_y,
			initial_state.position_z, 9U, &changed);
	if (result == FT_ERR_SUCCESS && changed)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = WorldAnimalSimulation::tick(world, first_store,
			initial_state.position_x, initial_state.position_y,
			initial_state.position_z, 10U, &changed);
	if (result == FT_ERR_SUCCESS && !changed)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = WorldAnimalSimulation::tick(world, second_store,
			initial_state.position_x, initial_state.position_y,
			initial_state.position_z, 10U, &changed);
	if (result == FT_ERR_SUCCESS)
		result = first_store.get(first_id, &first);
	if (result == FT_ERR_SUCCESS)
		result = second_store.get(second_id, &second);
	if (result == FT_ERR_SUCCESS
		&& (std::fabs(first.state.position_x - initial_state.position_x)
			<= 1.0e-9
			&& std::fabs(first.state.position_z - initial_state.position_z)
				<= 1.0e-9))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS
		&& (std::fabs(first.state.position_x - second.state.position_x)
				> 1.0e-9
			|| std::fabs(first.state.position_y - second.state.position_y)
				> 1.0e-9
			|| std::fabs(first.state.position_z - second.state.position_z)
				> 1.0e-9
			|| first.state.action_state != static_cast<uint32_t>(
				EntityState::ActionState::WALKING)
			|| first.state.tick != 10U))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = first_store.serialize(serialized);
	if (result == FT_ERR_SUCCESS)
		result = restored_store.deserialize(serialized.data(),
			serialized.size());
	if (result == FT_ERR_SUCCESS)
		result = restored_store.get(first_id, &restored);
	if (result == FT_ERR_SUCCESS
		&& (std::fabs(restored.state.position_x - first.state.position_x)
				> 1.0e-9
			|| std::fabs(restored.state.position_z - first.state.position_z)
				> 1.0e-9
			|| restored.state.tick != first.state.tick))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		EntityState chicken_state = initial_state;
		uint32_t chicken_id = 0U;
		uint64_t chicken_period = 0U;
		uint64_t egg_tick = 0U;
		chicken_state.type = static_cast<uint32_t>(EntityState::Kind::CHICKEN);
		chicken_state.tick = WorldAnimalSimulation::CHICKEN_EGG_PERIOD_MINIMUM_TICKS;
		result = chicken_store.spawn(chicken_state, &chicken_id);
		if (result == FT_ERR_SUCCESS)
		{
			chicken_period =
				WorldAnimalSimulation::CHICKEN_EGG_PERIOD_MINIMUM_TICKS
				+ chicken_id
				% WorldAnimalSimulation::CHICKEN_EGG_PERIOD_VARIANCE_TICKS;
			egg_tick = ((chicken_period / 10U) + 1U) * 10U;
			result = WorldAnimalSimulation::tick(world, chicken_store,
				chicken_state.position_x, chicken_state.position_y,
				chicken_state.position_z, egg_tick, &changed,
				&production_events);
			if (result == FT_ERR_SUCCESS
				&& (production_events.size() != 1U
					|| production_events[0].animal_entity_id != chicken_id
					|| production_events[0].previous_tick
						!= chicken_state.tick
					|| production_events[0].production_cycle != 1U))
				result = FT_ERR_INVALID_STATE;
		}
		if (result == FT_ERR_SUCCESS)
			result = chicken_store.serialize(serialized);
		if (result == FT_ERR_SUCCESS)
			result = chicken_restored_store.deserialize(serialized.data(),
				serialized.size());
		if (result == FT_ERR_SUCCESS)
			result = WorldAnimalSimulation::tick(world, chicken_restored_store,
				chicken_state.position_x, chicken_state.position_y,
				chicken_state.position_z, egg_tick, &changed,
				&production_events);
		if (result == FT_ERR_SUCCESS && !production_events.empty())
			result = FT_ERR_INVALID_STATE;
	}
	world.destroy();
	(void)first_store.destroy();
	(void)second_store.destroy();
	(void)restored_store.destroy();
	(void)chicken_store.destroy();
	(void)chicken_restored_store.destroy();
	return (result);
}

static int32_t world_item_validator_spawner_lifecycle() noexcept
{
	WorldEntityStore entities;
	WorldSpawnerStore source;
	WorldSpawnerStore restored;
	WorldSpawnerStore batch_store;
	WorldSpawnerDefinition definition = {};
	std::vector<WorldSpawnerDefinition> batch_definitions;
	std::vector<uint32_t> batch_ids(1U, 99U);
	std::vector<uint32_t> invalid_remove_ids;
	std::vector<uint8_t> batch_before;
	std::vector<uint8_t> batch_after;
	std::vector<WorldSpawnerDeath> deaths;
	std::vector<WorldSpawnerDeath> replayed_deaths;
	std::vector<uint8_t> bytes;
	std::vector<uint8_t> before_corruption;
	std::vector<uint8_t> after_corruption;
	std::vector<WorldEntitySnapshot> snapshots;
	WorldEntitySnapshot entity = {};
	std::vector<MinecraftLootDrop> loot_drops;
	std::vector<MinecraftLootDrop> repeated_loot_drops;
	uint32_t spawner_id = 0U;
	uint64_t revision = 0U;
	bool state_changed = false;
	int32_t result = entities.initialize();
	if (result == FT_ERR_SUCCESS)
		result = source.initialize();
	if (result == FT_ERR_SUCCESS)
		result = restored.initialize();
	if (result == FT_ERR_SUCCESS)
		result = batch_store.initialize();
	definition.x = 12.5;
	definition.y = 70.0;
	definition.z = -4.25;
	definition.entity_type = static_cast<uint32_t>(EntityState::Kind::ZOMBIE);
	definition.maximum_alive = 1U;
	definition.spawn_interval_ticks = 10U;
	definition.loot_table_id = 9U;
	if (result == FT_ERR_SUCCESS)
	{
		try
		{
			batch_definitions.push_back(definition);
			batch_definitions.push_back(definition);
			batch_definitions[1U].entity_type = 0U;
		}
		catch (...)
		{
			result = FT_ERR_NO_MEMORY;
		}
	}
	if (result == FT_ERR_SUCCESS)
		result = batch_store.serialize(batch_before);
	if (result == FT_ERR_SUCCESS
		&& batch_store.add_batch(batch_definitions, 10U, batch_ids)
			!= FT_ERR_INVALID_ARGUMENT)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = batch_store.serialize(batch_after);
	if (result == FT_ERR_SUCCESS
		&& (batch_before != batch_after || batch_ids.size() != 1U
			|| batch_ids[0U] != 99U))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		batch_definitions[1U] = definition;
		result = batch_store.add_batch(batch_definitions, 10U, batch_ids);
	}
	if (result == FT_ERR_SUCCESS
		&& (batch_ids.size() != 2U || batch_ids[0U] != 1U
			|| batch_ids[1U] != 2U))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		try
		{
			invalid_remove_ids.push_back(batch_ids[0U]);
			invalid_remove_ids.push_back(999U);
		}
		catch (...)
		{
			result = FT_ERR_NO_MEMORY;
		}
	}
	if (result == FT_ERR_SUCCESS)
		result = batch_store.serialize(batch_before);
	if (result == FT_ERR_SUCCESS
		&& batch_store.remove_batch(invalid_remove_ids) != FT_ERR_NOT_FOUND)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = batch_store.serialize(batch_after);
	if (result == FT_ERR_SUCCESS && batch_before != batch_after)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = batch_store.remove_batch(batch_ids);
	if (result == FT_ERR_SUCCESS)
		result = batch_store.serialize(batch_after);
	if (result == FT_ERR_SUCCESS
		&& (batch_after.size() < 16U || batch_after[12U] != 0U
			|| batch_after[13U] != 0U || batch_after[14U] != 0U
			|| batch_after[15U] != 0U))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = source.add(definition, 10U, &spawner_id);
	if (result == FT_ERR_SUCCESS && spawner_id != 1U)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = MinecraftLootCatalog::resolve(MINECRAFT_LOOT_SKELETON,
			77U, loot_drops);
	if (result == FT_ERR_SUCCESS)
		result = MinecraftLootCatalog::resolve(MINECRAFT_LOOT_SKELETON,
			77U, repeated_loot_drops);
	if (result == FT_ERR_SUCCESS
		&& (loot_drops.size() != 1U
			|| loot_drops.size() != repeated_loot_drops.size()
			|| loot_drops[0].item_id != MinecraftItemCatalog::BONE_ITEM_ID
			|| loot_drops[0].quantity != repeated_loot_drops[0].quantity))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = MinecraftLootCatalog::resolve(MINECRAFT_LOOT_SKELETON,
			1U, loot_drops);
	if (result == FT_ERR_SUCCESS)
		result = MinecraftLootCatalog::resolve(MINECRAFT_LOOT_SKELETON,
			1U, repeated_loot_drops);
	if (result == FT_ERR_SUCCESS
		&& (loot_drops.size() != 2U
			|| loot_drops.size() != repeated_loot_drops.size()
			|| loot_drops[0].item_id != MinecraftItemCatalog::BONE_ITEM_ID
			|| loot_drops[1].item_id != MinecraftItemCatalog::STRING_ITEM_ID
			|| loot_drops[0].quantity != repeated_loot_drops[0].quantity
			|| loot_drops[1].quantity != repeated_loot_drops[1].quantity))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		const std::size_t prior_drop_count = repeated_loot_drops.size();
		const int32_t unknown_table_result = MinecraftLootCatalog::resolve(
			999U, 77U, repeated_loot_drops);
		if (prior_drop_count != 2U
			|| unknown_table_result != FT_ERR_CONFIGURATION
			|| repeated_loot_drops.size() != prior_drop_count
			|| repeated_loot_drops[0].item_id
				!= MinecraftItemCatalog::BONE_ITEM_ID
			|| repeated_loot_drops[1].item_id
				!= MinecraftItemCatalog::STRING_ITEM_ID)
			result = FT_ERR_INVALID_STATE;
	}
	if (result == FT_ERR_SUCCESS
		&& MinecraftLootCatalog::resolve(MINECRAFT_LOOT_SKELETON, 0U,
			repeated_loot_drops) != FT_ERR_INVALID_ARGUMENT)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		struct AnimalLootCase
		{
			uint32_t loot_table_id;
			int32_t primary_item_id;
			int32_t secondary_item_id;
		};
		static const AnimalLootCase cases[] = {
			{MINECRAFT_LOOT_COW, MinecraftItemCatalog::RAW_BEEF_ITEM_ID,
				MinecraftItemCatalog::LEATHER_ITEM_ID},
			{MINECRAFT_LOOT_PIG, MinecraftItemCatalog::RAW_PORK_ITEM_ID, 0},
			{MINECRAFT_LOOT_CHICKEN,
				MinecraftItemCatalog::RAW_CHICKEN_ITEM_ID,
				MinecraftItemCatalog::FEATHER_ITEM_ID},
			{MINECRAFT_LOOT_SHEEP,
				MinecraftItemCatalog::RAW_MUTTON_ITEM_ID,
				MinecraftItemCatalog::WOOL_ITEM_ID}};
		for (std::size_t index = 0U;
			index < sizeof(cases) / sizeof(cases[0])
				&& result == FT_ERR_SUCCESS; ++index)
		{
			std::vector<MinecraftLootDrop> animal_drops;
			std::vector<MinecraftLootDrop> repeated_animal_drops;
			result = MinecraftLootCatalog::resolve(cases[index].loot_table_id,
				700U + static_cast<uint32_t>(index), animal_drops);
			if (result == FT_ERR_SUCCESS)
				result = MinecraftLootCatalog::resolve(
					cases[index].loot_table_id,
					700U + static_cast<uint32_t>(index), repeated_animal_drops);
			if (result == FT_ERR_SUCCESS
				&& (animal_drops.empty() || animal_drops.size() > 2U
					|| animal_drops.size() != repeated_animal_drops.size()))
				result = FT_ERR_INVALID_STATE;
			bool found_primary = false;
			for (std::size_t drop_index = 0U;
				drop_index < animal_drops.size()
					&& result == FT_ERR_SUCCESS; ++drop_index)
			{
				const MinecraftLootDrop &drop = animal_drops[drop_index];
				if (drop.item_id == cases[index].primary_item_id)
					found_primary = drop.quantity >= 1 && drop.quantity <= 3;
				else if (drop.item_id == cases[index].secondary_item_id
					&& cases[index].secondary_item_id != 0)
				{
					if (drop.quantity < 1 || drop.quantity > 2)
						result = FT_ERR_INVALID_STATE;
				}
				else
					result = FT_ERR_INVALID_STATE;
				if (result == FT_ERR_SUCCESS
					&& (drop.item_id != repeated_animal_drops[drop_index].item_id
						|| drop.quantity
							!= repeated_animal_drops[drop_index].quantity))
					result = FT_ERR_INVALID_STATE;
			}
			if (result == FT_ERR_SUCCESS
				&& !found_primary)
				result = FT_ERR_INVALID_STATE;
		}
	}
	if (result == FT_ERR_SUCCESS)
		result = source.tick(9U, entities, deaths, &state_changed);
	if (result == FT_ERR_SUCCESS)
		result = entities.snapshot(snapshots);
	if (result == FT_ERR_SUCCESS && !snapshots.empty())
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = source.tick(10U, entities, deaths, &state_changed);
	if (result == FT_ERR_SUCCESS)
		result = entities.snapshot(snapshots);
	if (result == FT_ERR_SUCCESS
		&& (!state_changed || snapshots.size() != 1U || snapshots[0].state.type
			!= definition.entity_type || snapshots[0].state.health != 20))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = source.tick(11U, entities, deaths, &state_changed);
	if (result == FT_ERR_SUCCESS)
		result = entities.get(snapshots[0].state.id, &entity);
	if (result == FT_ERR_SUCCESS)
	{
		entity.state.health = 0;
		result = entities.update(entity.state.id, entity.revision,
			entity.state, &revision);
	}
	if (result == FT_ERR_SUCCESS)
		result = source.tick(12U, entities, deaths, &state_changed);
	if (result == FT_ERR_SUCCESS
		&& (deaths.size() != 1U || deaths[0].spawner_id != spawner_id
			|| deaths[0].entity_id != entity.state.id
			|| deaths[0].loot_table_id != definition.loot_table_id))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = source.serialize(bytes);
	if (result == FT_ERR_SUCCESS)
		result = restored.deserialize(bytes.data(), bytes.size());
	if (result == FT_ERR_SUCCESS)
		result = restored.tick(13U, entities, replayed_deaths, &state_changed);
	if (result == FT_ERR_SUCCESS
		&& (replayed_deaths.size() != 1U
			|| replayed_deaths[0].entity_id != entity.state.id
			|| replayed_deaths[0].loot_table_id != definition.loot_table_id))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = restored.acknowledge_death(spawner_id, entity.state.id);
	if (result == FT_ERR_SUCCESS
		&& restored.acknowledge_death(spawner_id, entity.state.id)
			!= FT_ERR_NOT_FOUND)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = restored.serialize(before_corruption);
	if (result == FT_ERR_SUCCESS)
	{
		bytes.back() ^= 1U;
		if (restored.deserialize(bytes.data(), bytes.size())
			!= FT_ERR_INVALID_ARGUMENT)
			result = FT_ERR_INVALID_STATE;
	}
	if (result == FT_ERR_SUCCESS)
		result = restored.serialize(after_corruption);
	if (result == FT_ERR_SUCCESS && before_corruption != after_corruption)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		static const uint32_t animal_types[] = {
			static_cast<uint32_t>(EntityState::Kind::COW),
			static_cast<uint32_t>(EntityState::Kind::PIG),
			static_cast<uint32_t>(EntityState::Kind::CHICKEN),
			static_cast<uint32_t>(EntityState::Kind::SHEEP)};
		static const uint32_t animal_loot_tables[] = {
			MINECRAFT_LOOT_COW, MINECRAFT_LOOT_PIG,
			MINECRAFT_LOOT_CHICKEN, MINECRAFT_LOOT_SHEEP};
		for (std::size_t animal_index = 0U;
			animal_index < sizeof(animal_types) / sizeof(animal_types[0])
				&& result == FT_ERR_SUCCESS; ++animal_index)
		{
			const uint64_t spawn_tick = 100U
				+ ft_platform_cast<uint64_t>(animal_index) * 10U;
			definition.entity_type = animal_types[animal_index];
			definition.loot_table_id = animal_loot_tables[animal_index];
			result = restored.add(definition, spawn_tick, &spawner_id);
			deaths.clear();
			state_changed = false;
			if (result == FT_ERR_SUCCESS)
				result = restored.tick(spawn_tick, entities, deaths,
					&state_changed);
			if (result == FT_ERR_SUCCESS && !state_changed)
				result = FT_ERR_INVALID_STATE;
			if (result == FT_ERR_SUCCESS)
				result = entities.snapshot(snapshots);
			bool animal_spawned = false;
			for (const WorldEntitySnapshot &animal_snapshot : snapshots)
			{
				if (animal_snapshot.state.type == animal_types[animal_index])
					animal_spawned = true;
			}
			if (result == FT_ERR_SUCCESS && !animal_spawned)
				result = FT_ERR_INVALID_STATE;
		}
	}
	(void)source.destroy();
	(void)restored.destroy();
	(void)batch_store.destroy();
	(void)entities.destroy();
	return (result);
}

namespace
{
	static int32_t validate_revision_metadata_payload(
		const std::vector<uint8_t> &bytes)
	{
		std::vector<uint8_t> base_bytes = bytes;
		std::vector<WorldRevisionManager::RevisionChunk> protected_chunks;
		WorldRevisionCostPolicy cost_policy = {};
		uint32_t revision_id = 0U;
		uint64_t cost_policy_revision = 0U;
		bool cost_policy_configured = false;
		bool cost_policy_stored = false;
		bool station_configured = false;
		int32_t station_chunk_x = 0;
		int32_t station_chunk_z = 0;
		bool station_stored = false;
		bool station_block_configured = false;
		int32_t station_block_x = 0;
		int32_t station_block_y = 0;
		int32_t station_block_z = 0;
		uint32_t station_block_id = 0U;
		if (bytes.size() >= 44U)
		{
			const std::size_t footer = bytes.size() - 4U;
			const uint32_t extension_size =
				static_cast<uint32_t>(bytes[footer])
				| (static_cast<uint32_t>(bytes[footer + 1U]) << 8U)
				| (static_cast<uint32_t>(bytes[footer + 2U]) << 16U)
				| (static_cast<uint32_t>(bytes[footer + 3U]) << 24U);
			if (extension_size >= 44U && extension_size <= bytes.size())
			{
				const std::size_t extension_start = bytes.size() - extension_size;
				const uint32_t magic =
					static_cast<uint32_t>(bytes[extension_start])
					| (static_cast<uint32_t>(bytes[extension_start + 1U]) << 8U)
					| (static_cast<uint32_t>(bytes[extension_start + 2U]) << 16U)
					| (static_cast<uint32_t>(bytes[extension_start + 3U]) << 24U);
				if (magic == 0x314C5257U)
					base_bytes.resize(extension_start);
			}
		}
		const int32_t result = WorldRevisionMetadataStore::deserialize(base_bytes,
			revision_id, protected_chunks, cost_policy, cost_policy_revision,
			cost_policy_configured, cost_policy_stored, station_configured,
			station_chunk_x, station_chunk_z, station_stored,
			station_block_configured, station_block_x, station_block_y,
			station_block_z, station_block_id);

		if (result != FT_ERR_SUCCESS)
			return (result);
		if (revision_id != 7U || !protected_chunks.empty()
			|| cost_policy_revision != 0U || cost_policy_configured
			|| !cost_policy_stored || station_configured || station_chunk_x != 0
			|| station_chunk_z != 0 || !station_stored
			|| station_block_configured || station_block_x != 0
			|| station_block_y != 0 || station_block_z != 0
			|| station_block_id != 0U)
			return (FT_ERR_INVALID_STATE);
		return (FT_ERR_SUCCESS);
	}

	struct SessionSnapshotWriterFixture
	{
		const char *path;
		game_character *character;
		WorldItemStore *world_items;
		MinecraftRecipeCatalog *recipe_catalog;
		GameTransactionJournal *journal;
		std::vector<uint8_t> *card_state;
		std::vector<uint8_t> *rules_state;
		uint32_t fail_on_call;
		uint32_t call_count;
	};

	static int32_t session_snapshot_writer_fixture_save(void *context) noexcept
	{
		SessionSnapshotWriterFixture *fixture =
			static_cast<SessionSnapshotWriterFixture *>(context);

		if (fixture == nullptr || fixture->path == nullptr
			|| fixture->character == nullptr || fixture->world_items == nullptr
			|| fixture->recipe_catalog == nullptr || fixture->journal == nullptr
			|| fixture->card_state == nullptr || fixture->rules_state == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		++fixture->call_count;
		if (fixture->fail_on_call != 0U
			&& fixture->call_count == fixture->fail_on_call)
			return (FT_ERR_IO);
		return (GameSessionPersistence::save_file(fixture->path,
			*fixture->character, *fixture->world_items,
			*fixture->recipe_catalog, *fixture->journal,
			*fixture->card_state, *fixture->rules_state));
	}

	static uint64_t session_snapshot_read_u64(
		const std::vector<uint8_t> &bytes, std::size_t offset)
	{
		uint64_t value = 0U;
		uint32_t shift = 0U;

		while (shift < 64U)
		{
			value |= static_cast<uint64_t>(bytes[offset + shift / 8U]) << shift;
			shift += 8U;
		}
		return (value);
	}

	static uint64_t session_snapshot_checksum(const uint8_t *bytes,
		std::size_t size)
	{
		uint64_t hash = 14695981039346656037ULL;
		std::size_t index = 0U;

		while (index < size)
		{
			hash ^= bytes[index];
			hash *= 1099511628211ULL;
			index += 1U;
		}
		return (hash);
	}

	static std::vector<uint8_t> session_crop_growth_state_fixture()
	{
		std::vector<uint8_t> bytes = {'C', 'G', 'S', '1'};
		auto append_u32 = [&](uint32_t value)
		{
			for (uint32_t shift = 0U; shift < 32U; shift += 8U)
				bytes.push_back(static_cast<uint8_t>(value >> shift));
		};
		auto append_u64 = [&](uint64_t value)
		{
			for (uint32_t shift = 0U; shift < 64U; shift += 8U)
				bytes.push_back(static_cast<uint8_t>(value >> shift));
		};
		append_u32(1U);
		append_u32(200U);
		append_u32(3U);
		bytes.push_back(9U);
		bytes.push_back(9U);
		bytes.push_back(4U);
		bytes.push_back(0U);
		append_u64(400U);
		append_u32(1U);
		append_u32(static_cast<uint32_t>(-2));
		append_u32(7U);
		append_u64(600U);
		append_u64(session_snapshot_checksum(bytes.data(), bytes.size()));
		return (bytes);
	}

	static bool session_snapshot_has_version(const char *path,
		uint32_t expected_version)
	{
		std::ifstream input(path, std::ios::binary);
		uint8_t header[8] = {};

		if (path == nullptr || !input.is_open())
			return (false);
		input.read(reinterpret_cast<char *>(header), sizeof(header));
		return (input.gcount() == static_cast<std::streamsize>(sizeof(header))
			&& header[0] == 'M' && header[1] == 'C'
			&& header[2] == 'S' && header[3] == 'P'
			&& (static_cast<uint32_t>(header[4])
				| (static_cast<uint32_t>(header[5]) << 8U)
				| (static_cast<uint32_t>(header[6]) << 16U)
				| (static_cast<uint32_t>(header[7]) << 24U))
				== expected_version);
	}

	static int32_t session_snapshot_downgrade_crop_policy_v15_to_v14(
		const char *path)
	{
		std::ifstream input(path, std::ios::binary);
		std::vector<uint8_t> bytes;
		if (path == nullptr || !input.is_open())
			return (FT_ERR_IO);
		try
		{
			bytes.assign(std::istreambuf_iterator<char>(input),
				std::istreambuf_iterator<char>());
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		if (input.bad() || bytes.size() < 140U || bytes[0] != 'M'
			|| bytes[1] != 'C' || bytes[2] != 'S' || bytes[3] != 'P'
			|| bytes[4] != 15U || bytes[5] != 0U || bytes[6] != 0U
			|| bytes[7] != 0U || bytes[112U] != 20U
			|| bytes[113U] != 0U || bytes[114U] != 0U
			|| bytes[115U] != 0U || bytes[116U] != 0U
			|| bytes[117U] != 0U || bytes[118U] != 0U
			|| bytes[119U] != 0U)
			return (FT_ERR_INVALID_ARGUMENT);
		const std::size_t crop_policy_offset = bytes.size() - 8U - 20U;
		bytes[crop_policy_offset + 10U] = 0U;
		bytes.erase(bytes.end() - 12, bytes.end() - 8);
		bytes[4] = 14U;
		bytes[112U] = 16U;
		const uint64_t checksum = session_snapshot_checksum(bytes.data(),
			bytes.size() - 8U);
		for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			bytes[bytes.size() - 8U + shift / 8U] = static_cast<uint8_t>(
				checksum >> shift);
		input.close();
		return (filesystem_atomic_write(path,
			reinterpret_cast<const char *>(bytes.data()), bytes.size()));
	}

	static int32_t make_world_item(int32_t item_id, int32_t quantity,
		int32_t max_stack, ft_sharedptr<game_item> *item_out)
	{
		int32_t result;

		if (item_out == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		result = item_out->initialize(new (std::nothrow) game_item());
		if (result != FT_ERR_SUCCESS)
			return (result);
		result = item_out->get()->initialize();
		if (result != FT_ERR_SUCCESS)
			return (result);
		item_out->get()->set_item_id(item_id);
		item_out->get()->set_stack_size(quantity);
		item_out->get()->set_max_stack(max_stack);
		item_out->get()->set_width(1);
		item_out->get()->set_height(1);
		return (FT_ERR_SUCCESS);
	}

	static int32_t validate_inventory_snapshot_restore()
	{
		game_inventory source;
		game_inventory destination;
		game_inventory_snapshot before_transfer = {};
		game_inventory_snapshot restored = {};
		ft_sharedptr<game_item> item;
		game_inventory_transfer_result first_result = {};
		game_inventory_transfer_result replay_result = {};
		uint64_t source_revision = 0U;
		uint64_t destination_revision = 0U;
		int32_t result = source.initialize(4U, 0);

		if (result == FT_ERR_SUCCESS)
			result = destination.initialize(4U, 0);
		if (result == FT_ERR_SUCCESS)
			result = make_world_item(91, 5, 8, &item);
		if (result == FT_ERR_SUCCESS)
			result = source.add_item(item);
		if (result == FT_ERR_SUCCESS)
			result = source.get_revision(&source_revision);
		if (result == FT_ERR_SUCCESS)
			result = destination.get_revision(&destination_revision);
		if (result == FT_ERR_SUCCESS)
			result = source.transfer_to(destination, 0, 1, source_revision,
				destination_revision, 0x524553544F5245ULL, 77U,
				&first_result);
		if (result == FT_ERR_SUCCESS)
			result = source.get_snapshot(before_transfer);
		if (result == FT_ERR_SUCCESS)
			source.remove_item(0);
		if (result == FT_ERR_SUCCESS)
			result = source.restore_snapshot(before_transfer);
		if (result == FT_ERR_SUCCESS)
			result = source.get_snapshot(restored);
		if (result == FT_ERR_SUCCESS
			&& (restored.revision != before_transfer.revision
				|| restored.next_slot != before_transfer.next_slot
				|| restored.items.size() != before_transfer.items.size()
				|| restored.items[0].slot != before_transfer.items[0].slot
				|| restored.items[0].stack_size
					!= before_transfer.items[0].stack_size))
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
			result = source.transfer_to(destination, 0, 1, source_revision,
				destination_revision, 0x524553544F5245ULL, 77U,
				&replay_result);
		if (result == FT_ERR_SUCCESS
			&& (first_result.result_code != FT_ERR_SUCCESS
				|| replay_result.result_code != FT_ERR_SUCCESS
				|| replay_result.transferred_quantity != 1
				|| replay_result.source_revision
					!= first_result.source_revision
				|| replay_result.destination_revision
					!= first_result.destination_revision
				|| destination.count_item(91) != 1))
			result = FT_ERR_INVALID_STATE;
		(void)source.destroy();
		(void)destination.destroy();
		return (result);
	}

	static int32_t validate_card_transfer_journal_recovery()
	{
		static const char *SNAPSHOT_PATH =
			"world_item_card_transfer_recovery.tmp";
		game_character character;
		game_character recovered_character;
		WorldItemStore world_items;
		WorldItemStore recovered_world_items;
		MinecraftItemCatalog items;
		MinecraftRecipeCatalog recipes;
		MinecraftRecipeCatalog recovered_recipes;
		GameTransactionJournal journal;
		GameTransactionJournal recovered_journal;
		std::unique_ptr<RegenerationCardSession> cards(
			new (std::nothrow) RegenerationCardSession());
		std::unique_ptr<RegenerationCardSession> recovered_cards(
			new (std::nothrow) RegenerationCardSession());
		std::vector<uint8_t> card_state;
		std::vector<uint8_t> recovered_card_state;
		std::vector<uint8_t> rules_state = {'R', 'W', 'R', 'S', 1U};
		std::vector<uint8_t> recovered_rules_state;
		SessionSnapshotWriterFixture writer_fixture = {};
		GameTransactionJournal::Record record = {};
		card_game_deck_card imported = {};
		uint64_t inventory_revision = 0U;
		uint32_t draw_count = 0U;
		uint32_t mapped_instance = 0U;
		int32_t result = FT_ERR_NO_MEMORY;
		const char *failure_stage = "allocate-card-transfer-fixtures";

		if (cards == nullptr || recovered_cards == nullptr)
			return (FT_ERR_NO_MEMORY);
		failure_stage = "initialize-card-transfer-stores";
		result = world_items.initialize();

		if (result == FT_ERR_SUCCESS)
			failure_stage = "initialize-card-transfer-catalogs-and-characters";
		if (result == FT_ERR_SUCCESS)
			result = recovered_world_items.initialize();
		if (result == FT_ERR_SUCCESS)
			result = items.initialize();
		if (result == FT_ERR_SUCCESS)
			result = recipes.initialize(items);
		if (result == FT_ERR_SUCCESS)
			result = recovered_recipes.initialize(items);
		if (result == FT_ERR_SUCCESS)
			result = character.initialize();
		if (result == FT_ERR_SUCCESS)
			result = recovered_character.initialize();
		if (result == FT_ERR_SUCCESS)
		{
			character.get_inventory().resize(4U);
			result = character.get_inventory().get_error();
		}
		if (result == FT_ERR_SUCCESS)
			character.get_inventory().set_weight_limit(
				std::numeric_limits<int32_t>::max());
		if (result == FT_ERR_SUCCESS)
		{
			recovered_character.get_inventory().resize(4U);
			result = recovered_character.get_inventory().get_error();
		}
		if (result == FT_ERR_SUCCESS)
			recovered_character.get_inventory().set_weight_limit(
				std::numeric_limits<int32_t>::max());
		if (result == FT_ERR_SUCCESS)
			failure_stage = "initialize-card-transfer-journals-and-deck";
		if (result == FT_ERR_SUCCESS)
			result = journal.initialize();
		if (result == FT_ERR_SUCCESS)
			result = recovered_journal.initialize();
		if (result == FT_ERR_SUCCESS)
			result = cards->initialize(4U, 0x5452414E53464552ULL);
		if (result == FT_ERR_SUCCESS)
			failure_stage = "serialize-card-transfer-card-state";
		if (result == FT_ERR_SUCCESS)
			result = cards->serialize_snapshot(card_state);
		ft_sharedptr<game_item> card_item;
		if (result == FT_ERR_SUCCESS)
			result = make_world_item(
				MinecraftItemCatalog::VILLAGE_CHARTER_CARD_ITEM_ID, 1, 1,
				&card_item);
		if (result == FT_ERR_SUCCESS)
		{
			card_item->set_instance_id(9001U);
			result = character.get_inventory().add_item(card_item);
		}
		if (result == FT_ERR_SUCCESS)
			result = character.get_inventory().get_revision(&inventory_revision);
		writer_fixture.path = SNAPSHOT_PATH;
		writer_fixture.character = &character;
		writer_fixture.world_items = &world_items;
		writer_fixture.recipe_catalog = &recipes;
		writer_fixture.journal = &journal;
		writer_fixture.card_state = &card_state;
		writer_fixture.rules_state = &rules_state;
		const GameSessionSnapshotWriter writer = {
			&session_snapshot_writer_fixture_save, &writer_fixture};
		if (result == FT_ERR_SUCCESS)
			failure_stage = "prepare-card-transfer-import";
		if (result == FT_ERR_SUCCESS)
			result = WorldItemCardTransferTransaction::prepare_import(
				SNAPSHOT_PATH, character, world_items, recipes, journal, 0,
				inventory_revision, 801U, &writer);
		if (result == FT_ERR_SUCCESS)
		{
			std::ifstream checkpoint(SNAPSHOT_PATH, std::ios::binary);
			if (!checkpoint.is_open())
				result = FT_ERR_NOT_FOUND;
		}
		if (result == FT_ERR_SUCCESS)
			failure_stage = "load-prepared-card-transfer-snapshot";
		if (result == FT_ERR_SUCCESS)
			result = GameSessionPersistence::load_file(SNAPSHOT_PATH,
				recovered_character, recovered_world_items, recovered_recipes,
				recovered_journal, recovered_card_state,
				recovered_rules_state);
		if (result == FT_ERR_SUCCESS)
			failure_stage = "restore-prepared-card-state";
		if (result == FT_ERR_SUCCESS)
			result = recovered_cards->restore_snapshot(
				recovered_card_state.data(), recovered_card_state.size());
		writer_fixture.character = &recovered_character;
		writer_fixture.world_items = &recovered_world_items;
		writer_fixture.recipe_catalog = &recovered_recipes;
		writer_fixture.journal = &recovered_journal;
		writer_fixture.card_state = &recovered_card_state;
		writer_fixture.rules_state = &recovered_rules_state;
		if (result == FT_ERR_SUCCESS)
			failure_stage = "recover-prepared-card-transfer";
		if (result == FT_ERR_SUCCESS)
			result = WorldItemCardTransferTransaction::recover_pending(
				SNAPSHOT_PATH, recovered_character, recovered_world_items,
				recovered_recipes, *recovered_cards, recovered_journal, &writer);
		if (result == FT_ERR_SUCCESS)
			failure_stage = "verify-card-transfer-commit";
		if (result == FT_ERR_SUCCESS)
			result = recovered_journal.find(
				WorldItemCardTransferTransaction::journal_scope(), 801U,
				&record);
		if (result == FT_ERR_SUCCESS
			&& (record.state != GameTransactionJournal::COMMITTED
				|| recovered_character.get_inventory().count_item(
					MinecraftItemCatalog::VILLAGE_CHARTER_CARD_ITEM_ID) != 0
				|| recovered_cards->get_draw_count(&draw_count) != FT_ERR_SUCCESS
				|| draw_count != 17U
				|| recovered_cards->resolve_physical_card_instance(9001U,
					&mapped_instance) != FT_ERR_SUCCESS
				|| mapped_instance == 0U))
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
			failure_stage = "verify-card-transfer-retry";
		if (result == FT_ERR_SUCCESS)
			result = WorldItemCardTransferTransaction::import_to_deck(
				SNAPSHOT_PATH, recovered_character, recovered_world_items,
				recovered_recipes, *recovered_cards, recovered_journal,
				0, inventory_revision, 801U, &imported, &writer);
		if (result == FT_ERR_SUCCESS
			&& (imported.instance_id != mapped_instance
				|| recovered_cards->get_draw_count(&draw_count) != FT_ERR_SUCCESS
				|| draw_count != 17U))
			result = FT_ERR_INVALID_STATE;
		ft_sharedptr<game_item> retry_card_item;
		uint64_t retry_inventory_revision = 0U;
		game_inventory_snapshot retry_inventory_snapshot = {};
		int32_t retry_card_slot = -1;
		uint32_t retry_item_index = 0U;
		if (result == FT_ERR_SUCCESS)
			failure_stage = "add-card-transfer-retry-item";
		if (result == FT_ERR_SUCCESS)
			result = make_world_item(
				MinecraftItemCatalog::VILLAGE_CHARTER_CARD_ITEM_ID,
				1, 1, &retry_card_item);
		if (result == FT_ERR_SUCCESS)
		{
			retry_card_item->set_instance_id(9002U);
			result = recovered_character.get_inventory().add_item(retry_card_item);
		}
		if (result == FT_ERR_SUCCESS)
			failure_stage = "snapshot-card-transfer-retry-inventory";
		if (result == FT_ERR_SUCCESS)
			result = recovered_character.get_inventory().get_revision(
				&retry_inventory_revision);
		if (result == FT_ERR_SUCCESS)
			result = recovered_character.get_inventory().get_snapshot(
				retry_inventory_snapshot);
		while (result == FT_ERR_SUCCESS
			&& retry_item_index < retry_inventory_snapshot.items.size())
		{
			if (retry_inventory_snapshot.items[retry_item_index].instance_id
				== 9002U)
				retry_card_slot = retry_inventory_snapshot.items[
					retry_item_index].slot;
			++retry_item_index;
		}
		if (result == FT_ERR_SUCCESS)
			failure_stage = "find-card-transfer-retry-slot";
		if (result == FT_ERR_SUCCESS && retry_card_slot < 0)
			result = FT_ERR_NOT_FOUND;
		writer_fixture.call_count = 0U;
		writer_fixture.fail_on_call = 2U;
		if (result == FT_ERR_SUCCESS)
			failure_stage = "inject-card-transfer-save-failure";
		if (result == FT_ERR_SUCCESS)
		{
			const int32_t expected_failure =
				WorldItemCardTransferTransaction::import_to_deck(
					SNAPSHOT_PATH, recovered_character, recovered_world_items,
					recovered_recipes, *recovered_cards, recovered_journal,
					retry_card_slot, retry_inventory_revision, 802U, &imported,
					&writer);
			if (expected_failure != FT_ERR_IO)
				result = FT_ERR_INVALID_STATE;
		}
		if (result == FT_ERR_SUCCESS)
			failure_stage = "check-prepared-card-transfer-retry";
		if (result == FT_ERR_SUCCESS)
			result = recovered_journal.find(
				WorldItemCardTransferTransaction::journal_scope(), 802U,
				&record);
		if (result == FT_ERR_SUCCESS
			&& (record.state != GameTransactionJournal::PREPARED
				|| recovered_character.get_inventory().count_item(
					MinecraftItemCatalog::VILLAGE_CHARTER_CARD_ITEM_ID) != 1
				|| recovered_cards->get_draw_count(&draw_count) != FT_ERR_SUCCESS
				|| draw_count != 17U))
			result = FT_ERR_INVALID_STATE;
		writer_fixture.fail_on_call = 0U;
		writer_fixture.call_count = 0U;
		if (result == FT_ERR_SUCCESS)
			failure_stage = "retry-card-transfer-commit";
		if (result == FT_ERR_SUCCESS)
			result = WorldItemCardTransferTransaction::import_to_deck(
				SNAPSHOT_PATH, recovered_character, recovered_world_items,
				recovered_recipes, *recovered_cards, recovered_journal,
				retry_card_slot, retry_inventory_revision, 802U, &imported,
				&writer);
		if (result == FT_ERR_SUCCESS
			&& (recovered_character.get_inventory().count_item(
					MinecraftItemCatalog::VILLAGE_CHARTER_CARD_ITEM_ID) != 0
				|| recovered_cards->resolve_physical_card_instance(9002U,
					&mapped_instance) != FT_ERR_SUCCESS
				|| mapped_instance == 0U))
			result = FT_ERR_INVALID_STATE;
		if (result != FT_ERR_SUCCESS)
			std::fprintf(stderr,
				"world-items: card-transfer recovery detail stage=%s error=%d slot=%d retry-items=%zu\n",
				failure_stage, result, retry_card_slot,
				retry_inventory_snapshot.items.size());
		(void)std::remove(SNAPSHOT_PATH);
		return (result);
	}
}

namespace
{
	struct BoatTransferSaveProbe
	{
		uint32_t calls;
		uint32_t fail_on_call;
	};

	static int32_t boat_transfer_save_probe(void *context) noexcept
	{
		if (context == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		BoatTransferSaveProbe *probe =
			static_cast<BoatTransferSaveProbe *>(context);
		++probe->calls;
		return (probe->calls == probe->fail_on_call
			? FT_ERR_IO : FT_ERR_SUCCESS);
	}

	static void boat_transfer_write_u32(std::vector<uint8_t> &bytes,
		std::size_t offset, uint32_t value)
	{
		for (uint32_t shift = 0U; shift < 32U; shift += 8U)
			bytes[offset + shift / 8U] = static_cast<uint8_t>(value >> shift);
	}

	static void boat_transfer_write_u64(std::vector<uint8_t> &bytes,
		std::size_t offset, uint64_t value)
	{
		for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			bytes[offset + shift / 8U] = static_cast<uint8_t>(value >> shift);
	}
}

static int32_t world_item_validator_boat_transfer() noexcept
{
	static const uint64_t BOAT_INSTANCE_ID = 0xB047U;
	WorldItemStore world_items;
	WorldEntityStore entities;
	GameTransactionJournal journal;
	MinecraftItemCatalog item_catalog;
	game_character character;
	ft_sharedptr<game_item> boat_item;
	game_inventory_snapshot inventory_snapshot = {};
	WorldBoatTransferTransaction::Result placed = {};
	WorldBoatTransferTransaction::Result replayed_place = {};
	WorldBoatTransferTransaction::Result returned = {};
	WorldBoatTransferTransaction::Result replayed_return = {};
	WorldEntitySnapshot boat = {};
	BoatTransferSaveProbe probe = {};
	GameSessionSnapshotWriter writer = {&boat_transfer_save_probe, &probe};
	std::vector<uint8_t> pending_payload(140U, 0U);
	GameTransactionJournal::Record pending = {};
	uint64_t inventory_revision = 0U;
	int32_t result = character.initialize();
	if (result == FT_ERR_SUCCESS)
		result = world_items.initialize();
	if (result == FT_ERR_SUCCESS)
		result = entities.initialize();
	if (result == FT_ERR_SUCCESS)
		result = journal.initialize();
	if (result == FT_ERR_SUCCESS)
		result = item_catalog.initialize();
	if (result == FT_ERR_SUCCESS)
	{
		game_inventory &inventory = character.get_inventory();
		inventory.resize(8U);
		inventory.set_weight_limit(1000);
		result = inventory.get_error();
	}
	if (result == FT_ERR_SUCCESS)
		result = item_catalog.create_item(MinecraftItemCatalog::BOAT_ITEM_ID,
			1, &boat_item);
	if (result == FT_ERR_SUCCESS)
	{
		boat_item->set_instance_id(BOAT_INSTANCE_ID);
		result = character.get_inventory().add_item(boat_item);
	}
	if (result == FT_ERR_SUCCESS)
		result = character.get_inventory().get_snapshot(inventory_snapshot);
	if (result == FT_ERR_SUCCESS)
		inventory_revision = inventory_snapshot.revision;
	EntityState boat_state;
	boat_state.type = static_cast<uint32_t>(EntityState::Kind::BOAT);
	boat_state.position_x = 4.5;
	boat_state.position_y = 63.0;
	boat_state.position_z = -2.5;
	boat_state.orientation_yaw = 1.25;
	boat_state.health = 1;
	boat_state.action_state = static_cast<uint32_t>(
		EntityState::ActionState::IDLE);
	if (result == FT_ERR_SUCCESS)
		result = WorldBoatTransferTransaction::place("boat-transfer-probe",
			character, world_items, entities, journal, 501U, 0,
			inventory_revision, boat_state, &writer, &placed);
	if (result == FT_ERR_SUCCESS
		&& (probe.calls != 2U || placed.replayed
			|| placed.item_instance_id != BOAT_INSTANCE_ID
			|| placed.entity_id == 0U
			|| character.get_inventory().count_item(
				MinecraftItemCatalog::BOAT_ITEM_ID) != 0))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = entities.get(placed.entity_id, &boat);
	if (result == FT_ERR_SUCCESS
		&& (boat.item_instance_id != BOAT_INSTANCE_ID
			|| boat.state.type != static_cast<uint32_t>(EntityState::Kind::BOAT)))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = WorldBoatTransferTransaction::place("boat-transfer-probe",
			character, world_items, entities, journal, 501U, 0,
			inventory_revision, boat_state, &writer, &replayed_place);
	if (result == FT_ERR_SUCCESS
		&& (!replayed_place.replayed
			|| replayed_place.entity_id != placed.entity_id
			|| probe.calls != 2U))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = character.get_inventory().get_snapshot(inventory_snapshot);
	if (result == FT_ERR_SUCCESS)
	{
		probe.fail_on_call = probe.calls + 2U;
		const int32_t failed_retrieve = WorldBoatTransferTransaction::retrieve("boat-transfer-probe",
				character, world_items, entities, item_catalog, journal, 502U,
				placed.entity_id, boat.revision, inventory_snapshot.revision,
				&writer, &returned);
		if (failed_retrieve != FT_ERR_IO)
			result = FT_ERR_INVALID_STATE;
		probe.fail_on_call = 0U;
	}
	if (result == FT_ERR_SUCCESS
		&& (entities.get(placed.entity_id, &boat) != FT_ERR_SUCCESS
			|| boat.item_instance_id != BOAT_INSTANCE_ID
			|| character.get_inventory().count_item(
				MinecraftItemCatalog::BOAT_ITEM_ID) != 0))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = character.get_inventory().get_snapshot(inventory_snapshot);
	if (result == FT_ERR_SUCCESS)
		result = WorldBoatTransferTransaction::retrieve("boat-transfer-probe",
			character, world_items, entities, item_catalog, journal, 503U,
			placed.entity_id, boat.revision, inventory_snapshot.revision,
			&writer, &returned);
	if (result == FT_ERR_SUCCESS
		&& (returned.replayed || returned.item_instance_id != BOAT_INSTANCE_ID
			|| returned.inventory_slot < 0
			|| entities.get(placed.entity_id, &boat) != FT_ERR_NOT_FOUND
			|| character.get_inventory().count_item(
				MinecraftItemCatalog::BOAT_ITEM_ID) != 1))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = WorldBoatTransferTransaction::retrieve("boat-transfer-probe",
			character, world_items, entities, item_catalog, journal, 503U,
			placed.entity_id, returned.entity_id == placed.entity_id
				? 1U : 0U, inventory_revision + 1U, &writer, &replayed_return);
	if (result == FT_ERR_SUCCESS
		&& (!replayed_return.replayed
			|| replayed_return.item_instance_id != BOAT_INSTANCE_ID
			|| replayed_return.inventory_slot != returned.inventory_slot))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		pending_payload[0] = 'W'; pending_payload[1] = 'B';
		pending_payload[2] = 'T'; pending_payload[3] = 'R';
		boat_transfer_write_u32(pending_payload, 4U, 1U);
		boat_transfer_write_u32(pending_payload, 8U, 1U);
		boat_transfer_write_u64(pending_payload, 12U, 504U);
		boat_transfer_write_u64(pending_payload, 20U, inventory_revision);
		boat_transfer_write_u32(pending_payload, 28U, 0U);
		boat_transfer_write_u64(pending_payload, 44U, 900U);
		boat_transfer_write_u32(pending_payload, 52U,
			static_cast<uint32_t>(MinecraftItemCatalog::BOAT_ITEM_ID));
		boat_transfer_write_u32(pending_payload, 56U,
			static_cast<uint32_t>(EntityState::Kind::BOAT));
		uint64_t sequence = 0U;
		bool was_replayed = false;
		result = journal.prepare(WorldBoatTransferTransaction::journal_scope(),
			504U, 8U, pending_payload.data(),
			static_cast<uint32_t>(pending_payload.size()), &sequence,
			&was_replayed);
	}
	if (result == FT_ERR_SUCCESS)
		result = WorldBoatTransferTransaction::recover_pending(
			"boat-transfer-probe", character, world_items, entities, journal,
			&writer);
	if (result == FT_ERR_SUCCESS)
		result = journal.find(WorldBoatTransferTransaction::journal_scope(),
			504U, &pending);
	if (result == FT_ERR_SUCCESS
		&& pending.state != GameTransactionJournal::ABORTED)
		result = FT_ERR_INVALID_STATE;
	(void)character.destroy();
	(void)item_catalog.destroy();
	(void)journal.destroy();
	(void)entities.destroy();
	(void)world_items.destroy();
	return (result);
}

WorldItemStoreValidator::WorldItemStoreValidator()
{
}

WorldItemStoreValidator::WorldItemStoreValidator(
	const WorldItemStoreValidator &other) : IValidator(other)
{
	(void)other;
}

WorldItemStoreValidator::~WorldItemStoreValidator()
{
}

WorldItemStoreValidator &WorldItemStoreValidator::operator=(
	const WorldItemStoreValidator &other)
{
	(void)other;
	return (*this);
}

int WorldItemStoreValidator::validate() const
{
	const int32_t boat_transfer_result = world_item_validator_boat_transfer();
	if (boat_transfer_result != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-items: boat transfer validation failed code=%d\n",
			boat_transfer_result);
		return (1);
	}
	const int32_t entity_store_result =
		world_item_validator_entity_store_roundtrip();
	if (entity_store_result != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-items: entity snapshot/revision validation failed code=%d\n",
			entity_store_result);
		return (1);
	}
	const int32_t animal_simulation_result =
		world_item_validator_animal_simulation();
	if (animal_simulation_result != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-items: animal simulation validation failed code=%d\n",
			animal_simulation_result);
		return (1);
	}
	const int32_t spawner_result = world_item_validator_spawner_lifecycle();
	if (spawner_result != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-items: spawner lifecycle validation failed code=%d\n",
			spawner_result);
		return (1);
	}
	const char *snapshot_file = "world_item_store_validator_snapshot.tmp";
	const char *session_snapshot_file = "game_session_validator_snapshot.tmp";
	const char *duplicate_id_save_file =
		"world_item_duplicate_id_save_validator.tmp";
	const char *pickup_recovery_file = "world_item_pickup_recovery.tmp";
	const char *craft_recovery_file = "world_item_craft_recovery.tmp";
	const char *processing_recovery_file =
		"world_item_processing_recovery.tmp";
	WorldItemStore store;
	WorldItemStore restored_store;
	WorldItemStore session_restored_store;
	WorldItemStore duplicate_id_store;
	WorldItemStore duplicate_restore_store;
	WorldEntityStore item_identity_entities;
	WorldItemStore pickup_recovery_store;
	WorldItemStore pickup_recovered_store;
	WorldItemStore craft_recovery_store;
	WorldItemStore craft_recovered_store;
	game_character saved_character;
	game_character loaded_character;
	game_character pickup_character;
	game_character pickup_recovered_character;
	game_character craft_character;
	game_character craft_recovered_character;
	MinecraftItemCatalog recipe_items;
	MinecraftRecipeCatalog saved_recipe_catalog;
	MinecraftRecipeCatalog loaded_recipe_catalog;
	MinecraftRecipeCatalog legacy_recipe_catalog;
	MinecraftRecipeCatalog legacy_v1_recipe_catalog;
	std::unique_ptr<game_character> legacy_v2_loaded_character(
		new (std::nothrow) game_character());
	std::unique_ptr<WorldItemStore> legacy_v2_restored_store(
		new (std::nothrow) WorldItemStore());
	std::unique_ptr<MinecraftRecipeCatalog> legacy_v2_recipe_catalog(
		new (std::nothrow) MinecraftRecipeCatalog());
	MinecraftRecipeCatalog pickup_recipe_catalog;
	MinecraftRecipeCatalog pickup_recovered_recipe_catalog;
	MinecraftRecipeCatalog craft_recipe_catalog;
	MinecraftRecipeCatalog craft_recovered_recipe_catalog;
	MinecraftRecipeCatalog processing_recipe_catalog;
	MinecraftRecipeCatalog processing_recovered_recipe_catalog;
	GameTransactionJournal saved_transaction_journal;
	GameTransactionJournal loaded_transaction_journal;
	GameTransactionJournal pickup_journal;
	GameTransactionJournal pickup_recovered_journal;
	GameTransactionJournal craft_journal;
	GameTransactionJournal craft_recovered_journal;
	GameTransactionJournal processing_journal;
	GameTransactionJournal processing_recovered_journal;
	WorldItemStore processing_recovery_store;
	WorldItemStore processing_recovered_store;
	game_character processing_character;
	game_character processing_recovered_character;
	game_inventory inventory;
	game_inventory item_identity_inventory;
	ft_vector<game_crafting_missing_ingredient> recipe_missing_groups;
	game_crafting_query_result recipe_query = {};
	ft_bool newly_unlocked = FT_FALSE;
	ft_bool recipe_known = FT_FALSE;
	ft_sharedptr<game_item> pickup_item;
	ft_sharedptr<game_item> carried_stack;
	ft_sharedptr<game_item> blocked_item;
	ft_sharedptr<game_item> merge_source_item;
	ft_sharedptr<game_item> merge_destination_item;
	ft_sharedptr<game_item> query_limit_item;
	ft_sharedptr<game_item> query_scan_item;
	ft_sharedptr<game_item> staged_drop_item;
	ft_sharedptr<game_item> unique_saved_item;
	ft_sharedptr<game_item> unique_ground_item;
	ft_sharedptr<game_item> second_unique_ground_item;
	ft_sharedptr<game_item> craft_ground_item;
	ft_sharedptr<game_item> crafting_log;
	ft_sharedptr<game_item> furnace_input;
	ft_sharedptr<game_item> furnace_fuel;
	ft_sharedptr<game_item> processing_recovery_input;
	ft_sharedptr<game_item> processing_recovery_fuel;
	WorldItemSnapshot snapshot = {};
	std::vector<WorldItemSnapshot> nearby;
	std::vector<uint64_t> query_stress_entity_ids;
	std::vector<uint8_t> snapshot_bytes;
	std::vector<uint8_t> duplicate_id_save_sentinel = {
		'P', 'R', 'E', 'S', 'E', 'R', 'V', 'E'};
	std::vector<uint8_t> saved_craft_cache_bytes;
	std::vector<uint8_t> loaded_craft_cache_bytes;
	std::vector<uint8_t> card_session_test_state =
		{'C', 'S', 'T', 1U, 0xA5U, 0x5AU};
	std::vector<uint8_t> restored_card_session_state;
	std::vector<uint8_t> regeneration_rules_test_state =
		{'R', 'W', 'R', 'S', 1U, 0x6AU};
	std::vector<uint8_t> restored_regeneration_rules_state;
	std::vector<uint8_t> chunk_instability_test_state =
		{'W', 'C', 'I', '1', 1U, 0xB7U};
	std::vector<uint8_t> restored_chunk_instability_state;
	std::vector<uint8_t> revision_metadata_test_state;
	std::vector<uint8_t> restored_revision_metadata_state;
	std::unique_ptr<RegenerationCardSession> card_session_source(
		new (std::nothrow) RegenerationCardSession());
	std::unique_ptr<RegenerationCardSession> card_session_restored(
		new (std::nothrow) RegenerationCardSession());
	std::unique_ptr<RegenerationCardSession> card_session_corrupt(
		new (std::nothrow) RegenerationCardSession());
	std::vector<uint8_t> regeneration_card_bytes;
	uint32_t card_draw_count = 0U;
	uint32_t restored_card_draw_count = 0U;
	uint32_t card_hand_count = 0U;
	uint32_t restored_card_hand_count = 0U;
	WorldItemPickupResult pickup_result = {};
	WorldItemPickupResult successful_pickup = {};
	WorldItemPickupResult recovered_pickup = {};
	ft_sharedptr<game_item> recovery_item;
	ft_sharedptr<game_item> recovery_log;
	MinecraftCraftActionResult recovered_craft = {};
	std::vector<WorldItemSnapshot> recovery_nearby;
	uint64_t pickup_entity_id = 0U;
	uint64_t blocked_entity_id = 0U;
	uint64_t merge_source_id = 0U;
	uint64_t merge_destination_id = 0U;
	uint64_t staged_drop_id = 0U;
	uint64_t inventory_revision = 0U;
	uint64_t pickup_entity_revision = 0U;
	uint64_t recovery_entity_id = 0U;
	uint64_t recovery_inventory_revision = 0U;
	uint64_t recovery_loaded_inventory_revision = 0U;
	uint64_t recovery_retry_inventory_revision = 0U;
	uint64_t craft_inventory_revision = 0U;
	uint64_t restored_craft_inventory_revision = 0U;
	uint64_t craft_retry_inventory_revision = 0U;
	uint64_t pickup_inventory_revision = 0U;
	uint64_t recipe_inventory_revision = 0U;
	uint64_t processing_pre_craft_revision = 0U;
	uint64_t craft_action_inventory_revision = 0U;
	MinecraftCraftActionResult craft_action_result = {};
	MinecraftProcessingActionResult processing_result = {};
	MinecraftProcessingActionResult recovered_processing = {};
	std::vector<uint8_t> processing_restored_card_state;
	std::vector<uint8_t> processing_restored_rules_state;
	std::vector<uint8_t> processing_advance_before;
	std::vector<uint8_t> processing_advance_after;
	std::vector<uint8_t> processing_advance_retry;
	std::vector<uint8_t> processing_advance_after_second;
	SessionSnapshotWriterFixture processing_writer_fixture = {};
	GameTransactionJournal::Record journal_record = {};
	const uint8_t journal_payload[] = {0xA1U, 0xB2U};
	uint64_t journal_sequence = 0U;
	bool journal_replayed = false;
	uint32_t completed_processing_jobs = 0U;
	uint32_t pending_processing_outputs = 0U;
	uint32_t advance_completed_jobs = 0U;
	uint32_t advance_pending_outputs = 0U;
	int32_t error_code;
	const char *legacy_snapshot_stage = "setup";
	const char *session_compatibility_stage = "setup";

	if (card_session_source == nullptr || card_session_restored == nullptr
		|| card_session_corrupt == nullptr
		|| legacy_v2_loaded_character == nullptr
		|| legacy_v2_restored_store == nullptr
		|| legacy_v2_recipe_catalog == nullptr)
		error_code = FT_ERR_NO_MEMORY;
	else
		error_code = validate_inventory_snapshot_restore();
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"world-items: stage=inventory-snapshot-restore error=%d\n",
			error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = validate_card_transfer_journal_recovery();
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"world-items: stage=card-transfer-journal-recovery error=%d\n",
			error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = store.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipe_items.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.initialize(1U, 0);
	if (error_code == FT_ERR_SUCCESS)
		error_code = make_world_item(91, 5, 8, &pickup_item);
	if (error_code == FT_ERR_SUCCESS)
		error_code = make_world_item(91, 6, 8, &carried_stack);
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.add_item(carried_stack);
	if (error_code == FT_ERR_SUCCESS)
		error_code = store.spawn(pickup_item, 0.5, 0.0, 0.5, 1U, 0U,
			0U, &pickup_entity_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = store.query_nearby(0.5, 0.0, 0.5, 2.0, 7U, 1U,
			&nearby);
	if (error_code == FT_ERR_SUCCESS
		&& (nearby.size() != 1U || nearby[0].entity_id != pickup_entity_id))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		pickup_entity_revision = nearby[0].entity_revision;
		error_code = inventory.get_revision(&inventory_revision);
		pickup_inventory_revision = inventory_revision;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = store.pickup(pickup_entity_id, nearby[0].entity_revision,
			7U, 77U, inventory_revision, 5, 0.5, 0.0, 0.5, 1U,
			inventory, &pickup_result);
	if (error_code == FT_ERR_SUCCESS)
		successful_pickup = pickup_result;
	if (error_code == FT_ERR_SUCCESS
		&& (pickup_result.transferred_quantity != 2
			|| pickup_result.remaining_quantity != 3
			|| pickup_result.entity_removed
			|| inventory.count_item(91) != 8))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		WorldItemPickupResult replay = {};

		if (store.pickup(pickup_entity_id, nearby[0].entity_revision, 7U, 77U,
				inventory_revision, 5, 0.5, 0.0, 0.5, 1U, inventory,
				&replay) != FT_ERR_SUCCESS
			|| replay.transferred_quantity != pickup_result.transferred_quantity
			|| replay.entity_revision != pickup_result.entity_revision
			|| inventory.count_item(91) != 8)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS
		&& store.pickup(pickup_entity_id, nearby[0].entity_revision, 7U, 77U,
			inventory_revision, 4, 0.5, 0.0, 0.5, 1U, inventory,
			&pickup_result) != FT_ERR_INVALID_ARGUMENT)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=first-pickup error=%d\n", error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = make_world_item(92, 2, 8, &blocked_item);
	if (error_code == FT_ERR_SUCCESS)
		error_code = store.spawn(blocked_item, 0.5, 0.0, 0.5, 2U, 0U,
			0U, &blocked_entity_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = store.query_nearby(0.5, 0.0, 0.5, 2.0, 7U, 2U,
			&nearby);
	if (error_code == FT_ERR_SUCCESS)
	{
		for (const WorldItemSnapshot &entry : nearby)
		{
			if (entry.entity_id == blocked_entity_id)
				snapshot = entry;
		}
		if (snapshot.entity_id != blocked_entity_id)
			error_code = FT_ERR_NOT_FOUND;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.get_revision(&inventory_revision);
	if (error_code == FT_ERR_SUCCESS
		&& store.pickup(blocked_entity_id, snapshot.entity_revision, 7U, 88U,
			inventory_revision, 2, 0.5, 0.0, 0.5, 2U, inventory,
			&pickup_result) != FT_ERR_FULL)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		inventory.remove_item(0);
		if (store.pickup(blocked_entity_id, snapshot.entity_revision, 7U, 88U,
				inventory_revision, 2, 0.5, 0.0, 0.5, 2U, inventory,
				&pickup_result) != FT_ERR_FULL
			|| pickup_result.transferred_quantity != 0)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS
		&& store.pickup(blocked_entity_id, snapshot.entity_revision, 7U, 88U,
			inventory_revision, 1, 0.5, 0.0, 0.5, 2U, inventory,
			&pickup_result) != FT_ERR_INVALID_ARGUMENT)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		nearby.clear();
		error_code = store.query_nearby(0.5, 0.0, 0.5, 2.0, 7U, 2U,
			&nearby);
		if (error_code == FT_ERR_SUCCESS)
		{
			bool retained = false;
			for (const WorldItemSnapshot &entry : nearby)
			{
				if (entry.entity_id == blocked_entity_id
					&& entry.quantity == 2)
					retained = true;
			}
			if (!retained)
				error_code = FT_ERR_INVALID_STATE;
		}
	}
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=blocked-pickup error=%d\n", error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = make_world_item(94, 5, 8, &merge_source_item);
	if (error_code == FT_ERR_SUCCESS)
		error_code = make_world_item(94, 6, 8, &merge_destination_item);
	if (error_code == FT_ERR_SUCCESS)
		error_code = store.spawn(merge_source_item, 5.0, 0.0, 5.0, 0U, 0U,
			0U, &merge_source_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = store.spawn(merge_destination_item, 5.5, 0.0, 5.0, 0U, 0U,
			0U, &merge_destination_id);
	if (error_code == FT_ERR_SUCCESS)
	{
		std::vector<WorldItemSnapshot> merge_snapshots;
		uint64_t source_revision = 0U;
		uint64_t destination_revision = 0U;
		int32_t transferred = 0;
		error_code = store.query_nearby(5.0, 0.0, 5.0, 1.0, 7U, 3U,
			&merge_snapshots);
		for (const WorldItemSnapshot &entry : merge_snapshots)
		{
			if (entry.entity_id == merge_source_id)
				source_revision = entry.entity_revision;
			if (entry.entity_id == merge_destination_id)
				destination_revision = entry.entity_revision;
		}
		if (error_code == FT_ERR_SUCCESS && (source_revision == 0U
			|| destination_revision == 0U))
			error_code = FT_ERR_NOT_FOUND;
		if (error_code == FT_ERR_SUCCESS)
			error_code = store.merge_into_nearby(merge_source_id, source_revision,
				5.0, 0.0, 5.0, 7U, 3U, &transferred);
		if (error_code == FT_ERR_SUCCESS && transferred != 2)
			error_code = FT_ERR_INVALID_STATE;
		merge_snapshots.clear();
		if (error_code == FT_ERR_SUCCESS)
			error_code = store.query_nearby(5.0, 0.0, 5.0, 1.0, 7U, 3U,
				&merge_snapshots);
		bool source_remaining = false;
		bool destination_full = false;
		for (const WorldItemSnapshot &entry : merge_snapshots)
		{
			if (entry.entity_id == merge_source_id && entry.quantity == 3)
				source_remaining = true;
			if (entry.entity_id == merge_destination_id && entry.quantity == 8)
				destination_full = true;
		}
		if (error_code == FT_ERR_SUCCESS && (!source_remaining
			|| !destination_full))
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
		{
			int32_t no_capacity_transfer = -1;
			if (store.merge_into_nearby(merge_source_id, source_revision + 1U,
					5.0, 0.0, 5.0, 7U, 3U, &no_capacity_transfer)
				!= FT_ERR_SUCCESS || no_capacity_transfer != 0)
				error_code = FT_ERR_INVALID_STATE;
		}
		if (error_code == FT_ERR_SUCCESS
			&& store.merge_stacks(merge_source_id, source_revision,
				merge_destination_id, destination_revision, &transferred)
				!= FT_ERR_INVALID_STATE)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=merge error=%d\n", error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = make_world_item(95, 1, 1, &query_limit_item);
	if (error_code == FT_ERR_SUCCESS)
	{
		try
		{
			query_stress_entity_ids.reserve(1282U);
		}
		catch (const std::bad_alloc &)
		{
			error_code = FT_ERR_NO_MEMORY;
		}
	}
	for (int32_t index = 0; error_code == FT_ERR_SUCCESS && index < 257; ++index)
	{
		uint64_t query_entity_id = 0U;
		error_code = store.spawn(query_limit_item, 100.0, 0.0, 100.0, 0U,
			0U, 0U, &query_entity_id);
		if (error_code == FT_ERR_SUCCESS)
			query_stress_entity_ids.push_back(query_entity_id);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		WorldItemSnapshot sentinel = {};
		sentinel.entity_id = std::numeric_limits<uint64_t>::max();
		nearby.clear();
		nearby.push_back(sentinel);
		if (store.query_nearby(100.0, 0.0, 100.0, 1.0, 7U, 3U, &nearby)
			!= FT_ERR_FULL || nearby.size() != 1U
			|| nearby[0].entity_id != sentinel.entity_id)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=query-limit error=%d\n", error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = make_world_item(96, 1, 1, &query_scan_item);
	for (int32_t index = 0; error_code == FT_ERR_SUCCESS && index < 1025; ++index)
	{
		uint64_t query_entity_id = 0U;
		error_code = store.spawn(query_scan_item, 101.5, 0.0, 100.0, 0U,
			0U, 0U, &query_entity_id);
		if (error_code == FT_ERR_SUCCESS)
			query_stress_entity_ids.push_back(query_entity_id);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		WorldItemSnapshot sentinel = {};
		sentinel.entity_id = std::numeric_limits<uint64_t>::max();
		nearby.clear();
		nearby.push_back(sentinel);
		if (store.query_nearby(100.0, 0.0, 100.0, 1.0, 7U, 3U, &nearby)
			!= FT_ERR_FULL || nearby.size() != 1U
			|| nearby[0].entity_id != sentinel.entity_id)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=query-scan error=%d\n", error_code);
	/* The query ceilings have been exercised; remove those stress-only entities
	 * before the many persistence compatibility round trips below. */
	for (uint64_t entity_id : query_stress_entity_ids)
	{
		if (error_code == FT_ERR_SUCCESS)
			error_code = store.despawn(entity_id, 1U);
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = make_world_item(97, 2, 8, &staged_drop_item);
	if (error_code == FT_ERR_SUCCESS)
		error_code = store.spawn_pending(staged_drop_item, 300.0, 0.0, 300.0,
			4U, 7U, 0U, &staged_drop_id);
	if (error_code == FT_ERR_SUCCESS)
	{
		nearby.clear();
		if (store.query_nearby(300.0, 0.0, 300.0, 1.0, 7U, 4U, &nearby)
			!= FT_ERR_SUCCESS || !nearby.empty()
			|| store.pickup(staged_drop_id, 1U, 7U, 999U,
				pickup_inventory_revision, 1, 300.0, 0.0, 300.0, 4U,
				inventory, &pickup_result) != FT_ERR_NOT_FOUND
			|| store.serialize(snapshot_bytes) != FT_ERR_INVALID_OPERATION
			|| !snapshot_bytes.empty())
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = store.publish_spawn(staged_drop_id, 1U);
	if (error_code == FT_ERR_SUCCESS)
	{
		nearby.clear();
		if (store.publish_spawn(staged_drop_id, 1U) != FT_ERR_SUCCESS
			|| store.query_nearby(300.0, 0.0, 300.0, 1.0, 7U, 4U, &nearby)
				!= FT_ERR_SUCCESS || nearby.size() != 1U
			|| nearby[0].entity_id != staged_drop_id
			|| store.despawn(staged_drop_id, 1U) != FT_ERR_SUCCESS)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = store.serialize(snapshot_bytes);
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=pending-and-publish error=%d\n", error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = restored_store.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = store.save_file(snapshot_file);
	if (error_code == FT_ERR_SUCCESS)
		error_code = restored_store.load_file(snapshot_file);
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=world-item-file error=%d\n", error_code);
	if (error_code == FT_ERR_SUCCESS)
	{
		WorldItemPickupResult replay = {};
		error_code = restored_store.pickup(pickup_entity_id,
			pickup_entity_revision, 7U, 77U, pickup_inventory_revision, 5,
			0.5, 0.0, 0.5, 1U, inventory, &replay);
		if (error_code == FT_ERR_SUCCESS
			&& (replay.entity_id != successful_pickup.entity_id
				|| replay.transferred_quantity
					!= successful_pickup.transferred_quantity
				|| replay.entity_revision != successful_pickup.entity_revision
				|| replay.remaining_quantity != successful_pickup.remaining_quantity))
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		WorldItemPickupResult replay = {};
		if (restored_store.pickup(blocked_entity_id, snapshot.entity_revision,
				7U, 88U, inventory_revision, 2, 0.5, 0.0, 0.5, 2U,
				inventory, &replay) != FT_ERR_FULL
			|| restored_store.pickup(blocked_entity_id, snapshot.entity_revision,
				7U, 88U, inventory_revision, 1, 0.5, 0.0, 0.5, 2U,
				inventory, &replay) != FT_ERR_INVALID_ARGUMENT)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		std::vector<uint8_t> corrupted = snapshot_bytes;
		corrupted[10] ^= 0x01U;
		if (restored_store.deserialize(corrupted.data(), corrupted.size())
			!= FT_ERR_INVALID_ARGUMENT
			|| filesystem_atomic_write(snapshot_file,
				reinterpret_cast<const char *>(corrupted.data()),
				corrupted.size()) != FT_ERR_SUCCESS
			|| restored_store.load_file(snapshot_file)
				!= FT_ERR_INVALID_ARGUMENT)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		nearby.clear();
		error_code = restored_store.query_nearby(0.5, 0.0, 0.5, 2.0, 7U, 2U,
			&nearby);
		bool pickup_entity_retained = false;
		bool blocked_entity_retained = false;
		for (const WorldItemSnapshot &entry : nearby)
		{
			if (entry.entity_id == pickup_entity_id && entry.quantity == 3)
				pickup_entity_retained = true;
			if (entry.entity_id == blocked_entity_id && entry.quantity == 2)
				blocked_entity_retained = true;
		}
		if (error_code == FT_ERR_SUCCESS
			&& (!pickup_entity_retained || !blocked_entity_retained))
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_character.initialize();
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=world-item-replay error=%d\n", error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = loaded_character.initialize();
	if (error_code == FT_ERR_SUCCESS)
	{
		saved_character.set_coins(42);
		game_inventory &saved_inventory = saved_character.get_inventory();
		saved_inventory.resize(8U);
		saved_inventory.set_weight_limit(1000);
		if (saved_inventory.get_error() != FT_ERR_SUCCESS)
			error_code = saved_inventory.get_error();
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ft_sharedptr<game_item> saved_item;
		error_code = make_world_item(93, 4, 8, &saved_item);
		if (error_code == FT_ERR_SUCCESS)
			error_code = saved_character.get_inventory().add_item(saved_item);
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = make_world_item(95, 1, 1, &unique_saved_item);
	if (error_code == FT_ERR_SUCCESS)
	{
		unique_saved_item->set_instance_id(7000U);
		error_code = saved_character.get_inventory().add_item(unique_saved_item);
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipe_items.create_item(
			MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
				+ static_cast<int32_t>(VOXEL_GENERATOR_OAK_LOG_BLOCK),
			1, &crafting_log);
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_character.get_inventory().add_item(crafting_log);
	if (error_code == FT_ERR_SUCCESS)
		error_code = make_world_item(96, 1, 1, &unique_ground_item);
	if (error_code == FT_ERR_SUCCESS)
	{
		uint64_t unique_ground_entity_id = 0U;
		unique_ground_item->set_instance_id(9000U);
		error_code = store.spawn(unique_ground_item, 8.5, 0.0, 8.5,
			3U, 0U, 0U, &unique_ground_entity_id);
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = make_world_item(97, 1, 1, &second_unique_ground_item);
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=inventory-fixture error=%d\n", error_code);
	if (error_code == FT_ERR_SUCCESS)
		second_unique_ground_item->set_instance_id(9100U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = duplicate_id_store.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = item_identity_entities.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = item_identity_inventory.initialize(8U, 1000);
	if (error_code == FT_ERR_SUCCESS)
	{
		uint64_t duplicate_entity_id = 0U;
		error_code = duplicate_id_store.spawn(unique_saved_item, 20.5, 0.0,
			20.5, 3U, 0U, 0U, &duplicate_entity_id);
		if (error_code == FT_ERR_SUCCESS
			&& duplicate_id_store.validate_instance_id_uniqueness(
				saved_character.get_inventory()) != FT_ERR_ALREADY_EXISTS)
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
			error_code = filesystem_atomic_write(duplicate_id_save_file,
				reinterpret_cast<const char *>(
					duplicate_id_save_sentinel.data()),
				duplicate_id_save_sentinel.size());
		if (error_code == FT_ERR_SUCCESS
			&& GameSessionPersistence::save_file(duplicate_id_save_file,
				saved_character, duplicate_id_store) != FT_ERR_ALREADY_EXISTS)
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
		{
			std::ifstream sentinel_input(duplicate_id_save_file, std::ios::binary);
			const std::vector<uint8_t> preserved_bytes(
				(std::istreambuf_iterator<char>(sentinel_input)),
				std::istreambuf_iterator<char>());
			if (!sentinel_input.is_open() || sentinel_input.bad()
				|| preserved_bytes != duplicate_id_save_sentinel)
				error_code = FT_ERR_INVALID_STATE;
		}
		if (error_code == FT_ERR_SUCCESS)
			error_code = duplicate_id_store.despawn(duplicate_entity_id, 1U);
		if (error_code == FT_ERR_SUCCESS)
			error_code = duplicate_id_store.spawn(unique_ground_item, 20.5,
				0.0, 20.5, 3U, 0U, 0U, &duplicate_entity_id);
		if (error_code == FT_ERR_SUCCESS)
		{
			if (duplicate_id_store.spawn(unique_ground_item, 21.5, 0.0,
				20.5, 3U, 0U, 0U, &duplicate_entity_id)
				!= FT_ERR_ALREADY_EXISTS)
				error_code = FT_ERR_INVALID_STATE;
		}
		if (error_code == FT_ERR_SUCCESS
			&& duplicate_id_store.validate_instance_id_uniqueness(
				saved_character.get_inventory()) != FT_ERR_SUCCESS)
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
		{
			EntityState boat_state;
			uint32_t boat_entity_id = 0U;
			std::vector<uint64_t> additional_item_ids;
			boat_state.type = static_cast<uint32_t>(EntityState::Kind::BOAT);
			boat_state.position_x = 20.5;
			boat_state.position_y = 2.0;
			boat_state.position_z = 20.5;
			error_code = item_identity_entities.spawn_item(boat_state, 9000U,
				&boat_entity_id);
			if (error_code == FT_ERR_SUCCESS
				&& duplicate_id_store.validate_instance_id_uniqueness(
					item_identity_inventory, &item_identity_entities)
					!= FT_ERR_ALREADY_EXISTS)
				error_code = FT_ERR_INVALID_STATE;
			if (error_code == FT_ERR_SUCCESS)
				error_code = item_identity_entities.despawn(boat_entity_id, 1U);
			if (error_code == FT_ERR_SUCCESS)
				error_code = item_identity_entities.spawn_item(boat_state, 8000U,
					&boat_entity_id);
			if (error_code == FT_ERR_SUCCESS
				&& duplicate_id_store.validate_instance_id_uniqueness(
					item_identity_inventory, &item_identity_entities)
					!= FT_ERR_SUCCESS)
				error_code = FT_ERR_INVALID_STATE;
			if (error_code == FT_ERR_SUCCESS)
			{
				additional_item_ids.push_back(9000U);
				if (duplicate_id_store.validate_instance_id_uniqueness(
						item_identity_inventory, &item_identity_entities,
						additional_item_ids) != FT_ERR_ALREADY_EXISTS)
					error_code = FT_ERR_INVALID_STATE;
				additional_item_ids[0] = 9001U;
				if (error_code == FT_ERR_SUCCESS
					&& duplicate_id_store.validate_instance_id_uniqueness(
						item_identity_inventory, &item_identity_entities,
						additional_item_ids) != FT_ERR_SUCCESS)
					error_code = FT_ERR_INVALID_STATE;
			}
		}
		if (error_code == FT_ERR_SUCCESS)
			error_code = duplicate_id_store.spawn(second_unique_ground_item,
				21.5, 0.0, 20.5, 3U, 0U, 0U, &duplicate_entity_id);
		if (error_code == FT_ERR_SUCCESS && duplicate_entity_id != 3U)
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
			error_code = duplicate_id_store.serialize(snapshot_bytes);
		if (error_code == FT_ERR_SUCCESS)
		{
			for (uint32_t byte_index = 0U; byte_index < 8U; ++byte_index)
				snapshot_bytes[256U + byte_index]
					= snapshot_bytes[116U + byte_index];
			const uint64_t corrupted_checksum = session_snapshot_checksum(
				snapshot_bytes.data(), snapshot_bytes.size() - 8U);
			for (uint32_t byte_index = 0U; byte_index < 8U; ++byte_index)
				snapshot_bytes[snapshot_bytes.size() - 8U + byte_index]
					= static_cast<uint8_t>(corrupted_checksum
						>> (byte_index * 8U));
		}
		if (error_code == FT_ERR_SUCCESS)
			error_code = duplicate_restore_store.initialize();
		if (error_code == FT_ERR_SUCCESS
			&& duplicate_restore_store.deserialize(snapshot_bytes.data(),
				snapshot_bytes.size()) != FT_ERR_INVALID_ARGUMENT)
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
		{
			nearby.clear();
			error_code = duplicate_restore_store.query_nearby(20.5, 0.0,
				20.5, 2.0, 7U, 3U, &nearby);
			if (error_code == FT_ERR_SUCCESS && !nearby.empty())
				error_code = FT_ERR_INVALID_STATE;
		}
	}
	(void)duplicate_id_store.destroy();
	(void)duplicate_restore_store.destroy();
	(void)item_identity_entities.destroy();
	(void)item_identity_inventory.destroy();
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=identity-validation error=%d\n", error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = session_restored_store.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_recipe_catalog.initialize(recipe_items);
	if (error_code == FT_ERR_SUCCESS)
		error_code = loaded_recipe_catalog.initialize(recipe_items);
	if (error_code == FT_ERR_SUCCESS)
		error_code = legacy_recipe_catalog.initialize(recipe_items);
	if (error_code == FT_ERR_SUCCESS)
		error_code = legacy_v1_recipe_catalog.initialize(recipe_items);
	if (error_code == FT_ERR_SUCCESS)
		error_code = legacy_v2_recipe_catalog->initialize(recipe_items);
	if (error_code == FT_ERR_SUCCESS)
		error_code = legacy_v2_loaded_character->initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = legacy_v2_restored_store->initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipe_missing_groups.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_recipe_catalog.unlock_recipe(2008U,
			0x5749535000000001ULL, &newly_unlocked);
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=recipe-init error=%d\n", error_code);
	if (error_code == FT_ERR_SUCCESS && newly_unlocked != FT_TRUE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_recipe_catalog.restore_next_output_instance_id(5002U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_character.get_inventory().get_revision(
			&recipe_inventory_revision);
	processing_pre_craft_revision = recipe_inventory_revision;
	if (error_code == FT_ERR_SUCCESS)
		craft_action_inventory_revision = recipe_inventory_revision;
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_recipe_catalog.craft_hand_recipe(
			saved_character.get_inventory(), 77U, 1001U, 1,
			recipe_inventory_revision, &craft_action_result);
	if (error_code == FT_ERR_SUCCESS
		&& (craft_action_result.status != FT_ERR_SUCCESS
			|| craft_action_result.replayed
			|| saved_character.get_inventory().count_item(
				MinecraftItemCatalog::OAK_PLANKS_ITEM_ID) != 4))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& saved_recipe_catalog.craft_hand_recipe(
			saved_character.get_inventory(), 78U, 1001U, 1,
			recipe_inventory_revision, &craft_action_result)
		!= FT_ERR_INVALID_STATE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=craft-setup error=%d\n", error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipe_items.create_item(
			MinecraftItemCatalog::RAW_BEEF_ITEM_ID, 1, &furnace_input);
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipe_items.create_item(
			MinecraftItemCatalog::COAL_ITEM_ID, 1, &furnace_fuel);
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_character.get_inventory().add_item(furnace_input);
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_character.get_inventory().add_item(furnace_fuel);
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_character.get_inventory().get_revision(
			&recipe_inventory_revision);
	processing_pre_craft_revision = recipe_inventory_revision;
	if (error_code == FT_ERR_SUCCESS
		&& saved_recipe_catalog.craft_recipe_at_station(
			saved_character.get_inventory(), 299U, 1032U, 1,
			recipe_inventory_revision,
			MinecraftRecipeCatalog::FURNACE_STATION_ID, 0, 64, 0, 1U,
			&craft_action_result) != FT_ERR_INVALID_OPERATION)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& (saved_recipe_catalog.craft_recipe_at_station(
			saved_character.get_inventory(), 299U, 1032U, 1,
			recipe_inventory_revision,
			MinecraftRecipeCatalog::FURNACE_STATION_ID, 0, 64, 0, 1U,
			&craft_action_result) != FT_ERR_INVALID_OPERATION
			|| !craft_action_result.replayed
			|| saved_recipe_catalog.craft_recipe_at_station(
				saved_character.get_inventory(), 299U, 1032U, 2,
				recipe_inventory_revision,
				MinecraftRecipeCatalog::FURNACE_STATION_ID, 0, 64, 0, 1U,
				&craft_action_result) != FT_ERR_INVALID_ARGUMENT))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"world-items: timed-recipe rejection error=%d status=%d replay=%d raw=%d coal=%d revision=%llu\n",
			error_code, craft_action_result.status,
			craft_action_result.replayed ? 1 : 0,
			saved_character.get_inventory().count_item(
				MinecraftItemCatalog::RAW_BEEF_ITEM_ID),
			saved_character.get_inventory().count_item(
				MinecraftItemCatalog::COAL_ITEM_ID),
			static_cast<unsigned long long>(recipe_inventory_revision));
	if (error_code == FT_ERR_SUCCESS
		&& (saved_character.get_inventory().count_item(
				MinecraftItemCatalog::RAW_BEEF_ITEM_ID) != 1
			|| saved_character.get_inventory().count_item(
				MinecraftItemCatalog::COAL_ITEM_ID) != 1
			|| saved_character.get_inventory().get_revision(
				&recipe_inventory_revision) != FT_ERR_SUCCESS
			|| recipe_inventory_revision != processing_pre_craft_revision))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_recipe_catalog.start_processing(
			saved_character.get_inventory(), 300U, 1032U, 1,
			recipe_inventory_revision, 0, 64, 0, 1U, &processing_result);
	if (error_code == FT_ERR_SUCCESS
		&& (processing_result.status != FT_ERR_SUCCESS
			|| processing_result.replayed
			|| processing_result.ticks_remaining != 200U
			|| saved_character.get_inventory().count_item(
				MinecraftItemCatalog::RAW_BEEF_ITEM_ID) != 0
			|| saved_character.get_inventory().count_item(
				MinecraftItemCatalog::COAL_ITEM_ID) != 0))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"world-items: stage=processing-start error=%d status=%d replay=%d ticks=%u raw=%d coal=%d revision=%llu\n",
			error_code, processing_result.status,
			processing_result.replayed ? 1 : 0,
			processing_result.ticks_remaining,
			saved_character.get_inventory().count_item(
				MinecraftItemCatalog::RAW_BEEF_ITEM_ID),
			saved_character.get_inventory().count_item(
				MinecraftItemCatalog::COAL_ITEM_ID),
			static_cast<unsigned long long>(recipe_inventory_revision));
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_recipe_catalog.start_processing(
			saved_character.get_inventory(), 300U, 1032U, 1,
			recipe_inventory_revision, 0, 64, 0, 1U, &processing_result);
	if (error_code == FT_ERR_SUCCESS
		&& (!processing_result.replayed
			|| saved_character.get_inventory().count_item(
				MinecraftItemCatalog::COOKED_BEEF_ITEM_ID) != 0))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_recipe_catalog.advance_processing(
			saved_character.get_inventory(), 75U, 64U,
			&completed_processing_jobs, &pending_processing_outputs);
	if (error_code == FT_ERR_SUCCESS
		&& (completed_processing_jobs != 0U
			|| saved_character.get_inventory().count_item(
				MinecraftItemCatalog::COOKED_BEEF_ITEM_ID) != 0))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=processing-first-75 error=%d\n", error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_recipe_catalog.serialize_craft_action_cache(
			saved_craft_cache_bytes);
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::save_file(session_snapshot_file,
		saved_character, store, saved_recipe_catalog);
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::load_file(session_snapshot_file,
		loaded_character, session_restored_store, loaded_recipe_catalog);
	if (error_code == FT_ERR_SUCCESS)
		error_code = loaded_recipe_catalog.serialize_craft_action_cache(
			loaded_craft_cache_bytes);
	if (error_code == FT_ERR_SUCCESS
		&& saved_craft_cache_bytes != loaded_craft_cache_bytes)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = loaded_recipe_catalog.start_processing(
			loaded_character.get_inventory(), 300U, 1032U, 1,
			recipe_inventory_revision, 0, 64, 0, 1U, &processing_result);
	if (error_code == FT_ERR_SUCCESS && !processing_result.replayed)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = loaded_recipe_catalog.advance_processing(
			loaded_character.get_inventory(), 124U, 64U,
			&completed_processing_jobs, &pending_processing_outputs);
	if (error_code == FT_ERR_SUCCESS
		&& (completed_processing_jobs != 0U
			|| loaded_character.get_inventory().count_item(
				MinecraftItemCatalog::COOKED_BEEF_ITEM_ID) != 0))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = loaded_recipe_catalog.advance_processing(
			loaded_character.get_inventory(), 1U, 64U,
			&completed_processing_jobs, &pending_processing_outputs);
	if (error_code == FT_ERR_SUCCESS
		&& (completed_processing_jobs != 1U
			|| loaded_character.get_inventory().count_item(
				MinecraftItemCatalog::COOKED_BEEF_ITEM_ID) != 1))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=processing-restored-completion error=%d\n", error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = loaded_recipe_catalog.is_recipe_known(2008U,
			&recipe_known);
	if (error_code == FT_ERR_SUCCESS && recipe_known != FT_TRUE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=restored-recipe-known error=%d known=%u\n", error_code, static_cast<unsigned>(recipe_known));
	if (error_code == FT_ERR_SUCCESS
		&& loaded_recipe_catalog.craft_hand_recipe(
			loaded_character.get_inventory(), 77U, 1001U, 1,
			craft_action_inventory_revision, &craft_action_result)
		!= FT_ERR_SUCCESS)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& (!craft_action_result.replayed
			|| loaded_character.get_inventory().count_item(
				MinecraftItemCatalog::OAK_PLANKS_ITEM_ID) != 4
			|| loaded_character.get_inventory().count_item(
				MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
					+ static_cast<int32_t>(VOXEL_GENERATOR_OAK_LOG_BLOCK)) != 0))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=restored-craft-replay error=%d replay=%u status=%d planks=%d logs=%d\n", error_code, static_cast<unsigned>(craft_action_result.replayed), craft_action_result.status, loaded_character.get_inventory().count_item(MinecraftItemCatalog::OAK_PLANKS_ITEM_ID), loaded_character.get_inventory().count_item(MinecraftItemCatalog::BLOCK_ITEM_ID_BASE + static_cast<int32_t>(VOXEL_GENERATOR_OAK_LOG_BLOCK)));
	if (error_code == FT_ERR_SUCCESS
		&& loaded_recipe_catalog.craft_hand_recipe(
			loaded_character.get_inventory(), 78U, 1001U, 1,
			craft_action_inventory_revision, &craft_action_result)
		!= FT_ERR_INVALID_STATE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS && !craft_action_result.replayed)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& loaded_recipe_catalog.craft_hand_recipe(
			loaded_character.get_inventory(), 77U, 1002U, 1,
			craft_action_inventory_revision, &craft_action_result)
		!= FT_ERR_INVALID_ARGUMENT)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& loaded_recipe_catalog.next_output_instance_id() != 9001U)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& loaded_recipe_catalog.restore_next_output_instance_id(5002U)
		!= FT_ERR_OUT_OF_RANGE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& loaded_recipe_catalog.next_output_instance_id() != 9001U)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = loaded_recipe_catalog.crafting().query_recipe_at_station(
			2008, inventory, MinecraftRecipeCatalog::CARD_PRESS_STATION_ID,
			&recipe_query, &recipe_missing_groups);
	if (error_code == FT_ERR_SUCCESS
		&& (recipe_query.known != FT_TRUE || recipe_query.found != FT_TRUE))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& (loaded_character.get_coins() != 42
			|| loaded_character.get_inventory().count_item(93) != 4
			|| loaded_character.get_inventory().count_item(95) != 1))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		nearby.clear();
		error_code = session_restored_store.query_nearby(0.5, 0.0, 0.5,
			2.0, 7U, 2U, &nearby);
		if (error_code == FT_ERR_SUCCESS && nearby.size() != 2U)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		std::ifstream input(session_snapshot_file, std::ios::binary);
		std::vector<uint8_t> corrupted(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		if (!input.is_open() || input.bad() || corrupted.size() < 32U)
			error_code = FT_ERR_IO;
		else
		{
			input.close();
			corrupted[8] ^= 0x01U;
			if (filesystem_atomic_write(session_snapshot_file,
				reinterpret_cast<const char *>(corrupted.data()),
				corrupted.size()) != FT_ERR_SUCCESS
				|| GameSessionPersistence::load_file(session_snapshot_file,
					loaded_character, session_restored_store,
					loaded_recipe_catalog)
					!= FT_ERR_INVALID_ARGUMENT
				|| loaded_character.get_coins() != 42
				|| loaded_character.get_inventory().count_item(93) != 4
				|| loaded_recipe_catalog.next_output_instance_id() != 9001U)
				error_code = FT_ERR_INVALID_STATE;
		}
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = loaded_recipe_catalog.is_recipe_known(2008U,
			&recipe_known);
		if (error_code == FT_ERR_SUCCESS && recipe_known != FT_TRUE)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::save_file(session_snapshot_file,
			saved_character, store, saved_recipe_catalog);
	if (error_code == FT_ERR_SUCCESS)
	{
		legacy_snapshot_stage = "derive-v4";
		std::ifstream input(session_snapshot_file, std::ios::binary);
		std::vector<uint8_t> corrupted(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		uint64_t character_size;
		uint64_t item_size;
		uint64_t recipe_size;
		std::size_t recipe_offset;
		uint64_t processing_size;
		uint64_t checksum;

		if (!input.is_open() || input.bad() || corrupted.size() < 64U
			|| corrupted[4] != 5U)
			error_code = FT_ERR_IO;
		else
		{
			input.close();
			character_size = session_snapshot_read_u64(corrupted, 8U);
			item_size = session_snapshot_read_u64(corrupted, 16U);
			recipe_size = session_snapshot_read_u64(corrupted, 24U);
			processing_size = session_snapshot_read_u64(corrupted, 48U);
			if (recipe_size < 4U
				|| character_size > corrupted.size() - 64U
				|| item_size > corrupted.size() - 64U - character_size
				|| recipe_size > corrupted.size() - 64U - character_size
					- item_size
				|| processing_size > corrupted.size() - 64U
					- character_size - item_size - recipe_size)
				error_code = FT_ERR_INVALID_ARGUMENT;
			else
			{
				recipe_offset = 56U
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(item_size);
				corrupted[recipe_offset
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size) - 1U] ^= 0x01U;
				checksum = session_snapshot_checksum(corrupted.data(),
					corrupted.size() - 8U);
				uint32_t shift = 0U;
				while (shift < 64U)
				{
					corrupted[corrupted.size() - 8U + shift / 8U]
						= static_cast<uint8_t>(checksum >> shift);
					shift += 8U;
				}
				if (filesystem_atomic_write(session_snapshot_file,
					reinterpret_cast<const char *>(corrupted.data()),
					corrupted.size()) != FT_ERR_SUCCESS
					|| GameSessionPersistence::load_file(session_snapshot_file,
						loaded_character, session_restored_store,
						loaded_recipe_catalog) != FT_ERR_INVALID_ARGUMENT
					|| loaded_character.get_coins() != 42
					|| loaded_character.get_inventory().count_item(93) != 4
					|| loaded_recipe_catalog.next_output_instance_id() != 9001U)
					error_code = FT_ERR_INVALID_STATE;
			}
		}
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = loaded_recipe_catalog.is_recipe_known(2008U,
			&recipe_known);
		if (error_code == FT_ERR_SUCCESS && recipe_known != FT_TRUE)
			error_code = FT_ERR_INVALID_STATE;
		nearby.clear();
		if (error_code == FT_ERR_SUCCESS)
			error_code = session_restored_store.query_nearby(0.5, 0.0, 0.5,
				2.0, 7U, 2U, &nearby);
		if (error_code == FT_ERR_SUCCESS && nearby.size() != 2U)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::save_file(session_snapshot_file,
			saved_character, store, saved_recipe_catalog);
	if (error_code == FT_ERR_SUCCESS)
	{
		std::ifstream input(session_snapshot_file, std::ios::binary);
		std::vector<uint8_t> corrupted(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		uint64_t character_size;
		uint64_t item_size;
		uint64_t recipe_size;
		uint64_t action_cache_size;
		uint64_t processing_size;
		std::size_t action_cache_offset;
		uint64_t checksum;

		if (!input.is_open() || input.bad() || corrupted.size() < 64U
			|| corrupted[4] != 5U)
			error_code = FT_ERR_IO;
		else
		{
			input.close();
			character_size = session_snapshot_read_u64(corrupted, 8U);
			item_size = session_snapshot_read_u64(corrupted, 16U);
			recipe_size = session_snapshot_read_u64(corrupted, 24U);
			action_cache_size = session_snapshot_read_u64(corrupted, 40U);
			processing_size = session_snapshot_read_u64(corrupted, 48U);
			if (action_cache_size < 28U || processing_size < 28U
				|| character_size > corrupted.size() - 64U
				|| item_size > corrupted.size() - 64U - character_size
				|| recipe_size > corrupted.size() - 64U - character_size
					- item_size
				|| action_cache_size > corrupted.size() - 64U
					- character_size - item_size - recipe_size
				|| processing_size > corrupted.size() - 64U
					- character_size - item_size - recipe_size
					- action_cache_size)
				error_code = FT_ERR_INVALID_ARGUMENT;
			else
			{
				action_cache_offset = 56U
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size);
				corrupted[action_cache_offset
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(action_cache_size) - 1U] ^= 0x01U;
				checksum = session_snapshot_checksum(corrupted.data(),
					corrupted.size() - 8U);
				for (uint32_t shift = 0U; shift < 64U; shift += 8U)
					corrupted[corrupted.size() - 8U + shift / 8U]
						= static_cast<uint8_t>(checksum >> shift);
				if (filesystem_atomic_write(session_snapshot_file,
					reinterpret_cast<const char *>(corrupted.data()),
					corrupted.size()) != FT_ERR_SUCCESS
					|| GameSessionPersistence::load_file(session_snapshot_file,
						loaded_character, session_restored_store,
						loaded_recipe_catalog) != FT_ERR_INVALID_ARGUMENT
					|| loaded_character.get_coins() != 42
					|| loaded_character.get_inventory().count_item(93) != 4
					|| loaded_recipe_catalog.next_output_instance_id() != 9001U)
					error_code = FT_ERR_INVALID_STATE;
			}
		}
	}
	if (error_code == FT_ERR_SUCCESS
		&& loaded_recipe_catalog.craft_hand_recipe(
			loaded_character.get_inventory(), 77U, 1001U, 1,
			craft_action_inventory_revision, &craft_action_result)
		!= FT_ERR_SUCCESS)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& (!craft_action_result.replayed
			|| loaded_character.get_inventory().count_item(
				MinecraftItemCatalog::OAK_PLANKS_ITEM_ID) != 4))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::save_file(session_snapshot_file,
			saved_character, store, saved_recipe_catalog);
	if (error_code == FT_ERR_SUCCESS)
	{
		std::ifstream input(session_snapshot_file, std::ios::binary);
		std::vector<uint8_t> version_five(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		std::vector<uint8_t> version_four;
		std::vector<uint8_t> version_three;
		uint64_t character_size;
		uint64_t item_size;
		uint64_t recipe_size;
		uint64_t action_cache_size;
		uint64_t processing_size;
		std::size_t action_cache_offset;
		uint64_t checksum;

		if (!input.is_open() || input.bad() || version_five.size() < 64U
			|| version_five[4] != 5U)
			error_code = FT_ERR_IO;
		else
		{
			input.close();
			character_size = session_snapshot_read_u64(version_five, 8U);
			item_size = session_snapshot_read_u64(version_five, 16U);
			recipe_size = session_snapshot_read_u64(version_five, 24U);
			action_cache_size = session_snapshot_read_u64(version_five, 40U);
			processing_size = session_snapshot_read_u64(version_five, 48U);
			if (character_size > version_five.size() - 64U
				|| item_size > version_five.size() - 64U - character_size
				|| recipe_size > version_five.size() - 64U - character_size
					- item_size)
				error_code = FT_ERR_INVALID_ARGUMENT;
			if (error_code == FT_ERR_SUCCESS)
				action_cache_offset = 56U
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size);
		if (error_code == FT_ERR_SUCCESS && (action_cache_size < 28U
			|| processing_size < 32U
			|| action_cache_offset > version_five.size() - 8U
			|| action_cache_size > version_five.size() - 8U
				- action_cache_offset
			|| processing_size != version_five.size() - 8U
				- action_cache_offset - action_cache_size))
				error_code = FT_ERR_INVALID_ARGUMENT;
			if (error_code == FT_ERR_SUCCESS)
			{
				try
				{
					version_four.insert(version_four.end(),
						version_five.begin(), version_five.begin() + 48);
					version_four[4] = 4U;
					version_four[5] = 0U;
					version_four[6] = 0U;
					version_four[7] = 0U;
					version_four.insert(version_four.end(),
						version_five.begin() + 56,
						version_five.begin()
							+ static_cast<std::ptrdiff_t>(action_cache_offset
								+ action_cache_size));
					checksum = session_snapshot_checksum(
						version_four.data(), version_four.size());
					for (uint32_t shift = 0U; shift < 64U; shift += 8U)
						version_four.push_back(static_cast<uint8_t>(
							checksum >> shift));
					if (filesystem_atomic_write(session_snapshot_file,
						reinterpret_cast<const char *>(version_four.data()),
						version_four.size()) != FT_ERR_SUCCESS)
						error_code = FT_ERR_IO;
					if (error_code == FT_ERR_SUCCESS)
						error_code = GameSessionPersistence::load_file(
							session_snapshot_file, loaded_character,
							session_restored_store, legacy_recipe_catalog);
					if (error_code == FT_ERR_SUCCESS
						&& (legacy_recipe_catalog.next_output_instance_id() != 9001U
							|| legacy_recipe_catalog.craft_hand_recipe(
								loaded_character.get_inventory(), 77U, 1001U, 1,
								craft_action_inventory_revision, &craft_action_result)
							!= FT_ERR_SUCCESS || !craft_action_result.replayed))
						error_code = FT_ERR_INVALID_STATE;
					if (error_code == FT_ERR_SUCCESS)
					{
					version_three.insert(version_three.end(),
						version_four.begin(), version_four.begin() + 40);
					version_three[4] = 3U;
					version_three[5] = 0U;
					version_three[6] = 0U;
					version_three[7] = 0U;
					version_three.insert(version_three.end(),
						version_four.begin() + 48,
						version_four.begin() + 48
							+ static_cast<std::ptrdiff_t>(character_size + item_size
								+ recipe_size));
					checksum = session_snapshot_checksum(
						version_three.data(), version_three.size());
					for (uint32_t shift = 0U; shift < 64U; shift += 8U)
						version_three.push_back(static_cast<uint8_t>(
							checksum >> shift));
					}
				}
				catch (const std::bad_alloc &)
				{
					error_code = FT_ERR_NO_MEMORY;
				}
				if (error_code == FT_ERR_SUCCESS
					&& filesystem_atomic_write(session_snapshot_file,
						reinterpret_cast<const char *>(version_three.data()),
						version_three.size()) != FT_ERR_SUCCESS)
					error_code = FT_ERR_IO;
				if (error_code == FT_ERR_SUCCESS)
					error_code = GameSessionPersistence::load_file(
						session_snapshot_file, loaded_character,
						session_restored_store, legacy_recipe_catalog);
			}
		}
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		legacy_snapshot_stage = "derive-v1-and-v3";
		int32_t first_action_status;

		if (error_code == FT_ERR_SUCCESS)
		{
			first_action_status = legacy_recipe_catalog.craft_hand_recipe(
				loaded_character.get_inventory(), 77U, 1001U, 1,
				recipe_inventory_revision, &craft_action_result);
			if (first_action_status == FT_ERR_SUCCESS
				|| craft_action_result.replayed)
				error_code = FT_ERR_INVALID_STATE;
			else if (legacy_recipe_catalog.craft_hand_recipe(
					loaded_character.get_inventory(), 77U, 1001U, 1,
					recipe_inventory_revision, &craft_action_result)
				!= first_action_status || !craft_action_result.replayed)
				error_code = FT_ERR_INVALID_STATE;
		}
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::save_file(session_snapshot_file,
			saved_character, store);
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::load_file(session_snapshot_file,
			loaded_character, session_restored_store,
			legacy_v1_recipe_catalog);
	if (error_code == FT_ERR_SUCCESS)
		error_code = legacy_v1_recipe_catalog.is_recipe_known(2001U,
			&recipe_known);
	if (error_code == FT_ERR_SUCCESS && recipe_known != FT_TRUE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = legacy_v1_recipe_catalog.is_recipe_known(2008U,
			&recipe_known);
	if (error_code == FT_ERR_SUCCESS && recipe_known != FT_FALSE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = loaded_character.get_inventory().get_revision(
			&recipe_inventory_revision);
	if (error_code == FT_ERR_SUCCESS)
	{
		const int32_t first_action_status =
			legacy_v1_recipe_catalog.craft_hand_recipe(
				loaded_character.get_inventory(), 77U, 1001U, 1,
				recipe_inventory_revision, &craft_action_result);
		if (first_action_status == FT_ERR_SUCCESS
			|| craft_action_result.replayed)
			error_code = FT_ERR_INVALID_STATE;
		else if (legacy_v1_recipe_catalog.craft_hand_recipe(
				loaded_character.get_inventory(), 77U, 1001U, 1,
				recipe_inventory_revision, &craft_action_result)
			!= first_action_status || !craft_action_result.replayed)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		legacy_snapshot_stage = "source-v2-recipe-book";
		error_code = saved_recipe_catalog.is_recipe_known(1001U,
			&recipe_known);
	}
	if (error_code == FT_ERR_SUCCESS && recipe_known != FT_TRUE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		legacy_snapshot_stage = "save-v2-recipe-book";
		error_code = GameSessionPersistence::save_file(session_snapshot_file,
			saved_character, store, saved_recipe_catalog.recipe_book());
	}
	if (error_code == FT_ERR_SUCCESS
		&& !session_snapshot_has_version(session_snapshot_file, 2U))
	{
		legacy_snapshot_stage = "check-v2-header";
		error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		legacy_snapshot_stage = "load-v2-recipe-book";
		error_code = GameSessionPersistence::load_file(session_snapshot_file,
			*legacy_v2_loaded_character, *legacy_v2_restored_store,
			*legacy_v2_recipe_catalog);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		legacy_snapshot_stage = "query-v2-recipe-book";
		error_code = legacy_v2_recipe_catalog->is_recipe_known(1001U,
			&recipe_known);
	}
	if (error_code == FT_ERR_SUCCESS && recipe_known != FT_TRUE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"world-items: stage=legacy-snapshot-checks substage=%s error=%d recipe_known=%u\n",
			legacy_snapshot_stage, error_code,
			static_cast<unsigned>(recipe_known));
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_transaction_journal.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_transaction_journal.prepare(501U, 1U, 17U,
			journal_payload, sizeof(journal_payload), &journal_sequence,
			&journal_replayed);
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_transaction_journal.mark_committed(501U, 1U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = saved_transaction_journal.prepare(501U, 2U, 18U,
			nullptr, 0U, &journal_sequence, &journal_replayed);
	if (error_code == FT_ERR_SUCCESS)
		error_code = loaded_transaction_journal.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = pickup_recovery_store.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = pickup_recovered_store.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = pickup_character.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = pickup_recovered_character.initialize();
	if (error_code == FT_ERR_SUCCESS)
	{
		pickup_character.get_inventory().resize(8U);
		error_code = pickup_character.get_inventory().get_error();
	}
	if (error_code == FT_ERR_SUCCESS)
		pickup_character.get_inventory().set_weight_limit(
			std::numeric_limits<int32_t>::max());
	if (error_code == FT_ERR_SUCCESS)
	{
		pickup_recovered_character.get_inventory().resize(8U);
		error_code = pickup_recovered_character.get_inventory().get_error();
	}
	if (error_code == FT_ERR_SUCCESS)
		pickup_recovered_character.get_inventory().set_weight_limit(
			std::numeric_limits<int32_t>::max());
	if (error_code == FT_ERR_SUCCESS)
		error_code = pickup_recipe_catalog.initialize(recipe_items);
	if (error_code == FT_ERR_SUCCESS)
		error_code = pickup_recovered_recipe_catalog.initialize(recipe_items);
	if (error_code == FT_ERR_SUCCESS)
		error_code = pickup_journal.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = pickup_recovered_journal.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = make_world_item(92, 3, 8, &recovery_item);
	if (error_code == FT_ERR_SUCCESS)
		error_code = pickup_recovery_store.spawn(recovery_item, 4.5, 2.0, 4.5,
			0U, 0U, 0U, &recovery_entity_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = pickup_recovery_store.query_nearby(4.5, 2.0, 4.5, 1.0,
			1U, 0U, &recovery_nearby);
	if (error_code == FT_ERR_SUCCESS && recovery_nearby.size() != 1U)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = pickup_character.get_inventory().get_revision(
			&recovery_inventory_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemPickupTransaction::prepare(pickup_recovery_file,
			pickup_character, pickup_recovery_store, pickup_recipe_catalog,
			pickup_journal, recovery_nearby[0], 1U, 1U,
			recovery_inventory_revision, 4.5, 2.0, 4.5, 1U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::load_file(pickup_recovery_file,
			pickup_recovered_character, pickup_recovered_store,
			pickup_recovered_recipe_catalog, pickup_recovered_journal);
	if (error_code == FT_ERR_SUCCESS)
		error_code = pickup_recovered_character.get_inventory().get_revision(
			&recovery_loaded_inventory_revision);
	if (error_code == FT_ERR_SUCCESS
		&& recovery_loaded_inventory_revision != recovery_inventory_revision)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemPickupTransaction::recover_pending(
			pickup_recovery_file, pickup_recovered_character,
			pickup_recovered_store, pickup_recovered_recipe_catalog,
			pickup_recovered_journal);
	if (error_code == FT_ERR_SUCCESS
		&& pickup_recovered_character.get_inventory().count_item(92) != 3)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = pickup_recovered_journal.find(
			WorldItemPickupTransaction::journal_scope(), 1U, &journal_record);
	if (error_code == FT_ERR_SUCCESS
		&& journal_record.state != GameTransactionJournal::COMMITTED)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemPickupTransaction::execute(pickup_recovery_file,
			pickup_recovered_character, pickup_recovered_store,
			pickup_recovered_recipe_catalog, pickup_recovered_journal,
			recovery_nearby[0], 1U, 1U, recovery_inventory_revision,
			4.5, 2.0, 4.5, 1U, &recovered_pickup);
	if (error_code == FT_ERR_SUCCESS)
		error_code = pickup_recovered_character.get_inventory().get_revision(
			&recovery_retry_inventory_revision);
	if (error_code == FT_ERR_SUCCESS
		&& (pickup_recovered_character.get_inventory().count_item(92) != 3
			|| recovery_retry_inventory_revision
				!= recovered_pickup.inventory_revision))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = craft_recovery_store.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = craft_recovered_store.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = craft_character.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = craft_recovered_character.initialize();
	if (error_code == FT_ERR_SUCCESS)
	{
		craft_character.get_inventory().resize(8U);
		error_code = craft_character.get_inventory().get_error();
	}
	if (error_code == FT_ERR_SUCCESS)
		craft_character.get_inventory().set_weight_limit(
			std::numeric_limits<int32_t>::max());
	if (error_code == FT_ERR_SUCCESS)
	{
		craft_recovered_character.get_inventory().resize(8U);
		error_code = craft_recovered_character.get_inventory().get_error();
	}
	if (error_code == FT_ERR_SUCCESS)
		craft_recovered_character.get_inventory().set_weight_limit(
			std::numeric_limits<int32_t>::max());
	if (error_code == FT_ERR_SUCCESS)
		error_code = craft_recipe_catalog.initialize(recipe_items);
	if (error_code == FT_ERR_SUCCESS)
		error_code = craft_recovered_recipe_catalog.initialize(recipe_items);
	if (error_code == FT_ERR_SUCCESS)
		error_code = craft_journal.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = craft_recovered_journal.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipe_items.create_item(
			MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
				+ static_cast<int32_t>(VOXEL_GENERATOR_OAK_LOG_BLOCK),
			1, &recovery_log);
	if (error_code == FT_ERR_SUCCESS)
		error_code = craft_character.get_inventory().add_item(recovery_log);
	if (error_code == FT_ERR_SUCCESS)
		error_code = craft_character.get_inventory().get_revision(
			&craft_inventory_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemCraftTransaction::prepare(craft_recovery_file,
			craft_character, craft_recovery_store, craft_recipe_catalog,
			craft_journal, 1U, 1001U, 1, craft_inventory_revision,
			static_cast<uint32_t>(MinecraftRecipeCatalog::HAND_STATION_ID),
			0, 0, 0, 0U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::load_file(craft_recovery_file,
			craft_recovered_character, craft_recovered_store,
			craft_recovered_recipe_catalog, craft_recovered_journal);
	if (error_code == FT_ERR_SUCCESS)
		error_code = craft_recovered_character.get_inventory().get_revision(
			&restored_craft_inventory_revision);
	if (error_code == FT_ERR_SUCCESS
		&& restored_craft_inventory_revision != craft_inventory_revision)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = make_world_item(98, 1, 1, &craft_ground_item);
	if (error_code == FT_ERR_SUCCESS)
	{
		uint64_t entity_id = 0U;
		craft_ground_item->set_instance_id(12000U);
		error_code = craft_recovered_store.spawn(craft_ground_item,
			24.5, 0.0, 24.5, 3U, 0U, 0U, &entity_id);
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemCraftTransaction::recover_pending(
			craft_recovery_file, craft_recovered_character,
			craft_recovered_store, craft_recovered_recipe_catalog,
			craft_recovered_journal);
	if (error_code == FT_ERR_SUCCESS
		&& (craft_recovered_character.get_inventory().count_item(
				MinecraftItemCatalog::OAK_PLANKS_ITEM_ID) != 4
			|| craft_recovered_character.get_inventory().count_item(
				MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
					+ static_cast<int32_t>(VOXEL_GENERATOR_OAK_LOG_BLOCK)) != 0))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = craft_recovered_journal.find(
			WorldItemCraftTransaction::journal_scope(), 1U, &journal_record);
	if (error_code == FT_ERR_SUCCESS
		&& journal_record.state != GameTransactionJournal::COMMITTED)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& craft_recovered_recipe_catalog.next_output_instance_id() != 12001U)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemCraftTransaction::execute(craft_recovery_file,
			craft_recovered_character, craft_recovered_store,
			craft_recovered_recipe_catalog, craft_recovered_journal, 1U,
			1001U, 1, craft_inventory_revision,
			static_cast<uint32_t>(MinecraftRecipeCatalog::HAND_STATION_ID),
			0, 0, 0, 0U, &recovered_craft);
	if (error_code == FT_ERR_SUCCESS)
		error_code = craft_recovered_character.get_inventory().get_revision(
			&craft_retry_inventory_revision);
	if (error_code == FT_ERR_SUCCESS
		&& (!recovered_craft.replayed
			|| craft_retry_inventory_revision
				!= recovered_craft.inventory_revision
			|| craft_recovered_character.get_inventory().count_item(
				MinecraftItemCatalog::OAK_PLANKS_ITEM_ID) != 4))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_recovery_store.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_recovered_store.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_character.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_recovered_character.initialize();
	if (error_code == FT_ERR_SUCCESS)
	{
		processing_character.get_inventory().resize(8U);
		error_code = processing_character.get_inventory().get_error();
	}
	if (error_code == FT_ERR_SUCCESS)
		processing_character.get_inventory().set_weight_limit(
			std::numeric_limits<int32_t>::max());
	if (error_code == FT_ERR_SUCCESS)
	{
		processing_recovered_character.get_inventory().resize(8U);
		error_code = processing_recovered_character.get_inventory().get_error();
	}
	if (error_code == FT_ERR_SUCCESS)
		processing_recovered_character.get_inventory().set_weight_limit(
			std::numeric_limits<int32_t>::max());
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_recipe_catalog.initialize(recipe_items);
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_recovered_recipe_catalog.initialize(
			recipe_items);
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_journal.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_recovered_journal.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipe_items.create_item(
			MinecraftItemCatalog::RAW_BEEF_ITEM_ID, 1,
			&processing_recovery_input);
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipe_items.create_item(
			MinecraftItemCatalog::COAL_ITEM_ID, 1,
			&processing_recovery_fuel);
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_character.get_inventory().add_item(
			processing_recovery_input);
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_character.get_inventory().add_item(
			processing_recovery_fuel);
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_character.get_inventory().get_revision(
			&recipe_inventory_revision);
	processing_writer_fixture.path = processing_recovery_file;
	processing_writer_fixture.character = &processing_character;
	processing_writer_fixture.world_items = &processing_recovery_store;
	processing_writer_fixture.recipe_catalog = &processing_recipe_catalog;
	processing_writer_fixture.journal = &processing_journal;
	processing_writer_fixture.card_state = &card_session_test_state;
	processing_writer_fixture.rules_state = &regeneration_rules_test_state;
	const GameSessionSnapshotWriter processing_writer = {
		&session_snapshot_writer_fixture_save, &processing_writer_fixture};
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemProcessingTransaction::prepare(
			processing_recovery_file, processing_character,
			processing_recovery_store, processing_recipe_catalog,
			processing_journal, 7001U, 1032U, 1,
			recipe_inventory_revision, 0, 0, 0, 1U, &processing_writer);
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::load_file(
			processing_recovery_file, processing_recovered_character,
			processing_recovered_store, processing_recovered_recipe_catalog,
			processing_recovered_journal, processing_restored_card_state,
			processing_restored_rules_state);
	if (error_code == FT_ERR_SUCCESS
		&& (processing_restored_card_state != card_session_test_state
			|| processing_restored_rules_state != regeneration_rules_test_state
			|| processing_recovered_character.get_inventory().count_item(
				MinecraftItemCatalog::RAW_BEEF_ITEM_ID) != 1
			|| processing_recovered_character.get_inventory().count_item(
				MinecraftItemCatalog::COAL_ITEM_ID) != 1))
		error_code = FT_ERR_INVALID_STATE;
	processing_writer_fixture.character = &processing_recovered_character;
	processing_writer_fixture.world_items = &processing_recovered_store;
	processing_writer_fixture.recipe_catalog =
		&processing_recovered_recipe_catalog;
	processing_writer_fixture.journal = &processing_recovered_journal;
	processing_writer_fixture.card_state = &processing_restored_card_state;
	processing_writer_fixture.rules_state = &processing_restored_rules_state;
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemProcessingTransaction::recover_pending(
			processing_recovery_file, processing_recovered_character,
			processing_recovered_store, processing_recovered_recipe_catalog,
			processing_recovered_journal, &processing_writer);
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_recovered_journal.find(
			WorldItemProcessingTransaction::journal_scope(), 7001U,
			&journal_record);
	if (error_code == FT_ERR_SUCCESS
		&& (journal_record.state != GameTransactionJournal::COMMITTED
			|| processing_recovered_character.get_inventory().count_item(
				MinecraftItemCatalog::RAW_BEEF_ITEM_ID) != 0
			|| processing_recovered_character.get_inventory().count_item(
				MinecraftItemCatalog::COAL_ITEM_ID) != 0
			|| processing_restored_card_state != card_session_test_state
			|| processing_restored_rules_state != regeneration_rules_test_state))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemProcessingTransaction::execute(
			processing_recovery_file, processing_recovered_character,
			processing_recovered_store, processing_recovered_recipe_catalog,
			processing_recovered_journal, 7001U, 1032U, 1,
			recipe_inventory_revision, 0, 0, 0, 1U,
			&recovered_processing,
			&processing_writer);
	if (error_code == FT_ERR_SUCCESS
		&& (!recovered_processing.replayed
			|| recovered_processing.ticks_remaining != 200U
			|| processing_recovered_character.get_inventory().count_item(
				MinecraftItemCatalog::RAW_BEEF_ITEM_ID) != 0
			|| processing_recovered_character.get_inventory().count_item(
				MinecraftItemCatalog::COAL_ITEM_ID) != 0))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_recovered_recipe_catalog.serialize_processing_state(
			processing_advance_before);
	if (error_code == FT_ERR_SUCCESS)
	{
		processing_writer_fixture.fail_on_call =
			processing_writer_fixture.call_count + 2U;
		const int32_t advance_error =
			WorldItemProcessingAdvanceTransaction::execute(
				processing_recovery_file, processing_recovered_character,
				processing_recovered_store, processing_recovered_recipe_catalog,
				processing_recovered_journal, 1U, 10U, 64U,
				&advance_completed_jobs, &advance_pending_outputs,
				&processing_writer);
		if (advance_error != FT_ERR_IO)
			error_code = FT_ERR_INVALID_STATE;
		processing_writer_fixture.fail_on_call = 0U;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_recovered_recipe_catalog.serialize_processing_state(
			processing_advance_after);
	if (error_code == FT_ERR_SUCCESS
		&& processing_advance_after != processing_advance_before)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemProcessingAdvanceTransaction::recover_pending(
			processing_recovery_file, processing_recovered_character,
			processing_recovered_store, processing_recovered_recipe_catalog,
			processing_recovered_journal, &processing_writer);
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_recovered_journal.find(
			WorldItemProcessingAdvanceTransaction::journal_scope(), 1U,
			&journal_record);
	if (error_code == FT_ERR_SUCCESS
		&& journal_record.state != GameTransactionJournal::COMMITTED)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_recovered_recipe_catalog.serialize_processing_state(
			processing_advance_after);
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = WorldItemProcessingAdvanceTransaction::execute(
			processing_recovery_file, processing_recovered_character,
			processing_recovered_store, processing_recovered_recipe_catalog,
			processing_recovered_journal, 1U, 10U, 64U,
			&advance_completed_jobs, &advance_pending_outputs,
			&processing_writer);
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_recovered_recipe_catalog.serialize_processing_state(
			processing_advance_retry);
	if (error_code == FT_ERR_SUCCESS
		&& processing_advance_retry != processing_advance_after)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemProcessingAdvanceTransaction::execute(
			processing_recovery_file, processing_recovered_character,
			processing_recovered_store, processing_recovered_recipe_catalog,
			processing_recovered_journal, 2U, 5U, 64U,
			&advance_completed_jobs, &advance_pending_outputs,
			&processing_writer);
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_recovered_recipe_catalog.serialize_processing_state(
			processing_advance_after_second);
	if (error_code == FT_ERR_SUCCESS
		&& WorldItemProcessingAdvanceTransaction::execute(
			processing_recovery_file, processing_recovered_character,
			processing_recovered_store, processing_recovered_recipe_catalog,
			processing_recovered_journal, 1U, 10U, 64U,
			&advance_completed_jobs, &advance_pending_outputs,
			&processing_writer) != FT_ERR_INVALID_ARGUMENT)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = processing_recovered_recipe_catalog.serialize_processing_state(
			processing_advance_retry);
	if (error_code == FT_ERR_SUCCESS
		&& processing_advance_retry != processing_advance_after_second)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"world-items: stage=processing-transaction-recovery error=%d\n",
			error_code);
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = GameSessionPersistence::save_file(session_snapshot_file,
			saved_character, store, saved_recipe_catalog,
			saved_transaction_journal);
		if (error_code != FT_ERR_SUCCESS)
			std::fprintf(stderr, "world-items: journal v6 save failed=%d\n",
				error_code);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = GameSessionPersistence::load_file(session_snapshot_file,
			loaded_character, session_restored_store, loaded_recipe_catalog,
			loaded_transaction_journal);
		if (error_code != FT_ERR_SUCCESS)
			std::fprintf(stderr, "world-items: journal v6 load failed=%d\n",
				error_code);
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = loaded_transaction_journal.find(501U, 1U,
			&journal_record);
	if (error_code == FT_ERR_SUCCESS
		&& (journal_record.state != GameTransactionJournal::COMMITTED
			|| journal_record.payload.size() != sizeof(journal_payload)))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = loaded_transaction_journal.find(501U, 2U,
			&journal_record);
	if (error_code == FT_ERR_SUCCESS
		&& journal_record.state != GameTransactionJournal::PREPARED)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		std::ifstream input(session_snapshot_file, std::ios::binary);
		std::vector<uint8_t> corrupted(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		uint64_t character_size;
		uint64_t item_size;
		uint64_t recipe_size;
		uint64_t cache_size;
		uint64_t processing_size;
		uint64_t journal_size;
		std::size_t journal_offset;
		uint64_t checksum;

		if (!input.is_open() || input.bad() || corrupted.size() < 72U
			|| corrupted[4] != 6U)
			error_code = FT_ERR_IO;
		else
		{
			input.close();
			character_size = session_snapshot_read_u64(corrupted, 8U);
			item_size = session_snapshot_read_u64(corrupted, 16U);
			recipe_size = session_snapshot_read_u64(corrupted, 24U);
			cache_size = session_snapshot_read_u64(corrupted, 40U);
			processing_size = session_snapshot_read_u64(corrupted, 48U);
			journal_size = session_snapshot_read_u64(corrupted, 56U);
			if (journal_size < 32U
				|| character_size > corrupted.size() - 72U
				|| item_size > corrupted.size() - 72U - character_size
				|| recipe_size > corrupted.size() - 72U - character_size
					- item_size
				|| cache_size > corrupted.size() - 72U - character_size
					- item_size - recipe_size
				|| processing_size > corrupted.size() - 72U - character_size
					- item_size - recipe_size - cache_size
				|| journal_size != corrupted.size() - 72U - character_size
					- item_size - recipe_size - cache_size - processing_size)
				error_code = FT_ERR_INVALID_ARGUMENT;
			else
			{
				journal_offset = 64U + FT_UINT64_TO_NATIVE_SIZE_CAST(character_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(item_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(recipe_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(cache_size)
					+ FT_UINT64_TO_NATIVE_SIZE_CAST(processing_size);
				corrupted[journal_offset + 4U] ^= 0x01U;
				checksum = session_snapshot_checksum(corrupted.data(),
					corrupted.size() - 8U);
				for (uint32_t shift = 0U; shift < 64U; shift += 8U)
					corrupted[corrupted.size() - 8U + shift / 8U]
						= static_cast<uint8_t>(checksum >> shift);
				if (filesystem_atomic_write(session_snapshot_file,
					reinterpret_cast<const char *>(corrupted.data()),
					corrupted.size()) != FT_ERR_SUCCESS
					|| GameSessionPersistence::load_file(session_snapshot_file,
						loaded_character, session_restored_store,
						loaded_recipe_catalog, loaded_transaction_journal)
						!= FT_ERR_INVALID_ARGUMENT
					|| loaded_character.get_coins() != 42)
					error_code = FT_ERR_INVALID_STATE;
			}
		}
		if (error_code == FT_ERR_SUCCESS)
		{
			error_code = loaded_transaction_journal.find(501U, 2U,
				&journal_record);
			if (error_code == FT_ERR_SUCCESS
				&& journal_record.state != GameTransactionJournal::PREPARED)
				error_code = FT_ERR_INVALID_STATE;
		}
	}
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=transaction-recovery error=%d\n",
			error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::save_file(session_snapshot_file,
			saved_character, store, saved_recipe_catalog,
			saved_transaction_journal, card_session_test_state,
			regeneration_rules_test_state, chunk_instability_test_state);
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: v8 save error=%d\n", error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::load_file(session_snapshot_file,
			loaded_character, session_restored_store, loaded_recipe_catalog,
			loaded_transaction_journal, restored_card_session_state,
			restored_regeneration_rules_state, restored_chunk_instability_state);
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: v8 load error=%d\n", error_code);
	if (error_code == FT_ERR_SUCCESS
		&& (card_session_test_state != restored_card_session_state
			|| regeneration_rules_test_state
				!= restored_regeneration_rules_state
			|| chunk_instability_test_state
				!= restored_chunk_instability_state))
	{
		error_code = FT_ERR_INVALID_STATE;
		std::fprintf(stderr,
			"world-items: v8 payload mismatch card=%zu/%zu rules=%zu/%zu\n",
			restored_card_session_state.size(), card_session_test_state.size(),
			restored_regeneration_rules_state.size(),
			regeneration_rules_test_state.size());
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		session_compatibility_stage = "serialize-v10-metadata";
		error_code = WorldRevisionMetadataStore::serialize(7U,
			std::vector<WorldRevisionManager::RevisionChunk>(),
			WorldRevisionCostPolicy{}, 0U, false, false, 0, 0, false,
			0, 0, 0, 0U, revision_metadata_test_state);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		session_compatibility_stage = "save-v10";
		error_code = GameSessionPersistence::save_file(session_snapshot_file,
			saved_character, store, saved_recipe_catalog,
			saved_transaction_journal, card_session_test_state,
			regeneration_rules_test_state, chunk_instability_test_state,
			revision_metadata_test_state);
	}
	if (error_code == FT_ERR_SUCCESS
		&& !session_snapshot_has_version(session_snapshot_file, 10U))
	{
		session_compatibility_stage = "check-v10-header";
		error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		session_compatibility_stage = "load-v10";
		error_code = GameSessionPersistence::load_file(session_snapshot_file,
			loaded_character, session_restored_store, loaded_recipe_catalog,
			loaded_transaction_journal, restored_card_session_state,
			restored_regeneration_rules_state,
			restored_chunk_instability_state,
			restored_revision_metadata_state);
	}
	if (error_code == FT_ERR_SUCCESS
		&& (revision_metadata_test_state != restored_revision_metadata_state
			|| card_session_test_state != restored_card_session_state
			|| regeneration_rules_test_state
				!= restored_regeneration_rules_state
			|| chunk_instability_test_state
				!= restored_chunk_instability_state))
	{
		session_compatibility_stage = "compare-v10";
		error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		session_compatibility_stage = "validate-v10-metadata";
		error_code = validate_revision_metadata_payload(
			restored_revision_metadata_state);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		const std::vector<uint8_t> fluid_state_test =
			{0x46U, 0x4CU, 0x55U, 0x49U, 0x44U, 0x01U, 0x80U};
		std::vector<uint8_t> restored_fluid_state;
		std::vector<uint8_t> restored_entity_state;

		session_compatibility_stage = "save-v11";
		error_code = GameSessionPersistence::save_file(session_snapshot_file,
			saved_character, store, saved_recipe_catalog,
			saved_transaction_journal, card_session_test_state,
			regeneration_rules_test_state, chunk_instability_test_state,
			revision_metadata_test_state, fluid_state_test);
		if (error_code == FT_ERR_SUCCESS
			&& !session_snapshot_has_version(session_snapshot_file, 11U))
		{
			session_compatibility_stage = "check-v11-header";
			error_code = FT_ERR_INVALID_STATE;
		}
		if (error_code == FT_ERR_SUCCESS)
		{
			session_compatibility_stage = "load-v11";
			error_code = GameSessionPersistence::load_file(session_snapshot_file,
				loaded_character, session_restored_store, loaded_recipe_catalog,
				loaded_transaction_journal, restored_card_session_state,
				restored_regeneration_rules_state,
				restored_chunk_instability_state,
				restored_revision_metadata_state, restored_fluid_state,
				restored_entity_state);
		}
		if (error_code == FT_ERR_SUCCESS
			&& (restored_fluid_state != fluid_state_test
				|| restored_revision_metadata_state
					!= revision_metadata_test_state
				|| restored_card_session_state != card_session_test_state
				|| !restored_entity_state.empty()
				|| restored_regeneration_rules_state
					!= regeneration_rules_test_state
				|| restored_chunk_instability_state
				!= chunk_instability_test_state))
		{
			session_compatibility_stage = "compare-v11";
			std::fprintf(stderr,
				"world-items: v11 payload fluid=%zu/%zu metadata=%d card=%d rules=%d instability=%d\n",
				restored_fluid_state.size(), fluid_state_test.size(),
				restored_revision_metadata_state
					== revision_metadata_test_state,
				restored_card_session_state == card_session_test_state,
				restored_regeneration_rules_state
					== regeneration_rules_test_state,
				restored_chunk_instability_state
					== chunk_instability_test_state);
			error_code = FT_ERR_INVALID_STATE;
		}
		if (error_code == FT_ERR_SUCCESS)
		{
			std::ifstream input(session_snapshot_file, std::ios::binary);
			std::vector<uint8_t> corrupted(
				(std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			const std::vector<uint8_t> sentinel = {0xD7U, 0x11U};

			if (!input.is_open() || input.bad() || corrupted.size() < 80U)
				error_code = FT_ERR_IO;
			else
			{
				input.close();
				corrupted[corrupted.size() - 9U] ^= 0x01U;
				error_code = filesystem_atomic_write(session_snapshot_file,
					reinterpret_cast<const char *>(corrupted.data()),
					corrupted.size());
				if (error_code == FT_ERR_SUCCESS)
				{
					restored_card_session_state = sentinel;
					restored_regeneration_rules_state = sentinel;
					restored_chunk_instability_state = sentinel;
					restored_revision_metadata_state = sentinel;
					restored_fluid_state = sentinel;
					const int32_t corrupt_result =
						GameSessionPersistence::load_file(session_snapshot_file,
							loaded_character, session_restored_store,
							loaded_recipe_catalog, loaded_transaction_journal,
							restored_card_session_state,
							restored_regeneration_rules_state,
							restored_chunk_instability_state,
							restored_revision_metadata_state,
							restored_fluid_state);
					if (corrupt_result != FT_ERR_INVALID_ARGUMENT
						|| restored_card_session_state != sentinel
						|| restored_regeneration_rules_state != sentinel
						|| restored_chunk_instability_state != sentinel
						|| restored_revision_metadata_state != sentinel
						|| restored_fluid_state != sentinel
						|| loaded_character.get_coins() != 42)
						error_code = FT_ERR_INVALID_STATE;
				}
			}
		}
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		std::ifstream input(session_snapshot_file, std::ios::binary);
		std::vector<uint8_t> corrupted(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		const std::vector<uint8_t> sentinel = {0xD1U, 0x4EU};

		if (!input.is_open() || input.bad() || corrupted.size() < 16U)
			error_code = FT_ERR_IO;
		else
		{
			input.close();
			corrupted[corrupted.size() - 9U] ^= 0x01U;
			error_code = filesystem_atomic_write(session_snapshot_file,
				reinterpret_cast<const char *>(corrupted.data()),
				corrupted.size());
			if (error_code == FT_ERR_SUCCESS)
			{
				restored_card_session_state = sentinel;
				restored_regeneration_rules_state = sentinel;
				restored_chunk_instability_state = sentinel;
				restored_revision_metadata_state = sentinel;
				const int32_t corrupt_load_result =
					GameSessionPersistence::load_file(session_snapshot_file,
						loaded_character, session_restored_store,
						loaded_recipe_catalog, loaded_transaction_journal,
						restored_card_session_state,
						restored_regeneration_rules_state,
						restored_chunk_instability_state,
						restored_revision_metadata_state);
				if (corrupt_load_result != FT_ERR_INVALID_ARGUMENT
					|| restored_card_session_state != sentinel
					|| restored_regeneration_rules_state != sentinel
					|| restored_chunk_instability_state != sentinel
					|| restored_revision_metadata_state != sentinel
					|| loaded_character.get_coins() != 42)
					error_code = FT_ERR_INVALID_STATE;
			}
		}
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		WorldEntityStore entity_source;
		WorldEntityStore entity_restored;
		WorldSpawnerStore spawner_source;
		WorldSpawnerStore spawner_restored;
		WorldItemStore v14_restored_store;
		game_character v14_restored_character;
		MinecraftRecipeCatalog v14_restored_catalog;
		GameTransactionJournal v14_restored_journal;
		EntityState entity;
		WorldSpawnerDefinition spawner_definition = {};
		std::vector<uint8_t> entity_state;
		std::vector<uint8_t> restored_entity_state;
		std::vector<WorldEntitySnapshot> restored_entities;
		const std::vector<uint8_t> v13_fluid_state =
			{0x46U, 0x4CU, 0x55U, 0x49U, 0x44U, 0x02U};
		std::vector<uint8_t> v13_restored_fluid_state;
		const std::vector<uint8_t> v15_crop_policy_state = {
			0xC8U, 0x00U, 0x00U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U,
			0x08U, 0x0CU, 0x06U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
		const std::vector<uint8_t> v16_crop_growth_state =
			session_crop_growth_state_fixture();
		std::vector<uint8_t> restored_crop_policy_state;
		uint64_t restored_output_session_id = 0U;
		uint32_t entity_id = 0U;
		uint32_t spawner_id = 0U;

		error_code = entity_source.initialize();
		if (error_code == FT_ERR_SUCCESS)
			error_code = entity_restored.initialize();
		if (error_code == FT_ERR_SUCCESS)
			error_code = spawner_source.initialize();
		if (error_code == FT_ERR_SUCCESS)
			error_code = spawner_restored.initialize();
		entity.type = static_cast<uint32_t>(EntityState::Kind::ZOMBIE);
		entity.position_x = 31.0;
		entity.position_y = 68.0;
		entity.position_z = -9.0;
		entity.health = 20;
		if (error_code == FT_ERR_SUCCESS)
			error_code = entity_source.spawn(entity, &entity_id);
		if (error_code == FT_ERR_SUCCESS)
		{
			spawner_definition.x = 31.0;
			spawner_definition.y = 68.0;
			spawner_definition.z = -9.0;
			spawner_definition.entity_type = static_cast<uint32_t>(
				EntityState::Kind::SKELETON);
			spawner_definition.maximum_alive = 2U;
			spawner_definition.spawn_interval_ticks = 20U;
			spawner_definition.loot_table_id = 17U;
			error_code = spawner_source.add(spawner_definition, 100U,
				&spawner_id);
		}
		if (error_code == FT_ERR_SUCCESS)
			error_code = GameSessionEntityState::serialize(entity_source,
				spawner_source, entity_state);
		if (error_code == FT_ERR_SUCCESS)
		{
			session_compatibility_stage = "save-v13-spawners";
			error_code = GameSessionPersistence::save_file(
				session_snapshot_file, saved_character, store,
				saved_recipe_catalog, saved_transaction_journal,
				card_session_test_state, regeneration_rules_test_state,
				chunk_instability_test_state, revision_metadata_test_state,
				v13_fluid_state, entity_state);
		}
		if (error_code == FT_ERR_SUCCESS
			&& !session_snapshot_has_version(session_snapshot_file, 13U))
		{
			session_compatibility_stage = "check-v13-header";
			error_code = FT_ERR_INVALID_STATE;
		}
		if (error_code == FT_ERR_SUCCESS)
		{
			session_compatibility_stage = "load-v13-spawners";
			error_code = GameSessionPersistence::load_file(
				session_snapshot_file, loaded_character,
				session_restored_store, loaded_recipe_catalog,
				loaded_transaction_journal, restored_card_session_state,
				restored_regeneration_rules_state,
				restored_chunk_instability_state,
				restored_revision_metadata_state, v13_restored_fluid_state,
				restored_entity_state);
		}
		if (error_code == FT_ERR_SUCCESS
			&& (restored_entity_state != entity_state
				|| GameSessionEntityState::restore(restored_entity_state.data(),
					restored_entity_state.size(), entity_restored,
					spawner_restored) != FT_ERR_SUCCESS))
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
		{
			error_code = entity_restored.snapshot(restored_entities);
			if (error_code == FT_ERR_SUCCESS
				&& (restored_entities.size() != 1U
					|| restored_entities[0].state.id != entity_id))
				error_code = FT_ERR_INVALID_STATE;
		}
		if (error_code == FT_ERR_SUCCESS)
		{
			std::vector<WorldSpawnerDeath> deaths;
			bool changed = false;
			if (spawner_id != 1U
				|| spawner_restored.tick(99U, entity_restored, deaths,
					&changed) != FT_ERR_SUCCESS || changed)
				error_code = FT_ERR_INVALID_STATE;
			if (error_code == FT_ERR_SUCCESS
				&& spawner_restored.tick(100U, entity_restored, deaths,
					&changed) != FT_ERR_SUCCESS)
				error_code = FT_ERR_INVALID_STATE;
			if (error_code == FT_ERR_SUCCESS
				&& (!changed || entity_restored.snapshot(restored_entities)
					!= FT_ERR_SUCCESS || restored_entities.size() != 2U))
				error_code = FT_ERR_INVALID_STATE;
		}
		if (error_code == FT_ERR_SUCCESS)
		{
			session_compatibility_stage = "save-v15-crop-policy";
			error_code = GameSessionPersistence::save_file(
				session_snapshot_file, saved_character, store,
				saved_recipe_catalog, saved_transaction_journal,
				card_session_test_state, regeneration_rules_test_state,
				chunk_instability_test_state, revision_metadata_test_state,
				v13_fluid_state, entity_state, v15_crop_policy_state);
		}
		if (error_code == FT_ERR_SUCCESS
			&& !session_snapshot_has_version(session_snapshot_file, 15U))
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
		{
			session_compatibility_stage = "load-v15-crop-policy";
			error_code = v14_restored_store.initialize();
			if (error_code == FT_ERR_SUCCESS)
				error_code = v14_restored_character.initialize();
			if (error_code == FT_ERR_SUCCESS)
				error_code = v14_restored_catalog.initialize(recipe_items);
			if (error_code == FT_ERR_SUCCESS)
				error_code = v14_restored_journal.initialize();
			if (error_code == FT_ERR_SUCCESS)
				error_code = GameSessionPersistence::load_file(
					session_snapshot_file, v14_restored_character,
					v14_restored_store, v14_restored_catalog,
					v14_restored_journal, restored_card_session_state,
					restored_regeneration_rules_state,
					restored_chunk_instability_state,
					restored_revision_metadata_state,
					v13_restored_fluid_state, restored_entity_state,
					restored_crop_policy_state);
		}
		if (error_code == FT_ERR_SUCCESS
			&& (restored_crop_policy_state != v15_crop_policy_state
				|| restored_entity_state != entity_state))
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
		{
			session_compatibility_stage = "rewrite-v14-crop-policy";
			error_code = session_snapshot_downgrade_crop_policy_v15_to_v14(
				session_snapshot_file);
		}
		if (error_code == FT_ERR_SUCCESS
			&& !session_snapshot_has_version(session_snapshot_file, 14U))
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
		{
			(void)v14_restored_journal.destroy();
			(void)v14_restored_catalog.destroy();
			(void)v14_restored_character.destroy();
			(void)v14_restored_store.destroy();
			error_code = v14_restored_store.initialize();
			if (error_code == FT_ERR_SUCCESS)
				error_code = v14_restored_character.initialize();
			if (error_code == FT_ERR_SUCCESS)
			{
				v14_restored_character.get_inventory().resize(30U);
				if (v14_restored_character.get_inventory().get_error()
					== FT_ERR_SUCCESS)
					v14_restored_character.get_inventory().set_weight_limit(
						std::numeric_limits<int32_t>::max());
				error_code = v14_restored_character.get_inventory().get_error();
			}
			if (error_code == FT_ERR_SUCCESS)
				error_code = v14_restored_catalog.initialize(recipe_items);
			if (error_code == FT_ERR_SUCCESS)
				error_code = v14_restored_journal.initialize();
		}
		if (error_code == FT_ERR_SUCCESS)
		{
			session_compatibility_stage = "load-v14-default-hydration";
			error_code = GameSessionPersistence::load_file(
				session_snapshot_file, v14_restored_character,
				v14_restored_store, v14_restored_catalog,
				v14_restored_journal, restored_card_session_state,
				restored_regeneration_rules_state,
				restored_chunk_instability_state,
				restored_revision_metadata_state, v13_restored_fluid_state,
				restored_entity_state, restored_crop_policy_state);
		}
		if (error_code == FT_ERR_SUCCESS
			&& (restored_crop_policy_state.size() != 16U
				|| !std::equal(v15_crop_policy_state.begin(),
					v15_crop_policy_state.begin() + 10,
					restored_crop_policy_state.begin())
				|| !std::all_of(restored_crop_policy_state.begin() + 10,
					restored_crop_policy_state.end(),
					[](uint8_t value) { return (value == 0U); })
				|| restored_entity_state != entity_state))
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
		{
			session_compatibility_stage = "save-v16-crop-deadlines";
			error_code = GameSessionPersistence::save_file_with_crop_growth_state(
				session_snapshot_file, saved_character, store,
				saved_recipe_catalog, saved_transaction_journal,
				card_session_test_state, regeneration_rules_test_state,
				chunk_instability_test_state, revision_metadata_test_state,
				v13_fluid_state, entity_state, v16_crop_growth_state);
		}
		if (error_code == FT_ERR_SUCCESS
			&& !session_snapshot_has_version(session_snapshot_file, 16U))
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
		{
			session_compatibility_stage = "load-v16-crop-deadlines";
			(void)v14_restored_journal.destroy();
			(void)v14_restored_catalog.destroy();
			(void)v14_restored_character.destroy();
			(void)v14_restored_store.destroy();
			error_code = v14_restored_store.initialize();
			if (error_code == FT_ERR_SUCCESS)
				error_code = v14_restored_character.initialize();
			if (error_code == FT_ERR_SUCCESS)
			{
				v14_restored_character.get_inventory().resize(30U);
				if (v14_restored_character.get_inventory().get_error()
					== FT_ERR_SUCCESS)
					v14_restored_character.get_inventory().set_weight_limit(
						std::numeric_limits<int32_t>::max());
				error_code = v14_restored_character.get_inventory().get_error();
			}
			if (error_code == FT_ERR_SUCCESS)
				error_code = v14_restored_catalog.initialize(recipe_items);
			if (error_code == FT_ERR_SUCCESS)
				error_code = v14_restored_journal.initialize();
			if (error_code == FT_ERR_SUCCESS)
				error_code = GameSessionPersistence::
					load_file_with_output_session_id(
					session_snapshot_file, v14_restored_character,
					v14_restored_store, v14_restored_catalog,
					v14_restored_journal, restored_card_session_state,
					restored_regeneration_rules_state,
					restored_chunk_instability_state,
					restored_revision_metadata_state,
					v13_restored_fluid_state, restored_entity_state,
					restored_crop_policy_state,
					&restored_output_session_id);
		}
		if (error_code == FT_ERR_SUCCESS
			&& (restored_crop_policy_state != v16_crop_growth_state
				|| restored_entity_state != entity_state
				|| restored_output_session_id != 0U
				|| v14_restored_character.get_coins()
					!= saved_character.get_coins()))
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
		{
			const std::vector<uint8_t> revision_checkpoint_test = {
				'W', 'R', 'P', '1', 0x11U, 0x22U, 0x33U};
			session_compatibility_stage = "save-v18-revision-server-checkpoint";
			error_code = GameSessionPersistence::
				save_file_with_revision_server_checkpoint(
					session_snapshot_file, saved_character, store,
					saved_recipe_catalog, saved_transaction_journal,
					card_session_test_state, regeneration_rules_test_state,
					chunk_instability_test_state, revision_metadata_test_state,
					v13_fluid_state, entity_state, v16_crop_growth_state,
						0xA17E000000000001ULL,
					revision_checkpoint_test);
		}
		if (error_code == FT_ERR_SUCCESS
			&& !session_snapshot_has_version(session_snapshot_file, 18U))
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
		{
			session_compatibility_stage = "load-v18-revision-server-checkpoint";
			error_code = GameSessionPersistence::
				load_file_with_output_session_id(session_snapshot_file,
					v14_restored_character, v14_restored_store,
					v14_restored_catalog, v14_restored_journal,
					restored_card_session_state,
					restored_regeneration_rules_state,
					restored_chunk_instability_state,
					restored_revision_metadata_state,
					v13_restored_fluid_state, restored_entity_state,
					restored_crop_policy_state,
					&restored_output_session_id);
		}
		if (error_code == FT_ERR_SUCCESS
			&& (restored_output_session_id
							!= 0xA17E000000000001ULL
				|| restored_crop_policy_state != v16_crop_growth_state
				|| restored_entity_state != entity_state))
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
		{
			std::vector<uint8_t> restored_revision_checkpoint;
			error_code = GameSessionPersistence::
				load_file_with_revision_server_checkpoint(
				session_snapshot_file, v14_restored_character,
				v14_restored_store, v14_restored_catalog,
				v14_restored_journal, restored_card_session_state,
				restored_regeneration_rules_state,
				restored_chunk_instability_state,
				restored_revision_metadata_state, v13_restored_fluid_state,
				restored_entity_state, restored_crop_policy_state,
				&restored_output_session_id, restored_revision_checkpoint);
			if (error_code == FT_ERR_SUCCESS
				&& restored_revision_checkpoint
					!= std::vector<uint8_t>({'W', 'R', 'P', '1',
						0x11U, 0x22U, 0x33U}))
				error_code = FT_ERR_INVALID_STATE;
		}
		(void)entity_source.destroy();
		(void)entity_restored.destroy();
		(void)spawner_source.destroy();
		(void)spawner_restored.destroy();
		(void)v14_restored_journal.destroy();
		(void)v14_restored_catalog.destroy();
		(void)v14_restored_character.destroy();
		(void)v14_restored_store.destroy();
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		std::ifstream input(session_snapshot_file, std::ios::binary);
		std::vector<uint8_t> future_version(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		const std::vector<uint8_t> sentinel = {0xA7U, 0x39U};

		if (!input.is_open() || input.bad() || future_version.size() < 16U)
			error_code = FT_ERR_IO;
		else
		{
			input.close();
			future_version[4] = 19U;
			future_version[5] = 0U;
			future_version[6] = 0U;
			future_version[7] = 0U;
			const uint64_t checksum = session_snapshot_checksum(
				future_version.data(), future_version.size() - 8U);
			for (uint32_t shift = 0U; shift < 64U; shift += 8U)
				future_version[future_version.size() - 8U + shift / 8U]
					= static_cast<uint8_t>(checksum >> shift);
			error_code = filesystem_atomic_write(session_snapshot_file,
				reinterpret_cast<const char *>(future_version.data()),
				future_version.size());
			if (error_code == FT_ERR_SUCCESS)
			{
				restored_card_session_state = sentinel;
				restored_regeneration_rules_state = sentinel;
				restored_chunk_instability_state = sentinel;
				restored_revision_metadata_state = sentinel;
				const int32_t future_load_result =
					GameSessionPersistence::load_file(session_snapshot_file,
						loaded_character, session_restored_store,
						loaded_recipe_catalog, loaded_transaction_journal,
						restored_card_session_state,
						restored_regeneration_rules_state,
						restored_chunk_instability_state,
						restored_revision_metadata_state);
				if (future_load_result != FT_ERR_INVALID_ARGUMENT
					|| restored_card_session_state != sentinel
					|| restored_regeneration_rules_state != sentinel
					|| restored_chunk_instability_state != sentinel
					|| restored_revision_metadata_state != sentinel
					|| loaded_character.get_coins() != 42)
					error_code = FT_ERR_INVALID_STATE;
			}
		}
	}
	if (error_code == FT_ERR_SUCCESS)
		session_compatibility_stage = "save-v9";
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::save_file(session_snapshot_file,
			saved_character, store, saved_recipe_catalog,
			saved_transaction_journal, card_session_test_state,
			regeneration_rules_test_state, chunk_instability_test_state);
	if (error_code == FT_ERR_SUCCESS)
		session_compatibility_stage = "check-v9-header";
	if (error_code == FT_ERR_SUCCESS
		&& !session_snapshot_has_version(session_snapshot_file, 9U))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		session_compatibility_stage = "load-v9";
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::load_file(session_snapshot_file,
			loaded_character, session_restored_store, loaded_recipe_catalog,
			loaded_transaction_journal, restored_card_session_state,
			restored_regeneration_rules_state,
			restored_chunk_instability_state,
			restored_revision_metadata_state);
	if (error_code == FT_ERR_SUCCESS)
		session_compatibility_stage = "compare-v9";
	if (error_code == FT_ERR_SUCCESS
		&& (card_session_test_state != restored_card_session_state
			|| regeneration_rules_test_state
				!= restored_regeneration_rules_state
			|| chunk_instability_test_state
				!= restored_chunk_instability_state
			|| !restored_revision_metadata_state.empty()))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		session_compatibility_stage = "save-v8";
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::save_file(session_snapshot_file,
			saved_character, store, saved_recipe_catalog,
			saved_transaction_journal, card_session_test_state,
			regeneration_rules_test_state);
	if (error_code == FT_ERR_SUCCESS)
		session_compatibility_stage = "check-v8-header";
	if (error_code == FT_ERR_SUCCESS
		&& !session_snapshot_has_version(session_snapshot_file, 8U))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		session_compatibility_stage = "load-v8";
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::load_file(session_snapshot_file,
			loaded_character, session_restored_store, loaded_recipe_catalog,
			loaded_transaction_journal, restored_card_session_state,
			restored_regeneration_rules_state,
			restored_chunk_instability_state,
			restored_revision_metadata_state);
	if (error_code == FT_ERR_SUCCESS)
		session_compatibility_stage = "compare-v8";
	if (error_code == FT_ERR_SUCCESS
		&& (card_session_test_state != restored_card_session_state
			|| regeneration_rules_test_state
				!= restored_regeneration_rules_state
			|| !restored_chunk_instability_state.empty()
			|| !restored_revision_metadata_state.empty()))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"world-items: session-compatibility stage=%s error=%d\n",
			session_compatibility_stage, error_code);
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = loaded_transaction_journal.find(501U, 2U,
			&journal_record);
		if (error_code == FT_ERR_SUCCESS
			&& (journal_record.state != GameTransactionJournal::PREPARED
				|| !journal_record.payload.empty()))
		{
			error_code = FT_ERR_INVALID_STATE;
			std::fprintf(stderr,
				"world-items: v8 journal state=%d payload=%zu\n",
				static_cast<int>(journal_record.state),
				journal_record.payload.size());
		}
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		std::ifstream input(session_snapshot_file, std::ios::binary);
		std::vector<uint8_t> corrupted(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		const std::vector<uint8_t> sentinel = {0xC3U};

		if (!input.is_open() || input.bad() || corrupted.empty())
			error_code = FT_ERR_IO;
		else
		{
			input.close();
			corrupted[corrupted.size() - 9U] ^= 0x01U;
			error_code = filesystem_atomic_write(session_snapshot_file,
				reinterpret_cast<const char *>(corrupted.data()),
				corrupted.size());
			if (error_code == FT_ERR_SUCCESS)
			{
				restored_card_session_state = sentinel;
				restored_regeneration_rules_state = sentinel;
				restored_chunk_instability_state = sentinel;
				const int32_t corrupt_load_result =
					GameSessionPersistence::load_file(session_snapshot_file,
						loaded_character, session_restored_store,
						loaded_recipe_catalog, loaded_transaction_journal,
						restored_card_session_state,
						restored_regeneration_rules_state,
						restored_chunk_instability_state);
				if (corrupt_load_result != FT_ERR_INVALID_ARGUMENT
					|| restored_card_session_state != sentinel
					|| restored_regeneration_rules_state != sentinel
					|| restored_chunk_instability_state != sentinel)
				{
					error_code = FT_ERR_INVALID_STATE;
					std::fprintf(stderr,
						"world-items: v8 corruption result=%d card=%zu rules=%zu\n",
						corrupt_load_result,
						restored_card_session_state.size(),
						restored_regeneration_rules_state.size());
				}
			}
		}
	}
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=card-session-snapshot error=%d\n",
			error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::save_file(session_snapshot_file,
			saved_character, store, saved_recipe_catalog,
			saved_transaction_journal, card_session_test_state);
	if (error_code == FT_ERR_SUCCESS
		&& !session_snapshot_has_version(session_snapshot_file, 7U))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = GameSessionPersistence::load_file(session_snapshot_file,
			loaded_character, session_restored_store, loaded_recipe_catalog,
			loaded_transaction_journal, restored_card_session_state,
			restored_regeneration_rules_state,
			restored_chunk_instability_state,
			restored_revision_metadata_state);
	if (error_code == FT_ERR_SUCCESS
		&& (card_session_test_state != restored_card_session_state
			|| !restored_regeneration_rules_state.empty()
			|| !restored_chunk_instability_state.empty()
			|| !restored_revision_metadata_state.empty()))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: v7 compatibility error=%d\n",
			error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = card_session_source->initialize(4U, 0xA15EEDU);
	if (error_code == FT_ERR_SUCCESS)
		error_code = card_session_source->serialize_snapshot(
			regeneration_card_bytes);
	if (error_code == FT_ERR_SUCCESS)
		error_code = card_session_restored->restore_snapshot(
			regeneration_card_bytes.data(), regeneration_card_bytes.size());
	if (error_code == FT_ERR_SUCCESS)
		error_code = card_session_source->get_draw_count(&card_draw_count);
	if (error_code == FT_ERR_SUCCESS)
		error_code = card_session_restored->get_draw_count(
			&restored_card_draw_count);
	if (error_code == FT_ERR_SUCCESS)
		error_code = card_session_source->get_hand_count(&card_hand_count);
	if (error_code == FT_ERR_SUCCESS)
		error_code = card_session_restored->get_hand_count(
			&restored_card_hand_count);
	if (error_code == FT_ERR_SUCCESS
		&& (card_draw_count != restored_card_draw_count
			|| card_hand_count != restored_card_hand_count))
		error_code = FT_ERR_INVALID_STATE;
	for (uint32_t index = 0U; error_code == FT_ERR_SUCCESS
		&& index < card_draw_count; ++index)
	{
		card_game_deck_card source_card = {};
		card_game_deck_card restored_card = {};
		error_code = card_session_source->draw_inspect(index, &source_card);
		if (error_code == FT_ERR_SUCCESS)
			error_code = card_session_restored->draw_inspect(index,
				&restored_card);
		if (error_code == FT_ERR_SUCCESS
			&& (source_card.instance_id != restored_card.instance_id
				|| source_card.card_id != restored_card.card_id))
			error_code = FT_ERR_INVALID_STATE;
	}
	for (uint32_t index = 0U; error_code == FT_ERR_SUCCESS
		&& index < card_hand_count; ++index)
	{
		card_game_deck_card source_card = {};
		card_game_deck_card restored_card = {};
		error_code = card_session_source->hand_inspect(index, &source_card);
		if (error_code == FT_ERR_SUCCESS)
			error_code = card_session_restored->hand_inspect(index,
				&restored_card);
		if (error_code == FT_ERR_SUCCESS
			&& (source_card.instance_id != restored_card.instance_id
				|| source_card.card_id != restored_card.card_id))
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		std::vector<uint8_t> corrupted = regeneration_card_bytes;

		corrupted[corrupted.size() - 9U] ^= 0x01U;
		if (card_session_corrupt->restore_snapshot(corrupted.data(),
				corrupted.size()) != FT_ERR_INVALID_ARGUMENT
			|| card_session_corrupt->is_initialized())
			error_code = FT_ERR_INVALID_STATE;
	}
	if (card_session_source != nullptr
		&& card_session_source->is_initialized())
		(void)card_session_source->destroy();
	if (card_session_restored != nullptr
		&& card_session_restored->is_initialized())
		(void)card_session_restored->destroy();
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "world-items: stage=regeneration-card-roundtrip error=%d\n",
			error_code);
	(void)std::remove(snapshot_file);
	(void)std::remove(session_snapshot_file);
	(void)std::remove(duplicate_id_save_file);
	(void)std::remove(pickup_recovery_file);
	(void)std::remove(craft_recovery_file);
	(void)std::remove(processing_recovery_file);
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "world-items: pickup transaction failed error=%d\n",
			error_code);
		return (1);
	}
	std::printf("world-items: ok partial=2 merge=partial query=bounded retry=stable snapshot=file-roundtrip inventory=paired inventory-restore=exact-retry recipe-book=restored craft-cache=restored transaction-journal=restored card-session-payload=restored pickup-recovery=exact-retry craft-recovery=exact-retry processing-recovery=exact-retry processing-advance-recovery=prepared-replay-and-exact-retry card-transfer-recovery=exact-retry\n");
	return (FT_ERR_SUCCESS);
}
