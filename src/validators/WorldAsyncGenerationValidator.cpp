#include "../../src/validators/WorldAsyncGenerationValidator.hpp"
#include "../../src/world/WorldChunkSnapshotCapture.hpp"
#include "../../src/world/WorldChunkSnapshotReader.hpp"
#include <chrono>
#include <cstdio>
#include <thread>

namespace
{
	static const int32_t ASYNC_WORLDGEN_MAX_STARTUP_FRAMES = 1200;
	static const int32_t ASYNC_STARTUP_EDIT_REPETITIONS = 4;
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
	int32_t *first_visible_mesh_frame) noexcept
{
	/* Require the same center-plus-playable-ring contract used by loading. */
	const int32_t target_chunk_x = -1;
	const int32_t target_chunk_z = 0;
	World::StreamDiagnostics initial_diagnostics;
	int32_t error_code;
	bool edit_attempted;

	if (startup_edit_applied == nullptr || remesh_queue_peak == nullptr
		|| first_visible_mesh_frame == nullptr)
		return (nullptr);
	*startup_edit_applied = false;
	*remesh_queue_peak = 0U;
	*first_visible_mesh_frame = -1;
	initial_diagnostics = world.stream_diagnostics();
	edit_attempted = false;

	while (!WorldAsyncGenerationValidator::playable_area_is_ready(world)
		&& *frame < ASYNC_WORLDGEN_MAX_STARTUP_FRAMES)
	{
		/* Exercise the same priority edit path while generation and ordinary
		 * arrival remeshes are still active.  A previous implementation could
		 * let this work starve generation or leave a neighboring border dirty
		 * indefinitely, while the normal startup validator only observed
		 * passive loading. */
		if (!edit_attempted && *frame >= 2)
		{
			const WorldChunk *edit_chunk = world.find_chunk(0, 0);
			if (edit_chunk != nullptr && edit_chunk->initialized)
			{
				int32_t edit_z = 0;
				while (edit_z < GAME_VOXEL_CHUNK_DEPTH && !edit_attempted)
				{
					int32_t edit_y = 0;
					while (edit_y < GAME_VOXEL_CHUNK_HEIGHT
						&& !edit_attempted)
					{
						int32_t edit_x = 0;
						while (edit_x < GAME_VOXEL_CHUNK_WIDTH)
						{
							uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
							if (edit_chunk->chunk.read_block(edit_x, edit_y,
								edit_z, &block_id) == FT_ERR_SUCCESS
								&& block_id != GAME_VOXEL_AIR_BLOCK
								&& voxel_block_is_breakable(block_id) != FT_FALSE)
							{
								const int32_t world_x = edit_chunk->world_x + edit_x;
								const int32_t world_z = edit_chunk->world_z + edit_z;
								int32_t edit_round;
								bool edit_sequence_succeeded;

								edit_round = 0;
								edit_sequence_succeeded = true;
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
									*startup_edit_applied = true;
								edit_attempted = true;
							}
							edit_x += 1;
						}
						edit_y += 1;
					}
					edit_z += 1;
				}
			}
			if (!edit_attempted)
				edit_attempted = true;
		}
		const std::chrono::steady_clock::time_point update_start =
			std::chrono::steady_clock::now();
		error_code = world.update_around(0.0, 0.0, 4,
				WorldCoordinates::REQUIRED_VISIBLE_DISTANCE);
		World::StreamDiagnostics current_diagnostics =
			world.stream_diagnostics();
		if (current_diagnostics.remesh_queue_peak > *remesh_queue_peak)
			*remesh_queue_peak = current_diagnostics.remesh_queue_peak;
		if (*first_visible_mesh_frame < 0
			&& current_diagnostics.playable_drawable_count
				> initial_diagnostics.playable_drawable_count)
			*first_visible_mesh_frame = *frame;
		const uint64_t update_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - update_start).count());
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
		if (update_us >= 1000000U)
		{
			World::StreamDiagnostics diagnostics = current_diagnostics;
			std::fprintf(stderr,
				"async-worldgen: slow update frame=%d duration_us=%llu "
				"loaded=%d pending=%zu ready=%zu active=%zu failed=%zu retry=%zu "
				"last_error=%d\n", *frame,
				static_cast<unsigned long long>(update_us),
				world.loaded_chunk_count, diagnostics.pending_count,
				diagnostics.ready_count, diagnostics.active_generation_count,
				diagnostics.failed_count,
				diagnostics.retryable_count, diagnostics.last_error);
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
	World world;
	game_voxel_chunk expected;
	const WorldChunk *generated;
	int32_t error_code;
	int32_t frame;
	int32_t initial_loaded_chunk_count;
	int32_t final_loaded_chunk_count;
	std::size_t remesh_queue_peak;
	int32_t first_visible_mesh_frame;
	World::StreamDiagnostics final_diagnostics;

	error_code = WorldAsyncGenerationValidator::validate_diagonal_lighting_halo();
	if (error_code != 0)
		return (error_code);
	error_code = WorldAsyncGenerationValidator::validate_cardinal_lighting_propagation();
	if (error_code != 0)
		return (error_code);
	error_code = WorldAsyncGenerationValidator::validate_diagonal_lighting_propagation();
	if (error_code != 0)
		return (error_code);
	error_code = world.initialize("async-validator");
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "async-worldgen: initialize failed error=%d\n",
			error_code);
		return (1);
	}
	initial_loaded_chunk_count = world.loaded_chunk_count;
	frame = 0;
	bool startup_edit_applied = false;
	generated = WorldAsyncGenerationValidator::stream_until_ready(world,
			&frame, &startup_edit_applied, &remesh_queue_peak,
			&first_visible_mesh_frame);
	if (generated == nullptr
		|| frame >= ASYNC_WORLDGEN_MAX_STARTUP_FRAMES
		|| world.loaded_chunk_count <= initial_loaded_chunk_count
		|| !startup_edit_applied
		|| first_visible_mesh_frame < 0
		|| (generated != nullptr
			&& !WorldAsyncGenerationValidator::mesh_payload_is_valid(
				generated->mesh))
		|| expected.initialize() != FT_ERR_SUCCESS
		|| voxel_generate_chunk(expected, -16, 0,
			"async-validator") != FT_ERR_SUCCESS
		|| !WorldAsyncGenerationValidator::chunks_equal(generated->chunk,
			expected))
	{
		if (!startup_edit_applied)
			std::fprintf(stderr,
				"async-worldgen: startup edit could not be applied while loading\n");
		WorldAsyncGenerationValidator::report_failure(world, frame);
		(void)expected.destroy();
		world.destroy();
		return (1);
	}
	final_loaded_chunk_count = world.loaded_chunk_count;
	final_diagnostics = world.stream_diagnostics();
	(void)expected.destroy();
	world.destroy();
	std::printf("async-worldgen: ok frame=%d initial_loaded=%d final_loaded=%d\n",
		frame, initial_loaded_chunk_count, final_loaded_chunk_count);
	std::printf("async-worldgen: startup_edit=1 remesh_queue_peak=%zu "
		"stale_result_count=%zu stale_stream=%zu stale_remesh=%zu "
		"first_visible_mesh_frame=%d startup_budget_frames=%d\n",
		remesh_queue_peak,
		final_diagnostics.stale_result_count,
		final_diagnostics.stale_stream_result_count,
		final_diagnostics.stale_remesh_result_count,
		first_visible_mesh_frame, ASYNC_WORLDGEN_MAX_STARTUP_FRAMES);
	std::printf("async-worldgen-metrics: {\"frames\":%d,"
		"\"initial_loaded\":%d,\"final_loaded\":%d,"
		"\"remesh_queue_peak\":%zu,\"snapshot_bytes\":%llu,"
		"\"scanned_cells\":%llu,\"propagated_cells\":%llu,"
		"\"light_queue_peak\":%llu,\"remesh_completed\":%llu,"
		"\"stale_results\":%zu,\"stale_stream\":%zu,"
		"\"stale_remesh\":%zu,\"first_visible_mesh_frame\":%d}\n",
		frame, initial_loaded_chunk_count, final_loaded_chunk_count,
		remesh_queue_peak,
		static_cast<unsigned long long>(final_diagnostics.remesh_snapshot_bytes),
		static_cast<unsigned long long>(final_diagnostics.remesh_scanned_cells),
		static_cast<unsigned long long>(final_diagnostics.remesh_propagated_cells),
		static_cast<unsigned long long>(final_diagnostics.remesh_light_queue_peak),
		static_cast<unsigned long long>(final_diagnostics.remesh_completed_count),
		final_diagnostics.stale_result_count,
		final_diagnostics.stale_stream_result_count,
		final_diagnostics.stale_remesh_result_count,
		first_visible_mesh_frame);
	return (0);
}
