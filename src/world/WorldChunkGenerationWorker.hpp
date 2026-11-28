#ifndef WORLD_CHUNK_GENERATION_WORKER_HPP
# define WORLD_CHUNK_GENERATION_WORKER_HPP

# include "../../src/world/WorldGenerationPipeline.hpp"

class WorldChunkGenerationWorker
{
  public:
	/*
	 * These functions are pure request consumers.  They may run on a long-
	 * lived worker thread and must never acquire World::world_data_mutex_,
	 * query World, or retain a live WorldChunk pointer.  World access belongs
	 * to the snapshot-capture/commit boundaries outside this class.
	 */
	WorldChunkGenerationWorker();
	WorldChunkGenerationWorker(const WorldChunkGenerationWorker &other);
	~WorldChunkGenerationWorker();
	WorldChunkGenerationWorker &operator=(const WorldChunkGenerationWorker &other);

	static std::unique_ptr<WorldGenerationPipeline::Result> process_generation(
		WorldGenerationPipeline::Request &request, const std::atomic<uint64_t> &stream_epoch) noexcept;
	static std::unique_ptr<WorldGenerationPipeline::Result> process_remesh(WorldGenerationPipeline::Request &request) noexcept;
	/* Apply deterministic planned structures to a generated or synthetic chunk.
	 * All writes stay within that chunk's owned footprint. */
	static int32_t apply_feature_plans(WorldChunk &chunk,
		const std::vector<WorldGenerationPipeline::GenerationFeaturePlan>
			&feature_plans) noexcept;

  private:
	static int32_t initialize_chunk_for_generation(WorldGenerationPipeline::Result &result,
		const WorldGenerationPipeline::Request &request,
		const std::atomic<uint64_t> &stream_epoch) noexcept;
	static std::unique_ptr<WorldGenerationPipeline::Result> finish_generation_result(std::unique_ptr<WorldGenerationPipeline::Result> result,
		WorldGenerationPipeline::Request &request) noexcept;
};

# include "../../src/world/WorldChunkSnapshotReader.hpp"

#endif
