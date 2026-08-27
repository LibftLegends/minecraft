#ifndef WORLD_REVISION_CHUNK_SET_HPP
# define WORLD_REVISION_CHUNK_SET_HPP

# include "../../src/world/WorldRevisionManager.hpp"

class WorldRevisionChunkSet
{
  public:
	WorldRevisionChunkSet();
	WorldRevisionChunkSet(const WorldRevisionChunkSet &other);
	~WorldRevisionChunkSet();
	WorldRevisionChunkSet &operator=(const WorldRevisionChunkSet &other);

	static bool contains(const std::vector<WorldRevisionManager::RevisionChunk> &list,
		int32_t chunk_x, int32_t chunk_z) noexcept;
	static void set_membership(std::vector<WorldRevisionManager::RevisionChunk> &list,
		int32_t chunk_x, int32_t chunk_z, bool enabled) noexcept;
};

#endif
