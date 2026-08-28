#ifndef WORLD_HPP
#define WORLD_HPP

#include "../ft_vox.hpp"
#include "../../src/chunks/WorldChunk.hpp"
#include "../../src/chunks/WorldChunkLoader.hpp"
#include "../../src/chunks/WorldChunkStore.hpp"
#include "../../src/coordinates/WorldCoordinates.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include "../../Libft/Modules/Voxel/terrain_api.hpp"
#include "../../src/queries/WorldBlockQuery.hpp"
#include "../../src/edits/WorldBlockEditor.hpp"
#include "../../src/queries/WorldRaycaster.hpp"
#include "../../src/validators/WorldVisibilityValidator.hpp"
#include "../../src/world/WorldGenerationPipeline.hpp"
#include <chrono>

class World
{
  public:
    enum RegenerationMode
    {
        REGEN_DECORATION_REFRESH = 0,
        REGEN_UNDERGROUND_REFRESH = 1,
        REGEN_TERRAIN_RESHAPING = 2,
        REGEN_FULL = 3
    };

    enum ChunkRevisionState
    {
        REVISION_UNCHANGED = 0,
        REVISION_PROTECTED = 1,
        REVISION_SELECTED = 2,
        REVISION_TRANSITION = 3
    };

    struct WorldRevision
    {
        uint32_t identifier;
        uint32_t stage_mask;
        RegenerationMode mode;
        bool pending;
        bool regenerating;
        size_t selected_count;
        size_t manually_protected_count;
    };

    struct RevisionPreviewEntry
    {
        int32_t chunk_x;
        int32_t chunk_z;
        ChunkRevisionState state;
    };

    struct RevisionChunkCoordinate
    {
        int32_t chunk_x;
        int32_t chunk_z;
    };

    struct RevisionRequest
    {
        terrain_generation_config config;
        RegenerationMode mode;
        uint32_t stage_mask;
        std::vector<RevisionChunkCoordinate> selected_chunks;
        std::vector<RevisionChunkCoordinate> protected_chunks;
    };

    struct RevisionRequestResult
    {
        uint32_t revision_identifier;
        uint32_t stage_mask;
        int32_t regenerated_count;
        int32_t skipped_count;
    };

    struct StreamDiagnostics
    {
        uint64_t frame;
        uint64_t progress_frame;
        size_t candidate_count;
        size_t ready_count;
        size_t pending_count;
        size_t retryable_count;
        size_t failed_count;
        uint64_t oldest_pending_age;
        int32_t last_error;
    };

    WorldChunk chunks[WorldCoordinates::CHUNK_COUNT];
    WorldChunk *chunk_index[WorldCoordinates::CHUNK_COUNT];
    int32_t chunk_index_center_x;
    int32_t chunk_index_center_z;
    bool chunk_index_valid;
    int32_t chunk_count;
    int32_t loaded_chunk_count;
    int32_t center_chunk_x;
    int32_t center_chunk_z;
    int32_t active_render_distance;
    char seed[128];
    terrain_generation_config terrain_config;
    terrain_generation_context terrain_context;
    bool terrain_generation_started;

    World();
    World(const World &other);
    ~World();
    World &operator=(const World &other);

    int32_t initialize(const char *seed_value);
    int32_t initialize(const char *seed_value,
                       const char *terrain_config_file_path);
    void set_terrain_config(const terrain_generation_config &config);
    const terrain_generation_config &terrain_generation_settings() const;
    int32_t load_terrain_config(const char *file_path);
    int32_t save_terrain_config(const char *file_path) const;
    void destroy();
    int32_t update_around(double camera_x, double camera_z, int32_t generation_budget);
    int32_t update_around(double camera_x, double camera_z, int32_t generation_budget,
                          int32_t render_distance);
    int32_t stream_last_error() const;
    int32_t stream_retryable_count() const;
    StreamDiagnostics stream_diagnostics() const;
    bool validate_visible_distance(double camera_x, double camera_z, double yaw,
                                   int32_t required_distance) const;
    bool surface_top_at(int32_t world_x, int32_t world_z, double *surface_top) const;
    bool solid_block_at(int32_t world_x, int32_t world_y, int32_t world_z) const;
    bool block_id_at(int32_t world_x, int32_t world_y, int32_t world_z, uint32_t *block_id) const;
    int32_t delete_block_at(int32_t world_x, int32_t world_y, int32_t world_z);
    int32_t place_block_at(int32_t world_x, int32_t world_y, int32_t world_z, uint32_t block_id);
    int32_t begin_world_revision(const terrain_generation_config &config,
                                 RegenerationMode mode);
    int32_t cancel_world_revision();
    WorldRevision world_revision() const;
    int32_t select_revision_chunk(int32_t chunk_x, int32_t chunk_z, bool selected);
    int32_t set_chunk_protected(int32_t chunk_x, int32_t chunk_z, bool protected_state);
    bool is_chunk_protected(int32_t chunk_x, int32_t chunk_z) const;
    ChunkRevisionState revision_state(int32_t chunk_x, int32_t chunk_z) const;
    int32_t build_revision_preview(int32_t preview_center_x, int32_t preview_center_z,
                                   int32_t radius,
                                   std::vector<RevisionPreviewEntry> &preview) const;
    int32_t regenerate_selected_chunks(int32_t *regenerated_count,
                                       int32_t *skipped_count);
    int32_t start_revision_regeneration();
    int32_t apply_revision_request(const RevisionRequest &request,
                                   RevisionRequestResult *result);
    int32_t save_revision_metadata(const char *file_path) const;
    int32_t load_revision_metadata(const char *file_path);
    int32_t raycast_solid(double origin_x, double origin_y, double origin_z, double direction_x,
                          double direction_y, double direction_z, double max_distance,
                          int32_t *block_x, int32_t *block_y, int32_t *block_z) const;
    int32_t raycast_edit_target(double origin_x, double origin_y, double origin_z,
                                double direction_x, double direction_y, double direction_z,
                                double max_distance, int32_t *hit_block_x, int32_t *hit_block_y,
                                int32_t *hit_block_z, int32_t *place_block_x,
                                int32_t *place_block_y, int32_t *place_block_z,
                                uint32_t *hit_block_id) const;
    const WorldChunk *find_chunk(int32_t chunk_x, int32_t chunk_z) const;
    WorldChunk *find_chunk_mutable(int32_t chunk_x, int32_t chunk_z);

  private:
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
    terrain_generation_config revision_config_;
    std::vector<RevisionChunk> revision_selected_;
    std::vector<RevisionChunk> revision_manual_protected_;
    std::vector<WorldDeferredBlockEdit> deferred_edits_;
    bool revision_regeneration_active_;
    uint64_t revision_generation_epoch_;
    std::size_t revision_job_count_;
    std::size_t revision_completed_job_count_;
    int32_t revision_regenerated_count_;
    int32_t revision_skipped_count_;
    int32_t revision_generation_error_;

    void copy_seed(const char *seed_value);
    void clear_chunk_index();
    void rebuild_chunk_index();
    void register_chunk_index(const WorldChunk &chunk);
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

#endif
