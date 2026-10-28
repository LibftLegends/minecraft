#include "WorldProjectileSimulation.hpp"

#include "../../Libft/Modules/Errno/errno.hpp"
#include "../../Libft/Modules/Game/game_item.hpp"
#include "../inventory/MinecraftLootCatalog.hpp"
#include "../coordinates/WorldCoordinates.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
	static const uint32_t PROJECTILE_QUERY_LIMIT = 128U;
	static const double PROJECTILE_COLLISION_RADIUS = 0.55;
	static const double PROJECTILE_MAXIMUM_SPEED = 1.5;
	static const double PROJECTILE_GRAVITY_PER_TICK = 0.02;

	static bool projectile_coordinate_is_representable(double coordinate) noexcept
	{
		return (std::isfinite(coordinate)
			&& coordinate > static_cast<double>(
				std::numeric_limits<int32_t>::min()) + 2.0
			&& coordinate < static_cast<double>(
				std::numeric_limits<int32_t>::max()) - 2.0);
	}

	static int32_t restore_projectile_state(WorldEntityStore &entities,
		WorldItemStore &items, const std::vector<uint8_t> &entities_before,
		const std::vector<uint8_t> &items_before,
		int32_t original_error) noexcept
	{
		int32_t result = entities.deserialize(entities_before.data(),
			entities_before.size());
		if (result == FT_ERR_SUCCESS)
			result = items.deserialize(items_before.data(), items_before.size());
		return (result == FT_ERR_SUCCESS ? original_error : result);
	}

	static int32_t drop_projectile_item(WorldItemStore &items,
		MinecraftItemCatalog &catalog, const EntityState &projectile,
		uint64_t server_tick) noexcept
	{
		ft_sharedptr<game_item> arrow;
		uint64_t item_entity_id = 0U;
		int32_t result = catalog.create_item(
			MinecraftItemCatalog::ARROW_ITEM_ID, 1, &arrow);
		if (result == FT_ERR_SUCCESS)
			result = items.spawn(arrow, projectile.position_x,
				projectile.position_y, projectile.position_z, server_tick, 0U,
				0U, &item_entity_id);
		return (result);
	}
}

int32_t WorldProjectileSimulation::tick(World &world,
	WorldEntityStore &entities, WorldSpawnerStore &spawners,
	WorldItemStore &items,
	MinecraftItemCatalog &catalog, uint64_t server_tick,
	const GameSessionSnapshotWriter *snapshot_writer,
	bool *state_changed_out) noexcept
{
	std::vector<WorldEntitySnapshot> entity_snapshot;
	std::vector<uint8_t> entities_before;
	std::vector<uint8_t> items_before;
	uint32_t projectile_count = 0U;
	bool changed = false;
	if (state_changed_out == nullptr || snapshot_writer == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*state_changed_out = false;
	int32_t result = entities.snapshot(entity_snapshot);
	if (result == FT_ERR_SUCCESS)
		result = entities.serialize(entities_before);
	if (result == FT_ERR_SUCCESS)
		result = items.serialize(items_before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	for (const WorldEntitySnapshot &snapshot : entity_snapshot)
		if (snapshot.state.type
			== static_cast<uint32_t>(EntityState::Kind::PROJECTILE))
			++projectile_count;
	if (projectile_count > WorldProjectileSimulation::MAXIMUM_ACTIVE_PROJECTILES)
		return (FT_ERR_FULL);
	for (const WorldEntitySnapshot &projectile : entity_snapshot)
	{
		if (projectile.state.type
			!= static_cast<uint32_t>(EntityState::Kind::PROJECTILE))
			continue ;
		if (projectile.state.health <= 0
			|| !std::isfinite(projectile.state.velocity_x)
			|| !std::isfinite(projectile.state.velocity_y)
			|| !std::isfinite(projectile.state.velocity_z)
			|| std::abs(projectile.state.velocity_x) > PROJECTILE_MAXIMUM_SPEED
			|| std::abs(projectile.state.velocity_y) > PROJECTILE_MAXIMUM_SPEED
			|| std::abs(projectile.state.velocity_z) > PROJECTILE_MAXIMUM_SPEED
			|| projectile.revision == 0U)
			return (restore_projectile_state(entities, items, entities_before,
				items_before, FT_ERR_INVALID_STATE));
		if (server_tick <= projectile.state.tick)
			continue ;
		if (projectile.revision > WorldProjectileSimulation::MAXIMUM_AGE_TICKS)
		{
			result = drop_projectile_item(items, catalog, projectile.state,
				server_tick);
			if (result == FT_ERR_SUCCESS)
				result = entities.despawn(projectile.state.id,
					projectile.revision);
			if (result != FT_ERR_SUCCESS)
				return (restore_projectile_state(entities, items, entities_before,
					items_before, result));
			changed = true;
			continue ;
		}
		EntityState next_state = projectile.state;
		const double start_x = next_state.position_x;
		const double start_y = next_state.position_y;
		const double start_z = next_state.position_z;
		const double end_x = start_x + next_state.velocity_x;
		const double end_y = start_y + next_state.velocity_y;
		const double end_z = start_z + next_state.velocity_z;
		if (!projectile_coordinate_is_representable(start_x)
			|| !projectile_coordinate_is_representable(start_y)
			|| !projectile_coordinate_is_representable(start_z)
			|| !projectile_coordinate_is_representable(end_x)
			|| !projectile_coordinate_is_representable(end_y)
			|| !projectile_coordinate_is_representable(end_z))
			return (restore_projectile_state(entities, items, entities_before,
				items_before, FT_ERR_INVALID_STATE));
		const double travel_x = end_x - start_x;
		const double travel_y = end_y - start_y;
		const double travel_z = end_z - start_z;
		const double travel_distance = std::sqrt(travel_x * travel_x
			+ travel_y * travel_y + travel_z * travel_z);
		const uint32_t sample_count = static_cast<uint32_t>(std::max(1.0,
			std::ceil(travel_distance / 0.25)));
		bool terrain_unloaded = false;
		bool terrain_hit = false;
		bool barrier_hit = false;
		bool discard_projectile = false;
		for (uint32_t sample_index = 1U;
			sample_index <= sample_count; ++sample_index)
		{
			const double fraction = static_cast<double>(sample_index)
				/ sample_count;
			const double sample_x = start_x + travel_x * fraction;
			const double sample_y = start_y + travel_y * fraction;
			const double sample_z = start_z + travel_z * fraction;
			if (sample_y < 0.0 || sample_y >= GAME_VOXEL_CHUNK_HEIGHT)
			{
				next_state.position_x = sample_x;
				next_state.position_y = sample_y;
				next_state.position_z = sample_z;
				terrain_hit = true;
				break ;
			}
			const int32_t sample_world_x = static_cast<int32_t>(
				std::floor(sample_x));
			const int32_t sample_world_z = static_cast<int32_t>(
				std::floor(sample_z));
			const int32_t sample_chunk_x = WorldCoordinates::floor_divide(
				sample_world_x, GAME_VOXEL_CHUNK_WIDTH);
			const int32_t sample_chunk_z = WorldCoordinates::floor_divide(
				sample_world_z, GAME_VOXEL_CHUNK_DEPTH);
			if (world.revision_barrier_blocks_chunk(sample_chunk_x,
				sample_chunk_z))
			{
				const double previous_fraction = static_cast<double>(
					sample_index - 1U) / sample_count;
				const double previous_x = start_x + travel_x * previous_fraction;
				const double previous_z = start_z + travel_z * previous_fraction;
				const int32_t previous_chunk_x = WorldCoordinates::floor_divide(
					static_cast<int32_t>(std::floor(previous_x)),
					GAME_VOXEL_CHUNK_WIDTH);
				const int32_t previous_chunk_z = WorldCoordinates::floor_divide(
					static_cast<int32_t>(std::floor(previous_z)),
					GAME_VOXEL_CHUNK_DEPTH);
				discard_projectile = world.revision_barrier_blocks_chunk(
					previous_chunk_x, previous_chunk_z);
				next_state.position_x = previous_x;
				next_state.position_y = start_y + travel_y * previous_fraction;
				next_state.position_z = previous_z;
				barrier_hit = true;
				terrain_hit = true;
				break ;
			}
			uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
			if (!world.block_id_at(static_cast<int32_t>(std::floor(sample_x)),
				static_cast<int32_t>(std::floor(sample_y)),
				static_cast<int32_t>(std::floor(sample_z)), &block_id))
			{
				terrain_unloaded = true;
				break ;
			}
			if (voxel_block_is_solid(block_id) == FT_TRUE)
			{
				next_state.position_x = sample_x;
				next_state.position_y = sample_y;
				next_state.position_z = sample_z;
				terrain_hit = true;
				break ;
			}
		}
		if (terrain_unloaded)
			continue ;
		if (!terrain_hit)
		{
			next_state.position_x = end_x;
			next_state.position_y = end_y;
			next_state.position_z = end_z;
		}
		next_state.velocity_y -= PROJECTILE_GRAVITY_PER_TICK;
		next_state.tick = server_tick;
		if (terrain_hit)
		{
			result = barrier_hit && discard_projectile ? FT_ERR_SUCCESS
				: drop_projectile_item(items, catalog, next_state, server_tick);
			if (result == FT_ERR_SUCCESS)
				result = entities.despawn(projectile.state.id,
					projectile.revision);
			if (result != FT_ERR_SUCCESS)
				return (restore_projectile_state(entities, items, entities_before,
					items_before, result));
			changed = true;
			continue ;
		}
		WorldEntitySnapshot hit_entity = {};
		bool entity_hit = false;
		for (uint32_t sample_index = 1U;
			sample_index <= sample_count && !entity_hit; ++sample_index)
		{
			const double fraction = static_cast<double>(sample_index)
				/ sample_count;
			const double sample_x = start_x + travel_x * fraction;
			const double sample_y = start_y + travel_y * fraction;
			const double sample_z = start_z + travel_z * fraction;
			std::vector<WorldEntitySnapshot> nearby_entities;
			result = entities.query_radius(sample_x, sample_y, sample_z,
				PROJECTILE_COLLISION_RADIUS, PROJECTILE_QUERY_LIMIT,
				nearby_entities);
			if (result != FT_ERR_SUCCESS)
				return (restore_projectile_state(entities, items,
					entities_before, items_before, result));
			double nearest_distance_squared =
				PROJECTILE_COLLISION_RADIUS * PROJECTILE_COLLISION_RADIUS;
			for (const WorldEntitySnapshot &candidate : nearby_entities)
			{
				if (candidate.state.id == projectile.state.id
					|| candidate.state.type == static_cast<uint32_t>(
						EntityState::Kind::PROJECTILE)
					|| candidate.state.type == static_cast<uint32_t>(
						EntityState::Kind::PLAYER)
					|| candidate.state.type == static_cast<uint32_t>(
						EntityState::Kind::BOAT)
					|| candidate.state.health <= 0)
					continue ;
				const double delta_x = candidate.state.position_x - sample_x;
				const double delta_y = candidate.state.position_y + 0.8
					- sample_y;
				const double delta_z = candidate.state.position_z - sample_z;
				const double distance_squared = delta_x * delta_x
					+ delta_y * delta_y + delta_z * delta_z;
				if (distance_squared <= nearest_distance_squared)
				{
					nearest_distance_squared = distance_squared;
					hit_entity = candidate;
					entity_hit = true;
				}
			}
		}
		if (entity_hit)
		{
			WorldEntitySnapshot damaged = {};
			result = entities.apply_damage(hit_entity.state.id,
				hit_entity.revision,
				projectile.state.health, projectile.owner_actor_id, &damaged);
			if (result == FT_ERR_SUCCESS && damaged.state.health == 0)
			{
				uint32_t loot_table_id = 0U;
				const int32_t loot_table_result =
					MinecraftLootCatalog::resolve_entity_loot_table(
						damaged.state.type, &loot_table_id);
				if (loot_table_result != FT_ERR_SUCCESS
					&& loot_table_result != FT_ERR_NOT_FOUND)
					result = loot_table_result;
				if (result == FT_ERR_SUCCESS && loot_table_id != 0U)
				{
					bool spawner_owned = false;
					result = spawners.is_entity_tracked(damaged.state.id,
						&spawner_owned);
					if (result == FT_ERR_SUCCESS && !spawner_owned)
					{
						std::vector<MinecraftLootDrop> drops;
						result = MinecraftLootCatalog::resolve(loot_table_id,
							damaged.state.id, drops);
						const uint64_t owner_lock_until_tick =
							projectile.owner_actor_id == 0U ? 0U
							: server_tick > UINT64_MAX - 40U ? UINT64_MAX
							: server_tick + 40U;
						for (const MinecraftLootDrop &loot_drop : drops)
						{
							ft_sharedptr<game_item> drop;
							uint64_t item_entity_id = 0U;
							if (result == FT_ERR_SUCCESS)
								result = catalog.create_item(loot_drop.item_id,
									loot_drop.quantity, &drop);
							if (result == FT_ERR_SUCCESS)
								result = items.spawn(drop,
										damaged.state.position_x,
										damaged.state.position_y,
										damaged.state.position_z, server_tick,
										projectile.owner_actor_id,
										owner_lock_until_tick, &item_entity_id);
							if (result != FT_ERR_SUCCESS)
								break ;
						}
						if (result == FT_ERR_SUCCESS)
							result = entities.despawn(damaged.state.id,
								damaged.revision);
					}
				}
			}
			if (result == FT_ERR_SUCCESS)
				result = entities.despawn(projectile.state.id,
					projectile.revision);
			if (result != FT_ERR_SUCCESS)
				return (restore_projectile_state(entities, items, entities_before,
					items_before, result));
			changed = true;
			continue ;
		}
		uint64_t new_revision = 0U;
		result = entities.update(projectile.state.id, projectile.revision,
			next_state, &new_revision);
		if (result != FT_ERR_SUCCESS)
			return (restore_projectile_state(entities, items, entities_before,
				items_before, result));
		changed = true;
	}
	if (changed)
	{
		result = snapshot_writer->save();
		if (result != FT_ERR_SUCCESS)
			return (restore_projectile_state(entities, items, entities_before,
				items_before, result));
	}
	*state_changed_out = changed;
	return (FT_ERR_SUCCESS);
}
