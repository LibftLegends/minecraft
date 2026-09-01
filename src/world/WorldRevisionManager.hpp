#ifndef WORLD_REVISION_MANAGER_HPP
# define WORLD_REVISION_MANAGER_HPP

# include "../../src/world/WorldChunkStreamer.hpp"
# include "../../src/world/WorldRegenerationProgress.hpp"

class WorldRevisionManager
{
  public:
	struct						RevisionChunk
	{
		int32_t					chunk_x;
		int32_t					chunk_z;
	};

	World &world_;
	bool						pending_;
	uint32_t					revision_id_;
	uint32_t					stage_mask_;
	int32_t						mode_;
	voxel_generation_config	config_;
	std::vector<RevisionChunk> selected_;
	std::vector<RevisionChunk> manual_protected_;
	WorldRegenerationProgress	progress_;

	WorldRevisionManager(World &world);
	WorldRevisionManager(const WorldRevisionManager &other);
	~WorldRevisionManager();
	WorldRevisionManager &operator=(const WorldRevisionManager &other);

	void reset() noexcept;
	int32_t begin(const voxel_generation_config &config,
		int32_t mode) noexcept;
	int32_t cancel() noexcept;
	uint32_t identifier() const noexcept;
	uint32_t stage_mask() const noexcept;
	int32_t mode() const noexcept;
	bool pending() const noexcept;
	bool regenerating() const noexcept;
	std::size_t selected_count() const noexcept;
	std::size_t manually_protected_count() const noexcept;
	int32_t select_chunk(int32_t chunk_x, int32_t chunk_z,
		bool selected) noexcept;
	int32_t set_chunk_protected(int32_t chunk_x, int32_t chunk_z,
		bool protected_state) noexcept;
	bool is_chunk_protected(int32_t chunk_x, int32_t chunk_z) const noexcept;
	int32_t chunk_state(int32_t chunk_x, int32_t chunk_z) const noexcept;
	int32_t save_metadata(const char *file_path) const noexcept;
	int32_t load_metadata(const char *file_path) noexcept;

	bool is_regenerating_for(uint64_t relevance_epoch) const noexcept;
	void record_regeneration_completed() noexcept;
	void record_regeneration_error(int32_t error_code) noexcept;
	void record_regeneration_skipped() noexcept;
	void record_regeneration_success() noexcept;
	bool all_regeneration_jobs_done() const noexcept;
	int32_t finish_regeneration() noexcept;
	int32_t start_regeneration() noexcept;
	int32_t regenerate_selected_chunks(int32_t *regenerated_count,
		int32_t *skipped_count) noexcept;
	int32_t apply_request(const voxel_generation_config &config, int32_t mode,
		uint32_t stage_mask, const std::vector<RevisionChunk> &selected_chunks,
		const std::vector<RevisionChunk> &protected_chunks,
		int32_t *regenerated_count, int32_t *skipped_count) noexcept;

  private:
	static uint32_t stage_mask_for_mode(int32_t mode) noexcept;
};

# include "../../src/world/WorldRevisionChunkSet.hpp"
# include "../../src/world/WorldRevisionMetadataStore.hpp"
# include "../../src/world/WorldRevisionRegenerator.hpp"

#endif
