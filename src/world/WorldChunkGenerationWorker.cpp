#include "../../src/world/WorldChunkGenerationWorker.hpp"

WorldChunkGenerationWorker::WorldChunkGenerationWorker()
{
}

WorldChunkGenerationWorker::WorldChunkGenerationWorker(const WorldChunkGenerationWorker &other)
{
	(void)other;
}

WorldChunkGenerationWorker::~WorldChunkGenerationWorker()
{
}

WorldChunkGenerationWorker &WorldChunkGenerationWorker::operator=(const WorldChunkGenerationWorker &other)
{
	(void)other;
	return (*this);
}

int32_t WorldChunkGenerationWorker::initialize_chunk_for_generation(WorldChunk &chunk,
	int32_t chunk_x, int32_t chunk_z, const char *seed,
	voxel_generation_config &config, uint32_t stage_mask,
	std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> &deferred_edits,
	const WorldGenerationPipeline::WorldChunkSnapshot *source_snapshot) noexcept
{
	int32_t error_code;

	chunk.chunk_x = chunk_x;
	chunk.chunk_z = chunk_z;
	chunk.world_x = chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	chunk.world_z = chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	if (source_snapshot == nullptr)
		error_code = chunk.chunk.initialize();
	else
		error_code = WorldChunkSnapshotReader::initialize_snapshot_chunk(chunk.chunk,
				*source_snapshot);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (chunk_mesh_initialize(chunk.mesh) != FT_ERR_SUCCESS)
	{
		(void)chunk.chunk.destroy();
		return (FT_ERR_NO_MEMORY);
	}
	error_code = voxel_generate_chunk_with_stage_mask(chunk.chunk,
			chunk.world_x, chunk.world_z, seed, config, stage_mask);
	if (error_code == FT_ERR_SUCCESS)
		error_code = chunk_mesh_generate_from_chunk(chunk.mesh, chunk.chunk);
	if (error_code != FT_ERR_SUCCESS)
	{
		(void)chunk_mesh_destroy(chunk.mesh);
		(void)chunk.chunk.destroy();
		return (error_code);
	}
	chunk.initialized = true;
	(void)deferred_edits;
	return (FT_ERR_SUCCESS);
}

std::unique_ptr<WorldGenerationPipeline::Result> WorldChunkGenerationWorker::process_generation(WorldGenerationPipeline::Request &request) noexcept
{
	std::unique_ptr<WorldGenerationPipeline::Result> result(new (std::nothrow) WorldGenerationPipeline::Result());

	if (result == nullptr)
		return (nullptr);
	result->request_id = request.request_id;
	result->world_epoch = request.world_epoch;
	result->relevance_epoch = request.relevance_epoch;
	result->generation_revision = request.generation_revision;
	result->configuration_signature = request.configuration_signature;
	result->stage_mask = request.stage_mask;
	result->voxel_revision = 0U;
	result->chunk_x = request.chunk_x;
	result->chunk_z = request.chunk_z;
	result->operation = request.operation;
	result->error_code = FT_ERR_SUCCESS;
	result->chunk.reset(new (std::nothrow) WorldChunk());
	if (result->chunk == nullptr)
	{
		result->error_code = FT_ERR_NO_MEMORY;
		return (result);
	}
	result->error_code = WorldChunkGenerationWorker::initialize_chunk_for_generation(*result->chunk,
			request.chunk_x, request.chunk_z, request.seed.c_str(),
			request.config, request.stage_mask, request.deferred_edits,
			request.snapshot.get());
	if (result->error_code != FT_ERR_SUCCESS)
	{
		result->chunk.reset();
		return (result);
	}
	return (WorldChunkGenerationWorker::finish_generation_result(std::move(result),
			request));
}

std::unique_ptr<WorldGenerationPipeline::Result> WorldChunkGenerationWorker::finish_generation_result(std::unique_ptr<WorldGenerationPipeline::Result> result,
	WorldGenerationPipeline::Request &request) noexcept
{
	game_voxel_generation_metadata metadata;

	metadata = result->chunk->chunk.get_generation_metadata();
	metadata.configuration_signature = request.configuration_signature;
	if (result->chunk->chunk.set_generation_metadata(metadata) != FT_ERR_SUCCESS)
	{
		result->error_code = FT_ERR_INVALID_OPERATION;
		result->chunk.reset();
		return (result);
	}
	result->deferred_edits = std::move(request.deferred_edits);
	for (WorldGenerationPipeline::WorldDeferredBlockEdit &edit : result->deferred_edits)
		edit.request_id = request.request_id;
	return (result);
}

std::unique_ptr<WorldGenerationPipeline::Result> WorldChunkGenerationWorker::process_remesh(WorldGenerationPipeline::Request &request) noexcept
{
	std::unique_ptr<WorldGenerationPipeline::Result> result(new (std::nothrow) WorldGenerationPipeline::Result());
	game_voxel_chunk target_chunk;

	if (result == nullptr)
		return (nullptr);
	result->request_id = request.request_id;
	result->world_epoch = request.world_epoch;
	result->relevance_epoch = request.relevance_epoch;
	result->generation_revision = request.generation_revision;
	result->configuration_signature = 0U;
	result->stage_mask = 0U;
	result->voxel_revision = request.voxel_revision;
	result->chunk_x = request.chunk_x;
	result->chunk_z = request.chunk_z;
	result->operation = request.operation;
	result->error_code = FT_ERR_SUCCESS;
	result->mesh.reset(new (std::nothrow) chunk_mesh());
	if (result->mesh == nullptr)
	{
		result->error_code = FT_ERR_NO_MEMORY;
		return (result);
	}
	if (chunk_mesh_initialize(*result->mesh) != FT_ERR_SUCCESS)
	{
		result->error_code = FT_ERR_NO_MEMORY;
		result->mesh.reset();
		return (result);
	}
	result->error_code = WorldChunkSnapshotReader::initialize_snapshot_chunk(target_chunk,
			*request.snapshot);
	if (result->error_code == FT_ERR_SUCCESS)
		result->error_code = chunk_mesh_generate_from_chunk_with_neighbors(*result->mesh,
				target_chunk, request.chunk_x, request.chunk_z,
				&WorldChunkSnapshotReader::lookup_snapshot_block,
				request.snapshot.get());
	(void)target_chunk.destroy();
	if (result->error_code != FT_ERR_SUCCESS)
		result->mesh.reset();
	return (result);
}
