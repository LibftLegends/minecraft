#ifndef WORLD_TERRAIN_CHUNK_STORE_HPP
# define WORLD_TERRAIN_CHUNK_STORE_HPP

# include "../../Libft/Modules/Game/game_voxel_chunk.hpp"
# include <cstdint>
# include <mutex>
# include <string>

/* Durable per-world voxel chunk payloads. The caller coordinates writes with
 * chunk mutation and invokes load before procedural generation. */
class WorldTerrainChunkStore
{
  public:
	WorldTerrainChunkStore() noexcept;
	WorldTerrainChunkStore(const WorldTerrainChunkStore &other);
	~WorldTerrainChunkStore();
	WorldTerrainChunkStore &operator=(const WorldTerrainChunkStore &other);

	int32_t initialize(const uint8_t world_id[32]) noexcept;
	int32_t save_chunk(int32_t chunk_x, int32_t chunk_z,
		const game_voxel_chunk &chunk) const noexcept;
	/* Returns FT_ERR_NOT_FOUND when no durable payload exists. Deserialization
	 * validates the complete record before replacing the target chunk. */
	int32_t load_chunk(int32_t chunk_x, int32_t chunk_z,
		game_voxel_chunk &chunk) const noexcept;
	int32_t remove_chunk(int32_t chunk_x, int32_t chunk_z) const noexcept;
	int32_t has_chunk(int32_t chunk_x, int32_t chunk_z,
		bool *exists_out) const noexcept;
	void reset() noexcept;
	bool is_initialized() const noexcept;

  private:
	std::string directory_;
	bool initialized_;
	mutable std::mutex mutex_;

	int32_t chunk_path(int32_t chunk_x, int32_t chunk_z,
		std::string &path_out) const noexcept;
};

#endif
