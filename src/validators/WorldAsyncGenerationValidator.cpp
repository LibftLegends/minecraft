#include "../../src/validators/WorldAsyncGenerationValidator.hpp"
#include "../../src/chunks/ChunkNeighborMesher.hpp"
#include "../../src/world/WorldChunkSnapshotCapture.hpp"
#include "../../src/world/WorldChunkSnapshotReader.hpp"
#include <chrono>
#include <cstdio>
#include <memory>
#include <new>
#include <thread>

namespace
{
	static const int32_t ASYNC_WORLDGEN_MAX_STARTUP_FRAMES = 240;
	static const int32_t ASYNC_STARTUP_EDIT_REPETITIONS = 4;
	static const int32_t ASYNC_STARTUP_PRIORITY_EDIT_ROUNDS = 8;
	static const int32_t ASYNC_STARTUP_PRIORITY_EDIT_INTERVAL = 8;
	static const uint64_t ASYNC_WORLDGEN_MAX_UPDATE_US = 100000U;
}

WorldAsyncGenerationValidator::WorldAsyncGenerationValidator()
{
}

WorldAsyncGenerationValidator::WorldAsyncGenerationValidator(const WorldAsyncGenerationValidator &other)
	: IValidator(other)
{
	(void)other;
}

WorldAsyncGenerationValidator::~WorldAsyncGenerationValidator()
{
}

WorldAsyncGenerationValidator &WorldAsyncGenerationValidator::operator=(const WorldAsyncGenerationValidator &other)
{
	(void)other;
	return (*this);
}

bool WorldAsyncGenerationValidator::chunks_equal(const game_voxel_chunk &left,
	const game_voxel_chunk &right) noexcept
{
	int32_t local_x;
	int32_t local_y;
	int32_t local_z;
	uint32_t left_block;
	uint32_t right_block;

	local_z = 0;
	while (local_z < GAME_VOXEL_CHUNK_DEPTH)
	{
		local_y = 0;
		while (local_y < GAME_VOXEL_CHUNK_HEIGHT)
		{
			local_x = 0;
			while (local_x < GAME_VOXEL_CHUNK_WIDTH)
			{
				if (left.read_block(local_x, local_y, local_z,
						&left_block) != FT_ERR_SUCCESS
					|| right.read_block(local_x, local_y, local_z,
						&right_block) != FT_ERR_SUCCESS
					|| left_block != right_block)
				{
					std::fprintf(stderr,
						"async-worldgen: mismatch at %d,%d,%d left=%u right=%u\n",
						local_x, local_y, local_z, left_block, right_block);
					return (false);
				}
				local_x += 1;
			}
			local_y += 1;
		}
		local_z += 1;
	}
	return (true);
}

namespace
{
	static int initialize_halo_chunk(WorldChunk &chunk, int32_t chunk_x,
		int32_t chunk_z, uint32_t corner_block) noexcept
	{
		int32_t error_code;

		error_code = chunk.chunk.initialize();
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		chunk.chunk_x = chunk_x;
		chunk.chunk_z = chunk_z;
		chunk.world_x = chunk_x * GAME_VOXEL_CHUNK_WIDTH;
		chunk.world_z = chunk_z * GAME_VOXEL_CHUNK_DEPTH;
		chunk.initialized = true;
		return (chunk.chunk.write_generated_block(
			chunk_x < 0 ? GAME_VOXEL_CHUNK_WIDTH - 1 : 0, 8,
			chunk_z < 0 ? GAME_VOXEL_CHUNK_DEPTH - 1 : 0, corner_block));
	}

	static int verify_halo_block(
		WorldGenerationPipeline::WorldChunkSnapshot &snapshot, int32_t world_x,
		int32_t world_z, uint32_t expected) noexcept
	{
		uint32_t actual;
		int32_t error_code;

		actual = 0U;
		error_code = WorldChunkSnapshotReader::lookup_snapshot_block(&snapshot,
			world_x, 8, world_z, &actual);
		if (error_code != FT_ERR_SUCCESS || actual != expected)
		{
			std::fprintf(stderr,
				"async-worldgen: diagonal halo mismatch world=(%d,8,%d) "
				"expected=%u actual=%u error=%d\n", world_x, world_z,
				expected, actual, error_code);
			return (1);
		}
		return (0);
	}

	static int fill_halo_chunk(WorldChunk &chunk, uint32_t block_id) noexcept
	{
		int32_t local_z;
		int32_t local_y;
		int32_t local_x;
		int32_t error_code;

		local_z = 0;
		while (local_z < GAME_VOXEL_CHUNK_DEPTH)
		{
			local_y = 0;
			while (local_y < GAME_VOXEL_CHUNK_HEIGHT)
			{
				local_x = 0;
				while (local_x < GAME_VOXEL_CHUNK_WIDTH)
				{
					error_code = chunk.chunk.write_generated_block(local_x,
						local_y, local_z, block_id);
					if (error_code != FT_ERR_SUCCESS)
						return (error_code);
					local_x += 1;
				}
				local_y += 1;
			}
			local_z += 1;
		}
		return (FT_ERR_SUCCESS);
	}
}

int WorldAsyncGenerationValidator::validate_diagonal_lighting_halo() noexcept
{
	WorldChunk target;
	WorldChunk northwest;
	WorldChunk northeast;
	WorldChunk southwest;
	WorldChunk southeast;
	WorldGenerationPipeline::WorldChunkSnapshot snapshot;
	int32_t error_code;

	error_code = initialize_halo_chunk(target, 0, 0, 10U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = initialize_halo_chunk(northwest, -1, -1, 101U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = initialize_halo_chunk(northeast, 1, -1, 102U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = initialize_halo_chunk(southwest, -1, 1, 103U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = initialize_halo_chunk(southeast, 1, 1, 104U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldChunkSnapshotCapture::capture(target, nullptr,
			nullptr, nullptr, nullptr, &northwest, &northeast, &southwest,
			&southeast, snapshot);
	if (error_code != FT_ERR_SUCCESS
		|| verify_halo_block(snapshot, -1, -1, 101U) != 0
		|| verify_halo_block(snapshot, GAME_VOXEL_CHUNK_WIDTH, -1, 102U) != 0
		|| verify_halo_block(snapshot, -1, GAME_VOXEL_CHUNK_DEPTH, 103U) != 0
		|| verify_halo_block(snapshot, GAME_VOXEL_CHUNK_WIDTH,
			GAME_VOXEL_CHUNK_DEPTH, 104U) != 0)
	{
		std::fprintf(stderr,
			"async-worldgen: diagonal lighting halo validation failed error=%d\n",
			error_code);
		return (1);
	}
	error_code = WorldChunkSnapshotCapture::capture(target, nullptr, nullptr,
		nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, snapshot);
	if (error_code != FT_ERR_SUCCESS
		|| verify_halo_block(snapshot, -1, -1,
			VOXEL_GENERATOR_STONE_BLOCK) != 0)
		return (1);
	return (0);
}

int WorldAsyncGenerationValidator::validate_cardinal_lighting_propagation() noexcept
{
	WorldChunk target;
	WorldChunk west;
	WorldGenerationPipeline::WorldChunkSnapshot snapshot;
	voxel_light_chunk light;
	int32_t error_code;
	int32_t local_y;
	uint8_t sky_light;

	error_code = initialize_halo_chunk(target, 0, 0,
		VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code == FT_ERR_SUCCESS)
		error_code = initialize_halo_chunk(west, -1, 0,
			VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code == FT_ERR_SUCCESS)
		error_code = fill_halo_chunk(target, VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code == FT_ERR_SUCCESS)
		error_code = fill_halo_chunk(west, VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code == FT_ERR_SUCCESS)
		error_code = target.chunk.write_generated_block(0, 8, 0,
			GAME_VOXEL_AIR_BLOCK);
	local_y = 8;
	while (error_code == FT_ERR_SUCCESS
		&& local_y < GAME_VOXEL_CHUNK_HEIGHT)
	{
		error_code = west.chunk.write_generated_block(
			GAME_VOXEL_CHUNK_WIDTH - 1, local_y, 0, GAME_VOXEL_AIR_BLOCK);
		local_y += 1;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldChunkSnapshotCapture::capture(target, &west,
			nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
			snapshot);
	if (error_code == FT_ERR_SUCCESS)
		error_code = voxel_light_build_chunk(light, 0, 0,
			&WorldChunkSnapshotReader::lookup_snapshot_block, &snapshot);
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"async-worldgen: cardinal propagation build failed error=%d\n",
			error_code);
		return (1);
	}
	sky_light = voxel_light_sky(light.get(0, 8, 0));
	if (sky_light != 14U)
	{
		std::fprintf(stderr,
			"async-worldgen: cardinal propagation expected=14 actual=%u\n",
			static_cast<unsigned int>(sky_light));
		return (1);
	}
	error_code = WorldChunkSnapshotCapture::capture(target, nullptr, nullptr,
		nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, snapshot);
	if (error_code == FT_ERR_SUCCESS)
		error_code = voxel_light_build_chunk(light, 0, 0,
			&WorldChunkSnapshotReader::lookup_snapshot_block, &snapshot);
	if (error_code != FT_ERR_SUCCESS)
		return (1);
	sky_light = voxel_light_sky(light.get(0, 8, 0));
	if (sky_light != 0U)
	{
		std::fprintf(stderr,
			"async-worldgen: absent cardinal source leaked sky=%u\n",
			static_cast<unsigned int>(sky_light));
		return (1);
	}
	return (0);
}

int WorldAsyncGenerationValidator::validate_diagonal_lighting_propagation() noexcept
{
	WorldChunk target;
	WorldChunk north;
	WorldChunk northwest;
	WorldGenerationPipeline::WorldChunkSnapshot snapshot;
	voxel_light_chunk light;
	int32_t error_code;
	int32_t local_y;
	uint8_t sky_light;

	error_code = initialize_halo_chunk(target, 0, 0,
		VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code == FT_ERR_SUCCESS)
		error_code = initialize_halo_chunk(north, 0, -1,
			VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code == FT_ERR_SUCCESS)
		error_code = initialize_halo_chunk(northwest, -1, -1,
			VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code == FT_ERR_SUCCESS)
		error_code = fill_halo_chunk(target, VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code == FT_ERR_SUCCESS)
		error_code = fill_halo_chunk(north, VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code == FT_ERR_SUCCESS)
		error_code = fill_halo_chunk(northwest, VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code == FT_ERR_SUCCESS)
		error_code = target.chunk.write_generated_block(0, 8, 0,
			GAME_VOXEL_AIR_BLOCK);
	if (error_code == FT_ERR_SUCCESS)
		error_code = north.chunk.write_generated_block(0, 8,
			GAME_VOXEL_CHUNK_DEPTH - 1, GAME_VOXEL_AIR_BLOCK);
	local_y = 8;
	while (error_code == FT_ERR_SUCCESS
		&& local_y < GAME_VOXEL_CHUNK_HEIGHT)
	{
		error_code = northwest.chunk.write_generated_block(
			GAME_VOXEL_CHUNK_WIDTH - 1, local_y,
			GAME_VOXEL_CHUNK_DEPTH - 1, GAME_VOXEL_AIR_BLOCK);
		local_y += 1;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldChunkSnapshotCapture::capture(target, nullptr,
			nullptr, &north, nullptr, &northwest, nullptr, nullptr, nullptr,
			snapshot);
	if (error_code == FT_ERR_SUCCESS)
		error_code = voxel_light_build_chunk(light, 0, 0,
			&WorldChunkSnapshotReader::lookup_snapshot_block, &snapshot);
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"async-worldgen: diagonal propagation build failed error=%d\n",
			error_code);
		return (1);
	}
	sky_light = voxel_light_sky(light.get(0, 8, 0));
	if (sky_light != 13U)
	{
		std::fprintf(stderr,
			"async-worldgen: diagonal propagation expected=13 actual=%u\n",
			static_cast<unsigned int>(sky_light));
		return (1);
	}
	error_code = WorldChunkSnapshotCapture::capture(target, nullptr, nullptr,
		&north, nullptr, nullptr, nullptr, nullptr, nullptr, snapshot);
	if (error_code == FT_ERR_SUCCESS)
		error_code = voxel_light_build_chunk(light, 0, 0,
			&WorldChunkSnapshotReader::lookup_snapshot_block, &snapshot);
	if (error_code != FT_ERR_SUCCESS)
		return (1);
	sky_light = voxel_light_sky(light.get(0, 8, 0));
	if (sky_light != 0U)
	{
		std::fprintf(stderr,
			"async-worldgen: absent diagonal source leaked sky=%u\n",
			static_cast<unsigned int>(sky_light));
		return (1);
	}
	return (0);
}

int WorldAsyncGenerationValidator::validate_light_scheduler_configuration()
	noexcept
{
	std::unique_ptr<World> world_storage(new (std::nothrow) World());
	if (world_storage == nullptr)
		return (1);
	World &world = *world_storage;
	voxel_light_update_config background;
	voxel_light_update_config interactive;
	voxel_light_update_config invalid;

	voxel_light_update_config_defaults(background);
	background.min_nodes_per_frame = 7U;
	background.target_nodes_per_frame = 31U;
	background.max_nodes_per_frame = 97U;
	background.time_budget_microseconds = 211U;
	if (world.set_light_update_config(background) != FT_ERR_SUCCESS
		|| world.light_update_config().min_nodes_per_frame != 7U
		|| world.light_update_config().target_nodes_per_frame != 31U
		|| world.light_update_config().max_nodes_per_frame != 97U
		|| world.light_update_config().time_budget_microseconds != 211U)
	{
		std::fprintf(stderr,
			"async-worldgen: background light scheduler config did not round-trip\n");
		return (1);
	}
	voxel_light_update_config_defaults(interactive);
	interactive.min_nodes_per_frame = 11U;
	interactive.target_nodes_per_frame = 43U;
	interactive.max_nodes_per_frame = 131U;
	interactive.time_budget_microseconds = 307U;
	if (world.set_interactive_light_update_config(interactive)
		!= FT_ERR_SUCCESS
		|| world.interactive_light_update_config().min_nodes_per_frame != 11U
		|| world.interactive_light_update_config().target_nodes_per_frame != 43U
		|| world.interactive_light_update_config().max_nodes_per_frame != 131U
		|| world.interactive_light_update_config().time_budget_microseconds != 307U)
	{
		std::fprintf(stderr,
			"async-worldgen: interactive light scheduler config did not round-trip\n");
		return (1);
	}
	invalid = interactive;
	invalid.min_nodes_per_frame = invalid.target_nodes_per_frame + 1U;
	if (world.set_interactive_light_update_config(invalid)
		!= FT_ERR_INVALID_ARGUMENT)
	{
		std::fprintf(stderr,
			"async-worldgen: invalid light scheduler config was accepted\n");
		return (1);
	}
	return (0);
}

int WorldAsyncGenerationValidator::validate_remesh_priority_metrics() noexcept
{
	std::unique_ptr<World> world_storage(new (std::nothrow) World());
	if (world_storage == nullptr)
		return (1);
	World &world = *world_storage;
	World::StreamDiagnostics diagnostics;
	std::size_t index;
	bool background_seen;

	/* This fixture intentionally does not initialize World.  That leaves the
	 * streamer without worker threads, so the queue ordering and diagnostics
	 * can be checked without racing a worker that drains the entries. */
	world.chunk_streamer.stream_frame_ = 0U;
	world.chunk_streamer.enqueue_background_remesh(30, 30);
	world.chunk_streamer.enqueue_background_remesh(31, 30);
	world.chunk_streamer.stream_frame_ = 12U;
	world.chunk_streamer.prioritize_chunk_remesh(7, 7);
	world.chunk_streamer.prioritize_chunk_remesh(8, 8);
	diagnostics = world.stream_diagnostics();
	if (diagnostics.remesh_priority_queue_depth != 4U
		|| diagnostics.interactive_remesh_queue_depth != 2U
		|| diagnostics.oldest_remesh_queue_age != 12U)
	{
		std::fprintf(stderr,
			"async-worldgen: priority metrics mismatch depth=%zu "
			"interactive=%zu oldest=%llu\n",
			diagnostics.remesh_priority_queue_depth,
			diagnostics.interactive_remesh_queue_depth,
			static_cast<unsigned long long>(
				diagnostics.oldest_remesh_queue_age));
		return (1);
	}
	background_seen = false;
	index = 0U;
	for (const WorldChunkStreamer::RemeshPriority &priority
		: world.chunk_streamer.priority_remeshes_)
	{
		if (!background_seen && !priority.interactive)
			background_seen = true;
		if (background_seen && priority.interactive)
		{
			std::fprintf(stderr,
				"async-worldgen: interactive remesh followed background "
				"entry at queue index=%zu\n", index);
			return (1);
		}
		if (index == 0U && (priority.chunk_x != 7
				|| priority.chunk_z != 7 || !priority.interactive))
		{
			std::fprintf(stderr,
				"async-worldgen: oldest equal-distance interactive remesh was not first "
				"chunk=(%d,%d) interactive=%d\n", priority.chunk_x,
				priority.chunk_z, priority.interactive ? 1 : 0);
			return (1);
		}
		if (index == 1U && (priority.chunk_x != 8
				|| priority.chunk_z != 8 || !priority.interactive))
		{
			std::fprintf(stderr,
				"async-worldgen: newer equal-distance interactive remesh was not second "
				"chunk=(%d,%d) interactive=%d\n", priority.chunk_x,
				priority.chunk_z, priority.interactive ? 1 : 0);
			return (1);
		}
		index += 1U;
	}
	if (!background_seen)
	{
		std::fprintf(stderr,
			"async-worldgen: background remesh entries disappeared from "
			"priority queue\n");
		return (1);
	}
	world.chunk_streamer.stream_frame_ = 132U;
	world.chunk_streamer.promote_starved_remeshes();
	diagnostics = world.stream_diagnostics();
	if (diagnostics.remesh_starvation_promotions != 2U
		|| diagnostics.interactive_remesh_queue_depth != 4U)
	{
		std::fprintf(stderr,
			"async-worldgen: starvation promotion mismatch promotions=%llu "
			"interactive=%zu\n",
			static_cast<unsigned long long>(
				diagnostics.remesh_starvation_promotions),
			diagnostics.interactive_remesh_queue_depth);
		return (1);
	}
	/* Long-run queue stress: repeated notifications for an existing chunk must
	 * replace/coalesce that entry instead of growing without bound.  Keep the
	 * frame fixed while injecting the duplicates so the age contract remains
	 * deterministic, then advance past the starvation threshold and require
	 * every surviving background entry to be promoted. */
	world.chunk_streamer.priority_remeshes_.clear();
	world.chunk_streamer.remesh_starvation_promotions_ = 0U;
	world.chunk_streamer.stream_frame_ = 0U;
	for (int32_t chunk_index = 0; chunk_index < 64; ++chunk_index)
		world.chunk_streamer.enqueue_background_remesh(chunk_index, 0);
	for (int32_t repeat = 0; repeat < 10000; ++repeat)
	{
		const int32_t chunk_index = repeat % 64;
		world.chunk_streamer.enqueue_background_remesh(chunk_index, 0);
	}
	if (world.chunk_streamer.priority_remeshes_.size() != 64U)
	{
		std::fprintf(stderr,
			"async-worldgen: long-run duplicate coalescing grew queue size=%zu\n",
			world.chunk_streamer.priority_remeshes_.size());
		return (1);
	}
	world.chunk_streamer.stream_frame_ = 120U;
	world.chunk_streamer.promote_starved_remeshes();
	diagnostics = world.stream_diagnostics();
	if (diagnostics.remesh_starvation_promotions != 64U
		|| diagnostics.interactive_remesh_queue_depth != 64U)
	{
		std::fprintf(stderr,
			"async-worldgen: long-run starvation mismatch promotions=%llu "
			"interactive=%zu\n",
			static_cast<unsigned long long>(
				diagnostics.remesh_starvation_promotions),
			diagnostics.interactive_remesh_queue_depth);
		return (1);
	}
	std::printf("async-worldgen: long-run queue stress passed duplicates=10000 "
		"unique=64 promotions=64\n");
	return (0);
}

int WorldAsyncGenerationValidator::validate_incremental_seed_coalescing() noexcept
{
	std::unique_ptr<World> world_storage(new (std::nothrow) World());
	if (world_storage == nullptr)
		return (1);
	World &world = *world_storage;
	WorldChunkStreamer::RemeshPriority *priority;

	/* The fixture does not initialize World, so this exercises only the
	 * scheduler's owned priority metadata and cannot race a worker. */
	world.chunk_streamer.prioritize_chunk_remesh_from_origin(4, 4, 0, 0,
		FT_TRUE, FT_FALSE, 3, 12, 5, GAME_VOXEL_AIR_BLOCK, 7U, 0U,
		FT_FALSE);
	world.chunk_streamer.prioritize_chunk_remesh_from_origin(4, 4, 0, 0,
		FT_TRUE, FT_FALSE, 3, 12, 5, GAME_VOXEL_AIR_BLOCK, 7U, 0U,
		FT_FALSE);
	if (world.chunk_streamer.priority_remeshes_.size() != 1U)
	{
		std::fprintf(stderr,
			"async-worldgen: duplicate seed created another priority entry\n");
		return (1);
	}
	priority = &world.chunk_streamer.priority_remeshes_.front();
	if (priority->incremental_light_seeds.size() != 1U
		|| priority->incremental_additive_light != FT_TRUE)
	{
		std::fprintf(stderr,
			"async-worldgen: identical seed was not coalesced\n");
		return (1);
	}
	world.chunk_streamer.prioritize_chunk_remesh_from_origin(4, 4, 0, 0,
		FT_TRUE, FT_FALSE, 3, 12, 5, GAME_VOXEL_AIR_BLOCK, 9U, 0U,
		FT_FALSE);
	if (world.chunk_streamer.priority_remeshes_.size() != 1U)
	{
		std::fprintf(stderr,
			"async-worldgen: conflicting seed created another priority entry\n");
		return (1);
	}
	priority = &world.chunk_streamer.priority_remeshes_.front();
	if (!priority->incremental_light_seeds.empty()
		|| priority->incremental_additive_light != FT_FALSE
		|| priority->incremental_removal_light != FT_FALSE)
	{
		std::fprintf(stderr,
			"async-worldgen: conflicting seed was not downgraded to full solve\n");
		return (1);
	}
	return (0);
}

int WorldAsyncGenerationValidator::validate_missing_neighbor_mesh_boundary() noexcept
{
	WorldChunk chunks[2];
	chunk_mesh mesh;
	WorldGenerationPipeline::WorldChunkSnapshot snapshot;
	int32_t error_code;
	uint32_t snapshot_block_id;
	bool east_face_found;
	std::size_t vertex_index;

	error_code = chunks[0].chunk.initialize();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	chunks[0].chunk_x = 0;
	chunks[0].chunk_z = 0;
	chunks[0].world_x = 0;
	chunks[0].world_z = 0;
	chunks[0].initialized = true;
	error_code = chunks[0].chunk.write_generated_block(
		GAME_VOXEL_CHUNK_WIDTH - 1, 8, 8, VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code != FT_ERR_SUCCESS)
	{
		chunks[0].chunk.destroy();
		return (error_code);
	}
	error_code = chunk_mesh_initialize(mesh);
	if (error_code != FT_ERR_SUCCESS)
	{
		chunks[0].chunk.destroy();
		return (error_code);
	}
	error_code = ChunkNeighborMesher::generate_with_neighbors(mesh,
		chunks[0].chunk, 0, 0, chunks, 2);
	if (error_code != FT_ERR_SUCCESS)
	{
		chunk_mesh_destroy(mesh);
		chunks[0].chunk.destroy();
		return (error_code);
	}
	east_face_found = false;
	vertex_index = 0U;
	while (vertex_index < mesh.vertices.size())
	{
		if (mesh.vertices[vertex_index].face == CHUNK_MESH_FACE_EAST)
		{
			east_face_found = true;
			break ;
		}
		vertex_index += 1U;
	}
	if (!east_face_found)
	{
		std::fprintf(stderr,
			"async-worldgen: absent-neighbor boundary was left open\n");
		chunk_mesh_destroy(mesh);
		chunks[0].chunk.destroy();
		return (1);
	}
	error_code = chunks[1].chunk.initialize();
	if (error_code != FT_ERR_SUCCESS)
	{
		chunk_mesh_destroy(mesh);
		chunks[0].chunk.destroy();
		return (error_code);
	}
	chunks[1].chunk_x = 1;
	chunks[1].chunk_z = 0;
	chunks[1].world_x = GAME_VOXEL_CHUNK_WIDTH;
	chunks[1].world_z = 0;
	chunks[1].initialized = true;
	error_code = chunks[1].chunk.write_generated_block(0, 8, 8,
		VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code != FT_ERR_SUCCESS)
	{
		chunk_mesh_destroy(mesh);
		chunks[1].chunk.destroy();
		chunks[0].chunk.destroy();
		return (error_code);
	}
	error_code = chunk_mesh_clear(mesh);
	if (error_code != FT_ERR_SUCCESS)
	{
		chunk_mesh_destroy(mesh);
		chunks[1].chunk.destroy();
		chunks[0].chunk.destroy();
		return (error_code);
	}
	error_code = ChunkNeighborMesher::generate_with_neighbors(mesh,
		chunks[0].chunk, 0, 0, chunks, 2);
	if (error_code != FT_ERR_SUCCESS)
	{
		chunk_mesh_destroy(mesh);
		chunks[1].chunk.destroy();
		chunks[0].chunk.destroy();
		return (error_code);
	}
	east_face_found = false;
	vertex_index = 0U;
	while (vertex_index < mesh.vertices.size())
	{
		if (mesh.vertices[vertex_index].face == CHUNK_MESH_FACE_EAST)
		{
			east_face_found = true;
			break ;
		}
		vertex_index += 1U;
	}
	chunk_mesh_destroy(mesh);
	chunks[1].chunk.destroy();
	chunks[0].chunk.destroy();
	if (east_face_found)
	{
		std::fprintf(stderr,
			"async-worldgen: loaded-neighbor boundary face was not removed\n");
		return (1);
	}
	snapshot.chunk_x = 0;
	snapshot.chunk_z = 0;
	snapshot.east_border_valid = FT_FALSE;
	snapshot_block_id = GAME_VOXEL_AIR_BLOCK;
	error_code = WorldChunkSnapshotReader::lookup_snapshot_mesh_block(&snapshot,
		GAME_VOXEL_CHUNK_WIDTH, 8, 8, &snapshot_block_id);
	if (error_code != FT_ERR_SUCCESS
		|| snapshot_block_id != GAME_VOXEL_AIR_BLOCK)
	{
		std::fprintf(stderr,
			"async-worldgen: absent snapshot mesh border fallback mismatch "
			"error=%d block=%u\n", error_code, snapshot_block_id);
		return (1);
	}
	std::printf("async-worldgen: missing-neighbor mesh boundary passed\n");
	return (0);
}

int WorldAsyncGenerationValidator::validate_evicted_neighbor_invalidation() noexcept
{
	World world;
	WorldChunk &source = world.chunks[0];
	WorldChunk &target = world.chunks[1];
	uint16_t light_input_version;
	int32_t error_code;

	error_code = source.chunk.initialize();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = target.chunk.initialize();
	if (error_code != FT_ERR_SUCCESS)
	{
		source.destroy();
		return (error_code);
	}
	source.chunk_x = 0;
	source.chunk_z = 0;
	source.world_x = 0;
	source.world_z = 0;
	source.initialized = true;
	target.chunk_x = 1;
	target.chunk_z = 0;
	target.world_x = GAME_VOXEL_CHUNK_WIDTH;
	target.world_z = 0;
	target.initialized = true;
	target.mesh_dirty = false;
	world.center_chunk_x = 0;
	world.center_chunk_z = 0;
	world.chunk_index_center_x = 0;
	world.chunk_index_center_z = 0;
	world.chunk_index_valid = true;
	world.register_chunk_index(source);
	world.register_chunk_index(target);
	light_input_version = target.light_input_version;
	source.destroy();
	world.chunk_streamer.mark_neighbor_remeshes(0, 0, false, true);
	if (!target.mesh_dirty || target.light_input_version != light_input_version)
	{
		std::fprintf(stderr,
			"async-worldgen: evicted-neighbor invalidation mismatch dirty=%d "
			"light_input=%u expected=%u\n", target.mesh_dirty ? 1 : 0,
			static_cast<unsigned int>(target.light_input_version),
			static_cast<unsigned int>(light_input_version));
		target.destroy();
		return (1);
	}
	target.destroy();
	std::printf("async-worldgen: evicted-neighbor invalidation passed\n");
	return (0);
}

int WorldAsyncGenerationValidator::validate_immutable_capture_equivalence(
	World &world) noexcept
{
	WorldGenerationPipeline::WorldChunkSnapshot live_snapshot;
	WorldGenerationPipeline::WorldChunkSnapshot state_snapshot;
	const WorldChunk *target = world.find_chunk(0, 0);
	const WorldChunk *sources[9];
	int32_t error_code;
	int32_t offsets[9][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1},
		{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};

	if (target == nullptr || target->read_state == nullptr)
		return (1);
	for (int32_t index = 0; index < 9; ++index)
	{
		sources[index] = world.find_chunk(offsets[index][0], offsets[index][1]);
		if (sources[index] != nullptr && sources[index]->initialized
			&& sources[index]->read_state == nullptr)
			std::fprintf(stderr,
				"async-worldgen: live source has no read state index=%d chunk=(%d,%d)\n",
				index, sources[index]->chunk_x, sources[index]->chunk_z);
	}
	error_code = world.capture_remesh_snapshot(0, 0, live_snapshot);
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldChunkSnapshotCapture::capture_read_states(
			sources[0] == nullptr ? nullptr : sources[0]->read_state,
			sources[1] == nullptr ? nullptr : sources[1]->read_state,
			sources[2] == nullptr ? nullptr : sources[2]->read_state,
			sources[3] == nullptr ? nullptr : sources[3]->read_state,
			sources[4] == nullptr ? nullptr : sources[4]->read_state,
			sources[5] == nullptr ? nullptr : sources[5]->read_state,
			sources[6] == nullptr ? nullptr : sources[6]->read_state,
			sources[7] == nullptr ? nullptr : sources[7]->read_state,
			sources[8] == nullptr ? nullptr : sources[8]->read_state,
			state_snapshot);
	bool blocks_match = live_snapshot.blocks == state_snapshot.blocks;
	bool light_match = live_snapshot.existing_light == state_snapshot.existing_light;
	bool borders_match = live_snapshot.west_border == state_snapshot.west_border
		&& live_snapshot.east_border == state_snapshot.east_border
		&& live_snapshot.north_border == state_snapshot.north_border
		&& live_snapshot.south_border == state_snapshot.south_border;
	bool halo_match = live_snapshot.lighting_blocks
		== state_snapshot.lighting_blocks
		&& live_snapshot.lighting_existing_light
			== state_snapshot.lighting_existing_light;
	if (error_code != FT_ERR_SUCCESS || !blocks_match || !light_match
		|| !borders_match || !halo_match
		|| live_snapshot.lighting_halo_valid != state_snapshot.lighting_halo_valid)
	{
		std::fprintf(stderr,
			"async-worldgen: immutable capture mismatch error=%d blocks=%d "
			"light=%d borders=%d halo=%d halo_valid=%d/%d\n", error_code,
			blocks_match ? 1 : 0, light_match ? 1 : 0,
			borders_match ? 1 : 0, halo_match ? 1 : 0,
			live_snapshot.lighting_halo_valid ? 1 : 0,
			state_snapshot.lighting_halo_valid ? 1 : 0);
		for (std::size_t index = 0U; index < live_snapshot.lighting_blocks.size(); ++index)
			if (live_snapshot.lighting_blocks[index]
				!= state_snapshot.lighting_blocks[index])
			{
				std::fprintf(stderr,
					"async-worldgen: halo block mismatch index=%zu live=%u state=%u\n",
					index, live_snapshot.lighting_blocks[index],
					state_snapshot.lighting_blocks[index]);
				break ;
			}
		for (std::size_t index = 0U;
			index < live_snapshot.lighting_existing_light.size(); ++index)
			if (live_snapshot.lighting_existing_light[index]
				!= state_snapshot.lighting_existing_light[index])
			{
				std::fprintf(stderr,
					"async-worldgen: halo light mismatch index=%zu live=%u state=%u\n",
					index, live_snapshot.lighting_existing_light[index],
					state_snapshot.lighting_existing_light[index]);
				break ;
			}
		return (1);
	}
	return (0);
}

bool WorldAsyncGenerationValidator::mesh_payload_is_valid(
	const chunk_mesh &mesh) noexcept
{
	ft_size_t index;

	if (mesh.indices.size() != mesh.solid_indices.size()
		+ mesh.water_indices.size())
	{
		std::fprintf(stderr,
			"async-worldgen: mesh partition mismatch indices=%zu solid=%zu "
			"water=%zu\n", mesh.indices.size(), mesh.solid_indices.size(),
			mesh.water_indices.size());
		return (false);
	}
	if (!mesh.vertices.empty() && mesh.has_occupied_bounds == FT_FALSE)
	{
		std::fprintf(stderr,
			"async-worldgen: mesh vertices exist without occupied bounds\n");
		return (false);
	}
	index = 0U;
	while (index < mesh.solid_indices.size())
	{
		if (mesh.solid_indices[index] >= mesh.vertices.size())
		{
			std::fprintf(stderr,
				"async-worldgen: solid index out of bounds index=%zu vertex=%zu\n",
				static_cast<size_t>(mesh.solid_indices[index]),
				mesh.vertices.size());
			return (false);
		}
		index += 1U;
	}
	index = 0U;
	while (index < mesh.water_indices.size())
	{
		if (mesh.water_indices[index] >= mesh.vertices.size())
		{
			std::fprintf(stderr,
				"async-worldgen: water index out of bounds index=%zu vertex=%zu\n",
				static_cast<size_t>(mesh.water_indices[index]),
				mesh.vertices.size());
			return (false);
		}
		index += 1U;
	}
	return (true);
}

bool WorldAsyncGenerationValidator::playable_area_is_ready(
	const World &world) noexcept
{
	const int32_t radius = WorldCoordinates::render_distance_to_chunk_radius(
		WorldCoordinates::MIN_RENDER_DISTANCE);
	const int32_t radius_squared = radius * radius;
	int32_t offset_z;

	offset_z = -radius;
	while (offset_z <= radius)
	{
		int32_t offset_x = -radius;
		while (offset_x <= radius)
		{
			if (offset_x * offset_x + offset_z * offset_z <= radius_squared)
			{
				const WorldChunk *chunk = world.find_chunk(
					world.center_chunk_x + offset_x,
					world.center_chunk_z + offset_z);
				if (chunk == nullptr
					|| !WorldChunk::mesh_is_drawable(chunk->mesh)
					|| !WorldAsyncGenerationValidator::mesh_payload_is_valid(
						chunk->mesh))
					return (false);
			}
			offset_x += 1;
		}
		offset_z += 1;
	}
	return (true);
}

void WorldAsyncGenerationValidator::report_playable_area_gaps(
	const World &world) noexcept
{
	const int32_t radius = WorldCoordinates::render_distance_to_chunk_radius(
		WorldCoordinates::MIN_RENDER_DISTANCE);
	const int32_t radius_squared = radius * radius;
	int32_t offset_z;
	int32_t reported;

	reported = 0;
	offset_z = -radius;
	while (offset_z <= radius)
	{
		int32_t offset_x = -radius;
		while (offset_x <= radius)
		{
			if (offset_x * offset_x + offset_z * offset_z <= radius_squared)
			{
				const int32_t chunk_x = world.center_chunk_x + offset_x;
				const int32_t chunk_z = world.center_chunk_z + offset_z;
				const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
				if (chunk == nullptr)
				{
					std::fprintf(stderr,
						"async-worldgen: missing playable chunk=(%d,%d)\n",
						chunk_x, chunk_z);
					for (const WorldChunkStreamer::StreamCandidate &candidate
						: world.chunk_streamer.stream_candidates_)
					{
						if (candidate.offset_x == offset_x
							&& candidate.offset_z == offset_z)
						{
							std::fprintf(stderr,
								"async-worldgen: candidate state=%d request=%llu "
								"queued_frame=%llu retry=%d last_error=%d\n",
								static_cast<int>(candidate.state),
								static_cast<unsigned long long>(candidate.request_id),
								static_cast<unsigned long long>(candidate.queued_frame),
								candidate.retry_count, candidate.last_error);
							break ;
						}
					}
					reported += 1;
				}
				else if (chunk->mesh.has_occupied_bounds == FT_FALSE
					|| chunk->mesh.vertices.empty()
					|| (chunk->mesh.solid_indices.empty()
						&& chunk->mesh.water_indices.empty()))
				{
					std::fprintf(stderr,
						"async-worldgen: non-drawable playable chunk=(%d,%d) "
						"occupied=%d vertices=%zu solid=%zu water=%zu\n",
						chunk_x, chunk_z,
						chunk->mesh.has_occupied_bounds == FT_TRUE ? 1 : 0,
						chunk->mesh.vertices.size(),
						chunk->mesh.solid_indices.size(),
						chunk->mesh.water_indices.size());
					reported += 1;
				}
				else
				{
					bool nonzero_light = false;
					for (int32_t light_z = 0;
						light_z < GAME_VOXEL_CHUNK_DEPTH && !nonzero_light;
						++light_z)
						for (int32_t light_y = 0;
							light_y < GAME_VOXEL_CHUNK_HEIGHT && !nonzero_light;
							++light_y)
							for (int32_t light_x = 0;
								light_x < GAME_VOXEL_CHUNK_WIDTH; ++light_x)
							{
								if (chunk->light.get(light_x, light_y, light_z) != 0U)
								{
									nonzero_light = true;
									break ;
								}
							}
					if (!chunk->light_ready_for_render
						|| !chunk->light_buffer_is_valid()
						|| !nonzero_light)
					{
						std::fprintf(stderr,
							"async-worldgen: invalid playable light baseline "
							"chunk=(%d,%d) ready=%d valid=%d nonzero=%d "
							"content=%u light=%u input=%u computed=%u\n",
							chunk_x, chunk_z,
							chunk->light_ready_for_render ? 1 : 0,
							chunk->light_buffer_is_valid() ? 1 : 0,
							nonzero_light ? 1 : 0,
							chunk->content_version, chunk->light_version,
							chunk->light_input_version,
							chunk->computed_light_input_version);
						reported += 1;
					}
				}
			}
			offset_x += 1;
		}
		offset_z += 1;
	}
	if (reported == 0)
		std::fprintf(stderr,
			"async-worldgen: playable area gap could not be classified\n");
}

const WorldChunk *WorldAsyncGenerationValidator::stream_until_ready(World &world,
	int32_t *frame, bool *startup_edit_applied,
	std::size_t *remesh_queue_peak,
	int32_t *first_visible_mesh_frame, uint64_t *max_update_us,
	bool *interactive_priority_observed,
	bool *generation_progress_with_priority_pending) noexcept
{
	/* Require the same center-plus-playable-ring contract used by loading. */
	const int32_t target_chunk_x = -1;
	const int32_t target_chunk_z = 0;
	World::StreamDiagnostics initial_diagnostics;
	int32_t error_code;
	int32_t priority_edit_rounds;
	int32_t initial_loaded_chunk_count;

	if (startup_edit_applied == nullptr || remesh_queue_peak == nullptr
		|| first_visible_mesh_frame == nullptr || max_update_us == nullptr
		|| interactive_priority_observed == nullptr
		|| generation_progress_with_priority_pending == nullptr)
		return (nullptr);
	*startup_edit_applied = false;
	*remesh_queue_peak = 0U;
	*first_visible_mesh_frame = -1;
	*max_update_us = 0U;
	*interactive_priority_observed = false;
	*generation_progress_with_priority_pending = false;
	initial_diagnostics = world.stream_diagnostics();
	initial_loaded_chunk_count = world.loaded_chunk_count;
	priority_edit_rounds = 0;

	while ((!WorldAsyncGenerationValidator::playable_area_is_ready(world)
			|| world.find_chunk(target_chunk_x, target_chunk_z) == nullptr
			|| !WorldChunk::mesh_is_drawable(world.find_chunk(target_chunk_x,
				target_chunk_z)->mesh))
		&& *frame < ASYNC_WORLDGEN_MAX_STARTUP_FRAMES)
	{
		/* Repeatedly exercise the same priority edit path while generation and
		 * ordinary arrival remeshes are still active.  The interval is long
		 * enough for the request to reach the worker, but the repeated rounds
		 * keep interactive work pending long enough to expose starvation. */
		if (*frame >= 2 && (*frame - 2)
			% ASYNC_STARTUP_PRIORITY_EDIT_INTERVAL == 0
			&& priority_edit_rounds < ASYNC_STARTUP_PRIORITY_EDIT_ROUNDS)
		{
			const WorldChunk *edit_chunk = world.find_chunk(0, 0);
			if (edit_chunk != nullptr && edit_chunk->initialized)
			{
				int32_t edit_z = 0;
				while (edit_z < GAME_VOXEL_CHUNK_DEPTH
					&& priority_edit_rounds
					< ASYNC_STARTUP_PRIORITY_EDIT_ROUNDS)
				{
					int32_t edit_y = 0;
					while (edit_y < GAME_VOXEL_CHUNK_HEIGHT
						&& priority_edit_rounds
						< ASYNC_STARTUP_PRIORITY_EDIT_ROUNDS)
					{
						int32_t edit_x = 0;
						while (edit_x < GAME_VOXEL_CHUNK_WIDTH
							&& priority_edit_rounds
							< ASYNC_STARTUP_PRIORITY_EDIT_ROUNDS)
						{
							uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
							if (edit_chunk->chunk.read_block(edit_x, edit_y,
								edit_z, &block_id) == FT_ERR_SUCCESS
								&& block_id != GAME_VOXEL_AIR_BLOCK
								&& voxel_block_is_breakable(block_id) != FT_FALSE)
							{
								const int32_t world_x = edit_chunk->world_x + edit_x;
								const int32_t world_z = edit_chunk->world_z + edit_z;
								bool edit_sequence_succeeded;

								edit_sequence_succeeded = true;
								int32_t edit_round = 0;
								while (edit_round < ASYNC_STARTUP_EDIT_REPETITIONS
									&& edit_sequence_succeeded)
								{
									if (world.delete_block_at(world_x, edit_y,
										world_z) != FT_ERR_SUCCESS
										|| world.place_block_at(world_x, edit_y,
										world_z, block_id) != FT_ERR_SUCCESS)
										edit_sequence_succeeded = false;
									edit_round += 1;
								}
				if (edit_sequence_succeeded)
				{
					*startup_edit_applied = true;
					*interactive_priority_observed = true;
					priority_edit_rounds += 1;
								}
							}
							edit_x += 1;
						}
						edit_y += 1;
					}
					edit_z += 1;
				}
			}
		}
		const std::chrono::steady_clock::time_point update_start =
			std::chrono::steady_clock::now();
		error_code = world.update_around(0.0, 0.0, 4,
				WorldCoordinates::REQUIRED_VISIBLE_DISTANCE);
		World::StreamDiagnostics current_diagnostics =
			world.stream_diagnostics();
		if (current_diagnostics.remesh_queue_peak > *remesh_queue_peak)
			*remesh_queue_peak = current_diagnostics.remesh_queue_peak;
		if (current_diagnostics.interactive_remesh_queue_depth
			> current_diagnostics.remesh_priority_queue_depth
			|| (current_diagnostics.remesh_priority_queue_depth == 0U
				&& current_diagnostics.oldest_remesh_queue_age != 0U)
			|| current_diagnostics.oldest_remesh_queue_age
				> current_diagnostics.frame)
		{
			std::fprintf(stderr,
				"async-worldgen: invalid remesh queue metrics frame=%llu "
				"depth=%zu interactive=%zu oldest=%llu\n",
				static_cast<unsigned long long>(current_diagnostics.frame),
				current_diagnostics.remesh_priority_queue_depth,
				current_diagnostics.interactive_remesh_queue_depth,
				static_cast<unsigned long long>(
					current_diagnostics.oldest_remesh_queue_age));
			return (nullptr);
		}
		if (current_diagnostics.interactive_remesh_queue_depth > 0U)
		{
			*interactive_priority_observed = true;
			if (world.loaded_chunk_count > initial_loaded_chunk_count
				|| current_diagnostics.progress_frame
					> initial_diagnostics.progress_frame)
				*generation_progress_with_priority_pending = true;
		}
		if (*startup_edit_applied
			&& world.loaded_chunk_count > initial_loaded_chunk_count)
			*generation_progress_with_priority_pending = true;
		if (*first_visible_mesh_frame < 0
			&& current_diagnostics.playable_drawable_count
				> initial_diagnostics.playable_drawable_count)
			*first_visible_mesh_frame = *frame;
		const uint64_t update_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - update_start).count());
		if (*max_update_us < update_us)
			*max_update_us = update_us;
		if (error_code != FT_ERR_SUCCESS)
		{
			World::StreamDiagnostics diagnostics = current_diagnostics;
			std::fprintf(stderr,
				"async-worldgen: update failed frame=%d error=%d "
				"pending=%zu ready=%zu active=%zu failed=%zu retry=%zu last_error=%d\n",
				*frame, error_code, diagnostics.pending_count,
				diagnostics.ready_count, diagnostics.active_generation_count,
				diagnostics.failed_count,
				diagnostics.retryable_count, diagnostics.last_error);
			return (nullptr);
		}
		if (update_us >= ASYNC_WORLDGEN_MAX_UPDATE_US)
		{
			World::StreamDiagnostics diagnostics = current_diagnostics;
			std::fprintf(stderr,
				"async-worldgen: update budget exceeded frame=%d duration_us=%llu "
				"loaded=%d pending=%zu ready=%zu active=%zu failed=%zu retry=%zu "
				"last_error=%d\n", *frame,
				static_cast<unsigned long long>(update_us),
				world.loaded_chunk_count, diagnostics.pending_count,
				diagnostics.ready_count, diagnostics.active_generation_count,
				diagnostics.failed_count,
				diagnostics.retryable_count, diagnostics.last_error);
			return (nullptr);
		}
		if ((*frame % 100) == 0)
		{
			World::StreamDiagnostics diagnostics = current_diagnostics;
			std::fprintf(stderr,
				"async-worldgen: progress frame=%d loaded=%d pending=%zu "
				"ready=%zu active=%zu failed=%zu retry=%zu result_age_ns=%llu\n", *frame,
				world.loaded_chunk_count, diagnostics.pending_count,
				diagnostics.ready_count, diagnostics.active_generation_count,
				diagnostics.failed_count,
				diagnostics.retryable_count,
				static_cast<unsigned long long>(
					diagnostics.oldest_result_age_nanoseconds));
		}
		/* Do not busy-spin while the persistent workers are generating. A
		 * tight validator loop can otherwise consume the scheduling opportunity
		 * needed by the very workers this test is intended to exercise. */
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		*frame += 1;
	}
	return (world.find_chunk(target_chunk_x, target_chunk_z));
}

void WorldAsyncGenerationValidator::report_failure(const World &world,
	int32_t frame) noexcept
{
	World::StreamDiagnostics diagnostics;

	diagnostics = world.stream_diagnostics();
	std::fprintf(stderr, "async-worldgen: failed frame=%d loaded=%d\n", frame,
		world.loaded_chunk_count);
	std::fprintf(stderr,
		"async-worldgen: pending=%zu failed=%zu retry=%zu error=%d\n",
		diagnostics.pending_count, diagnostics.failed_count,
		diagnostics.retryable_count, diagnostics.last_error);
	std::fprintf(stderr,
		"async-worldgen: remesh_priority=%zu interactive=%zu oldest=%llu "
		"capture=%zu pipeline=%zu remesh_in_flight=%zu\n",
		diagnostics.remesh_priority_queue_depth,
		diagnostics.interactive_remesh_queue_depth,
		static_cast<unsigned long long>(diagnostics.oldest_remesh_queue_age),
		world.chunk_streamer.remesh_capture_in_flight_.load(),
		world.chunk_streamer.pipeline().queued_count(),
		world.chunk_streamer.pipeline().remesh_in_flight_count());
	if (!WorldAsyncGenerationValidator::playable_area_is_ready(world))
	{
		std::fprintf(stderr,
			"async-worldgen: playable startup area is incomplete around "
			"center=(%d,%d)\n", world.center_chunk_x, world.center_chunk_z);
		WorldAsyncGenerationValidator::report_playable_area_gaps(world);
	}
}

int WorldAsyncGenerationValidator::validate() const
{
	/* World and game_voxel_chunk both own sizeable voxel storage.  Keeping
	 * them as automatic variables makes this validator exceed the default
	 * Windows thread stack after the lighting buffers were expanded.  The
	 * resulting 0xC00000FD looked like a generation hang because no validator
	 * output was flushed before the process died. */
	std::unique_ptr<World> world_storage(new (std::nothrow) World());
	std::unique_ptr<game_voxel_chunk> expected_storage(
		new (std::nothrow) game_voxel_chunk());
	World *world_pointer;
	game_voxel_chunk *expected_pointer;
	const WorldChunk *generated;
	int32_t error_code;
	int32_t frame;
	int32_t initial_loaded_chunk_count;
	int32_t final_loaded_chunk_count;
	std::size_t remesh_queue_peak;
	int32_t first_visible_mesh_frame;
	uint64_t max_update_us;
	bool interactive_priority_observed;
	bool generation_progress_with_priority_pending;
	World::StreamDiagnostics final_diagnostics;

	if (!world_storage || !expected_storage)
		return (1);
	world_pointer = world_storage.get();
	expected_pointer = expected_storage.get();

	error_code = WorldAsyncGenerationValidator::validate_diagonal_lighting_halo();
	if (error_code != 0)
		return (error_code);
	error_code = WorldAsyncGenerationValidator::validate_cardinal_lighting_propagation();
	if (error_code != 0)
		return (error_code);
	error_code = WorldAsyncGenerationValidator::validate_diagonal_lighting_propagation();
	if (error_code != 0)
		return (error_code);
	error_code = WorldAsyncGenerationValidator::validate_light_scheduler_configuration();
	if (error_code != 0)
		return (error_code);
	error_code = WorldAsyncGenerationValidator::validate_remesh_priority_metrics();
	if (error_code != 0)
		return (error_code);
	error_code = WorldAsyncGenerationValidator::validate_incremental_seed_coalescing();
	if (error_code != 0)
		return (error_code);
	error_code = WorldAsyncGenerationValidator::validate_missing_neighbor_mesh_boundary();
	if (error_code != 0)
		return (error_code);
	error_code = WorldAsyncGenerationValidator::validate_evicted_neighbor_invalidation();
	if (error_code != 0)
		return (error_code);
	error_code = world_pointer->initialize("async-validator");
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "async-worldgen: initialize failed error=%d\n",
			error_code);
		return (1);
	}
	if (WorldAsyncGenerationValidator::validate_immutable_capture_equivalence(
		*world_pointer) != 0)
	{
		std::fprintf(stderr,
			"async-worldgen: immutable capture equivalence failed\n");
		world_pointer->destroy();
		return (1);
	}
	initial_loaded_chunk_count = world_pointer->loaded_chunk_count;
	frame = 0;
	bool startup_edit_applied = false;
	generated = WorldAsyncGenerationValidator::stream_until_ready(*world_pointer,
			&frame, &startup_edit_applied, &remesh_queue_peak,
			&first_visible_mesh_frame, &max_update_us,
			&interactive_priority_observed,
			&generation_progress_with_priority_pending);
	if (generated == nullptr
		|| frame >= ASYNC_WORLDGEN_MAX_STARTUP_FRAMES
		|| world_pointer->loaded_chunk_count <= initial_loaded_chunk_count
		|| !startup_edit_applied
		|| !interactive_priority_observed
		|| !generation_progress_with_priority_pending
		|| first_visible_mesh_frame < 0
		|| (generated != nullptr
			&& !WorldAsyncGenerationValidator::mesh_payload_is_valid(
				generated->mesh))
		|| expected_pointer->initialize() != FT_ERR_SUCCESS
		|| voxel_generate_chunk(*expected_pointer, -16, 0,
			"async-validator") != FT_ERR_SUCCESS
		|| !WorldAsyncGenerationValidator::chunks_equal(generated->chunk,
			*expected_pointer))
	{
		if (!startup_edit_applied)
			std::fprintf(stderr,
				"async-worldgen: startup edit could not be applied while loading\n");
		WorldAsyncGenerationValidator::report_failure(*world_pointer, frame);
		error_code = expected_pointer->destroy();
		if (error_code != FT_ERR_SUCCESS)
			std::fprintf(stderr,
				"async-worldgen: expected cleanup failed error=%d\n", error_code);
		world_pointer->destroy();
		return (1);
	}
	final_loaded_chunk_count = world_pointer->loaded_chunk_count;
	final_diagnostics = world_pointer->stream_diagnostics();
	error_code = expected_pointer->destroy();
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"async-worldgen: expected cleanup failed error=%d\n", error_code);
		return (1);
	}
	world_pointer->destroy();
	std::printf("async-worldgen: ok frame=%d initial_loaded=%d final_loaded=%d\n",
		frame, initial_loaded_chunk_count, final_loaded_chunk_count);
	std::printf("async-worldgen: startup_edit=1 remesh_queue_peak=%zu "
		"stale_result_count=%zu stale_stream=%zu stale_remesh=%zu "
		"incremental_remesh=%llu full_remesh=%llu geometry_only=%llu "
		"canceled_remesh=%llu "
		"first_visible_mesh_frame=%d max_update_us=%llu "
		"startup_budget_frames=%d\n",
		remesh_queue_peak,
		final_diagnostics.stale_result_count,
		final_diagnostics.stale_stream_result_count,
		final_diagnostics.stale_remesh_result_count,
		static_cast<unsigned long long>(
			final_diagnostics.remesh_incremental_completed_count),
		static_cast<unsigned long long>(
			final_diagnostics.remesh_full_completed_count),
		static_cast<unsigned long long>(
			final_diagnostics.remesh_geometry_only_count),
		static_cast<unsigned long long>(
			final_diagnostics.remesh_canceled_count),
		first_visible_mesh_frame,
		static_cast<unsigned long long>(max_update_us),
		ASYNC_WORLDGEN_MAX_STARTUP_FRAMES);
	std::printf("async-worldgen-metrics: {\"frames\":%d,"
		"\"initial_loaded\":%d,\"final_loaded\":%d,"
		"\"remesh_queue_peak\":%zu,\"priority_queue_depth\":%zu,"
		"\"starvation_promotions\":%llu,"
		"\"interactive_queue_depth\":%zu,\"oldest_remesh_age\":%llu,"
		"\"snapshot_bytes\":%llu,\"capture_ns\":%llu,"
		"\"capture_count\":%llu,"
		"\"scanned_cells\":%llu,\"propagated_cells\":%llu,"
		"\"light_queue_peak\":%llu,\"remesh_completed\":%llu,"
		"\"incremental_remesh\":%llu,\"full_remesh\":%llu,"
		"\"geometry_only\":%llu,\"canceled_remesh\":%llu,"
		"\"stale_results\":%zu,\"stale_stream\":%zu,"
		"\"stale_remesh\":%zu,\"stale_capture\":%zu,"
		"\"max_update_us\":%llu,\"first_visible_mesh_frame\":%d}\n",
		frame, initial_loaded_chunk_count, final_loaded_chunk_count,
		remesh_queue_peak,
		final_diagnostics.remesh_priority_queue_depth,
		static_cast<unsigned long long>(
			final_diagnostics.remesh_starvation_promotions),
		final_diagnostics.interactive_remesh_queue_depth,
		static_cast<unsigned long long>(
			final_diagnostics.oldest_remesh_queue_age),
		static_cast<unsigned long long>(final_diagnostics.remesh_snapshot_bytes),
		static_cast<unsigned long long>(
			final_diagnostics.remesh_capture_duration_nanoseconds),
		static_cast<unsigned long long>(final_diagnostics.remesh_capture_count),
		static_cast<unsigned long long>(final_diagnostics.remesh_scanned_cells),
		static_cast<unsigned long long>(final_diagnostics.remesh_propagated_cells),
		static_cast<unsigned long long>(final_diagnostics.remesh_light_queue_peak),
		static_cast<unsigned long long>(final_diagnostics.remesh_completed_count),
		static_cast<unsigned long long>(
			final_diagnostics.remesh_incremental_completed_count),
		static_cast<unsigned long long>(
			final_diagnostics.remesh_full_completed_count),
		static_cast<unsigned long long>(
			final_diagnostics.remesh_geometry_only_count),
		static_cast<unsigned long long>(final_diagnostics.remesh_canceled_count),
		final_diagnostics.stale_result_count,
		final_diagnostics.stale_stream_result_count,
		final_diagnostics.stale_remesh_result_count,
		final_diagnostics.stale_remesh_capture_count,
		static_cast<unsigned long long>(max_update_us),
		first_visible_mesh_frame);
	return (0);
}
