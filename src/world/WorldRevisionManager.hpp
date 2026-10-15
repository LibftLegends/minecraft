#ifndef WORLD_REVISION_MANAGER_HPP
# define WORLD_REVISION_MANAGER_HPP

# include "../../src/world/WorldChunkStreamer.hpp"
# include "../../src/world/WorldRegenerationProgress.hpp"
# include "../../src/world/WorldRevisionPricing.hpp"
# include <vector>

class RegenerationWorldRules;

class WorldRevisionManager
{
  public:
	/* Leave room for the roadmap's minimum 9-by-9-chunk Volcano footprint. */
	static const uint32_t HARD_MAX_SELECTED_CHUNKS = 256U;
	static const uint32_t MAXIMUM_OCCUPIED_CHUNKS = 4096U;
	static const uint32_t LOOM_SAFE_ZONE_CHUNK_RADIUS = 1U;

	struct						RevisionChunk
	{
		int32_t					chunk_x;
		int32_t					chunk_z;
		bool					bonus;
	};
	struct OccupiedChunk
	{
		int32_t chunk_x;
		int32_t chunk_z;
	};

	World &world_;
	bool						pending_;
	uint32_t					revision_id_;
	uint64_t				selection_revision_;
	uint64_t				rules_hash_;
	uint64_t				cost_policy_revision_;
	WorldRevisionCostPolicy cost_policy_;
	bool cost_policy_configured_;
	bool barriers_active_;
	uint32_t					stage_mask_;
	int32_t						mode_;
	voxel_generation_config	config_;
	voxel_generation_config	biome_base_config_;
	voxel_biome_selector base_biome_selector_;
	void *base_biome_selector_user_data_;
	uint32_t biome_rule_count_;
	bool biome_enabled_[VOXEL_MAX_CUSTOM_BIOMES];
	int32_t biome_weight_adjustments_[VOXEL_MAX_CUSTOM_BIOMES];
	bool base_biome_selector_captured_;
	std::vector<RevisionChunk> selected_;
	std::vector<RevisionChunk> restored_selected_;
	uint64_t restored_selection_revision_;
	uint64_t restored_deadline_epoch_;
	uint64_t decision_deadline_epoch_;
	int32_t restored_mode_;
	uint32_t restored_stage_mask_;
	bool restored_barrier_state_;
	bool lifecycle_restore_pending_;
	std::vector<RevisionChunk> rules_target_chunks_;
	std::vector<WorldGenerationPipeline::GenerationFeaturePlan> feature_plans_;
	std::vector<RevisionChunk> manual_protected_;
	bool loom_station_configured_;
	int32_t loom_station_chunk_x_;
	int32_t loom_station_chunk_z_;
	bool loom_station_block_configured_;
	int32_t loom_station_block_x_;
	int32_t loom_station_block_y_;
	int32_t loom_station_block_z_;
	uint32_t loom_station_block_id_;
	std::vector<OccupiedChunk> occupied_chunks_;
	WorldRegenerationProgress	progress_;

	WorldRevisionManager(World &world);
	WorldRevisionManager(const WorldRevisionManager &other);
	~WorldRevisionManager();
	WorldRevisionManager &operator=(const WorldRevisionManager &other);

	void reset() noexcept;
	int32_t begin(const voxel_generation_config &config,
		int32_t mode) noexcept;
	int32_t begin(const voxel_generation_config &config, int32_t mode,
		const RegenerationWorldRules &rules) noexcept;
	int32_t update_pending_rules(const RegenerationWorldRules &rules) noexcept;
	int32_t cancel() noexcept;
	int32_t set_barriers_active(bool active) noexcept;
	bool barriers_active() const noexcept;
	bool barrier_blocks_chunk(int32_t chunk_x, int32_t chunk_z) const noexcept;
	uint32_t identifier() const noexcept;
	uint32_t stage_mask() const noexcept;
	int32_t mode() const noexcept;
	bool pending() const noexcept;
	bool regenerating() const noexcept;
	std::size_t selected_count() const noexcept;
	int32_t feature_reservation_upper_bound(
		uint32_t *count_out) const noexcept;
	int32_t copy_selected_chunks(
		std::vector<World::RevisionChunkCoordinate> &chunks_out) const noexcept;
	int32_t copy_selected_paid_chunks(
		std::vector<World::RevisionChunkCoordinate> &chunks_out) const noexcept;
	uint64_t selection_revision() const noexcept;
	uint64_t rules_hash() const noexcept;
	uint64_t cost_policy_revision() const noexcept;
	std::size_t manually_protected_count() const noexcept;
	int32_t select_chunk(int32_t chunk_x, int32_t chunk_z,
		bool selected) noexcept;
	int32_t select_chunk(uint64_t expected_selection_revision,
		int32_t chunk_x, int32_t chunk_z, bool selected,
		uint64_t *current_selection_revision) noexcept;
	int32_t select_chunk(uint64_t expected_selection_revision,
		uint32_t maximum_selected_chunks, int32_t chunk_x, int32_t chunk_z,
		bool selected, uint64_t *current_selection_revision) noexcept;
	int32_t select_bonus_chunk(uint64_t expected_selection_revision,
		uint32_t maximum_selected_chunks, int32_t chunk_x, int32_t chunk_z,
		uint64_t *current_selection_revision) noexcept;
	int32_t validate_bonus_chunk(uint64_t expected_selection_revision,
		uint32_t maximum_selected_chunks, int32_t chunk_x,
		int32_t chunk_z) const noexcept;
	int32_t unselect_chunk(int32_t chunk_x, int32_t chunk_z) noexcept;
	int32_t configure_cost_policy(uint64_t expected_policy_revision,
		const WorldRevisionCostPolicy &policy,
		uint64_t *current_policy_revision) noexcept;
	int32_t query_start_cost(uint32_t opening_card_count,
		const WorldRevisionCostPolicy &policy,
		WorldRevisionCostQuote *quote) const noexcept;
	int32_t query_start_cost(uint64_t expected_selection_revision,
		uint32_t opening_card_count, const WorldRevisionCostPolicy &policy,
		WorldRevisionCostQuote *quote) const noexcept;
	int32_t query_start_cost(uint64_t expected_selection_revision,
		uint64_t expected_policy_revision, uint32_t opening_card_count,
		WorldRevisionCostQuote *quote) const noexcept;
	int32_t set_chunk_protected(int32_t chunk_x, int32_t chunk_z,
		bool protected_state) noexcept;
	int32_t set_chunk_protected(uint64_t expected_selection_revision,
		int32_t chunk_x, int32_t chunk_z, bool protected_state,
		uint64_t *current_selection_revision) noexcept;
	int32_t set_occupied_chunks(
		const std::vector<OccupiedChunk> &occupied_chunks,
		uint64_t *current_selection_revision) noexcept;
	int32_t configure_loom_station(uint64_t expected_selection_revision,
		int32_t chunk_x, int32_t chunk_z,
		uint64_t *current_selection_revision) noexcept;
	int32_t configure_loom_station_block(uint64_t expected_selection_revision,
		int32_t block_x, int32_t block_y, int32_t block_z,
		uint32_t block_id,
		uint64_t *current_selection_revision) noexcept;
	bool has_loom_station() const noexcept;
	int32_t get_loom_station_chunk(int32_t *chunk_x_out,
		int32_t *chunk_z_out) const noexcept;
	int32_t get_loom_station_block(bool *configured_out, int32_t *block_x_out,
		int32_t *block_y_out, int32_t *block_z_out,
		uint32_t *block_id_out) const noexcept;
	int32_t invalidate_loom_station() noexcept;
	int32_t invalidate_loom_station_if_block_changed(int32_t block_x,
		int32_t block_y, int32_t block_z, uint32_t new_block_id) noexcept;
	int32_t get_protection_info(int32_t chunk_x, int32_t chunk_z,
		World::RevisionProtectionInfo *info_out) const noexcept;
	bool is_chunk_protected(int32_t chunk_x, int32_t chunk_z) const noexcept;
	int32_t chunk_state(int32_t chunk_x, int32_t chunk_z) const noexcept;
	int32_t save_metadata(const char *file_path) const noexcept;
	int32_t load_metadata(const char *file_path) noexcept;
	int32_t serialize_metadata(std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t deserialize_metadata(const uint8_t *bytes,
		std::size_t byte_count) noexcept;
	int32_t restore_committed_revision(
		const RegenerationWorldRules &rules) noexcept;
	int32_t set_decision_deadline_epoch(uint64_t deadline_epoch) noexcept;
	uint64_t decision_deadline_epoch() const noexcept;

	bool is_regenerating_for(uint64_t relevance_epoch) const noexcept;
	void record_regeneration_completed() noexcept;
	void record_regeneration_error(int32_t error_code) noexcept;
	void record_regeneration_skipped() noexcept;
	void record_regeneration_success() noexcept;
	bool all_regeneration_jobs_done() const noexcept;
	int32_t finish_regeneration() noexcept;
	int32_t start_regeneration() noexcept;
	int32_t retry_failed_regeneration() noexcept;
	int32_t regenerate_selected_chunks(int32_t *regenerated_count,
		int32_t *skipped_count) noexcept;
	int32_t apply_request(const voxel_generation_config &config, int32_t mode,
		uint32_t stage_mask, const std::vector<RevisionChunk> &selected_chunks,
		const std::vector<RevisionChunk> &protected_chunks,
		int32_t *regenerated_count, int32_t *skipped_count) noexcept;

  private:
	static uint32_t stage_mask_for_mode(int32_t mode) noexcept;
	static uint32_t select_biome_with_rules(uint64_t seed_value,
		int32_t world_block_x, int32_t world_block_z, uint32_t biome_count,
		void *user_data) noexcept;
};

# include "../../src/world/WorldRevisionChunkSet.hpp"
# include "../../src/world/WorldRevisionMetadataStore.hpp"
# include "../../src/world/WorldRevisionRegenerator.hpp"

#endif
