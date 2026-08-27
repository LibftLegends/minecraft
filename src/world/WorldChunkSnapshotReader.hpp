#ifndef WORLD_CHUNK_SNAPSHOT_READER_HPP
# define WORLD_CHUNK_SNAPSHOT_READER_HPP

# include "../../src/world/WorldGenerationPipeline.hpp"

class WorldChunkSnapshotReader
{
  public:
	WorldChunkSnapshotReader();
	WorldChunkSnapshotReader(const WorldChunkSnapshotReader &other);
	~WorldChunkSnapshotReader();
	WorldChunkSnapshotReader &operator=(const WorldChunkSnapshotReader &other);

	static int32_t initialize_snapshot_chunk(game_voxel_chunk &chunk,
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot) noexcept;
	static int32_t lookup_snapshot_block(void *user_data, int32_t world_x,
		int32_t world_y, int32_t world_z, uint32_t *block_id) noexcept;

  private:
	static int32_t lookup_border_block(const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		int32_t local_x, int32_t local_z, int32_t world_y,
		uint32_t *block_id) noexcept;
};

#endif
