#ifndef WORLD_REGENERATION_RESULT_APPLIER_HPP
# define WORLD_REGENERATION_RESULT_APPLIER_HPP

# include "../../src/world/WorldGenerationResultCommitter.hpp"

class WorldRegenerationResultApplier
{
  public:
	WorldRegenerationResultApplier();
	WorldRegenerationResultApplier(const WorldRegenerationResultApplier &other);
	~WorldRegenerationResultApplier();
	WorldRegenerationResultApplier &operator=(const WorldRegenerationResultApplier &other);

	static int32_t commit(WorldChunkStreamer &streamer, World &world,
		WorldGenerationPipeline::Result &result) noexcept;

  private:
	static void apply_chunk(WorldChunkStreamer &streamer, World &world,
		WorldGenerationPipeline::Result &result) noexcept;
};

#endif
