#include "WorldEntityStore.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <set>

namespace
{
	static const uint32_t ENTITY_SNAPSHOT_VERSION = 5U;
	static const uint32_t PROJECTILE_OWNER_ENTITY_SNAPSHOT_VERSION = 4U;
	static const uint32_t LEGACY_OWNER_ENTITY_SNAPSHOT_VERSION = 3U;
	static const uint32_t ITEM_ENTITY_SNAPSHOT_VERSION = 2U;
	static const uint32_t LEGACY_ENTITY_SNAPSHOT_VERSION = 1U;
	static const std::size_t ENTITY_SNAPSHOT_HEADER_SIZE = 16U;
	static const std::size_t ENTITY_SNAPSHOT_RECORD_SIZE = 112U;
	static const std::size_t ITEM_ENTITY_SNAPSHOT_RECORD_SIZE = 104U;
	static const std::size_t LEGACY_ENTITY_SNAPSHOT_RECORD_SIZE = 96U;
	static const std::size_t ENTITY_SNAPSHOT_CHECKSUM_SIZE = 8U;

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
		uint64_t value = 0U;
		for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			value |= static_cast<uint64_t>(bytes[offset + shift / 8U]) << shift;
		return (value);
	}

	static uint64_t double_to_bits(double value) noexcept
	{
		uint64_t bits;
		std::memcpy(&bits, &value, sizeof(bits));
		return (bits);
	}

	static double bits_to_double(uint64_t bits) noexcept
	{
		double value;
		std::memcpy(&value, &bits, sizeof(value));
		return (value);
	}

	static uint64_t snapshot_checksum(const uint8_t *bytes,
		std::size_t size) noexcept
	{
		uint64_t hash = 14695981039346656037ULL;
		for (std::size_t index = 0U; index < size; ++index)
		{
			hash ^= bytes[index];
			hash *= 1099511628211ULL;
		}
		return (hash);
	}

	static void append_entity(std::vector<uint8_t> &bytes,
		const WorldEntitySnapshot &snapshot)
	{
		const EntityState &state = snapshot.state;
		append_u64(bytes, snapshot.revision);
		append_u32(bytes, state.id);
		append_u32(bytes, state.type);
		append_u64(bytes, double_to_bits(state.position_x));
		append_u64(bytes, double_to_bits(state.position_y));
		append_u64(bytes, double_to_bits(state.position_z));
		append_u64(bytes, double_to_bits(state.velocity_x));
		append_u64(bytes, double_to_bits(state.velocity_y));
		append_u64(bytes, double_to_bits(state.velocity_z));
		append_u64(bytes, double_to_bits(state.orientation_yaw));
		append_u64(bytes, double_to_bits(state.orientation_pitch));
		append_u32(bytes, static_cast<uint32_t>(state.health));
		append_u32(bytes, state.action_state);
		append_u64(bytes, state.tick);
	append_u64(bytes, snapshot.item_instance_id);
	append_u64(bytes, snapshot.owner_actor_id);
	}

	static EntityState read_entity(const uint8_t *bytes,
		std::size_t offset) noexcept
	{
		EntityState state;
		offset += 8U;
		state.id = read_u32(bytes, offset);
		state.type = read_u32(bytes, offset + 4U);
		state.position_x = bits_to_double(read_u64(bytes, offset + 8U));
		state.position_y = bits_to_double(read_u64(bytes, offset + 16U));
		state.position_z = bits_to_double(read_u64(bytes, offset + 24U));
		state.velocity_x = bits_to_double(read_u64(bytes, offset + 32U));
		state.velocity_y = bits_to_double(read_u64(bytes, offset + 40U));
		state.velocity_z = bits_to_double(read_u64(bytes, offset + 48U));
		state.orientation_yaw = bits_to_double(read_u64(bytes, offset + 56U));
		state.orientation_pitch = bits_to_double(read_u64(bytes, offset + 64U));
		state.health = static_cast<int32_t>(read_u32(bytes, offset + 72U));
		state.action_state = read_u32(bytes, offset + 76U);
		state.tick = read_u64(bytes, offset + 80U);
		return (state);
	}
}

WorldEntityStore::WorldEntityStore() noexcept
	: mutex_(), entities_(), next_entity_id_(1U), initialized_(false)
{
}

int32_t WorldEntityStore::initialize() noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (this->initialized_)
		return (FT_ERR_ALREADY_INITIALISED);
	this->entities_.clear();
	this->next_entity_id_ = 1U;
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldEntityStore::destroy() noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	this->entities_.clear();
	this->next_entity_id_ = 1U;
	this->initialized_ = false;
	return (FT_ERR_SUCCESS);
}

bool WorldEntityStore::is_initialized() const noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	return (this->initialized_);
}

bool WorldEntityStore::state_is_valid(const EntityState &state) noexcept
{
	return (state.id != 0U && state.type != 0U && state.health >= 0
		&& state.action_state
			<= static_cast<uint32_t>(EntityState::ActionState::DEAD)
		&& std::isfinite(state.position_x) && std::isfinite(state.position_y)
		&& std::isfinite(state.position_z) && std::isfinite(state.velocity_x)
		&& std::isfinite(state.velocity_y) && std::isfinite(state.velocity_z)
		&& std::isfinite(state.orientation_yaw)
		&& std::isfinite(state.orientation_pitch));
}

int32_t WorldEntityStore::spawn(const EntityState &state,
	uint32_t *entity_id_out) noexcept
{
	return (this->spawn(state, 0U, entity_id_out));
}

int32_t WorldEntityStore::spawn(const EntityState &state,
	uint64_t owner_actor_id, uint32_t *entity_id_out) noexcept
{
	EntityState staged = state;
	uint32_t entity_id;
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (entity_id_out == nullptr || state.id != 0U || state.type == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (state.type == static_cast<uint32_t>(EntityState::Kind::BOAT))
		return (FT_ERR_INVALID_ARGUMENT);
	if (owner_actor_id != 0U && state.type
		!= static_cast<uint32_t>(EntityState::Kind::PROJECTILE))
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->entities_.size() >= WorldEntityStore::MAXIMUM_ENTITIES
		|| this->next_entity_id_ == 0U
		|| this->next_entity_id_ == UINT32_MAX)
		return (FT_ERR_FULL);
	entity_id = this->next_entity_id_;
	staged.id = entity_id;
	if (!WorldEntityStore::state_is_valid(staged))
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		if (!this->entities_.emplace(entity_id,
				Entry{staged, 1U, 0U, owner_actor_id}).second)
			return (FT_ERR_ALREADY_EXISTS);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	++this->next_entity_id_;
	*entity_id_out = entity_id;
	return (FT_ERR_SUCCESS);
}

int32_t WorldEntityStore::spawn_item(const EntityState &state,
	uint64_t item_instance_id, uint32_t *entity_id_out) noexcept
{
	return (this->spawn_item(state, item_instance_id, 0U, entity_id_out));
}

int32_t WorldEntityStore::spawn_item(const EntityState &state,
	uint64_t item_instance_id, uint64_t owner_actor_id,
	uint32_t *entity_id_out) noexcept
{
	EntityState staged = state;
	uint32_t entity_id;
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (entity_id_out == nullptr || state.id != 0U
		|| state.type != static_cast<uint32_t>(EntityState::Kind::BOAT)
		|| item_instance_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	for (const auto &entry : this->entities_)
	{
		if (entry.second.item_instance_id == item_instance_id)
			return (FT_ERR_ALREADY_EXISTS);
	}
	if (this->entities_.size() >= WorldEntityStore::MAXIMUM_ENTITIES
		|| this->next_entity_id_ == 0U
		|| this->next_entity_id_ == UINT32_MAX)
		return (FT_ERR_FULL);
	entity_id = this->next_entity_id_;
	staged.id = entity_id;
	if (!WorldEntityStore::state_is_valid(staged))
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		if (!this->entities_.emplace(entity_id,
				Entry{staged, 1U, item_instance_id, owner_actor_id}).second)
			return (FT_ERR_ALREADY_EXISTS);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	++this->next_entity_id_;
	*entity_id_out = entity_id;
	return (FT_ERR_SUCCESS);
}

int32_t WorldEntityStore::spawn_batch(
	const std::vector<EntityState> &states,
	std::vector<uint32_t> &ids_out) noexcept
{
	std::vector<uint32_t> staged_ids;
	std::size_t inserted_count = 0U;
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (states.empty())
		return (FT_ERR_INVALID_ARGUMENT);
	if (states.size() > WorldEntityStore::MAXIMUM_ENTITIES
			- this->entities_.size()
		|| this->next_entity_id_ == 0U
		|| states.size() > static_cast<std::size_t>(UINT32_MAX
			- this->next_entity_id_))
		return (FT_ERR_FULL);
	for (std::size_t index = 0U; index < states.size(); ++index)
	{
		EntityState staged_state = states[index];
		staged_state.id = this->next_entity_id_
			+ static_cast<uint32_t>(index);
		if (states[index].id != 0U
			|| states[index].type
				== static_cast<uint32_t>(EntityState::Kind::BOAT)
			|| !WorldEntityStore::state_is_valid(staged_state))
			return (FT_ERR_INVALID_ARGUMENT);
	}
	try
	{
		staged_ids.reserve(states.size());
		for (std::size_t index = 0U; index < states.size(); ++index)
		{
			const uint32_t entity_id = this->next_entity_id_
				+ static_cast<uint32_t>(index);
			EntityState staged_state = states[index];
			staged_state.id = entity_id;
			const auto inserted = this->entities_.emplace(entity_id,
				Entry{staged_state, 1U, 0U, 0U});
			if (!inserted.second)
			{
				for (std::size_t rollback = 0U; rollback < inserted_count;
					++rollback)
					this->entities_.erase(staged_ids[rollback]);
				return (FT_ERR_ALREADY_EXISTS);
			}
			staged_ids.push_back(entity_id);
			++inserted_count;
		}
	}
	catch (const std::bad_alloc &)
	{
		for (std::size_t rollback = 0U; rollback < inserted_count; ++rollback)
			this->entities_.erase(staged_ids[rollback]);
		return (FT_ERR_NO_MEMORY);
	}
	this->next_entity_id_ += static_cast<uint32_t>(states.size());
	ids_out.swap(staged_ids);
	return (FT_ERR_SUCCESS);
}

int32_t WorldEntityStore::update(uint32_t entity_id,
	uint64_t expected_revision, const EntityState &state,
	uint64_t *new_revision_out) noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (entity_id == 0U || expected_revision == 0U || new_revision_out == nullptr
		|| state.id != entity_id || !WorldEntityStore::state_is_valid(state))
		return (FT_ERR_INVALID_ARGUMENT);
	auto entry = this->entities_.find(entity_id);
	if (entry == this->entities_.end())
		return (FT_ERR_NOT_FOUND);
	if (entry->second.revision != expected_revision)
		return (FT_ERR_INVALID_STATE);
	if ((entry->second.item_instance_id != 0U
			&& state.type != static_cast<uint32_t>(EntityState::Kind::BOAT))
		|| (state.type == static_cast<uint32_t>(EntityState::Kind::BOAT)
			&& entry->second.item_instance_id == 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	if (entry->second.revision == UINT64_MAX)
		return (FT_ERR_OUT_OF_RANGE);
	entry->second.state = state;
	++entry->second.revision;
	*new_revision_out = entry->second.revision;
	return (FT_ERR_SUCCESS);
}

int32_t WorldEntityStore::apply_damage(uint32_t entity_id,
	uint64_t expected_revision, int32_t damage,
	WorldEntitySnapshot *snapshot_out) noexcept
{
	return (this->apply_damage(entity_id, expected_revision, damage, 0U,
		snapshot_out));
}

int32_t WorldEntityStore::apply_damage(uint32_t entity_id,
	uint64_t expected_revision, int32_t damage, uint64_t source_actor_id,
	WorldEntitySnapshot *snapshot_out) noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (entity_id == 0U || expected_revision == 0U || damage <= 0
		|| snapshot_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	auto entry = this->entities_.find(entity_id);
	if (entry == this->entities_.end())
		return (FT_ERR_NOT_FOUND);
	if (entry->second.revision != expected_revision)
		return (FT_ERR_INVALID_STATE);
	if (entry->second.item_instance_id != 0U
		|| entry->second.state.type
			< static_cast<uint32_t>(EntityState::Kind::ZOMBIE)
		|| entry->second.state.type
			> static_cast<uint32_t>(EntityState::Kind::SPIDER)
		|| entry->second.state.type
			== static_cast<uint32_t>(EntityState::Kind::BOAT))
		return (FT_ERR_PERMISSION_DENIED);
	if (entry->second.state.health <= 0
		|| entry->second.state.action_state
			== static_cast<uint32_t>(EntityState::ActionState::DEAD))
		return (FT_ERR_INVALID_STATE);
	if (entry->second.revision == UINT64_MAX)
		return (FT_ERR_OUT_OF_RANGE);
	EntityState staged_state = entry->second.state;
	if (damage >= staged_state.health)
	{
		staged_state.health = 0;
		staged_state.action_state = static_cast<uint32_t>(
			EntityState::ActionState::DEAD);
	}
	else
		staged_state.health -= damage;
	entry->second.state = staged_state;
	entry->second.owner_actor_id = source_actor_id;
	++entry->second.revision;
	snapshot_out->state = entry->second.state;
	snapshot_out->revision = entry->second.revision;
	snapshot_out->item_instance_id = entry->second.item_instance_id;
	snapshot_out->owner_actor_id = entry->second.owner_actor_id;
	return (FT_ERR_SUCCESS);
}

int32_t WorldEntityStore::rollback_update(
	const WorldEntitySnapshot &prior_snapshot,
	uint64_t expected_current_revision) noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (prior_snapshot.state.id == 0U || prior_snapshot.revision == 0U
		|| expected_current_revision == 0U
		|| prior_snapshot.revision >= expected_current_revision
		|| !WorldEntityStore::state_is_valid(prior_snapshot.state))
		return (FT_ERR_INVALID_ARGUMENT);
	auto entry = this->entities_.find(prior_snapshot.state.id);
	if (entry == this->entities_.end())
		return (FT_ERR_NOT_FOUND);
	if (entry->second.revision != expected_current_revision
		|| entry->second.item_instance_id != prior_snapshot.item_instance_id)
		return (FT_ERR_INVALID_STATE);
	entry->second.state = prior_snapshot.state;
	entry->second.revision = prior_snapshot.revision;
	entry->second.owner_actor_id = prior_snapshot.owner_actor_id;
	return (FT_ERR_SUCCESS);
}

int32_t WorldEntityStore::despawn(uint32_t entity_id,
	uint64_t expected_revision) noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (entity_id == 0U || expected_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	auto entry = this->entities_.find(entity_id);
	if (entry == this->entities_.end())
		return (FT_ERR_NOT_FOUND);
	if (entry->second.revision != expected_revision)
		return (FT_ERR_INVALID_STATE);
	this->entities_.erase(entry);
	return (FT_ERR_SUCCESS);
}

int32_t WorldEntityStore::get(uint32_t entity_id,
	WorldEntitySnapshot *snapshot_out) const noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (entity_id == 0U || snapshot_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	const auto entry = this->entities_.find(entity_id);
	if (entry == this->entities_.end())
		return (FT_ERR_NOT_FOUND);
	snapshot_out->state = entry->second.state;
	snapshot_out->revision = entry->second.revision;
	snapshot_out->item_instance_id = entry->second.item_instance_id;
	snapshot_out->owner_actor_id = entry->second.owner_actor_id;
	return (FT_ERR_SUCCESS);
}

int32_t WorldEntityStore::query_radius(double center_x, double center_y,
	double center_z, double radius, std::size_t maximum_results,
	std::vector<WorldEntitySnapshot> &snapshots_out) const noexcept
{
	std::vector<WorldEntitySnapshot> staged;
	const double radius_squared = radius * radius;
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (!std::isfinite(center_x) || !std::isfinite(center_y)
		|| !std::isfinite(center_z) || !std::isfinite(radius)
		|| radius <= 0.0 || !std::isfinite(radius_squared)
		|| maximum_results == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		staged.reserve(std::min(maximum_results, this->entities_.size()));
		for (const auto &entry : this->entities_)
		{
			const double dx = entry.second.state.position_x - center_x;
			const double dy = entry.second.state.position_y - center_y;
			const double dz = entry.second.state.position_z - center_z;
			if (dx * dx + dy * dy + dz * dz <= radius_squared)
			{
				if (staged.size() >= maximum_results)
					return (FT_ERR_FULL);
				staged.push_back(WorldEntitySnapshot{entry.second.state,
					entry.second.revision, entry.second.item_instance_id,
					entry.second.owner_actor_id});
			}
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	snapshots_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t WorldEntityStore::snapshot(
	std::vector<WorldEntitySnapshot> &snapshots_out) const noexcept
{
	std::vector<WorldEntitySnapshot> staged;
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	try
	{
		staged.reserve(this->entities_.size());
		for (const auto &entry : this->entities_)
			staged.push_back({entry.second.state, entry.second.revision,
				entry.second.item_instance_id, entry.second.owner_actor_id});
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	snapshots_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t WorldEntityStore::serialize(
	std::vector<uint8_t> &bytes_out) const noexcept
{
	std::vector<uint8_t> staged;
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	try
	{
		staged.reserve(ENTITY_SNAPSHOT_HEADER_SIZE
			+ this->entities_.size() * ENTITY_SNAPSHOT_RECORD_SIZE
			+ ENTITY_SNAPSHOT_CHECKSUM_SIZE);
		staged.insert(staged.end(), {'W', 'E', 'N', 'S'});
		append_u32(staged, ENTITY_SNAPSHOT_VERSION);
		append_u32(staged, this->next_entity_id_);
		append_u32(staged, static_cast<uint32_t>(this->entities_.size()));
		for (const auto &entry : this->entities_)
			append_entity(staged,
				{entry.second.state, entry.second.revision,
				entry.second.item_instance_id, entry.second.owner_actor_id});
		append_u64(staged, snapshot_checksum(staged.data(), staged.size()));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	bytes_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t WorldEntityStore::get_max_item_instance_id(
	uint64_t *instance_id_out) const noexcept
{
	uint64_t maximum_instance_id = 0U;
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (instance_id_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	for (const auto &entry : this->entities_)
		maximum_instance_id = std::max(maximum_instance_id,
			entry.second.item_instance_id);
	*instance_id_out = maximum_instance_id;
	return (FT_ERR_SUCCESS);
}

int32_t WorldEntityStore::deserialize(const uint8_t *bytes,
	std::size_t byte_count) noexcept
{
	std::map<uint32_t, Entry> staged;
	std::set<uint64_t> staged_item_instance_ids;
	uint32_t next_entity_id;
	uint32_t entity_count;
	uint32_t maximum_id = 0U;
	std::size_t expected_size;
	uint32_t snapshot_version;
	std::size_t record_size;
	if (bytes == nullptr || byte_count < ENTITY_SNAPSHOT_HEADER_SIZE
		+ ENTITY_SNAPSHOT_CHECKSUM_SIZE)
		return (FT_ERR_INVALID_ARGUMENT);
	snapshot_version = read_u32(bytes, 4U);
	if (bytes[0] != 'W' || bytes[1] != 'E' || bytes[2] != 'N'
		|| bytes[3] != 'S'
		|| (snapshot_version != ENTITY_SNAPSHOT_VERSION
			&& snapshot_version != PROJECTILE_OWNER_ENTITY_SNAPSHOT_VERSION
			&& snapshot_version != LEGACY_OWNER_ENTITY_SNAPSHOT_VERSION
			&& snapshot_version != ITEM_ENTITY_SNAPSHOT_VERSION
			&& snapshot_version != LEGACY_ENTITY_SNAPSHOT_VERSION))
		return (FT_ERR_INVALID_ARGUMENT);
	record_size = snapshot_version == LEGACY_ENTITY_SNAPSHOT_VERSION
		? LEGACY_ENTITY_SNAPSHOT_RECORD_SIZE
		: ((snapshot_version == ITEM_ENTITY_SNAPSHOT_VERSION)
			? ITEM_ENTITY_SNAPSHOT_RECORD_SIZE : ENTITY_SNAPSHOT_RECORD_SIZE);
	next_entity_id = read_u32(bytes, 8U);
	entity_count = read_u32(bytes, 12U);
	if (entity_count > WorldEntityStore::MAXIMUM_ENTITIES
		|| static_cast<std::size_t>(entity_count)
			> (std::numeric_limits<std::size_t>::max()
				- ENTITY_SNAPSHOT_HEADER_SIZE - ENTITY_SNAPSHOT_CHECKSUM_SIZE)
			/ record_size)
		return (FT_ERR_OUT_OF_RANGE);
	expected_size = ENTITY_SNAPSHOT_HEADER_SIZE
		+ static_cast<std::size_t>(entity_count) * record_size
		+ ENTITY_SNAPSHOT_CHECKSUM_SIZE;
	if (byte_count != expected_size || next_entity_id == 0U
		|| read_u64(bytes, byte_count - ENTITY_SNAPSHOT_CHECKSUM_SIZE)
			!= snapshot_checksum(bytes, byte_count - ENTITY_SNAPSHOT_CHECKSUM_SIZE))
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		for (uint32_t index = 0U; index < entity_count; ++index)
		{
			const std::size_t offset = ENTITY_SNAPSHOT_HEADER_SIZE
				+ static_cast<std::size_t>(index) * record_size;
			const uint64_t revision = read_u64(bytes, offset);
			EntityState state = read_entity(bytes, offset);
			const uint64_t item_instance_id = snapshot_version
				== LEGACY_ENTITY_SNAPSHOT_VERSION ? 0U
				: read_u64(bytes, offset + LEGACY_ENTITY_SNAPSHOT_RECORD_SIZE);
			const uint64_t owner_actor_id = snapshot_version
				>= LEGACY_OWNER_ENTITY_SNAPSHOT_VERSION
				? read_u64(bytes, offset + ITEM_ENTITY_SNAPSHOT_RECORD_SIZE) : 0U;
			bool owner_type_supported = false;
			if (item_instance_id != 0U)
				owner_type_supported = state.type
					== static_cast<uint32_t>(EntityState::Kind::BOAT);
			else if (snapshot_version
				>= PROJECTILE_OWNER_ENTITY_SNAPSHOT_VERSION
				&& state.type == static_cast<uint32_t>(
					EntityState::Kind::PROJECTILE))
				owner_type_supported = true;
		else if (snapshot_version >= ENTITY_SNAPSHOT_VERSION
			&& state.type >= static_cast<uint32_t>(EntityState::Kind::ZOMBIE)
			&& state.type <= static_cast<uint32_t>(EntityState::Kind::SPIDER)
			&& state.type != static_cast<uint32_t>(EntityState::Kind::BOAT))
			owner_type_supported = true;
		const bool owner_is_supported = owner_actor_id == 0U
			|| owner_type_supported;
			if (revision == 0U || !WorldEntityStore::state_is_valid(state)
				|| (state.type == static_cast<uint32_t>(EntityState::Kind::BOAT)
					&& item_instance_id == 0U)
				|| (item_instance_id != 0U
					&& state.type != static_cast<uint32_t>(EntityState::Kind::BOAT))
				|| !owner_is_supported
				|| (item_instance_id != 0U
					&& !staged_item_instance_ids.insert(item_instance_id).second)
				|| state.id <= maximum_id
				|| !staged.emplace(state.id, Entry{state, revision,
					item_instance_id, owner_actor_id}).second)
				return (FT_ERR_INVALID_ARGUMENT);
			maximum_id = state.id;
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (next_entity_id <= maximum_id)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	this->entities_.swap(staged);
	this->next_entity_id_ = next_entity_id;
	return (FT_ERR_SUCCESS);
}
