#include "../../src/network/WorldReplicationReplicaStore.hpp"
#include <new>

WorldReplicationReplicaStore::WorldReplicationReplicaStore()
	: replicas_(), maximum_replicas_(0U), initialized_(false)
{
}

WorldReplicationReplicaStore::WorldReplicationReplicaStore(
	const WorldReplicationReplicaStore &other)
	: replicas_(), maximum_replicas_(0U), initialized_(false)
{
	(void)other;
}

WorldReplicationReplicaStore::~WorldReplicationReplicaStore()
{
	if (this->destroy() != FT_ERR_SUCCESS)
		return ;
}

WorldReplicationReplicaStore &WorldReplicationReplicaStore::operator=(
	const WorldReplicationReplicaStore &other)
{
	(void)other;
	return (*this);
}

int32_t WorldReplicationReplicaStore::initialize(uint32_t maximum_replicas)
{
	int32_t error_code;

	if (this->initialized_)
		return (FT_ERR_ALREADY_INITIALISED);
	if (maximum_replicas == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = this->replicas_.initialize();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->replicas_.reserve(maximum_replicas);
	error_code = this->replicas_.get_error();
	if (error_code != FT_ERR_SUCCESS)
	{
		(void)this->replicas_.destroy();
		return (error_code);
	}
	this->maximum_replicas_ = maximum_replicas;
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

WorldReplicationBlockReplica *WorldReplicationReplicaStore::find(
	int32_t chunk_x, int32_t chunk_z) noexcept
{
	uint32_t index;

	index = 0U;
	while (index < this->replicas_.size())
	{
		if (this->replicas_[index] != ft_nullptr
			&& this->replicas_[index]->chunk_x() == chunk_x
			&& this->replicas_[index]->chunk_z() == chunk_z)
			return (this->replicas_[index]);
		index += 1U;
	}
	return (ft_nullptr);
}

const WorldReplicationBlockReplica *WorldReplicationReplicaStore::find(
	int32_t chunk_x, int32_t chunk_z) const noexcept
{
	uint32_t index;

	index = 0U;
	while (index < this->replicas_.size())
	{
		if (this->replicas_[index] != ft_nullptr
			&& this->replicas_[index]->chunk_x() == chunk_x
			&& this->replicas_[index]->chunk_z() == chunk_z)
			return (this->replicas_[index]);
		index += 1U;
	}
	return (ft_nullptr);
}

int32_t WorldReplicationReplicaStore::add_snapshot_replica(
	const ProtocolChunkSnapshotMessage &snapshot)
{
	WorldReplicationBlockReplica *replica;
	int32_t error_code;

	if (this->replicas_.size() >= this->maximum_replicas_)
		return (FT_ERR_FULL);
	replica = new (std::nothrow) WorldReplicationBlockReplica();
	if (replica == ft_nullptr)
		return (FT_ERR_NO_MEMORY);
	error_code = replica->initialize(snapshot.session_id, snapshot.world_id,
		snapshot.chunk_x, snapshot.chunk_z);
	if (error_code == FT_ERR_SUCCESS)
		error_code = replica->apply_snapshot(snapshot);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->replicas_.push_back(replica);
	if (error_code != FT_ERR_SUCCESS)
	{
		int32_t destroy_error = replica->destroy();
		delete replica;
		if (error_code == FT_ERR_SUCCESS)
			error_code = destroy_error;
	}
	return (error_code);
}

int32_t WorldReplicationReplicaStore::apply_snapshot(
	const ProtocolChunkSnapshotMessage &snapshot)
{
	WorldReplicationBlockReplica *replica;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	replica = this->find(snapshot.chunk_x, snapshot.chunk_z);
	if (replica != ft_nullptr)
		return (replica->apply_snapshot(snapshot));
	return (this->add_snapshot_replica(snapshot));
}

int32_t WorldReplicationReplicaStore::apply_block_delta(
	const ProtocolChunkBlockDeltaMessage &delta)
{
	WorldReplicationBlockReplica *replica;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	replica = this->find(delta.delta.chunk_x, delta.delta.chunk_z);
	if (replica == ft_nullptr)
		return (FT_ERR_NOT_FOUND);
	return (replica->apply_block_delta(delta));
}

int32_t WorldReplicationReplicaStore::apply_light_delta(
	const ProtocolChunkLightDeltaMessage &delta)
{
	WorldReplicationBlockReplica *replica;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	replica = this->find(delta.chunk_x, delta.chunk_z);
	if (replica == ft_nullptr)
		return (FT_ERR_NOT_FOUND);
	return (replica->apply_light_delta(delta));
}

int32_t WorldReplicationReplicaStore::apply_canonical_repair(
	const ProtocolChunkRepairResponseMessage &response)
{
	WorldReplicationBlockReplica *replica;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	replica = this->find(response.chunk_x, response.chunk_z);
	if (replica == ft_nullptr)
		return (FT_ERR_NOT_FOUND);
	return (replica->apply_canonical_repair(response));
}

uint32_t WorldReplicationReplicaStore::size() const noexcept
{
	return (static_cast<uint32_t>(this->replicas_.size()));
}

void WorldReplicationReplicaStore::clear_replicas() noexcept
{
	uint32_t index;

	index = 0U;
	while (index < this->replicas_.size())
	{
		if (this->replicas_[index] != ft_nullptr)
		{
			(void)this->replicas_[index]->destroy();
			delete this->replicas_[index];
		}
		index += 1U;
	}
	this->replicas_.clear();
}

int32_t WorldReplicationReplicaStore::destroy()
{
	int32_t error_code;

	if (!this->initialized_
		&& this->replicas_.is_initialised() == FT_FALSE)
		return (FT_ERR_SUCCESS);
	this->clear_replicas();
	error_code = this->replicas_.destroy();
	this->maximum_replicas_ = 0U;
	this->initialized_ = false;
	return (error_code);
}
