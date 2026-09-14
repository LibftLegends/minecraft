#ifndef WORLD_CHUNK_STREAMER_HPP
# define WORLD_CHUNK_STREAMER_HPP

# include "../../src/world/WorldGenerationPipeline.hpp"
# include <atomic>
# include <condition_variable>
# include <mutex>
# include <thread>

class						World;
class						WorldChunk;
struct						WorldChunkReadState;

class WorldChunkStreamer
{
  public:
	struct					Diagnostics
	{
		std::size_t deferred_edit_count;
		std::size_t deferred_edit_cursor;
		uint64_t			frame;
		uint64_t			progress_frame;
		std::size_t candidate_count;
		std::size_t ready_count;
		std::size_t pending_count;
		std::size_t retryable_count;
		std::size_t failed_count;
		std::size_t playable_failed_count;
		std::size_t playable_required_count;
		std::size_t playable_drawable_count;
		std::size_t active_generation_count;
	std::size_t remesh_queue_peak;
	uint64_t remesh_starvation_promotions;
		std::size_t remesh_priority_queue_depth;
		std::size_t interactive_remesh_queue_depth;
		uint64_t oldest_remesh_queue_age;
		uint64_t remesh_snapshot_bytes;
		uint64_t remesh_capture_duration_nanoseconds;
		uint64_t remesh_capture_count;
		uint64_t remesh_scanned_cells;
		uint64_t remesh_propagated_cells;
		uint64_t remesh_light_queue_peak;
		uint64_t remesh_completed_count;
		uint64_t remesh_incremental_completed_count;
		uint64_t remesh_full_completed_count;
		uint64_t remesh_geometry_only_count;
		uint64_t remesh_canceled_count;
		std::size_t stale_result_count;
		std::size_t stale_stream_result_count;
		std::size_t stale_remesh_capture_count;
		std::size_t stale_remesh_result_count;
		std::size_t stale_remesh_dependency_count;
		std::size_t stale_remesh_pending_count;
		std::size_t stale_remesh_revision_count;
		uint64_t		oldest_result_age_nanoseconds;
		uint64_t			oldest_pending_age;
		int32_t				last_error;
	};

	struct					StreamCandidate
	{
		int32_t				offset_x;
		int32_t				offset_z;
		int32_t				dist_sq;
		uint8_t				state;
		uint32_t			retry_count;
		int32_t				retry_frames;
		int32_t				last_error;
		uint64_t			relevance_epoch;
		uint32_t			generation_revision;
		uint64_t			queued_frame;
		uint64_t			request_id;
	};

	static const uint8_t	CANDIDATE_ABSENT;
	static const uint8_t	CANDIDATE_QUEUED;
	static const uint8_t	CANDIDATE_GENERATING;
	static const uint8_t	CANDIDATE_GENERATED;
	static const uint8_t	CANDIDATE_MESHING;
	static const uint8_t	CANDIDATE_READY;
	static const uint8_t	CANDIDATE_FAILED_RETRYABLE;

	World &world_;
	std::vector<StreamCandidate> stream_candidates_;
	std::vector<int32_t> stream_candidate_lookup_;
	int32_t					stream_candidates_radius_ = -1;
	std::size_t stream_candidate_cursor_ = 0U;
	int32_t					stream_last_error_ = FT_ERR_SUCCESS;
	int32_t					stream_retryable_count_ = 0;
	uint64_t				stream_relevance_epoch_ = 1U;
	uint32_t				generation_revision_ = 1U;
	uint64_t				stream_frame_ = 0U;
	uint64_t				stream_progress_frame_ = 0U;
	uint64_t				world_epoch_ = 1U;
	uint64_t				next_request_id_ = 1U;
	voxel_light_update_config light_update_config_;
	voxel_light_update_config interactive_light_update_config_;
	std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> deferred_edits_;
	std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> deferred_pending_edits_;
	std::vector<WorldChunk *> deferred_touched_chunks_;
	struct RemeshPriority
	{
		int32_t chunk_x;
		int32_t chunk_z;
		int32_t origin_chunk_x;
		int32_t origin_chunk_z;
		uint32_t origin_distance;
		uint64_t queued_frame;
		bool interactive;
		bool geometry_only;
		ft_bool incremental_additive_light;
		ft_bool incremental_removal_light;
		int32_t incremental_local_x;
		int32_t incremental_local_y;
		int32_t incremental_local_z;
		uint32_t incremental_old_block_id;
		uint32_t incremental_new_block_id;
		uint64_t voxel_revision;
		uint16_t content_version;
		uint16_t light_input_version;
		std::vector<WorldGenerationPipeline::IncrementalLightSeed>
			incremental_light_seeds;
	};

	std::atomic<uint64_t> remesh_snapshot_bytes_ = 0U;
	std::atomic<uint64_t> remesh_capture_duration_nanoseconds_ = 0U;
	std::atomic<uint64_t> remesh_capture_count_ = 0U;
	uint64_t remesh_scanned_cells_ = 0U;
	uint64_t remesh_propagated_cells_ = 0U;
	uint64_t remesh_light_queue_peak_ = 0U;
	uint64_t remesh_completed_count_ = 0U;
	uint64_t remesh_incremental_completed_count_ = 0U;
	uint64_t remesh_full_completed_count_ = 0U;
	uint64_t remesh_geometry_only_count_ = 0U;
	uint64_t remesh_canceled_count_ = 0U;
	std::deque<RemeshPriority> priority_remeshes_;
	uint64_t remesh_starvation_promotions_ = 0U;
	bool deferred_edits_sorted_ = false;
	std::size_t deferred_apply_cursor_ = 0U;
	std::size_t deferred_sorted_end_ = 0U;
	WorldGenerationPipeline	generation_pipeline_;
	int32_t					dirty_remesh_cursor_ = 0;
	uint64_t					next_remesh_submission_frame_ = 0U;
		bool					priority_remesh_pending_ = false;
		int32_t					priority_remesh_chunk_x_ = 0;
		int32_t					priority_remesh_chunk_z_ = 0;
		bool					remesh_priority_anchor_valid_ = false;
		int32_t					remesh_priority_anchor_x_ = 0;
		int32_t					remesh_priority_anchor_z_ = 0;
		uint64_t				remesh_priority_anchor_expiry_frame_ = 0U;
		std::size_t			remesh_queue_peak_ = 0U;
		std::size_t			stale_result_count_ = 0U;
		std::size_t			stale_stream_result_count_ = 0U;
		std::size_t			stale_remesh_result_count_ = 0U;
	std::size_t stale_remesh_capture_count_ = 0U;
	std::size_t stale_remesh_dependency_count_ = 0U;
	std::size_t stale_remesh_pending_count_ = 0U;
	std::size_t stale_remesh_revision_count_ = 0U;
	/* A completed source result can be held briefly when a face-sharing chunk
	 * is explicitly waiting on it.  The committer then publishes the source and
	 * dependent result in one drain pass, while the bounded age fallback keeps a
	 * cancelled/missing dependency from stalling publication forever. */
	std::unique_ptr<WorldGenerationPipeline::Result>
		staged_border_source_result_;
	uint64_t staged_border_source_frame_ = 0U;
	struct RemeshCaptureTask
	{
		/*
		 * This is a capture request, not a lighting compute request.  The
		 * producer supplies immutable read-state handles while it still owns the
		 * live-world boundary.  The capture thread combines those handles into
		 * an owned WorldChunkSnapshot and releases them before submitting to the
		 * pipeline.  Neither this worker nor the pipeline workers receive World,
		 * a live WorldChunk pointer, or the world mutex.
		 */
		uint64_t request_id;
		uint64_t world_epoch;
		uint64_t relevance_epoch;
		uint32_t generation_revision;
		int32_t chunk_x;
		int32_t chunk_z;
		uint64_t voxel_revision;
		uint64_t light_revision;
		uint16_t content_version;
		uint16_t light_input_version;
		ft_bool interactive;
		bool geometry_only;
		ft_bool incremental_additive_light;
		ft_bool incremental_removal_light;
		int32_t incremental_local_x;
		int32_t incremental_local_y;
		int32_t incremental_local_z;
		uint32_t incremental_old_block_id;
		uint32_t incremental_new_block_id;
		std::vector<WorldGenerationPipeline::IncrementalLightSeed>
			incremental_light_seeds;
		std::shared_ptr<std::atomic<uint64_t>> cancellation_token;
		uint64_t cancellation_token_value;
		voxel_light_update_config light_update_config;
		ft_bool dependency_valid;
		int32_t dependency_chunk_x;
		int32_t dependency_chunk_z;
		std::shared_ptr<const WorldChunkReadState> read_states[9];
	};
	struct RemeshCaptureFailure
	{
		int32_t chunk_x;
		int32_t chunk_z;
		uint64_t request_id;
		int32_t error_code;
	};
	std::deque<RemeshCaptureTask> remesh_capture_tasks_;
	std::deque<RemeshCaptureFailure> remesh_capture_failures_;
	std::mutex remesh_capture_mutex_;
	std::condition_variable remesh_capture_condition_;
	std::thread remesh_capture_thread_;
	bool remesh_capture_stopping_ = false;
	/* A capture task is removed from remesh_capture_tasks_ before its snapshot
	 * is submitted to the generation pipeline.  Keep ownership visible during
	 * that handoff so orphan recovery cannot clear its pending marker. */
	uint64_t remesh_capture_processing_request_id_ = 0U;
	std::atomic<std::size_t> remesh_capture_in_flight_ = 0U;
	std::atomic<uint64_t> remesh_capture_relevance_epoch_ = 1U;

	WorldChunkStreamer(World &world);
	WorldChunkStreamer(const WorldChunkStreamer &other);
	~WorldChunkStreamer();
	WorldChunkStreamer &operator=(const WorldChunkStreamer &other);

	int32_t initialize_pipeline() noexcept;
	void reset() noexcept;
	int32_t seed_initial_stream(int32_t stream_radius, int32_t budget,
		int32_t *generated) noexcept;
	int32_t update(int32_t generation_budget, int32_t stream_radius,
		bool center_changed) noexcept;
	int32_t stream_last_error() const noexcept;
	int32_t stream_retryable_count() const noexcept;
	int32_t set_light_update_config(
		const voxel_light_update_config &config) noexcept;
	const voxel_light_update_config &light_update_config() const noexcept;
	int32_t set_interactive_light_update_config(
		const voxel_light_update_config &config) noexcept;
	const voxel_light_update_config &interactive_light_update_config() const noexcept;
	Diagnostics diagnostics() const noexcept;

	uint64_t allocate_request_id() noexcept;
	uint64_t world_epoch() const noexcept;
	uint32_t generation_revision() const noexcept;
	void bump_generation_revision() noexcept;
	WorldGenerationPipeline &pipeline() noexcept;
	const WorldGenerationPipeline &pipeline() const noexcept;
	/* Cancel remesh work together with the chunk-side request ownership
	 * markers.  The pipeline deliberately has no World access and cannot do
	 * this cleanup on its own. */
	void cancel_pending_remeshes() noexcept;
	void invalidate_non_ready_candidates() noexcept;
	void reset_candidates_after_regeneration() noexcept;
	int32_t queue_chunk_remesh(WorldChunk &chunk,
		ft_bool incremental_additive_light = FT_FALSE,
		ft_bool incremental_removal_light = FT_FALSE,
		int32_t incremental_local_x = 0, int32_t incremental_local_y = 0,
		int32_t incremental_local_z = 0,
		uint32_t incremental_old_block_id = GAME_VOXEL_AIR_BLOCK,
		uint32_t incremental_new_block_id = GAME_VOXEL_AIR_BLOCK,
		const std::vector<WorldGenerationPipeline::IncrementalLightSeed>
			*incremental_light_seeds = nullptr,
		bool geometry_only = false) noexcept;
	void mark_remesh_dirty(WorldChunk &chunk,
		ft_bool light_input_changed = FT_FALSE) noexcept;
	void mark_light_remesh_pending(WorldChunk &chunk) noexcept;
	void mark_neighbor_remeshes(int32_t chunk_x, int32_t chunk_z,
		bool incremental_edit = false, bool source_arrival = false) noexcept;
	void mark_edit_remeshes(int32_t chunk_x, int32_t chunk_z,
		int32_t local_x, int32_t local_y, int32_t local_z) noexcept;
	int32_t queue_neighbor_remeshes(int32_t chunk_x, int32_t chunk_z) noexcept;
	void prioritize_chunk_remesh(int32_t chunk_x, int32_t chunk_z) noexcept;
	void prioritize_chunk_remesh_from_origin(int32_t chunk_x,
		int32_t chunk_z, int32_t origin_chunk_x, int32_t origin_chunk_z,
		ft_bool incremental_additive_light = FT_FALSE,
		ft_bool incremental_removal_light = FT_FALSE,
		int32_t incremental_local_x = 0, int32_t incremental_local_y = 0,
		int32_t incremental_local_z = 0,
		uint32_t incremental_old_block_id = GAME_VOXEL_AIR_BLOCK,
		uint32_t incremental_new_block_id = GAME_VOXEL_AIR_BLOCK,
		uint8_t incremental_old_source_light = 0U,
		ft_bool incremental_external_source = FT_FALSE,
		bool geometry_only = false, bool interactive = true) noexcept;
	void prioritize_edit_border_remeshes(int32_t chunk_x, int32_t chunk_z,
		int32_t local_x, int32_t local_y, int32_t local_z,
		ft_bool incremental_additive_light = FT_FALSE,
		ft_bool incremental_removal_light = FT_FALSE,
		uint32_t incremental_old_block_id = GAME_VOXEL_AIR_BLOCK,
		uint32_t incremental_new_block_id = GAME_VOXEL_AIR_BLOCK,
		uint8_t incremental_old_source_light = 0U) noexcept;
	void enqueue_background_remesh(int32_t chunk_x, int32_t chunk_z) noexcept;
	void promote_starved_remeshes() noexcept;

  private:
	int32_t					generation_credit_ = 0;

	void handle_recenter() noexcept;
	int32_t start_remesh_capture_worker() noexcept;
	void stop_remesh_capture_worker() noexcept;
	void run_remesh_capture_worker() noexcept;
	void drain_remesh_capture_failures() noexcept;
	void recover_orphaned_remesh_markers() noexcept;
	int32_t stream_full_sync(int32_t stream_radius, int32_t generation_budget,
		int32_t *generated) noexcept;
	int32_t dispatch_incremental_stream(int32_t stream_radius,
		int32_t generation_budget, int32_t *generated) noexcept;
};

# include "../../src/world/World.hpp"
# include "../../src/world/WorldChunkCandidateScanner.hpp"
# include "../../src/world/WorldChunkStreamDiagnosticsBuilder.hpp"
# include "../../src/world/WorldGenerationResultCommitter.hpp"

#endif
