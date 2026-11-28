#ifndef WORLD_CHUNK_INSTABILITY_STORE_HPP
# define WORLD_CHUNK_INSTABILITY_STORE_HPP

# include <cstddef>
# include <cstdint>
# include <map>
# include <mutex>
# include <vector>

struct WorldChunkInstabilityPolicy
{
	uint32_t maximum_value;
	uint32_t default_initial_value;
	uint32_t reward_threshold_count;
	uint32_t danger_threshold_count;
	uint32_t reward_thresholds[8];
	uint32_t danger_thresholds[8];
};

struct WorldChunkInstabilityRecord
{
	struct VersionedId
	{
		uint32_t id;
		uint32_t version;
	};
	int32_t chunk_x;
	int32_t chunk_z;
	uint32_t initial_value;
	uint32_t current_value;
	uint32_t reward_band;
	uint32_t danger_band;
	uint64_t generation_revision;
	uint64_t rules_hash;
	uint64_t record_revision;
	std::vector<VersionedId> permanent_modifiers;
	std::vector<VersionedId> feature_outcomes;
};

struct WorldChunkInstabilityPublicView
{
	int32_t chunk_x;
	int32_t chunk_z;
	uint32_t reward_band;
	uint32_t danger_band;
};

class WorldChunkInstabilityStore
{
  public:
	static const uint32_t MAXIMUM_RECORDS = 100000U;
	static const uint32_t MAXIMUM_ENTRIES_PER_RECORD = 64U;

	WorldChunkInstabilityStore();
	WorldChunkInstabilityStore(const WorldChunkInstabilityStore &other) = delete;
	WorldChunkInstabilityStore &operator=(
		const WorldChunkInstabilityStore &other) = delete;

	int32_t configure(const WorldChunkInstabilityPolicy &policy) noexcept;
	void clear_records() noexcept;
	bool is_configured() const noexcept;
	int32_t get(int32_t chunk_x, int32_t chunk_z,
		WorldChunkInstabilityRecord *record_out) const noexcept;
	int32_t get_public_view(int32_t chunk_x, int32_t chunk_z,
		WorldChunkInstabilityPublicView *view_out) const noexcept;
	int32_t ensure_record(int32_t chunk_x, int32_t chunk_z) noexcept;
	int32_t adjust(uint64_t expected_record_revision, int32_t chunk_x,
		int32_t chunk_z, int32_t delta,
		WorldChunkInstabilityRecord *record_out) noexcept;
	int32_t add_permanent_modifier(uint64_t expected_record_revision,
		int32_t chunk_x, int32_t chunk_z, uint32_t modifier_id,
		uint32_t modifier_version,
		WorldChunkInstabilityRecord *record_out) noexcept;
	int32_t add_feature_outcome(uint64_t expected_record_revision,
		int32_t chunk_x, int32_t chunk_z, uint32_t outcome_id,
		uint32_t outcome_version,
		WorldChunkInstabilityRecord *record_out) noexcept;
	int32_t record_generation(uint64_t expected_record_revision,
		int32_t chunk_x, int32_t chunk_z, uint64_t generation_revision,
		uint64_t rules_hash,
		WorldChunkInstabilityRecord *record_out,
		const std::vector<WorldChunkInstabilityRecord::VersionedId>
			*feature_outcomes = nullptr) noexcept;
	int32_t restore_record(uint64_t expected_current_revision,
		const WorldChunkInstabilityRecord &previous_record) noexcept;
	int32_t serialize(std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t deserialize(const uint8_t *bytes, std::size_t byte_count) noexcept;
	int32_t save_file(const char *file_path) const noexcept;
	int32_t load_file(const char *file_path) noexcept;

  private:
	mutable std::mutex mutex_;
	WorldChunkInstabilityPolicy policy_;
	bool configured_;
	std::map<std::pair<int32_t, int32_t>, WorldChunkInstabilityRecord> records_;

	static bool policy_is_valid(
		const WorldChunkInstabilityPolicy &policy) noexcept;
	static uint64_t policy_hash(
		const WorldChunkInstabilityPolicy &policy) noexcept;
	void update_bands(WorldChunkInstabilityRecord &record) const noexcept;
	int32_t get_unlocked(int32_t chunk_x, int32_t chunk_z,
		WorldChunkInstabilityRecord *record_out) const noexcept;
	int32_t publish_unlocked(WorldChunkInstabilityRecord &&record,
		WorldChunkInstabilityRecord *record_out) noexcept;
};

#endif
