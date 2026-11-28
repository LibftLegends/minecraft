#include "../../src/world/WorldChunkInstabilityStore.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include "../../Libft/Modules/File/file_utils.hpp"

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

static const uint32_t WORLD_CHUNK_INSTABILITY_MAGIC = 0x57434931U;
static const uint32_t WORLD_CHUNK_INSTABILITY_VERSION = 1U;
static const uint64_t WORLD_CHUNK_INSTABILITY_FNV_OFFSET =
	14695981039346656037ULL;
static const uint64_t WORLD_CHUNK_INSTABILITY_FNV_PRIME =
	1099511628211ULL;

static void world_instability_append_u32(std::vector<uint8_t> &bytes,
	uint32_t value)
{
	for (uint32_t shift = 0U; shift < 32U; shift += 8U)
		bytes.push_back(static_cast<uint8_t>(value >> shift));
}

static void world_instability_append_u64(std::vector<uint8_t> &bytes,
	uint64_t value)
{
	for (uint32_t shift = 0U; shift < 64U; shift += 8U)
		bytes.push_back(static_cast<uint8_t>(value >> shift));
}

static uint64_t world_instability_hash(const uint8_t *bytes,
	std::size_t size) noexcept
{
	uint64_t hash = WORLD_CHUNK_INSTABILITY_FNV_OFFSET;
	for (std::size_t index = 0U; index < size; ++index)
	{
		hash ^= bytes[index];
		hash *= WORLD_CHUNK_INSTABILITY_FNV_PRIME;
	}
	return (hash);
}

static bool world_instability_read_u32(const uint8_t *bytes,
	std::size_t size, std::size_t *offset, uint32_t *value) noexcept
{
	if (bytes == nullptr || offset == nullptr || value == nullptr
		|| *offset > size || size - *offset < 4U)
		return (false);
	*value = static_cast<uint32_t>(bytes[*offset])
		| (static_cast<uint32_t>(bytes[*offset + 1U]) << 8U)
		| (static_cast<uint32_t>(bytes[*offset + 2U]) << 16U)
		| (static_cast<uint32_t>(bytes[*offset + 3U]) << 24U);
	*offset += 4U;
	return (true);
}

static bool world_instability_read_u64(const uint8_t *bytes,
	std::size_t size, std::size_t *offset, uint64_t *value) noexcept
{
	uint32_t low;
	uint32_t high;
	if (!world_instability_read_u32(bytes, size, offset, &low)
		|| !world_instability_read_u32(bytes, size, offset, &high))
		return (false);
	*value = static_cast<uint64_t>(low)
		| (static_cast<uint64_t>(high) << 32U);
	return (true);
}

WorldChunkInstabilityStore::WorldChunkInstabilityStore()
	: mutex_(), policy_(), configured_(false), records_()
{
}

void WorldChunkInstabilityStore::clear_records() noexcept
{
	std::lock_guard<std::mutex> guard(this->mutex_);
	this->records_.clear();
}

bool WorldChunkInstabilityStore::policy_is_valid(
	const WorldChunkInstabilityPolicy &policy) noexcept
{
	if (policy.maximum_value == 0U
		|| policy.default_initial_value > policy.maximum_value
		|| policy.reward_threshold_count > 8U
		|| policy.danger_threshold_count > 8U)
		return (false);
	for (uint32_t index = 0U; index < policy.reward_threshold_count; ++index)
	{
		if (policy.reward_thresholds[index] == 0U
			|| policy.reward_thresholds[index] > policy.maximum_value
			|| (index > 0U && policy.reward_thresholds[index]
				<= policy.reward_thresholds[index - 1U]))
			return (false);
	}
	for (uint32_t index = 0U; index < policy.danger_threshold_count; ++index)
	{
		if (policy.danger_thresholds[index] == 0U
			|| policy.danger_thresholds[index] > policy.maximum_value
			|| (index > 0U && policy.danger_thresholds[index]
				<= policy.danger_thresholds[index - 1U]))
			return (false);
	}
	return (true);
}

uint64_t WorldChunkInstabilityStore::policy_hash(
	const WorldChunkInstabilityPolicy &policy) noexcept
{
	uint64_t hash = WORLD_CHUNK_INSTABILITY_FNV_OFFSET;
	uint32_t values[3U + 8U + 8U] = {};
	values[0] = policy.maximum_value;
	values[1] = policy.default_initial_value;
	values[2] = policy.reward_threshold_count;
	for (uint32_t index = 0U; index < 8U; ++index)
		values[3U + index] = policy.reward_thresholds[index];
	for (uint32_t index = 0U; index < 8U; ++index)
		values[11U + index] = policy.danger_thresholds[index];
	for (uint32_t index = 0U; index < 19U; ++index)
	{
		uint32_t value = values[index];
		for (uint32_t byte_index = 0U; byte_index < 4U; ++byte_index)
		{
			hash ^= static_cast<uint8_t>(value);
			hash *= WORLD_CHUNK_INSTABILITY_FNV_PRIME;
			value >>= 8U;
		}
	}
	uint32_t danger_count = policy.danger_threshold_count;
	for (uint32_t byte_index = 0U; byte_index < 4U; ++byte_index)
	{
		hash ^= static_cast<uint8_t>(danger_count);
		hash *= WORLD_CHUNK_INSTABILITY_FNV_PRIME;
		danger_count >>= 8U;
	}
	return (hash == 0U ? 1U : hash);
}

void WorldChunkInstabilityStore::update_bands(
	WorldChunkInstabilityRecord &record) const noexcept
{
	uint32_t reward_band = 0U;
	uint32_t danger_band = 0U;
	while (reward_band < this->policy_.reward_threshold_count
		&& record.current_value
			>= this->policy_.reward_thresholds[reward_band])
		++reward_band;
	while (danger_band < this->policy_.danger_threshold_count
		&& record.current_value
			>= this->policy_.danger_thresholds[danger_band])
		++danger_band;
	/* These are unlocked opportunity tiers, not a live reading of the
	 * current value. Once earned, a tier survives later stabilizer reductions. */
	record.reward_band = std::max(record.reward_band, reward_band);
	record.danger_band = std::max(record.danger_band, danger_band);
}

int32_t WorldChunkInstabilityStore::configure(
	const WorldChunkInstabilityPolicy &policy) noexcept
{
	if (!WorldChunkInstabilityStore::policy_is_valid(policy))
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> guard(this->mutex_);
	if (this->configured_ && !this->records_.empty()
		&& WorldChunkInstabilityStore::policy_hash(this->policy_)
			!= WorldChunkInstabilityStore::policy_hash(policy))
		return (FT_ERR_INVALID_OPERATION);
	this->policy_ = policy;
	this->configured_ = true;
	return (FT_ERR_SUCCESS);
}

bool WorldChunkInstabilityStore::is_configured() const noexcept
{
	std::lock_guard<std::mutex> guard(this->mutex_);
	return (this->configured_);
}

int32_t WorldChunkInstabilityStore::get_unlocked(int32_t chunk_x,
	int32_t chunk_z, WorldChunkInstabilityRecord *record_out) const noexcept
{
	if (record_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->configured_)
		return (FT_ERR_NOT_INITIALISED);
	try
	{
		const auto found = this->records_.find(std::make_pair(chunk_x, chunk_z));
		if (found != this->records_.end())
		{
			*record_out = found->second;
			return (FT_ERR_SUCCESS);
		}
		WorldChunkInstabilityRecord staged = {};
		staged.chunk_x = chunk_x;
		staged.chunk_z = chunk_z;
		staged.initial_value = this->policy_.default_initial_value;
		staged.current_value = this->policy_.default_initial_value;
		this->update_bands(staged);
		*record_out = std::move(staged);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkInstabilityStore::publish_unlocked(
	WorldChunkInstabilityRecord &&record,
	WorldChunkInstabilityRecord *record_out) noexcept
{
	const std::pair<int32_t, int32_t> key(record.chunk_x, record.chunk_z);
	auto found = this->records_.find(key);
	WorldChunkInstabilityRecord output_copy = {};
	try
	{
		if (record_out != nullptr)
			output_copy = record;
		if (found == this->records_.end())
			this->records_.emplace(key, std::move(record));
		else
			std::swap(found->second, record);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (record_out != nullptr)
		*record_out = std::move(output_copy);
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkInstabilityStore::get(int32_t chunk_x, int32_t chunk_z,
	WorldChunkInstabilityRecord *record_out) const noexcept
{
	std::lock_guard<std::mutex> guard(this->mutex_);
	return (this->get_unlocked(chunk_x, chunk_z, record_out));
}

int32_t WorldChunkInstabilityStore::get_public_view(int32_t chunk_x,
	int32_t chunk_z, WorldChunkInstabilityPublicView *view_out) const noexcept
{
	WorldChunkInstabilityPublicView staged = {};
	if (view_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> guard(this->mutex_);
	if (!this->configured_)
		return (FT_ERR_NOT_INITIALISED);
	staged.chunk_x = chunk_x;
	staged.chunk_z = chunk_z;
	auto found = this->records_.find(std::make_pair(chunk_x, chunk_z));
	if (found != this->records_.end())
	{
		staged.reward_band = found->second.reward_band;
		staged.danger_band = found->second.danger_band;
	}
	else
	{
		uint32_t value = this->policy_.default_initial_value;
		while (staged.reward_band < this->policy_.reward_threshold_count
			&& value >= this->policy_.reward_thresholds[staged.reward_band])
			++staged.reward_band;
		while (staged.danger_band < this->policy_.danger_threshold_count
			&& value >= this->policy_.danger_thresholds[staged.danger_band])
			++staged.danger_band;
	}
	*view_out = staged;
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkInstabilityStore::ensure_record(int32_t chunk_x,
	int32_t chunk_z) noexcept
{
	WorldChunkInstabilityRecord record = {};
	std::lock_guard<std::mutex> guard(this->mutex_);
	if (!this->configured_)
		return (FT_ERR_NOT_INITIALISED);
	if (this->records_.find(std::make_pair(chunk_x, chunk_z))
		!= this->records_.end())
		return (FT_ERR_SUCCESS);
	if (this->records_.size() >= WorldChunkInstabilityStore::MAXIMUM_RECORDS)
		return (FT_ERR_FULL);
	int32_t result = this->get_unlocked(chunk_x, chunk_z, &record);
	if (result != FT_ERR_SUCCESS)
		return (result);
	try
	{
		this->records_.emplace(std::make_pair(chunk_x, chunk_z),
			std::move(record));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkInstabilityStore::adjust(uint64_t expected_record_revision,
	int32_t chunk_x, int32_t chunk_z, int32_t delta,
	WorldChunkInstabilityRecord *record_out) noexcept
{
	WorldChunkInstabilityRecord staged = {};
	int32_t result;
	std::lock_guard<std::mutex> guard(this->mutex_);
	result = this->get_unlocked(chunk_x, chunk_z, &staged);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (staged.record_revision != expected_record_revision)
		return (FT_ERR_INVALID_STATE);
	if (staged.record_revision == UINT64_MAX)
		return (FT_ERR_OUT_OF_RANGE);
	if (delta >= 0)
	{
		if (static_cast<uint64_t>(staged.current_value)
			+ static_cast<uint32_t>(delta) > this->policy_.maximum_value)
			return (FT_ERR_OUT_OF_RANGE);
		staged.current_value += static_cast<uint32_t>(delta);
	}
	else
	{
		const uint64_t reduction = static_cast<uint64_t>(-
			static_cast<int64_t>(delta));
		if (reduction > staged.current_value)
			return (FT_ERR_OUT_OF_RANGE);
		staged.current_value -= static_cast<uint32_t>(reduction);
	}
	staged.record_revision += 1U;
	this->update_bands(staged);
	return (this->publish_unlocked(std::move(staged), record_out));
}

static bool world_instability_contains(
	const std::vector<WorldChunkInstabilityRecord::VersionedId> &entries,
	uint32_t id, uint32_t version) noexcept
{
	for (const auto &entry : entries)
		if (entry.id == id && entry.version == version)
			return (true);
	return (false);
}

static int32_t world_instability_add_entry(
	WorldChunkInstabilityRecord &record, uint32_t id, uint32_t version,
	bool modifier) noexcept
{
	std::vector<WorldChunkInstabilityRecord::VersionedId> &entries = modifier
		? record.permanent_modifiers : record.feature_outcomes;
	if (id == 0U || version == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (world_instability_contains(entries, id, version))
		return (FT_ERR_ALREADY_EXISTS);
	if (entries.size() >= WorldChunkInstabilityStore::MAXIMUM_ENTRIES_PER_RECORD)
		return (FT_ERR_FULL);
	try
	{
		entries.push_back({id, version});
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkInstabilityStore::add_permanent_modifier(
	uint64_t expected_record_revision, int32_t chunk_x, int32_t chunk_z,
	uint32_t modifier_id, uint32_t modifier_version,
	WorldChunkInstabilityRecord *record_out) noexcept
{
	WorldChunkInstabilityRecord staged = {};
	int32_t result;
	std::lock_guard<std::mutex> guard(this->mutex_);
	result = this->get_unlocked(chunk_x, chunk_z, &staged);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (staged.record_revision != expected_record_revision)
		return (FT_ERR_INVALID_STATE);
	if (staged.record_revision == UINT64_MAX)
		return (FT_ERR_OUT_OF_RANGE);
	result = world_instability_add_entry(staged, modifier_id,
		modifier_version, true);
	if (result != FT_ERR_SUCCESS)
		return (result);
	staged.record_revision += 1U;
	return (this->publish_unlocked(std::move(staged), record_out));
}

int32_t WorldChunkInstabilityStore::add_feature_outcome(
	uint64_t expected_record_revision, int32_t chunk_x, int32_t chunk_z,
	uint32_t outcome_id, uint32_t outcome_version,
	WorldChunkInstabilityRecord *record_out) noexcept
{
	WorldChunkInstabilityRecord staged = {};
	int32_t result;
	std::lock_guard<std::mutex> guard(this->mutex_);
	result = this->get_unlocked(chunk_x, chunk_z, &staged);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (staged.record_revision != expected_record_revision)
		return (FT_ERR_INVALID_STATE);
	if (staged.record_revision == UINT64_MAX)
		return (FT_ERR_OUT_OF_RANGE);
	result = world_instability_add_entry(staged, outcome_id,
		outcome_version, false);
	if (result != FT_ERR_SUCCESS)
		return (result);
	staged.record_revision += 1U;
	return (this->publish_unlocked(std::move(staged), record_out));
}

int32_t WorldChunkInstabilityStore::record_generation(
	uint64_t expected_record_revision, int32_t chunk_x, int32_t chunk_z,
	uint64_t generation_revision, uint64_t rules_hash,
	WorldChunkInstabilityRecord *record_out,
	const std::vector<WorldChunkInstabilityRecord::VersionedId>
		*feature_outcomes) noexcept
{
	WorldChunkInstabilityRecord staged = {};
	int32_t result;
	std::lock_guard<std::mutex> guard(this->mutex_);
	if ((feature_outcomes == nullptr || feature_outcomes->empty())
		&& record_out == nullptr && this->configured_)
	{
		auto found = this->records_.find(std::make_pair(chunk_x, chunk_z));
		if (found != this->records_.end())
		{
			WorldChunkInstabilityRecord &record = found->second;
			if (record.record_revision != expected_record_revision)
				return (FT_ERR_INVALID_STATE);
			if (record.record_revision == UINT64_MAX
				|| generation_revision == 0U || rules_hash == 0U
				|| generation_revision <= record.generation_revision)
				return (FT_ERR_INVALID_ARGUMENT);
			record.generation_revision = generation_revision;
			record.rules_hash = rules_hash;
			record.record_revision += 1U;
			return (FT_ERR_SUCCESS);
		}
	}
	result = this->get_unlocked(chunk_x, chunk_z, &staged);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (staged.record_revision != expected_record_revision)
		return (FT_ERR_INVALID_STATE);
	if (staged.record_revision == UINT64_MAX
		|| generation_revision == 0U || rules_hash == 0U
		|| generation_revision <= staged.generation_revision)
		return (FT_ERR_INVALID_ARGUMENT);
	if (feature_outcomes != nullptr)
	{
		for (const auto &outcome : *feature_outcomes)
		{
			if (outcome.id == 0U || outcome.version == 0U)
				return (FT_ERR_INVALID_ARGUMENT);
			if (world_instability_contains(staged.feature_outcomes,
				outcome.id, outcome.version))
				continue ;
			result = world_instability_add_entry(staged, outcome.id,
				outcome.version, false);
			if (result != FT_ERR_SUCCESS)
				return (result);
		}
	}
	staged.generation_revision = generation_revision;
	staged.rules_hash = rules_hash;
	staged.record_revision += 1U;
	return (this->publish_unlocked(std::move(staged), record_out));
}

int32_t WorldChunkInstabilityStore::restore_record(
	uint64_t expected_current_revision,
	const WorldChunkInstabilityRecord &previous_record) noexcept
{
	if (expected_current_revision == 0U
		|| previous_record.record_revision == 0U
		|| previous_record.record_revision >= expected_current_revision)
		return (FT_ERR_INVALID_ARGUMENT);
	WorldChunkInstabilityRecord staged;
	try
	{
		staged = previous_record;
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	std::lock_guard<std::mutex> lock(this->mutex_);
	const auto found = this->records_.find(std::make_pair(
		previous_record.chunk_x, previous_record.chunk_z));
	if (found == this->records_.end())
		return (FT_ERR_NOT_FOUND);
	if (found->second.record_revision != expected_current_revision)
		return (FT_ERR_INVALID_STATE);
	using std::swap;
	swap(found->second, staged);
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkInstabilityStore::serialize(
	std::vector<uint8_t> &bytes_out) const noexcept
{
	std::vector<uint8_t> staged;
	std::lock_guard<std::mutex> guard(this->mutex_);
	if (!this->configured_)
		return (FT_ERR_NOT_INITIALISED);
	if (this->records_.size() > WorldChunkInstabilityStore::MAXIMUM_RECORDS)
		return (FT_ERR_FULL);
	try
	{
		world_instability_append_u32(staged, WORLD_CHUNK_INSTABILITY_MAGIC);
		world_instability_append_u32(staged, WORLD_CHUNK_INSTABILITY_VERSION);
		world_instability_append_u64(staged,
			WorldChunkInstabilityStore::policy_hash(this->policy_));
		world_instability_append_u32(staged,
			static_cast<uint32_t>(this->records_.size()));
		for (const auto &entry : this->records_)
		{
			const WorldChunkInstabilityRecord &record = entry.second;
			if (record.permanent_modifiers.size()
				> WorldChunkInstabilityStore::MAXIMUM_ENTRIES_PER_RECORD
				|| record.feature_outcomes.size()
				> WorldChunkInstabilityStore::MAXIMUM_ENTRIES_PER_RECORD)
				return (FT_ERR_FULL);
			world_instability_append_u32(staged,
				static_cast<uint32_t>(record.chunk_x));
			world_instability_append_u32(staged,
				static_cast<uint32_t>(record.chunk_z));
			world_instability_append_u32(staged, record.initial_value);
			world_instability_append_u32(staged, record.current_value);
			world_instability_append_u32(staged, record.reward_band);
			world_instability_append_u32(staged, record.danger_band);
			world_instability_append_u64(staged, record.generation_revision);
			world_instability_append_u64(staged, record.rules_hash);
			world_instability_append_u64(staged, record.record_revision);
			world_instability_append_u32(staged,
				static_cast<uint32_t>(record.permanent_modifiers.size()));
			world_instability_append_u32(staged,
				static_cast<uint32_t>(record.feature_outcomes.size()));
			for (const auto &modifier : record.permanent_modifiers)
			{
				world_instability_append_u32(staged, modifier.id);
				world_instability_append_u32(staged, modifier.version);
			}
			for (const auto &outcome : record.feature_outcomes)
			{
				world_instability_append_u32(staged, outcome.id);
				world_instability_append_u32(staged, outcome.version);
			}
		}
		if (staged.size() > 64U * 1024U * 1024U - 8U)
			return (FT_ERR_FULL);
		world_instability_append_u64(staged,
			world_instability_hash(staged.data(), staged.size()));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	bytes_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkInstabilityStore::deserialize(const uint8_t *bytes,
	std::size_t byte_count) noexcept
{
	std::map<std::pair<int32_t, int32_t>, WorldChunkInstabilityRecord> staged_records;
	std::size_t offset = 0U;
	uint32_t magic;
	uint32_t version;
	uint64_t stored_policy_hash;
	uint32_t record_count;
	uint64_t checksum;
	std::size_t checksum_offset;
	std::lock_guard<std::mutex> guard(this->mutex_);
	if (!this->configured_)
		return (FT_ERR_NOT_INITIALISED);
	if (bytes == nullptr || byte_count < 28U
		|| byte_count > 64U * 1024U * 1024U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!world_instability_read_u32(bytes, byte_count, &offset, &magic)
		|| !world_instability_read_u32(bytes, byte_count, &offset, &version)
		|| !world_instability_read_u64(bytes, byte_count, &offset,
			&stored_policy_hash)
		|| !world_instability_read_u32(bytes, byte_count, &offset, &record_count)
		|| magic != WORLD_CHUNK_INSTABILITY_MAGIC
		|| version != WORLD_CHUNK_INSTABILITY_VERSION
		|| stored_policy_hash
			!= WorldChunkInstabilityStore::policy_hash(this->policy_)
		|| record_count > WorldChunkInstabilityStore::MAXIMUM_RECORDS)
		return (FT_ERR_INVALID_ARGUMENT);
	checksum_offset = byte_count - 8U;
	if (!world_instability_read_u64(bytes, byte_count,
			&checksum_offset, &checksum)
		|| checksum != world_instability_hash(bytes, byte_count - 8U))
		return (FT_ERR_INVALID_ARGUMENT);
	offset = 20U;
	try
	{
		for (uint32_t record_index = 0U; record_index < record_count;
			++record_index)
		{
			WorldChunkInstabilityRecord record = {};
			uint32_t x_value;
			uint32_t z_value;
			uint32_t modifier_count;
			uint32_t outcome_count;
			uint32_t encoded_reward_band;
			uint32_t encoded_danger_band;
			if (!world_instability_read_u32(bytes, byte_count - 8U, &offset,
					&x_value)
				|| !world_instability_read_u32(bytes, byte_count - 8U, &offset,
					&z_value)
				|| !world_instability_read_u32(bytes, byte_count - 8U, &offset,
					&record.initial_value)
				|| !world_instability_read_u32(bytes, byte_count - 8U, &offset,
					&record.current_value)
				|| !world_instability_read_u32(bytes, byte_count - 8U, &offset,
					&record.reward_band)
				|| !world_instability_read_u32(bytes, byte_count - 8U, &offset,
					&record.danger_band)
				|| !world_instability_read_u64(bytes, byte_count - 8U, &offset,
					&record.generation_revision)
				|| !world_instability_read_u64(bytes, byte_count - 8U, &offset,
					&record.rules_hash)
				|| !world_instability_read_u64(bytes, byte_count - 8U, &offset,
					&record.record_revision)
				|| !world_instability_read_u32(bytes, byte_count - 8U, &offset,
					&modifier_count)
				|| !world_instability_read_u32(bytes, byte_count - 8U, &offset,
					&outcome_count)
				|| modifier_count > WorldChunkInstabilityStore::MAXIMUM_ENTRIES_PER_RECORD
				|| outcome_count > WorldChunkInstabilityStore::MAXIMUM_ENTRIES_PER_RECORD)
				return (FT_ERR_INVALID_ARGUMENT);
			record.chunk_x = static_cast<int32_t>(x_value);
			record.chunk_z = static_cast<int32_t>(z_value);
			if (record.initial_value > this->policy_.maximum_value
				|| record.current_value > this->policy_.maximum_value
				|| record.record_revision == 0U)
				return (FT_ERR_INVALID_ARGUMENT);
			encoded_reward_band = record.reward_band;
			encoded_danger_band = record.danger_band;
			if (encoded_reward_band > this->policy_.reward_threshold_count
				|| encoded_danger_band > this->policy_.danger_threshold_count)
				return (FT_ERR_INVALID_ARGUMENT);
			this->update_bands(record);
			if (encoded_reward_band != record.reward_band
				|| encoded_danger_band != record.danger_band)
				return (FT_ERR_INVALID_ARGUMENT);
			for (uint32_t entry_index = 0U; entry_index < modifier_count;
				++entry_index)
			{
				WorldChunkInstabilityRecord::VersionedId entry = {};
				if (!world_instability_read_u32(bytes, byte_count - 8U, &offset,
						&entry.id)
					|| !world_instability_read_u32(bytes, byte_count - 8U, &offset,
						&entry.version)
					|| entry.id == 0U || entry.version == 0U
					|| world_instability_contains(record.permanent_modifiers,
						entry.id, entry.version))
					return (FT_ERR_INVALID_ARGUMENT);
				record.permanent_modifiers.push_back(entry);
			}
			for (uint32_t entry_index = 0U; entry_index < outcome_count;
				++entry_index)
			{
				WorldChunkInstabilityRecord::VersionedId entry = {};
				if (!world_instability_read_u32(bytes, byte_count - 8U, &offset,
						&entry.id)
					|| !world_instability_read_u32(bytes, byte_count - 8U, &offset,
						&entry.version)
					|| entry.id == 0U || entry.version == 0U
					|| world_instability_contains(record.feature_outcomes,
						entry.id, entry.version))
					return (FT_ERR_INVALID_ARGUMENT);
				record.feature_outcomes.push_back(entry);
			}
			if (!staged_records.emplace(std::make_pair(record.chunk_x,
					record.chunk_z), std::move(record)).second)
				return (FT_ERR_INVALID_ARGUMENT);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (offset != byte_count - 8U)
		return (FT_ERR_INVALID_ARGUMENT);
	this->records_.swap(staged_records);
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunkInstabilityStore::save_file(const char *file_path) const noexcept
{
	std::vector<uint8_t> bytes;
	if (file_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	const int32_t result = this->serialize(bytes);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (file_replace_safe(file_path,
		reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}

int32_t WorldChunkInstabilityStore::load_file(const char *file_path) noexcept
{
	FILE *file;
	long file_size;
	std::vector<uint8_t> bytes;
	if (file_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	file = std::fopen(file_path, "rb");
	if (file == nullptr)
		return (FT_ERR_IO);
	if (std::fseek(file, 0, SEEK_END) != 0
		|| (file_size = std::ftell(file)) < 0L
		|| file_size > 64L * 1024L * 1024L
		|| std::fseek(file, 0, SEEK_SET) != 0)
	{
		std::fclose(file);
		return (FT_ERR_IO);
	}
	try
	{
		bytes.resize(static_cast<std::size_t>(file_size));
		if (std::fread(bytes.data(), 1U, bytes.size(), file) != bytes.size())
		{
			std::fclose(file);
			return (FT_ERR_IO);
		}
	}
	catch (const std::bad_alloc &)
	{
		std::fclose(file);
		return (FT_ERR_NO_MEMORY);
	}
	if (std::fclose(file) != 0)
		return (FT_ERR_IO);
	return (this->deserialize(bytes.data(), bytes.size()));
}
