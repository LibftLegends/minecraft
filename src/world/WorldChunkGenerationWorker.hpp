#ifndef WORLD_CHUNK_GENERATION_WORKER_HPP
# define WORLD_CHUNK_GENERATION_WORKER_HPP

# include "../../src/world/WorldGenerationPipeline.hpp"

class WorldChunkGenerationWorker
{
  public:
	WorldChunkGenerationWorker();
	WorldChunkGenerationWorker(const WorldChunkGenerationWorker &other);
	~WorldChunkGenerationWorker();
	WorldChunkGenerationWorker &operator=(const WorldChunkGenerationWorker &other);

	static std::unique_ptr<WorldGenerationPipeline::Result> process_generation(WorldGenerationPipeline::Request &request) noexcept;
	static std::unique_ptr<WorldGenerationPipeline::Result> process_remesh(WorldGenerationPipeline::Request &request) noexcept;

  private:
	static int32_t initialize_chunk_for_generation(WorldChunk &chunk,
		int32_t chunk_x, int32_t chunk_z, const char *seed,
		voxel_generation_config &config, uint32_t stage_mask,
		std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> &deferred_edits,
		const WorldGenerationPipeline::WorldChunkSnapshot *source_snapshot) noexcept;
	static std::unique_ptr<WorldGenerationPipeline::Result> finish_generation_result(std::unique_ptr<WorldGenerationPipeline::Result> result,
		WorldGenerationPipeline::Request &request) noexcept;
};

# include "../../src/world/WorldChunkSnapshotReader.hpp"

#endif
