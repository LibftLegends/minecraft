#ifndef WORLD_GENERATION_REQUEST_BUILDER_HPP
# define WORLD_GENERATION_REQUEST_BUILDER_HPP

# include "../../src/world/WorldGenerationPipeline.hpp"

class WorldGenerationRequestBuilder
{
  public:
	WorldGenerationRequestBuilder();
	WorldGenerationRequestBuilder(const WorldGenerationRequestBuilder &other);
	~WorldGenerationRequestBuilder();
	WorldGenerationRequestBuilder &operator=(const WorldGenerationRequestBuilder &other);

	static int32_t build(std::unique_ptr<WorldGenerationPipeline::Request> &request,
		uint64_t request_id, uint64_t cancellation_epoch, uint64_t world_epoch,
		uint64_t relevance_epoch, uint32_t generation_revision, int32_t chunk_x,
		int32_t chunk_z, const char *seed,
		const terrain_generation_config &config, uint32_t stage_mask,
		WorldGenerationPipeline::WorldGenerationOperation operation,
		const WorldGenerationPipeline::WorldChunkSnapshot *source_snapshot) noexcept;
	static int32_t build_remesh(std::unique_ptr<WorldGenerationPipeline::Request> &request,
		uint64_t request_id, uint64_t cancellation_epoch, uint64_t world_epoch,
		uint64_t relevance_epoch, uint32_t generation_revision, int32_t chunk_x,
		int32_t chunk_z, uint64_t voxel_revision,
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot) noexcept;

  private:
	static int32_t deferred_writer(int32_t world_x, int32_t world_y,
		int32_t world_z, uint32_t block_id, void *user_data) noexcept;
};

#endif
