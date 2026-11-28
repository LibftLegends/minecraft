#include "WorldAnimalSimulation.hpp"

#include "../coordinates/WorldCoordinates.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <cmath>
#include <limits>
#include <new>
#include <vector>

namespace
{
	static const int32_t ANIMAL_TICK_INTERVAL = 10;
	static const uint64_t HOSTILE_ATTACK_INTERVAL_TICKS = 60U;
	static const double ANIMAL_SIMULATION_RADIUS = 48.0;
	static const double ANIMAL_STEP_DISTANCE = 0.12;
	static const double DIAGONAL_SCALE = 0.7071067811865475244;
	static const int32_t DIRECTIONS[8][2] = {
		{1, 0}, {1, 1}, {0, 1}, {-1, 1},
		{-1, 0}, {-1, -1}, {0, -1}, {1, -1}};

	static bool is_animal(uint32_t type) noexcept
	{
		return (type == static_cast<uint32_t>(EntityState::Kind::COW)
			|| type == static_cast<uint32_t>(EntityState::Kind::PIG)
			|| type == static_cast<uint32_t>(EntityState::Kind::CHICKEN)
			|| type == static_cast<uint32_t>(EntityState::Kind::SHEEP));
	}

	static bool is_hostile(uint32_t type) noexcept
	{
		return (type == static_cast<uint32_t>(EntityState::Kind::ZOMBIE)
			|| type == static_cast<uint32_t>(EntityState::Kind::CREEPER)
			|| type == static_cast<uint32_t>(EntityState::Kind::SKELETON)
			|| type == static_cast<uint32_t>(EntityState::Kind::SPIDER));
	}

	static int32_t hostile_attack_damage(uint32_t type) noexcept
	{
		if (type == static_cast<uint32_t>(EntityState::Kind::CREEPER))
			return (4);
		if (type == static_cast<uint32_t>(EntityState::Kind::ZOMBIE)
			|| type == static_cast<uint32_t>(EntityState::Kind::SKELETON))
			return (2);
		if (type == static_cast<uint32_t>(EntityState::Kind::SPIDER))
			return (1);
		return (0);
	}

	static double hostile_attack_reach_squared(uint32_t type) noexcept
	{
		if (type == static_cast<uint32_t>(EntityState::Kind::CREEPER))
			return (6.25);
		if (type == static_cast<uint32_t>(EntityState::Kind::SKELETON))
			return (64.0);
		return (2.25);
	}

	static bool coordinate_is_representable(double value) noexcept;

	static bool has_attack_line_of_sight(const World &world,
		const EntityState &state, double target_x, double target_y,
		double target_z) noexcept
	{
		const double source_x = state.position_x;
		const double source_y = state.position_y + 1.0;
		const double source_z = state.position_z;
		const double dx = target_x - source_x;
		const double dy = target_y - source_y;
		const double dz = target_z - source_z;
		const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (!std::isfinite(distance) || distance > 16.0)
			return (false);
		const int32_t steps = static_cast<int32_t>(std::ceil(distance * 4.0));
		for (int32_t step = 1; step < steps; ++step)
		{
			const double progress = static_cast<double>(step)
				/ static_cast<double>(steps);
			const double sample_x = std::floor(source_x + dx * progress);
			const double sample_y = std::floor(source_y + dy * progress);
			const double sample_z = std::floor(source_z + dz * progress);
			if (!coordinate_is_representable(sample_x)
				|| !coordinate_is_representable(sample_y)
				|| !coordinate_is_representable(sample_z))
				return (false);
			const int32_t x = static_cast<int32_t>(sample_x);
			const int32_t y = static_cast<int32_t>(sample_y);
			const int32_t z = static_cast<int32_t>(sample_z);
			if (world.solid_block_at(x, y, z))
				return (false);
		}
		return (true);
	}

	static bool coordinate_is_representable(double value) noexcept
	{
		return (std::isfinite(value)
			&& value > static_cast<double>(std::numeric_limits<int32_t>::min())
			&& value < static_cast<double>(std::numeric_limits<int32_t>::max()));
	}

	static bool position_in_revision_barrier(const World &world,
		double x, double z) noexcept
	{
		if (!coordinate_is_representable(x)
			|| !coordinate_is_representable(z))
			return (true);
		const int32_t chunk_x = WorldCoordinates::floor_divide(
			static_cast<int32_t>(std::floor(x)), GAME_VOXEL_CHUNK_WIDTH);
		const int32_t chunk_z = WorldCoordinates::floor_divide(
			static_cast<int32_t>(std::floor(z)), GAME_VOXEL_CHUNK_DEPTH);
		return (world.revision_barrier_blocks_chunk(chunk_x, chunk_z));
	}

	static bool is_safe_step(const World &world, const EntityState &state,
		double x, double z) noexcept
	{
		const int32_t block_x = static_cast<int32_t>(std::floor(x));
		const int32_t block_y = static_cast<int32_t>(std::floor(
			state.position_y));
		const int32_t block_z = static_cast<int32_t>(std::floor(z));
		const int32_t chunk_x = WorldCoordinates::floor_divide(block_x,
			GAME_VOXEL_CHUNK_WIDTH);
		const int32_t chunk_z = WorldCoordinates::floor_divide(block_z,
			GAME_VOXEL_CHUNK_DEPTH);
		if (world.revision_barrier_blocks_chunk(chunk_x, chunk_z))
			return (false);
		const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
		if (chunk == nullptr || !chunk->initialized || block_y <= 0
			|| block_y + 1 >= GAME_VOXEL_CHUNK_HEIGHT)
			return (false);
		return (world.solid_block_at(block_x, block_y - 1, block_z)
			&& !world.solid_block_at(block_x, block_y, block_z)
			&& !world.solid_block_at(block_x, block_y + 1, block_z));
	}

	static uint32_t direction_seed(uint32_t entity_id,
		uint64_t server_tick) noexcept
	{
		uint64_t value = static_cast<uint64_t>(entity_id)
			* UINT64_C(0x9E3779B185EBCA87) ^ (server_tick / 300U);
		value ^= value >> 30U;
		value *= UINT64_C(0xBF58476D1CE4E5B9);
		value ^= value >> 27U;
		value *= UINT64_C(0x94D049BB133111EB);
		value ^= value >> 31U;
		return (static_cast<uint32_t>(value));
	}
}

int32_t WorldAnimalSimulation::tick(World &world,
	WorldEntityStore &entities, double center_x, double center_y,
	double center_z, uint64_t server_tick, bool *state_changed_out,
	std::vector<WorldAnimalProductionEvent> *production_events_out,
	std::vector<WorldHostileAttackEvent> *hostile_attacks_out) noexcept
{
	std::vector<WorldEntitySnapshot> nearby;
	std::vector<WorldAnimalProductionEvent> production_events;
	std::vector<WorldHostileAttackEvent> hostile_attacks;
	bool changed = false;
	if (state_changed_out == nullptr || !std::isfinite(center_x)
		|| !std::isfinite(center_y) || !std::isfinite(center_z))
		return (FT_ERR_INVALID_ARGUMENT);
	if (production_events_out != nullptr)
		production_events_out->clear();
	if (hostile_attacks_out != nullptr)
		hostile_attacks_out->clear();
	if (server_tick % ANIMAL_TICK_INTERVAL != 0U)
	{
		*state_changed_out = false;
		return (FT_ERR_SUCCESS);
	}
	int32_t result = entities.query_radius(center_x, center_y, center_z,
		ANIMAL_SIMULATION_RADIUS,
		WorldAnimalSimulation::MAXIMUM_ACTIVE_ANIMALS, nearby);
	if (result == FT_ERR_FULL)
	{
		*state_changed_out = false;
		return (FT_ERR_SUCCESS);
	}
	if (result != FT_ERR_SUCCESS)
		return (result);
	try
	{
		production_events.reserve(nearby.size());
		hostile_attacks.reserve(nearby.size());
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (WorldEntitySnapshot &snapshot : nearby)
	{
		EntityState &state = snapshot.state;
		const uint64_t previous_tick = state.tick;
		const bool animal = is_animal(state.type);
		const bool hostile = is_hostile(state.type);
		int32_t hostile_damage = 0;
		if ((!animal && !hostile) || server_tick <= state.tick
			|| !coordinate_is_representable(state.position_x)
			|| !coordinate_is_representable(state.position_z))
			continue;
		if (hostile && hostile_attacks_out != nullptr
			&& !position_in_revision_barrier(world, state.position_x,
				state.position_z)
			&& server_tick % HOSTILE_ATTACK_INTERVAL_TICKS
				== (static_cast<uint64_t>(state.id) % 6U)
					* static_cast<uint64_t>(ANIMAL_TICK_INTERVAL))
		{
			const double target_x = center_x - state.position_x;
			const double target_y = center_y - state.position_y;
			const double target_z = center_z - state.position_z;
			hostile_damage = hostile_attack_damage(state.type);
			if (!(hostile_damage > 0
				&& target_x * target_x + target_z * target_z
					<= hostile_attack_reach_squared(state.type)
				&& std::fabs(target_y) <= (state.type == static_cast<uint32_t>(
					EntityState::Kind::SKELETON) ? 4.0 : 2.5)
				&& has_attack_line_of_sight(world, state, center_x, center_y,
					center_z)))
				hostile_damage = 0;
		}
		const uint32_t first_direction = direction_seed(state.id, server_tick)
			% 8U;
		/* Animals loosely follow nearby members of their species and spread
		 * out when another animal is crowding them. The bounded local query
		 * above makes this deterministic and caps the work per server tick. */
		double desired_x = 0.0;
		double desired_z = 0.0;
		if (animal)
		{
			for (const WorldEntitySnapshot &neighbor : nearby)
			{
				if (neighbor.state.id == state.id)
					continue ;
				const double offset_x = neighbor.state.position_x
					- state.position_x;
				const double offset_z = neighbor.state.position_z
					- state.position_z;
				const double distance_squared = offset_x * offset_x
					+ offset_z * offset_z;
				if (is_hostile(neighbor.state.type))
				{
					if (distance_squared > 0.0001
						&& distance_squared < 100.0)
					{
						desired_x -= offset_x * (4.0 / distance_squared);
						desired_z -= offset_z * (4.0 / distance_squared);
					}
					continue ;
				}
				if (neighbor.state.type != state.type
					|| distance_squared > 64.0)
					continue ;
				/* Cohesion points toward the local center of mass. */
				desired_x += offset_x * 0.08;
				desired_z += offset_z * 0.08;
				/* Separation dominates at close range to avoid animal overlap. */
				if (distance_squared > 0.0001 && distance_squared < 9.0)
				{
					desired_x -= offset_x * (1.5 / distance_squared);
					desired_z -= offset_z * (1.5 / distance_squared);
				}
				/* Alignment contributes a small heading preference. */
				desired_x += neighbor.state.velocity_x * 0.5;
				desired_z += neighbor.state.velocity_z * 0.5;
			}
		}
		else
		{
			const double target_x = center_x - state.position_x;
			const double target_z = center_z - state.position_z;
			if (target_x * target_x + target_z * target_z <= 576.0)
			{
				desired_x = target_x;
				desired_z = target_z;
			}
		}
		bool moved = false;
		double best_score = -std::numeric_limits<double>::infinity();
		uint32_t best_direction = first_direction;
		for (uint32_t attempt = 0U; attempt < 8U; ++attempt)
		{
			const uint32_t direction = (first_direction + attempt) % 8U;
			const double diagonal = (DIRECTIONS[direction][0] != 0
				&& DIRECTIONS[direction][1] != 0) ? DIAGONAL_SCALE : 1.0;
			const double next_x = state.position_x
				+ DIRECTIONS[direction][0] * ANIMAL_STEP_DISTANCE * diagonal;
			const double next_z = state.position_z
				+ DIRECTIONS[direction][1] * ANIMAL_STEP_DISTANCE * diagonal;
			if (!coordinate_is_representable(next_x)
				|| !coordinate_is_representable(next_z)
				|| !is_safe_step(world, state, next_x, next_z))
				continue;
			const double score = desired_x * DIRECTIONS[direction][0]
				+ desired_z * DIRECTIONS[direction][1];
			if (score <= best_score)
				continue ;
			best_score = score;
			best_direction = direction;
		}
		if (best_score > -std::numeric_limits<double>::infinity())
		{
			const uint32_t direction = best_direction;
			const double diagonal = (DIRECTIONS[direction][0] != 0
				&& DIRECTIONS[direction][1] != 0) ? DIAGONAL_SCALE : 1.0;
			const double next_x = state.position_x
				+ DIRECTIONS[direction][0] * ANIMAL_STEP_DISTANCE * diagonal;
			const double next_z = state.position_z
				+ DIRECTIONS[direction][1] * ANIMAL_STEP_DISTANCE * diagonal;
			state.position_x = next_x;
			state.position_z = next_z;
			state.velocity_x = DIRECTIONS[direction][0]
				* ANIMAL_STEP_DISTANCE * diagonal;
			state.velocity_y = 0.0;
			state.velocity_z = DIRECTIONS[direction][1]
				* ANIMAL_STEP_DISTANCE * diagonal;
			state.orientation_yaw = std::atan2(state.velocity_x,
				state.velocity_z);
			state.action_state = static_cast<uint32_t>(
				EntityState::ActionState::WALKING);
			moved = true;
		}
		if (!moved)
		{
			state.velocity_x = 0.0;
			state.velocity_y = 0.0;
			state.velocity_z = 0.0;
			state.action_state = static_cast<uint32_t>(
				EntityState::ActionState::IDLE);
		}
		state.tick = server_tick;
		uint64_t new_revision = 0U;
		result = entities.update(state.id, snapshot.revision, state,
			&new_revision);
		if (result == FT_ERR_INVALID_STATE)
			continue;
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (hostile_damage > 0)
			hostile_attacks.push_back({state.id, hostile_damage,
				state.position_x, state.position_z});
		if (state.type == static_cast<uint32_t>(EntityState::Kind::CHICKEN)
			&& !position_in_revision_barrier(world, state.position_x,
				state.position_z))
		{
			const uint64_t period =
				WorldAnimalSimulation::CHICKEN_EGG_PERIOD_MINIMUM_TICKS
				+ (static_cast<uint64_t>(state.id)
					% WorldAnimalSimulation::CHICKEN_EGG_PERIOD_VARIANCE_TICKS);
			const uint64_t previous_cycle = previous_tick / period;
			const uint64_t current_cycle = server_tick / period;
			if (current_cycle > previous_cycle)
			{
				WorldAnimalProductionEvent event = {};
				event.animal_entity_id = state.id;
				event.previous_tick = previous_tick;
				event.production_cycle = current_cycle;
				event.position_x = state.position_x;
				event.position_y = state.position_y;
				event.position_z = state.position_z;
				production_events.push_back(event);
			}
		}
		changed = true;
	}
	*state_changed_out = changed;
	if (production_events_out != nullptr)
		production_events_out->swap(production_events);
	if (hostile_attacks_out != nullptr)
		hostile_attacks_out->swap(hostile_attacks);
	return (FT_ERR_SUCCESS);
}
