#ifndef WORLD_SPAWNER_STORE_HPP
# define WORLD_SPAWNER_STORE_HPP

# include "WorldEntityStore.hpp"
# include <cstddef>
# include <cstdint>
# include <map>
# include <mutex>
# include <vector>

class World;

struct WorldSpawnerDefinition
{
	double x;
	double y;
	double z;
	uint32_t entity_type;
	uint32_t maximum_alive;
	uint32_t spawn_interval_ticks;
	uint32_t loot_table_id;
};

struct WorldSpawnerDeath
{
	uint32_t spawner_id;
	uint32_t entity_id;
	uint32_t loot_table_id;
	uint64_t killer_actor_id;
	double position_x;
	double position_y;
	double position_z;
};

/* Persistent bounded spawn schedules. Death events are returned to the host
 * so item creation can be committed with its inventory/world-item journal. */
class WorldSpawnerStore
{
  public:
	static const uint32_t MAXIMUM_SPAWNERS = 4096U;
	static const uint32_t MAXIMUM_ALIVE_PER_SPAWNER = 16U;

	WorldSpawnerStore() noexcept;
	WorldSpawnerStore(const WorldSpawnerStore &) = delete;
	WorldSpawnerStore &operator=(const WorldSpawnerStore &) = delete;

	int32_t initialize() noexcept;
	int32_t destroy() noexcept;
	int32_t add(const WorldSpawnerDefinition &definition,
		uint64_t first_spawn_tick, uint32_t *spawner_id_out) noexcept;
	/* Adds a related group atomically. On failure neither the store nor the
	 * caller's ID vector changes. Useful for structures with multiple spawners. */
	int32_t add_batch(const std::vector<WorldSpawnerDefinition> &definitions,
		uint64_t first_spawn_tick,
		std::vector<uint32_t> &spawner_ids_out) noexcept;
	/* Removes an unstarted batch, for rollback before any spawn schedule runs. */
	int32_t remove_batch(const std::vector<uint32_t> &spawner_ids) noexcept;
	int32_t set_active(uint32_t spawner_id, bool active) noexcept;
	int32_t tick(uint64_t server_tick, WorldEntityStore &entities,
		std::vector<WorldSpawnerDeath> &deaths_out,
		bool *state_changed_out, const World *world = nullptr) noexcept;
	int32_t acknowledge_death(uint32_t spawner_id,
		uint32_t entity_id) noexcept;
	int32_t is_entity_tracked(uint32_t entity_id,
		bool *tracked_out) const noexcept;
	int32_t serialize(std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t deserialize(const uint8_t *bytes,
		std::size_t byte_count) noexcept;

  private:
	struct Entry
	{
		WorldSpawnerDefinition definition;
		uint64_t next_spawn_tick;
		bool active;
		std::vector<uint32_t> alive_entity_ids;
		std::vector<uint32_t> pending_deaths;
	};
	mutable std::mutex mutex_;
	std::map<uint32_t, Entry> entries_;
	uint32_t next_spawner_id_;
	bool initialized_;
};

#endif
