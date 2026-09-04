#ifndef WORLD_GENERATION_PIPELINE_HPP
# define WORLD_GENERATION_PIPELINE_HPP

# include "../../Libft/Modules/Voxel/voxel_api.hpp"
# include "../../src/chunks/WorldChunk.hpp"
# include <atomic>
# include <condition_variable>
# include <cstdint>
# include <deque>
# include <functional>
# include <memory>
# include <mutex>
# include <new>
# include <string>
# include <thread>
# include <vector>

class WorldGenerationPipeline
{
  public:
	static constexpr int32_t LIGHT_SNAPSHOT_HALO = 15;
	enum class WorldGenerationOperation : uint8_t
	{
		STREAM = 0,
		REMESH = 1,
		REGENERATE = 2
	};

	struct							WorldDeferredBlockEdit
	{
		int32_t						world_x;
		int32_t						world_y;
		int32_t						world_z;
		uint32_t					block_id;
		uint64_t					request_id;
		uint64_t					sequence;
	};

	struct							WorldChunkSnapshot
	{
		int32_t						chunk_x;
		int32_t						chunk_z;
		std::vector<uint32_t> blocks;
		std::vector<uint32_t> west_border;
		std::vector<uint32_t> east_border;
		std::vector<uint32_t> north_border;
		std::vector<uint32_t> south_border;
		std::vector<uint32_t> lighting_blocks;
	};

	struct							Result
	{
		uint64_t					request_id;
		uint64_t					world_epoch;
		uint64_t					relevance_epoch;
		uint32_t					generation_revision;
		uint32_t					configuration_signature;
		uint32_t					stage_mask;
		uint64_t					voxel_revision;
		uint64_t					completed_at_nanoseconds;
		uint64_t					generation_duration_nanoseconds;
		uint64_t					mesh_duration_nanoseconds;
		int32_t						chunk_x;
		int32_t						chunk_z;
		WorldGenerationOperation	operation;
		int32_t						error_code;
		std::unique_ptr<WorldChunk> chunk;
		std::unique_ptr<chunk_mesh> mesh;
		std::vector<WorldDeferredBlockEdit> deferred_edits;
	};

	struct							Request
	{
		uint64_t					request_id;
		uint64_t					cancellation_epoch;
		uint64_t					world_epoch;
		uint64_t					relevance_epoch;
		uint32_t					generation_revision;
		uint32_t					configuration_signature;
		uint32_t					stage_mask;
		uint64_t					voxel_revision;
		int32_t						chunk_x;
		int32_t						chunk_z;
		WorldGenerationOperation	operation;
		std::string seed;
		voxel_generation_config	config;
		std::unique_ptr<WorldChunkSnapshot> snapshot;
		std::vector<WorldDeferredBlockEdit> deferred_edits;
	};

	WorldGenerationPipeline() noexcept;
	WorldGenerationPipeline(const WorldGenerationPipeline &other) noexcept;
	~WorldGenerationPipeline() noexcept;
	WorldGenerationPipeline &operator=(const WorldGenerationPipeline &other) noexcept;

	int32_t initialize(std::size_t worker_count,
		std::size_t maximum_queued) noexcept;
	int32_t destroy() noexcept;
	int32_t submit_generation(uint64_t request_id, uint64_t world_epoch,
		uint64_t relevance_epoch, uint32_t generation_revision, int32_t chunk_x,
		int32_t chunk_z, const char *seed,
		const voxel_generation_config &config, uint32_t stage_mask,
		WorldGenerationOperation operation,
		const WorldChunkSnapshot *source_snapshot = nullptr) noexcept;
	int32_t submit_remesh(uint64_t request_id, uint64_t world_epoch,
		uint64_t relevance_epoch, uint32_t generation_revision, int32_t chunk_x,
		int32_t chunk_z, uint64_t voxel_revision,
		const WorldChunkSnapshot &snapshot) noexcept;
	int32_t poll(std::unique_ptr<Result> &result) noexcept;
	void retire_result(std::unique_ptr<Result> result) noexcept;
	int32_t capture_snapshot(const WorldChunk &target, const WorldChunk *west,
		const WorldChunk *east, const WorldChunk *north,
		const WorldChunk *south, WorldChunkSnapshot &snapshot) const noexcept;
	void cancel_queued() noexcept;
	std::size_t queued_count() const noexcept;
	std::size_t completed_count() const noexcept;
	std::size_t active_count() const noexcept;
	std::size_t remesh_in_flight_count() const noexcept;
	uint64_t oldest_completed_result_age_nanoseconds() const noexcept;
	bool is_initialized() const noexcept;

	std::deque<std::unique_ptr<Request>> requests_;
	std::deque<std::unique_ptr<Result>> results_;
	std::deque<std::unique_ptr<Result>> retired_results_;
	mutable std::mutex mutex_;
	mutable std::mutex results_mutex_;
	std::condition_variable condition_;
	std::atomic<uint64_t> pipeline_epoch_;
	std::atomic<std::size_t> remesh_in_flight_;
	std::atomic<std::size_t> active_requests_;
	std::atomic<bool>				stopping_;

  private:
	std::vector<std::thread> workers_;
	std::size_t maximum_queued_;
	bool							initialized_;

	static std::size_t resolve_worker_count(std::size_t worker_count) noexcept;
};

# include "../../src/world/WorldChunkGenerationWorker.hpp"
# include "../../src/world/WorldChunkSnapshotCapture.hpp"
# include "../../src/world/WorldGenerationRequestBuilder.hpp"
# include "../../src/world/WorldGenerationWorkerLoop.hpp"

#endif
