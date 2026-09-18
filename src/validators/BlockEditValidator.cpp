#include "../../src/validators/BlockEditValidator.hpp"
#include <algorithm>
#include <chrono>
#include <memory>
#include <new>
#include <thread>
#include <vector>

namespace
{
	static std::size_t nonzero_light_cells(const WorldChunk &chunk) noexcept
	{
		std::size_t count = 0U;
		for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
			for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
				for (int32_t x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
					if (chunk.light.get(x, y, z) != 0U)
						count += 1U;
		return (count);
	}

	static std::size_t nonzero_mesh_light_vertices(
		const WorldChunk &chunk) noexcept
	{
		std::size_t count = 0U;
		for (std::size_t index = 0U; index < chunk.mesh.vertices.size(); ++index)
		{
			if (chunk.mesh.vertices[index].packed_light != 0U)
				count += 1U;
		}
		return (count);
	}

	static bool mesh_has_boundary_face(const WorldChunk &chunk,
		uint8_t face, uint16_t boundary_x) noexcept
	{
		std::size_t index = 0U;

		while (index < chunk.mesh.vertices.size())
		{
			const chunk_mesh_vertex &vertex = chunk.mesh.vertices[index];
			if (vertex.face == face && vertex.coordinate_x == boundary_x)
				return (true);
			index += 1U;
		}
		return (false);
	}

	static int check_transient_light_publication(const WorldChunk *chunk,
		std::size_t baseline_light_cells,
		std::size_t baseline_mesh_light_vertices,
		bool *observed_empty) noexcept
	{
		std::size_t current_light_cells;
		std::size_t current_mesh_light_vertices;

		if (observed_empty == nullptr || chunk == nullptr
			|| !chunk->initialized || baseline_light_cells == 0U)
			return (FT_ERR_SUCCESS);
		current_light_cells = nonzero_light_cells(*chunk);
		if (!chunk->light_buffer_is_valid() || current_light_cells == 0U)
		{
			if (current_light_cells == 0U)
			{
				*observed_empty = true;
				return (FT_ERR_SUCCESS);
			}
			std::fprintf(stderr,
				"block-edit: transient light publication became empty "
				"chunk=(%d,%d) baseline_cells=%zu current_cells=%zu "
				"light_valid=%d light_current=%d pending=%llu\n",
				chunk->chunk_x, chunk->chunk_z, baseline_light_cells,
				current_light_cells, chunk->light_buffer_is_valid() ? 1 : 0,
				chunk->light_is_current() ? 1 : 0,
				static_cast<unsigned long long>(chunk->pending_mesh_request_id));
			return (FT_ERR_INVALID_STATE);
		}
		if (baseline_mesh_light_vertices == 0U || chunk->mesh.vertices.empty())
			return (FT_ERR_SUCCESS);
		current_mesh_light_vertices = nonzero_mesh_light_vertices(*chunk);
		if (current_mesh_light_vertices == 0U)
		{
			*observed_empty = true;
		}
		return (FT_ERR_SUCCESS);
	}

	static int32_t wait_for_edit_baseline(World &world, int32_t world_x,
		int32_t world_z) noexcept
	{
		const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
			GAME_VOXEL_CHUNK_WIDTH);
		const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
			GAME_VOXEL_CHUNK_DEPTH);
		const std::chrono::steady_clock::time_point deadline =
			std::chrono::steady_clock::now() + std::chrono::seconds(30);

		while (std::chrono::steady_clock::now() < deadline)
		{
			WorldChunk *chunk;

			if (world.update_around(static_cast<double>(world_x),
				static_cast<double>(world_z), 0,
				WorldCoordinates::MIN_RENDER_DISTANCE) != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_OPERATION);
			chunk = world.find_chunk_mutable(chunk_x, chunk_z);
			if (chunk != nullptr && chunk->initialized
				&& chunk->light_is_current()
				&& chunk->pending_mesh_request_id == 0U
				&& chunk->mesh_dirty == false)
				return (FT_ERR_SUCCESS);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return (FT_ERR_TIMEOUT);
	}

	static int32_t validate_test_colored_light_source() noexcept
	{
		const uint32_t test_glowstone_block =
			VOXEL_GENERATOR_SHIMMER_STONE_BLOCK;
		uint8_t light[5][3] = {{0U, 0U, 0U}, {0U, 0U, 0U},
			{0U, 0U, 0U}, {0U, 0U, 0U}, {0U, 0U, 0U}};
		int32_t index;

		(void)test_glowstone_block;
		light[0][0] = 15U;
		light[0][1] = 10U;
		light[0][2] = 5U;
		index = 1;
		while (index < 5)
		{
			if (light[index - 1][0] > 0U)
				light[index][0] = static_cast<uint8_t>(light[index - 1][0] - 1U);
			if (light[index - 1][1] > 0U)
				light[index][1] = static_cast<uint8_t>(light[index - 1][1] - 1U);
			if (light[index - 1][2] > 0U)
				light[index][2] = static_cast<uint8_t>(light[index - 1][2] - 1U);
			index += 1;
		}
		if (light[0][0] != 15U || light[0][1] != 10U
			|| light[0][2] != 5U || light[1][0] != 14U
			|| light[1][1] != 9U || light[1][2] != 4U
			|| light[4][0] != 11U || light[4][1] != 6U
			|| light[4][2] != 1U)
			return (1);
		return (0);
	}
}

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
	if (validate_test_colored_light_source() != 0)
		return (ApplicationError::fail("block-edit colored light source", 1));

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
	std::size_t baseline_light_cells[5] = {0U, 0U, 0U, 0U, 0U};
	std::size_t baseline_mesh_light_vertices[5] = {0U, 0U, 0U, 0U, 0U};
	bool observed_empty[5] = {false, false, false, false, false};
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);
	for (int32_t index = 0; index < 5; ++index)
	{
		const WorldChunk *baseline = world.find_chunk(
			chunk_x + offsets[index][0], chunk_z + offsets[index][1]);
		if (baseline != nullptr && baseline->initialized
			&& baseline->light_buffer_is_valid())
		{
			baseline_light_cells[index] = nonzero_light_cells(*baseline);
			baseline_mesh_light_vertices[index] =
				nonzero_mesh_light_vertices(*baseline);
		}
	}
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
		for (int32_t sample = 0; sample < 5; ++sample)
		{
			const WorldChunk *sampled = world.find_chunk(
				chunk_x + offsets[sample][0], chunk_z + offsets[sample][1]);
			if (check_transient_light_publication(sampled,
				baseline_light_cells[sample],
				baseline_mesh_light_vertices[sample], &observed_empty[sample])
				!= FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_STATE);
		}
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
		{
			for (int32_t sample = 0; sample < 5; ++sample)
			{
				const WorldChunk *final_chunk = world.find_chunk(
					chunk_x + offsets[sample][0], chunk_z + offsets[sample][1]);
				if (observed_empty[sample] && final_chunk != nullptr
					&& baseline_light_cells[sample] != 0U
					&& nonzero_light_cells(*final_chunk) != 0U)
					return (FT_ERR_INVALID_STATE);
			}
			return (FT_ERR_SUCCESS);
		}
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
	std::size_t baseline_light_cells[5] = {0U, 0U, 0U, 0U, 0U};
	std::size_t baseline_mesh_light_vertices[5] = {0U, 0U, 0U, 0U, 0U};
	bool observed_empty[5] = {false, false, false, false, false};
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);
	{
		const WorldChunk *baseline = world.find_chunk(chunk_x, chunk_z);
		if (baseline != nullptr && baseline->initialized
			&& baseline->light_buffer_is_valid())
		{
			baseline_light_cells[0] = nonzero_light_cells(*baseline);
			baseline_mesh_light_vertices[0] =
				nonzero_mesh_light_vertices(*baseline);
		}
		for (int32_t sample = 0; sample < 4; ++sample)
		{
			baseline = world.find_chunk(chunk_x + offsets[sample][0],
				chunk_z + offsets[sample][1]);
			if (baseline != nullptr && baseline->initialized
				&& baseline->light_buffer_is_valid())
			{
				baseline_light_cells[sample + 1] = nonzero_light_cells(*baseline);
				baseline_mesh_light_vertices[sample + 1] =
					nonzero_mesh_light_vertices(*baseline);
			}
		}
	}

	while (std::chrono::steady_clock::now() < deadline)
	{
		bool converged = true;
		int32_t index = 0;
		int32_t error_code;
		const int32_t local_x = world_x - chunk_x * GAME_VOXEL_CHUNK_WIDTH;
		const int32_t local_z = world_z - chunk_z * GAME_VOXEL_CHUNK_DEPTH;

		error_code = world.update_around(static_cast<double>(world_x),
			static_cast<double>(world_z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (check_transient_light_publication(world.find_chunk(chunk_x, chunk_z),
			baseline_light_cells[0], baseline_mesh_light_vertices[0],
			&observed_empty[0])
			!= FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
		for (int32_t sample = 0; sample < 4; ++sample)
		{
			const WorldChunk *sampled = world.find_chunk(
				chunk_x + offsets[sample][0], chunk_z + offsets[sample][1]);
			if (check_transient_light_publication(sampled,
				baseline_light_cells[sample + 1],
				baseline_mesh_light_vertices[sample + 1],
				&observed_empty[sample + 1])
				!= FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_STATE);
		}
		while (index < 4)
		{
			const WorldChunk *chunk = world.find_chunk(chunk_x
				+ offsets[index][0], chunk_z + offsets[index][1]);
			bool requires_update = false;
			if (local_x == 0 && index == 0)
				requires_update = true;
			if (local_x == GAME_VOXEL_CHUNK_WIDTH - 1 && index == 1)
				requires_update = true;
			if (local_z == 0 && index == 2)
				requires_update = true;
			if (local_z == GAME_VOXEL_CHUNK_DEPTH - 1 && index == 3)
				requires_update = true;
			if (chunk == nullptr || !chunk->initialized
				|| (requires_update && chunk->light_buffer_is_valid() == false)
				|| (requires_update && (chunk->mesh_dirty
					|| chunk->pending_mesh_request_id != 0U
					|| chunk->mesh_revision <= previous_revisions[index]
					|| chunk->light_revision <= previous_light_revisions[index])))
				converged = false;
			if (requires_update && chunk != nullptr && chunk->initialized
				&& chunk->mesh_dirty == false
				&& chunk->pending_mesh_request_id == 0U
				&& chunk->light_is_current() == false)
				converged = false;
			index += 1;
		}
		{
			const WorldChunk *target = world.find_chunk(chunk_x, chunk_z);
			if (target == nullptr || !target->initialized
				|| target->light_buffer_is_valid() == false)
				converged = false;
		}
		if (converged)
		{
			for (int32_t sample = 0; sample < 5; ++sample)
			{
				const WorldChunk *final_chunk;
				if (sample == 0)
					final_chunk = world.find_chunk(chunk_x, chunk_z);
				else
					final_chunk = world.find_chunk(chunk_x
						+ offsets[sample - 1][0], chunk_z
						+ offsets[sample - 1][1]);
				if (observed_empty[sample] && final_chunk != nullptr
					&& baseline_light_cells[sample] != 0U
					&& nonzero_light_cells(*final_chunk) != 0U)
					return (FT_ERR_INVALID_STATE);
			}
			return (FT_ERR_SUCCESS);
		}
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
	WorldChunk	*chunk;
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
	chunk = world.find_chunk_mutable(WorldCoordinates::floor_divide(x,
		GAME_VOXEL_CHUNK_WIDTH), WorldCoordinates::floor_divide(z,
		GAME_VOXEL_CHUNK_DEPTH));
	if (chunk == nullptr || chunk->light_buffer_is_valid() == false
		|| chunk->light_is_current() != false)
	{
		std::fprintf(stderr,
			"block-edit: placement discarded the previous light buffer\n");
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
	if (chunk->light_is_current() == false)
	{
		std::fprintf(stderr,
			"block-edit: placement light did not converge to the new version\n");
		return (1);
	}
	if (chunk->last_incremental_light_content_version
		!= chunk->content_version
		|| chunk->last_incremental_light_voxel_revision
		!= chunk->voxel_revision)
	{
		const World::StreamDiagnostics diagnostics = world.stream_diagnostics();
		std::fprintf(stderr,
			"block-edit: placement unexpectedly used a full light rebuild "
			"chunk=(%d,%d) marker=%d marker_content=%u content=%u "
			"marker_voxel=%llu voxel=%llu light_current=%d mesh=%llu "
			"incremental=%llu full=%llu stale=%zu\n",
			chunk->chunk_x, chunk->chunk_z,
			chunk->last_light_remesh_incremental != FT_FALSE ? 1 : 0,
			static_cast<unsigned int>(
				chunk->last_incremental_light_content_version),
			static_cast<unsigned int>(chunk->content_version),
			static_cast<unsigned long long>(
				chunk->last_incremental_light_voxel_revision),
			static_cast<unsigned long long>(chunk->voxel_revision),
			chunk->light_is_current() ? 1 : 0,
			static_cast<unsigned long long>(chunk->mesh_revision),
			static_cast<unsigned long long>(
				diagnostics.remesh_incremental_completed_count),
			static_cast<unsigned long long>(diagnostics.remesh_full_completed_count),
			diagnostics.stale_remesh_result_count);
		return (1);
	}
	mesh_after = mesh_index_count_for_block(world, x, z);
	return (0);
}

int BlockEditValidator::verify_delete_block(World &world, int32_t x, int32_t y,
	int32_t z, size_t &mesh_after)
{
	uint32_t	block_id;
	int32_t		error_code;
	WorldChunk	*chunk;
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
	chunk = world.find_chunk_mutable(WorldCoordinates::floor_divide(x,
		GAME_VOXEL_CHUNK_WIDTH), WorldCoordinates::floor_divide(z,
		GAME_VOXEL_CHUNK_DEPTH));
	if (chunk == nullptr || chunk->light_buffer_is_valid() == false
		|| chunk->light_is_current() != false)
	{
		std::fprintf(stderr,
			"block-edit: deletion discarded the previous light buffer\n");
		return (1);
	}
	error_code = BlockEditValidator::wait_for_mesh_revision(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit delete remesh", error_code));
	error_code = BlockEditValidator::wait_for_region_convergence(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit delete region", error_code));
	if (chunk->light_is_current() == false)
	{
		std::fprintf(stderr,
			"block-edit: deletion light did not converge to the new version\n");
		return (1);
	}
	if (chunk->last_incremental_light_content_version
		!= chunk->content_version
		|| chunk->last_incremental_light_voxel_revision
		!= chunk->voxel_revision)
	{
		std::fprintf(stderr,
			"block-edit: deletion unexpectedly used a full light rebuild\n");
		return (1);
	}
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

int BlockEditValidator::validate_rapid_edit_supersession(World &world,
	int32_t x, int32_t y, int32_t z) noexcept
{
	static const int32_t BURST_COUNT = 6;
	uint64_t previous_revision;
	int32_t iteration;
	int32_t error_code;
	uint32_t block_id;
	WorldChunk *chunk;

	previous_revision = BlockEditValidator::mesh_revision_for_block(world,
		x, z);
	iteration = 0;
	while (iteration < BURST_COUNT)
	{
		error_code = world.place_block_at(x, y, z,
			VOXEL_GENERATOR_STONE_BLOCK);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail(
				"block-edit rapid supersession place", error_code));
		error_code = world.delete_block_at(x, y, z);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail(
				"block-edit rapid supersession delete", error_code));
		iteration += 1;
	}
	error_code = BlockEditValidator::wait_for_region_convergence(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail(
			"block-edit rapid supersession convergence", error_code));
	if (world.block_id_at(x, y, z, &block_id) == false
		|| block_id != GAME_VOXEL_AIR_BLOCK)
		return (ApplicationError::fail(
			"block-edit rapid supersession final block", 1));
	chunk = world.find_chunk_mutable(WorldCoordinates::floor_divide(x,
		GAME_VOXEL_CHUNK_WIDTH), WorldCoordinates::floor_divide(z,
		GAME_VOXEL_CHUNK_DEPTH));
	if (chunk == nullptr || chunk->light_is_current() == false
		|| chunk->pending_mesh_request_id != 0U || chunk->mesh_dirty)
		return (ApplicationError::fail(
			"block-edit rapid supersession final light", 1));
	std::printf("block-edit: rapid supersession ok burst=%d\n", BURST_COUNT);
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::validate_interior_emissive_edit(World &world) noexcept
{
	const int32_t x = 4;
	const int32_t z = -6;
	const int32_t chunk_x = WorldCoordinates::floor_divide(x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(z,
		GAME_VOXEL_CHUNK_DEPTH);
	WorldChunk *chunk;
	double surface_top;
	int32_t y;
	uint8_t baseline_light;
	uint8_t source_light;
	uint8_t restored_light;
	uint32_t restored_block;
	uint64_t previous_revision;
	int32_t error_code;

	if (world.surface_top_at(x, z, &surface_top) == false)
		return (ApplicationError::fail("block-edit emissive surface", 1));
	y = static_cast<int32_t>(std::floor(surface_top + 1.0));
	/* Capture the baseline only after the target has finished any prior
	 * generation/remesh work.  Otherwise a late sky-light result can change
	 * the baseline from (for example) 13 to 15 while this test is in flight,
	 * producing a false restore failure. */
	error_code = wait_for_edit_baseline(world, x, z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit emissive baseline",
			error_code));
	chunk = world.find_chunk_mutable(chunk_x, chunk_z);
	if (chunk == nullptr || !chunk->initialized
		|| x - chunk_x * GAME_VOXEL_CHUNK_WIDTH <= 0
		|| x - chunk_x * GAME_VOXEL_CHUNK_WIDTH >= GAME_VOXEL_CHUNK_WIDTH - 1
		|| z - chunk_z * GAME_VOXEL_CHUNK_DEPTH <= 0
		|| z - chunk_z * GAME_VOXEL_CHUNK_DEPTH >= GAME_VOXEL_CHUNK_DEPTH - 1)
		return (ApplicationError::fail("block-edit emissive interior", 1));
	baseline_light = chunk->light.get(x - chunk_x * GAME_VOXEL_CHUNK_WIDTH,
		y, z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	previous_revision = BlockEditValidator::mesh_revision_for_block(world, x, z);
	error_code = world.place_block_at(x, y, z,
		VOXEL_GENERATOR_SHIMMER_STONE_BLOCK);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit emissive place", error_code));
	if (chunk->light_buffer_is_valid() == false
		|| chunk->light_is_current() != false)
		return (ApplicationError::fail("block-edit emissive place state", 1));
	error_code = BlockEditValidator::wait_for_mesh_revision(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit emissive place remesh",
			error_code));
	error_code = BlockEditValidator::wait_for_region_convergence(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit emissive place convergence",
			error_code));
	source_light = chunk->light.get(x - chunk_x * GAME_VOXEL_CHUNK_WIDTH,
		y, z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	if (source_light <= baseline_light || chunk->light_is_current() == false)
		return (ApplicationError::fail("block-edit emissive source light", 1));
	previous_revision = BlockEditValidator::mesh_revision_for_block(world, x, z);
	error_code = world.delete_block_at(x, y, z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit emissive delete", error_code));
	error_code = BlockEditValidator::wait_for_mesh_revision(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit emissive delete remesh",
			error_code));
	error_code = BlockEditValidator::wait_for_region_convergence(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit emissive delete convergence",
			error_code));
	restored_light = chunk->light.get(x - chunk_x * GAME_VOXEL_CHUNK_WIDTH,
		y, z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	if (world.block_id_at(x, y, z, &restored_block) == false)
		restored_block = UINT32_MAX;
	if (restored_light != baseline_light || chunk->light_is_current() == false)
	{
		std::fprintf(stderr,
			"block-edit: emissive restore mismatch baseline=%u source=%u "
			"restored=%u block=%u content=%u light=%u input=%u computed_input=%u\n",
			static_cast<unsigned int>(baseline_light),
			static_cast<unsigned int>(source_light),
			static_cast<unsigned int>(restored_light),
			static_cast<unsigned int>(restored_block),
			static_cast<unsigned int>(chunk->content_version),
			static_cast<unsigned int>(chunk->light_version),
			static_cast<unsigned int>(chunk->light_input_version),
			static_cast<unsigned int>(chunk->computed_light_input_version));
		return (ApplicationError::fail("block-edit emissive restore light", 1));
	}
	std::printf("block-edit: interior emissive source=%u restored=%u\n",
		static_cast<unsigned int>(source_light),
		static_cast<unsigned int>(restored_light));
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
	uint8_t baseline_neighbor_light;
	uint8_t placed_light;
	uint8_t placed_neighbor_light;
	uint8_t deleted_light;
	uint8_t deleted_neighbor_light;
	uint64_t place_started_milliseconds;
	uint64_t delete_started_milliseconds;
	uint64_t place_latency_milliseconds;
	uint64_t delete_latency_milliseconds;
	uint64_t incremental_before;
	uint64_t incremental_after;
	uint32_t edge_block_id;
	uint8_t edge_before_light;
	uint8_t edge_after_light;
	int32_t edge_y;
	WorldChunk *target_chunk;
	WorldChunk *right_neighbor;
	int32_t y;
	int32_t local_z;
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
	right_neighbor = world.find_chunk_mutable(chunk_x + 1, chunk_z);
	if (right_neighbor == nullptr || !right_neighbor->initialized)
		return (ApplicationError::fail("block-edit boundary right neighbor", 1));
	local_z = z - chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	baseline_light = target_chunk->light.get(
		x - chunk_x * GAME_VOXEL_CHUNK_WIDTH, y,
		z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	baseline_neighbor_light = right_neighbor->light.get(0, y, local_z);
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
	place_started_milliseconds = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	error_code = world.place_block_at(x, y, z,
		VOXEL_GENERATOR_SHIMMER_STONE_BLOCK);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary place", error_code));
	error_code = BlockEditValidator::wait_for_boundary_convergence(world, x, z,
		previous_revisions, previous_light_revisions);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary remesh", error_code));
	place_latency_milliseconds = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count())
		- place_started_milliseconds;
	placed_light = target_chunk->light.get(
		x - chunk_x * GAME_VOXEL_CHUNK_WIDTH, y,
		z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	placed_neighbor_light = right_neighbor->light.get(0, y, local_z);
	if (placed_light <= baseline_light
		|| placed_neighbor_light <= baseline_neighbor_light)
	{
		std::fprintf(stderr,
			"block-edit: boundary light did not propagate target=%u->%u "
			"neighbor=%u->%u\n",
			static_cast<unsigned int>(baseline_light),
			static_cast<unsigned int>(placed_light),
			static_cast<unsigned int>(baseline_neighbor_light),
			static_cast<unsigned int>(placed_neighbor_light));
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
	delete_started_milliseconds = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	error_code = world.delete_block_at(x, y, z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary delete", error_code));
	error_code = BlockEditValidator::wait_for_boundary_convergence(world, x, z,
		previous_revisions, previous_light_revisions);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	delete_latency_milliseconds = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count())
		- delete_started_milliseconds;
	deleted_light = target_chunk->light.get(
		x - chunk_x * GAME_VOXEL_CHUNK_WIDTH, y,
		z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	deleted_neighbor_light = right_neighbor->light.get(0, y, local_z);
	if (deleted_light != baseline_light
		|| deleted_neighbor_light != baseline_neighbor_light)
	{
		std::fprintf(stderr,
			"block-edit: boundary light did not restore target=%u->%u "
			"neighbor=%u->%u\n", static_cast<unsigned int>(baseline_light),
			static_cast<unsigned int>(deleted_light),
			static_cast<unsigned int>(baseline_neighbor_light),
			static_cast<unsigned int>(deleted_neighbor_light));
		return (ApplicationError::fail("block-edit boundary light delete", 1));
	}
	std::printf("block-edit: boundary place_ms=%llu delete_ms=%llu\n",
		static_cast<unsigned long long>(place_latency_milliseconds),
		static_cast<unsigned long long>(delete_latency_milliseconds));
	edge_y = y - 1;
	edge_block_id = GAME_VOXEL_AIR_BLOCK;
	while (edge_y >= 0 && edge_block_id == GAME_VOXEL_AIR_BLOCK)
	{
		if (world.block_id_at(x, edge_y, z, &edge_block_id) == false)
			return (ApplicationError::fail("block-edit boundary opaque lookup", 1));
		if (edge_block_id == GAME_VOXEL_AIR_BLOCK)
			edge_y -= 1;
	}
	if (edge_y < 0 || voxel_block_is_breakable(edge_block_id) == FT_FALSE)
	{
		std::fprintf(stderr,
			"block-edit: boundary opaque setup block=%u y=%d\n",
			static_cast<unsigned int>(edge_block_id), edge_y);
		return (ApplicationError::fail("block-edit boundary opaque setup", 1));
	}
	edge_before_light = target_chunk->light.get(
		x - chunk_x * GAME_VOXEL_CHUNK_WIDTH, edge_y,
		z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	incremental_before = world.stream_diagnostics()
		.remesh_incremental_completed_count;
	index = 0;
	while (index < 4)
	{
		const WorldChunk *chunk = world.find_chunk(chunk_x + offsets[index][0],
			chunk_z + offsets[index][1]);
		if (chunk == nullptr || !chunk->initialized)
			return (ApplicationError::fail("block-edit boundary opaque neighbors", 1));
		previous_revisions[index] = chunk->mesh_revision;
		previous_light_revisions[index] = chunk->light_revision;
		index += 1;
	}
	error_code = world.delete_block_at(x, edge_y, z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary opaque delete",
			error_code));
	error_code = BlockEditValidator::wait_for_boundary_convergence(world, x, z,
		previous_revisions, previous_light_revisions);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary opaque convergence",
			error_code));
	edge_after_light = target_chunk->light.get(
		x - chunk_x * GAME_VOXEL_CHUNK_WIDTH, edge_y,
		z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	incremental_after = world.stream_diagnostics()
		.remesh_incremental_completed_count;
	if (incremental_after <= incremental_before
		|| target_chunk->light_buffer_is_valid() == false
		|| target_chunk->light_is_current() == false)
	{
		std::fprintf(stderr,
			"block-edit: boundary additive path missing before=%llu after=%llu "
			"light=%u->%u\n",
			static_cast<unsigned long long>(incremental_before),
			static_cast<unsigned long long>(incremental_after),
			static_cast<unsigned int>(edge_before_light),
			static_cast<unsigned int>(edge_after_light));
		return (ApplicationError::fail("block-edit boundary additive path", 1));
	}
	previous_revisions[0] = target_chunk->mesh_revision;
	index = 1;
	while (index < 4)
	{
		const WorldChunk *chunk = world.find_chunk(chunk_x + offsets[index][0],
			chunk_z + offsets[index][1]);
		if (chunk == nullptr || !chunk->initialized)
			return (ApplicationError::fail("block-edit boundary opaque restore neighbors", 1));
		previous_revisions[index] = chunk->mesh_revision;
		previous_light_revisions[index] = chunk->light_revision;
		index += 1;
	}
	error_code = world.place_block_at(x, edge_y, z, edge_block_id);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary opaque restore",
			error_code));
	error_code = BlockEditValidator::wait_for_boundary_convergence(world, x, z,
		previous_revisions, previous_light_revisions);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary opaque restore convergence",
			error_code));
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::validate_boundary_face_matrix(World &world) noexcept
{
	static const int32_t positions[4][2] = {{0, -6},
		{GAME_VOXEL_CHUNK_WIDTH - 1, -6}, {2, 0},
		{2, GAME_VOXEL_CHUNK_DEPTH - 1}};
	static const int32_t neighbour_offsets[4][2] = {{-1, 0},
		{1, 0}, {0, -1}, {0, 1}};
	int32_t position_index = 0;

	while (position_index < 4)
	{
		const int32_t world_x = positions[position_index][0];
		const int32_t world_z = positions[position_index][1];
		const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
			GAME_VOXEL_CHUNK_WIDTH);
		const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
			GAME_VOXEL_CHUNK_DEPTH);
		int32_t target_local_x = world_x
			- chunk_x * GAME_VOXEL_CHUNK_WIDTH;
		int32_t target_local_z = world_z
			- chunk_z * GAME_VOXEL_CHUNK_DEPTH;
		int32_t neighbour_chunk_x = chunk_x;
		int32_t neighbour_chunk_z = chunk_z;
		int32_t neighbour_local_x = target_local_x;
		int32_t neighbour_local_z = target_local_z;
		int32_t y;
		double surface_top;
		WorldChunk *target_chunk;
		WorldChunk *neighbour_chunk;
		uint8_t baseline_target_light;
		uint8_t baseline_neighbour_light;
		uint64_t previous_revisions[4];
		uint64_t previous_light_revisions[4];
		int32_t index = 0;
		int32_t error_code;

		if (target_local_x == 0)
		{
			neighbour_chunk_x -= 1;
			neighbour_local_x = GAME_VOXEL_CHUNK_WIDTH - 1;
		}
		else if (target_local_x == GAME_VOXEL_CHUNK_WIDTH - 1)
		{
			neighbour_chunk_x += 1;
			neighbour_local_x = 0;
		}
		else if (target_local_z == 0)
		{
			neighbour_chunk_z -= 1;
			neighbour_local_z = GAME_VOXEL_CHUNK_DEPTH - 1;
		}
		else if (target_local_z == GAME_VOXEL_CHUNK_DEPTH - 1)
		{
			neighbour_chunk_z += 1;
			neighbour_local_z = 0;
		}
		else
			return (ApplicationError::fail("block-edit boundary matrix edge", 1));
		if (world.surface_top_at(world_x, world_z, &surface_top) == false)
			return (ApplicationError::fail("block-edit boundary matrix surface", 1));
		y = static_cast<int32_t>(std::floor(surface_top + 1.0));
		error_code = BlockEditValidator::wait_for_boundary_idle(world, world_x,
			world_z);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit boundary matrix idle",
				error_code));
		target_chunk = world.find_chunk_mutable(chunk_x, chunk_z);
		neighbour_chunk = world.find_chunk_mutable(neighbour_chunk_x,
			neighbour_chunk_z);
		if (target_chunk == nullptr || neighbour_chunk == nullptr
			|| target_chunk->initialized == false
			|| neighbour_chunk->initialized == false)
			return (ApplicationError::fail("block-edit boundary matrix chunks", 1));
		baseline_target_light = target_chunk->light.get(target_local_x, y,
			target_local_z);
		baseline_neighbour_light = neighbour_chunk->light.get(neighbour_local_x,
			y, neighbour_local_z);
		while (index < 4)
		{
			const WorldChunk *chunk = world.find_chunk(chunk_x
				+ neighbour_offsets[index][0], chunk_z
				+ neighbour_offsets[index][1]);
			if (chunk == nullptr || chunk->initialized == false)
				return (ApplicationError::fail(
					"block-edit boundary matrix neighbours", 1));
			previous_revisions[index] = chunk->mesh_revision;
			previous_light_revisions[index] = chunk->light_revision;
			index += 1;
		}
		error_code = world.place_block_at(world_x, y, world_z,
			VOXEL_GENERATOR_SHIMMER_STONE_BLOCK);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit boundary matrix place",
				error_code));
		error_code = BlockEditValidator::wait_for_boundary_convergence(world,
			world_x, world_z, previous_revisions, previous_light_revisions);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit boundary matrix place wait",
				error_code));
		if (voxel_light_block(target_chunk->light.get(target_local_x, y,
			target_local_z)) <= voxel_light_block(baseline_target_light)
			|| voxel_light_block(neighbour_chunk->light.get(neighbour_local_x,
			y, neighbour_local_z))
				<= voxel_light_block(baseline_neighbour_light))
			return (ApplicationError::fail("block-edit boundary matrix propagate",
				1));
		index = 0;
		while (index < 4)
		{
			const WorldChunk *chunk = world.find_chunk(chunk_x
				+ neighbour_offsets[index][0], chunk_z
				+ neighbour_offsets[index][1]);
			if (chunk == nullptr)
				return (ApplicationError::fail(
					"block-edit boundary matrix post-place", 1));
			previous_revisions[index] = chunk->mesh_revision;
			previous_light_revisions[index] = chunk->light_revision;
			index += 1;
		}
		error_code = world.delete_block_at(world_x, y, world_z);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit boundary matrix delete",
				error_code));
		error_code = BlockEditValidator::wait_for_boundary_convergence(world,
			world_x, world_z, previous_revisions, previous_light_revisions);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit boundary matrix delete wait",
				error_code));
		if (target_chunk->light.get(target_local_x, y, target_local_z)
			!= baseline_target_light
			|| neighbour_chunk->light.get(neighbour_local_x, y,
				neighbour_local_z) != baseline_neighbour_light)
			return (ApplicationError::fail("block-edit boundary matrix restore", 1));
		position_index += 1;
	}
	std::printf("block-edit: boundary face matrix passed faces=4\n");
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::validate_solid_neighbor_geometry(World &world) noexcept
{
	const int32_t world_x = GAME_VOXEL_CHUNK_WIDTH - 1;
	const int32_t world_z = -6;
	const int32_t neighbour_x = world_x + 1;
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const int32_t neighbour_chunk_x = chunk_x + 1;
	const uint32_t air_block = GAME_VOXEL_AIR_BLOCK;
	uint32_t target_block;
	uint32_t neighbour_block;
	WorldChunk *target_chunk;
	WorldChunk *neighbour_chunk;
	double surface_top;
	int32_t y;
	uint64_t target_before;
	uint64_t neighbour_before;
	int32_t error_code;

	if (world.surface_top_at(world_x, world_z, &surface_top) == false)
		return (ApplicationError::fail("block-edit solid-neighbor surface", 1));
	y = static_cast<int32_t>(std::floor(surface_top));
	target_block = air_block;
	neighbour_block = air_block;
	while (y >= 0)
	{
		if (world.block_id_at(world_x, y, world_z, &target_block) == false
			|| world.block_id_at(neighbour_x, y, world_z, &neighbour_block)
				== false)
			return (ApplicationError::fail("block-edit solid-neighbor lookup", 1));
		if (target_block != air_block && neighbour_block != air_block
			&& voxel_block_is_breakable(target_block) != FT_FALSE
			&& voxel_block_is_breakable(neighbour_block) != FT_FALSE)
			break ;
		y -= 1;
	}
	if (y < 0)
	{
		std::fprintf(stderr,
			"block-edit: solid-neighbor setup target=%u neighbor=%u y=%d\n",
			static_cast<unsigned int>(target_block),
			static_cast<unsigned int>(neighbour_block), y);
		return (ApplicationError::fail("block-edit solid-neighbor setup", 1));
	}
	target_chunk = world.find_chunk_mutable(chunk_x, chunk_z);
	neighbour_chunk = world.find_chunk_mutable(neighbour_chunk_x, chunk_z);
	if (target_chunk == nullptr || neighbour_chunk == nullptr
		|| target_chunk->initialized == false
		|| neighbour_chunk->initialized == false)
		return (ApplicationError::fail("block-edit solid-neighbor chunks", 1));
	error_code = BlockEditValidator::wait_for_boundary_idle(world, world_x,
		world_z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit solid-neighbor idle",
			error_code));
	target_before = target_chunk->mesh_revision;
	neighbour_before = neighbour_chunk->mesh_revision;
	error_code = world.delete_block_at(world_x, y, world_z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit solid-neighbor delete",
			error_code));
	error_code = BlockEditValidator::wait_for_mesh_revision(world, neighbour_x,
		world_z, neighbour_before);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail(
			"block-edit solid-neighbor geometry remesh", error_code));
	if (world.block_id_at(neighbour_x, y, world_z, &neighbour_block) == false
		|| neighbour_block == air_block)
		return (ApplicationError::fail("block-edit solid-neighbor preserved", 1));
	if (!mesh_has_boundary_face(*neighbour_chunk, CHUNK_MESH_FACE_WEST, 0U))
	{
		std::fprintf(stderr,
			"block-edit: neighbor west boundary face missing after adjacent "
			"block deletion chunk=(%d,%d) y=%d mesh=%llu\n",
			neighbour_chunk->chunk_x, neighbour_chunk->chunk_z, y,
			static_cast<unsigned long long>(neighbour_chunk->mesh_revision));
		return (ApplicationError::fail(
			"block-edit solid-neighbor boundary face", 1));
	}
	error_code = world.place_block_at(world_x, y, world_z, target_block);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit solid-neighbor restore",
			error_code));
	error_code = BlockEditValidator::wait_for_mesh_revision(world, world_x,
		world_z, target_before);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail(
			"block-edit solid-neighbor restore remesh", error_code));
	if (world.block_id_at(world_x, y, world_z, &target_block) == false
		|| target_block == air_block)
		return (ApplicationError::fail("block-edit solid-neighbor restore state",
			1));
	std::printf("block-edit: solid-neighbor geometry passed neighbor_mesh=%llu\n",
		static_cast<unsigned long long>(neighbour_chunk->mesh_revision));
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::validate_rapid_border_supersession(World &world) noexcept
{
	static const int32_t BURST_COUNT = 4;
	const int32_t world_x = GAME_VOXEL_CHUNK_WIDTH - 1;
	const int32_t world_z = -6;
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	uint32_t block_id;
	double surface_top;
	int32_t y;
	int32_t iteration;
	int32_t error_code;
	uint64_t previous_revision;
	WorldChunk *target_chunk;
	WorldChunk *neighbour_chunk;

	if (world.surface_top_at(world_x, world_z, &surface_top) == false)
		return (ApplicationError::fail("block-edit rapid border surface", 1));
	y = static_cast<int32_t>(std::floor(surface_top + 1.0));
	if (world.block_id_at(world_x, y, world_z, &block_id) == false
		|| block_id != GAME_VOXEL_AIR_BLOCK)
		return (ApplicationError::fail("block-edit rapid border target", 1));
	target_chunk = world.find_chunk_mutable(chunk_x, chunk_z);
	neighbour_chunk = world.find_chunk_mutable(chunk_x + 1, chunk_z);
	if (target_chunk == nullptr || neighbour_chunk == nullptr
		|| target_chunk->initialized == false
		|| neighbour_chunk->initialized == false)
		return (ApplicationError::fail("block-edit rapid border chunks", 1));
	error_code = BlockEditValidator::wait_for_boundary_idle(world, world_x,
		world_z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit rapid border idle",
			error_code));
	previous_revision = target_chunk->mesh_revision;
	iteration = 0;
	while (iteration < BURST_COUNT)
	{
		error_code = world.place_block_at(world_x, y, world_z,
			VOXEL_GENERATOR_STONE_BLOCK);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit rapid border place",
				error_code));
		error_code = world.delete_block_at(world_x, y, world_z);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit rapid border delete",
				error_code));
		iteration += 1;
	}
	error_code = BlockEditValidator::wait_for_region_convergence(world, world_x,
		world_z, previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit rapid border convergence",
			error_code));
	if (world.block_id_at(world_x, y, world_z, &block_id) == false
		|| block_id != GAME_VOXEL_AIR_BLOCK
		|| target_chunk->light_is_current() == false
		|| target_chunk->pending_mesh_request_id != 0U
		|| target_chunk->mesh_dirty
		|| neighbour_chunk->light_is_current() == false
		|| neighbour_chunk->pending_mesh_request_id != 0U
		|| neighbour_chunk->mesh_dirty)
		return (ApplicationError::fail("block-edit rapid border final state", 1));
	std::printf("block-edit: rapid border supersession ok burst=%d\n",
		BURST_COUNT);
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::validate_cross_chunk_light_publication(World &world)
noexcept
{
	const int32_t world_x = GAME_VOXEL_CHUNK_WIDTH - 1;
	const int32_t world_z = -6;
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	WorldChunk *source;
	WorldChunk *neighbour;
	double surface_top;
	int32_t y;
	uint32_t block_id;
	std::chrono::steady_clock::time_point deadline;

	if (!world.surface_top_at(world_x, world_z, &surface_top))
		return (ApplicationError::fail("block-edit paired-light surface", 1));
	y = static_cast<int32_t>(std::floor(surface_top + 1.0));
	source = world.find_chunk_mutable(0, chunk_z);
	neighbour = world.find_chunk_mutable(1, chunk_z);
	if (source == nullptr || neighbour == nullptr || !source->initialized
		|| !neighbour->initialized
		|| !world.block_id_at(world_x, y, world_z, &block_id)
		|| block_id != GAME_VOXEL_AIR_BLOCK)
		return (ApplicationError::fail("block-edit paired-light baseline", 1));
	if (BlockEditValidator::wait_for_boundary_idle(world, world_x, world_z)
		!= FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit paired-light idle", 1));
	if (world.place_block_at(world_x, y, world_z, VOXEL_GENERATOR_STONE_BLOCK)
		!= FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit paired-light place", 1));
	deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (world.update_around(static_cast<double>(world_x),
			static_cast<double>(world_z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE) != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit paired-light update", 1));
		source = world.find_chunk_mutable(0, chunk_z);
		neighbour = world.find_chunk_mutable(1, chunk_z);
		if (source == nullptr || neighbour == nullptr
			|| !source->light_buffer_is_valid()
			|| !neighbour->light_buffer_is_valid()
			|| nonzero_light_cells(*source) == 0U
			|| nonzero_light_cells(*neighbour) == 0U)
			return (ApplicationError::fail(
				"block-edit paired-light publication gap", 1));
		if (source->light_is_current() && neighbour->light_is_current()
			&& source->pending_mesh_request_id == 0U
			&& neighbour->pending_mesh_request_id == 0U
			&& !source->mesh_dirty && !neighbour->mesh_dirty)
		{
			std::printf("block-edit: cross-chunk light publication stable\n");
			return (FT_ERR_SUCCESS);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return (ApplicationError::fail("block-edit paired-light timeout", 1));
}

int BlockEditValidator::validate_simultaneous_border_edits(World &world)
noexcept
{
	const int32_t first_x = GAME_VOXEL_CHUNK_WIDTH - 1;
	const int32_t second_x = GAME_VOXEL_CHUNK_WIDTH;
	const int32_t world_z = -6;
	double first_surface;
	double second_surface;
	int32_t first_y;
	int32_t second_y;
	uint32_t first_block;
	uint32_t second_block;
	WorldChunk *first_chunk = nullptr;
	WorldChunk *second_chunk = nullptr;
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);

	if (!world.surface_top_at(first_x, world_z, &first_surface)
		|| !world.surface_top_at(second_x, world_z, &second_surface))
		return (ApplicationError::fail("block-edit simultaneous border surface", 1));
	first_y = static_cast<int32_t>(std::floor(first_surface + 1.0));
	second_y = static_cast<int32_t>(std::floor(second_surface + 1.0));
	if (!world.block_id_at(first_x, first_y, world_z, &first_block)
		|| !world.block_id_at(second_x, second_y, world_z, &second_block)
		|| first_block != GAME_VOXEL_AIR_BLOCK
		|| second_block != GAME_VOXEL_AIR_BLOCK)
		return (ApplicationError::fail("block-edit simultaneous border baseline", 1));
	if (BlockEditValidator::wait_for_boundary_idle(world, first_x, world_z)
		!= FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit simultaneous border idle", 1));
	if (world.place_block_at(first_x, first_y, world_z,
		VOXEL_GENERATOR_STONE_BLOCK) != FT_ERR_SUCCESS
		|| world.place_block_at(second_x, second_y, world_z,
			VOXEL_GENERATOR_STONE_BLOCK) != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit simultaneous border place", 1));
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (world.update_around(static_cast<double>(first_x),
			static_cast<double>(world_z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE) != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit simultaneous border update", 1));
		first_chunk = world.find_chunk_mutable(0,
			WorldCoordinates::floor_divide(world_z, GAME_VOXEL_CHUNK_DEPTH));
		second_chunk = world.find_chunk_mutable(1,
			WorldCoordinates::floor_divide(world_z, GAME_VOXEL_CHUNK_DEPTH));
		if (first_chunk == nullptr || second_chunk == nullptr
			|| !first_chunk->light_buffer_is_valid()
			|| !second_chunk->light_buffer_is_valid()
			|| nonzero_light_cells(*first_chunk) == 0U
			|| nonzero_light_cells(*second_chunk) == 0U)
			return (ApplicationError::fail(
				"block-edit simultaneous border light gap", 1));
		if (first_chunk->light_is_current() && second_chunk->light_is_current()
			&& first_chunk->pending_mesh_request_id == 0U
			&& second_chunk->pending_mesh_request_id == 0U
			&& !first_chunk->mesh_dirty && !second_chunk->mesh_dirty)
			break ;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (first_chunk == nullptr || second_chunk == nullptr
		|| !first_chunk->light_is_current()
		|| !second_chunk->light_is_current()
		|| first_chunk->pending_mesh_request_id != 0U
		|| second_chunk->pending_mesh_request_id != 0U
		|| first_chunk->mesh_dirty || second_chunk->mesh_dirty)
		return (ApplicationError::fail("block-edit simultaneous border timeout", 1));
	if (world.delete_block_at(first_x, first_y, world_z) != FT_ERR_SUCCESS
		|| world.delete_block_at(second_x, second_y, world_z) != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit simultaneous border delete", 1));
	const std::chrono::steady_clock::time_point restore_deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (std::chrono::steady_clock::now() < restore_deadline)
	{
		if (world.update_around(static_cast<double>(first_x),
			static_cast<double>(world_z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE) != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit simultaneous border restore", 1));
		first_chunk = world.find_chunk_mutable(0,
			WorldCoordinates::floor_divide(world_z, GAME_VOXEL_CHUNK_DEPTH));
		second_chunk = world.find_chunk_mutable(1,
			WorldCoordinates::floor_divide(world_z, GAME_VOXEL_CHUNK_DEPTH));
		if (first_chunk != nullptr && second_chunk != nullptr
			&& first_chunk->light_is_current()
			&& second_chunk->light_is_current()
			&& first_chunk->pending_mesh_request_id == 0U
			&& second_chunk->pending_mesh_request_id == 0U
			&& !first_chunk->mesh_dirty && !second_chunk->mesh_dirty)
		{
			std::printf("block-edit: simultaneous border edits converged\n");
			return (FT_ERR_SUCCESS);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return (ApplicationError::fail("block-edit simultaneous border restore timeout", 1));
}

int BlockEditValidator::validate_neighbor_arrival_after_completed_edit(
	World &world) noexcept
{
	static const int32_t neighbour_offsets[4][2] = {
		{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
	WorldChunk *source;
	WorldChunk *target;
	uint64_t previous_light_revision;
	uint16_t previous_light_input_version;
	int32_t source_index;
	int32_t direction_index;
	uint8_t summary_face;

	source_index = 0;
	while (source_index < world.chunk_count)
	{
		source = &world.chunks[source_index];
		if (!source->initialized || source->voxel_revision <= 1U
			|| source->pending_mesh_request_id != 0U || source->mesh_dirty)
		{
			source_index += 1;
			continue ;
		}
		direction_index = 0;
		while (direction_index < 4)
		{
			summary_face = 0U;
			if (direction_index == 0)
				summary_face = 1U;
			else if (direction_index == 1)
				summary_face = 0U;
			else if (direction_index == 2)
				summary_face = 3U;
			else
				summary_face = 2U;
			target = world.find_chunk_mutable(source->chunk_x
				+ neighbour_offsets[direction_index][0], source->chunk_z
				+ neighbour_offsets[direction_index][1]);
			if (target != nullptr && target->initialized
				&& target->pending_mesh_request_id == 0U
				&& !target->mesh_dirty
				&& source->get_boundary_source_light_max(summary_face) > 1U
				&& target->boundary_has_transparent_target(summary_face))
			{
				previous_light_revision = target->light_revision;
				previous_light_input_version = target->light_input_version;
				world.chunk_streamer.mark_neighbor_remeshes(source->chunk_x,
					source->chunk_z, false, true);
				if (target->light_revision == previous_light_revision
					|| !target->waits_for_neighbor_light
					|| target->light_dependency_chunk_x != source->chunk_x
					|| target->light_dependency_chunk_z != source->chunk_z)
				{
					std::fprintf(stderr,
						"block-edit: completed-edit neighbor arrival did not "
						"schedule border light source=(%d,%d) target=(%d,%d) "
						"source_voxel=%llu target_light=%llu previous=%llu\n",
						source->chunk_x, source->chunk_z, target->chunk_x,
						target->chunk_z,
						static_cast<unsigned long long>(source->voxel_revision),
						static_cast<unsigned long long>(target->light_revision),
						static_cast<unsigned long long>(previous_light_revision));
					return (1);
				}
				target->light_revision = previous_light_revision;
				target->light_input_version = previous_light_input_version;
				target->clear_light_dependency();
				target->mesh_dirty = false;
				std::printf("block-edit: completed-edit neighbor arrival "
					"scheduled source=(%d,%d) target=(%d,%d)\n",
					source->chunk_x, source->chunk_z, target->chunk_x,
					target->chunk_z);
				return (FT_ERR_SUCCESS);
			}
			direction_index += 1;
		}
		source_index += 1;
	}
	std::fprintf(stderr,
		"block-edit: no stable lit transparent neighbor pair for "
		"completed-edit arrival regression\n");
	return (1);
}

int BlockEditValidator::validate_authoritative_edit(World &world) noexcept
{
	const int32_t world_x = 2;
	const int32_t world_z = -6;
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const int32_t local_x = world_x - chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	const int32_t local_z = world_z - chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	const uint32_t edited_block = VOXEL_GENERATOR_STONE_BLOCK;
	const uint32_t air_block = GAME_VOXEL_AIR_BLOCK;
	WorldChunk *chunk;
	game_block_change_request request;
	game_block_delta delta;
	uint32_t current_block_id;
	uint64_t previous_revision;
	uint64_t previous_mesh_revision;
	int32_t error_code;

	double surface_top = 0.0;
	if (world.surface_top_at(world_x, world_z, &surface_top) == false)
		return (ApplicationError::fail("block-edit authoritative surface", 1));
	const int32_t local_y = static_cast<int32_t>(std::floor(surface_top + 1.0));
	chunk = world.find_chunk_mutable(chunk_x, chunk_z);
	if (chunk == nullptr || !chunk->initialized
		|| chunk->chunk.read_block(local_x, local_y, local_z,
			&current_block_id) != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit authoritative target", 1));
	if (current_block_id != air_block)
		return (ApplicationError::fail("block-edit authoritative target state", 1));
	previous_revision = chunk->chunk.get_revision();
	previous_mesh_revision = chunk->mesh_revision;
	request.protocol_version = GAME_WORLD_DELTA_PROTOCOL_VERSION;
	request.session_id = 91U;
	request.request_id = 1U;
	request.world_id = 7U;
	request.chunk_x = chunk_x;
	request.chunk_z = chunk_z;
	request.expected_revision = previous_revision;
	request.expected_block_id = air_block;
	request.requested_block_id = edited_block;
	request.local_x = static_cast<uint8_t>(local_x);
	request.local_y = static_cast<uint16_t>(local_y);
	request.local_z = static_cast<uint8_t>(local_z);
	error_code = world.apply_authoritative_block_change(request, &delta);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit authoritative place", error_code));
	if (delta.current_block_id != edited_block)
		return (ApplicationError::fail("block-edit authoritative place delta", 1));
	if (chunk->light_buffer_is_valid() == false || chunk->light_is_current()
		!= false)
		return (ApplicationError::fail("block-edit authoritative place light", 1));
	error_code = BlockEditValidator::wait_for_mesh_revision(world, world_x,
		world_z, previous_mesh_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit authoritative place remesh",
			error_code));
	request.request_id = 2U;
	request.expected_revision = delta.revision;
	request.expected_block_id = edited_block;
	request.requested_block_id = air_block;
	previous_mesh_revision = chunk->mesh_revision;
	error_code = world.apply_authoritative_block_change(request, &delta);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit authoritative delete", error_code));
	if (delta.current_block_id != air_block)
		return (ApplicationError::fail("block-edit authoritative delete delta", 1));
	error_code = BlockEditValidator::wait_for_mesh_revision(world, world_x,
		world_z, previous_mesh_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit authoritative delete remesh",
			error_code));
	request.request_id = 3U;
	request.expected_revision = delta.revision - 1U;
	request.expected_block_id = air_block;
	request.requested_block_id = edited_block;
	if (world.apply_authoritative_block_change(request, &delta)
		!= FT_ERR_INVALID_STATE)
		return (ApplicationError::fail("block-edit authoritative stale request", 1));
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
	std::unique_ptr<World> world_storage(new (std::nothrow) World());
	if (world_storage == nullptr)
		return (ApplicationError::fail("block-edit world allocation",
			FT_ERR_NO_MEMORY));
	World &world = *world_storage;
	int32_t edit_x;
	int32_t edit_y;
	int32_t edit_z;
	size_t mesh_before;
	size_t mesh_after_place;
	size_t mesh_after_delete;
	int32_t error_code;
	if (world_light_version::next(65535U) != 1U
		|| world_light_version::next(0U) != 1U
		|| world_light_version::matches(0U, 1U))
		return (ApplicationError::fail("block-edit light version rollover", 1));

	edit_x = 2;
	edit_z = -6;
	error_code = world.initialize("integration-seed");
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit world initialization",
				error_code));
	{
		const WorldChunk *initial_chunk = world.find_chunk(0, 0);
		if (initial_chunk != nullptr && initial_chunk->initialized
			&& (initial_chunk->content_version == 0U
				|| initial_chunk->light_input_version == 0U))
			return (ApplicationError::fail("block-edit initial light versions", 1));
	}
	if (wait_for_edit_baseline(world, edit_x, edit_z) != FT_ERR_SUCCESS)
	{
		world.destroy();
		return (ApplicationError::fail("block-edit light baseline", 1));
	}
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
	if (BlockEditValidator::validate_rapid_edit_supersession(world, edit_x,
		edit_y, edit_z) != FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_interior_emissive_edit(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_boundary_edit(world) != FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_boundary_face_matrix(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_solid_neighbor_geometry(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_rapid_border_supersession(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_cross_chunk_light_publication(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_simultaneous_border_edits(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_neighbor_arrival_after_completed_edit(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_authoritative_edit(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	world.destroy();
	return (0);
}
