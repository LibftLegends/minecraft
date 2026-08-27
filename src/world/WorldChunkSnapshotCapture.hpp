#ifndef WORLD_CHUNK_SNAPSHOT_CAPTURE_HPP
# define WORLD_CHUNK_SNAPSHOT_CAPTURE_HPP

# include "../../src/world/WorldGenerationPipeline.hpp"

class WorldChunkSnapshotCapture
{
  public:
	WorldChunkSnapshotCapture();
	WorldChunkSnapshotCapture(const WorldChunkSnapshotCapture &other);
	~WorldChunkSnapshotCapture();
	WorldChunkSnapshotCapture &operator=(const WorldChunkSnapshotCapture &other);

	static int32_t capture(const WorldChunk &target, const WorldChunk *west,
		const WorldChunk *east, const WorldChunk *north,
		const WorldChunk *south,
		WorldGenerationPipeline::WorldChunkSnapshot &snapshot) noexcept;

  private:
	static int32_t capture_blocks(const WorldChunk &target,
		WorldGenerationPipeline::WorldChunkSnapshot &snapshot) noexcept;
	static int32_t capture_border_column(const WorldChunk *source,
		std::vector<uint32_t> &border, int32_t border_local_x,
		int32_t border_local_z) noexcept;
};

#endif
