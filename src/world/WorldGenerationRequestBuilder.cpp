#include "../../src/world/WorldGenerationRequestBuilder.hpp"

WorldGenerationRequestBuilder::WorldGenerationRequestBuilder()
{
}

WorldGenerationRequestBuilder::WorldGenerationRequestBuilder(const WorldGenerationRequestBuilder &other)
{
	(void)other;
}

WorldGenerationRequestBuilder::~WorldGenerationRequestBuilder()
{
}

WorldGenerationRequestBuilder &WorldGenerationRequestBuilder::operator=(const WorldGenerationRequestBuilder &other)
{
	(void)other;
	return (*this);
}

int32_t WorldGenerationRequestBuilder::deferred_writer(int32_t world_x,
	int32_t world_y, int32_t world_z, uint32_t block_id,
	void *user_data) noexcept
{
	std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> *edits;

	if (user_data == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	edits = static_cast<std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> *>(user_data);
	try
	{
		edits->push_back({world_x, world_y, world_z, block_id, 0U,
			edits->size()});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationRequestBuilder::build(std::unique_ptr<WorldGenerationPipeline::Request> &request,
	uint64_t request_id, uint64_t cancellation_epoch, uint64_t world_epoch,
	uint64_t relevance_epoch, uint32_t generation_revision, int32_t chunk_x,
	int32_t chunk_z, const char *seed, const terrain_generation_config &config,
	uint32_t stage_mask,
	WorldGenerationPipeline::WorldGenerationOperation operation,
	const WorldGenerationPipeline::WorldChunkSnapshot *source_snapshot) noexcept
{
	request.reset(new (std::nothrow) WorldGenerationPipeline::Request());
	if (request == nullptr)
		return (FT_ERR_NO_MEMORY);
	request->request_id = request_id;
	request->cancellation_epoch = cancellation_epoch;
	request->world_epoch = world_epoch;
	request->relevance_epoch = relevance_epoch;
	request->generation_revision = generation_revision;
	request->configuration_signature = terrain_generation_config_signature(config);
	request->stage_mask = stage_mask;
	request->voxel_revision = 0U;
	request->chunk_x = chunk_x;
	request->chunk_z = chunk_z;
	request->operation = operation;
	request->seed = seed == nullptr ? "" : seed;
	if (request->config.initialize(config) != FT_ERR_SUCCESS)
		return (FT_ERR_NO_MEMORY);
	if (config.cross_chunk_block_writer != nullptr)
	{
		if (request->config.set_cross_chunk_writer(&WorldGenerationRequestBuilder::deferred_writer,
				&request->deferred_edits) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_OPERATION);
		if (request->config.set_cross_chunk_features_enabled(FT_TRUE) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_OPERATION);
	}
	if (source_snapshot != nullptr)
	{
		request->snapshot.reset(new (std::nothrow) WorldGenerationPipeline::WorldChunkSnapshot(*source_snapshot));
		if (request->snapshot == nullptr)
			return (FT_ERR_NO_MEMORY);
	}
	request->deferred_edits.reserve(256U);
	return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationRequestBuilder::build_remesh(std::unique_ptr<WorldGenerationPipeline::Request> &request,
	uint64_t request_id, uint64_t cancellation_epoch, uint64_t world_epoch,
	uint64_t relevance_epoch, uint32_t generation_revision, int32_t chunk_x,
	int32_t chunk_z, uint64_t voxel_revision,
	const WorldGenerationPipeline::WorldChunkSnapshot &snapshot) noexcept
{
	request.reset(new (std::nothrow) WorldGenerationPipeline::Request());
	if (request == nullptr)
		return (FT_ERR_NO_MEMORY);
	request->request_id = request_id;
	request->cancellation_epoch = cancellation_epoch;
	request->world_epoch = world_epoch;
	request->relevance_epoch = relevance_epoch;
	request->generation_revision = generation_revision;
	request->configuration_signature = 0U;
	request->stage_mask = 0U;
	request->voxel_revision = voxel_revision;
	request->chunk_x = chunk_x;
	request->chunk_z = chunk_z;
	request->operation = WorldGenerationPipeline::WorldGenerationOperation::REMESH;
	request->snapshot.reset(new (std::nothrow) WorldGenerationPipeline::WorldChunkSnapshot(snapshot));
	if (request->snapshot == nullptr)
		return (FT_ERR_NO_MEMORY);
	return (FT_ERR_SUCCESS);
}
