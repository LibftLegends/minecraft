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

	static std::unique_ptr<WorldGenerationPipeline::Result> process_generation(WorldGenerationPipeline::Request &request) noexcept;
	static std::unique_ptr<WorldGenerationPipeline::Result> process_remesh(WorldGenerationPipeline::Request &request) noexcept;
	/* Apply deterministic planned structures to a generated or synthetic chunk.
	 * All writes stay within that chunk's owned footprint. */
	static int32_t apply_feature_plans(WorldChunk &chunk,
		const std::vector<WorldGenerationPipeline::GenerationFeaturePlan>
			&feature_plans) noexcept;

  private:
	static int32_t initialize_chunk_for_generation(WorldChunk &chunk,
		int32_t chunk_x, int32_t chunk_z, const char *seed,
		voxel_generation_config &config, uint32_t stage_mask,
		std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> &deferred_edits,
		const std::vector<WorldGenerationPipeline::GenerationFeaturePlan>
			&feature_plans,
		const WorldGenerationPipeline::WorldChunkSnapshot *source_snapshot,
		uint64_t *generation_duration_nanoseconds,
		uint64_t *mesh_duration_nanoseconds) noexcept;
	static std::unique_ptr<WorldGenerationPipeline::Result> finish_generation_result(std::unique_ptr<WorldGenerationPipeline::Result> result,
		WorldGenerationPipeline::Request &request) noexcept;
};

# include "../../src/world/WorldChunkSnapshotReader.hpp"

#endif
