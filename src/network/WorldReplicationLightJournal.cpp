#include "../../src/network/WorldReplicationLightJournal.hpp"

WorldReplicationLightJournal::WorldReplicationLightJournal()
	: entries_(), maximum_entries_(0U), initialized_(false)
{
}

WorldReplicationLightJournal::WorldReplicationLightJournal(
	const WorldReplicationLightJournal &other)
	: entries_(), maximum_entries_(0U), initialized_(false)
{
	(void)other;
}

WorldReplicationLightJournal::~WorldReplicationLightJournal()
{
	if (this->destroy() != FT_ERR_SUCCESS)
		return ;
}

WorldReplicationLightJournal &WorldReplicationLightJournal::operator=(
	const WorldReplicationLightJournal &other)
{
	(void)other;
	return (*this);
}

int32_t WorldReplicationLightJournal::initialize(uint32_t maximum_entries)
{
	int32_t error_code;

	if (this->initialized_)
		return (FT_ERR_ALREADY_INITIALISED);
	if (maximum_entries == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = this->entries_.initialize();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->maximum_entries_ = maximum_entries;
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationLightJournal::latest_revision(int32_t chunk_x,
	int32_t chunk_z, uint64_t &revision) const
{
	ft_size_t index;
	bool found;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	index = 0U;
	found = false;
	revision = 0U;
	while (index < this->entries_.size())
	{
		const ProtocolChunkLightDeltaMessage &entry = this->entries_[index];
		if (entry.chunk_x == chunk_x && entry.chunk_z == chunk_z
			&& (!found || entry.final_light_revision > revision))
		{
			revision = entry.final_light_revision;
			found = true;
		}
		index += 1U;
	}
	if (!found)
		return (FT_ERR_NOT_FOUND);
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationLightJournal::append(
	const ProtocolChunkLightDeltaMessage &delta)
{
	uint64_t latest;
	int32_t error_code;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (delta.world_id == 0U || delta.base_light_revision == 0U
		|| delta.final_light_revision <= delta.base_light_revision
		|| delta.source_block_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = this->latest_revision(delta.chunk_x, delta.chunk_z, latest);
	if (error_code == FT_ERR_SUCCESS
		&& delta.base_light_revision != latest)
		return (FT_ERR_INVALID_STATE);
	if (error_code != FT_ERR_SUCCESS && error_code != FT_ERR_NOT_FOUND)
		return (error_code);
	if (this->entries_.size() >= this->maximum_entries_)
		this->entries_.erase(this->entries_.begin());
	return (this->entries_.push_back(delta));
}

int32_t WorldReplicationLightJournal::next_delta(int32_t chunk_x,
	int32_t chunk_z, uint64_t base_revision,
	ProtocolChunkLightDeltaMessage &delta) const
{
	ft_size_t index;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (base_revision == UINT64_MAX)
		return (FT_ERR_OUT_OF_RANGE);
	index = 0U;
	while (index < this->entries_.size())
	{
		const ProtocolChunkLightDeltaMessage &entry = this->entries_[index];
		if (entry.chunk_x == chunk_x && entry.chunk_z == chunk_z
			&& entry.base_light_revision == base_revision)
		{
			delta = entry;
			return (FT_ERR_SUCCESS);
		}
		index += 1U;
	}
	return (FT_ERR_NOT_FOUND);
}

ft_bool WorldReplicationLightJournal::needs_snapshot(int32_t chunk_x,
	int32_t chunk_z, uint64_t base_revision) const
{
	uint64_t latest;
	uint64_t expected_revision;

	if (!this->initialized_
		|| this->latest_revision(chunk_x, chunk_z, latest) != FT_ERR_SUCCESS
		|| base_revision >= latest)
		return (FT_FALSE);
	if (base_revision == UINT64_MAX)
		return (FT_TRUE);
	expected_revision = base_revision;
	while (expected_revision < latest)
	{
		ProtocolChunkLightDeltaMessage delta;
		if (this->next_delta(chunk_x, chunk_z, expected_revision, delta)
			!= FT_ERR_SUCCESS)
			return (FT_TRUE);
		expected_revision = delta.final_light_revision;
	}
	return (FT_FALSE);
}

int32_t WorldReplicationLightJournal::latest_chunk_revision(
	int32_t chunk_x, int32_t chunk_z, uint64_t &revision) const
{
	return (this->latest_revision(chunk_x, chunk_z, revision));
}

int32_t WorldReplicationLightJournal::prune_through(int32_t chunk_x,
	int32_t chunk_z, uint64_t revision)
{
	ft_size_t index;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	index = 0U;
	while (index < this->entries_.size())
	{
		if (this->entries_[index].chunk_x == chunk_x
			&& this->entries_[index].chunk_z == chunk_z
			&& this->entries_[index].final_light_revision <= revision)
		{
			this->entries_.erase(this->entries_.begin() + index);
			continue ;
		}
		index += 1U;
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationLightJournal::destroy()
{
	int32_t error_code;

	if (!this->initialized_)
		return (FT_ERR_SUCCESS);
	error_code = this->entries_.destroy();
	this->maximum_entries_ = 0U;
	this->initialized_ = false;
	return (error_code);
}
