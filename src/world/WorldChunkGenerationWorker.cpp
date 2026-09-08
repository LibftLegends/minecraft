#include "../../src/world/WorldChunkGenerationWorker.hpp"
#include <chrono>
#include <cstdio>

namespace
{
	int32_t lookup_local_light_block(void *user_data, int32_t world_x,
		int32_t world_y, int32_t world_z, uint32_t *block_id) noexcept
	{
		game_voxel_chunk *chunk = static_cast<game_voxel_chunk *>(user_data);
		if (chunk == nullptr || block_id == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		world_x %= GAME_VOXEL_CHUNK_WIDTH;
		world_z %= GAME_VOXEL_CHUNK_DEPTH;
		if (world_x < 0)
			world_x += GAME_VOXEL_CHUNK_WIDTH;
		if (world_z < 0)
			world_z += GAME_VOXEL_CHUNK_DEPTH;
		return (chunk->read_block(world_x, world_y, world_z, block_id));
	}
}

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
	const WorldGenerationPipeline::WorldChunkSnapshot *source_snapshot,
	uint64_t *generation_duration_nanoseconds,
	uint64_t *mesh_duration_nanoseconds) noexcept
{
	int32_t error_code;
	std::chrono::steady_clock::time_point phase_start;

	chunk.chunk_x = chunk_x;
	chunk.chunk_z = chunk_z;
	chunk.world_x = chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	chunk.world_z = chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	if (source_snapshot == nullptr)
		error_code = chunk.chunk.initialize();
	else
	{
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		std::fprintf(stderr,
			"[WorldRevision] worker snapshot-init chunk=(%d,%d) blocks=%zu "
			"metadata_valid=%d stages=%u\n", chunk_x, chunk_z,
			source_snapshot->blocks.size(),
			source_snapshot->generation_metadata.valid != FT_FALSE ? 1 : 0,
			source_snapshot->generation_metadata.completed_stage_mask);
	#endif
		error_code = WorldChunkSnapshotReader::initialize_snapshot_chunk(chunk.chunk,
				*source_snapshot);
	}
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (chunk_mesh_initialize(chunk.mesh) != FT_ERR_SUCCESS)
	{
		(void)chunk.chunk.destroy();
		return (FT_ERR_NO_MEMORY);
	}
	phase_start = std::chrono::steady_clock::now();
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (source_snapshot != nullptr)
		std::fprintf(stderr,
			"[WorldRevision] worker generate chunk=(%d,%d) stages=%u\n",
			chunk_x, chunk_z, stage_mask);
	#endif
	error_code = voxel_generate_chunk_with_stage_mask(chunk.chunk,
			chunk.world_x, chunk.world_z, seed, config, stage_mask);
	if (generation_duration_nanoseconds != nullptr)
		*generation_duration_nanoseconds = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - phase_start).count());
	phase_start = std::chrono::steady_clock::now();
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = voxel_light_build_chunk_local(chunk.light, chunk.world_x,
			chunk.world_z, lookup_local_light_block, &chunk.chunk);
		if (error_code == FT_ERR_SUCCESS)
			error_code = chunk_mesh_generate_from_chunk_with_light(chunk.mesh,
				chunk.chunk, chunk.light);
	}
	if (mesh_duration_nanoseconds != nullptr)
		*mesh_duration_nanoseconds = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - phase_start).count());
	if (error_code != FT_ERR_SUCCESS)
	{
		(void)chunk_mesh_destroy(chunk.mesh);
		(void)chunk.chunk.destroy();
		return (error_code);
	}
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (source_snapshot != nullptr)
		std::fprintf(stderr,
			"[WorldRevision] worker generate complete chunk=(%d,%d)\n",
			chunk_x, chunk_z);
	#endif
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
	result->light_revision = 0U;
	result->chunk_x = request.chunk_x;
	result->chunk_z = request.chunk_z;
	result->operation = request.operation;
	result->error_code = FT_ERR_SUCCESS;
	result->generation_duration_nanoseconds = 0U;
	result->mesh_duration_nanoseconds = 0U;
	result->light_scanned_cells = 0U;
	result->light_propagated_cells = 0U;
	result->light_queue_peak = 0U;
	result->chunk.reset(new (std::nothrow) WorldChunk());
	if (result->chunk == nullptr)
	{
		result->error_code = FT_ERR_NO_MEMORY;
		return (result);
	}
	result->error_code = WorldChunkGenerationWorker::initialize_chunk_for_generation(*result->chunk,
			request.chunk_x, request.chunk_z, request.seed.c_str(),
			request.config, request.stage_mask, request.deferred_edits,
			request.snapshot.get(), &result->generation_duration_nanoseconds,
			&result->mesh_duration_nanoseconds);
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
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	std::fprintf(stderr,
		"[WorldRevision] worker finalize request=%llu chunk=(%d,%d)\n",
		static_cast<unsigned long long>(request.request_id), request.chunk_x,
		request.chunk_z);
	#endif
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
	voxel_light_build_stats light_stats;
	voxel_light_update_config light_config;
	ft_bool light_complete;
	int32_t error_code;
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	std::chrono::steady_clock::time_point light_step_start;
	std::chrono::steady_clock::time_point mesh_start;
#endif

	if (result == nullptr)
		return (nullptr);
	result->request_id = request.request_id;
	result->world_epoch = request.world_epoch;
	result->relevance_epoch = request.relevance_epoch;
	result->generation_revision = request.generation_revision;
	result->configuration_signature = 0U;
	result->stage_mask = 0U;
	result->voxel_revision = request.voxel_revision;
	result->light_revision = request.light_revision;
	result->chunk_x = request.chunk_x;
	result->chunk_z = request.chunk_z;
	result->operation = request.operation;
	result->error_code = FT_ERR_SUCCESS;
	result->generation_duration_nanoseconds = 0U;
	result->mesh_duration_nanoseconds = 0U;
	result->light_scanned_cells = 0U;
	result->light_propagated_cells = 0U;
	result->light_queue_peak = 0U;
	if (request.snapshot == nullptr)
	{
		result->error_code = FT_ERR_NO_MEMORY;
		return (result);
	}
	if (request.remesh_light_operation == nullptr)
	{
		request.remesh_target.reset(new (std::nothrow) game_voxel_chunk());
		request.remesh_light.reset(new (std::nothrow) voxel_light_chunk());
		request.remesh_light_operation.reset(
			new (std::nothrow) voxel_light_build_operation());
		if (request.remesh_target == nullptr || request.remesh_light == nullptr
			|| request.remesh_light_operation == nullptr)
		{
			result->error_code = FT_ERR_NO_MEMORY;
			return (result);
		}
		error_code = WorldChunkSnapshotReader::initialize_snapshot_chunk(
			*request.remesh_target, *request.snapshot);
		if (error_code == FT_ERR_SUCCESS)
			error_code = request.remesh_light_operation->initialize(
				*request.remesh_light,
				request.chunk_x * GAME_VOXEL_CHUNK_WIDTH,
				request.chunk_z * GAME_VOXEL_CHUNK_DEPTH,
				&WorldChunkSnapshotReader::lookup_snapshot_block,
				request.snapshot.get(), FT_TRUE);
		if (error_code != FT_ERR_SUCCESS)
		{
			result->error_code = error_code;
			return (result);
		}
	}
	voxel_light_update_config_defaults(light_config);
	light_config = request.light_update_config;
	if (voxel_light_update_config_is_valid(light_config) == FT_FALSE)
	{
		result->error_code = FT_ERR_INVALID_ARGUMENT;
		return (result);
	}
	light_complete = FT_FALSE;
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	light_step_start = std::chrono::steady_clock::now();
	#endif
	error_code = request.remesh_light_operation->step(light_config,
			&light_stats, &light_complete);
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	{
		const uint64_t light_step_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - light_step_start).count());
		if (light_step_us >= 100000U)
			std::fprintf(stderr,
				"[WorldGen] slow remesh light step request=%llu chunk=(%d,%d) "
				"duration_us=%llu scanned=%llu propagated=%llu complete=%d\n",
				static_cast<unsigned long long>(request.request_id),
				request.chunk_x, request.chunk_z,
				static_cast<unsigned long long>(light_step_us),
				static_cast<unsigned long long>(light_stats.scanned_cells),
				static_cast<unsigned long long>(light_stats.propagated_cells),
				light_complete != FT_FALSE ? 1 : 0);
	}
	#endif
	result->light_scanned_cells = light_stats.scanned_cells;
	result->light_propagated_cells = light_stats.propagated_cells;
	result->light_queue_peak = light_stats.queue_peak;
	if (error_code != FT_ERR_SUCCESS)
	{
		result->error_code = error_code;
		return (result);
	}
	if (light_complete == FT_FALSE)
	{
		request.remesh_in_progress = FT_TRUE;
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		if (request.request_id % 32U == 0U && light_stats.scanned_cells % 65536U == 0U)
			std::fprintf(stderr,
				"[WorldGen] remesh lighting slice chunk=(%d,%d) scanned=%llu "
				"propagated=%llu queue_peak=%llu\n", request.chunk_x,
				request.chunk_z,
				static_cast<unsigned long long>(light_stats.scanned_cells),
				static_cast<unsigned long long>(light_stats.propagated_cells),
				static_cast<unsigned long long>(light_stats.queue_peak));
	#endif
		return (nullptr);
	}
	result->mesh.reset(new (std::nothrow) chunk_mesh());
	if (result->mesh == nullptr)
	{
		result->error_code = FT_ERR_NO_MEMORY;
		return (result);
	}
	if (chunk_mesh_initialize(*result->mesh) != FT_ERR_SUCCESS)
	{
		result->error_code = FT_ERR_NO_MEMORY;
		return (result);
	}
	result->light = std::move(request.remesh_light);
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	mesh_start = std::chrono::steady_clock::now();
	#endif
	result->error_code =
		chunk_mesh_generate_from_chunk_with_neighbors_and_light_lookup(
		*result->mesh, *request.remesh_target, request.chunk_x, request.chunk_z,
		&WorldChunkSnapshotReader::lookup_snapshot_block, request.snapshot.get(),
		result->light.get(), &voxel_light_build_operation_lookup,
		request.remesh_light_operation.get());
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	{
		const uint64_t mesh_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - mesh_start).count());
		if (mesh_us >= 100000U)
			std::fprintf(stderr,
				"[WorldGen] slow remesh mesh request=%llu chunk=(%d,%d) "
				"duration_us=%llu error=%d\n",
				static_cast<unsigned long long>(request.request_id),
				request.chunk_x, request.chunk_z,
				static_cast<unsigned long long>(mesh_us), result->error_code);
	}
	#endif
	request.remesh_light_operation.reset();
	request.remesh_target.reset();
	request.remesh_in_progress = FT_FALSE;
	if (result->error_code != FT_ERR_SUCCESS)
		result->mesh.reset();
	return (result);
}
