#ifndef WORLD_HPP
# define WORLD_HPP

#include "../ft_vox.hpp"
#include "../../src/chunks/WorldChunk.hpp"
#include "../../src/chunks/WorldChunkLoader.hpp"
#include "../../src/chunks/WorldChunkStore.hpp"
#include "../../src/edits/WorldEditHistory.hpp"
#include "../../src/coordinates/WorldCoordinates.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include "../../Libft/Modules/Voxel/voxel_api.hpp"
#include "../../src/queries/WorldBlockQuery.hpp"
#include "../../src/edits/WorldBlockEditor.hpp"
#include "../../src/queries/WorldRaycaster.hpp"
#include "../../src/validators/WorldVisibilityValidator.hpp"
#include "../../src/world/WorldGenerationPipeline.hpp"
#include "../../src/world/WorldRevisionPricing.hpp"
#include "../../src/world/WorldChunkInstabilityStore.hpp"
#include "../../src/world/WorldTerrainChunkStore.hpp"
#include "../../src/entities/WorldEntityStore.hpp"
#include "../../src/entities/WorldSpawnerStore.hpp"
#include "../../src/entities/WorldItemStore.hpp"
#include "../../src/inventory/MinecraftItemCatalog.hpp"
#include <chrono>
#include <shared_mutex>

class WorldChunkStreamer;
class WorldRevisionManager;
class RegenerationWorldRules;
typedef int32_t (*WorldGenerationCheckpointCallback)(void *user_data) noexcept;

class World
{
  public:
	enum							RegenerationMode
	{
		REGEN_DECORATION_REFRESH = 0,
		REGEN_UNDERGROUND_REFRESH = 1,
		REGEN_VOXEL_RESHAPING = 2,
		REGEN_FULL = 3
	};

	enum							ChunkRevisionState
	{
		REVISION_UNCHANGED = 0,
		REVISION_PROTECTED = 1,
		REVISION_SELECTED = 2,
		REVISION_TRANSITION = 3
	};
	struct RevisionProtectionInfo
	{
		bool generation_protected;
		bool loom_safe_zone;
		bool manually_protected;
		bool occupied_player_exclusion;
	};

	struct							WorldRevision
	{
		uint32_t					identifier;
		uint64_t				selection_revision;
		uint64_t				cost_policy_revision;
		uint32_t					stage_mask;
		RegenerationMode			mode;
		bool						pending;
		bool						regenerating;
		size_t						selected_count;
		size_t						manually_protected_count;
		size_t						completed_jobs;
		size_t						total_jobs;
		int32_t						regenerated_chunks;
		int32_t						skipped_chunks;
		int32_t						regeneration_error;
	};

	struct							RevisionPreviewEntry
	{
		int32_t						chunk_x;
		int32_t						chunk_z;
		ChunkRevisionState			state;
		RevisionProtectionInfo	protection_info;
	};

	struct							RevisionChunkCoordinate
	{
		int32_t						chunk_x;
		int32_t						chunk_z;
	};

	struct							RevisionRequest
	{
		voxel_generation_config	config;
		RegenerationMode			mode;
		uint32_t					stage_mask;
		std::vector<RevisionChunkCoordinate> selected_chunks;
		std::vector<RevisionChunkCoordinate> protected_chunks;
	};

	struct							RevisionRequestResult
	{
		uint32_t					revision_identifier;
		uint32_t					stage_mask;
		int32_t						regenerated_count;
		int32_t						skipped_count;
	};

	struct							StreamDiagnostics
	{
		size_t						stale_result_count;
		size_t						stale_stream_result_count;
		size_t						stale_remesh_result_count;
		size_t						playable_failed_count;
		size_t						playable_required_count;
		size_t						playable_drawable_count;
		size_t						active_generation_count;
		size_t						remesh_queue_peak;
		size_t						remesh_priority_queue_depth;
		size_t						interactive_remesh_queue_depth;
		uint64_t					remesh_starvation_promotions;
		uint64_t					oldest_remesh_queue_age;
		uint64_t					remesh_snapshot_bytes;
		uint64_t					remesh_capture_duration_nanoseconds;
		uint64_t					remesh_capture_count;
		uint64_t					remesh_scanned_cells;
		uint64_t					remesh_propagated_cells;
		uint64_t					remesh_light_queue_peak;
		uint64_t					remesh_completed_count;
		uint64_t					remesh_incremental_completed_count;
		uint64_t					remesh_full_completed_count;
		uint64_t					remesh_geometry_only_count;
		uint64_t					remesh_canceled_count;
		size_t					deferred_edit_count;
		size_t					deferred_edit_cursor;
		uint64_t					frame;
		uint64_t					progress_frame;
		size_t						candidate_count;
		size_t						ready_count;
		size_t						pending_count;
		size_t						retryable_count;
		size_t						failed_count;
		size_t					stale_remesh_capture_count;
		size_t					stale_remesh_dependency_count;
		size_t					stale_remesh_pending_count;
		size_t					stale_remesh_revision_count;
		uint64_t					oldest_result_age_nanoseconds;
		uint64_t					oldest_pending_age;
		int32_t						last_error;
	};

	WorldChunk						chunks[WorldCoordinates::CHUNK_COUNT];
	WorldChunk						*chunk_index[WorldCoordinates::CHUNK_COUNT];
	int32_t							chunk_index_center_x;
	int32_t							chunk_index_center_z;
	bool							chunk_index_valid;
	int32_t							chunk_count;
	int32_t							loaded_chunk_count;
	int32_t							center_chunk_x;
	int32_t							center_chunk_z;
	int32_t							active_render_distance;
	char							seed[128];
	voxel_generation_config		voxel_config;
	voxel_generation_context		voxel_context;
	bool							voxel_generation_started;
	bool							lifecycle_active_;
	uint64_t						current_tick;
	uint64_t						geometry_revision;
	WorldEditHistory				edit_history;
	ft_uniqueptr<WorldChunkStreamer> chunk_streamer_storage_;
	ft_uniqueptr<WorldRevisionManager> revision_manager_storage_;
	WorldChunkStreamer &chunk_streamer;
	WorldRevisionManager &revision_manager;

	World();
	World(const World &other);
	~World();
	World &operator=(const World &other);

	int32_t initialize(const char *seed_value);
	int32_t initialize(const char *seed_value,
		const char *voxel_config_file_path);
	int32_t initialize_for_world(const char *seed_value,
		const uint8_t world_id[32], const char *voxel_config_file_path = nullptr);
	void mark_geometry_changed() noexcept;
	void set_voxel_config(const voxel_generation_config &config);
	const voxel_generation_config &voxel_generation_settings() const;
	int32_t load_voxel_config(const char *file_path);
	int32_t save_voxel_config(const char *file_path) const;
	void destroy();
	int32_t update_around(double camera_x, double camera_z,
		int32_t generation_budget);
	int32_t update_around(double camera_x, double camera_z,
		int32_t generation_budget, int32_t render_distance);
	int32_t set_light_update_config(
		const voxel_light_update_config &config) noexcept;
	const voxel_light_update_config &light_update_config() const noexcept;
	int32_t set_interactive_light_update_config(
		const voxel_light_update_config &config) noexcept;
	const voxel_light_update_config &interactive_light_update_config() const noexcept;
	int32_t stream_last_error() const;
	int32_t stream_retryable_count() const;
	StreamDiagnostics stream_diagnostics() const;
	bool validate_visible_distance(double camera_x, double camera_z, double yaw,
		int32_t required_distance) const;
	bool surface_top_at(int32_t world_x, int32_t world_z,
		double *surface_top) const;
	bool solid_block_at(int32_t world_x, int32_t world_y,
		int32_t world_z) const;
	bool block_id_at(int32_t world_x, int32_t world_y, int32_t world_z,
		uint32_t *block_id) const;
	bool light_level_at(int32_t world_x, int32_t world_y, int32_t world_z,
		uint8_t *light_level) const;
	int32_t delete_block_at(int32_t world_x, int32_t world_y, int32_t world_z);
	int32_t place_block_at(int32_t world_x, int32_t world_y, int32_t world_z,
		uint32_t block_id);
	int32_t replace_fluid_source_at(int32_t world_x, int32_t world_y,
		int32_t world_z, uint32_t expected_block_id,
		uint32_t replacement_block_id,
		uint64_t expected_voxel_revision);
	int32_t replace_block_state_at(int32_t world_x, int32_t world_y,
		int32_t world_z, uint32_t expected_block_id,
		uint32_t replacement_block_id,
		uint64_t expected_voxel_revision);
	int32_t till_farmland_at(int32_t world_x, int32_t world_y,
		int32_t world_z, uint64_t expected_voxel_revision);
	bool farmland_is_hydrated(int32_t world_x, int32_t world_y,
		int32_t world_z, uint8_t horizontal_radius = 4U) const;
	bool has_water_source_near(int32_t world_x, int32_t soil_y,
		int32_t world_z, uint8_t horizontal_radius = 4U) const;
	/* Records a known block state for the next publication of this world cell.
	 * Used by recovery before the target chunk has streamed in. */
	int32_t set_block_state_override(int32_t world_x, int32_t world_y,
		int32_t world_z, uint32_t block_id) noexcept;
	int32_t serialize_fluid_state(std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t deserialize_fluid_state(const uint8_t *bytes,
		std::size_t byte_count) noexcept;
	int32_t apply_fluid_state_to_chunk(WorldChunk &chunk,
		bool *changed_out = nullptr) noexcept;
	int32_t apply_authoritative_block_change(
		const game_block_change_request &request,
		game_block_delta *delta_out);
	void advance_tick();
	int32_t undo_last_edit();
	int32_t redo_last_edit();
	int32_t raycast_solid(double origin_x, double origin_y, double origin_z,
		double direction_x, double direction_y, double direction_z,
		double max_distance, int32_t *block_x, int32_t *block_y,
		int32_t *block_z) const;
	int32_t raycast_edit_target(double origin_x, double origin_y,
		double origin_z, double direction_x, double direction_y,
		double direction_z, double max_distance, int32_t *hit_block_x,
		int32_t *hit_block_y, int32_t *hit_block_z, int32_t *place_block_x,
		int32_t *place_block_y, int32_t *place_block_z,
		uint32_t *hit_block_id) const;
	const WorldChunk *find_chunk(int32_t chunk_x, int32_t chunk_z) const;
	WorldChunk *find_chunk_mutable(int32_t chunk_x, int32_t chunk_z);
	void register_chunk_index(const WorldChunk &chunk);
	int32_t capture_remesh_snapshot(int32_t chunk_x, int32_t chunk_z,
		WorldGenerationPipeline::WorldChunkSnapshot &snapshot) const noexcept;
	bool remesh_capture_is_current(int32_t chunk_x, int32_t chunk_z,
		uint64_t request_id, uint64_t voxel_revision,
		uint64_t light_revision, uint16_t content_version,
		uint16_t light_input_version) const noexcept;
	void clear_pending_remesh(int32_t chunk_x, int32_t chunk_z,
		uint64_t request_id) noexcept;
	/* WorldChunkStreamer::update() runs while World already owns the write
	 * lock.  This helper is restricted to that call boundary. */
	void clear_pending_remesh_unlocked(int32_t chunk_x, int32_t chunk_z,
		uint64_t request_id) noexcept;

	int32_t begin_world_revision(const voxel_generation_config &config,
		RegenerationMode mode);
	int32_t begin_world_revision(const voxel_generation_config &config,
		const RegenerationWorldRules &rules, RegenerationMode mode);
	int32_t update_pending_revision_rules(
		const RegenerationWorldRules &rules);
	int32_t cancel_world_revision();
	WorldRevision world_revision() const;
	int32_t copy_selected_revision_chunks(
		std::vector<RevisionChunkCoordinate> &chunks_out) const noexcept;
	int32_t revision_feature_reservation_upper_bound(
		uint32_t *count_out) const noexcept;
	int32_t copy_selected_paid_revision_chunks(
		std::vector<RevisionChunkCoordinate> &chunks_out) const noexcept;
	int32_t select_revision_chunk(int32_t chunk_x, int32_t chunk_z,
		bool selected);
	int32_t update_revision_chunk_selection(uint64_t expected_selection_revision,
		uint32_t maximum_selected_chunks, int32_t chunk_x, int32_t chunk_z,
		bool selected, uint64_t *current_selection_revision);
	int32_t update_revision_chunk_selection(
		uint64_t expected_selection_revision, int32_t chunk_x, int32_t chunk_z,
		bool selected, uint64_t *current_selection_revision);
	/* Trusted host effect path; client selection messages cannot mark bonuses. */
	int32_t select_revision_bonus_chunk(uint64_t expected_selection_revision,
		uint32_t maximum_selected_chunks, int32_t chunk_x, int32_t chunk_z,
		uint64_t *current_selection_revision);
	int32_t validate_revision_bonus_chunk(
		uint64_t expected_selection_revision,
		uint32_t maximum_selected_chunks, int32_t chunk_x,
		int32_t chunk_z) const;
	int32_t unselect_revision_chunk(int32_t chunk_x, int32_t chunk_z);
	int32_t configure_revision_cost_policy(uint64_t expected_policy_revision,
		const WorldRevisionCostPolicy &policy,
		uint64_t *current_policy_revision);
	int32_t query_revision_start_cost(uint64_t expected_selection_revision,
		uint64_t expected_policy_revision, uint32_t opening_card_count,
		WorldRevisionCostQuote *quote) const;
	int32_t query_revision_start_cost(uint32_t opening_card_count,
		const WorldRevisionCostPolicy &policy,
		WorldRevisionCostQuote *quote) const;
	int32_t query_revision_start_cost(uint64_t expected_selection_revision,
		uint32_t opening_card_count, const WorldRevisionCostPolicy &policy,
		WorldRevisionCostQuote *quote) const;
	int32_t update_revision_chunk_protection(
		uint64_t expected_selection_revision, int32_t chunk_x, int32_t chunk_z,
		bool protected_state, uint64_t *current_selection_revision);
	int32_t set_revision_occupied_chunks(
		const std::vector<RevisionChunkCoordinate> &occupied_chunks,
		uint64_t *current_selection_revision);
	/* Host activates barriers after a committed World Loom start. They block
	 * movement into reserved chunks until successful regeneration completes. */
	int32_t set_revision_barriers_active(bool active) noexcept;
	bool revision_barriers_active() const noexcept;
	int32_t set_revision_decision_deadline_epoch(
		uint64_t deadline_epoch) noexcept;
	int32_t clear_revision_decision_deadline_epoch() noexcept;
	uint64_t revision_decision_deadline_epoch() const noexcept;
	bool revision_barrier_blocks_chunk(int32_t chunk_x,
		int32_t chunk_z) const noexcept;
	/* Host-owned chunk anchor; its chunk and eight neighbors are protected.
	 * Prefer configure_loom_station_block for authenticated access checks. */
	int32_t configure_loom_station(uint64_t expected_selection_revision,
		int32_t chunk_x, int32_t chunk_z,
		uint64_t *current_selection_revision);
	int32_t configure_loom_station_block(uint64_t expected_selection_revision,
		int32_t block_x, int32_t block_y, int32_t block_z,
		uint32_t block_id, uint64_t *current_selection_revision);
	bool has_loom_station() const noexcept;
	int32_t get_loom_station_chunk(int32_t *chunk_x_out,
		int32_t *chunk_z_out) const noexcept;
	int32_t get_loom_station_block(bool *configured_out,
		int32_t *block_x_out, int32_t *block_y_out, int32_t *block_z_out,
		uint32_t *block_id_out) const noexcept;
	int32_t invalidate_loom_station() noexcept;
	int32_t invalidate_loom_station_if_block_changed(int32_t block_x,
		int32_t block_y, int32_t block_z, uint32_t new_block_id) noexcept;
	int32_t set_chunk_protected(int32_t chunk_x, int32_t chunk_z,
		bool protected_state);
	bool is_chunk_protected(int32_t chunk_x, int32_t chunk_z) const;
	int32_t get_revision_protection_info(int32_t chunk_x, int32_t chunk_z,
		RevisionProtectionInfo *info_out) const noexcept;
	ChunkRevisionState revision_state(int32_t chunk_x, int32_t chunk_z) const;
	bool revision_chunk_is_published(int32_t chunk_x,
		int32_t chunk_z) const noexcept;
	int32_t build_revision_preview(int32_t preview_center_x,
		int32_t preview_center_z, int32_t radius,
		std::vector<RevisionPreviewEntry> &preview) const;
	int32_t start_revision_regeneration();
	int32_t load_selected_revision_chunks(uint32_t load_budget,
		bool *ready_out) noexcept;
	int32_t set_revision_generation_requested(bool requested) noexcept;
	bool revision_generation_requested() const noexcept;
	bool revision_replaying_completed_generation() const noexcept;
	bool revision_completed_rules_pending() const noexcept;
	int32_t rehydrate_completed_revision_rules(
		const RegenerationWorldRules &rules) noexcept;
	/* Trusted host recovery path; retries the still-reserved plan after a
	 * failed generation attempt while retaining its movement barrier. */
	int32_t retry_failed_revision_regeneration();
	int32_t regenerate_selected_chunks(int32_t *regenerated_count,
		int32_t *skipped_count);
	int32_t apply_revision_request(const RevisionRequest &request,
		RevisionRequestResult *result);
	int32_t save_revision_metadata(const char *file_path) const;
	int32_t load_revision_metadata(const char *file_path);
	int32_t serialize_revision_metadata(std::vector<uint8_t> &bytes_out) const;
	int32_t deserialize_revision_metadata(const uint8_t *bytes,
		std::size_t byte_count);
	int32_t restore_committed_revision(
		const RegenerationWorldRules &rules);
	int32_t configure_chunk_instability(
		const WorldChunkInstabilityPolicy &policy) noexcept;
	bool is_chunk_instability_configured() const noexcept;
	int32_t serialize_chunk_instability(
		std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t deserialize_chunk_instability(const uint8_t *bytes,
		std::size_t byte_count) noexcept;
	int32_t get_chunk_instability(int32_t chunk_x, int32_t chunk_z,
		WorldChunkInstabilityRecord *record_out) const noexcept;
	int32_t get_chunk_instability_public_view(int32_t chunk_x,
		int32_t chunk_z,
		WorldChunkInstabilityPublicView *view_out) const noexcept;
	int32_t ensure_chunk_instability_record(int32_t chunk_x,
		int32_t chunk_z) noexcept;
	int32_t adjust_chunk_instability(uint64_t expected_record_revision,
		int32_t chunk_x, int32_t chunk_z, int32_t delta,
		WorldChunkInstabilityRecord *record_out) noexcept;
	int32_t add_chunk_permanent_modifier(uint64_t expected_record_revision,
		int32_t chunk_x, int32_t chunk_z, uint32_t modifier_id,
		uint32_t modifier_version,
		WorldChunkInstabilityRecord *record_out) noexcept;
	int32_t add_chunk_feature_outcome(uint64_t expected_record_revision,
		int32_t chunk_x, int32_t chunk_z, uint32_t outcome_id,
		uint32_t outcome_version,
		WorldChunkInstabilityRecord *record_out) noexcept;
	int32_t record_chunk_generation(uint64_t expected_record_revision,
		int32_t chunk_x, int32_t chunk_z, uint64_t generation_revision,
		uint64_t rules_hash,
		WorldChunkInstabilityRecord *record_out,
		const std::vector<WorldChunkInstabilityRecord::VersionedId>
			*feature_outcomes = nullptr) noexcept;
	int32_t restore_chunk_instability_record(uint64_t expected_current_revision,
		const WorldChunkInstabilityRecord &previous_record) noexcept;
	int32_t save_chunk_instability(const char *file_path) const noexcept;
	int32_t load_chunk_instability(const char *file_path) noexcept;
	int32_t set_generation_checkpoint_callback(
		WorldGenerationCheckpointCallback callback, void *user_data) noexcept;
	int32_t checkpoint_generation_commit() noexcept;
	/* GameSession remains the persistence owner; World borrows these stores so
	 * generation commits can publish structure-authored spawners. */
	int32_t attach_entity_stores(WorldEntityStore &entities,
		WorldSpawnerStore &spawners, WorldItemStore &items,
		MinecraftItemCatalog &item_catalog) noexcept
	{
		if (!this->lifecycle_active_ || this->entity_store_ != nullptr
			|| this->spawner_store_ != nullptr || this->item_store_ != nullptr
			|| this->item_catalog_ != nullptr)
			return (FT_ERR_INVALID_OPERATION);
		this->entity_store_ = &entities;
		this->spawner_store_ = &spawners;
		this->item_store_ = &items;
		this->item_catalog_ = &item_catalog;
		return (FT_ERR_SUCCESS);
	}
	void detach_entity_stores() noexcept
	{
		this->entity_store_ = nullptr;
		this->spawner_store_ = nullptr;
		this->item_store_ = nullptr;
		this->item_catalog_ = nullptr;
	}
	WorldEntityStore *entity_store() noexcept
	{
		return (this->entity_store_);
	}
	WorldSpawnerStore *spawner_store() noexcept
	{
		return (this->spawner_store_);
	}
	WorldItemStore *item_store() noexcept { return (this->item_store_); }
	const WorldItemStore *item_store() const noexcept
	{
		return (this->item_store_);
	}
	MinecraftItemCatalog *item_catalog() noexcept { return (this->item_catalog_); }
	WorldTerrainChunkStore &terrain_chunk_store() noexcept
	{
		return (this->terrain_chunk_store_);
	}

  private:
	friend class WorldEditHistory;
    struct StreamCandidate
    {
        int32_t offset_x;
        int32_t offset_z;
        int32_t dist_sq;
        uint8_t state;
        uint32_t retry_count;
        int32_t retry_frames;
        int32_t last_error;
        uint64_t relevance_epoch;
        uint32_t generation_revision;
        uint64_t queued_frame;
        uint64_t request_id;
    };

    std::vector<StreamCandidate> stream_candidates_;
    int32_t stream_candidates_radius_;
    size_t stream_candidate_cursor_;
    int32_t generation_credit_;
    int32_t stream_last_error_;
    int32_t stream_retryable_count_;
    uint64_t stream_relevance_epoch_;
    uint32_t generation_revision_;
    uint64_t stream_frame_;
    uint64_t stream_progress_frame_;
    uint64_t world_epoch_;
    uint64_t next_request_id_;
    WorldGenerationPipeline generation_pipeline_;

    struct RevisionChunk
    {
        int32_t chunk_x;
        int32_t chunk_z;
    };
    bool revision_pending_;
    uint32_t world_revision_id_;
    uint32_t revision_stage_mask_;
    RegenerationMode revision_mode_;
    voxel_generation_config revision_config_;
    std::vector<RevisionChunk> revision_selected_;
    std::vector<RevisionChunk> revision_manual_protected_;
    std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> deferred_edits_;
    bool revision_regeneration_active_;
    uint64_t revision_generation_epoch_;
    std::size_t revision_job_count_;
    std::size_t revision_completed_job_count_;
    int32_t revision_regenerated_count_;
    int32_t revision_skipped_count_;
    int32_t revision_generation_error_;
    mutable std::shared_mutex world_data_mutex_;
	WorldChunkInstabilityStore chunk_instability_store_;
	WorldTerrainChunkStore terrain_chunk_store_;
	WorldEntityStore *entity_store_;
	WorldSpawnerStore *spawner_store_;
	WorldItemStore *item_store_;
	MinecraftItemCatalog *item_catalog_;
	WorldGenerationCheckpointCallback generation_checkpoint_callback_;
	void *generation_checkpoint_user_data_;
	struct FluidCellOverride
	{
		int32_t world_x;
		int32_t world_y;
		int32_t world_z;
		uint32_t block_id;
	};
	std::vector<FluidCellOverride> fluid_cell_overrides_;

    void copy_seed(const char *seed_value);
    int32_t initialize_internal(const char *seed_value,
		const char *voxel_config_file_path, const uint8_t *world_id);
    int32_t seed_first_chunk_and_stream();
    void clear_chunk_index();
    void rebuild_chunk_index();
    int32_t try_load_chunk_at(int32_t chunk_x, int32_t chunk_z);
    int32_t stream_chunks_sync(int32_t stream_radius, int32_t budget, int32_t *generated);
    int32_t stream_chunks_async(int32_t stream_radius, int32_t budget, int32_t *generated);
    int32_t drain_generation_results() noexcept;
    int32_t commit_generation_result(
        std::unique_ptr<WorldGenerationPipeline::Result> &result) noexcept;
    int32_t queue_chunk_remesh(WorldChunk &chunk);
    int32_t queue_neighbor_remeshes(int32_t chunk_x, int32_t chunk_z);
    StreamCandidate *find_stream_candidate(int32_t chunk_x, int32_t chunk_z);
    int32_t apply_deferred_edits(
        const std::chrono::steady_clock::time_point &deadline);
    int32_t finish_revision_regeneration();
    void prepare_stream_candidates(int32_t stream_radius);
    static uint32_t stage_mask_for_mode(RegenerationMode mode);
    static bool revision_contains(const std::vector<RevisionChunk> &list,
                                  int32_t chunk_x, int32_t chunk_z);
    static void revision_set(std::vector<RevisionChunk> &list,
                             int32_t chunk_x, int32_t chunk_z, bool enabled);
    int32_t regenerate_chunk_for_revision(WorldChunk &chunk);
    void blend_transition_boundary(WorldChunk &chunk);
};

# include "../../src/world/WorldChunkStreamer.hpp"
# include "../../src/world/WorldRevisionManager.hpp"

#endif
