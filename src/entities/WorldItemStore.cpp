#include "../../src/entities/WorldItemStore.hpp"
#include "../../src/entities/WorldEntityStore.hpp"
#include "../../src/inventory/MinecraftItemCatalog.hpp"

#include "../../Libft/Modules/Game/game_voxel_chunk.hpp"
#include "../../Libft/Modules/Filesystem/filesystem.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <cstring>
#include <new>
#include <set>
#include <unordered_set>

namespace
{
	static_assert(sizeof(double) == sizeof(uint64_t),
		"World item snapshots require 64-bit doubles");
	static const uint32_t WORLD_ITEM_SNAPSHOT_VERSION = 1U;
	static const uint32_t WORLD_ITEM_SNAPSHOT_MAX_ENTRIES = 100000U;
	static const std::size_t WORLD_ITEM_SNAPSHOT_MAX_BYTES = 64U * 1024U * 1024U;

	static void world_item_append_u32(std::vector<uint8_t> &bytes,
		uint32_t value)
	{
		for (uint32_t index = 0U; index < 4U; ++index)
			bytes.push_back(static_cast<uint8_t>(value >> (index * 8U)));
	}

	static void world_item_append_u64(std::vector<uint8_t> &bytes,
		uint64_t value)
	{
		for (uint32_t index = 0U; index < 8U; ++index)
			bytes.push_back(static_cast<uint8_t>(value >> (index * 8U)));
	}

	static uint64_t world_item_checksum(const uint8_t *bytes,
		std::size_t byte_count) noexcept
	{
		uint64_t hash = 14695981039346656037ULL;
		for (std::size_t index = 0U; index < byte_count; ++index)
		{
			hash ^= bytes[index];
			hash *= 1099511628211ULL;
		}
		return (hash);
	}

	struct WorldItemSnapshotReader
	{
		const uint8_t *bytes;
		std::size_t size;
		std::size_t offset;

		bool read_u32(uint32_t *value) noexcept
		{
			if (value == nullptr || this->offset > this->size
				|| this->size - this->offset < 4U)
				return (false);
			*value = static_cast<uint32_t>(this->bytes[this->offset])
				| (static_cast<uint32_t>(this->bytes[this->offset + 1U]) << 8U)
				| (static_cast<uint32_t>(this->bytes[this->offset + 2U]) << 16U)
				| (static_cast<uint32_t>(this->bytes[this->offset + 3U]) << 24U);
			this->offset += 4U;
			return (true);
		}

		bool read_u64(uint64_t *value) noexcept
		{
			if (value == nullptr || this->offset > this->size
				|| this->size - this->offset < 8U)
				return (false);
			*value = 0U;
			for (uint32_t index = 0U; index < 8U; ++index)
				*value |= static_cast<uint64_t>(this->bytes[this->offset + index])
					<< (index * 8U);
			this->offset += 8U;
			return (true);
		}
	};
}

struct WorldItemStore::Entry
{
	uint64_t entity_id;
	uint64_t entity_revision;
	uint64_t spawned_tick;
	uint64_t owner_id;
	uint64_t owner_lock_until_tick;
	int32_t chunk_x;
	int32_t chunk_z;
	int32_t item_id;
	int32_t max_stack;
	double x;
	double y;
	double z;
	bool published;
	game_inventory contents;

	Entry() : entity_id(0U), entity_revision(1U), spawned_tick(0U),
		owner_id(0U), owner_lock_until_tick(0U), chunk_x(0), chunk_z(0),
		item_id(0), max_stack(0), x(0.0), y(0.0), z(0.0),
		published(true), contents()
	{
	}
};

const double WorldItemStore::MAX_QUERY_RADIUS = 256.0;
const double WorldItemStore::MAX_PICKUP_REACH = 8.0;

WorldItemStore::WorldItemStore() : mutex_(), entries_(), chunk_index_(),
	pickup_cache_(), pickup_order_(), next_entity_id_(1U),
	next_transfer_transaction_id_(1U), change_revision_(1U), initialized_(false)
{
}

WorldItemStore::~WorldItemStore()
{
	(void)this->destroy();
}

uint64_t WorldItemStore::chunk_key(int32_t chunk_x, int32_t chunk_z) noexcept
{
	return ((static_cast<uint64_t>(static_cast<uint32_t>(chunk_x)) << 32)
		| static_cast<uint64_t>(static_cast<uint32_t>(chunk_z)));
}

int32_t WorldItemStore::world_coordinate_to_chunk(double coordinate,
	int32_t *chunk_out) noexcept
{
	double chunk_coordinate;

	if (chunk_out == nullptr || !std::isfinite(coordinate))
		return (FT_ERR_INVALID_ARGUMENT);
	chunk_coordinate = std::floor(coordinate
		/ static_cast<double>(GAME_VOXEL_CHUNK_WIDTH));
	if (chunk_coordinate < static_cast<double>(std::numeric_limits<int32_t>::min())
		|| chunk_coordinate > static_cast<double>(std::numeric_limits<int32_t>::max()))
		return (FT_ERR_OUT_OF_RANGE);
	*chunk_out = static_cast<int32_t>(chunk_coordinate);
	return (FT_ERR_SUCCESS);
}

bool WorldItemStore::pickup_matches(const CachedPickup &cached,
	uint64_t entity_id, uint64_t expected_entity_revision,
	uint64_t expected_inventory_revision, int32_t requested_quantity) noexcept
{
	return (cached.entity_id == entity_id
		&& cached.expected_entity_revision == expected_entity_revision
		&& cached.expected_inventory_revision == expected_inventory_revision
		&& cached.requested_quantity == requested_quantity);
}

int32_t WorldItemStore::initialize() noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);

	if (this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	this->entries_.clear();
	this->chunk_index_.clear();
	this->pickup_cache_.clear();
	this->pickup_order_.clear();
	this->next_entity_id_ = 1U;
	this->next_transfer_transaction_id_ = 1U;
	this->change_revision_ = 1U;
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldItemStore::destroy() noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);

	this->initialized_ = false;
	this->entries_.clear();
	this->chunk_index_.clear();
	this->pickup_cache_.clear();
	this->pickup_order_.clear();
	this->next_entity_id_ = 1U;
	this->next_transfer_transaction_id_ = 1U;
	this->change_revision_ = 1U;
	return (FT_ERR_SUCCESS);
}

bool WorldItemStore::is_initialized() const noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	return (this->initialized_);
}

int32_t WorldItemStore::spawn_pending(const ft_sharedptr<game_item> &item,
	double x, double y, double z, uint64_t spawned_tick, uint64_t owner_id,
	uint64_t owner_lock_until_tick, uint64_t *entity_id_out) noexcept
{
	std::unique_ptr<Entry> entry;
	std::unordered_map<uint64_t, std::vector<uint64_t> >::iterator bucket;
	bool bucket_created;
	int32_t chunk_x;
	int32_t chunk_z;
	int32_t error_code;
	uint64_t key;
	uint64_t entity_id;

	if (!item || entity_id_out == nullptr || !std::isfinite(x)
		|| !std::isfinite(y) || !std::isfinite(z)
		|| item->get_item_id() <= 0 || item->get_stack_size() <= 0
		|| item->get_max_stack() <= 0
		|| item->get_stack_size() > item->get_max_stack()
		|| item->get_width() <= 0 || item->get_height() <= 0)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = WorldItemStore::world_coordinate_to_chunk(x, &chunk_x);
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemStore::world_coordinate_to_chunk(z, &chunk_z);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	entry.reset(new (std::nothrow) Entry());
	if (!entry)
		return (FT_ERR_NO_MEMORY);
	error_code = entry->contents.initialize(1U,
		std::numeric_limits<int32_t>::max());
	if (error_code == FT_ERR_SUCCESS)
		error_code = entry->contents.enable_thread_safety();
	if (error_code == FT_ERR_SUCCESS)
		error_code = entry->contents.add_item(item);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	entry->spawned_tick = spawned_tick;
	entry->owner_id = owner_id;
	entry->owner_lock_until_tick = owner_lock_until_tick;
	entry->chunk_x = chunk_x;
	entry->chunk_z = chunk_z;
	entry->item_id = item->get_item_id();
	entry->max_stack = item->get_max_stack();
	entry->x = x;
	entry->y = y;
	entry->z = z;
	entry->published = false;
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (item->get_instance_id() != 0U)
	{
		for (const auto &existing_entry : this->entries_)
		{
			game_inventory_snapshot existing_snapshot = {};
			error_code = existing_entry.second->contents.get_snapshot(
				existing_snapshot);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			if (existing_snapshot.items.size() != 1U)
				return (FT_ERR_INVALID_STATE);
			if (existing_snapshot.items[0].instance_id
				== item->get_instance_id())
				return (FT_ERR_ALREADY_EXISTS);
		}
	}
	if (this->next_entity_id_ == 0U
		|| this->next_entity_id_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	entity_id = this->next_entity_id_;
	key = WorldItemStore::chunk_key(chunk_x, chunk_z);
	bucket_created = false;
	try
	{
		bucket = this->chunk_index_.find(key);
		if (bucket == this->chunk_index_.end())
		{
			bucket = this->chunk_index_.try_emplace(key).first;
			bucket_created = true;
		}
		bucket->second.push_back(entity_id);
		entry->entity_id = entity_id;
		if (!this->entries_.emplace(entity_id, std::move(entry)).second)
		{
			bucket->second.pop_back();
			if (bucket_created && bucket->second.empty())
				this->chunk_index_.erase(bucket);
			return (FT_ERR_INVALID_STATE);
		}
	}
	catch (const std::bad_alloc &)
	{
		bucket = this->chunk_index_.find(key);
		if (bucket != this->chunk_index_.end())
		{
			if (!bucket->second.empty() && bucket->second.back() == entity_id)
				bucket->second.pop_back();
			if (bucket->second.empty())
				this->chunk_index_.erase(bucket);
		}
		return (FT_ERR_NO_MEMORY);
	}
	this->next_entity_id_ += 1U;
	*entity_id_out = entity_id;
	return (FT_ERR_SUCCESS);
}

int32_t WorldItemStore::spawn(const ft_sharedptr<game_item> &item,
	double x, double y, double z, uint64_t spawned_tick, uint64_t owner_id,
	uint64_t owner_lock_until_tick, uint64_t *entity_id_out) noexcept
{
	uint64_t entity_id = 0U;
	int32_t result;

	if (entity_id_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*entity_id_out = 0U;
	result = this->spawn_pending(item, x, y, z, spawned_tick, owner_id,
		owner_lock_until_tick, &entity_id);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->publish_spawn(entity_id, 1U);
	if (result != FT_ERR_SUCCESS)
	{
		(void)this->despawn(entity_id, 1U);
		return (result);
	}
	*entity_id_out = entity_id;
	return (FT_ERR_SUCCESS);
}

int32_t WorldItemStore::publish_spawn(uint64_t entity_id,
	uint64_t expected_entity_revision) noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (entity_id == 0U || expected_entity_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	const auto found = this->entries_.find(entity_id);
	if (found == this->entries_.end())
		return (FT_ERR_NOT_FOUND);
	Entry &entry = *found->second;
	if (entry.entity_revision != expected_entity_revision)
		return (FT_ERR_INVALID_STATE);
	if (entry.published)
		return (FT_ERR_SUCCESS);
	if (this->change_revision_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	entry.published = true;
	this->change_revision_ += 1U;
	return (FT_ERR_SUCCESS);
}

int32_t WorldItemStore::despawn(uint64_t entity_id,
	uint64_t expected_entity_revision) noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	std::unordered_map<uint64_t, std::unique_ptr<Entry> >::iterator found;

	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	found = this->entries_.find(entity_id);
	if (found == this->entries_.end())
		return (FT_ERR_NOT_FOUND);
	if (found->second->entity_revision != expected_entity_revision)
		return (FT_ERR_INVALID_STATE);
	if (found->second->published
		&& this->change_revision_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	if (found->second->published)
		this->change_revision_ += 1U;
	this->remove_entity_locked(entity_id, *found->second);
	return (FT_ERR_SUCCESS);
}

int32_t WorldItemStore::merge_stacks(uint64_t source_entity_id,
	uint64_t expected_source_revision, uint64_t destination_entity_id,
	uint64_t expected_destination_revision,
	int32_t *transferred_quantity_out) noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	std::unordered_map<uint64_t, std::unique_ptr<Entry> >::iterator source;
	std::unordered_map<uint64_t, std::unique_ptr<Entry> >::iterator destination;
	game_inventory_snapshot source_snapshot = {};
	game_inventory_snapshot destination_snapshot = {};
	uint64_t source_inventory_revision;
	uint64_t destination_inventory_revision;
	uint64_t transaction_id;
	int32_t transfer_quantity;
	int32_t source_remaining;
	int32_t result;
	game_inventory_transfer_result transfer_result = {};

	if (transferred_quantity_out == nullptr || source_entity_id == 0U
		|| destination_entity_id == 0U
		|| source_entity_id == destination_entity_id
		|| expected_source_revision == 0U
		|| expected_destination_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	*transferred_quantity_out = 0;
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	source = this->entries_.find(source_entity_id);
	destination = this->entries_.find(destination_entity_id);
	if (source == this->entries_.end() || destination == this->entries_.end())
		return (FT_ERR_NOT_FOUND);
	Entry &source_entry = *source->second;
	Entry &destination_entry = *destination->second;
	if (!source_entry.published || !destination_entry.published)
		return (FT_ERR_NOT_FOUND);
	if (source_entry.entity_revision != expected_source_revision
		|| destination_entry.entity_revision != expected_destination_revision)
		return (FT_ERR_INVALID_STATE);
	if (source_entry.entity_revision == std::numeric_limits<uint64_t>::max()
		|| destination_entry.entity_revision
			== std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	if (this->change_revision_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	if (source_entry.item_id != destination_entry.item_id
		|| source_entry.max_stack != destination_entry.max_stack
		|| source_entry.owner_id != destination_entry.owner_id
		|| source_entry.owner_lock_until_tick
			!= destination_entry.owner_lock_until_tick)
		return (FT_ERR_INVALID_ARGUMENT);
	{
		const double dx = source_entry.x - destination_entry.x;
		const double dy = source_entry.y - destination_entry.y;
		const double dz = source_entry.z - destination_entry.z;
		if (dx * dx + dy * dy + dz * dz > 1.0)
			return (FT_ERR_OUT_OF_RANGE);
	}
	result = source_entry.contents.get_snapshot(source_snapshot);
	if (result == FT_ERR_SUCCESS)
		result = destination_entry.contents.get_snapshot(destination_snapshot);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (source_snapshot.items.size() != 1U
		|| destination_snapshot.items.size() != 1U
		|| source_snapshot.items[0].item_id != source_entry.item_id
		|| destination_snapshot.items[0].item_id != destination_entry.item_id
		|| source_snapshot.items[0].instance_id != 0U
		|| destination_snapshot.items[0].instance_id != 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (source_snapshot.items[0].stack_size <= 0
		|| destination_snapshot.items[0].stack_size
			>= destination_snapshot.items[0].max_stack)
		return (FT_ERR_FULL);
	result = source_entry.contents.get_revision(&source_inventory_revision);
	if (result == FT_ERR_SUCCESS)
		result = destination_entry.contents.get_revision(
			&destination_inventory_revision);
	if (result != FT_ERR_SUCCESS)
		return (result);
	transfer_quantity = source_snapshot.items[0].stack_size;
	while (transfer_quantity > 0)
	{
		if (this->next_transfer_transaction_id_ == 0U
			|| this->next_transfer_transaction_id_
				== std::numeric_limits<uint64_t>::max())
			return (FT_ERR_OUT_OF_RANGE);
		transaction_id = this->next_transfer_transaction_id_++;
		result = source_entry.contents.transfer_to(
			destination_entry.contents, 0, transfer_quantity,
			source_inventory_revision, destination_inventory_revision,
			0x57494D45524745ULL, transaction_id, &transfer_result);
		if (result != FT_ERR_FULL)
			break ;
		transfer_quantity /= 2;
	}
	if (result != FT_ERR_SUCCESS)
		return (result);
	this->change_revision_ += 1U;
	source_remaining = source_entry.contents.count_item(source_entry.item_id);
	if (source_remaining < 0)
		return (FT_ERR_INVALID_STATE);
	destination_entry.entity_revision += 1U;
	if (source_remaining == 0)
	{
		this->remove_entity_locked(source_entity_id, source_entry);
	}
	else
		source_entry.entity_revision += 1U;
	*transferred_quantity_out = transfer_result.transferred_quantity;
	return (FT_ERR_SUCCESS);
}

int32_t WorldItemStore::merge_into_nearby(uint64_t source_entity_id,
	uint64_t expected_source_revision, double x, double y, double z,
	uint64_t actor_id, uint64_t server_tick,
	int32_t *transferred_quantity_out) noexcept
{
	std::vector<WorldItemSnapshot> nearby;
	const WorldItemSnapshot *source = nullptr;
	int32_t result;

	if (transferred_quantity_out == nullptr || source_entity_id == 0U
		|| expected_source_revision == 0U || !std::isfinite(x)
		|| !std::isfinite(y) || !std::isfinite(z))
		return (FT_ERR_INVALID_ARGUMENT);
	*transferred_quantity_out = 0;
	result = this->query_nearby(x, y, z, 1.0, actor_id, server_tick, &nearby);
	if (result != FT_ERR_SUCCESS)
		return (result);
	for (const WorldItemSnapshot &entry : nearby)
	{
		if (entry.entity_id == source_entity_id)
		{
			if (entry.entity_revision != expected_source_revision)
				return (FT_ERR_INVALID_STATE);
			source = &entry;
			break ;
		}
	}
	if (source == nullptr)
		return (FT_ERR_NOT_FOUND);
	for (const WorldItemSnapshot &candidate : nearby)
	{
		int32_t transferred = 0;

		if (candidate.entity_id == source_entity_id
			|| candidate.item_id != source->item_id
			|| candidate.max_stack != source->max_stack
			|| candidate.owner_id != source->owner_id
			|| candidate.owner_lock_until_tick
				!= source->owner_lock_until_tick)
			continue ;
		result = this->merge_stacks(source_entity_id, expected_source_revision,
			candidate.entity_id, candidate.entity_revision, &transferred);
		if (result == FT_ERR_SUCCESS && transferred > 0)
		{
			*transferred_quantity_out = transferred;
			return (FT_ERR_SUCCESS);
		}
		if (result == FT_ERR_INVALID_STATE || result == FT_ERR_NOT_FOUND
			|| result == FT_ERR_OUT_OF_RANGE || result == FT_ERR_FULL
			|| result == FT_ERR_INVALID_ARGUMENT)
			continue ;
		return (result);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldItemStore::query_nearby(double x, double y, double z,
	double radius, uint64_t actor_id, uint64_t server_tick,
	std::vector<WorldItemSnapshot> *snapshots_out) const noexcept
{
	return (this->query_internal(x, y, z, radius, actor_id, server_tick,
		true, snapshots_out));
}

int32_t WorldItemStore::query_renderable(double x, double y, double z,
	double radius, std::vector<WorldItemSnapshot> *snapshots_out) const noexcept
{
	return (this->query_internal(x, y, z, radius, 0U, 0U, false,
		snapshots_out));
}

int32_t WorldItemStore::query_internal(double x, double y, double z,
	double radius, uint64_t actor_id, uint64_t server_tick,
	bool respect_owner_lock,
	std::vector<WorldItemSnapshot> *snapshots_out) const noexcept
{
	std::vector<WorldItemSnapshot> staged;
	int32_t center_x;
	int32_t center_z;
	int32_t radius_chunks;
	int32_t error_code;
	std::size_t scanned_count = 0U;
	int64_t min_chunk_x;
	int64_t max_chunk_x;
	int64_t min_chunk_z;
	int64_t max_chunk_z;
	const double radius_squared = radius * radius;

	if (snapshots_out == nullptr || !std::isfinite(x) || !std::isfinite(y)
		|| !std::isfinite(z) || !std::isfinite(radius) || radius <= 0.0
		|| radius > WorldItemStore::MAX_QUERY_RADIUS)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = WorldItemStore::world_coordinate_to_chunk(x, &center_x);
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemStore::world_coordinate_to_chunk(z, &center_z);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	radius_chunks = static_cast<int32_t>(std::ceil(radius
		/ static_cast<double>(GAME_VOXEL_CHUNK_WIDTH)));
	min_chunk_x = static_cast<int64_t>(center_x) - radius_chunks;
	max_chunk_x = static_cast<int64_t>(center_x) + radius_chunks;
	min_chunk_z = static_cast<int64_t>(center_z) - radius_chunks;
	max_chunk_z = static_cast<int64_t>(center_z) + radius_chunks;
	min_chunk_x = std::max(min_chunk_x,
		static_cast<int64_t>(std::numeric_limits<int32_t>::min()));
	max_chunk_x = std::min(max_chunk_x,
		static_cast<int64_t>(std::numeric_limits<int32_t>::max()));
	min_chunk_z = std::max(min_chunk_z,
		static_cast<int64_t>(std::numeric_limits<int32_t>::min()));
	max_chunk_z = std::min(max_chunk_z,
		static_cast<int64_t>(std::numeric_limits<int32_t>::max()));
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	try
	{
		for (int64_t chunk_z = min_chunk_z; chunk_z <= max_chunk_z; ++chunk_z)
		{
			for (int64_t chunk_x = min_chunk_x; chunk_x <= max_chunk_x; ++chunk_x)
			{
				const uint64_t key = WorldItemStore::chunk_key(
					static_cast<int32_t>(chunk_x), static_cast<int32_t>(chunk_z));
				const auto bucket = this->chunk_index_.find(key);

				if (bucket == this->chunk_index_.end())
					continue ;
				for (uint64_t entity_id : bucket->second)
				{
					if (++scanned_count > WorldItemStore::MAX_QUERY_SCAN)
						return (FT_ERR_FULL);
					const auto found = this->entries_.find(entity_id);
					if (found == this->entries_.end())
						continue ;
					const Entry &entry = *found->second;
					if (!entry.published)
						continue ;
					const double dx = entry.x - x;
					const double dy = entry.y - y;
					const double dz = entry.z - z;

					if (dx * dx + dy * dy + dz * dz > radius_squared)
						continue ;
					WorldItemSnapshot snapshot = {};
					std::vector<game_inventory_item_snapshot> item_values;
					uint64_t inventory_revision = 0U;
					if (entry.contents.get_snapshot(item_values,
							&inventory_revision) != FT_ERR_SUCCESS
						|| item_values.size() != 1U)
						return (FT_ERR_INVALID_STATE);
					snapshot.entity_id = entry.entity_id;
					snapshot.entity_revision = entry.entity_revision;
					snapshot.spawned_tick = entry.spawned_tick;
					snapshot.owner_id = entry.owner_id;
					snapshot.owner_lock_until_tick = entry.owner_lock_until_tick;
					snapshot.chunk_x = entry.chunk_x;
					snapshot.chunk_z = entry.chunk_z;
					snapshot.item_id = item_values[0].item_id;
					snapshot.quantity = item_values[0].stack_size;
					snapshot.max_stack = item_values[0].max_stack;
					snapshot.item_instance_id = item_values[0].instance_id;
					snapshot.rarity = item_values[0].rarity;
					snapshot.width = item_values[0].width;
					snapshot.height = item_values[0].height;
					snapshot.modifier1_id = item_values[0].modifier1_id;
					snapshot.modifier1_value = item_values[0].modifier1_value;
					snapshot.modifier2_id = item_values[0].modifier2_id;
					snapshot.modifier2_value = item_values[0].modifier2_value;
					snapshot.modifier3_id = item_values[0].modifier3_id;
					snapshot.modifier3_value = item_values[0].modifier3_value;
					snapshot.modifier4_id = item_values[0].modifier4_id;
					snapshot.modifier4_value = item_values[0].modifier4_value;
					snapshot.x = entry.x;
					snapshot.y = entry.y;
					snapshot.z = entry.z;
					if (snapshot.quantity > 0
						&& (!respect_owner_lock || entry.owner_id == 0U
							|| actor_id == entry.owner_id
							|| server_tick >= entry.owner_lock_until_tick))
					{
						if (staged.size() >= WorldItemStore::MAX_QUERY_RESULTS)
							return (FT_ERR_FULL);
						staged.push_back(snapshot);
					}
				}
			}
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	std::sort(staged.begin(), staged.end(),
		[](const WorldItemSnapshot &left, const WorldItemSnapshot &right)
		{
			return (left.entity_id < right.entity_id);
		});
	snapshots_out->swap(staged);
	return (FT_ERR_SUCCESS);
}

void WorldItemStore::remove_entity_locked(uint64_t entity_id,
	const Entry &entry) noexcept
{
	const uint64_t key = WorldItemStore::chunk_key(entry.chunk_x, entry.chunk_z);
	const auto bucket = this->chunk_index_.find(key);

	if (bucket != this->chunk_index_.end())
	{
		std::vector<uint64_t> &ids = bucket->second;
		ids.erase(std::remove(ids.begin(), ids.end(), entity_id), ids.end());
		if (ids.empty())
			this->chunk_index_.erase(bucket);
	}
	this->entries_.erase(entity_id);
}

int32_t WorldItemStore::serialize(std::vector<uint8_t> &bytes_out) const noexcept
{
	std::vector<uint8_t> staged;
	std::unordered_map<uint64_t, std::unique_ptr<Entry> >::const_iterator entry;
	std::map<std::pair<uint64_t, uint64_t>, CachedPickup>::const_iterator cached;
	std::deque<std::pair<uint64_t, uint64_t> >::const_iterator order;
	std::lock_guard<std::mutex> lock(this->mutex_);

	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	for (const auto &pending_entry : this->entries_)
	{
		if (!pending_entry.second->published)
			return (FT_ERR_INVALID_OPERATION);
	}
	if (this->entries_.size() > WORLD_ITEM_SNAPSHOT_MAX_ENTRIES
		|| this->pickup_cache_.size() > PICKUP_CACHE_LIMIT
		|| this->pickup_cache_.size() != this->pickup_order_.size())
		return (FT_ERR_OUT_OF_RANGE);
	try
	{
		staged.reserve(40U + this->entries_.size() * 160U
			+ this->pickup_cache_.size() * 112U);
		staged.push_back('W');
		staged.push_back('I');
		staged.push_back('S');
		staged.push_back('P');
		world_item_append_u32(staged, WORLD_ITEM_SNAPSHOT_VERSION);
		world_item_append_u64(staged, this->next_entity_id_);
		world_item_append_u64(staged,
			this->next_transfer_transaction_id_);
		world_item_append_u32(staged,
			static_cast<uint32_t>(this->entries_.size()));
		world_item_append_u32(staged,
			static_cast<uint32_t>(this->pickup_cache_.size()));
		for (entry = this->entries_.begin(); entry != this->entries_.end();
			++entry)
		{
			game_inventory_snapshot inventory_snapshot = {};
			const Entry &value = *entry->second;
			uint64_t bits;
			int32_t error_code = value.contents.get_snapshot(inventory_snapshot);

			if (error_code != FT_ERR_SUCCESS
				|| inventory_snapshot.items.size() != 1U)
				return (error_code == FT_ERR_SUCCESS
					? FT_ERR_INVALID_STATE : error_code);
			const game_inventory_item_snapshot &item =
				inventory_snapshot.items[0];
			if (item.item_id != value.item_id || item.max_stack != value.max_stack
				|| item.stack_size <= 0 || item.stack_size > item.max_stack)
				return (FT_ERR_INVALID_STATE);
			world_item_append_u64(staged, value.entity_id);
			world_item_append_u64(staged, value.entity_revision);
			world_item_append_u64(staged, value.spawned_tick);
			world_item_append_u64(staged, value.owner_id);
			world_item_append_u64(staged, value.owner_lock_until_tick);
			world_item_append_u32(staged, static_cast<uint32_t>(value.item_id));
			world_item_append_u32(staged, static_cast<uint32_t>(value.max_stack));
			std::memcpy(&bits, &value.x, sizeof(bits));
			world_item_append_u64(staged, bits);
			std::memcpy(&bits, &value.y, sizeof(bits));
			world_item_append_u64(staged, bits);
			std::memcpy(&bits, &value.z, sizeof(bits));
			world_item_append_u64(staged, bits);
			world_item_append_u32(staged, static_cast<uint32_t>(value.chunk_x));
			world_item_append_u32(staged, static_cast<uint32_t>(value.chunk_z));
			world_item_append_u32(staged, static_cast<uint32_t>(item.slot));
			world_item_append_u64(staged, item.instance_id);
			world_item_append_u32(staged, static_cast<uint32_t>(item.stack_size));
			world_item_append_u32(staged, static_cast<uint32_t>(item.rarity));
			world_item_append_u32(staged, static_cast<uint32_t>(item.width));
			world_item_append_u32(staged, static_cast<uint32_t>(item.height));
			world_item_append_u32(staged, static_cast<uint32_t>(item.modifier1_id));
			world_item_append_u32(staged, static_cast<uint32_t>(item.modifier1_value));
			world_item_append_u32(staged, static_cast<uint32_t>(item.modifier2_id));
			world_item_append_u32(staged, static_cast<uint32_t>(item.modifier2_value));
			world_item_append_u32(staged, static_cast<uint32_t>(item.modifier3_id));
			world_item_append_u32(staged, static_cast<uint32_t>(item.modifier3_value));
			world_item_append_u32(staged, static_cast<uint32_t>(item.modifier4_id));
			world_item_append_u32(staged, static_cast<uint32_t>(item.modifier4_value));
			if (staged.size() > WORLD_ITEM_SNAPSHOT_MAX_BYTES)
				return (FT_ERR_OUT_OF_RANGE);
		}
		for (cached = this->pickup_cache_.begin();
			cached != this->pickup_cache_.end(); ++cached)
		{
			const CachedPickup &value = cached->second;
			world_item_append_u64(staged, cached->first.first);
			world_item_append_u64(staged, cached->first.second);
			world_item_append_u64(staged, value.entity_id);
			world_item_append_u64(staged, value.expected_entity_revision);
			world_item_append_u64(staged, value.expected_inventory_revision);
			world_item_append_u32(staged,
				static_cast<uint32_t>(value.requested_quantity));
			world_item_append_u32(staged,
				static_cast<uint32_t>(value.result_code));
			world_item_append_u64(staged, value.result.entity_id);
			world_item_append_u64(staged, value.result.entity_revision);
			world_item_append_u64(staged, value.result.inventory_revision);
			world_item_append_u32(staged,
				static_cast<uint32_t>(value.result.transferred_quantity));
			world_item_append_u32(staged,
				static_cast<uint32_t>(value.result.remaining_quantity));
			world_item_append_u32(staged,
				value.result.entity_removed ? 1U : 0U);
		}
		for (order = this->pickup_order_.begin();
			order != this->pickup_order_.end(); ++order)
		{
			world_item_append_u64(staged, order->first);
			world_item_append_u64(staged, order->second);
		}
		if (staged.size() + sizeof(uint64_t)
			> WORLD_ITEM_SNAPSHOT_MAX_BYTES)
			return (FT_ERR_OUT_OF_RANGE);
		world_item_append_u64(staged,
			world_item_checksum(staged.data(), staged.size()));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	bytes_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t WorldItemStore::get_max_item_instance_id(
	uint64_t *instance_id_out) const noexcept
{
	uint64_t maximum_instance_id = 0U;
	std::lock_guard<std::mutex> lock(this->mutex_);

	if (!this->initialized_ || instance_id_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	for (const auto &entry : this->entries_)
	{
		game_inventory_snapshot snapshot = {};
		const int32_t error_code = entry.second->contents.get_snapshot(snapshot);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (snapshot.items.size() != 1U)
			return (FT_ERR_INVALID_STATE);
		if (snapshot.items[0].instance_id > maximum_instance_id)
			maximum_instance_id = snapshot.items[0].instance_id;
	}
	*instance_id_out = maximum_instance_id;
	return (FT_ERR_SUCCESS);
}

uint64_t WorldItemStore::change_revision() const noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	return (this->change_revision_);
}

int32_t WorldItemStore::validate_instance_id_uniqueness(
	const game_inventory &inventory) const noexcept
{
	return (this->validate_instance_id_uniqueness(inventory, nullptr));
}

int32_t WorldItemStore::validate_instance_id_uniqueness(
	const game_inventory &inventory,
	const WorldEntityStore *entities) const noexcept
	{
	return (this->validate_instance_id_uniqueness(inventory, entities,
		std::vector<uint64_t>()));
}

int32_t WorldItemStore::validate_instance_id_uniqueness(
	const game_inventory &inventory, const WorldEntityStore *entities,
	const std::vector<uint64_t> &additional_instance_ids) const noexcept
{
	game_inventory_snapshot character_snapshot = {};
	std::unordered_set<uint64_t> instance_ids;
	std::vector<WorldEntitySnapshot> entity_snapshots;
	int32_t error_code = inventory.get_snapshot(character_snapshot);

	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	try
	{
		instance_ids.reserve(character_snapshot.items.size());
		for (const game_inventory_item_snapshot &item
			: character_snapshot.items)
		{
			if (item.instance_id != 0U
				&& !instance_ids.insert(item.instance_id).second)
				return (FT_ERR_ALREADY_EXISTS);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	try
	{
		for (uint64_t instance_id : additional_instance_ids)
		{
			if (instance_id != 0U && !instance_ids.insert(instance_id).second)
				return (FT_ERR_ALREADY_EXISTS);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (entities != nullptr)
	{
		error_code = entities->snapshot(entity_snapshots);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		try
		{
			for (const WorldEntitySnapshot &entity : entity_snapshots)
			{
				if (entity.item_instance_id != 0U
					&& !instance_ids.insert(entity.item_instance_id).second)
					return (FT_ERR_ALREADY_EXISTS);
			}
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	try
	{
		for (const auto &entry : this->entries_)
		{
			game_inventory_snapshot ground_snapshot = {};
			error_code = entry.second->contents.get_snapshot(ground_snapshot);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			if (ground_snapshot.items.size() != 1U)
				return (FT_ERR_INVALID_STATE);
			const uint64_t instance_id = ground_snapshot.items[0].instance_id;
			if (instance_id != 0U
				&& !instance_ids.insert(instance_id).second)
				return (FT_ERR_ALREADY_EXISTS);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldItemStore::validate_item_instance_policy(
	const MinecraftItemCatalog &catalog) const noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_ || !catalog.is_initialized())
		return (FT_ERR_INVALID_STATE);
	for (const auto &entry : this->entries_)
	{
		game_inventory_snapshot snapshot = {};
		const int32_t result = entry.second->contents.get_snapshot(snapshot);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (snapshot.items.size() != 1U
			|| snapshot.items[0].item_id != entry.second->item_id
			|| snapshot.items[0].max_stack != entry.second->max_stack
			|| snapshot.items[0].stack_size <= 0
			|| snapshot.items[0].stack_size > snapshot.items[0].max_stack)
			return (FT_ERR_INVALID_STATE);
		const int32_t policy_result =
			catalog.validate_inventory_instance_policy(snapshot);
		if (policy_result != FT_ERR_SUCCESS)
			return (policy_result);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldItemStore::save_file(const char *file_path) const noexcept
{
	std::vector<uint8_t> bytes;
	int32_t error_code;

	if (file_path == nullptr || file_path[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = this->serialize(bytes);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (filesystem_atomic_write(file_path,
		reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}

int32_t WorldItemStore::load_file(const char *file_path) noexcept
{
	FILE *file;
	long file_size;
	std::vector<uint8_t> bytes;
	int32_t error_code = FT_ERR_SUCCESS;
	int extra_byte;

	if (file_path == nullptr || file_path[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	file = std::fopen(file_path, "rb");
	if (file == nullptr)
		return (FT_ERR_IO);
	if (std::fseek(file, 0, SEEK_END) != 0
		|| (file_size = std::ftell(file)) < 0
		|| file_size > static_cast<long>(WORLD_ITEM_SNAPSHOT_MAX_BYTES)
		|| std::fseek(file, 0, SEEK_SET) != 0)
		error_code = FT_ERR_IO;
	if (error_code == FT_ERR_SUCCESS && file_size < 40L)
		error_code = FT_ERR_INVALID_ARGUMENT;
	if (error_code == FT_ERR_SUCCESS)
	{
		try
		{
			bytes.resize(static_cast<std::size_t>(file_size));
		}
		catch (const std::bad_alloc &)
		{
			error_code = FT_ERR_NO_MEMORY;
		}
	}
	if (error_code == FT_ERR_SUCCESS
		&& std::fread(bytes.data(), 1U, bytes.size(), file) != bytes.size())
		error_code = FT_ERR_IO;
	if (error_code == FT_ERR_SUCCESS)
	{
		extra_byte = std::fgetc(file);
		if (extra_byte != EOF || std::ferror(file))
			error_code = FT_ERR_IO;
	}
	if (std::fclose(file) != 0 && error_code == FT_ERR_SUCCESS)
		error_code = FT_ERR_IO;
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (this->deserialize(bytes.data(), bytes.size()));
}

int32_t WorldItemStore::deserialize(const uint8_t *bytes,
	std::size_t byte_count) noexcept
{
	WorldItemSnapshotReader reader = {};
	std::unordered_map<uint64_t, std::unique_ptr<Entry> > staged_entries;
	std::unordered_map<uint64_t, std::vector<uint64_t> > staged_index;
	std::map<std::pair<uint64_t, uint64_t>, CachedPickup> staged_cache;
	std::deque<std::pair<uint64_t, uint64_t> > staged_order;
	std::set<std::pair<uint64_t, uint64_t> > ordered_keys;
	std::unordered_set<uint64_t> staged_instance_ids;
	uint32_t version;
	uint32_t entry_count;
	uint32_t cache_count;
	uint64_t staged_next_entity_id;
	uint64_t staged_next_transfer_id;
	uint64_t maximum_entity_id = 0U;
	uint64_t expected_checksum = 0U;
	int32_t error_code = FT_ERR_INVALID_ARGUMENT;

	if (bytes == nullptr || byte_count < 32U
		|| byte_count > WORLD_ITEM_SNAPSHOT_MAX_BYTES)
		return (FT_ERR_INVALID_ARGUMENT);
	if (bytes[0] != 'W' || bytes[1] != 'I' || bytes[2] != 'S'
		|| bytes[3] != 'P')
		return (FT_ERR_INVALID_ARGUMENT);
	for (uint32_t index = 0U; index < 8U; ++index)
		expected_checksum |= static_cast<uint64_t>(
			bytes[byte_count - 8U + index]) << (index * 8U);
	if (world_item_checksum(bytes, byte_count - 8U) != expected_checksum)
		return (FT_ERR_INVALID_ARGUMENT);
	reader.bytes = bytes;
	reader.size = byte_count - 8U;
	reader.offset = 4U;
	if (!reader.read_u32(&version) || version != WORLD_ITEM_SNAPSHOT_VERSION
		|| !reader.read_u64(&staged_next_entity_id)
		|| !reader.read_u64(&staged_next_transfer_id)
		|| !reader.read_u32(&entry_count)
		|| !reader.read_u32(&cache_count)
		|| entry_count > WORLD_ITEM_SNAPSHOT_MAX_ENTRIES
		|| cache_count > PICKUP_CACHE_LIMIT
		|| staged_next_entity_id == 0U || staged_next_transfer_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		staged_entries.reserve(entry_count);
		staged_index.reserve(entry_count);
		staged_instance_ids.reserve(entry_count);
		for (uint32_t index = 0U; index < entry_count; ++index)
		{
			uint64_t entity_id;
			uint64_t entity_revision;
			uint64_t spawned_tick;
			uint64_t owner_id;
			uint64_t owner_lock_until_tick;
			uint64_t x_bits;
			uint64_t y_bits;
			uint64_t z_bits;
			uint64_t instance_id;
			uint32_t item_id;
			uint32_t max_stack;
			uint32_t chunk_x_bits;
			uint32_t chunk_z_bits;
			uint32_t slot;
			uint32_t quantity;
			uint32_t rarity;
			uint32_t width;
			uint32_t height;
			uint32_t modifiers[8];
			int32_t chunk_x;
			int32_t chunk_z;
			double x;
			double y;
			double z;
			int32_t computed_chunk_x;
			int32_t computed_chunk_z;
			ft_sharedptr<game_item> item;
			std::unique_ptr<Entry> entry(new (std::nothrow) Entry());

			if (!entry)
				return (FT_ERR_NO_MEMORY);
			if (!reader.read_u64(&entity_id)
				|| !reader.read_u64(&entity_revision)
				|| !reader.read_u64(&spawned_tick)
				|| !reader.read_u64(&owner_id)
				|| !reader.read_u64(&owner_lock_until_tick)
				|| !reader.read_u32(&item_id)
				|| !reader.read_u32(&max_stack)
				|| !reader.read_u64(&x_bits) || !reader.read_u64(&y_bits)
				|| !reader.read_u64(&z_bits)
				|| !reader.read_u32(&chunk_x_bits)
				|| !reader.read_u32(&chunk_z_bits) || !reader.read_u32(&slot)
				|| !reader.read_u64(&instance_id)
				|| !reader.read_u32(&quantity) || !reader.read_u32(&rarity)
				|| !reader.read_u32(&width) || !reader.read_u32(&height))
				return (FT_ERR_INVALID_ARGUMENT);
			for (uint32_t modifier = 0U; modifier < 8U; ++modifier)
				if (!reader.read_u32(&modifiers[modifier]))
					return (FT_ERR_INVALID_ARGUMENT);
			chunk_x = static_cast<int32_t>(chunk_x_bits);
			chunk_z = static_cast<int32_t>(chunk_z_bits);
			std::memcpy(&x, &x_bits, sizeof(x));
			std::memcpy(&y, &y_bits, sizeof(y));
			std::memcpy(&z, &z_bits, sizeof(z));
			if (entity_id == 0U || entity_revision == 0U || item_id == 0U
				|| item_id > static_cast<uint32_t>(FT_INT32_MAX)
				|| max_stack == 0U
				|| max_stack > static_cast<uint32_t>(FT_INT32_MAX)
				|| quantity == 0U || quantity > max_stack || slot != 0U
				|| width == 0U || width > static_cast<uint32_t>(FT_INT32_MAX)
				|| height == 0U || height > static_cast<uint32_t>(FT_INT32_MAX)
				|| rarity > static_cast<uint32_t>(FT_INT32_MAX)
				|| !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
				|| WorldItemStore::world_coordinate_to_chunk(x,
					&computed_chunk_x) != FT_ERR_SUCCESS
				|| WorldItemStore::world_coordinate_to_chunk(z,
					&computed_chunk_z) != FT_ERR_SUCCESS
				|| computed_chunk_x != chunk_x || computed_chunk_z != chunk_z)
				return (FT_ERR_INVALID_ARGUMENT);
			if (instance_id != 0U
				&& !staged_instance_ids.insert(instance_id).second)
				return (FT_ERR_INVALID_ARGUMENT);
			error_code = item.initialize(new (std::nothrow) game_item());
			if (error_code == FT_ERR_SUCCESS)
				error_code = item->initialize();
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			item->set_item_id(static_cast<int32_t>(item_id));
			item->set_instance_id(instance_id);
			item->set_stack_size(static_cast<int32_t>(quantity));
			item->set_max_stack(static_cast<int32_t>(max_stack));
			item->set_rarity(static_cast<int32_t>(rarity));
			item->set_width(static_cast<int32_t>(width));
			item->set_height(static_cast<int32_t>(height));
			item->set_modifier1_id(static_cast<int32_t>(modifiers[0]));
			item->set_modifier1_value(static_cast<int32_t>(modifiers[1]));
			item->set_modifier2_id(static_cast<int32_t>(modifiers[2]));
			item->set_modifier2_value(static_cast<int32_t>(modifiers[3]));
			item->set_modifier3_id(static_cast<int32_t>(modifiers[4]));
			item->set_modifier3_value(static_cast<int32_t>(modifiers[5]));
			item->set_modifier4_id(static_cast<int32_t>(modifiers[6]));
			item->set_modifier4_value(static_cast<int32_t>(modifiers[7]));
			error_code = entry->contents.initialize(1U,
				std::numeric_limits<int32_t>::max());
			if (error_code == FT_ERR_SUCCESS)
				error_code = entry->contents.enable_thread_safety();
			if (error_code == FT_ERR_SUCCESS)
				error_code = entry->contents.add_item(item);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			entry->entity_id = entity_id;
			entry->entity_revision = entity_revision;
			entry->spawned_tick = spawned_tick;
			entry->owner_id = owner_id;
			entry->owner_lock_until_tick = owner_lock_until_tick;
			entry->chunk_x = chunk_x;
			entry->chunk_z = chunk_z;
			entry->item_id = static_cast<int32_t>(item_id);
			entry->max_stack = static_cast<int32_t>(max_stack);
			entry->x = x;
			entry->y = y;
			entry->z = z;
			if (!staged_entries.emplace(entity_id, std::move(entry)).second)
				return (FT_ERR_INVALID_ARGUMENT);
			staged_index[WorldItemStore::chunk_key(chunk_x, chunk_z)]
				.push_back(entity_id);
			maximum_entity_id = std::max(maximum_entity_id, entity_id);
		}
		for (uint32_t index = 0U; index < cache_count; ++index)
		{
			std::pair<uint64_t, uint64_t> key;
			CachedPickup cached = {};
			uint32_t requested_quantity;
			uint32_t result_code;
			uint32_t transferred_quantity;
			uint32_t remaining_quantity;
			uint32_t entity_removed;

			if (!reader.read_u64(&key.first) || !reader.read_u64(&key.second)
				|| !reader.read_u64(&cached.entity_id)
				|| !reader.read_u64(&cached.expected_entity_revision)
				|| !reader.read_u64(&cached.expected_inventory_revision)
				|| !reader.read_u32(&requested_quantity)
				|| !reader.read_u32(&result_code)
				|| !reader.read_u64(&cached.result.entity_id)
				|| !reader.read_u64(&cached.result.entity_revision)
				|| !reader.read_u64(&cached.result.inventory_revision)
				|| !reader.read_u32(&transferred_quantity)
				|| !reader.read_u32(&remaining_quantity)
				|| !reader.read_u32(&entity_removed))
				return (FT_ERR_INVALID_ARGUMENT);
			if (key.first == 0U || key.second == 0U || cached.entity_id == 0U
				|| cached.expected_entity_revision == 0U
				|| requested_quantity == 0U || requested_quantity > FT_INT32_MAX
				|| transferred_quantity > FT_INT32_MAX
				|| remaining_quantity > FT_INT32_MAX || entity_removed > 1U)
				return (FT_ERR_INVALID_ARGUMENT);
			cached.requested_quantity = static_cast<int32_t>(requested_quantity);
			cached.result_code = static_cast<int32_t>(result_code);
			cached.result.transferred_quantity =
				static_cast<int32_t>(transferred_quantity);
			cached.result.remaining_quantity =
				static_cast<int32_t>(remaining_quantity);
			cached.result.entity_removed = entity_removed != 0U;
			if (cached.result_code == FT_ERR_SUCCESS)
			{
				if (cached.result.entity_id != cached.entity_id
					|| cached.result.entity_revision
						!= cached.expected_entity_revision + 1U
					|| cached.expected_entity_revision
						== std::numeric_limits<uint64_t>::max()
					|| cached.expected_inventory_revision
						== std::numeric_limits<uint64_t>::max()
					|| cached.result.inventory_revision
						!= cached.expected_inventory_revision + 1U
					|| cached.result.transferred_quantity <= 0
					|| cached.result.transferred_quantity
						> cached.requested_quantity
					|| cached.result.entity_removed
						!= (cached.result.remaining_quantity == 0))
					return (FT_ERR_INVALID_ARGUMENT);
			}
			else if (cached.result_code == FT_ERR_SUCCESS
				|| cached.result.entity_id != 0U
				|| cached.result.entity_revision != 0U
				|| cached.result.inventory_revision != 0U
				|| cached.result.transferred_quantity != 0
				|| cached.result.remaining_quantity != 0
			|| cached.result.entity_removed)
				return (FT_ERR_INVALID_ARGUMENT);
			if (!staged_cache.emplace(key, cached).second)
				return (FT_ERR_INVALID_ARGUMENT);
		}
		for (uint32_t index = 0U; index < cache_count; ++index)
		{
			std::pair<uint64_t, uint64_t> key;

			if (!reader.read_u64(&key.first) || !reader.read_u64(&key.second)
				|| staged_cache.find(key) == staged_cache.end()
				|| !ordered_keys.insert(key).second)
				return (FT_ERR_INVALID_ARGUMENT);
			staged_order.push_back(key);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (reader.offset != reader.size || staged_order.size() != staged_cache.size()
		|| staged_next_entity_id <= maximum_entity_id)
		return (FT_ERR_INVALID_ARGUMENT);
	{
		std::lock_guard<std::mutex> lock(this->mutex_);
		if (!this->initialized_)
			return (FT_ERR_INVALID_OPERATION);
		if (this->change_revision_ == std::numeric_limits<uint64_t>::max())
			return (FT_ERR_OUT_OF_RANGE);
		this->entries_.swap(staged_entries);
		this->chunk_index_.swap(staged_index);
		this->pickup_cache_.swap(staged_cache);
		this->pickup_order_.swap(staged_order);
		this->next_entity_id_ = staged_next_entity_id;
		this->next_transfer_transaction_id_ = staged_next_transfer_id;
		this->change_revision_ += 1U;
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldItemStore::pickup(uint64_t entity_id,
	uint64_t expected_entity_revision, uint64_t actor_id, uint64_t request_id,
	uint64_t expected_inventory_revision, int32_t requested_quantity,
	double actor_x, double actor_y, double actor_z, uint64_t server_tick,
	game_inventory &destination,
	WorldItemPickupResult *result_out) noexcept
{
	const std::pair<uint64_t, uint64_t> cache_key(actor_id, request_id);
	std::unique_lock<std::mutex> lock(this->mutex_);
	std::unordered_map<uint64_t, std::unique_ptr<Entry> >::iterator found;
	std::map<std::pair<uint64_t, uint64_t>, CachedPickup>::iterator cached;
	uint64_t source_revision;
	uint64_t transfer_transaction_id;
	game_inventory_transfer_result transfer_result = {};
	int32_t available;
	int32_t transfer_quantity;
	int32_t error_code;
	bool cache_inserted = false;

	if (result_out == nullptr || entity_id == 0U || actor_id == 0U
		|| request_id == 0U || requested_quantity <= 0
		|| !std::isfinite(actor_x) || !std::isfinite(actor_y)
		|| !std::isfinite(actor_z))
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	*result_out = {};
	cached = this->pickup_cache_.find(cache_key);
	if (cached != this->pickup_cache_.end())
	{
		if (!WorldItemStore::pickup_matches(cached->second, entity_id,
				expected_entity_revision, expected_inventory_revision,
				requested_quantity))
			return (FT_ERR_INVALID_ARGUMENT);
		*result_out = cached->second.result;
		return (cached->second.result_code);
	}
	found = this->entries_.find(entity_id);
	if (found == this->entries_.end())
		return (FT_ERR_NOT_FOUND);
	Entry &entry = *found->second;
	if (!entry.published)
		return (FT_ERR_NOT_FOUND);
	if (entry.entity_revision != expected_entity_revision)
		return (FT_ERR_INVALID_STATE);
	if (entry.owner_id != 0U && actor_id != entry.owner_id
		&& server_tick < entry.owner_lock_until_tick)
		return (FT_ERR_PERMISSION_DENIED);
	{
		const double dx = entry.x - actor_x;
		const double dy = entry.y - actor_y;
		const double dz = entry.z - actor_z;

		if (dx * dx + dy * dy + dz * dz
			> WorldItemStore::MAX_PICKUP_REACH
				* WorldItemStore::MAX_PICKUP_REACH)
			return (FT_ERR_OUT_OF_RANGE);
	}
	if (entry.entity_revision == std::numeric_limits<uint64_t>::max()
		|| expected_inventory_revision == std::numeric_limits<uint64_t>::max()
		|| this->change_revision_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	available = entry.contents.count_item(entry.item_id);
	if (available <= 0 || requested_quantity > available)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = entry.contents.get_revision(&source_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	while (this->pickup_cache_.size() >= WorldItemStore::PICKUP_CACHE_LIMIT)
	{
		if (this->pickup_order_.empty())
			return (FT_ERR_INVALID_STATE);
		this->pickup_cache_.erase(this->pickup_order_.front());
		this->pickup_order_.pop_front();
	}
	try
	{
		this->pickup_order_.push_back(cache_key);
		CachedPickup new_cache = {};
		new_cache.entity_id = entity_id;
		new_cache.expected_entity_revision = expected_entity_revision;
		new_cache.expected_inventory_revision = expected_inventory_revision;
		new_cache.requested_quantity = requested_quantity;
		new_cache.result_code = FT_ERR_INVALID_STATE;
		this->pickup_cache_.emplace(cache_key, new_cache);
		cache_inserted = true;
	}
	catch (const std::bad_alloc &)
	{
		if (!this->pickup_order_.empty()
			&& this->pickup_order_.back() == cache_key)
			this->pickup_order_.pop_back();
		this->pickup_cache_.erase(cache_key);
		return (FT_ERR_NO_MEMORY);
	}
	transfer_quantity = requested_quantity;
	while (transfer_quantity > 0)
	{
		if (this->next_transfer_transaction_id_ == 0U
			|| this->next_transfer_transaction_id_
				== std::numeric_limits<uint64_t>::max())
		{
			error_code = FT_ERR_OUT_OF_RANGE;
			break ;
		}
		transfer_transaction_id = this->next_transfer_transaction_id_++;
		error_code = entry.contents.transfer_to(destination, 0,
			transfer_quantity, source_revision, expected_inventory_revision,
			actor_id, transfer_transaction_id, &transfer_result);
		if (error_code != FT_ERR_FULL)
			break ;
		transfer_quantity /= 2;
	}
	if (error_code != FT_ERR_SUCCESS)
	{
		if (cache_inserted)
		{
			cached = this->pickup_cache_.find(cache_key);
			if (cached != this->pickup_cache_.end())
			{
				cached->second.result_code = error_code;
				cached->second.result = {};
			}
		}
		return (error_code);
	}
	WorldItemPickupResult result = {};
	result.entity_id = entity_id;
	result.entity_revision = entry.entity_revision + 1U;
	result.inventory_revision = expected_inventory_revision + 1U;
	result.transferred_quantity = transfer_quantity;
	result.remaining_quantity = available - transfer_quantity;
	result.entity_removed = result.remaining_quantity == 0;
	entry.entity_revision = result.entity_revision;
	this->change_revision_ += 1U;
	if (result.entity_removed)
		this->remove_entity_locked(entity_id, entry);
	cached = this->pickup_cache_.find(cache_key);
	if (cached != this->pickup_cache_.end())
	{
		cached->second.result_code = FT_ERR_SUCCESS;
		cached->second.result = result;
	}
	*result_out = result;
	return (FT_ERR_SUCCESS);
}
