#ifndef WORLD_PROJECTILE_SIMULATION_HPP
# define WORLD_PROJECTILE_SIMULATION_HPP

# include "WorldEntityStore.hpp"
# include "WorldItemStore.hpp"
# include "../app/GameSessionSnapshotWriter.hpp"
# include "../inventory/MinecraftItemCatalog.hpp"
# include "../world/World.hpp"
# include <cstdint>

class WorldProjectileSimulation
{
  public:
	static const uint32_t MAXIMUM_ACTIVE_PROJECTILES = 1024U;
	static const uint64_t MAXIMUM_AGE_TICKS = 120U;
	static int32_t tick(World &world, WorldEntityStore &entities,
		WorldItemStore &items, MinecraftItemCatalog &catalog,
		uint64_t server_tick, const GameSessionSnapshotWriter *snapshot_writer,
		bool *state_changed_out) noexcept;
};

#endif
