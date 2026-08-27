#ifndef WORLD_REVISION_METADATA_STORE_HPP
# define WORLD_REVISION_METADATA_STORE_HPP

# include "../../src/world/WorldRevisionManager.hpp"

class WorldRevisionMetadataStore
{
  public:
	WorldRevisionMetadataStore();
	WorldRevisionMetadataStore(const WorldRevisionMetadataStore &other);
	~WorldRevisionMetadataStore();
	WorldRevisionMetadataStore &operator=(const WorldRevisionMetadataStore &other);

	static int32_t save(const char *file_path, uint32_t revision_id,
		const std::vector<WorldRevisionManager::RevisionChunk> &manual_protected) noexcept;
	static int32_t load(const char *file_path, uint32_t &revision_id,
		std::vector<WorldRevisionManager::RevisionChunk> &manual_protected) noexcept;
};

#endif
