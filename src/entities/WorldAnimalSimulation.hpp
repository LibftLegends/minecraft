#ifndef WORLD_ANIMAL_SIMULATION_HPP
# define WORLD_ANIMAL_SIMULATION_HPP

# include "WorldEntityStore.hpp"
# include "../world/World.hpp"

struct WorldAnimalProductionEvent
{
	uint32_t animal_entity_id;
	uint64_t previous_tick;
	uint64_t production_cycle;
	double position_x;
	double position_y;
	double position_z;
};

struct WorldHostileAttackEvent
{
	uint32_t hostile_entity_id;
	int32_t damage;
	double source_x;
	double source_z;
};

class WorldAnimalSimulation
{
  public:
	static const uint32_t MAXIMUM_ACTIVE_ANIMALS = 256U;
	static const uint64_t CHICKEN_EGG_PERIOD_MINIMUM_TICKS = 6000U;
	static const uint64_t CHICKEN_EGG_PERIOD_VARIANCE_TICKS = 6000U;
	static int32_t tick(World &world, WorldEntityStore &entities,
		double center_x, double center_y, double center_z,
		uint64_t server_tick, bool *state_changed_out,
		std::vector<WorldAnimalProductionEvent> *production_events_out
			= nullptr,
		std::vector<WorldHostileAttackEvent> *hostile_attacks_out = nullptr)
		noexcept;
};

#endif
