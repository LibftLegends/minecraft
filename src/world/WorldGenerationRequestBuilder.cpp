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
	int32_t chunk_z, const char *seed, const voxel_generation_config &config,
	uint32_t stage_mask,
	WorldGenerationPipeline::WorldGenerationOperation operation,
	const WorldGenerationPipeline::WorldChunkSnapshot *source_snapshot) noexcept
{
	request.reset(new (std::nothrow) WorldGenerationPipeline::Request());
	if (request == nullptr)
		return (FT_ERR_NO_MEMORY);
	request->request_id = request_id;
	request->cancellation_epoch = cancellation_epoch;
	request->background_remesh_epoch = 0U;
	request->world_epoch = world_epoch;
	request->relevance_epoch = relevance_epoch;
	request->generation_revision = generation_revision;
	request->configuration_signature = voxel_generation_config_signature(config);
	request->stage_mask = stage_mask;
	request->voxel_revision = 0U;
	request->light_revision = 0U;
	request->content_version = 1U;
	request->light_input_version = 1U;
	request->chunk_x = chunk_x;
	request->chunk_z = chunk_z;
	request->operation = operation;
	request->remesh_in_progress = FT_FALSE;
	request->remesh_geometry_published = FT_FALSE;
	request->remesh_interactive = FT_FALSE;
	request->incremental_additive_light = FT_FALSE;
	request->incremental_removal_light = FT_FALSE;
	request->incremental_local_x = 0;
	request->incremental_local_y = 0;
	request->incremental_local_z = 0;
	request->incremental_old_block_id = GAME_VOXEL_AIR_BLOCK;
	request->incremental_new_block_id = GAME_VOXEL_AIR_BLOCK;
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
	int32_t chunk_z, uint64_t voxel_revision, uint64_t light_revision,
	uint16_t content_version, uint16_t light_input_version,
	WorldGenerationPipeline::WorldChunkSnapshot &&snapshot,
	const voxel_light_update_config &light_update_config,
	ft_bool interactive, ft_bool geometry_only,
	ft_bool incremental_additive_light,
	ft_bool incremental_removal_light,
	int32_t incremental_local_x, int32_t incremental_local_y,
	int32_t incremental_local_z,
	uint32_t incremental_old_block_id, uint32_t incremental_new_block_id,
	const std::vector<WorldGenerationPipeline::IncrementalLightSeed>
		*incremental_light_seeds,
	const std::shared_ptr<std::atomic<uint64_t>> &cancellation_token,
	uint64_t cancellation_token_value) noexcept
{
	request.reset(new (std::nothrow) WorldGenerationPipeline::Request());
	if (request == nullptr)
		return (FT_ERR_NO_MEMORY);
	request->request_id = request_id;
	request->cancellation_epoch = cancellation_epoch;
	request->background_remesh_epoch = 0U;
	request->world_epoch = world_epoch;
	request->relevance_epoch = relevance_epoch;
	request->generation_revision = generation_revision;
	request->configuration_signature = 0U;
	request->stage_mask = 0U;
	request->voxel_revision = voxel_revision;
	request->light_revision = light_revision;
	request->content_version = content_version;
	request->light_input_version = light_input_version;
	request->chunk_x = chunk_x;
	request->chunk_z = chunk_z;
	request->operation = WorldGenerationPipeline::WorldGenerationOperation::REMESH;
	request->remesh_in_progress = FT_FALSE;
	request->remesh_geometry_published = FT_FALSE;
	request->remesh_interactive = interactive;
	request->remesh_geometry_only = geometry_only;
	request->incremental_additive_light = incremental_additive_light;
	request->incremental_removal_light = incremental_removal_light;
	request->incremental_local_x = incremental_local_x;
	request->incremental_local_y = incremental_local_y;
	request->incremental_local_z = incremental_local_z;
	request->incremental_old_block_id = incremental_old_block_id;
	request->incremental_new_block_id = incremental_new_block_id;
	request->incremental_additive_queue_index = 0U;
	request->incremental_removal_queue_index = 0U;
	request->incremental_removal_addition_queue_index = 0U;
	request->incremental_removal_state_initialized = FT_FALSE;
	request->incremental_removal_addition_started = FT_FALSE;
	request->incremental_removal_external_new_channel[0] = 0U;
	request->incremental_removal_external_new_channel[1] = 0U;
	request->incremental_seed_index = 0U;
	request->incremental_additive_state_initialized = FT_FALSE;
	request->incremental_processed_cells = 0U;
	try
	{
		if (incremental_light_seeds != nullptr)
			request->incremental_light_seeds = *incremental_light_seeds;
		if (request->incremental_light_seeds.empty()
			&& (incremental_additive_light != FT_FALSE
				|| incremental_removal_light != FT_FALSE))
		{
			request->incremental_light_seeds.push_back({incremental_additive_light,
				incremental_removal_light, FT_FALSE, incremental_local_x, incremental_local_y,
				incremental_local_z, incremental_old_block_id,
				incremental_new_block_id, 0U});
		}
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	request->cancellation_token = cancellation_token;
	request->cancellation_token_value = cancellation_token_value;
	request->light_update_config = light_update_config;
	request->snapshot.reset(new (std::nothrow)
		WorldGenerationPipeline::WorldChunkSnapshot(std::move(snapshot)));
	if (request->snapshot == nullptr)
		return (FT_ERR_NO_MEMORY);
	return (FT_ERR_SUCCESS);
}
