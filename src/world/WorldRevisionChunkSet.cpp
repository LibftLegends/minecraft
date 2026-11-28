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

int32_t WorldRevisionChunkSet::reserve_next_unoccupied(
	const std::vector<WorldRevisionManager::RevisionChunk> &selected,
	const std::vector<WorldRevisionManager::RevisionChunk> &reserved,
	std::size_t &cursor, std::size_t &selected_index_out) noexcept
{
	while (cursor < selected.size())
	{
		const std::size_t candidate_index = cursor++;
		const WorldRevisionManager::RevisionChunk &candidate =
			selected[candidate_index];
		if (WorldRevisionChunkSet::contains(reserved, candidate.chunk_x,
				candidate.chunk_z))
			continue ;
		selected_index_out = candidate_index;
		return (FT_ERR_SUCCESS);
	}
	return (FT_ERR_FULL);
}

int32_t WorldRevisionChunkSet::find_paid_square_anchor(
	const std::vector<WorldRevisionManager::RevisionChunk> &selected,
	const std::vector<WorldRevisionManager::RevisionChunk> &reserved,
	uint32_t footprint_chunks, int32_t &anchor_x,
	int32_t &anchor_z) noexcept
{
	if (footprint_chunks < 9U || footprint_chunks > 16U)
		return (FT_ERR_CONFIGURATION);
	bool found = false;
	int32_t best_x = 0;
	int32_t best_z = 0;
	for (const WorldRevisionManager::RevisionChunk &candidate : selected)
	{
		if (candidate.bonus)
			continue ;
		const int64_t end_x = static_cast<int64_t>(candidate.chunk_x)
			+ footprint_chunks - 1U;
		const int64_t end_z = static_cast<int64_t>(candidate.chunk_z)
			+ footprint_chunks - 1U;
		bool complete = true;
		if (end_x > INT32_MAX || end_z > INT32_MAX)
			continue ;
		for (uint32_t dz = 0U; dz < footprint_chunks && complete; ++dz)
		{
			for (uint32_t dx = 0U; dx < footprint_chunks; ++dx)
			{
				const int32_t x = static_cast<int32_t>(
					static_cast<int64_t>(candidate.chunk_x) + dx);
				const int32_t z = static_cast<int32_t>(
					static_cast<int64_t>(candidate.chunk_z) + dz);
				if (!WorldRevisionChunkSet::contains(selected, x, z))
				{
					complete = false;
					break ;
				}
				bool paid = false;
				for (const WorldRevisionManager::RevisionChunk &entry : selected)
				{
					if (entry.chunk_x == x && entry.chunk_z == z)
					{
						paid = !entry.bonus;
						break ;
					}
				}
				if (!paid || WorldRevisionChunkSet::contains(reserved, x, z))
				{
					complete = false;
					break ;
				}
			}
		}
		if (complete && (!found || candidate.chunk_z < best_z
			|| (candidate.chunk_z == best_z && candidate.chunk_x < best_x)))
		{
			best_x = candidate.chunk_x;
			best_z = candidate.chunk_z;
			found = true;
		}
	}
	if (!found)
		return (FT_ERR_FULL);
	anchor_x = best_x;
	anchor_z = best_z;
	return (FT_ERR_SUCCESS);
}

bool WorldRevisionChunkSet::within_protection_ring(int32_t chunk_x,
	int32_t chunk_z, int32_t protected_x, int32_t protected_z) noexcept
{
	const int64_t offset_x = static_cast<int64_t>(chunk_x)
		- static_cast<int64_t>(protected_x);
	const int64_t offset_z = static_cast<int64_t>(chunk_z)
		- static_cast<int64_t>(protected_z);

	return (offset_x >= -1 && offset_x <= 1 && offset_z >= -1
		&& offset_z <= 1);
}

bool WorldRevisionChunkSet::within_protection_square(int32_t chunk_x,
	int32_t chunk_z, int32_t protected_x, int32_t protected_z,
	uint32_t radius) noexcept
{
	const int64_t offset_x = static_cast<int64_t>(chunk_x)
		- static_cast<int64_t>(protected_x);
	const int64_t offset_z = static_cast<int64_t>(chunk_z)
		- static_cast<int64_t>(protected_z);
	const int64_t signed_radius = static_cast<int64_t>(radius);

	return (offset_x >= -signed_radius && offset_x <= signed_radius
		&& offset_z >= -signed_radius && offset_z <= signed_radius);
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
		list.push_back({chunk_x, chunk_z, false});
}
