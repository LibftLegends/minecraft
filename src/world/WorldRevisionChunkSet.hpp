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
	static int32_t reserve_next_unoccupied(
		const std::vector<WorldRevisionManager::RevisionChunk> &selected,
		const std::vector<WorldRevisionManager::RevisionChunk> &reserved,
		std::size_t &cursor, std::size_t &selected_index_out) noexcept;
	static int32_t find_paid_square_anchor(
		const std::vector<WorldRevisionManager::RevisionChunk> &selected,
		const std::vector<WorldRevisionManager::RevisionChunk> &reserved,
		uint32_t footprint_chunks, int32_t &anchor_x,
		int32_t &anchor_z) noexcept;
	static bool within_protection_ring(int32_t chunk_x, int32_t chunk_z,
		int32_t protected_x, int32_t protected_z) noexcept;
	static bool within_protection_square(int32_t chunk_x, int32_t chunk_z,
		int32_t protected_x, int32_t protected_z,
		uint32_t radius) noexcept;
	static void set_membership(std::vector<WorldRevisionManager::RevisionChunk> &list,
		int32_t chunk_x, int32_t chunk_z, bool enabled) noexcept;
};

#endif
