#include "../../src/validators/BlockEditPerformanceValidator.hpp"
#include "../../src/diagnostics/ApplicationError.hpp"
#include "../../src/coordinates/WorldCoordinates.hpp"
#include "../../Libft/Modules/Basic/limits.hpp"
#include <chrono>
#include <cstdio>
#include <thread>

namespace
{
	static const uint64_t MAX_WORKLOAD_FRAMES = 4096U;
	static const uint64_t DEFAULT_EDIT_COUNT = 64U;
	static const int32_t EDIT_X = 2;
	static const int32_t EDIT_Z = -6;
	static const char *REPORT_PATH = "minecraft_block_edit_workload.jsonl";
}

BlockEditPerformanceValidator::BlockEditPerformanceValidator()
{
}

BlockEditPerformanceValidator::BlockEditPerformanceValidator(
	const BlockEditPerformanceValidator &other)
	: IValidator(other)
{
	(void)other;
}

BlockEditPerformanceValidator::~BlockEditPerformanceValidator()
{
}

BlockEditPerformanceValidator &BlockEditPerformanceValidator::operator=(
	const BlockEditPerformanceValidator &other)
{
	(void)other;
	return (*this);
}

uint64_t BlockEditPerformanceValidator::count_dirty_remeshes(
	const World &world) noexcept
{
	uint64_t count;
	int32_t index;

	count = 0U;
	index = 0;
	while (index < world.chunk_count)
	{
		if (world.chunks[index].initialized
			&& world.chunks[index].mesh_dirty)
			count += 1U;
		index += 1;
	}
	return (count);
}

uint64_t BlockEditPerformanceValidator::counter_delta(uint64_t before,
	uint64_t after) noexcept
{
	if (after < before)
		return (after);
	return (after - before);
}

int32_t BlockEditPerformanceValidator::capture_frame(World &world,
	uint64_t frame_index, const World::StreamDiagnostics &before,
	std::vector<BlockEditWorkloadFrame> &frames) noexcept
{
	World::StreamDiagnostics after;
	const std::chrono::steady_clock::time_point start =
		std::chrono::steady_clock::now();
	const int32_t error_code = world.update_around(
		static_cast<double>(EDIT_X), static_cast<double>(EDIT_Z), 0,
		WorldCoordinates::MIN_RENDER_DISTANCE);
	const uint64_t drain_us = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - start).count());

	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	after = world.stream_diagnostics();
	BlockEditWorkloadFrame frame;
	frame.frame_index = frame_index;
	frame.world_drain_us = drain_us;
	frame.frame_total_us = drain_us;
	frame.light_nodes_processed =
		BlockEditPerformanceValidator::counter_delta(
			before.remesh_scanned_cells, after.remesh_scanned_cells)
		+ BlockEditPerformanceValidator::counter_delta(
			before.remesh_propagated_cells, after.remesh_propagated_cells);
	frame.snapshot_bytes = BlockEditPerformanceValidator::counter_delta(
		before.remesh_snapshot_bytes, after.remesh_snapshot_bytes);
	frame.light_queue_peak = after.remesh_light_queue_peak;
	frame.dirty_remesh_count =
		BlockEditPerformanceValidator::count_dirty_remeshes(world);
	frame.stale_results =
		BlockEditPerformanceValidator::counter_delta(
			before.stale_result_count, after.stale_result_count);
	frame.remesh_completed =
		BlockEditPerformanceValidator::counter_delta(
			before.remesh_completed_count, after.remesh_completed_count);
	frames.push_back(frame);
	return (FT_ERR_SUCCESS);
}

int32_t BlockEditPerformanceValidator::wait_for_revision(World &world,
	int32_t world_x, int32_t world_z, uint64_t previous_revision,
	uint64_t frame_index, std::vector<BlockEditWorkloadFrame> &frames,
	uint64_t &latency_us) noexcept
{
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
	const std::chrono::steady_clock::time_point deadline = started
		+ std::chrono::seconds(30);
	uint64_t current_frame;

	current_frame = frame_index;
	while (std::chrono::steady_clock::now() < deadline)
	{
		const World::StreamDiagnostics before = world.stream_diagnostics();
		int32_t error_code;

		error_code = BlockEditPerformanceValidator::capture_frame(world,
			current_frame, before, frames);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (world.find_chunk(WorldCoordinates::floor_divide(world_x,
				GAME_VOXEL_CHUNK_WIDTH), WorldCoordinates::floor_divide(world_z,
				GAME_VOXEL_CHUNK_DEPTH)) != nullptr
			&& world.find_chunk(WorldCoordinates::floor_divide(world_x,
				GAME_VOXEL_CHUNK_WIDTH), WorldCoordinates::floor_divide(world_z,
				GAME_VOXEL_CHUNK_DEPTH))->mesh_revision > previous_revision)
		{
			latency_us = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - started).count());
			return (FT_ERR_SUCCESS);
		}
		current_frame += 1U;
		if (current_frame >= MAX_WORKLOAD_FRAMES)
			return (FT_ERR_TIMEOUT);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return (FT_ERR_TIMEOUT);
}

int32_t BlockEditPerformanceValidator::prepare_target(World &world,
	int32_t world_x, int32_t world_z, int32_t &world_y) noexcept
{
	double surface_top;

	if (world.surface_top_at(world_x, world_z, &surface_top) == false)
		return (FT_ERR_NOT_FOUND);
	world_y = static_cast<int32_t>(surface_top + 1.0);
	return (FT_ERR_SUCCESS);
}

int32_t BlockEditPerformanceValidator::restore_block(World &world,
	int32_t world_x, int32_t world_y, int32_t world_z,
	uint64_t &frame_index, std::vector<BlockEditWorkloadFrame> &frames) noexcept
{
	const uint64_t previous_revision = world.find_chunk(
		WorldCoordinates::floor_divide(world_x, GAME_VOXEL_CHUNK_WIDTH),
		WorldCoordinates::floor_divide(world_z, GAME_VOXEL_CHUNK_DEPTH))
		== nullptr ? 0U : world.find_chunk(
		WorldCoordinates::floor_divide(world_x, GAME_VOXEL_CHUNK_WIDTH),
		WorldCoordinates::floor_divide(world_z, GAME_VOXEL_CHUNK_DEPTH))
		->mesh_revision;
	int32_t error_code;
	uint64_t ignored_latency;

	error_code = world.place_block_at(world_x, world_y, world_z,
		VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = BlockEditPerformanceValidator::wait_for_revision(world,
		world_x, world_z, previous_revision, frame_index, frames,
		ignored_latency);
	if (error_code == FT_ERR_SUCCESS)
		frame_index = frames.size();
	return (error_code);
}

int32_t BlockEditPerformanceValidator::run_workload(World &world,
	int32_t world_y, uint64_t requested_edits,
	std::vector<BlockEditWorkloadFrame> &frames,
	std::vector<uint64_t> &latencies_us, uint64_t &completed_edits,
	uint64_t &failed_edits) noexcept
{
	uint64_t frame_index;
	uint64_t edit_index;

	completed_edits = 0U;
	failed_edits = 0U;
	frame_index = frames.size();
	edit_index = 0U;
	while (edit_index < requested_edits)
	{
		const WorldChunk *chunk = world.find_chunk(
			WorldCoordinates::floor_divide(EDIT_X, GAME_VOXEL_CHUNK_WIDTH),
			WorldCoordinates::floor_divide(EDIT_Z, GAME_VOXEL_CHUNK_DEPTH));
		const uint64_t previous_revision = chunk == nullptr ? 0U
			: chunk->mesh_revision;
		const std::chrono::steady_clock::time_point started =
			std::chrono::steady_clock::now();
		int32_t error_code;
		uint64_t latency_us;

		error_code = world.delete_block_at(EDIT_X, world_y, EDIT_Z);
		if (error_code != FT_ERR_SUCCESS)
		{
			failed_edits += 1U;
			return (error_code);
		}
		uint32_t block_id;
		if (world.block_id_at(EDIT_X, world_y, EDIT_Z, &block_id) == false
			|| block_id != GAME_VOXEL_AIR_BLOCK)
		{
			failed_edits += 1U;
			return (FT_ERR_INTERNAL);
		}
		error_code = BlockEditPerformanceValidator::wait_for_revision(world,
			EDIT_X, EDIT_Z, previous_revision, frame_index, frames, latency_us);
		if (error_code != FT_ERR_SUCCESS)
		{
			failed_edits += 1U;
			return (error_code);
		}
		latency_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - started).count());
		latencies_us.push_back(latency_us);
		completed_edits += 1U;
		frame_index = frames.size();
		error_code = BlockEditPerformanceValidator::restore_block(world,
			EDIT_X, world_y, EDIT_Z, frame_index, frames);
		if (error_code != FT_ERR_SUCCESS)
		{
			failed_edits += 1U;
			return (error_code);
		}
		if (world.block_id_at(EDIT_X, world_y, EDIT_Z, &block_id) == false
			|| block_id != VOXEL_GENERATOR_STONE_BLOCK)
		{
			failed_edits += 1U;
			return (FT_ERR_INTERNAL);
		}
		edit_index += 1U;
	}
	return (FT_ERR_SUCCESS);
}

int BlockEditPerformanceValidator::validate() const
{
	World world;
	std::vector<BlockEditWorkloadFrame> frames;
	std::vector<uint64_t> latencies_us;
	uint64_t completed_edits;
	uint64_t failed_edits;
	int32_t world_y;
	int32_t error_code;

	error_code = world.initialize("block-edit-performance-seed");
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit-performance world",
			error_code));
	error_code = BlockEditPerformanceValidator::prepare_target(world, EDIT_X,
		EDIT_Z, world_y);
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = world.place_block_at(EDIT_X, world_y, EDIT_Z,
			VOXEL_GENERATOR_STONE_BLOCK);
		if (error_code == FT_ERR_SUCCESS)
		{
			uint64_t frame_index = 0U;
			uint64_t ignored_latency;
			error_code = BlockEditPerformanceValidator::wait_for_revision(world,
				EDIT_X, EDIT_Z, 0U, frame_index, frames, ignored_latency);
		}
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = BlockEditPerformanceValidator::run_workload(world,
			world_y, DEFAULT_EDIT_COUNT, frames, latencies_us, completed_edits,
			failed_edits);
	if (error_code == FT_ERR_SUCCESS)
		error_code = BlockEditWorkloadAnalytics::write_report(REPORT_PATH,
			frames, latencies_us, DEFAULT_EDIT_COUNT, completed_edits,
			failed_edits);
	world.destroy();
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit-performance", error_code));
	return (0);
}
