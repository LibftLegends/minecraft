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
		const std::vector<WorldRevisionManager::RevisionChunk> &manual_protected,
		const WorldRevisionCostPolicy &cost_policy,
		uint64_t cost_policy_revision,
		bool cost_policy_configured, bool station_configured,
		int32_t station_chunk_x, int32_t station_chunk_z,
		bool station_block_configured, int32_t station_block_x,
		int32_t station_block_y, int32_t station_block_z,
		uint32_t station_block_id) noexcept;
	static int32_t serialize(uint32_t revision_id,
		const std::vector<WorldRevisionManager::RevisionChunk> &manual_protected,
		const WorldRevisionCostPolicy &cost_policy,
		uint64_t cost_policy_revision, bool cost_policy_configured,
		bool station_configured, int32_t station_chunk_x,
		int32_t station_chunk_z, bool station_block_configured,
		int32_t station_block_x, int32_t station_block_y,
		int32_t station_block_z, uint32_t station_block_id,
		std::vector<uint8_t> &bytes_out) noexcept;
	static int32_t load(const char *file_path, uint32_t &revision_id,
		std::vector<WorldRevisionManager::RevisionChunk> &manual_protected,
		WorldRevisionCostPolicy &cost_policy,
		uint64_t &cost_policy_revision,
		bool &cost_policy_configured,
		bool &cost_policy_stored, bool &station_configured,
	int32_t &station_chunk_x, int32_t &station_chunk_z,
	bool &station_stored, bool &station_block_configured,
	int32_t &station_block_x, int32_t &station_block_y,
	int32_t &station_block_z, uint32_t &station_block_id) noexcept;
	static int32_t deserialize(const std::vector<uint8_t> &bytes,
		uint32_t &revision_id,
		std::vector<WorldRevisionManager::RevisionChunk> &manual_protected,
		WorldRevisionCostPolicy &cost_policy,
		uint64_t &cost_policy_revision,
		bool &cost_policy_configured, bool &cost_policy_stored,
		bool &station_configured, int32_t &station_chunk_x,
		int32_t &station_chunk_z, bool &station_stored,
		bool &station_block_configured, int32_t &station_block_x,
		int32_t &station_block_y, int32_t &station_block_z,
		uint32_t &station_block_id) noexcept;
};

#endif
