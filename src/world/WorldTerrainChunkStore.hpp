#ifndef WORLD_TERRAIN_CHUNK_STORE_HPP
# define WORLD_TERRAIN_CHUNK_STORE_HPP

# include "../../Libft/Modules/Game/game_voxel_chunk.hpp"
# include <atomic>
# include <cstdint>
# include <memory>
# include <mutex>
# include <string>

struct WorldChunkReadState;

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
	/* Copies the path for a worker. An empty path means no terrain store. */
	int32_t capture_stream_path(int32_t chunk_x, int32_t chunk_z,
		std::string &path_out) const noexcept;
	static int32_t load_stream_chunk(const std::string &path,
		int32_t chunk_x, int32_t chunk_z, game_voxel_chunk &chunk) noexcept;
	/* Only the publication step installs the canonical file. A discarded
	 * worker result removes its private staging file. */
	class StagedChunk
	{
	  public:
		StagedChunk() = default;
		~StagedChunk() noexcept;
		StagedChunk(const StagedChunk &) = delete;
		StagedChunk &operator=(const StagedChunk &) = delete;

		int32_t prepare(const std::string &destination, int32_t chunk_x,
			int32_t chunk_z, const game_voxel_chunk &chunk) noexcept;

	  private:
		friend class WorldTerrainChunkStore;
		std::string path_;
		std::string destination_;
	};
	int32_t commit_staged_chunk(int32_t chunk_x, int32_t chunk_z,
		StagedChunk &staged, bool replace_existing = false) const noexcept;
	struct PreparedEdit
	{
		~PreparedEdit() noexcept;
		int32_t publish() noexcept;
		bool has_reservation() const noexcept;
		int32_t chunk_x = 0;
		int32_t chunk_z = 0;
		int32_t world_y = 0;
		int32_t local_x = 0;
		int32_t local_z = 0;
		uint32_t replacement = 0U;
		uint64_t terrain_revision = 0U;
		std::string destination;
		std::shared_ptr<const WorldChunkReadState> base;
		game_voxel_chunk voxels;
		StagedChunk file;
		bool published = false;
	  private:
		friend class WorldTerrainChunkStore;
		std::atomic<WorldTerrainChunkStore *> reservation_owner{nullptr};
	};
	int32_t reserve_prepared_edit(PreparedEdit &edit) noexcept;
	int32_t publish_prepared_edit(PreparedEdit &edit) noexcept;
	void release_prepared_edit(PreparedEdit &edit) noexcept;
	bool has_reserved_edit() const noexcept;
	int32_t remove_chunk(int32_t chunk_x, int32_t chunk_z) const noexcept;
	int32_t has_chunk(int32_t chunk_x, int32_t chunk_z,
		bool *exists_out) const noexcept;
	void reset() noexcept;
	bool is_initialized() const noexcept;

  private:
	std::string directory_;
	bool initialized_;
	mutable std::mutex mutex_;
	std::atomic<PreparedEdit *> reserved_edit_{nullptr};
	bool chunk_is_reserved(int32_t chunk_x, int32_t chunk_z) const noexcept;
	int32_t commit_staged_chunk_locked(int32_t chunk_x, int32_t chunk_z,
		StagedChunk &staged, bool replace_existing) const noexcept;

	int32_t chunk_path(int32_t chunk_x, int32_t chunk_z,
		std::string &path_out, bool validate_resolution = true) const noexcept;
};

#endif
