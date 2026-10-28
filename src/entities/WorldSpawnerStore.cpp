#include "WorldSpawnerStore.hpp"
#include "../world/World.hpp"
#include "../coordinates/WorldCoordinates.hpp"

#include "../../Libft/Modules/Errno/errno.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

static bool world_spawner_position_in_barrier(const World *world,
	double x, double z) noexcept
{
	if (world == nullptr || !world->revision_barriers_active())
		return (false);
	if (!std::isfinite(x) || !std::isfinite(z)
		|| x <= static_cast<double>(std::numeric_limits<int32_t>::min())
		|| x >= static_cast<double>(std::numeric_limits<int32_t>::max())
		|| z <= static_cast<double>(std::numeric_limits<int32_t>::min())
		|| z >= static_cast<double>(std::numeric_limits<int32_t>::max()))
		return (true);
	const int32_t chunk_x = WorldCoordinates::floor_divide(
		static_cast<int32_t>(std::floor(x)), GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(
		static_cast<int32_t>(std::floor(z)), GAME_VOXEL_CHUNK_DEPTH);
	return (world->revision_barrier_blocks_chunk(chunk_x, chunk_z));
}
#include <set>

namespace
{
	static const uint32_t WORLD_SPAWNER_MAGIC = 0x57535031U;
	static const uint32_t WORLD_SPAWNER_VERSION = 1U;
	static const uint64_t WORLD_SPAWNER_FNV_OFFSET = 14695981039346656037ULL;
	static const uint64_t WORLD_SPAWNER_FNV_PRIME = 1099511628211ULL;

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

	static uint32_t read_u32(const uint8_t *bytes, std::size_t offset) noexcept
	{
		return (static_cast<uint32_t>(bytes[offset])
			| (static_cast<uint32_t>(bytes[offset + 1U]) << 8U)
			| (static_cast<uint32_t>(bytes[offset + 2U]) << 16U)
			| (static_cast<uint32_t>(bytes[offset + 3U]) << 24U));
	}

	static uint64_t read_u64(const uint8_t *bytes, std::size_t offset) noexcept
	{
		return (static_cast<uint64_t>(read_u32(bytes, offset))
			| (static_cast<uint64_t>(read_u32(bytes, offset + 4U)) << 32U));
	}

	static uint64_t hash_bytes(const uint8_t *bytes,
		std::size_t byte_count) noexcept
	{
		uint64_t hash = WORLD_SPAWNER_FNV_OFFSET;
		for (std::size_t index = 0U; index < byte_count; ++index)
		{
			hash ^= bytes[index];
			hash *= WORLD_SPAWNER_FNV_PRIME;
		}
		return (hash);
	}

	static bool is_supported_mob_type(uint32_t type) noexcept
	{
		return (type == static_cast<uint32_t>(EntityState::Kind::ZOMBIE)
			|| type == static_cast<uint32_t>(EntityState::Kind::CREEPER)
			|| type == static_cast<uint32_t>(EntityState::Kind::SKELETON)
			|| type == static_cast<uint32_t>(EntityState::Kind::COW)
			|| type == static_cast<uint32_t>(EntityState::Kind::PIG)
			|| type == static_cast<uint32_t>(EntityState::Kind::CHICKEN)
			|| type == static_cast<uint32_t>(EntityState::Kind::SHEEP)
			|| type == static_cast<uint32_t>(EntityState::Kind::SPIDER));
	}

	static bool contains_id(const std::vector<uint32_t> &ids,
		uint32_t id) noexcept
	{
		for (uint32_t value : ids)
			if (value == id)
				return (true);
		return (false);
	}
}

WorldSpawnerStore::WorldSpawnerStore() noexcept
	: mutex_(), entries_(), next_spawner_id_(1U), initialized_(false)
{
}

int32_t WorldSpawnerStore::initialize() noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	this->entries_.clear();
	this->next_spawner_id_ = 1U;
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldSpawnerStore::destroy() noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	this->entries_.clear();
	this->next_spawner_id_ = 1U;
	this->initialized_ = false;
	return (FT_ERR_SUCCESS);
}

int32_t WorldSpawnerStore::add(const WorldSpawnerDefinition &definition,
	uint64_t first_spawn_tick, uint32_t *spawner_id_out) noexcept
{
	if (spawner_id_out == nullptr || !std::isfinite(definition.x)
		|| !std::isfinite(definition.y) || !std::isfinite(definition.z)
		|| !is_supported_mob_type(definition.entity_type)
		|| definition.maximum_alive == 0U
		|| definition.maximum_alive
			> WorldSpawnerStore::MAXIMUM_ALIVE_PER_SPAWNER
		|| definition.spawn_interval_ticks == 0U
		|| definition.loot_table_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (this->entries_.size() >= WorldSpawnerStore::MAXIMUM_SPAWNERS
		|| this->next_spawner_id_ == 0U
		|| this->next_spawner_id_ == UINT32_MAX)
		return (FT_ERR_FULL);
	Entry entry = {};
	entry.definition = definition;
	entry.next_spawn_tick = first_spawn_tick;
	entry.active = true;
	try
	{
		if (!this->entries_.emplace(this->next_spawner_id_,
				std::move(entry)).second)
			return (FT_ERR_ALREADY_EXISTS);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	*spawner_id_out = this->next_spawner_id_++;
	return (FT_ERR_SUCCESS);
}

int32_t WorldSpawnerStore::add_batch(
	const std::vector<WorldSpawnerDefinition> &definitions,
	uint64_t first_spawn_tick,
	std::vector<uint32_t> &spawner_ids_out) noexcept
{
	std::vector<uint32_t> staged_ids;
	std::vector<uint32_t> inserted_ids;
	if (definitions.empty())
		return (FT_ERR_INVALID_ARGUMENT);
	for (const WorldSpawnerDefinition &definition : definitions)
	{
		if (!std::isfinite(definition.x) || !std::isfinite(definition.y)
			|| !std::isfinite(definition.z)
			|| !is_supported_mob_type(definition.entity_type)
			|| definition.maximum_alive == 0U
			|| definition.maximum_alive
				> WorldSpawnerStore::MAXIMUM_ALIVE_PER_SPAWNER
			|| definition.spawn_interval_ticks == 0U
			|| definition.loot_table_id == 0U)
			return (FT_ERR_INVALID_ARGUMENT);
	}
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (definitions.size() > WorldSpawnerStore::MAXIMUM_SPAWNERS
			- this->entries_.size()
		|| this->next_spawner_id_ == 0U
		|| static_cast<uint64_t>(this->next_spawner_id_)
			+ definitions.size() > UINT32_MAX)
		return (FT_ERR_FULL);
	try
	{
		staged_ids.reserve(definitions.size());
		inserted_ids.reserve(definitions.size());
		uint32_t next_id = this->next_spawner_id_;
		for (std::size_t index = 0U; index < definitions.size(); ++index)
			staged_ids.push_back(next_id++);
		for (std::size_t index = 0U; index < definitions.size(); ++index)
		{
			Entry entry = {};
			entry.definition = definitions[index];
			entry.next_spawn_tick = first_spawn_tick;
			entry.active = true;
			if (!this->entries_.emplace(staged_ids[index],
					std::move(entry)).second)
			{
				for (uint32_t id : inserted_ids)
					this->entries_.erase(id);
				return (FT_ERR_ALREADY_EXISTS);
			}
			inserted_ids.push_back(staged_ids[index]);
		}
	}
	catch (const std::bad_alloc &)
	{
		for (uint32_t id : inserted_ids)
			this->entries_.erase(id);
		return (FT_ERR_NO_MEMORY);
	}
	this->next_spawner_id_ += static_cast<uint32_t>(definitions.size());
	spawner_ids_out.swap(staged_ids);
	return (FT_ERR_SUCCESS);
}

int32_t WorldSpawnerStore::remove_batch(
	const std::vector<uint32_t> &spawner_ids) noexcept
{
	if (spawner_ids.empty())
		return (FT_ERR_INVALID_ARGUMENT);
	for (std::size_t index = 0U; index < spawner_ids.size(); ++index)
	{
		if (spawner_ids[index] == 0U
			|| std::find(spawner_ids.begin(), spawner_ids.begin()
				+ static_cast<std::ptrdiff_t>(index), spawner_ids[index])
				!= spawner_ids.begin() + static_cast<std::ptrdiff_t>(index))
			return (FT_ERR_INVALID_ARGUMENT);
	}
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (uint32_t id : spawner_ids)
	{
		const auto entry = this->entries_.find(id);
		if (entry == this->entries_.end())
			return (FT_ERR_NOT_FOUND);
		if (!entry->second.alive_entity_ids.empty()
			|| !entry->second.pending_deaths.empty())
			return (FT_ERR_INVALID_OPERATION);
	}
	for (uint32_t id : spawner_ids)
		this->entries_.erase(id);
	return (FT_ERR_SUCCESS);
}

int32_t WorldSpawnerStore::set_active(uint32_t spawner_id,
	bool active) noexcept
{
	if (spawner_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	auto entry = this->entries_.find(spawner_id);
	if (entry == this->entries_.end())
		return (FT_ERR_NOT_FOUND);
	entry->second.active = active;
	return (FT_ERR_SUCCESS);
}

int32_t WorldSpawnerStore::tick(uint64_t server_tick,
	WorldEntityStore &entities,
	std::vector<WorldSpawnerDeath> &deaths_out,
	bool *state_changed_out, const World *world) noexcept
{
	std::vector<WorldSpawnerDeath> staged_deaths;
	bool state_changed = false;
	if (state_changed_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	try
	{
		for (auto &pair : this->entries_)
		{
			Entry &entry = pair.second;
			for (std::size_t index = 0U;
				index < entry.alive_entity_ids.size();)
			{
				WorldEntitySnapshot entity = {};
				const uint32_t entity_id = entry.alive_entity_ids[index];
				const int32_t result = entities.get(entity_id, &entity);
				if (result == FT_ERR_NOT_FOUND
					|| (result == FT_ERR_SUCCESS
						&& (entity.state.health == 0
							|| entity.state.action_state
								== static_cast<uint32_t>(
									EntityState::ActionState::DEAD))))
				{
					if (!contains_id(entry.pending_deaths, entity_id))
						entry.pending_deaths.push_back(entity_id);
					state_changed = true;
					entry.alive_entity_ids.erase(
						entry.alive_entity_ids.begin()
							+ static_cast<std::ptrdiff_t>(index));
					continue ;
				}
				if (result != FT_ERR_SUCCESS)
					return (result);
				++index;
			}
			for (uint32_t entity_id : entry.pending_deaths)
			{
				WorldEntitySnapshot dead_entity = {};
				const int32_t entity_result = entities.get(entity_id,
					&dead_entity);
				if (entity_result != FT_ERR_SUCCESS
					&& entity_result != FT_ERR_NOT_FOUND)
					return (entity_result);
				const double death_x = entity_result == FT_ERR_SUCCESS
					? dead_entity.state.position_x : entry.definition.x;
				const double death_y = entity_result == FT_ERR_SUCCESS
					? dead_entity.state.position_y : entry.definition.y;
				const double death_z = entity_result == FT_ERR_SUCCESS
					? dead_entity.state.position_z : entry.definition.z;
				if (!world_spawner_position_in_barrier(world, death_x, death_z))
					staged_deaths.push_back({pair.first, entity_id,
						entry.definition.loot_table_id,
						entity_result == FT_ERR_SUCCESS
							? dead_entity.owner_actor_id : 0U,
						death_x, death_y, death_z});
			}
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (auto &pair : this->entries_)
	{
		Entry &entry = pair.second;
		if (!entry.active || server_tick < entry.next_spawn_tick
			|| world_spawner_position_in_barrier(world, entry.definition.x,
				entry.definition.z)
			|| entry.alive_entity_ids.size() + entry.pending_deaths.size()
				>= entry.definition.maximum_alive)
			continue ;
		EntityState state;
		state.type = entry.definition.entity_type;
		state.position_x = entry.definition.x;
		state.position_y = entry.definition.y;
		state.position_z = entry.definition.z;
		state.health = 20;
		std::vector<EntityState> spawn_states;
		std::vector<uint32_t> spawned_ids;
		try
		{
			spawn_states.push_back(state);
			entry.alive_entity_ids.reserve(entry.alive_entity_ids.size() + 1U);
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		const int32_t result = entities.spawn_batch(spawn_states, spawned_ids);
		if (result != FT_ERR_SUCCESS)
			return (result);
		entry.alive_entity_ids.push_back(spawned_ids[0]);
		entry.next_spawn_tick = server_tick
			> UINT64_MAX - entry.definition.spawn_interval_ticks
			? UINT64_MAX : server_tick + entry.definition.spawn_interval_ticks;
		state_changed = true;
	}
	deaths_out.swap(staged_deaths);
	*state_changed_out = state_changed;
	return (FT_ERR_SUCCESS);
}

int32_t WorldSpawnerStore::acknowledge_death(uint32_t spawner_id,
	uint32_t entity_id) noexcept
{
	if (spawner_id == 0U || entity_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	auto entry = this->entries_.find(spawner_id);
	if (entry == this->entries_.end())
		return (FT_ERR_NOT_FOUND);
	auto death = std::find(entry->second.pending_deaths.begin(),
		entry->second.pending_deaths.end(), entity_id);
	if (death == entry->second.pending_deaths.end())
		return (FT_ERR_NOT_FOUND);
	entry->second.pending_deaths.erase(death);
	return (FT_ERR_SUCCESS);
}

int32_t WorldSpawnerStore::is_entity_tracked(uint32_t entity_id,
	bool *tracked_out) const noexcept
{
	if (entity_id == 0U || tracked_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	for (const auto &pair : this->entries_)
	{
		const Entry &entry = pair.second;
		if (contains_id(entry.alive_entity_ids, entity_id)
			|| contains_id(entry.pending_deaths, entity_id))
		{
			*tracked_out = true;
			return (FT_ERR_SUCCESS);
		}
	}
	*tracked_out = false;
	return (FT_ERR_SUCCESS);
}

int32_t WorldSpawnerStore::serialize(std::vector<uint8_t> &bytes_out) const noexcept
{
	std::vector<uint8_t> staged;
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (this->entries_.size() > WorldSpawnerStore::MAXIMUM_SPAWNERS)
		return (FT_ERR_FULL);
	try
	{
		staged.insert(staged.end(), {'W', 'S', 'P', '1'});
		append_u32(staged, WORLD_SPAWNER_VERSION);
		append_u32(staged, this->next_spawner_id_);
		append_u32(staged, static_cast<uint32_t>(this->entries_.size()));
		for (const auto &pair : this->entries_)
		{
			const Entry &entry = pair.second;
			if (entry.alive_entity_ids.size()
				> WorldSpawnerStore::MAXIMUM_ALIVE_PER_SPAWNER
				|| entry.pending_deaths.size()
				> WorldSpawnerStore::MAXIMUM_ALIVE_PER_SPAWNER
				|| entry.alive_entity_ids.size()
					+ entry.pending_deaths.size()
					> entry.definition.maximum_alive)
				return (FT_ERR_FULL);
			append_u32(staged, pair.first);
			uint64_t bits = 0U;
			std::memcpy(&bits, &entry.definition.x, sizeof(bits));
			append_u64(staged, bits);
			std::memcpy(&bits, &entry.definition.y, sizeof(bits));
			append_u64(staged, bits);
			std::memcpy(&bits, &entry.definition.z, sizeof(bits));
			append_u64(staged, bits);
			append_u32(staged, entry.definition.entity_type);
			append_u32(staged, entry.definition.maximum_alive);
			append_u32(staged, entry.definition.spawn_interval_ticks);
			append_u32(staged, entry.definition.loot_table_id);
			append_u64(staged, entry.next_spawn_tick);
			append_u32(staged, entry.active ? 1U : 0U);
			append_u32(staged,
				static_cast<uint32_t>(entry.alive_entity_ids.size()));
			append_u32(staged,
				static_cast<uint32_t>(entry.pending_deaths.size()));
			for (uint32_t id : entry.alive_entity_ids)
				append_u32(staged, id);
			for (uint32_t id : entry.pending_deaths)
				append_u32(staged, id);
		}
		append_u64(staged, hash_bytes(staged.data(), staged.size()));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	bytes_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t WorldSpawnerStore::deserialize(const uint8_t *bytes,
	std::size_t byte_count) noexcept
{
	std::map<uint32_t, Entry> staged;
	std::set<uint32_t> tracked_entity_ids;
	uint32_t next_id;
	uint32_t count;
	uint32_t maximum_id = 0U;
	std::size_t offset = 16U;
	if (bytes == nullptr || byte_count < 24U
		|| std::memcmp(bytes, "WSP1", 4U) != 0
		|| read_u32(bytes, 4U) != WORLD_SPAWNER_VERSION)
		return (FT_ERR_INVALID_ARGUMENT);
	next_id = read_u32(bytes, 8U);
	count = read_u32(bytes, 12U);
	if (count > WorldSpawnerStore::MAXIMUM_SPAWNERS || next_id == 0U
		|| read_u64(bytes, byte_count - 8U)
			!= hash_bytes(bytes, byte_count - 8U))
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		for (uint32_t index = 0U; index < count; ++index)
		{
			if (offset > byte_count - 8U || byte_count - 8U - offset < 64U)
				return (FT_ERR_INVALID_ARGUMENT);
			const uint32_t id = read_u32(bytes, offset);
			Entry entry = {};
			uint64_t bits = read_u64(bytes, offset + 4U);
			std::memcpy(&entry.definition.x, &bits, sizeof(bits));
			bits = read_u64(bytes, offset + 12U);
			std::memcpy(&entry.definition.y, &bits, sizeof(bits));
			bits = read_u64(bytes, offset + 20U);
			std::memcpy(&entry.definition.z, &bits, sizeof(bits));
			entry.definition.entity_type = read_u32(bytes, offset + 28U);
			entry.definition.maximum_alive = read_u32(bytes, offset + 32U);
			entry.definition.spawn_interval_ticks = read_u32(bytes, offset + 36U);
			entry.definition.loot_table_id = read_u32(bytes, offset + 40U);
			entry.next_spawn_tick = read_u64(bytes, offset + 44U);
			const uint32_t active = read_u32(bytes, offset + 52U);
			const uint32_t alive_count = read_u32(bytes, offset + 56U);
			const uint32_t death_count = read_u32(bytes, offset + 60U);
			if (offset > byte_count - 8U || byte_count - 8U - offset < 64U
				|| id == 0U || id <= maximum_id || active > 1U
				|| alive_count > WorldSpawnerStore::MAXIMUM_ALIVE_PER_SPAWNER
				|| death_count > WorldSpawnerStore::MAXIMUM_ALIVE_PER_SPAWNER
				|| !std::isfinite(entry.definition.x)
				|| !std::isfinite(entry.definition.y)
				|| !std::isfinite(entry.definition.z)
				|| !is_supported_mob_type(entry.definition.entity_type)
				|| entry.definition.maximum_alive == 0U
				|| entry.definition.maximum_alive
					> WorldSpawnerStore::MAXIMUM_ALIVE_PER_SPAWNER
				|| entry.definition.spawn_interval_ticks == 0U
				|| entry.definition.loot_table_id == 0U
				|| static_cast<uint64_t>(alive_count) + death_count
					> entry.definition.maximum_alive)
				return (FT_ERR_INVALID_ARGUMENT);
			entry.active = active != 0U;
			offset += 64U;
			if (static_cast<uint64_t>(alive_count) + death_count
				> (byte_count - 8U - offset) / 4U)
				return (FT_ERR_INVALID_ARGUMENT);
			entry.alive_entity_ids.reserve(alive_count);
			entry.pending_deaths.reserve(death_count);
			for (uint32_t entity_index = 0U; entity_index < alive_count;
				++entity_index)
			{
				const uint32_t entity_id = read_u32(bytes, offset);
				offset += 4U;
				if (entity_id == 0U
					|| contains_id(entry.alive_entity_ids, entity_id)
					|| !tracked_entity_ids.insert(entity_id).second)
					return (FT_ERR_INVALID_ARGUMENT);
				entry.alive_entity_ids.push_back(entity_id);
			}
			for (uint32_t death_index = 0U; death_index < death_count;
				++death_index)
			{
				const uint32_t entity_id = read_u32(bytes, offset);
				offset += 4U;
				if (entity_id == 0U
					|| contains_id(entry.alive_entity_ids, entity_id)
					|| contains_id(entry.pending_deaths, entity_id)
					|| !tracked_entity_ids.insert(entity_id).second)
					return (FT_ERR_INVALID_ARGUMENT);
				entry.pending_deaths.push_back(entity_id);
			}
			if (!staged.emplace(id, std::move(entry)).second)
				return (FT_ERR_INVALID_ARGUMENT);
			maximum_id = id;
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (offset != byte_count - 8U || next_id <= maximum_id)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	this->entries_.swap(staged);
	this->next_spawner_id_ = next_id;
	return (FT_ERR_SUCCESS);
}
