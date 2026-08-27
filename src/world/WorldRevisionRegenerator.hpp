#ifndef WORLD_REVISION_REGENERATOR_HPP
# define WORLD_REVISION_REGENERATOR_HPP

# include "../../src/world/WorldChunkStreamer.hpp"

class WorldRevisionRegenerator
{
  public:
	WorldRevisionRegenerator();
	WorldRevisionRegenerator(const WorldRevisionRegenerator &other);
	~WorldRevisionRegenerator();
	WorldRevisionRegenerator &operator=(const WorldRevisionRegenerator &other);

	static int32_t start(WorldRevisionManager &manager, World &world) noexcept;
	static int32_t finish(WorldRevisionManager &manager, World &world) noexcept;
	static int32_t regenerate_selected_chunks(WorldRevisionManager &manager,
		World &world, int32_t *regenerated_count,
		int32_t *skipped_count) noexcept;

  private:
	static int32_t capture_chunk_snapshot(WorldRevisionManager &manager,
		World &world, WorldChunk &chunk,
		WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		const WorldGenerationPipeline::WorldChunkSnapshot **source_snapshot) noexcept;
	static int32_t submit_chunk_regeneration(WorldRevisionManager &manager,
		World &world,
		const WorldRevisionManager::RevisionChunk &entry) noexcept;
	static int32_t commit_terrain_config(WorldRevisionManager &manager,
		World &world) noexcept;
};

# include "../../src/world/WorldRevisionRequestApplier.hpp"

#endif
