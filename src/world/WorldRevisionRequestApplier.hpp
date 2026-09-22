#ifndef WORLD_REVISION_REQUEST_APPLIER_HPP
# define WORLD_REVISION_REQUEST_APPLIER_HPP

# include "../../src/world/WorldRevisionRegenerator.hpp"

class WorldRevisionRequestApplier
{
  public:
	WorldRevisionRequestApplier();
	WorldRevisionRequestApplier(const WorldRevisionRequestApplier &other);
	~WorldRevisionRequestApplier();
	WorldRevisionRequestApplier &operator=(const WorldRevisionRequestApplier &other);

	static int32_t apply(WorldRevisionManager &manager, World &world,
		const voxel_generation_config &config, int32_t mode,
		uint32_t stage_mask,
		const std::vector<WorldRevisionManager::RevisionChunk> &selected_chunks,
		const std::vector<WorldRevisionManager::RevisionChunk> &protected_chunks,
		int32_t *regenerated_count, int32_t *skipped_count) noexcept;

  private:
	static int32_t validate_request_chunks(WorldRevisionManager &manager,
		uint32_t stage_mask,
		const std::vector<WorldRevisionManager::RevisionChunk> &selected_chunks,
		const std::vector<WorldRevisionManager::RevisionChunk> &protected_chunks) noexcept;
	static int32_t apply_protection_and_selection(WorldRevisionManager &manager,
		const std::vector<WorldRevisionManager::RevisionChunk> &selected_chunks,
		const std::vector<WorldRevisionManager::RevisionChunk> &protected_chunks) noexcept;
};

#endif
