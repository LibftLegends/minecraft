#ifndef WORLD_CHUNK_STREAMER_HPP
# define WORLD_CHUNK_STREAMER_HPP

# include "../../src/world/WorldGenerationPipeline.hpp"

class						World;
class						WorldChunk;

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
	std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> deferred_edits_;
	std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> deferred_pending_edits_;
	std::vector<WorldChunk *> deferred_touched_chunks_;
	bool deferred_edits_sorted_ = false;
	std::size_t deferred_apply_cursor_ = 0U;
	std::size_t deferred_sorted_end_ = 0U;
	WorldGenerationPipeline	generation_pipeline_;
	int32_t					dirty_remesh_cursor_ = 0;

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
	Diagnostics diagnostics() const noexcept;

	uint64_t allocate_request_id() noexcept;
	uint64_t world_epoch() const noexcept;
	uint32_t generation_revision() const noexcept;
	void bump_generation_revision() noexcept;
	WorldGenerationPipeline &pipeline() noexcept;
	void invalidate_non_ready_candidates() noexcept;
	void reset_candidates_after_regeneration() noexcept;
	int32_t queue_chunk_remesh(WorldChunk &chunk) noexcept;
	void mark_neighbor_remeshes(int32_t chunk_x, int32_t chunk_z) noexcept;
	int32_t queue_neighbor_remeshes(int32_t chunk_x, int32_t chunk_z) noexcept;

  private:
	int32_t					generation_credit_ = 0;

	void handle_recenter() noexcept;
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
