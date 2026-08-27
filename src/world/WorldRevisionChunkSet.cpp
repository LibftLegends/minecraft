#include "../../src/world/WorldRevisionChunkSet.hpp"

WorldRevisionChunkSet::WorldRevisionChunkSet()
{
}

WorldRevisionChunkSet::WorldRevisionChunkSet(const WorldRevisionChunkSet &other)
{
	(void)other;
}

WorldRevisionChunkSet::~WorldRevisionChunkSet()
{
}

WorldRevisionChunkSet &WorldRevisionChunkSet::operator=(const WorldRevisionChunkSet &other)
{
	(void)other;
	return (*this);
}

bool WorldRevisionChunkSet::contains(const std::vector<WorldRevisionManager::RevisionChunk> &list,
	int32_t chunk_x, int32_t chunk_z) noexcept
{
	for (const WorldRevisionManager::RevisionChunk &entry : list)
	{
		if (entry.chunk_x == chunk_x && entry.chunk_z == chunk_z)
			return (true);
	}
	return (false);
}

void WorldRevisionChunkSet::set_membership(std::vector<WorldRevisionManager::RevisionChunk> &list,
	int32_t chunk_x, int32_t chunk_z, bool enabled) noexcept
{
	for (std::vector<WorldRevisionManager::RevisionChunk>::iterator it = list.begin(); it != list.end(); ++it)
	{
		if (it->chunk_x == chunk_x && it->chunk_z == chunk_z)
		{
			if (!enabled)
				list.erase(it);
			return ;
		}
	}
	if (enabled)
		list.push_back({chunk_x, chunk_z});
}
