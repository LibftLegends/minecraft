#include "../../src/validators/BlockEditValidator.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

BlockEditValidator::BlockEditValidator()
{
}

BlockEditValidator::BlockEditValidator(const BlockEditValidator &other)
	: IValidator(other)
{
	*this = other;
}

BlockEditValidator::~BlockEditValidator()
{
}

BlockEditValidator &BlockEditValidator::operator=(const BlockEditValidator &other)
{
	(void)other;
	return (*this);
}

size_t BlockEditValidator::mesh_index_count_for_block(const World &world,
	int32_t world_x, int32_t world_z)
{
	int32_t	chunk_x;
	int32_t	chunk_z;
	int32_t	index;

	chunk_x = world_x >= 0 ? world_x / GAME_VOXEL_CHUNK_WIDTH : -(((-world_x)
				+ GAME_VOXEL_CHUNK_WIDTH - 1) / GAME_VOXEL_CHUNK_WIDTH);
	chunk_z = world_z >= 0 ? world_z / GAME_VOXEL_CHUNK_DEPTH : -(((-world_z)
				+ GAME_VOXEL_CHUNK_DEPTH - 1) / GAME_VOXEL_CHUNK_DEPTH);
	index = 0;
	while (index < world.chunk_count)
	{
		if (world.chunks[index].initialized == true
			&& world.chunks[index].chunk_x == chunk_x
			&& world.chunks[index].chunk_z == chunk_z)
			return (world.chunks[index].mesh.indices.size());
		index = index + 1;
	}
	return (0U);
}

uint64_t BlockEditValidator::mesh_revision_for_block(const World &world,
	int32_t world_x, int32_t world_z)
{
	int32_t chunk_x;
	int32_t chunk_z;
	const WorldChunk *chunk;

	chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	chunk = world.find_chunk(chunk_x, chunk_z);
	if (chunk == nullptr)
		return (0U);
	return (chunk->mesh_revision);
}

int BlockEditValidator::wait_for_mesh_revision(World &world, int32_t world_x,
	int32_t world_z, uint64_t previous_revision,
	uint64_t *latency_milliseconds) noexcept
{
	std::chrono::steady_clock::time_point deadline;
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
	int32_t error_code;

	deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (std::chrono::steady_clock::now() < deadline)
	{
		error_code = world.update_around(static_cast<double>(world_x),
			static_cast<double>(world_z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (BlockEditValidator::mesh_revision_for_block(world, world_x,
			world_z) > previous_revision)
		{
			const uint64_t elapsed_ms = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - started).count());
			std::printf("block-edit: remesh latency_ms=%llu\n",
				static_cast<unsigned long long>(elapsed_ms));
			if (latency_milliseconds != nullptr)
				*latency_milliseconds = elapsed_ms;
			return (FT_ERR_SUCCESS);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	{
		const WorldChunk *chunk = world.find_chunk(
			WorldCoordinates::floor_divide(world_x, GAME_VOXEL_CHUNK_WIDTH),
			WorldCoordinates::floor_divide(world_z, GAME_VOXEL_CHUNK_DEPTH));
		World::StreamDiagnostics diagnostics = world.stream_diagnostics();
		std::fprintf(stderr,
			"block-edit: remesh timeout chunk=%s mesh_revision=%llu "
			"previous_revision=%llu pending=%llu dirty=%s light_revision=%llu "
			"queued=%zu completed=%zu active=%zu oldest_pending=%llu\n",
			chunk == nullptr ? "missing" : "present",
			chunk == nullptr ? 0ULL
				: static_cast<unsigned long long>(chunk->mesh_revision),
			static_cast<unsigned long long>(previous_revision),
			chunk == nullptr ? 0ULL
				: static_cast<unsigned long long>(chunk->pending_mesh_request_id),
			chunk == nullptr || chunk->mesh_dirty == false ? "false" : "true",
			chunk == nullptr ? 0ULL
				: static_cast<unsigned long long>(chunk->light_revision),
			diagnostics.pending_count, diagnostics.ready_count,
			diagnostics.active_generation_count,
			static_cast<unsigned long long>(diagnostics.oldest_pending_age));
		std::fprintf(stderr,
			"block-edit: scheduler priority_pending=%s priority=(%d,%d) "
			"remesh_in_flight=%zu pipeline_queued=%zu\n",
			world.chunk_streamer.priority_remesh_pending_ ? "true" : "false",
			world.chunk_streamer.priority_remesh_chunk_x_,
			world.chunk_streamer.priority_remesh_chunk_z_,
			world.chunk_streamer.pipeline().remesh_in_flight_count(),
			world.chunk_streamer.pipeline().queued_count());
	}
	return (FT_ERR_TIMEOUT);
}

int BlockEditValidator::wait_for_region_convergence(World &world,
	int32_t world_x, int32_t world_z, uint64_t previous_revision) noexcept
{
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const int32_t offsets[5][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1},
		{0, 1}};
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (std::chrono::steady_clock::now() < deadline)
	{
		bool converged = true;
		int32_t index = 0;
		int32_t error_code;

		error_code = world.update_around(static_cast<double>(world_x),
			static_cast<double>(world_z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		while (index < 5)
		{
			const WorldChunk *chunk = world.find_chunk(chunk_x + offsets[index][0],
				chunk_z + offsets[index][1]);
			if (chunk != nullptr && chunk->initialized
				&& (chunk->mesh_dirty
					|| chunk->pending_mesh_request_id != 0U))
				converged = false;
			index += 1;
		}
		const WorldChunk *target = world.find_chunk(chunk_x, chunk_z);
		if (converged && target != nullptr
			&& target->mesh_revision > previous_revision)
			return (FT_ERR_SUCCESS);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	{
		const WorldChunk *target = world.find_chunk(chunk_x, chunk_z);
		World::StreamDiagnostics diagnostics = world.stream_diagnostics();
		std::fprintf(stderr,
			"block-edit: region convergence timeout target=%s mesh_revision=%llu "
			"previous_revision=%llu dirty=%s pending=%llu queued=%zu "
			"completed=%zu remesh_in_flight=%zu\n",
			target == nullptr ? "missing" : "present",
			target == nullptr ? 0ULL
				: static_cast<unsigned long long>(target->mesh_revision),
			static_cast<unsigned long long>(previous_revision),
			target == nullptr || !target->mesh_dirty ? "false" : "true",
			target == nullptr ? 0ULL
				: static_cast<unsigned long long>(target->pending_mesh_request_id),
			diagnostics.pending_count, diagnostics.ready_count,
			world.chunk_streamer.pipeline().remesh_in_flight_count());
		int32_t index = 0;
		while (index < 5)
		{
			const WorldChunk *chunk = world.find_chunk(
				chunk_x + offsets[index][0], chunk_z + offsets[index][1]);
			if (chunk != nullptr)
				std::fprintf(stderr,
					"block-edit: region chunk=(%d,%d) mesh=%llu dirty=%s "
					"pending=%llu light_revision=%llu\n",
					chunk->chunk_x, chunk->chunk_z,
					static_cast<unsigned long long>(chunk->mesh_revision),
					chunk->mesh_dirty ? "true" : "false",
					static_cast<unsigned long long>(chunk->pending_mesh_request_id),
					static_cast<unsigned long long>(chunk->light_revision));
			index += 1;
		}
	}
	return (FT_ERR_TIMEOUT);
}

int BlockEditValidator::wait_for_boundary_convergence(World &world,
	int32_t world_x, int32_t world_z,
	const uint64_t previous_revisions[4],
	const uint64_t previous_light_revisions[4]) noexcept
{
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const int32_t offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);

	while (std::chrono::steady_clock::now() < deadline)
	{
		bool converged = true;
		int32_t index = 0;
		int32_t error_code;

		error_code = world.update_around(static_cast<double>(world_x),
			static_cast<double>(world_z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		while (index < 4)
		{
			const WorldChunk *chunk = world.find_chunk(chunk_x
				+ offsets[index][0], chunk_z + offsets[index][1]);
			if (chunk == nullptr || !chunk->initialized
				|| chunk->mesh_dirty || chunk->pending_mesh_request_id != 0U
				|| chunk->mesh_revision <= previous_revisions[index]
				|| chunk->light_revision <= previous_light_revisions[index])
				converged = false;
			index += 1;
		}
		if (converged)
			return (FT_ERR_SUCCESS);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	std::fprintf(stderr,
		"block-edit: boundary neighbor remesh convergence timed out at "
		"world=(%d,%d)\n", world_x, world_z);
	{
		int32_t index = 0;
		while (index < 4)
		{
			const WorldChunk *chunk = world.find_chunk(chunk_x
				+ offsets[index][0], chunk_z + offsets[index][1]);
			if (chunk == nullptr)
				std::fprintf(stderr,
					"block-edit: boundary light neighbor[%d] missing\n", index);
			else
				std::fprintf(stderr,
					"block-edit: boundary light neighbor[%d] chunk=(%d,%d) "
					"mesh=%llu previous_mesh=%llu light=%llu previous_light=%llu "
					"dirty=%s pending=%llu\n", index, chunk->chunk_x,
					chunk->chunk_z,
					static_cast<unsigned long long>(chunk->mesh_revision),
					static_cast<unsigned long long>(previous_revisions[index]),
					static_cast<unsigned long long>(chunk->light_revision),
					static_cast<unsigned long long>(
						previous_light_revisions[index]),
					chunk->mesh_dirty ? "true" : "false",
					static_cast<unsigned long long>(
						chunk->pending_mesh_request_id));
			index += 1;
		}
	}
	return (FT_ERR_TIMEOUT);
}

int BlockEditValidator::wait_for_boundary_idle(World &world, int32_t world_x,
	int32_t world_z) noexcept
{
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const int32_t offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);

	while (std::chrono::steady_clock::now() < deadline)
	{
		bool idle = true;
		int32_t index = 0;
		int32_t error_code;

		error_code = world.update_around(static_cast<double>(world_x),
			static_cast<double>(world_z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		while (index < 4)
		{
			const WorldChunk *chunk = world.find_chunk(chunk_x
				+ offsets[index][0], chunk_z + offsets[index][1]);
			if (chunk == nullptr || !chunk->initialized || chunk->mesh_dirty
				|| chunk->pending_mesh_request_id != 0U)
				idle = false;
			index += 1;
		}
		if (idle)
			return (FT_ERR_SUCCESS);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return (FT_ERR_TIMEOUT);
}

int BlockEditValidator::prepare_edit_target(World &world, int32_t x, int32_t z,
	int32_t &y, size_t &mesh_before)
{
	double	surface_top;

	if (world.surface_top_at(x, z, &surface_top) == false)
	{
		std::fprintf(stderr, "block-edit: failed surface lookup\n");
		return (1);
	}
	y = static_cast<int32_t>(std::floor(surface_top + 1.0));
	mesh_before = mesh_index_count_for_block(world, x, z);
	return (0);
}

int BlockEditValidator::verify_place_block(World &world, int32_t x, int32_t y,
	int32_t z, size_t &mesh_after)
{
	uint32_t	block_id;
	int32_t		error_code;
	uint64_t previous_revision;

	previous_revision = BlockEditValidator::mesh_revision_for_block(world, x, z);
	error_code = world.place_block_at(x, y, z, VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit place", error_code));
	if (world.block_id_at(x, y, z, &block_id) == false
		|| block_id != VOXEL_GENERATOR_STONE_BLOCK)
	{
		std::fprintf(stderr,
			"block-edit: placed block not visible in storage\n");
		return (1);
	}
	error_code = BlockEditValidator::wait_for_mesh_revision(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit place remesh", error_code));
	error_code = BlockEditValidator::wait_for_region_convergence(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit place region", error_code));
	mesh_after = mesh_index_count_for_block(world, x, z);
	return (0);
}

int BlockEditValidator::verify_delete_block(World &world, int32_t x, int32_t y,
	int32_t z, size_t &mesh_after)
{
	uint32_t	block_id;
	int32_t		error_code;
	uint64_t previous_revision;

	previous_revision = BlockEditValidator::mesh_revision_for_block(world, x, z);
	error_code = world.delete_block_at(x, y, z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit delete", error_code));
	if (world.block_id_at(x, y, z, &block_id) == false
		|| block_id != GAME_VOXEL_AIR_BLOCK)
	{
		std::fprintf(stderr,
			"block-edit: deleted block not visible in storage\n");
		return (1);
	}
	error_code = BlockEditValidator::wait_for_mesh_revision(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit delete remesh", error_code));
	mesh_after = mesh_index_count_for_block(world, x, z);
	return (0);
}

int BlockEditValidator::validate_repeated_edits(World &world, int32_t x,
	int32_t y, int32_t z) noexcept
{
	static const int32_t EDIT_COUNT = 8;
	std::vector<uint64_t> latencies;
	int32_t iteration;

	latencies.reserve(static_cast<size_t>(EDIT_COUNT * 2));
	iteration = 0;
	while (iteration < EDIT_COUNT)
	{
		uint64_t previous_revision;
		uint64_t latency;
		int32_t error_code;

		previous_revision = BlockEditValidator::mesh_revision_for_block(world,
			x, z);
		error_code = world.place_block_at(x, y, z,
			VOXEL_GENERATOR_STONE_BLOCK);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit repeated place", error_code));
		latency = 0U;
		error_code = BlockEditValidator::wait_for_mesh_revision(world, x, z,
			previous_revision, &latency);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit repeated place remesh",
				error_code));
		latencies.push_back(latency);

		previous_revision = BlockEditValidator::mesh_revision_for_block(world,
			x, z);
		error_code = world.delete_block_at(x, y, z);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit repeated delete", error_code));
		latency = 0U;
		error_code = BlockEditValidator::wait_for_mesh_revision(world, x, z,
			previous_revision, &latency);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit repeated delete remesh",
				error_code));
		latencies.push_back(latency);
		iteration += 1;
	}
	if (latencies.empty())
		return (FT_ERR_INTERNAL);
	std::sort(latencies.begin(), latencies.end());
	const size_t p50_index = (latencies.size() - 1U) * 50U / 100U;
	const size_t p95_index = (latencies.size() - 1U) * 95U / 100U;
	const size_t p99_index = (latencies.size() - 1U) * 99U / 100U;
	std::printf("block-edit: repeated samples=%zu p50_ms=%llu p95_ms=%llu "
		"p99_ms=%llu max_ms=%llu\n", latencies.size(),
		static_cast<unsigned long long>(latencies[p50_index]),
		static_cast<unsigned long long>(latencies[p95_index]),
		static_cast<unsigned long long>(latencies[p99_index]),
		static_cast<unsigned long long>(latencies.back()));
	if (latencies.back() > 1000U)
	{
		std::fprintf(stderr,
			"block-edit: repeated remesh exceeded one-second bound\n");
		return (FT_ERR_TIMEOUT);
	}
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::validate_boundary_edit(World &world) noexcept
{
	const int32_t x = GAME_VOXEL_CHUNK_WIDTH - 1;
	const int32_t z = -6;
	const int32_t chunk_x = WorldCoordinates::floor_divide(x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(z,
		GAME_VOXEL_CHUNK_DEPTH);
	const int32_t offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
	uint64_t previous_revisions[4];
	uint64_t previous_light_revisions[4];
	uint8_t baseline_light;
	uint8_t placed_light;
	uint8_t deleted_light;
	WorldChunk *target_chunk;
	int32_t y;
	double surface_top;
	int32_t index;
	int32_t error_code;

	if (world.surface_top_at(x, z, &surface_top) == false)
		return (ApplicationError::fail("block-edit boundary surface", 1));
	y = static_cast<int32_t>(std::floor(surface_top + 1.0));
	error_code = BlockEditValidator::wait_for_boundary_idle(world, x, z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary baseline", error_code));
	target_chunk = world.find_chunk_mutable(chunk_x, chunk_z);
	if (target_chunk == nullptr || !target_chunk->initialized)
		return (ApplicationError::fail("block-edit boundary target", 1));
	baseline_light = target_chunk->light.get(
		x - chunk_x * GAME_VOXEL_CHUNK_WIDTH, y,
		z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	index = 0;
	while (index < 4)
	{
		const WorldChunk *chunk = world.find_chunk(chunk_x + offsets[index][0],
			chunk_z + offsets[index][1]);
		if (chunk == nullptr || !chunk->initialized)
			return (ApplicationError::fail("block-edit boundary neighbor", 1));
		previous_revisions[index] = chunk->mesh_revision;
		previous_light_revisions[index] = chunk->light_revision;
		index += 1;
	}
	error_code = world.place_block_at(x, y, z, VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary place", error_code));
	error_code = BlockEditValidator::wait_for_boundary_convergence(world, x, z,
		previous_revisions, previous_light_revisions);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary remesh", error_code));
	placed_light = target_chunk->light.get(
		x - chunk_x * GAME_VOXEL_CHUNK_WIDTH, y,
		z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	if (placed_light == baseline_light)
	{
		std::fprintf(stderr,
			"block-edit: boundary light did not change before=%u after=%u\n",
			static_cast<unsigned int>(baseline_light),
			static_cast<unsigned int>(placed_light));
		return (ApplicationError::fail("block-edit boundary light place", 1));
	}
	index = 0;
	while (index < 4)
	{
		const WorldChunk *chunk = world.find_chunk(chunk_x + offsets[index][0],
			chunk_z + offsets[index][1]);
		if (chunk == nullptr || !chunk->initialized)
			return (ApplicationError::fail("block-edit boundary post-place", 1));
		previous_revisions[index] = chunk->mesh_revision;
		previous_light_revisions[index] = chunk->light_revision;
		index += 1;
	}
	error_code = world.delete_block_at(x, y, z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary delete", error_code));
	error_code = BlockEditValidator::wait_for_boundary_convergence(world, x, z,
		previous_revisions, previous_light_revisions);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	deleted_light = target_chunk->light.get(
		x - chunk_x * GAME_VOXEL_CHUNK_WIDTH, y,
		z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	if (deleted_light != baseline_light)
	{
		std::fprintf(stderr,
			"block-edit: boundary light did not restore baseline=%u "
			"after_delete=%u\n", static_cast<unsigned int>(baseline_light),
			static_cast<unsigned int>(deleted_light));
		return (ApplicationError::fail("block-edit boundary light delete", 1));
	}
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::report_mesh_result(size_t before, size_t after_place,
	size_t after_delete)
{
	if (after_place == before || after_delete == after_place)
	{
		std::fprintf(stderr,
						"block-edit: mesh counts did not update"
						" before=%zu place=%zu delete=%zu\n",
						before,
						after_place,
						after_delete);
		return (1);
	}
	std::printf("block-edit: ok before=%zu place=%zu delete=%zu\n", before,
		after_place, after_delete);
	return (0);
}

int BlockEditValidator::validate() const
{
	World world;
	int32_t edit_x;
	int32_t edit_y;
	int32_t edit_z;
	size_t mesh_before;
	size_t mesh_after_place;
	size_t mesh_after_delete;
	int32_t error_code;

	edit_x = 2;
	edit_z = -6;
	error_code = world.initialize("integration-seed");
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit world initialization",
				error_code));
	if (prepare_edit_target(world, edit_x, edit_z, edit_y, mesh_before) != 0
		|| verify_place_block(world, edit_x, edit_y, edit_z,
			mesh_after_place) != 0 || verify_delete_block(world, edit_x, edit_y,
			edit_z, mesh_after_delete) != 0 || report_mesh_result(mesh_before,
			mesh_after_place, mesh_after_delete) != 0)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_repeated_edits(world, edit_x, edit_y,
		edit_z) != FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_boundary_edit(world) != FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	world.destroy();
	return (0);
}
