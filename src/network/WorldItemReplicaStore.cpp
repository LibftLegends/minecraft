#include "../../src/network/WorldItemReplicaStore.hpp"
#include <new>

WorldItemReplicaStore::WorldItemReplicaStore()
	: mutex_(), items_(), world_id_(0U), initialized_(false)
{
}

int32_t WorldItemReplicaStore::initialize(uint64_t world_id) noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (this->initialized_)
		return (FT_ERR_ALREADY_INITIALISED);
	if (world_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	this->items_.clear();
	this->world_id_ = world_id;
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldItemReplicaStore::replace_chunk(
	const ProtocolWorldItemSnapshotMessage &snapshot) noexcept
{
	std::map<uint64_t, WorldItemSnapshot> staged;

	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (snapshot.world_id != this->world_id_ || snapshot.session_id == 0U
		|| snapshot.items.size() > 256U)
		return (FT_ERR_PERMISSION_DENIED);
	try
	{
		staged = this->items_;
		for (std::map<uint64_t, WorldItemSnapshot>::iterator it = staged.begin();
			it != staged.end(); )
		{
			if (it->second.chunk_x == snapshot.chunk_x
				&& it->second.chunk_z == snapshot.chunk_z)
				it = staged.erase(it);
			else
				++it;
		}
		for (std::size_t index = 0U; index < snapshot.items.size(); ++index)
		{
			const WorldItemSnapshot &item = snapshot.items[index];
			if (item.chunk_x != snapshot.chunk_x || item.chunk_z != snapshot.chunk_z
				|| item.entity_id == 0U || item.entity_revision == 0U
				|| item.item_id <= 0 || item.quantity <= 0
				|| item.max_stack < item.quantity
				|| (item.item_instance_id != 0U && item.quantity != 1)
				|| !staged.emplace(item.entity_id, item).second)
				return (FT_ERR_INVALID_ARGUMENT);
		}
		if (staged.size() > WorldItemReplicaStore::MAX_ITEMS)
			return (FT_ERR_FULL);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	this->items_.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t WorldItemReplicaStore::query_chunk(int32_t chunk_x, int32_t chunk_z,
	std::vector<WorldItemSnapshot> *items_out) const noexcept
{
	std::vector<WorldItemSnapshot> staged;
	if (items_out == nullptr)
		return (FT_ERR_INVALID_POINTER);
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	try
	{
		for (std::map<uint64_t, WorldItemSnapshot>::const_iterator it =
			this->items_.begin(); it != this->items_.end(); ++it)
		{
			if (it->second.chunk_x == chunk_x && it->second.chunk_z == chunk_z)
				staged.push_back(it->second);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	items_out->swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t WorldItemReplicaStore::destroy() noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	this->items_.clear();
	this->world_id_ = 0U;
	this->initialized_ = false;
	return (FT_ERR_SUCCESS);
}
