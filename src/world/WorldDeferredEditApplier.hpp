#ifndef WORLD_DEFERRED_EDIT_APPLIER_HPP
# define WORLD_DEFERRED_EDIT_APPLIER_HPP

# include "../../src/world/WorldChunkStreamer.hpp"

class WorldDeferredEditApplier
{
  public:
	WorldDeferredEditApplier();
	WorldDeferredEditApplier(const WorldDeferredEditApplier &other);
	~WorldDeferredEditApplier();
	WorldDeferredEditApplier &operator=(const WorldDeferredEditApplier &other);

	static int32_t apply(WorldChunkStreamer &streamer, World &world) noexcept;

  private:
	static bool deferred_edit_less(const WorldGenerationPipeline::WorldDeferredBlockEdit &left,
		const WorldGenerationPipeline::WorldDeferredBlockEdit &right) noexcept;
	static int32_t apply_single_edit(WorldChunkStreamer &streamer, World &world,
		const WorldGenerationPipeline::WorldDeferredBlockEdit &edit,
		std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> &pending) noexcept;
};

#endif
