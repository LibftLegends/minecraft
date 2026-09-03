#include "../../src/validators/WorldAsyncGenerationValidator.hpp"
#include <chrono>
#include <cstdio>
#include <thread>

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
	int32_t *frame) noexcept
{
	/* Require the same center-plus-playable-ring contract used by loading. */
	const int32_t target_chunk_x = -1;
	const int32_t target_chunk_z = 0;
	int32_t error_code;

	while (!WorldAsyncGenerationValidator::playable_area_is_ready(world)
		&& *frame < 4000)
	{
		const std::chrono::steady_clock::time_point update_start =
			std::chrono::steady_clock::now();
		error_code = world.update_around(0.0, 0.0, 4,
				WorldCoordinates::REQUIRED_VISIBLE_DISTANCE);
		const uint64_t update_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - update_start).count());
		if (error_code != FT_ERR_SUCCESS)
		{
			World::StreamDiagnostics diagnostics = world.stream_diagnostics();
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
			World::StreamDiagnostics diagnostics = world.stream_diagnostics();
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
			World::StreamDiagnostics diagnostics = world.stream_diagnostics();
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

	error_code = world.initialize("async-validator");
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "async-worldgen: initialize failed error=%d\n",
			error_code);
		return (1);
	}
	frame = 0;
	generated = WorldAsyncGenerationValidator::stream_until_ready(world,
			&frame);
	if (generated == nullptr
		|| (generated != nullptr
			&& !WorldAsyncGenerationValidator::mesh_payload_is_valid(
				generated->mesh))
		|| expected.initialize() != FT_ERR_SUCCESS
		|| voxel_generate_chunk(expected, -16, 0,
			"async-validator") != FT_ERR_SUCCESS
		|| !WorldAsyncGenerationValidator::chunks_equal(generated->chunk,
			expected))
	{
		WorldAsyncGenerationValidator::report_failure(world, frame);
		(void)expected.destroy();
		world.destroy();
		return (1);
	}
	(void)expected.destroy();
	world.destroy();
	std::printf("async-worldgen: ok frame=%d\n", frame);
	return (0);
}
