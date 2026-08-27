#ifndef WORLD_GENERATION_RESULT_COMMITTER_HPP
# define WORLD_GENERATION_RESULT_COMMITTER_HPP

# include "../../src/world/WorldChunkStreamer.hpp"

class WorldGenerationResultCommitter
{
  public:
	WorldGenerationResultCommitter();
	WorldGenerationResultCommitter(const WorldGenerationResultCommitter &other);
	~WorldGenerationResultCommitter();
	WorldGenerationResultCommitter &operator=(const WorldGenerationResultCommitter &other);

	static int32_t drain(WorldChunkStreamer &streamer, World &world) noexcept;
	static int32_t move_mesh(chunk_mesh &destination,
		chunk_mesh &source) noexcept;

  private:
	static int32_t commit(WorldChunkStreamer &streamer, World &world,
		WorldGenerationPipeline::Result &result) noexcept;
	static int32_t commit_remesh_result(World &world,
		WorldGenerationPipeline::Result &result) noexcept;
	static int32_t commit_stream_result(WorldChunkStreamer &streamer,
		World &world, WorldGenerationPipeline::Result &result) noexcept;
	static int32_t create_chunk_from_stream_result(WorldChunkStreamer &streamer,
		World &world, WorldGenerationPipeline::Result &result,
		WorldChunkStreamer::StreamCandidate &candidate) noexcept;
	static void populate_chunk_slot(WorldChunk &slot,
		const WorldGenerationPipeline::Result &result) noexcept;
};

# include "../../src/world/WorldDeferredEditApplier.hpp"
# include "../../src/world/WorldRegenerationResultApplier.hpp"

#endif
