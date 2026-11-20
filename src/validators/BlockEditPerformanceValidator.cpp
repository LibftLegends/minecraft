#include "../../src/validators/BlockEditPerformanceValidator.hpp"
#include "../../src/diagnostics/ApplicationError.hpp"
#include "../../src/coordinates/WorldCoordinates.hpp"
#include "../../src/network/WorldReplicationService.hpp"
#include "../../src/render/VoxelRenderer.hpp"
#include "../../Libft/Modules/Basic/limits.hpp"
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <new>
#include <numeric>
#include <thread>

namespace
{
	static const uint64_t MAX_WORKLOAD_FRAMES = 65536U;
	static const uint64_t DEFAULT_EDIT_COUNT = 64U;
	static const uint64_t MAX_EDIT_P95_US = 100000U;
	static const uint64_t MAX_EDIT_P99_US = 250000U;
	static const uint64_t MAX_LIGHT_NODES_PER_EDIT = 4000000U;
	static const uint64_t MAX_SNAPSHOT_BYTES_PER_EDIT = 512U * 1024U;
	static const uint64_t MAX_DIRTY_REMESHES = 16U;
	static const int32_t EDIT_X = 2;
	static const int32_t EDIT_Z = -6;
	static const char *REPORT_PATH = "minecraft_block_edit_workload.jsonl";

	static uint64_t workload_percentile(
		const std::vector<uint64_t> &samples, uint64_t percentage) noexcept
	{
		std::vector<uint64_t> sorted_samples;
		size_t index;

		if (samples.empty())
			return (0U);
		sorted_samples = samples;
		std::sort(sorted_samples.begin(), sorted_samples.end());
		index = (sorted_samples.size() - 1U) * percentage / 100U;
		return (sorted_samples[index]);
	}

	static void print_software_frame_times(const char *workload,
		const std::vector<uint64_t> &samples)
	{
		const uint64_t mean = std::accumulate(samples.begin(), samples.end(), 0ULL)
			/ samples.size();
		std::printf("block-edit-performance: software 640x360 workload=%s "
			"frames=%zu mean_us=" FT_UINT64_DECIMAL_FORMAT
			" p95_us=" FT_UINT64_DECIMAL_FORMAT
			" p99_us=" FT_UINT64_DECIMAL_FORMAT "\n",
			workload, samples.size(), mean, workload_percentile(samples, 95U),
			workload_percentile(samples, 99U));
	}

	static int32_t verify_failed_write_preserves_edit_state(World &world,
		WorldReplicationService &service, const WorldChunk &chunk,
		const ProtocolEditIntentMessage &accepted, const uint8_t (&world_id)[32])
	{
		static const char HEX[] = "0123456789abcdef";
		char encoded_id[65] = {};
		for (std::size_t index = 0; index < sizeof(world_id); ++index)
		{
			encoded_id[index * 2U] = HEX[world_id[index] >> 4U];
			encoded_id[index * 2U + 1U] = HEX[world_id[index] & 15U];
		}
		const auto directory = std::filesystem::path("saves/terrain") / encoded_id;
		auto hidden_directory = directory;
		hidden_directory += ".rollback-test";
		std::error_code filesystem_error;
		std::filesystem::rename(directory, hidden_directory, filesystem_error);
		if (filesystem_error)
			return (FT_ERR_IO);

		game_block_change_request request = accepted.request;
		const uint64_t revision = chunk.chunk.get_revision();
		const uint32_t light_input_version = chunk.light_input_version;
		const auto read_state = chunk.read_state;
		request.request_id += 1U;
		request.expected_revision = revision;
		request.expected_block_id = accepted.request.requested_block_id;
		request.requested_block_id = accepted.request.expected_block_id;
		game_block_delta unpublished = {};
		const int32_t apply_error = world.apply_authoritative_block_change(
			request, &unpublished);
		std::filesystem::rename(hidden_directory, directory, filesystem_error);
		if (filesystem_error)
			return (FT_ERR_IO);

		uint32_t block_id = UINT32_MAX;
		if (apply_error == FT_ERR_SUCCESS || unpublished.revision != 0U
			|| chunk.chunk.get_revision() != revision
			|| chunk.voxel_revision != revision
			|| chunk.light_input_version != light_input_version
			|| chunk.read_state != read_state
			|| chunk.chunk.read_block(request.local_x, request.local_y,
				request.local_z, &block_id) != FT_ERR_SUCCESS
			|| block_id != request.expected_block_id)
			return (FT_ERR_INVALID_STATE);
		ProtocolEditResultMessage replay;
		const int32_t replay_error = service.handle_edit_intent(accepted, replay);
		if (replay_error != FT_ERR_SUCCESS || replay.accepted == 0U
			|| replay.authoritative_revision != revision
			|| replay.delta.current_block_id != block_id
			|| chunk.read_state != read_state)
			return (FT_ERR_INVALID_STATE);
		std::printf("block-edit-performance: write failure preserved revision and replay\n");
		return (FT_ERR_SUCCESS);
	}

	static int32_t measure_persisted_server_edits(World &world, int32_t world_y)
	{
		const int32_t chunk_x = WorldCoordinates::floor_divide(EDIT_X,
			GAME_VOXEL_CHUNK_WIDTH);
		const int32_t chunk_z = WorldCoordinates::floor_divide(EDIT_Z,
			GAME_VOXEL_CHUNK_DEPTH);
		const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
		if (chunk == nullptr || !chunk->initialized)
			return (FT_ERR_NOT_FOUND);
		WorldReplicationService service;
		int32_t error = service.initialize(world, 1U, 1U);
		if (error != FT_ERR_SUCCESS)
			return (error);
		std::vector<uint64_t> latencies;
		std::vector<uint64_t> idle_frames;
		std::vector<uint64_t> edit_frames;
		latencies.reserve(DEFAULT_EDIT_COUNT);
		idle_frames.reserve(DEFAULT_EDIT_COUNT * 2U);
		edit_frames.reserve(DEFAULT_EDIT_COUNT * 2U);
		std::unique_ptr<VoxelRenderer> renderer(new (std::nothrow) VoxelRenderer());
		if (renderer == nullptr)
			return (FT_ERR_NO_MEMORY);
		std::vector<uint32_t> pixels(640U * 360U);
		ft_render_framebuffer framebuffer = {640, 360, pixels.data()};
		Camera camera;
		camera.initialize();
		camera.x = EDIT_X + 0.5;
		camera.y = world_y + 3.0;
		camera.z = EDIT_Z - 5.0;
		camera.pitch = -0.45;
		renderer->preload_assets();
		const auto render_frame = [&]() -> int32_t
		{
			const int32_t update_error = world.update_around(EDIT_X, EDIT_Z, 0,
				WorldCoordinates::MIN_RENDER_DISTANCE);
			if (update_error == FT_ERR_SUCCESS)
				renderer->render_world(framebuffer, camera, world, nullptr);
			return (update_error);
		};
		error = render_frame();
		for (uint64_t index = 0U; index < DEFAULT_EDIT_COUNT * 2U
				&& error == FT_ERR_SUCCESS; ++index)
		{
			const auto started = std::chrono::steady_clock::now();
			error = render_frame();
			idle_frames.push_back(static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - started).count()));
		}
		if (error != FT_ERR_SUCCESS)
			return (error);
		uint8_t id[32] = {'e', 'd', 'i', 't', '-', 'p', 'e', 'r', 'f'};
		const uint64_t nonce = static_cast<uint64_t>(
			std::chrono::system_clock::now().time_since_epoch().count());
		for (uint32_t index = 0U; index < sizeof(nonce); ++index)
			id[16U + index] = static_cast<uint8_t>(nonce >> (index * 8U));
		WorldTerrainChunkStore &store = world.terrain_chunk_store();
		error = store.initialize(id);
		if (error != FT_ERR_SUCCESS)
			return (error);
		/* Save and render one edit per frame. Measure the server handler
		 * separately so its time can be compared with the full frame. */
		for (uint64_t index = 0U; index < DEFAULT_EDIT_COUNT * 2U
				&& error == FT_ERR_SUCCESS; ++index)
		{
			const bool removing = index % 2U == 0U;
			ProtocolEditIntentMessage intent;
			intent.request.protocol_version = GAME_WORLD_DELTA_PROTOCOL_VERSION;
			intent.request.session_id = 1U;
			intent.request.request_id = index + 1U;
			intent.request.world_id = service.world_id();
			intent.request.chunk_x = chunk_x;
			intent.request.chunk_z = chunk_z;
			intent.request.local_x = static_cast<uint8_t>(
				EDIT_X - chunk_x * GAME_VOXEL_CHUNK_WIDTH);
			intent.request.local_y = static_cast<uint16_t>(world_y);
			intent.request.local_z = static_cast<uint8_t>(
				EDIT_Z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
			intent.request.expected_revision = chunk->chunk.get_revision();
			intent.request.expected_block_id = removing
				? VOXEL_GENERATOR_STONE_BLOCK : GAME_VOXEL_AIR_BLOCK;
			intent.request.requested_block_id = removing
				? GAME_VOXEL_AIR_BLOCK : VOXEL_GENERATOR_STONE_BLOCK;
			ProtocolEditResultMessage result;
			const auto started = std::chrono::steady_clock::now();
			error = service.handle_edit_intent(intent, result);
			const uint64_t elapsed_us = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - started).count());
			if (error != FT_ERR_SUCCESS)
				break ;
			if (result.accepted == 0U)
				error = result.result_code;
			else if (result.authoritative_revision != intent.request.expected_revision + 1U
				|| result.delta.current_block_id != intent.request.requested_block_id)
				error = FT_ERR_INTERNAL;
			else if (removing)
				latencies.push_back(elapsed_us);
			if (error == FT_ERR_SUCCESS)
			{
				error = render_frame();
				edit_frames.push_back(static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::microseconds>(
						std::chrono::steady_clock::now() - started).count()));
			}
			if (error == FT_ERR_SUCCESS && (index == 0U
					|| index + 1U == DEFAULT_EDIT_COUNT * 2U))
			{
				game_voxel_chunk saved;
				error = saved.initialize();
				if (error == FT_ERR_SUCCESS)
					error = store.load_chunk(chunk_x, chunk_z, saved);
				uint32_t saved_id = UINT32_MAX;
				if (error == FT_ERR_SUCCESS)
					error = saved.read_block(intent.request.local_x, world_y,
						intent.request.local_z, &saved_id);
				if (error == FT_ERR_SUCCESS && (saved.get_revision()
						!= result.authoritative_revision
					|| saved_id != result.delta.current_block_id))
					error = FT_ERR_INTERNAL;
				if (error == FT_ERR_SUCCESS && index + 1U == DEFAULT_EDIT_COUNT * 2U)
					error = verify_failed_write_preserves_edit_state(world,
						service, *chunk, intent, id);
			}
		}
		const int32_t cleanup_error = store.remove_chunk(chunk_x, chunk_z);
		store.reset();
		if (error == FT_ERR_SUCCESS)
			error = cleanup_error;
		if (error == FT_ERR_SUCCESS && latencies.size() != DEFAULT_EDIT_COUNT)
			error = FT_ERR_INTERNAL;
		if (error == FT_ERR_SUCCESS)
		{
			std::printf("block-edit-performance: persisted server deletes=%zu "
				"p50_us=" FT_UINT64_DECIMAL_FORMAT " p95_us=" FT_UINT64_DECIMAL_FORMAT
				" p99_us=" FT_UINT64_DECIMAL_FORMAT " max_us=" FT_UINT64_DECIMAL_FORMAT "\n",
				latencies.size(), workload_percentile(latencies, 50U),
				workload_percentile(latencies, 95U), workload_percentile(latencies, 99U),
				*std::max_element(latencies.begin(), latencies.end()));
			print_software_frame_times("idle", idle_frames);
			print_software_frame_times("saved-edit", edit_frames);
		}
		return (error);
	}

	static uint64_t workload_frame_sum(
		const std::vector<BlockEditWorkloadFrame> &frames,
		uint64_t BlockEditWorkloadFrame::*field) noexcept
	{
		uint64_t total;
		size_t index;

		total = 0U;
		index = 0U;
		while (index < frames.size())
		{
			total += frames[index].*field;
			index += 1U;
		}
		return (total);
	}

	static uint64_t workload_frame_max(
		const std::vector<BlockEditWorkloadFrame> &frames,
		uint64_t BlockEditWorkloadFrame::*field) noexcept
	{
		uint64_t maximum;
		size_t index;

		maximum = 0U;
		index = 0U;
		while (index < frames.size())
		{
			if (frames[index].*field > maximum)
				maximum = frames[index].*field;
			index += 1U;
		}
		return (maximum);
	}

	static bool performance_bounds_are_valid(
		const std::vector<BlockEditWorkloadFrame> &frames,
		const std::vector<uint64_t> &latencies, uint64_t completed_edits,
		uint64_t failed_edits) noexcept
	{
		uint64_t nodes_total;
		uint64_t snapshot_total;
		uint64_t nodes_per_edit;
		uint64_t p95;
		uint64_t p99;
		uint64_t dirty_peak;

		if (completed_edits == 0U || failed_edits != 0U)
			return (false);
		p95 = workload_percentile(latencies, 95U);
		p99 = workload_percentile(latencies, 99U);
		nodes_total = workload_frame_sum(frames,
			&BlockEditWorkloadFrame::light_nodes_processed);
		snapshot_total = workload_frame_sum(frames,
			&BlockEditWorkloadFrame::snapshot_bytes);
		nodes_per_edit = nodes_total / completed_edits;
		dirty_peak = workload_frame_max(frames,
			&BlockEditWorkloadFrame::dirty_remesh_count);
		/* Each deletion is followed by a measured restoration. Apply the same
		 * byte allowance to each of the two voxel edits. */
		const uint64_t snapshot_budget = completed_edits * 2U
			* MAX_SNAPSHOT_BYTES_PER_EDIT;
		if (p95 > MAX_EDIT_P95_US || p99 > MAX_EDIT_P99_US
			|| nodes_per_edit > MAX_LIGHT_NODES_PER_EDIT
			|| snapshot_total > snapshot_budget
			|| dirty_peak > MAX_DIRTY_REMESHES)
		{
			std::fprintf(stderr,
				"block-edit-performance: bounds failed p95_us=%llu "
				"p99_us=%llu nodes_per_edit=%llu snapshot_bytes=%llu "
				"dirty_peak=%llu failed=%llu\n",
				static_cast<unsigned long long>(p95),
				static_cast<unsigned long long>(p99),
				static_cast<unsigned long long>(nodes_per_edit),
				static_cast<unsigned long long>(snapshot_total),
				static_cast<unsigned long long>(dirty_peak),
				static_cast<unsigned long long>(failed_edits));
			return (false);
		}
		return (true);
	}
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

int32_t BlockEditPerformanceValidator::wait_for_stream_to_settle(
	World &world) noexcept
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (std::chrono::steady_clock::now() < deadline)
	{
		const int32_t error_code = world.update_around(
			static_cast<double>(EDIT_X), static_cast<double>(EDIT_Z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		const World::StreamDiagnostics diagnostics = world.stream_diagnostics();
		if (diagnostics.candidate_count != 0U
			&& diagnostics.ready_count == diagnostics.candidate_count
			&& diagnostics.pending_count == 0U
			&& diagnostics.remesh_priority_queue_depth == 0U
			&& BlockEditPerformanceValidator::count_dirty_remeshes(world) == 0U)
			return (FT_ERR_SUCCESS);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return (FT_ERR_TIMEOUT);
}

uint64_t BlockEditPerformanceValidator::counter_delta(uint64_t before,
	uint64_t after) noexcept
{
	if (after < before)
		return (after);
	return (after - before);
}

int32_t BlockEditPerformanceValidator::capture_frame(World &world,
	uint64_t frame_index, World::StreamDiagnostics &previous_sample,
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
			previous_sample.remesh_scanned_cells, after.remesh_scanned_cells)
		+ BlockEditPerformanceValidator::counter_delta(
			previous_sample.remesh_propagated_cells, after.remesh_propagated_cells);
	frame.snapshot_bytes = BlockEditPerformanceValidator::counter_delta(
		previous_sample.remesh_snapshot_bytes, after.remesh_snapshot_bytes);
	frame.light_queue_peak = after.remesh_light_queue_peak;
	frame.dirty_remesh_count =
		BlockEditPerformanceValidator::count_dirty_remeshes(world);
	frame.stale_results =
		BlockEditPerformanceValidator::counter_delta(
			previous_sample.stale_result_count, after.stale_result_count);
	frame.remesh_completed =
		BlockEditPerformanceValidator::counter_delta(
			previous_sample.remesh_completed_count, after.remesh_completed_count);
	frames.push_back(frame);
	previous_sample = after;
	return (FT_ERR_SUCCESS);
}

int32_t BlockEditPerformanceValidator::wait_for_revision(World &world,
	int32_t world_x, int32_t world_z, uint64_t previous_revision,
	World::StreamDiagnostics &previous_sample,
	std::vector<BlockEditWorkloadFrame> &frames,
	bool require_current_light) noexcept
{
	const auto deadline = std::chrono::steady_clock::now()
		+ std::chrono::seconds(30);
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);

	while (frames.size() < MAX_WORKLOAD_FRAMES
		&& std::chrono::steady_clock::now() < deadline)
	{
		const int32_t error_code = BlockEditPerformanceValidator::capture_frame(
			world, frames.size(), previous_sample, frames);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
		if (chunk != nullptr && chunk->mesh_revision > previous_revision
			&& chunk->last_mesh_publication_voxel_revision == chunk->voxel_revision
			&& (!require_current_light || chunk->light_is_current()))
			return (FT_ERR_SUCCESS);
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
	World::StreamDiagnostics &previous_sample,
	std::vector<BlockEditWorkloadFrame> &frames) noexcept
{
	const WorldChunk *chunk = world.find_chunk(
		WorldCoordinates::floor_divide(world_x, GAME_VOXEL_CHUNK_WIDTH),
		WorldCoordinates::floor_divide(world_z, GAME_VOXEL_CHUNK_DEPTH));
	const uint64_t previous_revision = chunk == nullptr ? 0U : chunk->mesh_revision;

	const int32_t error_code = world.place_block_at(world_x, world_y, world_z,
		VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (BlockEditPerformanceValidator::wait_for_revision(world,
		world_x, world_z, previous_revision, previous_sample, frames));
}

int32_t BlockEditPerformanceValidator::run_workload(World &world,
	int32_t world_y, uint64_t requested_edits,
	std::vector<BlockEditWorkloadFrame> &frames,
	std::vector<uint64_t> &latencies_us, uint64_t &completed_edits,
	uint64_t &failed_edits) noexcept
{
	uint64_t edit_index;
	const World::StreamDiagnostics initial_sample = world.stream_diagnostics();
	World::StreamDiagnostics previous_sample = initial_sample;

	completed_edits = 0U;
	failed_edits = 0U;
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
			EDIT_X, EDIT_Z, previous_revision, previous_sample, frames);
		if (error_code != FT_ERR_SUCCESS)
		{
			failed_edits += 1U;
			return (error_code);
		}
		const uint64_t latency_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - started).count());
		latencies_us.push_back(latency_us);
		completed_edits += 1U;
		error_code = BlockEditPerformanceValidator::restore_block(world,
			EDIT_X, world_y, EDIT_Z, previous_sample, frames);
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
	const int32_t error_code = BlockEditPerformanceValidator::wait_for_revision(
		world, EDIT_X, EDIT_Z, 0U, previous_sample, frames, true);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (workload_frame_sum(frames, &BlockEditWorkloadFrame::snapshot_bytes)
		!= previous_sample.remesh_snapshot_bytes - initial_sample.remesh_snapshot_bytes
		|| workload_frame_sum(frames, &BlockEditWorkloadFrame::remesh_completed)
			!= previous_sample.remesh_completed_count - initial_sample.remesh_completed_count)
		return (FT_ERR_INTERNAL);
	return (FT_ERR_SUCCESS);
}

int BlockEditPerformanceValidator::validate() const
{
	std::unique_ptr<World> world_storage(new (std::nothrow) World());
	if (world_storage == nullptr)
		return (ApplicationError::fail("block-edit-performance world allocation",
			FT_ERR_NO_MEMORY));
	World &world = *world_storage;
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
			World::StreamDiagnostics previous_sample = world.stream_diagnostics();
			error_code = BlockEditPerformanceValidator::wait_for_revision(world,
				EDIT_X, EDIT_Z, 0U, previous_sample, frames, true);
			frames.clear();
		}
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = BlockEditPerformanceValidator::wait_for_stream_to_settle(world);
	if (error_code == FT_ERR_SUCCESS)
		error_code = BlockEditPerformanceValidator::run_workload(world,
			world_y, DEFAULT_EDIT_COUNT, frames, latencies_us, completed_edits,
			failed_edits);
	if (error_code == FT_ERR_SUCCESS)
		error_code = BlockEditWorkloadAnalytics::write_report(REPORT_PATH,
			frames, latencies_us, DEFAULT_EDIT_COUNT, completed_edits,
			failed_edits);
	if (error_code == FT_ERR_SUCCESS
		&& !performance_bounds_are_valid(frames, latencies_us,
			completed_edits, failed_edits))
		error_code = FT_ERR_OUT_OF_RANGE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = measure_persisted_server_edits(world, world_y);
	world.destroy();
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit-performance", error_code));
	return (0);
}
