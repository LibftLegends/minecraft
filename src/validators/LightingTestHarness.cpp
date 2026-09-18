#include "../../src/validators/LightingTestHarness.hpp"
#include "../../src/chunks/WorldChunk.hpp"
#include "../../src/coordinates/WorldCoordinates.hpp"
#include "../../src/world/World.hpp"
#include "../../src/world/WorldChunkSnapshotCapture.hpp"
#include "../../src/world/WorldChunkSnapshotReader.hpp"
#include "../../src/world/WorldLightVersion.hpp"
#include "../../src/validators/IValidator.hpp"
#include "../../Libft/Modules/CMA/CMA.hpp"
#include "../../Libft/Modules/Voxel/voxel_api.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>

namespace
{
	static const uint64_t LIGHT_HARNESS_MAX_STARTUP_FRAMES = 2400U;
	static const uint64_t LIGHT_HARNESS_MAX_EDIT_FRAMES = 8U;
	static const uint64_t LIGHT_HARNESS_MAX_EDIT_MILLISECONDS = 200U;
	static const uint64_t LIGHT_HARNESS_P50_MAX_MILLISECONDS = 35U;
	static const uint64_t LIGHT_HARNESS_P95_MAX_MILLISECONDS = 75U;
	static const uint64_t LIGHT_HARNESS_P99_MAX_MILLISECONDS = 120U;
	static const std::size_t LIGHT_HARNESS_MINIMUM_STATISTICAL_SAMPLES =
		1000U;
	static const uint64_t LIGHT_HARNESS_MAX_INCREMENTAL_SCANNED_CELLS =
		static_cast<uint64_t>(GAME_VOXEL_CHUNK_WIDTH)
		* static_cast<uint64_t>(GAME_VOXEL_CHUNK_HEIGHT)
		* static_cast<uint64_t>(GAME_VOXEL_CHUNK_DEPTH);
	static const uint64_t LIGHT_HARNESS_WATCHDOG_MARKER_INTERVAL_SECONDS = 1U;
	static const uint64_t LIGHT_HARNESS_WATCHDOG_SECONDS = 120U;
	static const uint64_t LIGHT_HARNESS_REPORT_VERSION = 3U;
	#if defined(_WIN32)
	static const char *LIGHT_HARNESS_PLATFORM = "windows";
	#elif defined(__APPLE__)
	static const char *LIGHT_HARNESS_PLATFORM = "macos";
	#elif defined(__linux__)
	static const char *LIGHT_HARNESS_PLATFORM = "linux";
	#else
	static const char *LIGHT_HARNESS_PLATFORM = "unknown";
	#endif
	#if defined(LIBFT_ENABLE_ANALYTICS)
	static const char *LIGHT_HARNESS_WATCHDOG_REPORT =
		"minecraft_lighting_watchdog_analytics.json";
	#else
	static const char *LIGHT_HARNESS_WATCHDOG_REPORT =
		"minecraft_lighting_watchdog_normal.json";
	#endif

	class LightingHarnessWatchdog;
	static LightingHarnessWatchdog *g_watchdog = nullptr;

	static uint64_t watchdog_now_nanoseconds() noexcept
	{
		return (static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count()));
	}

	class LightingHarnessWatchdog
	{
	  private:
		std::atomic<bool> stop_requested_;
		std::atomic<uint64_t> last_progress_nanoseconds_;
		std::atomic<uint64_t> frame_;
		std::atomic<uint64_t> edit_;
		std::atomic<const char *> phase_;
		const char *marker_path_;
		std::thread thread_;

		int32_t write_marker(const char *type, const char *phase,
			uint64_t frame, uint64_t edit, uint64_t stalled_seconds) noexcept
		{
			std::FILE *file;

			if (marker_path_ == nullptr || type == nullptr)
				return (FT_ERR_INVALID_ARGUMENT);
			file = std::fopen(marker_path_, "w");
			if (file == nullptr)
				return (FT_ERR_FILE_OPEN_FAILED);
			if (std::fprintf(file,
					"{\"type\":\"%s\",\"phase\":\"%s\","
					"\"frame\":%llu,\"edit\":%llu,"
					"\"stalled_seconds\":%llu,\"watchdog_seconds\":%llu}\n",
					type, phase == nullptr ? "unknown" : phase,
					static_cast<unsigned long long>(frame),
					static_cast<unsigned long long>(edit),
					static_cast<unsigned long long>(stalled_seconds),
					static_cast<unsigned long long>(
						LIGHT_HARNESS_WATCHDOG_SECONDS)) < 0)
			{
				std::fclose(file);
				return (FT_ERR_IO);
			}
			if (std::fclose(file) != 0)
				return (FT_ERR_IO);
			return (FT_ERR_SUCCESS);
		}

		void run() noexcept
		{
			uint64_t last_marker_nanoseconds = 0U;

			while (!stop_requested_.load(std::memory_order_acquire))
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
				if (stop_requested_.load(std::memory_order_acquire))
					break ;
				const uint64_t now = watchdog_now_nanoseconds();
				const uint64_t last = last_progress_nanoseconds_.load(
					std::memory_order_acquire);
				const char *phase = phase_.load(std::memory_order_acquire);
				const uint64_t frame = frame_.load(std::memory_order_acquire);
				const uint64_t edit = edit_.load(std::memory_order_acquire);

				if (marker_path_ != nullptr
					&& (last_marker_nanoseconds == 0U
						|| now - last_marker_nanoseconds
							>= LIGHT_HARNESS_WATCHDOG_MARKER_INTERVAL_SECONDS
								* 1000000000ULL))
				{
					const int32_t marker_error = this->write_marker(
						"watchdog_progress", phase, frame, edit,
						now >= last ? (now - last) / 1000000000ULL : 0U);

					if (marker_error != FT_ERR_SUCCESS)
						std::fprintf(stderr,
							"lighting-harness: watchdog progress marker failed "
							"error=%d path=%s\n", marker_error, marker_path_);
					last_marker_nanoseconds = now;
				}

				if (now - last < LIGHT_HARNESS_WATCHDOG_SECONDS
					* 1000000000ULL)
					continue ;

				std::fprintf(stderr,
					"lighting-harness: watchdog timeout phase=%s frame=%llu "
					"edit=%llu stalled_seconds=%llu marker=%s\n",
					phase == nullptr ? "unknown" : phase,
					static_cast<unsigned long long>(frame),
					static_cast<unsigned long long>(edit),
					(now - last) / 1000000000ULL,
					marker_path_ == nullptr ? "<none>" : marker_path_);
				std::fflush(stderr);
				const int32_t marker_error = this->write_marker(
					"watchdog_timeout", phase, frame, edit,
					now >= last ? (now - last) / 1000000000ULL : 0U);

				if (marker_error != FT_ERR_SUCCESS)
					std::fprintf(stderr,
						"lighting-harness: watchdog timeout marker failed "
						"error=%d path=%s\n", marker_error, marker_path_);
				std::abort();
			}
		}

  public:
		LightingHarnessWatchdog(const char *marker_path) noexcept
			: stop_requested_(false),
			  last_progress_nanoseconds_(watchdog_now_nanoseconds()), frame_(0U),
			  edit_(0U), phase_("initializing"), marker_path_(marker_path),
			  thread_()
		{
		}

		~LightingHarnessWatchdog() noexcept
		{
			stop();
		}

		int32_t start() noexcept
		{
			try
			{
				thread_ = std::thread(&LightingHarnessWatchdog::run, this);
			}
			catch (...)
			{
				return (FT_ERR_THREAD_BUSY);
			}
			return (FT_ERR_SUCCESS);
		}

		void stop() noexcept
		{
			stop_requested_.store(true, std::memory_order_release);
			if (thread_.joinable())
				thread_.join();
		}

		void heartbeat(const char *phase, uint64_t frame,
			uint64_t edit) noexcept
		{
			phase_.store(phase, std::memory_order_release);
			frame_.store(frame, std::memory_order_release);
			edit_.store(edit, std::memory_order_release);
			last_progress_nanoseconds_.store(watchdog_now_nanoseconds(),
				std::memory_order_release);
		}
	};

	class LightingHarnessWatchdogScope
	{
	  private:
		LightingHarnessWatchdog watchdog_;
		bool active_;

	  public:
		LightingHarnessWatchdogScope(const char *marker_path) noexcept
			: watchdog_(marker_path), active_(false)
		{
		}

		~LightingHarnessWatchdogScope() noexcept
		{
			if (g_watchdog == &this->watchdog_)
				g_watchdog = nullptr;
		}

		int32_t start() noexcept;
		void heartbeat(const char *phase, uint64_t frame,
			uint64_t edit) noexcept;
	};

	int32_t LightingHarnessWatchdogScope::start() noexcept
	{
		const int32_t error_code = this->watchdog_.start();

		if (error_code == FT_ERR_SUCCESS)
		{
			g_watchdog = &this->watchdog_;
			this->active_ = true;
		}
		return (error_code);
	}

	void LightingHarnessWatchdogScope::heartbeat(const char *phase,
		uint64_t frame, uint64_t edit) noexcept
	{
		if (this->active_)
			this->watchdog_.heartbeat(phase, frame, edit);
	}

	static void lighting_watchdog_heartbeat(const char *phase,
		uint64_t frame, uint64_t edit) noexcept
	{
		if (g_watchdog != nullptr)
			g_watchdog->heartbeat(phase, frame, edit);
	}
	#if defined(__clang__)
	static const char *LIGHT_HARNESS_COMPILER = "clang";
	#elif defined(__GNUC__)
	static const char *LIGHT_HARNESS_COMPILER = "gcc";
	#elif defined(_MSC_VER)
	static const char *LIGHT_HARNESS_COMPILER = "msvc";
	#else
	static const char *LIGHT_HARNESS_COMPILER = "unknown";
	#endif
	static const uint32_t LIGHT_HARNESS_STONE = VOXEL_GENERATOR_STONE_BLOCK;
	#if defined(LIBFT_ENABLE_ANALYTICS)
	static const char *LIGHT_HARNESS_BUILD_VARIANT = "analytics";
	static const char *LIGHT_HARNESS_STARTUP_REPORT =
		"minecraft_lighting_harness_analytics.jsonl";
	static const char *LIGHT_HARNESS_STARTUP_FAILURE_REPORT =
		"minecraft_lighting_harness_failure_analytics.jsonl";
	static const char *LIGHT_HARNESS_EDIT_REPORT =
		"minecraft_lighting_edit_matrix_analytics.jsonl";
	static const char *LIGHT_HARNESS_FAILURE_REPORT =
		"minecraft_lighting_edit_matrix_failure_analytics.jsonl";
	#else
	static const char *LIGHT_HARNESS_BUILD_VARIANT = "normal";
	static const char *LIGHT_HARNESS_STARTUP_REPORT =
		"minecraft_lighting_harness_normal.jsonl";
	static const char *LIGHT_HARNESS_STARTUP_FAILURE_REPORT =
		"minecraft_lighting_harness_failure_normal.jsonl";
	static const char *LIGHT_HARNESS_EDIT_REPORT =
		"minecraft_lighting_edit_matrix_normal.jsonl";
	static const char *LIGHT_HARNESS_FAILURE_REPORT =
		"minecraft_lighting_edit_matrix_failure_normal.jsonl";
	#endif

	static uint64_t elapsed_milliseconds(
		const std::chrono::steady_clock::time_point &started) noexcept
	{
		return (static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - started).count()));
	}

	static std::size_t percentile_index(std::size_t sample_count,
		std::size_t percentile) noexcept
	{
		return ((sample_count * percentile + 99U) / 100U) - 1U;
	}

	static uint64_t percentile_value(const std::vector<uint64_t> &values,
		std::size_t percentile) noexcept
	{
		if (values.empty())
			return (0U);
		return (values[percentile_index(values.size(), percentile)]);
	}

	static void report_stage_latency(const char *name,
		std::vector<uint64_t> &values) noexcept
	{
		if (name == nullptr)
			return ;
		if (values.empty())
		{
			std::fprintf(stderr,
				"lighting-harness: latency stage=%s samples=0\n", name);
			return ;
		}
		std::sort(values.begin(), values.end());
		std::fprintf(stderr,
			"lighting-harness: latency stage=%s samples=%zu p50_ms=%llu "
			"p95_ms=%llu p99_ms=%llu max_ms=%llu\n", name, values.size(),
			static_cast<unsigned long long>(values[percentile_index(
				values.size(), 50U)]),
			static_cast<unsigned long long>(values[percentile_index(
				values.size(), 95U)]),
			static_cast<unsigned long long>(values[percentile_index(
				values.size(), 99U)]),
			static_cast<unsigned long long>(values.back()));
	}

	struct ReferenceLightNode
	{
		int16_t x;
		int16_t y;
		int16_t z;
		uint8_t channel;
	};

	static std::size_t reference_light_index(int32_t x, int32_t y,
		int32_t z) noexcept
	{
		static const int32_t halo = 1;
		static const int32_t edge = 18;

		return ((static_cast<std::size_t>(z + halo)
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
			+ static_cast<std::size_t>(y))
			* static_cast<std::size_t>(edge))
			+ static_cast<std::size_t>(x + halo);
	}

	static std::size_t reference_output_index(int32_t x, int32_t y,
		int32_t z) noexcept
	{
		return ((static_cast<std::size_t>(z)
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
			+ static_cast<std::size_t>(y))
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH))
			+ static_cast<std::size_t>(x);
	}

	/*
	 * This is deliberately a separate implementation from voxel_light_*.
	 * It consumes only the immutable snapshot and implements the reference
	 * rules directly: direct sky seeding, emissive sources, and monotonic
	 * six-neighbour propagation.  Keeping this code test-local prevents a
	 * production solver regression from becoming its own oracle.
	 */
	static int32_t build_reference_light(
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		std::vector<uint8_t> &packed) noexcept
	{
		static const int32_t region_min = -1;
		static const int32_t region_max = 17;
		static const int8_t delta_x[6] = {1, -1, 0, 0, 0, 0};
		static const int8_t delta_y[6] = {0, 0, 1, -1, 0, 0};
		static const int8_t delta_z[6] = {0, 0, 0, 0, 1, -1};
		const std::size_t grid_size = static_cast<std::size_t>(18U)
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
			* static_cast<std::size_t>(18U);
		std::vector<uint8_t> sky;
		std::vector<uint8_t> block;
		std::vector<uint8_t> sky_queued;
		std::vector<uint8_t> block_queued;
		std::vector<uint32_t> block_ids;
		std::vector<ReferenceLightNode> queue;
		int32_t error_code;

		try
		{
			sky.assign(grid_size, 0U);
			block.assign(grid_size, 0U);
			sky_queued.assign(grid_size, 0U);
			block_queued.assign(grid_size, 0U);
			block_ids.assign(grid_size, GAME_VOXEL_AIR_BLOCK);
			queue.reserve(65536U);
			packed.assign(static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_DEPTH), 0U);
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}

		for (int32_t z = region_min; z < region_max; ++z)
		{
			for (int32_t x = region_min; x < region_max; ++x)
			{
				bool direct_sky = true;

				for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1; y >= 0; --y)
				{
					uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
					const std::size_t index = reference_light_index(x, y, z);

					error_code = WorldChunkSnapshotReader::lookup_snapshot_block(
						const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(
							&snapshot), snapshot.chunk_x * GAME_VOXEL_CHUNK_WIDTH + x,
						y, snapshot.chunk_z * GAME_VOXEL_CHUNK_DEPTH + z,
						&block_id);
					if (error_code != FT_ERR_SUCCESS)
						return (error_code);
					block_ids[index] = block_id;
					const voxel_block_metadata &actual_metadata =
						voxel_get_block_metadata(block_id);
					if (direct_sky
						&& actual_metadata.light_attenuation < 15U
						&& actual_metadata.transparent != FT_FALSE)
					{
						sky[index] = 15U;
						if (sky_queued[index] == 0U)
						{
							try
							{
								queue.push_back({static_cast<int16_t>(x),
									static_cast<int16_t>(y), static_cast<int16_t>(z),
									0U});
							}
							catch (const std::bad_alloc &)
							{
								return (FT_ERR_NO_MEMORY);
							}
							sky_queued[index] = 1U;
						}
					}
					else
						direct_sky = false;
					const uint8_t emission =
						voxel_block_emitted_light_level(block_id);

					if (emission > block[index])
					{
						block[index] = emission;
						if (block_queued[index] == 0U)
						{
							try
							{
								queue.push_back({static_cast<int16_t>(x),
									static_cast<int16_t>(y), static_cast<int16_t>(z),
									1U});
							}
							catch (const std::bad_alloc &)
							{
								return (FT_ERR_NO_MEMORY);
							}
							block_queued[index] = 1U;
						}
					}
				}
			}
		}

		while (!queue.empty())
		{
			const ReferenceLightNode node = queue.back();
			const std::size_t node_index = reference_light_index(node.x, node.y,
				node.z);
			const uint8_t level = node.channel == 0U
				? sky[node_index] : block[node_index];

			queue.pop_back();
			if (node.channel == 0U)
				sky_queued[node_index] = 0U;
			else
				block_queued[node_index] = 0U;
			for (uint8_t direction = 0U; direction < 6U; ++direction)
			{
				const int32_t x = static_cast<int32_t>(node.x)
					+ delta_x[direction];
				const int32_t y = static_cast<int32_t>(node.y)
					+ delta_y[direction];
				const int32_t z = static_cast<int32_t>(node.z)
					+ delta_z[direction];
				std::size_t index;
				const voxel_block_metadata *metadata;
				uint8_t cost;
				uint8_t candidate;
				uint8_t *destination;
				uint8_t *queued;

				if (x < region_min || x >= region_max || y < 0
					|| y >= GAME_VOXEL_CHUNK_HEIGHT || z < region_min
					|| z >= region_max)
					continue;
				index = reference_light_index(x, y, z);
				metadata = &voxel_get_block_metadata(block_ids[index]);
				cost = metadata->light_attenuation > 1U
					? metadata->light_attenuation : 1U;
				candidate = level > cost
					? static_cast<uint8_t>(level - cost) : 0U;
				if (metadata->light_attenuation >= 15U
					|| metadata->occludes_faces != FT_FALSE || level <= cost)
					continue;
				destination = node.channel == 0U ? &sky[index] : &block[index];
				queued = node.channel == 0U ? &sky_queued[index]
					: &block_queued[index];
				if (candidate <= *destination)
					continue;
				*destination = candidate;
				if (*queued == 0U)
				{
					try
					{
						queue.push_back({static_cast<int16_t>(x),
							static_cast<int16_t>(y), static_cast<int16_t>(z),
							node.channel});
					}
					catch (const std::bad_alloc &)
					{
						return (FT_ERR_NO_MEMORY);
					}
					*queued = 1U;
				}
			}
		}

		for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
			for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
				for (int32_t x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
					packed[reference_output_index(x, y, z)] = voxel_light_pack(
						sky[reference_light_index(x, y, z)],
						block[reference_light_index(x, y, z)]);
		return (FT_ERR_SUCCESS);
	}

	static int32_t validate_reference_oracle_fixtures() noexcept
	{
		WorldGenerationPipeline::WorldChunkSnapshot snapshot;
		std::vector<uint8_t> expected;
		const std::size_t chunk_cell_count = static_cast<std::size_t>(
			GAME_VOXEL_CHUNK_WIDTH) * static_cast<std::size_t>(
			GAME_VOXEL_CHUNK_HEIGHT) * static_cast<std::size_t>(
			GAME_VOXEL_CHUNK_DEPTH);
		int32_t error_code;

		try
		{
			snapshot.chunk_x = 0;
			snapshot.chunk_z = 0;
			snapshot.blocks.assign(chunk_cell_count, GAME_VOXEL_AIR_BLOCK);
			snapshot.west_border.assign(
				static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
					* static_cast<std::size_t>(GAME_VOXEL_CHUNK_DEPTH),
				GAME_VOXEL_AIR_BLOCK);
			snapshot.east_border = snapshot.west_border;
			snapshot.north_border.assign(
				static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
					* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH),
				GAME_VOXEL_AIR_BLOCK);
			snapshot.south_border = snapshot.north_border;
			snapshot.west_border_valid = FT_TRUE;
			snapshot.east_border_valid = FT_TRUE;
			snapshot.north_border_valid = FT_TRUE;
			snapshot.south_border_valid = FT_TRUE;
			const std::size_t ring_edge = static_cast<std::size_t>(
				GAME_VOXEL_CHUNK_WIDTH
					+ WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO * 2);
			uint32_t ring_column = 0U;

			snapshot.lighting_ring_offsets.assign(ring_edge * ring_edge,
				UINT32_MAX);
			for (std::size_t ring_z = 0U; ring_z < ring_edge; ++ring_z)
				for (std::size_t ring_x = 0U; ring_x < ring_edge; ++ring_x)
					if (ring_x < static_cast<std::size_t>(
							WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO)
						|| ring_z < static_cast<std::size_t>(
							WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO)
						|| ring_x >= static_cast<std::size_t>(
							GAME_VOXEL_CHUNK_WIDTH
								+ WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO)
						|| ring_z >= static_cast<std::size_t>(
							GAME_VOXEL_CHUNK_DEPTH
								+ WorldGenerationPipeline::LIGHT_SNAPSHOT_HALO))
						snapshot.lighting_ring_offsets[ring_z * ring_edge + ring_x]
							= ring_column++ * static_cast<uint32_t>(
								GAME_VOXEL_CHUNK_HEIGHT);
			snapshot.lighting_blocks.assign(static_cast<std::size_t>(ring_column)
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT),
				GAME_VOXEL_AIR_BLOCK);
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		error_code = build_reference_light(snapshot, expected);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
			for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
				for (int32_t x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
					if (voxel_light_sky(expected[reference_output_index(x, y, z)])
						!= 15U
						|| voxel_light_block(expected[reference_output_index(x, y, z)])
						!= 0U)
						return (FT_ERR_INTERNAL);

		snapshot.blocks[reference_output_index(8, 64, 8)] =
			LIGHT_HARNESS_STONE;
		error_code = build_reference_light(snapshot, expected);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (voxel_light_sky(expected[reference_output_index(8, 64, 8)]) != 0U
			|| voxel_light_sky(expected[reference_output_index(8, 65, 8)]) != 15U
			|| voxel_light_sky(expected[reference_output_index(8, 63, 8)]) == 0U
			|| voxel_light_sky(expected[reference_output_index(8, 63, 8)]) >= 15U)
			return (FT_ERR_INTERNAL);
		return (FT_ERR_SUCCESS);
	}

	static int32_t wait_for_startup(World &world, uint64_t &frame) noexcept
	{
		while (frame < LIGHT_HARNESS_MAX_STARTUP_FRAMES)
		{
			const WorldChunk *chunk;
			int32_t error_code;

			lighting_watchdog_heartbeat("startup_update", frame, 0U);
			error_code = world.update_around(0.0, 0.0, 0,
				WorldCoordinates::MIN_RENDER_DISTANCE);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			chunk = world.find_chunk(0, 0);
			if (chunk != nullptr && chunk->initialized
				&& chunk->light_buffer_is_valid()
				&& chunk->light_is_current()
				&& chunk->mesh_revision != 0U)
				return (FT_ERR_SUCCESS);
			frame += 1U;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return (FT_ERR_TIMEOUT);
	}

	static bool chunk_is_ready_for_oracle(const WorldChunk *chunk) noexcept
	{
		return (chunk != nullptr && chunk->initialized
			&& chunk->light_buffer_is_valid() && chunk->light_is_current()
			&& chunk->mesh_revision != 0U && !chunk->mesh_dirty
			&& chunk->pending_mesh_request_id == 0U);
	}

	static int32_t wait_for_startup_neighborhood(World &world,
		uint64_t &frame) noexcept
	{
		static const int32_t offsets[8][2] = {
			{-1, 0}, {1, 0}, {0, -1}, {0, 1},
			{-1, -1}, {-1, 1}, {1, -1}, {1, 1}
		};
		while (frame < LIGHT_HARNESS_MAX_STARTUP_FRAMES)
		{
			lighting_watchdog_heartbeat("startup_neighborhood", frame, 0U);
			bool ready = chunk_is_ready_for_oracle(world.find_chunk(0, 0));
			int32_t index = 0;

			while (ready == true && index < 8)
			{
				ready = chunk_is_ready_for_oracle(world.find_chunk(
					offsets[index][0], offsets[index][1]));
				index += 1;
			}
			if (ready == true)
				return (FT_ERR_SUCCESS);
			int32_t error_code = world.update_around(0.0, 0.0, 0,
				WorldCoordinates::MIN_RENDER_DISTANCE);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			frame += 1U;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return (FT_ERR_TIMEOUT);
	}

	static int32_t compare_light_to_oracle(World &world,
		int32_t chunk_x, int32_t chunk_z, const char *context) noexcept
	{
		WorldGenerationPipeline::WorldChunkSnapshot snapshot;
		std::vector<uint8_t> expected;
		const WorldChunk *chunk;
		int32_t error_code;
		int32_t local_x;
		int32_t local_y;
		int32_t local_z;

		chunk = world.find_chunk(chunk_x, chunk_z);
		if (chunk == nullptr || !chunk->initialized
			|| !chunk->light_buffer_is_valid() || !chunk->light_is_current())
		{
			std::fprintf(stderr,
				"lighting-harness: oracle source unavailable context=%s "
				"chunk=(%d,%d)\n", context, chunk_x, chunk_z);
			return (FT_ERR_INVALID_STATE);
		}
		error_code = world.capture_remesh_snapshot(chunk_x, chunk_z, snapshot);
		if (error_code != FT_ERR_SUCCESS)
		{
			std::fprintf(stderr,
				"lighting-harness: snapshot failed context=%s chunk=(%d,%d) "
				"error=%d\n", context, chunk_x, chunk_z, error_code);
			return (error_code);
		}
		error_code = build_reference_light(snapshot, expected);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		local_z = 0;
		while (local_z < GAME_VOXEL_CHUNK_DEPTH)
		{
			local_y = 0;
			while (local_y < GAME_VOXEL_CHUNK_HEIGHT)
			{
				local_x = 0;
				while (local_x < GAME_VOXEL_CHUNK_WIDTH)
				{
					const uint8_t expected_value = expected[reference_output_index(
						local_x, local_y, local_z)];
					const uint8_t actual_value = chunk->light.get(local_x,
						local_y, local_z);

					if (expected_value != actual_value)
					{
						uint32_t snapshot_block_id = GAME_VOXEL_AIR_BLOCK;
						int32_t first_opaque_y = -1;
						int32_t scan_y = local_y;
						int32_t snapshot_lookup_error;

						snapshot_lookup_error =
							WorldChunkSnapshotReader::lookup_snapshot_block(
							&snapshot, chunk_x * GAME_VOXEL_CHUNK_WIDTH + local_x,
							local_y, chunk_z * GAME_VOXEL_CHUNK_DEPTH + local_z,
							&snapshot_block_id);
						while (scan_y < GAME_VOXEL_CHUNK_HEIGHT)
						{
							uint32_t scan_block_id = GAME_VOXEL_AIR_BLOCK;

							if (WorldChunkSnapshotReader::lookup_snapshot_block(
								&snapshot, chunk_x * GAME_VOXEL_CHUNK_WIDTH + local_x,
								scan_y, chunk_z * GAME_VOXEL_CHUNK_DEPTH + local_z,
								&scan_block_id) != FT_ERR_SUCCESS)
								break;
							if (voxel_get_block_metadata(scan_block_id).light_attenuation
								>= 15U || voxel_get_block_metadata(scan_block_id).transparent
								== FT_FALSE)
							{
								first_opaque_y = scan_y;
								break;
							}
							scan_y += 1;
						}
						std::fprintf(stderr,
							"lighting-harness: oracle mismatch context=%s "
							"chunk=(%d,%d) local=(%d,%d,%d) expected=%u "
							"actual=%u voxel=%llu light=%llu content=%u "
							"input=%u computed=%u "
							"snapshot_block=%u snapshot_error=%d first_opaque_y=%d\n",
							context, chunk_x, chunk_z,
							local_x, local_y, local_z,
							static_cast<unsigned int>(expected_value),
							static_cast<unsigned int>(actual_value),
							static_cast<unsigned long long>(chunk->voxel_revision),
							static_cast<unsigned long long>(chunk->light_revision),
							static_cast<unsigned int>(chunk->content_version),
							static_cast<unsigned int>(chunk->light_input_version),
							static_cast<unsigned int>(
								chunk->computed_light_input_version),
							static_cast<unsigned int>(snapshot_block_id),
							snapshot_lookup_error,
							first_opaque_y);
						return (FT_ERR_INTERNAL);
					}
					local_x += 1;
				}
				local_y += 1;
			}
			local_z += 1;
		}
		return (FT_ERR_SUCCESS);
	}

	static bool light_neighborhood_is_current(World &world, int32_t chunk_x,
		int32_t chunk_z) noexcept
	{
		int32_t offset_z = -1;

		while (offset_z <= 1)
		{
			int32_t offset_x = -1;

			while (offset_x <= 1)
			{
				const WorldChunk *chunk = world.find_chunk(chunk_x + offset_x,
					chunk_z + offset_z);

				if (chunk == nullptr || !chunk->initialized
					|| !chunk->light_buffer_is_valid()
					|| !chunk->light_is_current())
					return (false);
				offset_x += 1;
			}
			offset_z += 1;
		}
		return (true);
	}

	static int32_t compare_light_neighborhood_to_oracle(World &world,
		int32_t chunk_x, int32_t chunk_z, const char *context) noexcept
	{
		int32_t offset_z = -1;

		if (!light_neighborhood_is_current(world, chunk_x, chunk_z))
			return (FT_ERR_INVALID_STATE);
		while (offset_z <= 1)
		{
			int32_t offset_x = -1;

			while (offset_x <= 1)
			{
				const int32_t error_code = compare_light_to_oracle(world,
					chunk_x + offset_x, chunk_z + offset_z, context);

				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				offset_x += 1;
			}
			offset_z += 1;
		}
		return (FT_ERR_SUCCESS);
	}

	static int32_t observe_edit_neighborhood(World &world,
		LightingObservationHarness &harness, int32_t chunk_x, int32_t chunk_z,
		uint64_t frame, uint64_t edit_id) noexcept
	{
		static const int32_t offsets[9][2] = {
			{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1},
			{-1, -1}, {-1, 1}, {1, -1}, {1, 1}
		};
		int32_t index = 0;

		while (index < 9)
		{
			const int32_t observed_chunk_x = chunk_x + offsets[index][0];
			const int32_t observed_chunk_z = chunk_z + offsets[index][1];
			const WorldChunk *chunk = world.find_chunk(observed_chunk_x,
				observed_chunk_z);

			if (chunk == nullptr || !chunk->initialized)
			{
				std::fprintf(stderr,
					"lighting-harness: neighborhood observation missing "
					"edit=%llu chunk=(%d,%d) target=(%d,%d)\n",
					static_cast<unsigned long long>(edit_id), observed_chunk_x,
					observed_chunk_z, chunk_x, chunk_z);
				return (FT_ERR_NOT_FOUND);
			}
			if (harness.observe(world, observed_chunk_x, observed_chunk_z,
					frame, edit_id) != FT_ERR_SUCCESS)
				return (FT_ERR_INTERNAL);
			index += 1;
		}
		return (FT_ERR_SUCCESS);
	}

	static int32_t observe_opposite_border_neighborhood(World &world,
		LightingObservationHarness &harness, uint64_t frame,
		uint64_t west_edit_id, uint64_t east_edit_id) noexcept
	{
		static const int32_t offsets[6][2] = {
			{0, 0}, {1, 0}, {0, -1}, {0, 1}, {1, -1}, {1, 1}
		};
		int32_t index = 0;

		while (index < 6)
		{
			const int32_t chunk_x = offsets[index][0];
			const int32_t chunk_z = offsets[index][1];
			const uint64_t edit_id = chunk_x == 0 ? west_edit_id : east_edit_id;
			const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);

			if (chunk == nullptr || !chunk->initialized)
			{
				std::fprintf(stderr,
					"lighting-harness: opposite-border observation missing "
					"edit=%llu chunk=(%d,%d)\n",
					static_cast<unsigned long long>(edit_id), chunk_x, chunk_z);
				return (FT_ERR_NOT_FOUND);
			}
			if (harness.observe(world, chunk_x, chunk_z, frame, edit_id)
				!= FT_ERR_SUCCESS)
				return (FT_ERR_INTERNAL);
			index += 1;
		}
		return (FT_ERR_SUCCESS);
	}

	static int32_t run_edit(World &world, LightingObservationHarness &harness,
		uint64_t edit_id, int32_t world_x, int32_t world_y,
		int32_t world_z, uint32_t block_id, uint64_t &frame) noexcept
	{
		const uint32_t expected_block_id = block_id;
		const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
			GAME_VOXEL_CHUNK_WIDTH);
		const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
			GAME_VOXEL_CHUNK_DEPTH);
		const std::chrono::steady_clock::time_point started =
			std::chrono::steady_clock::now();
		const WorldChunk *before_chunk = world.find_chunk(chunk_x, chunk_z);
		const uint64_t before_mesh = before_chunk == nullptr ? 0U
			: before_chunk->mesh_revision;
		const uint64_t before_voxel = before_chunk == nullptr ? 0U
			: before_chunk->voxel_revision;
		const World::StreamDiagnostics before_diagnostics =
			world.stream_diagnostics();
		uint64_t edit_frame = frame;
		uint64_t elapsed;
		int32_t error_code;

		if (before_chunk == nullptr || !before_chunk->initialized)
			return (FT_ERR_NOT_FOUND);
		error_code = harness.begin_edit(edit_id, frame, world_x, world_y,
			world_z, before_diagnostics.remesh_scanned_cells,
			before_diagnostics.remesh_incremental_completed_count,
			before_diagnostics.remesh_full_completed_count);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (block_id == GAME_VOXEL_AIR_BLOCK)
			error_code = world.delete_block_at(world_x, world_y, world_z);
		else
			error_code = world.place_block_at(world_x, world_y, world_z, block_id);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (world.block_id_at(world_x, world_y, world_z, &block_id) == false
			|| block_id != expected_block_id)
			return (FT_ERR_INTERNAL);
		error_code = harness.mark_authoritative(edit_id, frame,
			elapsed_milliseconds(started));
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		while (frame - edit_frame <= LIGHT_HARNESS_MAX_EDIT_FRAMES)
		{
			const WorldChunk *chunk;

			lighting_watchdog_heartbeat("edit_update", frame, edit_id);
			error_code = world.update_around(0.0, 0.0, 0,
				WorldCoordinates::MIN_RENDER_DISTANCE);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			chunk = world.find_chunk(chunk_x, chunk_z);
			if (chunk == nullptr || !chunk->initialized)
				return (FT_ERR_NOT_FOUND);
			error_code = observe_edit_neighborhood(world, harness, chunk_x,
				chunk_z, frame, edit_id);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			if (chunk->light_buffer_is_valid() && chunk->light_is_current()
				&& light_neighborhood_is_current(world, chunk_x, chunk_z))
			{
				error_code = harness.mark_light_ready(edit_id, frame,
					elapsed_milliseconds(started));
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
			}
			if (chunk->mesh_revision > before_mesh
				&& chunk->light_buffer_is_valid() && chunk->light_is_current()
				&& chunk->voxel_revision >= before_voxel)
			{
				const World::StreamDiagnostics after_diagnostics =
					world.stream_diagnostics();
				elapsed = elapsed_milliseconds(started);
				error_code = harness.mark_mesh_ready(edit_id, frame, elapsed,
					after_diagnostics.remesh_scanned_cells,
					after_diagnostics.remesh_incremental_completed_count,
					after_diagnostics.remesh_full_completed_count,
					chunk->last_light_remesh_incremental != FT_FALSE);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				return (compare_light_neighborhood_to_oracle(world, chunk_x,
					chunk_z, "edit"));
			}
			frame += 1U;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		const WorldChunk *last_chunk = world.find_chunk(chunk_x, chunk_z);
		const World::StreamDiagnostics timeout_diagnostics =
			world.stream_diagnostics();
		if (last_chunk == nullptr || !last_chunk->initialized)
			return (FT_ERR_NOT_FOUND);
		std::fprintf(stderr,
			"lighting-harness: edit timeout id=%llu coordinate=(%d,%d,%d) "
			"frames=" FT_UINT64_DECIMAL_FORMAT " elapsed_ms="
			FT_UINT64_DECIMAL_FORMAT " content=%u light_version=%u "
			"light_input=%u computed_input=%u light_revision="
			FT_UINT64_DECIMAL_FORMAT " mesh_revision=" FT_UINT64_DECIMAL_FORMAT
			" dirty=%d pending=%llu pending_voxel=" FT_UINT64_DECIMAL_FORMAT
			" pending_content=%u pending_input=%u ready=%d valid=%d current=%d "
			"queue=%zu interactive=%zu active=%zu playable=%zu/%zu/%zu "
			"deferred=%zu cursor=%zu candidates=%zu ready_candidates=%zu "
			"pending_candidates=%zu retryable=%zu failed_candidates=%zu "
			"stream_frame=%llu progress_frame=%llu oldest_pending=%llu "
			"oldest_result_ns=%llu starvation=%llu geometry_only=%llu "
			"scanned=%llu propagated=%llu last_error=%d\n",
			static_cast<unsigned long long>(edit_id), world_x, world_y, world_z,
			frame - edit_frame, elapsed_milliseconds(started),
			static_cast<unsigned int>(last_chunk->content_version),
			static_cast<unsigned int>(last_chunk->light_version),
			static_cast<unsigned int>(last_chunk->light_input_version),
			static_cast<unsigned int>(last_chunk->computed_light_input_version),
			static_cast<unsigned long long>(last_chunk->light_revision),
			static_cast<unsigned long long>(last_chunk->mesh_revision),
			last_chunk->mesh_dirty ? 1 : 0,
			static_cast<unsigned long long>(last_chunk->pending_mesh_request_id),
			static_cast<unsigned long long>(
				last_chunk->pending_mesh_request_voxel_revision),
			static_cast<unsigned int>(
				last_chunk->pending_mesh_request_content_version),
			static_cast<unsigned int>(
				last_chunk->pending_mesh_request_light_input_version),
			last_chunk->light_ready_for_render ? 1 : 0,
			last_chunk->light_buffer_is_valid() ? 1 : 0,
			last_chunk->light_is_current() ? 1 : 0,
			timeout_diagnostics.remesh_priority_queue_depth,
			timeout_diagnostics.interactive_remesh_queue_depth,
			timeout_diagnostics.active_generation_count,
			timeout_diagnostics.playable_failed_count,
			timeout_diagnostics.playable_required_count,
			timeout_diagnostics.playable_drawable_count,
			timeout_diagnostics.deferred_edit_count,
			timeout_diagnostics.deferred_edit_cursor,
			timeout_diagnostics.candidate_count,
			timeout_diagnostics.ready_count,
			timeout_diagnostics.pending_count,
			timeout_diagnostics.retryable_count,
			timeout_diagnostics.failed_count,
			static_cast<unsigned long long>(timeout_diagnostics.frame),
			static_cast<unsigned long long>(timeout_diagnostics.progress_frame),
			static_cast<unsigned long long>(
				timeout_diagnostics.oldest_pending_age),
			static_cast<unsigned long long>(
				timeout_diagnostics.oldest_result_age_nanoseconds),
			static_cast<unsigned long long>(
				timeout_diagnostics.remesh_starvation_promotions),
			static_cast<unsigned long long>(
				timeout_diagnostics.remesh_geometry_only_count),
			static_cast<unsigned long long>(
				timeout_diagnostics.remesh_scanned_cells),
			static_cast<unsigned long long>(
				timeout_diagnostics.remesh_propagated_cells),
			timeout_diagnostics.last_error);
		return (FT_ERR_TIMEOUT);
	}

	static int32_t run_transition(World &world,
		LightingObservationHarness &harness, uint64_t &edit_id,
		int32_t world_x, int32_t world_z, uint32_t block_id,
		uint64_t &frame, const char *name) noexcept
	;

	static int32_t wait_for_neighbor_oracle(World &world,
		LightingObservationHarness &harness, int32_t chunk_x, int32_t chunk_z,
		uint64_t &frame, const char *context) noexcept;

	static int32_t verify_transition_neighbors(World &world,
		LightingObservationHarness &harness, int32_t world_x, int32_t world_z,
		uint64_t &frame) noexcept
	{
		const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
			GAME_VOXEL_CHUNK_WIDTH);
		const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
			GAME_VOXEL_CHUNK_DEPTH);
		const int32_t local_x = world_x - chunk_x * GAME_VOXEL_CHUNK_WIDTH;
		const int32_t local_z = world_z - chunk_z * GAME_VOXEL_CHUNK_DEPTH;
		int32_t error_code = FT_ERR_SUCCESS;
		bool west_or_east = local_x == 0
			|| local_x == GAME_VOXEL_CHUNK_WIDTH - 1;
		bool north_or_south = local_z == 0
			|| local_z == GAME_VOXEL_CHUNK_DEPTH - 1;

		if (west_or_east)
		{
			const int32_t neighbor_x = local_x == 0 ? chunk_x - 1
				: chunk_x + 1;
			error_code = wait_for_neighbor_oracle(world, harness, neighbor_x,
				chunk_z, frame, "transition face neighbor");
		}
		if (error_code == FT_ERR_SUCCESS && north_or_south)
		{
			const int32_t neighbor_z = local_z == 0 ? chunk_z - 1
				: chunk_z + 1;
			error_code = wait_for_neighbor_oracle(world, harness, chunk_x,
				neighbor_z, frame, "transition depth neighbor");
		}
		if (error_code == FT_ERR_SUCCESS && west_or_east && north_or_south)
		{
			const int32_t neighbor_x = local_x == 0 ? chunk_x - 1
				: chunk_x + 1;
			const int32_t neighbor_z = local_z == 0 ? chunk_z - 1
				: chunk_z + 1;
			error_code = wait_for_neighbor_oracle(world, harness, neighbor_x,
				neighbor_z, frame, "transition diagonal neighbor");
		}
		return (error_code);
	}

	static int32_t run_transition(World &world,
		LightingObservationHarness &harness, uint64_t &edit_id,
		int32_t world_x, int32_t world_z, uint32_t block_id,
		uint64_t &frame, const char *name) noexcept
	{
		int32_t candidate_x = world_x;
		int32_t candidate_z = world_z;
		double surface;
		int32_t world_y = 0;
		uint32_t existing_block_id = GAME_VOXEL_AIR_BLOCK;
		int32_t error_code;
		int32_t offset = 0;
		bool found = false;

		while (offset < GAME_VOXEL_CHUNK_WIDTH && found == false)
		{
			candidate_x = world_x;
			candidate_z = world_z;
			if (world_x == 0 || world_x == GAME_VOXEL_CHUNK_WIDTH - 1)
				candidate_z = (world_z + offset) % GAME_VOXEL_CHUNK_DEPTH;
			else if (world_z == 0 || world_z == GAME_VOXEL_CHUNK_DEPTH - 1)
				candidate_x = (world_x + offset) % GAME_VOXEL_CHUNK_WIDTH;
			else
			{
				candidate_x = 1 + ((world_x + offset) %
					(GAME_VOXEL_CHUNK_WIDTH - 2));
				candidate_z = 1 + ((world_z + offset) %
					(GAME_VOXEL_CHUNK_DEPTH - 2));
			}
			if (world.surface_top_at(candidate_x, candidate_z, &surface))
			{
				world_y = static_cast<int32_t>(surface + 1.0);
				if (world.block_id_at(candidate_x, world_y, candidate_z,
					&existing_block_id)
					&& existing_block_id == GAME_VOXEL_AIR_BLOCK)
					found = true;
			}
			offset += 1;
		}
		if (found == false)
			return (FT_ERR_NOT_FOUND);
		std::fprintf(stderr,
			"lighting-harness: transition begin name=%s coordinate=(%d,%d,%d) "
			"block=%u\n", name, candidate_x, world_y, candidate_z,
			static_cast<unsigned int>(block_id));
		error_code = run_edit(world, harness, edit_id++, candidate_x, world_y,
			candidate_z, block_id, frame);
		if (error_code == FT_ERR_SUCCESS)
			error_code = run_edit(world, harness, edit_id++, candidate_x, world_y,
				candidate_z, GAME_VOXEL_AIR_BLOCK, frame);
		if (error_code == FT_ERR_SUCCESS)
			error_code = verify_transition_neighbors(world, harness, candidate_x,
			candidate_z, frame);
		return (error_code);
	}

	static int32_t register_harness_emissive_block(uint32_t *block_id,
		const char *name, uint8_t emitted_light_level) noexcept
	{
		voxel_block_registration registration = {};

		if (block_id == nullptr || name == nullptr || emitted_light_level == 0U)
			return (FT_ERR_INVALID_ARGUMENT);
		registration.name = name;
		registration.metadata.solid = FT_TRUE;
		registration.metadata.transparent = FT_FALSE;
		registration.metadata.liquid = FT_FALSE;
		registration.metadata.replaceable = FT_FALSE;
		registration.metadata.light_emitting = FT_TRUE;
		registration.metadata.occludes_faces = FT_TRUE;
		registration.metadata.hardness = 1U;
		registration.metadata.breakable = FT_TRUE;
		registration.metadata.emitted_light_level = emitted_light_level;
		registration.metadata.light_attenuation = 15U;
		/* Runtime block registration requires a loadable asset for every face. */
		registration.asset_paths[VOXEL_BLOCK_ASSET_FACE_TOP] =
			"Libft/Test/Scripting/export_values.asset";
		registration.asset_paths[VOXEL_BLOCK_ASSET_FACE_BOTTOM] =
			"Libft/Test/Scripting/export_values.asset";
		registration.asset_paths[VOXEL_BLOCK_ASSET_FACE_NORTH] =
			"Libft/Test/Scripting/export_values.asset";
		registration.asset_paths[VOXEL_BLOCK_ASSET_FACE_SOUTH] =
			"Libft/Test/Scripting/export_values.asset";
		registration.asset_paths[VOXEL_BLOCK_ASSET_FACE_EAST] =
			"Libft/Test/Scripting/export_values.asset";
		registration.asset_paths[VOXEL_BLOCK_ASSET_FACE_WEST] =
			"Libft/Test/Scripting/export_values.asset";
		return (voxel_register_block(registration, block_id));
	}

	static int32_t run_section_boundary_transition(World &world,
		LightingObservationHarness &harness, uint64_t &edit_id,
		int32_t world_x, int32_t world_z, uint32_t block_id,
		uint64_t &frame) noexcept
	{
		double surface;
		int32_t surface_y;
		int32_t world_y;
		uint32_t existing_block_id = GAME_VOXEL_AIR_BLOCK;
		int32_t error_code;

		if (!world.surface_top_at(world_x, world_z, &surface))
			return (FT_ERR_NOT_FOUND);
		surface_y = static_cast<int32_t>(surface);
		world_y = ((surface_y / 16) + 1) * 16;
		while (world_y < GAME_VOXEL_CHUNK_HEIGHT)
		{
			if (!world.block_id_at(world_x, world_y, world_z,
					&existing_block_id))
				return (FT_ERR_NOT_FOUND);
			if (existing_block_id == GAME_VOXEL_AIR_BLOCK)
				break ;
			world_y += 16;
		}
		if (world_y >= GAME_VOXEL_CHUNK_HEIGHT)
			return (FT_ERR_NOT_FOUND);
		std::fprintf(stderr,
			"lighting-harness: section-boundary transition coordinate="
			"(%d,%d,%d) block=%u\n", world_x, world_y, world_z,
			static_cast<unsigned int>(block_id));
		error_code = run_edit(world, harness, edit_id++, world_x, world_y,
			world_z, block_id, frame);
		if (error_code == FT_ERR_SUCCESS)
			error_code = run_edit(world, harness, edit_id++, world_x, world_y,
				world_z, GAME_VOXEL_AIR_BLOCK, frame);
		if (error_code == FT_ERR_SUCCESS)
			error_code = verify_transition_neighbors(world, harness, world_x,
			world_z, frame);
		return (error_code);
	}

	static int32_t run_superseded_transition(World &world,
		LightingObservationHarness &harness, uint64_t &edit_id,
		int32_t world_x, int32_t world_z, uint64_t &frame) noexcept
	{
		double surface;
		int32_t world_y;
		uint32_t existing_block_id = GAME_VOXEL_AIR_BLOCK;
		const WorldChunk *before_chunk;
		uint64_t before_mesh;
		uint64_t before_voxel;
		const uint64_t first_edit_id = edit_id++;
		const uint64_t second_edit_id = edit_id++;
		const std::chrono::steady_clock::time_point first_started =
			std::chrono::steady_clock::now();
		int32_t error_code;

		if (!world.surface_top_at(world_x, world_z, &surface))
			return (FT_ERR_NOT_FOUND);
		world_y = static_cast<int32_t>(surface + 1.0);
		if (!world.block_id_at(world_x, world_y, world_z,
				&existing_block_id)
			|| existing_block_id != GAME_VOXEL_AIR_BLOCK)
			return (FT_ERR_INVALID_STATE);
		before_chunk = world.find_chunk(0, 0);
		if (before_chunk == nullptr || !before_chunk->initialized)
			return (FT_ERR_NOT_FOUND);
		before_mesh = before_chunk->mesh_revision;
		before_voxel = before_chunk->voxel_revision;
		error_code = harness.begin_edit(first_edit_id, frame, world_x, world_y,
			world_z, world.stream_diagnostics().remesh_scanned_cells,
			world.stream_diagnostics().remesh_incremental_completed_count,
			world.stream_diagnostics().remesh_full_completed_count);
		if (error_code == FT_ERR_SUCCESS)
			error_code = world.place_block_at(world_x, world_y, world_z,
				LIGHT_HARNESS_STONE);
		if (error_code == FT_ERR_SUCCESS)
		{
			uint32_t block_id = GAME_VOXEL_AIR_BLOCK;

			if (!world.block_id_at(world_x, world_y, world_z, &block_id)
				|| block_id != LIGHT_HARNESS_STONE)
				error_code = FT_ERR_INTERNAL;
		}
		if (error_code == FT_ERR_SUCCESS)
			error_code = harness.mark_authoritative(first_edit_id, frame,
			elapsed_milliseconds(first_started));

		const std::chrono::steady_clock::time_point second_started =
			std::chrono::steady_clock::now();
		if (error_code == FT_ERR_SUCCESS)
			error_code = harness.begin_edit(second_edit_id, frame, world_x,
				world_y, world_z, world.stream_diagnostics().remesh_scanned_cells,
				world.stream_diagnostics().remesh_incremental_completed_count,
				world.stream_diagnostics().remesh_full_completed_count);
		if (error_code == FT_ERR_SUCCESS)
			error_code = world.delete_block_at(world_x, world_y, world_z);
		if (error_code == FT_ERR_SUCCESS)
		{
			uint32_t block_id = LIGHT_HARNESS_STONE;

			if (!world.block_id_at(world_x, world_y, world_z, &block_id)
				|| block_id != GAME_VOXEL_AIR_BLOCK)
				error_code = FT_ERR_INTERNAL;
		}
		if (error_code == FT_ERR_SUCCESS)
			error_code = harness.mark_authoritative(second_edit_id, frame,
			elapsed_milliseconds(second_started));
		if (error_code == FT_ERR_SUCCESS)
			error_code = harness.mark_superseded(first_edit_id, frame,
			elapsed_milliseconds(first_started));
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);

		const uint64_t start_frame = frame;
		while (frame - start_frame <= LIGHT_HARNESS_MAX_EDIT_FRAMES)
		{
			const WorldChunk *chunk;
			World::StreamDiagnostics diagnostics;

			lighting_watchdog_heartbeat("superseded_update", frame,
				second_edit_id);
			error_code = world.update_around(0.0, 0.0, 0,
				WorldCoordinates::MIN_RENDER_DISTANCE);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			error_code = observe_edit_neighborhood(world, harness, 0, 0,
				frame, second_edit_id);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			chunk = world.find_chunk(0, 0);
			if (chunk == nullptr || !chunk->initialized)
				return (FT_ERR_NOT_FOUND);
			if (chunk->light_buffer_is_valid() && chunk->light_is_current()
				&& chunk->mesh_revision > before_mesh
				&& chunk->voxel_revision >= before_voxel)
			{
				error_code = harness.mark_light_ready(second_edit_id, frame,
					elapsed_milliseconds(second_started));
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				diagnostics = world.stream_diagnostics();
				error_code = harness.mark_mesh_ready(second_edit_id, frame,
					elapsed_milliseconds(second_started),
					diagnostics.remesh_scanned_cells,
					diagnostics.remesh_incremental_completed_count,
					diagnostics.remesh_full_completed_count,
					chunk->last_light_remesh_incremental != FT_FALSE);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				return (compare_light_to_oracle(world, 0, 0,
					"superseded final state"));
			}
			frame += 1U;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		std::fprintf(stderr,
			"lighting-harness: superseded transition timeout first=%llu "
			"second=%llu frames=%llu\n",
			static_cast<unsigned long long>(first_edit_id),
			static_cast<unsigned long long>(second_edit_id),
			frame - start_frame);
		return (FT_ERR_TIMEOUT);
	}

	static bool is_expected_backpressure_error(int32_t error_code) noexcept
	{
		return (error_code == FT_ERR_FULL || error_code == FT_ERR_THREAD_BUSY
			|| error_code == FT_ERR_TIMEOUT
			|| error_code == FT_ERR_PRIORITY_QUEUE_NO_MEMORY);
	}

	static int32_t run_supersession_permutations(World &world,
		LightingObservationHarness &harness, uint64_t &edit_id,
		uint64_t &frame) noexcept
	{
		static const uint32_t requested_blocks[3] = {
			LIGHT_HARNESS_STONE, GAME_VOXEL_AIR_BLOCK,
			VOXEL_GENERATOR_DIRT_BLOCK
		};
		uint64_t edit_ids[3] = {edit_id++, edit_id++, edit_id++};
		double surface;
		int32_t world_y;
		uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
		const WorldChunk *before_chunk;
		uint64_t before_mesh;
		uint32_t index = 0U;
		int32_t error_code;

		if (!world.surface_top_at(8, 8, &surface))
			return (FT_ERR_NOT_FOUND);
		world_y = static_cast<int32_t>(surface + 1.0);
		if (!world.block_id_at(8, world_y, 8, &block_id)
			|| block_id != GAME_VOXEL_AIR_BLOCK)
			return (FT_ERR_INVALID_STATE);
		before_chunk = world.find_chunk(0, 0);
		if (before_chunk == nullptr || !before_chunk->initialized)
			return (FT_ERR_NOT_FOUND);
		before_mesh = before_chunk->mesh_revision;
		while (index < 3U)
		{
			const World::StreamDiagnostics diagnostics = world.stream_diagnostics();

			error_code = harness.begin_edit(edit_ids[index], frame, 8, world_y,
				8, diagnostics.remesh_scanned_cells,
				diagnostics.remesh_incremental_completed_count,
				diagnostics.remesh_full_completed_count);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			if (requested_blocks[index] == GAME_VOXEL_AIR_BLOCK)
				error_code = world.delete_block_at(8, world_y, 8);
			else
				error_code = world.place_block_at(8, world_y, 8,
					requested_blocks[index]);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			if (!world.block_id_at(8, world_y, 8, &block_id)
				|| block_id != requested_blocks[index])
				return (FT_ERR_INTERNAL);
			error_code = harness.mark_authoritative(edit_ids[index], frame, 0U);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			if (index > 0U)
			{
				error_code = harness.mark_superseded(edit_ids[index - 1U],
					frame, 0U);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
			}
			index += 1U;
		}

		const uint64_t start_frame = frame;
		while (frame - start_frame <= LIGHT_HARNESS_MAX_EDIT_FRAMES * 4U)
		{
			World::StreamDiagnostics diagnostics;

			lighting_watchdog_heartbeat("supersession_update", frame,
				edit_ids[2]);
			error_code = world.update_around(0.0, 0.0, 0,
				WorldCoordinates::MIN_RENDER_DISTANCE);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			error_code = observe_edit_neighborhood(world, harness, 0, 0,
				frame, edit_ids[2]);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			const WorldChunk *chunk = world.find_chunk(0, 0);

			if (chunk == nullptr || !chunk->initialized)
				return (FT_ERR_NOT_FOUND);
			if (chunk->light_buffer_is_valid() && chunk->light_is_current()
				&& !chunk->mesh_dirty && chunk->pending_mesh_request_id == 0U
				&& chunk->mesh_revision > before_mesh
				&& world.block_id_at(8, world_y, 8, &block_id)
				&& block_id == requested_blocks[2])
			{
				error_code = harness.mark_light_ready(edit_ids[2], frame, 0U);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				diagnostics = world.stream_diagnostics();
				error_code = harness.mark_mesh_ready(edit_ids[2], frame, 0U,
					diagnostics.remesh_scanned_cells,
					diagnostics.remesh_incremental_completed_count,
					diagnostics.remesh_full_completed_count,
					chunk->last_light_remesh_incremental != FT_FALSE);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				return (compare_light_to_oracle(world, 0, 0,
					"supersession permutations"));
			}
			frame += 1U;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return (FT_ERR_TIMEOUT);
	}

	static int32_t run_queue_pressure(World &world,
		LightingObservationHarness &harness, uint64_t &frame) noexcept
	{
		static const uint32_t burst_count = 32U;
		double surface;
		int32_t world_y;
		uint32_t existing_block_id = GAME_VOXEL_AIR_BLOCK;
		const WorldChunk *before_chunk;
		uint64_t before_mesh;
		uint32_t accepted_count = 0U;
		uint32_t rejected_count = 0U;
		uint32_t index = 0U;
		int32_t error_code;

		if (!world.surface_top_at(6, 6, &surface))
			return (FT_ERR_NOT_FOUND);
		world_y = static_cast<int32_t>(surface + 1.0);
		if (!world.block_id_at(6, world_y, 6, &existing_block_id)
			|| existing_block_id != GAME_VOXEL_AIR_BLOCK)
			return (FT_ERR_INVALID_STATE);
		before_chunk = world.find_chunk(0, 0);
		if (before_chunk == nullptr || !before_chunk->initialized)
			return (FT_ERR_NOT_FOUND);
		before_mesh = before_chunk->mesh_revision;
		while (index < burst_count)
		{
			const uint32_t block_id = (index & 1U) == 0U
				? LIGHT_HARNESS_STONE : GAME_VOXEL_AIR_BLOCK;

			if (block_id == GAME_VOXEL_AIR_BLOCK)
				error_code = world.delete_block_at(6, world_y, 6);
			else
				error_code = world.place_block_at(6, world_y, 6, block_id);
			if (error_code != FT_ERR_SUCCESS)
			{
				if (!is_expected_backpressure_error(error_code))
					return (error_code);
				rejected_count += 1U;
			}
			else
			{
				uint32_t observed_block_id = GAME_VOXEL_AIR_BLOCK;

				if (!world.block_id_at(6, world_y, 6, &observed_block_id)
					|| observed_block_id != block_id)
					return (FT_ERR_INTERNAL);
				accepted_count += 1U;
			}
			index += 1U;
		}
		if (accepted_count == 0U)
			return (FT_ERR_FULL);
		/* Make the final intended state explicit after the burst. */
		error_code = world.delete_block_at(6, world_y, 6);
		if (error_code != FT_ERR_SUCCESS
			&& !is_expected_backpressure_error(error_code))
			return (error_code);

		const uint64_t start_frame = frame;
		while (frame - start_frame <= LIGHT_HARNESS_MAX_EDIT_FRAMES * 4U)
		{
			const WorldChunk *chunk;
			World::StreamDiagnostics diagnostics;

			lighting_watchdog_heartbeat("queue_pressure_update", frame, 0U);
			error_code = world.update_around(0.0, 0.0, 0,
				WorldCoordinates::MIN_RENDER_DISTANCE);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			error_code = observe_edit_neighborhood(world, harness, 0, 0,
				frame, 0U);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			chunk = world.find_chunk(0, 0);
			if (chunk == nullptr || !chunk->initialized)
				return (FT_ERR_NOT_FOUND);
			if (chunk->light_buffer_is_valid() && chunk->light_is_current()
				&& chunk->mesh_revision > before_mesh
				&& chunk->pending_mesh_request_id == 0U
				&& world.block_id_at(6, world_y, 6,
					&existing_block_id)
				&& existing_block_id == GAME_VOXEL_AIR_BLOCK)
			{
				diagnostics = world.stream_diagnostics();
				if (diagnostics.remesh_priority_queue_depth != 0U
					|| diagnostics.interactive_remesh_queue_depth != 0U)
					return (FT_ERR_INTERNAL);
				error_code = compare_light_to_oracle(world, 0, 0,
					"queue-pressure final state");
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				std::fprintf(stderr,
					"lighting-harness: queue pressure accepted=%u "
					"rejected=%u peak=%zu\n",
					static_cast<unsigned int>(accepted_count),
					static_cast<unsigned int>(rejected_count),
					diagnostics.remesh_queue_peak);
				return (FT_ERR_SUCCESS);
			}
			frame += 1U;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return (FT_ERR_TIMEOUT);
	}

	static int32_t run_simultaneous_opposite_border_edits(World &world,
		LightingObservationHarness &harness, uint64_t &edit_id,
		uint64_t &frame) noexcept
	{
		static const int32_t world_x[2] = {GAME_VOXEL_CHUNK_WIDTH - 1,
			GAME_VOXEL_CHUNK_WIDTH};
		static const int32_t world_z[2] = {4, 4};
		uint64_t ids[2] = {edit_id++, edit_id++};
		int32_t world_y[2] = {0, 0};
		uint64_t before_mesh[2] = {0U, 0U};
		bool light_marked[2] = {false, false};
		bool mesh_marked[2] = {false, false};
		int32_t edit_errors[2] = {FT_ERR_SUCCESS, FT_ERR_SUCCESS};
		double surface;
		uint32_t block_id;
		int32_t error_code = FT_ERR_SUCCESS;
		uint32_t index = 0U;

		while (index < 2U)
		{
			if (!world.surface_top_at(world_x[index], world_z[index],
				&surface))
				return (FT_ERR_NOT_FOUND);
			world_y[index] = static_cast<int32_t>(surface + 1.0);
			block_id = GAME_VOXEL_AIR_BLOCK;
			if (!world.block_id_at(world_x[index], world_y[index],
				world_z[index], &block_id)
				|| block_id != GAME_VOXEL_AIR_BLOCK)
				return (FT_ERR_INVALID_STATE);
			const int32_t chunk_x = index == 0U ? 0 : 1;
			const WorldChunk *chunk = world.find_chunk(chunk_x, 0);

			if (chunk == nullptr || !chunk->initialized)
				return (FT_ERR_NOT_FOUND);
			before_mesh[index] = chunk->mesh_revision;
			error_code = harness.begin_edit(ids[index], frame,
				world_x[index], world_y[index], world_z[index],
				world.stream_diagnostics().remesh_scanned_cells,
				world.stream_diagnostics().remesh_incremental_completed_count,
				world.stream_diagnostics().remesh_full_completed_count);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			index += 1U;
		}
		/* Submit both edits concurrently before the worker is given another
		 * frame. This covers callers racing on opposite sides of one chunk
		 * border, rather than only testing sequential request coalescing. */
		{
			std::thread edit_workers[2];
			index = 0U;
			while (index < 2U)
			{
				const uint32_t worker_index = index;

				edit_workers[index] = std::thread([&world, &edit_errors,
					&world_y, worker_index]()
				{
					uint32_t observed_block_id = GAME_VOXEL_AIR_BLOCK;
					int32_t worker_error = world.place_block_at(
						world_x[worker_index], world_y[worker_index],
						world_z[worker_index], LIGHT_HARNESS_STONE);

					if (worker_error == FT_ERR_SUCCESS
						&& (!world.block_id_at(world_x[worker_index],
							world_y[worker_index], world_z[worker_index],
							&observed_block_id)
							|| observed_block_id != LIGHT_HARNESS_STONE))
						worker_error = FT_ERR_INTERNAL;
					edit_errors[worker_index] = worker_error;
				});
				index += 1U;
			}
			index = 0U;
			while (index < 2U)
			{
				edit_workers[index].join();
				if (edit_errors[index] != FT_ERR_SUCCESS)
					return (edit_errors[index]);
				error_code = harness.mark_authoritative(ids[index], frame, 0U);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				index += 1U;
			}
		}

		const uint64_t start_frame = frame;
		while (frame - start_frame <= LIGHT_HARNESS_MAX_EDIT_FRAMES)
		{
			lighting_watchdog_heartbeat("border_race_update", frame, ids[0]);
			error_code = world.update_around(0.0, 0.0, 0,
				WorldCoordinates::MIN_RENDER_DISTANCE);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			index = 0U;
			while (index < 2U)
			{
				const int32_t chunk_x = index == 0U ? 0 : 1;
				const WorldChunk *chunk = world.find_chunk(chunk_x, 0);
				World::StreamDiagnostics diagnostics;

				error_code = observe_opposite_border_neighborhood(world,
					harness, frame, ids[0], ids[1]);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				if (chunk == nullptr || !chunk->initialized)
					return (FT_ERR_NOT_FOUND);
				if (!light_marked[index] && chunk->light_buffer_is_valid()
					&& chunk->light_is_current())
				{
					error_code = harness.mark_light_ready(ids[index], frame, 0U);
					if (error_code != FT_ERR_SUCCESS)
						return (error_code);
					light_marked[index] = true;
				}
				if (light_marked[index] && !mesh_marked[index]
					&& !chunk->mesh_dirty && chunk->pending_mesh_request_id == 0U
					&& chunk->mesh_revision > before_mesh[index]
					&& chunk->voxel_revision != 0U)
				{
					diagnostics = world.stream_diagnostics();
					error_code = harness.mark_mesh_ready(ids[index], frame, 0U,
						diagnostics.remesh_scanned_cells,
						diagnostics.remesh_incremental_completed_count,
						diagnostics.remesh_full_completed_count,
						chunk->last_light_remesh_incremental != FT_FALSE);
					if (error_code != FT_ERR_SUCCESS)
						return (error_code);
					mesh_marked[index] = true;
				}
				index += 1U;
			}
			if (mesh_marked[0] && mesh_marked[1])
			{
				error_code = compare_light_to_oracle(world, 0, 0,
					"simultaneous-opposite-border-west");
				if (error_code == FT_ERR_SUCCESS)
					error_code = compare_light_to_oracle(world, 1, 0,
						"simultaneous-opposite-border-east");
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				break ;
			}
			frame += 1U;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		if (!mesh_marked[0] || !mesh_marked[1])
			return (FT_ERR_TIMEOUT);
		/* Restore the baseline through the normal edit path.  These deletes are
		 * intentionally after the paired publication so the report contains the
		 * simultaneous case independently of cleanup latency. */
		index = 0U;
		while (index < 2U)
		{
			error_code = world.delete_block_at(world_x[index], world_y[index],
				world_z[index]);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			index += 1U;
		}
		return (FT_ERR_SUCCESS);
	}

	static int32_t run_transactional_failure_inputs(World &world,
		LightingObservationHarness &harness, uint64_t frame) noexcept
	{
		const int32_t world_x = 4;
		const int32_t world_z = 4;
		double surface;
		int32_t world_y;
		uint32_t before_block = GAME_VOXEL_AIR_BLOCK;
		uint32_t after_block = GAME_VOXEL_AIR_BLOCK;
		const WorldChunk *before_chunk;
		const WorldChunk *after_chunk;
		const int32_t invalid_world_x = GAME_VOXEL_CHUNK_WIDTH * 1000;
		const int32_t invalid_world_z = GAME_VOXEL_CHUNK_DEPTH * 1000;
		WorldGenerationPipeline::WorldChunkSnapshot missing_snapshot;
		int32_t error_code;

		if (!world.surface_top_at(world_x, world_z, &surface))
			return (FT_ERR_NOT_FOUND);
		world_y = static_cast<int32_t>(surface + 1.0);
		before_chunk = world.find_chunk(0, 0);
		if (before_chunk == nullptr || !before_chunk->initialized
			|| !world.block_id_at(world_x, world_y, world_z, &before_block))
			return (FT_ERR_NOT_FOUND);
		error_code = world.place_block_at(world_x, -1, world_z,
			LIGHT_HARNESS_STONE);
		if (error_code == FT_ERR_SUCCESS)
			return (FT_ERR_INTERNAL);
		error_code = world.delete_block_at(world_x, GAME_VOXEL_CHUNK_HEIGHT,
			world_z);
		if (error_code == FT_ERR_SUCCESS)
			return (FT_ERR_INTERNAL);
		error_code = world.place_block_at(invalid_world_x, world_y,
			invalid_world_z, LIGHT_HARNESS_STONE);
		if (error_code == FT_ERR_SUCCESS)
			return (FT_ERR_INTERNAL);
		error_code = world.delete_block_at(invalid_world_x, world_y,
			invalid_world_z);
		if (error_code == FT_ERR_SUCCESS)
			return (FT_ERR_INTERNAL);
		error_code = world.capture_remesh_snapshot(1000, 1000,
			missing_snapshot);
		if (error_code == FT_ERR_SUCCESS)
			return (FT_ERR_INTERNAL);
		after_chunk = world.find_chunk(0, 0);
		if (after_chunk == nullptr || !after_chunk->initialized
			|| !world.block_id_at(world_x, world_y, world_z, &after_block)
			|| after_block != before_block
			|| after_chunk->voxel_revision != before_chunk->voxel_revision
			|| after_chunk->content_version != before_chunk->content_version
			|| after_chunk->light_input_version
				!= before_chunk->light_input_version)
			return (FT_ERR_INTERNAL);
		if (harness.observe(world, 0, 0, frame, 0U) != FT_ERR_SUCCESS)
			return (FT_ERR_INTERNAL);
		return (compare_light_to_oracle(world, 0, 0,
			"transactional-failure-inputs"));
	}

	static int32_t run_allocation_failure_sweep(World &world) noexcept
	{
		static const ft_size_t allocation_limits[14] = {
			1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U,
			256U, 512U, 1024U, 2048U, 4096U, 8192U
		};
		const int32_t world_x = 7;
		const int32_t world_z = 7;
		double surface;
		int32_t world_y;
		uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
		uint32_t observed_block_id = GAME_VOXEL_AIR_BLOCK;
		uint32_t failure_count = 0U;
		int32_t first_failure = FT_ERR_SUCCESS;
		uint32_t index = 0U;

		if (!world.surface_top_at(world_x, world_z, &surface))
			return (FT_ERR_NOT_FOUND);
		world_y = static_cast<int32_t>(surface + 1.0);
		if (!world.block_id_at(world_x, world_y, world_z, &block_id)
			|| block_id != GAME_VOXEL_AIR_BLOCK)
			return (FT_ERR_INVALID_STATE);
		while (index < 14U)
		{
			const WorldChunk *before_chunk = world.find_chunk(0, 0);
			const uint64_t before_voxel = before_chunk == nullptr ? 0U
				: before_chunk->voxel_revision;
			const uint16_t before_content = before_chunk == nullptr ? 0U
				: before_chunk->content_version;
			const uint16_t before_input = before_chunk == nullptr ? 0U
				: before_chunk->light_input_version;
			int32_t error_code;

			if (before_chunk == nullptr || !before_chunk->initialized)
				return (FT_ERR_NOT_FOUND);
			if (cma_set_alloc_limit(allocation_limits[index])
				!= FT_ERR_SUCCESS)
				return (FT_ERR_INTERNAL);
			error_code = world.place_block_at(world_x, world_y, world_z,
				LIGHT_HARNESS_STONE);
			if (cma_set_alloc_limit(0U) != FT_ERR_SUCCESS)
				return (FT_ERR_INTERNAL);
			if (error_code != FT_ERR_SUCCESS)
			{
				failure_count += 1U;
				observed_block_id = GAME_VOXEL_AIR_BLOCK;
				if (!world.block_id_at(world_x, world_y, world_z,
					&observed_block_id))
				{
					if (first_failure == FT_ERR_SUCCESS)
						first_failure = FT_ERR_INTERNAL;
				}
				if (observed_block_id != GAME_VOXEL_AIR_BLOCK
					&& world.delete_block_at(world_x, world_y, world_z)
					!= FT_ERR_SUCCESS)
				{
					if (first_failure == FT_ERR_SUCCESS)
						first_failure = FT_ERR_INTERNAL;
				}
				before_chunk = world.find_chunk(0, 0);
				if (before_chunk == nullptr
					|| before_chunk->voxel_revision != before_voxel
					|| before_chunk->content_version != before_content
					|| before_chunk->light_input_version != before_input)
				{
					if (first_failure == FT_ERR_SUCCESS)
						first_failure = FT_ERR_INTERNAL;
				}
			}
			else
			{
				if (!world.block_id_at(world_x, world_y, world_z,
					&observed_block_id)
					|| observed_block_id != LIGHT_HARNESS_STONE)
				{
					if (first_failure == FT_ERR_SUCCESS)
						first_failure = FT_ERR_INTERNAL;
				}
				if (world.delete_block_at(world_x, world_y, world_z)
					!= FT_ERR_SUCCESS)
				{
					if (first_failure == FT_ERR_SUCCESS)
						first_failure = FT_ERR_INTERNAL;
				}
			}
			std::fprintf(stderr,
				"lighting-harness: allocation attempt limit=%zu error=%d "
				"observed_block=%u first_failure=%d\n",
				static_cast<std::size_t>(allocation_limits[index]), error_code,
				static_cast<unsigned int>(observed_block_id), first_failure);
			index += 1U;
		}
		std::fprintf(stderr,
			"lighting-harness: allocation failure sweep attempts=14 failures=%u\n",
			static_cast<unsigned int>(failure_count));
		if (failure_count == 0U && first_failure == FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_OPERATION);
		if (first_failure != FT_ERR_SUCCESS)
			return (first_failure);
		return (FT_ERR_SUCCESS);
	}

	static int32_t run_version_rollover_checks() noexcept
	{
		const uint16_t before_wrap = 0xffffU;
		const uint16_t after_wrap = world_light_version::next(before_wrap);
		const uint64_t newest_after_wrap = 0U;
		const uint64_t oldest_before_wrap = UINT64_MAX;
		const uint64_t newest_before_wrap = UINT64_MAX;

		if (after_wrap != 1U
			|| world_light_version::next(0U) != 1U
			|| world_light_version::matches(0U, 0U)
			|| !world_light_version::matches(1U, 1U))
			return (FT_ERR_INTERNAL);
		if (static_cast<int64_t>(newest_after_wrap - oldest_before_wrap)
			< 0
			|| static_cast<int64_t>(newest_before_wrap - newest_after_wrap)
			>= 0)
			return (FT_ERR_INTERNAL);
		return (FT_ERR_SUCCESS);
	}

	static int32_t run_emissive_transition(World &world,
		LightingObservationHarness &harness, uint64_t &edit_id,
		uint64_t &frame, uint32_t emissive_block_id) noexcept
	{
		return (run_transition(world, harness, edit_id, 4, 4,
			emissive_block_id, frame, "emissive-source"));
	}

	static int32_t wait_for_neighbor_oracle(World &world,
		LightingObservationHarness &harness, int32_t chunk_x, int32_t chunk_z,
		uint64_t &frame, const char *context) noexcept
	{
		const uint64_t start_frame = frame;

		while (frame - start_frame <= LIGHT_HARNESS_MAX_EDIT_FRAMES)
		{
			const WorldChunk *chunk;
			int32_t error_code;

			lighting_watchdog_heartbeat("neighbor_update", frame, 0U);
			error_code = world.update_around(0.0, 0.0, 0,
				WorldCoordinates::MIN_RENDER_DISTANCE);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			chunk = world.find_chunk(chunk_x, chunk_z);
			if (chunk == nullptr || !chunk->initialized)
				return (FT_ERR_NOT_FOUND);
			error_code = harness.observe(world, chunk_x, chunk_z, frame, 0U);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			if (!chunk->mesh_dirty && chunk->pending_mesh_request_id == 0U
				&& chunk->light_buffer_is_valid() && chunk->light_is_current())
			{
				error_code = compare_light_to_oracle(world, chunk_x, chunk_z,
					context);
				if (error_code == FT_ERR_SUCCESS)
					return (FT_ERR_SUCCESS);
			}
			frame += 1U;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		std::fprintf(stderr,
			"lighting-harness: neighbor oracle timeout chunk=(%d,%d) "
			"frames=" FT_UINT64_DECIMAL_FORMAT "\n", chunk_x, chunk_z,
			frame - start_frame);
		return (FT_ERR_TIMEOUT);
	}

	static void retain_first_failure(int32_t result, const char *scenario,
		int32_t &first_failure) noexcept
	{
		if (result == FT_ERR_SUCCESS)
			return ;
		std::fprintf(stderr,
			"lighting-harness: scenario failed name=%s error=%d\n",
			scenario == nullptr ? "<unnamed>" : scenario, result);
		if (first_failure == FT_ERR_SUCCESS)
			first_failure = result;
	}

	static void retain_scenario_result(int32_t result, const char *scenario,
		uint32_t &scenario_count, uint32_t &scenario_failure_count,
		int32_t &first_failure) noexcept
	{
		scenario_count += 1U;
		if (result != FT_ERR_SUCCESS)
			scenario_failure_count += 1U;
		retain_first_failure(result, scenario, first_failure);
	}

	static void record_scenario_result(LightingObservationHarness &harness,
		int32_t result, const char *scenario, uint32_t &scenario_count,
		uint32_t &scenario_failure_count, int32_t &first_failure) noexcept
	{
		const int32_t record_error = harness.record_scenario(scenario, result);

		retain_scenario_result(result, scenario, scenario_count,
			scenario_failure_count, first_failure);
		retain_first_failure(record_error, "scenario-record", first_failure);
	}
}

LightingObservationHarness::LightingObservationHarness()
	: frames_(), last_observations_(), edits_(), scenarios_(),
	  frame_retention_limit_(0U), dropped_frame_count_(0U),
	  storage_error_(FT_ERR_SUCCESS),
	  observation_invariant_error_(FT_ERR_SUCCESS)
{
	try
	{
		last_observations_.reserve(128U);
		edits_.reserve(512U);
		scenarios_.reserve(64U);
	}
	catch (const std::bad_alloc &)
	{
		storage_error_ = FT_ERR_NO_MEMORY;
	}
}

LightingObservationHarness::LightingObservationHarness(
	const LightingObservationHarness &other)
	: frames_(other.frames_), last_observations_(other.last_observations_),
	  edits_(other.edits_),
	  scenarios_(other.scenarios_),
	  frame_retention_limit_(other.frame_retention_limit_),
	  dropped_frame_count_(other.dropped_frame_count_),
	  storage_error_(other.storage_error_),
	  observation_invariant_error_(other.observation_invariant_error_)
{
}

LightingObservationHarness::~LightingObservationHarness()
{
}

LightingObservationHarness &LightingObservationHarness::operator=(
	const LightingObservationHarness &other)
{
	if (this != &other)
	{
		frames_ = other.frames_;
		last_observations_ = other.last_observations_;
		edits_ = other.edits_;
		scenarios_ = other.scenarios_;
		frame_retention_limit_ = other.frame_retention_limit_;
		dropped_frame_count_ = other.dropped_frame_count_;
		storage_error_ = other.storage_error_;
		observation_invariant_error_ = other.observation_invariant_error_;
	}
	return (*this);
}

int32_t LightingObservationHarness::record_scenario(const char *name,
	int32_t result) noexcept
{
	LightingScenarioObservation observation;

	if (name == nullptr || name[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	if (storage_error_ != FT_ERR_SUCCESS)
		return (storage_error_);
	try
	{
		observation.name = name;
		observation.result = result;
		scenarios_.push_back(observation);
	}
	catch (const std::bad_alloc &)
	{
		storage_error_ = FT_ERR_NO_MEMORY;
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t LightingObservationHarness::mesh_light_summary(const World &world,
	int32_t chunk_x, int32_t chunk_z, uint8_t *minimum, uint8_t *maximum,
	uint32_t *nonzero, uint32_t *faces) noexcept
{
	const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
	uint8_t minimum_value = 255U;
	uint8_t maximum_value = 0U;
	uint32_t nonzero_value = 0U;
	uint32_t face_value = 0U;
	std::size_t index = 0U;

	if (chunk == nullptr || minimum == nullptr || maximum == nullptr
		|| nonzero == nullptr || faces == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	while (index < chunk->mesh.vertices.size())
	{
		const uint8_t value = chunk->mesh.vertices[index].packed_light;

		if (value < minimum_value)
			minimum_value = value;
		if (value > maximum_value)
			maximum_value = value;
		if (value != 0U)
			nonzero_value += 1U;
		if (chunk->mesh.vertices[index].face != 255U)
			face_value += 1U;
		index += 1U;
	}
	if (chunk->mesh.vertices.empty())
		minimum_value = 0U;
	*minimum = minimum_value;
	*maximum = maximum_value;
	*nonzero = nonzero_value;
	*faces = face_value;
	return (FT_ERR_SUCCESS);
}

bool LightingObservationHarness::revision_is_older(uint64_t current,
	uint64_t previous) noexcept
{
	return (current != previous
		&& static_cast<int64_t>(current - previous) < 0);
}

int32_t LightingObservationHarness::publication_transition_error(
	const LightingFrameObservation &previous,
	const LightingFrameObservation &current) noexcept
{
	if (revision_is_older(current.voxel_revision, previous.voxel_revision)
		|| revision_is_older(current.light_revision, previous.light_revision)
		|| (previous.light_buffer_valid && !current.light_buffer_valid)
		|| (previous.light_ready_for_render
			&& !current.light_ready_for_render
			&& current.mesh_revision > previous.mesh_revision))
		return (FT_ERR_INTERNAL);
	if (current.mesh_revision > previous.mesh_revision
		&& previous.nonzero_light_vertices > 0U
		&& current.nonzero_light_vertices == 0U)
		return (FT_ERR_INTERNAL);
	if (current.mesh_revision > previous.mesh_revision
		&& (!current.light_buffer_valid || !current.light_current
			|| !current.light_ready_for_render))
		return (FT_ERR_INTERNAL);
	return (FT_ERR_SUCCESS);
}

int32_t LightingObservationHarness::observe(const World &world,
	int32_t chunk_x, int32_t chunk_z, uint64_t frame_index,
	uint64_t edit_id) noexcept
{
	const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
	LightingFrameObservation observation;
	World::StreamDiagnostics diagnostics;
	int32_t error_code;

	if (storage_error_ != FT_ERR_SUCCESS)
		return (storage_error_);
	if (chunk == nullptr || !chunk->initialized)
		return (FT_ERR_NOT_FOUND);
	error_code = LightingObservationHarness::mesh_light_summary(world, chunk_x,
		chunk_z, &observation.minimum_vertex_light,
		&observation.maximum_vertex_light, &observation.nonzero_light_vertices,
		&observation.visible_face_count);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	observation.frame_index = frame_index;
	observation.edit_id = edit_id;
	observation.chunk_x = chunk_x;
	observation.chunk_z = chunk_z;
	observation.voxel_revision = chunk->voxel_revision;
	observation.light_revision = chunk->light_revision;
	observation.content_version = chunk->content_version;
	observation.light_version = chunk->light_version;
	observation.light_input_version = chunk->light_input_version;
	observation.computed_light_input_version =
		chunk->computed_light_input_version;
	observation.mesh_revision = chunk->mesh_revision;
	observation.pending_mesh_request_id = chunk->pending_mesh_request_id;
	observation.pending_mesh_request_voxel_revision =
		chunk->pending_mesh_request_voxel_revision;
	observation.pending_mesh_request_content_version =
		chunk->pending_mesh_request_content_version;
	observation.pending_mesh_request_light_input_version =
		chunk->pending_mesh_request_light_input_version;
	observation.block_state_visible = true;
	observation.light_ready_for_render = chunk->light_ready_for_render;
	observation.light_buffer_valid = chunk->light_buffer_is_valid();
	observation.light_current = chunk->light_is_current();
	observation.mesh_dirty = chunk->mesh_dirty;
	observation.remesh_pending = chunk->pending_mesh_request_id != 0U;
	diagnostics = world.stream_diagnostics();
	/* The public diagnostics expose the priority queue depth and historical
	 * queue peak.  Keep those names distinct so a report cannot mistake a peak
	 * counter for the current in-flight count. */
	observation.remesh_queue_depth = diagnostics.remesh_priority_queue_depth;
	observation.priority_queue_depth = diagnostics.remesh_priority_queue_depth;
	observation.interactive_queue_depth =
		diagnostics.interactive_remesh_queue_depth;
	observation.active_generation_count = diagnostics.active_generation_count;
	observation.playable_failed_count = diagnostics.playable_failed_count;
	observation.playable_required_count = diagnostics.playable_required_count;
	observation.playable_drawable_count = diagnostics.playable_drawable_count;
	observation.deferred_edit_count = diagnostics.deferred_edit_count;
	observation.deferred_edit_cursor = diagnostics.deferred_edit_cursor;
	observation.candidate_count = diagnostics.candidate_count;
	observation.ready_count = diagnostics.ready_count;
	observation.pending_count = diagnostics.pending_count;
	observation.retryable_count = diagnostics.retryable_count;
	observation.failed_count = diagnostics.failed_count;
	observation.stream_frame = diagnostics.frame;
	observation.stream_progress_frame = diagnostics.progress_frame;
	observation.remesh_starvation_promotions =
		diagnostics.remesh_starvation_promotions;
	observation.oldest_remesh_queue_age = diagnostics.oldest_remesh_queue_age;
	observation.remesh_snapshot_bytes = diagnostics.remesh_snapshot_bytes;
	observation.remesh_capture_duration_nanoseconds =
		diagnostics.remesh_capture_duration_nanoseconds;
	observation.remesh_capture_count = diagnostics.remesh_capture_count;
	observation.remesh_light_queue_peak = diagnostics.remesh_light_queue_peak;
	observation.remesh_geometry_only_count =
		diagnostics.remesh_geometry_only_count;
	observation.oldest_result_age_nanoseconds =
		diagnostics.oldest_result_age_nanoseconds;
	observation.oldest_pending_age = diagnostics.oldest_pending_age;
	observation.remesh_queue_peak = diagnostics.remesh_queue_peak;
	observation.stale_remesh_count = diagnostics.stale_remesh_result_count;
	observation.stale_remesh_capture_count =
		diagnostics.stale_remesh_capture_count;
	observation.stale_remesh_dependency_count =
		diagnostics.stale_remesh_dependency_count;
	observation.stale_remesh_pending_count =
		diagnostics.stale_remesh_pending_count;
	observation.stale_remesh_revision_count =
		diagnostics.stale_remesh_revision_count;
	observation.remesh_completed_count = diagnostics.remesh_completed_count;
	observation.remesh_incremental_completed_count =
		diagnostics.remesh_incremental_completed_count;
	observation.remesh_full_completed_count =
		diagnostics.remesh_full_completed_count;
	observation.remesh_canceled_count = diagnostics.remesh_canceled_count;
	observation.remesh_scanned_cells = diagnostics.remesh_scanned_cells;
	observation.remesh_propagated_cells = diagnostics.remesh_propagated_cells;
	observation.stream_last_error = diagnostics.last_error;
	{
		std::size_t previous_index = 0U;
		bool has_previous = false;

		while (previous_index < last_observations_.size())
		{
			if (last_observations_[previous_index].chunk_x == observation.chunk_x
				&& last_observations_[previous_index].chunk_z
					== observation.chunk_z)
			{
				has_previous = true;
				break ;
			}
			previous_index += 1U;
		}
		if (has_previous == false)
		{
			try
			{
				last_observations_.push_back(observation);
			}
			catch (const std::bad_alloc &)
			{
				storage_error_ = FT_ERR_NO_MEMORY;
				return (FT_ERR_NO_MEMORY);
			}
		}
		else
		{
			const LightingFrameObservation &previous =
				last_observations_[previous_index];
			const int32_t transition_error =
				LightingObservationHarness::publication_transition_error(
					previous, observation);

			if (transition_error != FT_ERR_SUCCESS)
			{
				if (observation_invariant_error_ == FT_ERR_SUCCESS)
				{
					observation_invariant_error_ = transition_error;
					std::fprintf(stderr,
						"lighting-harness: online publication invariant failed "
						"frame=%llu chunk=(%d,%d) previous=(voxel=%llu "
						"light=%llu mesh=%llu valid=%d current=%d ready=%d) "
						"current=(voxel=%llu light=%llu mesh=%llu valid=%d "
						"current=%d ready=%d)\n",
						static_cast<unsigned long long>(observation.frame_index),
						observation.chunk_x, observation.chunk_z,
						static_cast<unsigned long long>(previous.voxel_revision),
						static_cast<unsigned long long>(previous.light_revision),
						static_cast<unsigned long long>(previous.mesh_revision),
						previous.light_buffer_valid ? 1 : 0,
						previous.light_current ? 1 : 0,
						previous.light_ready_for_render ? 1 : 0,
						static_cast<unsigned long long>(observation.voxel_revision),
						static_cast<unsigned long long>(observation.light_revision),
						static_cast<unsigned long long>(observation.mesh_revision),
						observation.light_buffer_valid ? 1 : 0,
						observation.light_current ? 1 : 0,
						observation.light_ready_for_render ? 1 : 0);
				}
			}
			last_observations_[previous_index] = observation;
		}
	}
	try
	{
		frames_.push_back(observation);
	}
	catch (const std::bad_alloc &)
	{
		storage_error_ = FT_ERR_NO_MEMORY;
		return (FT_ERR_NO_MEMORY);
	}
	if (frame_retention_limit_ != 0U)
	{
		while (frames_.size() > frame_retention_limit_)
		{
			frames_.pop_front();
			dropped_frame_count_ += 1U;
		}
	}
	return (FT_ERR_SUCCESS);
}

int32_t LightingObservationHarness::begin_edit(uint64_t edit_id,
	uint64_t frame_index, int32_t world_x, int32_t world_y,
	int32_t world_z, uint64_t scanned_cells,
	uint64_t incremental_completed_count,
	uint64_t full_completed_count) noexcept
{
	LightingEditObservation observation;

	if (storage_error_ != FT_ERR_SUCCESS)
		return (storage_error_);
	observation.edit_id = edit_id;
	observation.start_frame = frame_index;
	observation.authoritative_frame = UINT64_MAX;
	observation.light_frame = UINT64_MAX;
	observation.mesh_frame = UINT64_MAX;
	observation.authoritative_elapsed_milliseconds = UINT64_MAX;
	observation.light_elapsed_milliseconds = UINT64_MAX;
	observation.light_stage_elapsed_milliseconds = UINT64_MAX;
	observation.mesh_elapsed_milliseconds = UINT64_MAX;
	observation.visible_elapsed_milliseconds = UINT64_MAX;
	observation.visible_stage_elapsed_milliseconds = UINT64_MAX;
	observation.elapsed_milliseconds = UINT64_MAX;
	observation.observed_frames = 0U;
	observation.visible_frame = UINT64_MAX;
	observation.start_scanned_cells = scanned_cells;
	observation.end_scanned_cells = scanned_cells;
	observation.start_incremental_completed_count =
		incremental_completed_count;
	observation.end_incremental_completed_count =
		incremental_completed_count;
	observation.start_full_completed_count = full_completed_count;
	observation.end_full_completed_count = full_completed_count;
	observation.incremental_light_publication = false;
	observation.world_x = world_x;
	observation.world_y = world_y;
	observation.world_z = world_z;
	observation.superseded = false;
	observation.superseded_frame = UINT64_MAX;
	observation.completed = false;
	try
	{
		edits_.push_back(observation);
	}
	catch (const std::bad_alloc &)
	{
		storage_error_ = FT_ERR_NO_MEMORY;
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t LightingObservationHarness::mark_authoritative(uint64_t edit_id,
	uint64_t frame_index, uint64_t elapsed) noexcept
{
	std::size_t index = edits_.size();

	while (index > 0U)
	{
		index -= 1U;
		if (edits_[index].edit_id == edit_id)
		{
			edits_[index].authoritative_frame = frame_index;
			edits_[index].authoritative_elapsed_milliseconds = elapsed;
			return (FT_ERR_SUCCESS);
		}
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t LightingObservationHarness::mark_light_ready(uint64_t edit_id,
	uint64_t frame_index, uint64_t elapsed) noexcept
{
	std::size_t index = edits_.size();

	while (index > 0U)
	{
		index -= 1U;
		if (edits_[index].edit_id == edit_id)
		{
		if (edits_[index].light_frame == UINT64_MAX)
			{
				edits_[index].light_frame = frame_index;
				edits_[index].light_elapsed_milliseconds = elapsed;
				edits_[index].light_stage_elapsed_milliseconds =
					edits_[index].authoritative_elapsed_milliseconds != UINT64_MAX
					&& elapsed >= edits_[index].authoritative_elapsed_milliseconds
					? elapsed - edits_[index].authoritative_elapsed_milliseconds
					: UINT64_MAX;
			}
			return (FT_ERR_SUCCESS);
		}
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t LightingObservationHarness::mark_mesh_ready(uint64_t edit_id,
	uint64_t frame_index, uint64_t elapsed, uint64_t scanned_cells,
	uint64_t incremental_completed_count, uint64_t full_completed_count,
	bool incremental_light_publication) noexcept
{
	std::size_t index = edits_.size();

	while (index > 0U)
	{
		index -= 1U;
		if (edits_[index].edit_id == edit_id)
		{
			edits_[index].mesh_frame = frame_index;
			edits_[index].elapsed_milliseconds = elapsed;
			edits_[index].mesh_elapsed_milliseconds =
				edits_[index].light_elapsed_milliseconds != UINT64_MAX
				&& elapsed >= edits_[index].light_elapsed_milliseconds
				? elapsed - edits_[index].light_elapsed_milliseconds : UINT64_MAX;
			/* The fake publication adapter observes the same accepted event as
			 * the first visible frame.  A real renderer may overwrite this with
			 * its framebuffer-observation timestamp. */
			edits_[index].visible_frame = frame_index;
			edits_[index].visible_elapsed_milliseconds = elapsed;
			edits_[index].visible_stage_elapsed_milliseconds = 0U;
			edits_[index].observed_frames = frame_index
				- edits_[index].start_frame;
			edits_[index].end_scanned_cells = scanned_cells;
			edits_[index].end_incremental_completed_count =
			incremental_completed_count;
		edits_[index].end_full_completed_count = full_completed_count;
		edits_[index].incremental_light_publication =
			incremental_light_publication;
			edits_[index].completed = true;
			return (FT_ERR_SUCCESS);
		}
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t LightingObservationHarness::mark_superseded(uint64_t edit_id,
	uint64_t frame_index, uint64_t elapsed) noexcept
{
	std::size_t index = edits_.size();

	while (index > 0U)
	{
		index -= 1U;
		if (edits_[index].edit_id == edit_id)
		{
			if (edits_[index].completed)
				return (FT_ERR_INVALID_STATE);
			edits_[index].superseded = true;
			edits_[index].superseded_frame = frame_index;
		/* Preserve the time at which the supersession became authoritative
		 * in the existing end-to-end field for report consumers that do not
		 * understand the newer superseded marker yet. */
			edits_[index].elapsed_milliseconds = elapsed;
			return (FT_ERR_SUCCESS);
		}
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t LightingObservationHarness::validate_publication_invariants() const
	noexcept
{
	std::vector<LightingFrameObservation> last_observations;
	std::size_t index;

	if (storage_error_ != FT_ERR_SUCCESS)
		return (storage_error_);
	if (observation_invariant_error_ != FT_ERR_SUCCESS)
		return (observation_invariant_error_);
	try
	{
		last_observations.reserve(frames_.size());
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	index = 0U;
	while (index < frames_.size())
	{
		const LightingFrameObservation &current = frames_[index];
		std::size_t previous_index = 0U;
		bool has_previous = false;

		while (previous_index < last_observations.size())
		{
			if (last_observations[previous_index].chunk_x == current.chunk_x
				&& last_observations[previous_index].chunk_z == current.chunk_z)
			{
				has_previous = true;
				break ;
			}
			previous_index += 1U;
		}
		if (has_previous == false)
		{
			try
			{
				last_observations.push_back(current);
			}
			catch (const std::bad_alloc &)
			{
				return (FT_ERR_NO_MEMORY);
			}
			index += 1U;
			continue ;
		}
		const LightingFrameObservation &previous =
			last_observations[previous_index];

		if (revision_is_older(current.voxel_revision,
				previous.voxel_revision)
			|| revision_is_older(current.light_revision,
				previous.light_revision)
			|| (previous.light_buffer_valid && !current.light_buffer_valid)
			|| (previous.light_ready_for_render
				&& !current.light_ready_for_render
				&& current.mesh_revision > previous.mesh_revision))
		{
			std::fprintf(stderr,
				"lighting-harness: publication invariant failed frame=%llu "
				"chunk=(%d,%d) previous=(voxel=%llu light=%llu mesh=%llu "
				"valid=%d ready=%d) current=(voxel=%llu light=%llu "
				"mesh=%llu valid=%d ready=%d)\n",
				static_cast<unsigned long long>(current.frame_index),
				current.chunk_x, current.chunk_z,
				static_cast<unsigned long long>(previous.voxel_revision),
				static_cast<unsigned long long>(previous.light_revision),
				static_cast<unsigned long long>(previous.mesh_revision),
				previous.light_buffer_valid ? 1 : 0,
				previous.light_ready_for_render ? 1 : 0,
				static_cast<unsigned long long>(current.voxel_revision),
				static_cast<unsigned long long>(current.light_revision),
				static_cast<unsigned long long>(current.mesh_revision),
				current.light_buffer_valid ? 1 : 0,
				current.light_ready_for_render ? 1 : 0);
			return (FT_ERR_INTERNAL);
		}
		if (current.mesh_revision > previous.mesh_revision
			&& previous.nonzero_light_vertices > 0U
			&& current.nonzero_light_vertices == 0U)
		{
			std::fprintf(stderr,
				"lighting-harness: zero-light publication failed frame=%llu "
				"chunk=(%d,%d) previous_mesh=%llu current_mesh=%llu "
				"previous_nonzero=%u current_nonzero=%u "
				"input=%u computed=%u valid=%d current=%d ready=%d\n",
				static_cast<unsigned long long>(current.frame_index),
				current.chunk_x, current.chunk_z,
				static_cast<unsigned long long>(previous.mesh_revision),
				static_cast<unsigned long long>(current.mesh_revision),
				previous.nonzero_light_vertices,
				current.nonzero_light_vertices,
				static_cast<unsigned int>(current.light_input_version),
				static_cast<unsigned int>(
					current.computed_light_input_version),
				current.light_buffer_valid ? 1 : 0,
				current.light_current ? 1 : 0,
				current.light_ready_for_render ? 1 : 0);
			return (FT_ERR_INTERNAL);
		}
		/* This is the fake GPU adapter's publication rule: a newer mesh
		 * identity may not replace the previous one until the matching
		 * light payload is valid, current, and explicitly render-ready.
		 * Keeping an older valid mesh while work is pending is allowed;
		 * publishing a newer stale/zero-light mesh is not. */
		if (current.mesh_revision > previous.mesh_revision
			&& (!current.light_buffer_valid || !current.light_current
				|| !current.light_ready_for_render))
		{
			std::fprintf(stderr,
				"lighting-harness: fake-gpu publication failed frame=%llu "
				"chunk=(%d,%d) previous_mesh=%llu current_mesh=%llu "
				"valid=%d current=%d ready=%d input=%u computed=%u\n",
				static_cast<unsigned long long>(current.frame_index),
				current.chunk_x, current.chunk_z,
				static_cast<unsigned long long>(previous.mesh_revision),
				static_cast<unsigned long long>(current.mesh_revision),
				current.light_buffer_valid ? 1 : 0,
				current.light_current ? 1 : 0,
				current.light_ready_for_render ? 1 : 0,
				static_cast<unsigned int>(current.light_input_version),
				static_cast<unsigned int>(
					current.computed_light_input_version));
			return (FT_ERR_INTERNAL);
		}
		last_observations[previous_index] = current;
		index += 1U;
	}
	return (FT_ERR_SUCCESS);
}

int32_t LightingObservationHarness::validate_latency(uint64_t maximum_frames,
	uint64_t maximum_milliseconds, std::size_t minimum_samples) const noexcept
{
	std::vector<uint64_t> latencies;
	std::vector<uint64_t> authoritative_latencies;
	std::vector<uint64_t> light_latencies;
	std::vector<uint64_t> light_stage_latencies;
	std::vector<uint64_t> mesh_latencies;
	std::vector<uint64_t> visible_latencies;
	std::vector<uint64_t> visible_stage_latencies;
	std::size_t index = 0U;
	bool failed = false;

	if (edits_.empty())
		return (FT_ERR_EMPTY);
	try
	{
		latencies.reserve(edits_.size());
		authoritative_latencies.reserve(edits_.size());
		light_latencies.reserve(edits_.size());
		light_stage_latencies.reserve(edits_.size());
		mesh_latencies.reserve(edits_.size());
		visible_latencies.reserve(edits_.size());
		visible_stage_latencies.reserve(edits_.size());
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	while (index < edits_.size())
	{
		const LightingEditObservation &edit = edits_[index];
		if (edit.superseded)
		{
			index += 1U;
			continue ;
		}
		const bool scanned_counter_rolled_back = edit.end_scanned_cells
			< edit.start_scanned_cells;
		const uint64_t scanned_cells = scanned_counter_rolled_back
			? 0U : edit.end_scanned_cells - edit.start_scanned_cells;
		const uint64_t incremental_completions =
			edit.end_incremental_completed_count
			>= edit.start_incremental_completed_count
			? edit.end_incremental_completed_count
				- edit.start_incremental_completed_count : 0U;
		const uint64_t full_completions = edit.end_full_completed_count
			>= edit.start_full_completed_count
			? edit.end_full_completed_count - edit.start_full_completed_count : 0U;

		if (scanned_counter_rolled_back)
		{
			std::fprintf(stderr,
				"lighting-harness: counter rollback id=%llu "
				"start_scanned=%llu end_scanned=%llu\n",
				static_cast<unsigned long long>(edit.edit_id),
				static_cast<unsigned long long>(edit.start_scanned_cells),
				static_cast<unsigned long long>(edit.end_scanned_cells));
			failed = true;
		}
		if (!edit.completed || edit.authoritative_frame == UINT64_MAX
			|| edit.light_frame == UINT64_MAX
			|| edit.mesh_frame == UINT64_MAX
			|| edit.visible_frame == UINT64_MAX
			|| edit.authoritative_elapsed_milliseconds > maximum_milliseconds
			|| edit.light_elapsed_milliseconds > maximum_milliseconds
			|| edit.mesh_elapsed_milliseconds > maximum_milliseconds
			|| edit.visible_elapsed_milliseconds > maximum_milliseconds
			|| edit.observed_frames > maximum_frames
			|| edit.elapsed_milliseconds > maximum_milliseconds)
		{
			std::fprintf(stderr,
				"lighting-harness: latency gate failed id=%llu "
				"authoritative_frame=%llu light_frame=%llu mesh_frame=%llu "
				"visible_frame=%llu authoritative_ms=%llu light_ms=%llu "
				"mesh_ms=%llu visible_ms=%llu "
				"frames=%llu elapsed_ms=%llu limits=(%llu,%llu)\n",
				static_cast<unsigned long long>(edit.edit_id),
				static_cast<unsigned long long>(edit.authoritative_frame),
				static_cast<unsigned long long>(edit.light_frame),
				static_cast<unsigned long long>(edit.mesh_frame),
				static_cast<unsigned long long>(edit.visible_frame),
				static_cast<unsigned long long>(
					edit.authoritative_elapsed_milliseconds),
				static_cast<unsigned long long>(edit.light_elapsed_milliseconds),
				static_cast<unsigned long long>(edit.mesh_elapsed_milliseconds),
				static_cast<unsigned long long>(
					edit.visible_elapsed_milliseconds),
				static_cast<unsigned long long>(edit.observed_frames),
				static_cast<unsigned long long>(edit.elapsed_milliseconds),
				static_cast<unsigned long long>(maximum_frames),
				static_cast<unsigned long long>(maximum_milliseconds));
			failed = true;
		}
		if (!edit.incremental_light_publication || incremental_completions == 0U
			|| scanned_cells >= LIGHT_HARNESS_MAX_INCREMENTAL_SCANNED_CELLS)
		{
			std::fprintf(stderr,
				"lighting-harness: incremental-light gate failed id=%llu "
				"scanned=%llu incremental_completions=%llu full_completions=%llu "
				"incremental_publication=%d scan_limit=%llu\n",
				static_cast<unsigned long long>(edit.edit_id),
				static_cast<unsigned long long>(scanned_cells),
				static_cast<unsigned long long>(incremental_completions),
				static_cast<unsigned long long>(full_completions),
				edit.incremental_light_publication ? 1 : 0,
				static_cast<unsigned long long>(
					LIGHT_HARNESS_MAX_INCREMENTAL_SCANNED_CELLS));
			failed = true;
		}
		if (edit.completed && edit.elapsed_milliseconds != UINT64_MAX)
		{
			latencies.push_back(edit.elapsed_milliseconds);
			if (edit.authoritative_elapsed_milliseconds != UINT64_MAX)
				authoritative_latencies.push_back(
					edit.authoritative_elapsed_milliseconds);
			if (edit.light_elapsed_milliseconds != UINT64_MAX)
				light_latencies.push_back(edit.light_elapsed_milliseconds);
			if (edit.light_stage_elapsed_milliseconds != UINT64_MAX)
				light_stage_latencies.push_back(
					edit.light_stage_elapsed_milliseconds);
			if (edit.mesh_elapsed_milliseconds != UINT64_MAX)
				mesh_latencies.push_back(edit.mesh_elapsed_milliseconds);
			if (edit.visible_elapsed_milliseconds != UINT64_MAX)
				visible_latencies.push_back(edit.visible_elapsed_milliseconds);
			if (edit.visible_stage_elapsed_milliseconds != UINT64_MAX)
				visible_stage_latencies.push_back(
					edit.visible_stage_elapsed_milliseconds);
		}
		index += 1U;
	}
	if (latencies.empty())
		return (FT_ERR_TIMEOUT);
	if (latencies.size() < minimum_samples)
	{
		std::fprintf(stderr,
			"lighting-harness: insufficient latency samples count=%zu "
			"required=%zu\n", latencies.size(), minimum_samples);
		failed = true;
	}
	std::sort(latencies.begin(), latencies.end());
	report_stage_latency("authoritative", authoritative_latencies);
	report_stage_latency("light_total", light_latencies);
	report_stage_latency("light_stage", light_stage_latencies);
	report_stage_latency("mesh", mesh_latencies);
	report_stage_latency("visible_total", visible_latencies);
	report_stage_latency("visible_stage", visible_stage_latencies);
	{
		const std::size_t sample_count = latencies.size();
		const std::size_t p50_index = percentile_index(sample_count, 50U);
		const std::size_t p90_index = percentile_index(sample_count, 90U);
		const std::size_t p95_index = percentile_index(sample_count, 95U);
		const std::size_t p99_index = percentile_index(sample_count, 99U);
		std::vector<uint64_t> deviations;
		std::size_t deviation_index;

		try
		{
			deviations.reserve(sample_count);
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		deviation_index = 0U;
		while (deviation_index < sample_count)
		{
			const uint64_t median = latencies[p50_index];
			deviations.push_back(latencies[deviation_index] >= median
				? latencies[deviation_index] - median
				: median - latencies[deviation_index]);
			deviation_index += 1U;
		}
		std::sort(deviations.begin(), deviations.end());

		std::fprintf(stderr,
			"lighting-harness: latency samples=%zu p50_ms=%llu "
			"p90_ms=%llu p95_ms=%llu p99_ms=%llu max_ms=%llu mad_ms=%llu "
			"gates=(%llu,%llu,%llu,%llu)\n",
			sample_count,
			static_cast<unsigned long long>(latencies[p50_index]),
			static_cast<unsigned long long>(latencies[p90_index]),
			static_cast<unsigned long long>(latencies[p95_index]),
			static_cast<unsigned long long>(latencies[p99_index]),
			static_cast<unsigned long long>(latencies.back()),
			static_cast<unsigned long long>(
				deviations[percentile_index(sample_count, 50U)]),
			static_cast<unsigned long long>(LIGHT_HARNESS_P50_MAX_MILLISECONDS),
			static_cast<unsigned long long>(LIGHT_HARNESS_P95_MAX_MILLISECONDS),
			static_cast<unsigned long long>(LIGHT_HARNESS_P99_MAX_MILLISECONDS),
			static_cast<unsigned long long>(maximum_milliseconds));
		if (latencies[p50_index] > LIGHT_HARNESS_P50_MAX_MILLISECONDS
			|| latencies[p95_index] > LIGHT_HARNESS_P95_MAX_MILLISECONDS
			|| latencies[p99_index] > LIGHT_HARNESS_P99_MAX_MILLISECONDS)
			failed = true;
	}
	return (failed ? FT_ERR_TIMEOUT : FT_ERR_SUCCESS);
}

int32_t LightingObservationHarness::write_report(const char *path) const
	noexcept
{
	std::FILE *file;
	std::size_t index;
	std::size_t completed_count = 0U;
	std::size_t superseded_count = 0U;
	std::size_t incomplete_count = 0U;
	std::size_t scenario_failure_count = 0U;
	std::vector<uint64_t> latencies;
	std::vector<uint64_t> authoritative_latencies;
	std::vector<uint64_t> light_latencies;
	std::vector<uint64_t> light_stage_latencies;
	std::vector<uint64_t> mesh_latencies;
	std::vector<uint64_t> visible_latencies;
	std::vector<uint64_t> visible_stage_latencies;
	std::vector<uint64_t> scanned_cells;
	std::vector<uint64_t> deviations;
	std::size_t p50_index = 0U;
	std::size_t p90_index = 0U;
	std::size_t p95_index = 0U;
	std::size_t p99_index = 0U;
	std::size_t mad_index = 0U;

	if (path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	file = std::fopen(path, "w");
	if (file == nullptr)
		return (FT_ERR_FILE_OPEN_FAILED);
	try
	{
		latencies.reserve(edits_.size());
		authoritative_latencies.reserve(edits_.size());
		light_latencies.reserve(edits_.size());
		light_stage_latencies.reserve(edits_.size());
		mesh_latencies.reserve(edits_.size());
		visible_latencies.reserve(edits_.size());
		visible_stage_latencies.reserve(edits_.size());
		scanned_cells.reserve(edits_.size());
	}
	catch (const std::bad_alloc &)
	{
		std::fclose(file);
		return (FT_ERR_NO_MEMORY);
	}
	index = 0U;
	while (index < edits_.size())
	{
		if (edits_[index].superseded)
			superseded_count += 1U;
		if (edits_[index].completed && !edits_[index].superseded
			&& edits_[index].elapsed_milliseconds != UINT64_MAX)
		{
			completed_count += 1U;
			latencies.push_back(edits_[index].elapsed_milliseconds);
			if (edits_[index].authoritative_elapsed_milliseconds
				!= UINT64_MAX)
				authoritative_latencies.push_back(
					edits_[index].authoritative_elapsed_milliseconds);
			if (edits_[index].light_elapsed_milliseconds != UINT64_MAX)
				light_latencies.push_back(
					edits_[index].light_elapsed_milliseconds);
			if (edits_[index].light_stage_elapsed_milliseconds != UINT64_MAX)
				light_stage_latencies.push_back(
					edits_[index].light_stage_elapsed_milliseconds);
			if (edits_[index].mesh_elapsed_milliseconds != UINT64_MAX)
				mesh_latencies.push_back(edits_[index].mesh_elapsed_milliseconds);
			if (edits_[index].visible_elapsed_milliseconds != UINT64_MAX)
				visible_latencies.push_back(
					edits_[index].visible_elapsed_milliseconds);
			if (edits_[index].visible_stage_elapsed_milliseconds != UINT64_MAX)
				visible_stage_latencies.push_back(
					edits_[index].visible_stage_elapsed_milliseconds);
			if (edits_[index].end_scanned_cells
				>= edits_[index].start_scanned_cells)
				scanned_cells.push_back(edits_[index].end_scanned_cells
					- edits_[index].start_scanned_cells);
		}
		index += 1U;
	}
	std::sort(latencies.begin(), latencies.end());
	std::sort(authoritative_latencies.begin(), authoritative_latencies.end());
	std::sort(light_latencies.begin(), light_latencies.end());
	std::sort(light_stage_latencies.begin(), light_stage_latencies.end());
	std::sort(mesh_latencies.begin(), mesh_latencies.end());
	std::sort(visible_latencies.begin(), visible_latencies.end());
	std::sort(visible_stage_latencies.begin(), visible_stage_latencies.end());
	std::sort(scanned_cells.begin(), scanned_cells.end());
	index = 0U;
	while (index < scenarios_.size())
	{
		if (scenarios_[index].result != FT_ERR_SUCCESS)
			scenario_failure_count += 1U;
		index += 1U;
	}
	incomplete_count = edits_.size() - completed_count - superseded_count;
	if (!latencies.empty())
	{
		p50_index = percentile_index(latencies.size(), 50U);
		p90_index = percentile_index(latencies.size(), 90U);
		p95_index = percentile_index(latencies.size(), 95U);
		p99_index = percentile_index(latencies.size(), 99U);
		try
		{
			deviations.reserve(latencies.size());
		}
		catch (const std::bad_alloc &)
		{
			std::fclose(file);
			return (FT_ERR_NO_MEMORY);
		}
		index = 0U;
		while (index < latencies.size())
		{
			const uint64_t median = latencies[p50_index];

			deviations.push_back(latencies[index] >= median
				? latencies[index] - median : median - latencies[index]);
			index += 1U;
		}
		std::sort(deviations.begin(), deviations.end());
		mad_index = percentile_index(deviations.size(), 50U);
	}
	if (std::fprintf(file,
			"{\"version\":%llu,\"type\":\"lighting_harness\"," 
			"\"build_variant\":\"%s\",\"platform\":\"%s\","
			"\"compiler\":\"%s\",\"cplusplus\":%lld,"
			"\"pointer_bits\":%zu,\"seed\":\"lighting-harness\","
			"\"clock\":\"steady_clock\",\"render_distance\":%d,"
			"\"frames_retained\":%zu,\"frames_dropped\":%llu,"
			"\"frame_retention_limit\":%zu,"
			"\"online_publication_error\":%d,"
			"\"edit_count\":%zu,\"completed_edits\":%zu,"
			"\"max_edit_frames\":%llu,\"max_edit_ms\":%llu,"
			"\"p50_gate_ms\":%llu,\"p95_gate_ms\":%llu,"
			"\"p99_gate_ms\":%llu}\n",
			static_cast<unsigned long long>(LIGHT_HARNESS_REPORT_VERSION),
			LIGHT_HARNESS_BUILD_VARIANT,
			LIGHT_HARNESS_PLATFORM,
			LIGHT_HARNESS_COMPILER,
			static_cast<long long>(__cplusplus),
			sizeof(void *) * 8U,
			WorldCoordinates::MIN_RENDER_DISTANCE,
			frames_.size(),
			static_cast<unsigned long long>(dropped_frame_count_),
			frame_retention_limit_,
			observation_invariant_error_,
			edits_.size(), completed_count,
			static_cast<unsigned long long>(LIGHT_HARNESS_MAX_EDIT_FRAMES),
			static_cast<unsigned long long>(
				LIGHT_HARNESS_MAX_EDIT_MILLISECONDS),
			static_cast<unsigned long long>(LIGHT_HARNESS_P50_MAX_MILLISECONDS),
			static_cast<unsigned long long>(LIGHT_HARNESS_P95_MAX_MILLISECONDS),
			static_cast<unsigned long long>(LIGHT_HARNESS_P99_MAX_MILLISECONDS)) < 0)
	{
		std::fclose(file);
		return (FT_ERR_IO);
	}
	index = 0U;
	while (index < frames_.size())
	{
		const LightingFrameObservation &frame = frames_[index];
		if (std::fprintf(file,
				"{\"type\":\"frame\",\"frame\":%llu,\"edit\":%llu,"
				"\"chunk_x\":%d,"
				"\"chunk_z\":%d,\"voxel\":%llu,\"light\":%llu,"
				"\"mesh\":%llu,\"content\":%u,\"light_version\":%u,"
				"\"light_input\":%u,\"computed_input\":%u,"
				"\"pending_request\":%llu,"
				"\"pending_voxel\":%llu,\"pending_content\":%u,"
				"\"pending_input\":%u,"
				"\"valid\":%d,\"current\":%d,\"ready\":%d,"
				"\"dirty\":%d,\"pending\":%d,\"light_min\":%u,"
				"\"light_max\":%u,\"light_nonzero\":%u,"
				"\"faces\":%u,\"remesh_queue\":%zu,"
				"\"priority_queue\":%zu,\"interactive_queue\":%zu,"
				"\"active_generation\":%zu,\"playable_failed\":%zu,"
				"\"playable_required\":%zu,\"playable_drawable\":%zu,"
				"\"deferred_edits\":%zu,\"deferred_cursor\":%zu,"
				"\"candidates\":%zu,\"ready_candidates\":%zu,"
				"\"pending_candidates\":%zu,\"retryable\":%zu,"
				"\"failed_candidates\":%zu,\"stream_frame\":%llu,"
				"\"stream_progress_frame\":%llu,"
				"\"starvation_promotions\":%llu,"
				"\"oldest_queue_age\":%llu,\"snapshot_bytes\":%llu,"
				"\"capture_duration_ns\":%llu,\"capture_count\":%llu,"
				"\"light_queue_peak\":%llu,\"geometry_only\":%llu,"
				"\"oldest_result_age_ns\":%llu,\"oldest_pending_age\":%llu,"
				"\"queue_peak\":%llu,"
				"\"stale_remesh\":%llu,\"stale_capture\":%llu,"
				"\"stale_dependency\":%llu,\"stale_pending\":%llu,"
				"\"stale_revision\":%llu,\"completed\":%llu,"
				"\"incremental_completed\":%llu,\"full_completed\":%llu,"
				"\"canceled\":%llu,\"scanned\":%llu,"
				"\"propagated\":%llu,\"stream_error\":%d}\n",
				static_cast<unsigned long long>(frame.frame_index),
				static_cast<unsigned long long>(frame.edit_id), frame.chunk_x,
				frame.chunk_z,
				static_cast<unsigned long long>(frame.voxel_revision),
				static_cast<unsigned long long>(frame.light_revision),
				static_cast<unsigned long long>(frame.mesh_revision),
				static_cast<unsigned int>(frame.content_version),
				static_cast<unsigned int>(frame.light_version),
				static_cast<unsigned int>(frame.light_input_version),
				static_cast<unsigned int>(frame.computed_light_input_version),
				static_cast<unsigned long long>(frame.pending_mesh_request_id),
				static_cast<unsigned long long>(
					frame.pending_mesh_request_voxel_revision),
				static_cast<unsigned int>(
					frame.pending_mesh_request_content_version),
				static_cast<unsigned int>(
					frame.pending_mesh_request_light_input_version),
				frame.light_buffer_valid ? 1 : 0,
				frame.light_current ? 1 : 0,
				frame.light_ready_for_render ? 1 : 0, frame.mesh_dirty ? 1 : 0,
				frame.remesh_pending ? 1 : 0,
				static_cast<unsigned int>(frame.minimum_vertex_light),
				static_cast<unsigned int>(frame.maximum_vertex_light),
				frame.nonzero_light_vertices, frame.visible_face_count,
				frame.remesh_queue_depth, frame.priority_queue_depth,
				frame.interactive_queue_depth, frame.active_generation_count,
				frame.playable_failed_count, frame.playable_required_count,
				frame.playable_drawable_count, frame.deferred_edit_count,
				frame.deferred_edit_cursor, frame.candidate_count,
				frame.ready_count, frame.pending_count, frame.retryable_count,
				frame.failed_count,
				static_cast<unsigned long long>(frame.stream_frame),
				static_cast<unsigned long long>(frame.stream_progress_frame),
				static_cast<unsigned long long>(
					frame.remesh_starvation_promotions),
				static_cast<unsigned long long>(
					frame.oldest_remesh_queue_age),
				static_cast<unsigned long long>(frame.remesh_snapshot_bytes),
				static_cast<unsigned long long>(
					frame.remesh_capture_duration_nanoseconds),
				static_cast<unsigned long long>(frame.remesh_capture_count),
				static_cast<unsigned long long>(frame.remesh_light_queue_peak),
				static_cast<unsigned long long>(
					frame.remesh_geometry_only_count),
				static_cast<unsigned long long>(
					frame.oldest_result_age_nanoseconds),
				static_cast<unsigned long long>(frame.oldest_pending_age),
				static_cast<unsigned long long>(frame.remesh_queue_peak),
				static_cast<unsigned long long>(frame.stale_remesh_count),
				static_cast<unsigned long long>(frame.stale_remesh_capture_count),
				static_cast<unsigned long long>(frame.stale_remesh_dependency_count),
				static_cast<unsigned long long>(frame.stale_remesh_pending_count),
				static_cast<unsigned long long>(frame.stale_remesh_revision_count),
				static_cast<unsigned long long>(frame.remesh_completed_count),
				static_cast<unsigned long long>(
					frame.remesh_incremental_completed_count),
				static_cast<unsigned long long>(frame.remesh_full_completed_count),
				static_cast<unsigned long long>(frame.remesh_canceled_count),
				static_cast<unsigned long long>(frame.remesh_scanned_cells),
				static_cast<unsigned long long>(frame.remesh_propagated_cells),
				frame.stream_last_error) < 0)
		{
			std::fclose(file);
			return (FT_ERR_IO);
		}
		index += 1U;
	}
	index = 0U;
	while (index < edits_.size())
	{
		const LightingEditObservation &edit = edits_[index];
		if (std::fprintf(file,
				"{\"type\":\"edit\",\"edit\":%llu,"
				"\"world_x\":%d,\"world_y\":%d,\"world_z\":%d,"
				"\"start_frame\":%llu,\"authoritative_frame\":%llu,"
				"\"light_frame\":%llu,\"mesh_frame\":%llu,"
				"\"visible_frame\":%llu,"
				"\"authoritative_ms\":%llu,\"light_ms\":%llu,"
				"\"light_stage_ms\":%llu,\"mesh_ms\":%llu,"
				"\"visible_ms\":%llu,\"visible_stage_ms\":%llu,"
				"\"elapsed_ms\":%llu,\"observed_frames\":%llu,"
				"\"start_scanned\":%llu,\"end_scanned\":%llu,"
				"\"start_incremental\":%llu,\"end_incremental\":%llu,"
				"\"start_full\":%llu,\"end_full\":%llu,"
				"\"incremental_publication\":%d,\"superseded\":%d,"
				"\"superseded_frame\":%llu,\"completed\":%d}\n",
				static_cast<unsigned long long>(edit.edit_id), edit.world_x,
				edit.world_y, edit.world_z,
				static_cast<unsigned long long>(edit.start_frame),
				static_cast<unsigned long long>(edit.authoritative_frame),
				static_cast<unsigned long long>(edit.light_frame),
				static_cast<unsigned long long>(edit.mesh_frame),
				static_cast<unsigned long long>(edit.visible_frame),
				static_cast<unsigned long long>(
					edit.authoritative_elapsed_milliseconds),
				static_cast<unsigned long long>(edit.light_elapsed_milliseconds),
				static_cast<unsigned long long>(
					edit.light_stage_elapsed_milliseconds),
				static_cast<unsigned long long>(edit.mesh_elapsed_milliseconds),
				static_cast<unsigned long long>(
					edit.visible_elapsed_milliseconds),
				static_cast<unsigned long long>(
					edit.visible_stage_elapsed_milliseconds),
				static_cast<unsigned long long>(edit.elapsed_milliseconds),
				static_cast<unsigned long long>(edit.observed_frames),
				static_cast<unsigned long long>(edit.start_scanned_cells),
				static_cast<unsigned long long>(edit.end_scanned_cells),
				static_cast<unsigned long long>(
					edit.start_incremental_completed_count),
				static_cast<unsigned long long>(
					edit.end_incremental_completed_count),
				static_cast<unsigned long long>(edit.start_full_completed_count),
				static_cast<unsigned long long>(edit.end_full_completed_count),
				edit.incremental_light_publication ? 1 : 0,
				edit.superseded ? 1 : 0,
				static_cast<unsigned long long>(edit.superseded_frame),
				edit.completed ? 1 : 0) < 0)
		{
			std::fclose(file);
			return (FT_ERR_IO);
		}
		index += 1U;
	}
	index = 0U;
	while (index < scenarios_.size())
	{
		const LightingScenarioObservation &scenario = scenarios_[index];

		if (std::fprintf(file,
				"{\"type\":\"scenario\",\"name\":\"%s\",\"result\":%d}\n",
				scenario.name.c_str(), scenario.result) < 0)
		{
			std::fclose(file);
			return (FT_ERR_IO);
		}
		index += 1U;
	}
	if (std::fprintf(file,
			"{\"type\":\"summary\",\"status\":\"%s\"," 
			"\"edit_count\":%zu,"
			"\"completed_edits\":%zu,\"superseded_edits\":%zu,"
			"\"incomplete_edits\":%zu,"
			"\"frames_retained\":%zu,\"frames_dropped\":%llu,"
			"\"online_publication_error\":%d,"
			"\"storage_error\":%d,"
			"\"scenario_count\":%zu,\"scenario_failures\":%zu,"
			"\"minimum_statistical_samples\":%zu,"
			"\"statistically_insufficient\":%d,"
			"\"p50_ms\":%llu,\"p90_ms\":%llu,\"p95_ms\":%llu,"
			"\"p99_ms\":%llu,\"max_ms\":%llu,\"mad_ms\":%llu,"
			"\"p50_authoritative_ms\":%llu,"
			"\"p90_authoritative_ms\":%llu,"
			"\"p95_authoritative_ms\":%llu,"
			"\"p99_authoritative_ms\":%llu,"
			"\"max_authoritative_ms\":%llu,"
			"\"p50_light_ms\":%llu,\"p90_light_ms\":%llu,"
			"\"p95_light_ms\":%llu,\"p99_light_ms\":%llu,"
			"\"max_light_ms\":%llu,"
			"\"p50_light_stage_ms\":%llu,"
			"\"p90_light_stage_ms\":%llu,"
			"\"p95_light_stage_ms\":%llu,"
			"\"p99_light_stage_ms\":%llu,"
			"\"max_light_stage_ms\":%llu,"
			"\"p50_mesh_ms\":%llu,\"p90_mesh_ms\":%llu,"
			"\"p95_mesh_ms\":%llu,\"p99_mesh_ms\":%llu,"
			"\"max_mesh_ms\":%llu,"
			"\"p50_visible_ms\":%llu,\"p90_visible_ms\":%llu,"
			"\"p95_visible_ms\":%llu,\"p99_visible_ms\":%llu,"
			"\"max_visible_ms\":%llu,"
			"\"p50_visible_stage_ms\":%llu,"
			"\"p90_visible_stage_ms\":%llu,"
			"\"p95_visible_stage_ms\":%llu,"
			"\"p99_visible_stage_ms\":%llu,"
			"\"max_visible_stage_ms\":%llu,"
			"\"scan_limit_cells\":%llu,\"p50_scanned_cells\":%llu,"
			"\"p90_scanned_cells\":%llu,"
			"\"p95_scanned_cells\":%llu,"
			"\"p99_scanned_cells\":%llu,"
			"\"max_scanned_cells\":%llu}\n",
			(storage_error_ == FT_ERR_SUCCESS
				&& observation_invariant_error_ == FT_ERR_SUCCESS
				&& scenario_failure_count == 0U && incomplete_count == 0U)
				? "PASS" : "FAIL",
			edits_.size(), completed_count, superseded_count, incomplete_count,
			frames_.size(),
			static_cast<unsigned long long>(dropped_frame_count_),
			observation_invariant_error_,
			storage_error_,
			scenarios_.size(), scenario_failure_count,
			LIGHT_HARNESS_MINIMUM_STATISTICAL_SAMPLES,
			completed_count < LIGHT_HARNESS_MINIMUM_STATISTICAL_SAMPLES ? 1 : 0,
			latencies.empty() ? 0ULL
				: static_cast<unsigned long long>(latencies[p50_index]),
			latencies.empty() ? 0ULL
				: static_cast<unsigned long long>(latencies[p90_index]),
			latencies.empty() ? 0ULL
				: static_cast<unsigned long long>(latencies[p95_index]),
			latencies.empty() ? 0ULL
				: static_cast<unsigned long long>(latencies[p99_index]),
			latencies.empty() ? 0ULL
				: static_cast<unsigned long long>(latencies.back()),
			deviations.empty() ? 0ULL
				: static_cast<unsigned long long>(deviations[mad_index]),
			percentile_value(authoritative_latencies, 50U),
			percentile_value(authoritative_latencies, 90U),
			percentile_value(authoritative_latencies, 95U),
			percentile_value(authoritative_latencies, 99U),
			authoritative_latencies.empty() ? 0U
				: authoritative_latencies.back(),
			percentile_value(light_latencies, 50U),
			percentile_value(light_latencies, 90U),
			percentile_value(light_latencies, 95U),
			percentile_value(light_latencies, 99U),
			light_latencies.empty() ? 0U : light_latencies.back(),
			percentile_value(light_stage_latencies, 50U),
			percentile_value(light_stage_latencies, 90U),
			percentile_value(light_stage_latencies, 95U),
			percentile_value(light_stage_latencies, 99U),
			light_stage_latencies.empty() ? 0U : light_stage_latencies.back(),
			percentile_value(mesh_latencies, 50U),
			percentile_value(mesh_latencies, 90U),
			percentile_value(mesh_latencies, 95U),
			percentile_value(mesh_latencies, 99U),
			mesh_latencies.empty() ? 0U : mesh_latencies.back(),
			percentile_value(visible_latencies, 50U),
			percentile_value(visible_latencies, 90U),
			percentile_value(visible_latencies, 95U),
			percentile_value(visible_latencies, 99U),
			visible_latencies.empty() ? 0U : visible_latencies.back(),
			percentile_value(visible_stage_latencies, 50U),
			percentile_value(visible_stage_latencies, 90U),
			percentile_value(visible_stage_latencies, 95U),
			percentile_value(visible_stage_latencies, 99U),
			visible_stage_latencies.empty() ? 0U
				: visible_stage_latencies.back(),
			static_cast<unsigned long long>(
				LIGHT_HARNESS_MAX_INCREMENTAL_SCANNED_CELLS),
			percentile_value(scanned_cells, 50U),
			percentile_value(scanned_cells, 90U),
			percentile_value(scanned_cells, 95U),
			percentile_value(scanned_cells, 99U),
			scanned_cells.empty() ? 0U : scanned_cells.back()) < 0)
	{
		std::fclose(file);
		return (FT_ERR_IO);
	}
	if (std::fclose(file) != 0)
		return (FT_ERR_IO);
	return (FT_ERR_SUCCESS);
}

int32_t LightingObservationHarness::set_frame_retention_limit(
	std::size_t maximum_frames) noexcept
{
	frame_retention_limit_ = maximum_frames;
	while (frame_retention_limit_ != 0U
		&& frames_.size() > frame_retention_limit_)
	{
		frames_.pop_front();
		dropped_frame_count_ += 1U;
	}
	return (FT_ERR_SUCCESS);
}

const std::deque<LightingFrameObservation> &
	LightingObservationHarness::frames() const noexcept
{
	return (frames_);
}

const std::vector<LightingEditObservation> &
	LightingObservationHarness::edits() const noexcept
{
	return (edits_);
}

LightingTestHarness::LightingTestHarness()
{
}

LightingTestHarness::LightingTestHarness(const LightingTestHarness &other)
	: IValidator(other)
{
	(void)other;
}

LightingTestHarness::~LightingTestHarness()
{
}

LightingTestHarness &LightingTestHarness::operator=(
	const LightingTestHarness &other)
{
	(void)other;
	return (*this);
}

int LightingTestHarness::validate_initial_light_oracle() noexcept
{
	World world;
	LightingObservationHarness harness;
	uint64_t frame = 0U;
	int32_t error_code = FT_ERR_SUCCESS;
	int32_t first_failure = FT_ERR_SUCCESS;
	uint32_t scenario_count = 0U;
	uint32_t scenario_failure_count = 0U;
	static const int32_t offsets[8][2] = {
		{-1, 0}, {1, 0}, {0, -1}, {0, 1},
		{-1, -1}, {-1, 1}, {1, -1}, {1, 1}
	};
	static const char *scenario_names[8] = {
		"startup-west", "startup-east", "startup-north", "startup-south",
		"startup-northwest", "startup-southwest", "startup-northeast",
		"startup-southeast"
	};
	LightingHarnessWatchdogScope watchdog(LIGHT_HARNESS_WATCHDOG_REPORT);

	if (watchdog.start() != FT_ERR_SUCCESS)
		return (1);
	watchdog.heartbeat("startup_initialize", frame, 0U);

	error_code = validate_reference_oracle_fixtures();
	record_scenario_result(harness, error_code, "reference-oracle-fixtures",
		scenario_count, scenario_failure_count, first_failure);
	error_code = world.initialize("lighting-harness-startup");
	if (error_code != FT_ERR_SUCCESS)
		record_scenario_result(harness, error_code, "startup-initialize",
			scenario_count, scenario_failure_count, first_failure);
	else
	{
		error_code = wait_for_startup(world, frame);
		if (error_code != FT_ERR_SUCCESS)
			record_scenario_result(harness, error_code,
				"startup-center-readiness", scenario_count,
				scenario_failure_count, first_failure);
		else
		{
			int32_t step_error;

			step_error = harness.observe(world, 0, 0, frame, 0U);
			if (step_error == FT_ERR_SUCCESS)
				step_error = compare_light_to_oracle(world, 0, 0,
					"startup center");
			record_scenario_result(harness, step_error, "startup-center",
				scenario_count, scenario_failure_count, first_failure);
			step_error = wait_for_startup_neighborhood(world, frame);
			if (step_error != FT_ERR_SUCCESS)
				record_scenario_result(harness, step_error,
					"startup-neighborhood-readiness", scenario_count,
					scenario_failure_count, first_failure);
			else
			{
				int32_t index = 0;

				while (index < 8)
				{
					step_error = harness.observe(world, offsets[index][0],
						offsets[index][1], frame, 0U);
					if (step_error == FT_ERR_SUCCESS)
						step_error = compare_light_to_oracle(world,
							offsets[index][0], offsets[index][1],
							scenario_names[index]);
					record_scenario_result(harness, step_error,
						scenario_names[index], scenario_count,
						scenario_failure_count, first_failure);
					index += 1;
				}
			}
		}
	}
	error_code = harness.validate_publication_invariants();
	record_scenario_result(harness, error_code, "startup-publication-invariants",
		scenario_count, scenario_failure_count, first_failure);
	error_code = harness.write_report(LIGHT_HARNESS_STARTUP_REPORT);
	retain_first_failure(error_code, "startup-report", first_failure);
	if (first_failure != FT_ERR_SUCCESS)
	{
		error_code = harness.write_report(LIGHT_HARNESS_STARTUP_FAILURE_REPORT);
		retain_first_failure(error_code, "startup-failure-report", first_failure);
	}
	world.destroy();
	if (first_failure != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"lighting-harness: startup oracle failed error=%d frame=%llu "
			"scenarios=%u scenario_failures=%u\n", first_failure,
			static_cast<unsigned long long>(frame),
			static_cast<unsigned int>(scenario_count),
			static_cast<unsigned int>(scenario_failure_count));
		return (1);
	}
	std::fprintf(stderr,
		"lighting-harness: startup oracle passed scenarios=%u frame=%llu\n",
		static_cast<unsigned int>(scenario_count),
		static_cast<unsigned long long>(frame));
	return (0);
}

int LightingTestHarness::validate_edit_matrix() noexcept
{
	World world;
	LightingObservationHarness harness;
	uint64_t frame = 0U;
	uint64_t edit_id = 1U;
	uint32_t emissive_block_ids[3] = {0U, 0U, 0U};
	uint32_t emissive_registered_count = 0U;
	int32_t error_code = FT_ERR_SUCCESS;
	int32_t first_failure = FT_ERR_SUCCESS;
	uint32_t scenario_count = 0U;
	uint32_t scenario_failure_count = 0U;
	int32_t side;
	LightingHarnessWatchdogScope watchdog(LIGHT_HARNESS_WATCHDOG_REPORT);

	if (watchdog.start() != FT_ERR_SUCCESS)
		return (1);
	watchdog.heartbeat("edit_matrix_initialize", frame, edit_id);

	while (emissive_registered_count < 3U
		&& error_code == FT_ERR_SUCCESS)
	{
		static const char *emissive_names[3] = {
			"test:lighting_harness_emissive_1",
			"test:lighting_harness_emissive_7",
			"test:lighting_harness_emissive_15"
		};
		static const uint8_t emissive_levels[3] = {1U, 7U, 15U};

		error_code = register_harness_emissive_block(
			&emissive_block_ids[emissive_registered_count],
			emissive_names[emissive_registered_count],
			emissive_levels[emissive_registered_count]);
		if (error_code == FT_ERR_SUCCESS)
			emissive_registered_count += 1U;
	}
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"lighting-harness: emissive registration failed error=%d\n",
			error_code);
		retain_first_failure(error_code, "emissive-registration",
			first_failure);
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = world.initialize("lighting-harness-edits");
	if (error_code == FT_ERR_SUCCESS)
		error_code = wait_for_startup(world, frame);
	if (error_code != FT_ERR_SUCCESS)
		record_scenario_result(harness, error_code, "edit-matrix-startup",
			scenario_count, scenario_failure_count, first_failure);
	else
	{
		error_code = run_transactional_failure_inputs(world, harness, frame);
		record_scenario_result(harness, error_code,
			"transactional-failure-inputs", scenario_count,
			scenario_failure_count, first_failure);
		error_code = run_allocation_failure_sweep(world);
		record_scenario_result(harness, error_code,
			"allocation-failure-rollback", scenario_count,
			scenario_failure_count, first_failure);
		error_code = run_version_rollover_checks();
		record_scenario_result(harness, error_code, "version-rollover",
			scenario_count, scenario_failure_count, first_failure);
		error_code = run_superseded_transition(world, harness, edit_id, 4, 4,
			frame);
		record_scenario_result(harness, error_code, "superseded-transition",
			scenario_count, scenario_failure_count, first_failure);
		error_code = run_supersession_permutations(world, harness, edit_id,
			frame);
		record_scenario_result(harness, error_code,
			"supersession-permutations", scenario_count,
			scenario_failure_count, first_failure);
		error_code = run_queue_pressure(world, harness, frame);
		record_scenario_result(harness, error_code, "queue-pressure",
			scenario_count, scenario_failure_count, first_failure);
		error_code = run_simultaneous_opposite_border_edits(world, harness,
			edit_id, frame);
		record_scenario_result(harness, error_code,
			"simultaneous-opposite-border-edits", scenario_count,
			scenario_failure_count, first_failure);
		side = 0;
		while (side < 4)
		{
			const int32_t world_x = side == 0 ? 0 : side == 1 ? 15 : 4;
			const int32_t world_z = side == 2 ? 0 : side == 3 ? 15 : 4;
			double surface;
			int32_t world_y;
			int32_t step_error = FT_ERR_SUCCESS;
			const char *scenario = side == 0 ? "west-border"
				: side == 1 ? "east-border"
				: side == 2 ? "north-border" : "south-border";

			if (!world.surface_top_at(world_x, world_z, &surface))
				step_error = FT_ERR_NOT_FOUND;
			else
			{
				world_y = static_cast<int32_t>(surface + 1.0);
				step_error = run_edit(world, harness, edit_id++, world_x,
					world_y, world_z, LIGHT_HARNESS_STONE, frame);
				if (step_error == FT_ERR_SUCCESS)
					step_error = run_edit(world, harness, edit_id++, world_x,
					world_y, world_z, GAME_VOXEL_AIR_BLOCK, frame);
				if (step_error == FT_ERR_SUCCESS && side < 2)
					step_error = wait_for_neighbor_oracle(world, harness,
						side == 0 ? -1 : 1, 0, frame,
						"west/east border neighbor");
				if (step_error == FT_ERR_SUCCESS && side >= 2)
					step_error = wait_for_neighbor_oracle(world, harness, 0,
						side == 2 ? -1 : 1, frame,
						"north/south border neighbor");
			}
			record_scenario_result(harness, step_error, scenario, scenario_count,
				scenario_failure_count, first_failure);
			side += 1;
		}
		error_code = run_transition(world, harness, edit_id, 15, 15,
			LIGHT_HARNESS_STONE, frame, "corner-transition");
		record_scenario_result(harness, error_code, "corner-transition",
			scenario_count, scenario_failure_count, first_failure);
		if (error_code == FT_ERR_SUCCESS)
			error_code = wait_for_neighbor_oracle(world, harness, 1, 0, frame,
				"corner east neighbor");
		record_scenario_result(harness, error_code, "corner-east-neighbor",
			scenario_count, scenario_failure_count, first_failure);
		if (error_code == FT_ERR_SUCCESS)
			error_code = wait_for_neighbor_oracle(world, harness, 0, 1, frame,
				"corner south neighbor");
		record_scenario_result(harness, error_code, "corner-south-neighbor",
			scenario_count, scenario_failure_count, first_failure);
		error_code = run_transition(world, harness, edit_id, 4, 4,
			VOXEL_GENERATOR_WATER_BLOCK, frame, "water-transition");
		record_scenario_result(harness, error_code, "water-transition",
			scenario_count, scenario_failure_count, first_failure);
		error_code = run_transition(world, harness, edit_id, 15, 4,
			VOXEL_GENERATOR_OAK_LEAVES_BLOCK, frame,
			"transparent-transition");
		record_scenario_result(harness, error_code, "transparent-transition",
			scenario_count, scenario_failure_count, first_failure);
		error_code = run_section_boundary_transition(world, harness, edit_id,
			4, 4, LIGHT_HARNESS_STONE, frame);
		record_scenario_result(harness, error_code, "section-boundary-transition",
			scenario_count, scenario_failure_count, first_failure);
		if (emissive_registered_count != 0U)
		{
			uint32_t emissive_index = 0U;

			while (emissive_index < emissive_registered_count)
			{
				error_code = run_emissive_transition(world, harness, edit_id,
					frame, emissive_block_ids[emissive_index]);
				if (emissive_index == 0U)
					record_scenario_result(harness, error_code,
						"emissive-level-1", scenario_count,
						scenario_failure_count, first_failure);
				else if (emissive_index == 1U)
					record_scenario_result(harness, error_code,
						"emissive-level-7", scenario_count,
						scenario_failure_count, first_failure);
				else
					record_scenario_result(harness, error_code,
						"emissive-level-15", scenario_count,
						scenario_failure_count, first_failure);
				emissive_index += 1U;
			}
		}
	}
	error_code = harness.validate_publication_invariants();
	record_scenario_result(harness, error_code, "publication-invariants",
		scenario_count, scenario_failure_count, first_failure);
	error_code = harness.validate_latency(
			LIGHT_HARNESS_MAX_EDIT_FRAMES, LIGHT_HARNESS_MAX_EDIT_MILLISECONDS);
	record_scenario_result(harness, error_code, "latency-gates",
		scenario_count, scenario_failure_count, first_failure);
	error_code = harness.write_report(LIGHT_HARNESS_EDIT_REPORT);
	retain_first_failure(error_code, "edit-report", first_failure);
	error_code = first_failure;
	if (error_code != FT_ERR_SUCCESS)
	{
		int32_t report_error = harness.write_report(
			LIGHT_HARNESS_FAILURE_REPORT);
		const std::deque<LightingFrameObservation> &frames = harness.frames();
		if (report_error != FT_ERR_SUCCESS)
			std::fprintf(stderr,
				"lighting-harness: failure report could not be written error=%d\n",
				report_error);
		if (!frames.empty())
		{
			const LightingFrameObservation &last = frames.back();
			std::fprintf(stderr,
				"lighting-harness: last observation frame="
				FT_UINT64_DECIMAL_FORMAT " edit=" FT_UINT64_DECIMAL_FORMAT
				" chunk=(%d,%d) voxel=" FT_UINT64_DECIMAL_FORMAT
				" light=" FT_UINT64_DECIMAL_FORMAT " mesh="
				FT_UINT64_DECIMAL_FORMAT " content=%u light_version=%u "
				"input=%u computed=%u valid=%d current=%d ready=%d "
				"dirty=%d pending=%llu light_range=%u..%u faces=%u\n",
				last.frame_index, last.edit_id, last.chunk_x, last.chunk_z,
				last.voxel_revision, last.light_revision, last.mesh_revision,
				static_cast<unsigned int>(last.content_version),
				static_cast<unsigned int>(last.light_version),
				static_cast<unsigned int>(last.light_input_version),
				static_cast<unsigned int>(last.computed_light_input_version),
				last.light_buffer_valid ? 1 : 0, last.light_current ? 1 : 0,
				last.light_ready_for_render ? 1 : 0, last.mesh_dirty ? 1 : 0,
				static_cast<unsigned long long>(last.pending_mesh_request_id),
				static_cast<unsigned int>(last.minimum_vertex_light),
				static_cast<unsigned int>(last.maximum_vertex_light),
				last.visible_face_count);
		}
	}
	world.destroy();
	while (emissive_registered_count > 0U)
	{
		int32_t unregister_error;

		emissive_registered_count -= 1U;
		unregister_error = voxel_unregister_block(
			emissive_block_ids[emissive_registered_count]);

		if (unregister_error != FT_ERR_SUCCESS)
		{
			std::fprintf(stderr,
				"lighting-harness: emissive test block cleanup failed error=%d\n",
				unregister_error);
			if (error_code == FT_ERR_SUCCESS)
				error_code = unregister_error;
		}
	}
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"lighting-harness: edit matrix failed error=%d edits="
			FT_UINT64_DECIMAL_FORMAT " scenarios=%u scenario_failures=%u "
			"frame=" FT_UINT64_DECIMAL_FORMAT "\n", error_code, edit_id - 1U,
			static_cast<unsigned int>(scenario_count),
			static_cast<unsigned int>(scenario_failure_count), frame);
		return (1);
	}
	std::printf("lighting-harness: edit matrix passed edits="
		FT_UINT64_DECIMAL_FORMAT " scenarios=%u frames="
		FT_UINT64_DECIMAL_FORMAT "\n", edit_id - 1U,
		static_cast<unsigned int>(scenario_count), frame);
	return (0);
}
int LightingTestHarness::validate_stress(uint32_t edit_count,
	const char *report_suffix) noexcept
{
	World world;
	LightingObservationHarness harness;
	uint64_t frame = 0U;
	uint64_t edit_id = 1U;
	uint32_t index;
	double surface;
	int32_t stress_y[6] = {0, 0, 0, 0, 0, 0};
	static const int32_t stress_positions[6][2] = {
		{4, 4}, {0, 4}, {15, 4}, {4, 0}, {4, 15}, {15, 15}
	};
	static const uint32_t stress_position_count = 6U;
	int32_t world_y;
	int32_t error_code = FT_ERR_SUCCESS;
	int32_t first_failure = FT_ERR_SUCCESS;
	uint32_t scenario_count = 0U;
	uint32_t scenario_failure_count = 0U;
	char report_path[128];
	char failure_path[128];
	int report_length;

	if (edit_count == 0U || report_suffix == nullptr
		|| report_suffix[0] == '\0')
		return (1);
	if (harness.set_frame_retention_limit(8192U) != FT_ERR_SUCCESS)
		return (1);
	report_length = std::snprintf(report_path, sizeof(report_path),
		"minecraft_lighting_%s_%s.jsonl", report_suffix,
		#if defined(LIBFT_ENABLE_ANALYTICS)
		"analytics"
		#else
		"normal"
		#endif
	);
	if (report_length < 0
		|| static_cast<std::size_t>(report_length) >= sizeof(report_path))
		return (1);
	report_length = std::snprintf(failure_path, sizeof(failure_path),
		"minecraft_lighting_%s_failure_%s.jsonl", report_suffix,
		#if defined(LIBFT_ENABLE_ANALYTICS)
		"analytics"
		#else
		"normal"
		#endif
	);
	if (report_length < 0
		|| static_cast<std::size_t>(report_length) >= sizeof(failure_path))
		return (1);
	std::fprintf(stderr,
		"[Validator] lighting-%s: begin edits=%u report=%s\n",
		report_suffix, static_cast<unsigned int>(edit_count), report_path);
	LightingHarnessWatchdogScope watchdog(LIGHT_HARNESS_WATCHDOG_REPORT);

	if (watchdog.start() != FT_ERR_SUCCESS)
		return (1);
	watchdog.heartbeat("stress_initialize", frame, edit_id);
	error_code = world.initialize("lighting-harness-stress");
	if (error_code == FT_ERR_SUCCESS)
		error_code = wait_for_startup(world, frame);
	if (error_code == FT_ERR_SUCCESS)
		error_code = wait_for_startup_neighborhood(world, frame);
	if (error_code == FT_ERR_SUCCESS)
	{
		index = 0U;
		while (index < stress_position_count
			&& error_code == FT_ERR_SUCCESS)
		{
			uint32_t existing_block_id = GAME_VOXEL_AIR_BLOCK;

			if (!world.surface_top_at(stress_positions[index][0],
				stress_positions[index][1], &surface))
				error_code = FT_ERR_NOT_FOUND;
			else
			{
				world_y = static_cast<int32_t>(surface + 1.0);
				while (world_y < GAME_VOXEL_CHUNK_HEIGHT)
				{
					if (!world.block_id_at(stress_positions[index][0], world_y,
						stress_positions[index][1], &existing_block_id))
					{
						error_code = FT_ERR_NOT_FOUND;
						break ;
					}
					if (existing_block_id == GAME_VOXEL_AIR_BLOCK)
						break ;
					world_y += 1;
				}
				if (error_code == FT_ERR_SUCCESS
					&& world_y >= GAME_VOXEL_CHUNK_HEIGHT)
					error_code = FT_ERR_NOT_FOUND;
				if (error_code == FT_ERR_SUCCESS)
					stress_y[index] = world_y;
			}
			index += 1U;
		}
	}
	if (error_code != FT_ERR_SUCCESS)
		record_scenario_result(harness, error_code, "stress-startup",
			scenario_count, scenario_failure_count, first_failure);
	index = 0U;
	while (index < edit_count)
	{
		const uint32_t position_index = (index / 2U)
			% stress_position_count;
		const uint32_t block_id = (index & 1U) == 0U
			? LIGHT_HARNESS_STONE : GAME_VOXEL_AIR_BLOCK;
		lighting_watchdog_heartbeat("stress_edit", frame, edit_id);
		const int32_t step_error = run_edit(world, harness, edit_id++,
			stress_positions[position_index][0], stress_y[position_index],
			stress_positions[position_index][1], block_id, frame);

		record_scenario_result(harness, step_error, "stress-edit",
			scenario_count, scenario_failure_count, first_failure);
		if (step_error == FT_ERR_SUCCESS && (index % 32U) == 31U)
		{
			const int32_t oracle_error = compare_light_to_oracle(world, 0, 0,
				"stress checkpoint");

			record_scenario_result(harness, oracle_error,
				"stress-oracle-checkpoint", scenario_count,
				scenario_failure_count, first_failure);
		}
		if ((index % 100U) == 99U)
		{
			const World::StreamDiagnostics diagnostics =
				world.stream_diagnostics();

			std::fprintf(stderr,
				"[Validator] lighting-%s: progress edits=%u/%u "
				"frame=" FT_UINT64_DECIMAL_FORMAT " queue=%zu "
				"interactive=%zu active=%zu stale=%llu scanned=%llu\n",
				report_suffix, index + 1U, edit_count, frame,
				diagnostics.remesh_priority_queue_depth,
				diagnostics.interactive_remesh_queue_depth,
				diagnostics.active_generation_count,
				static_cast<unsigned long long>(
					diagnostics.stale_remesh_result_count),
				static_cast<unsigned long long>(diagnostics.remesh_scanned_cells));
			std::fflush(stderr);
		}
		index += 1U;
	}
	error_code = harness.validate_publication_invariants();
	record_scenario_result(harness, error_code,
		"stress-publication-invariants", scenario_count,
		scenario_failure_count, first_failure);
		error_code = harness.validate_latency(
		LIGHT_HARNESS_MAX_EDIT_FRAMES, LIGHT_HARNESS_MAX_EDIT_MILLISECONDS,
		edit_count >= 1000U ? 1000U : edit_count);
	record_scenario_result(harness, error_code, "stress-latency-gates",
		scenario_count, scenario_failure_count, first_failure);
	error_code = harness.write_report(report_path);
	retain_first_failure(error_code, "stress-report", first_failure);
	if (first_failure != FT_ERR_SUCCESS)
	{
		error_code = harness.write_report(failure_path);
		retain_first_failure(error_code, "stress-failure-report",
			first_failure);
	}
	world.destroy();
	std::fprintf(stderr,
		"[Validator] lighting-%s: %s edits=%u frames="
		FT_UINT64_DECIMAL_FORMAT " scenarios=%u scenario_failures=%u\n",
		report_suffix,
		first_failure == FT_ERR_SUCCESS ? "passed" : "failed",
		static_cast<unsigned int>(edit_count), frame,
		static_cast<unsigned int>(scenario_count),
		static_cast<unsigned int>(scenario_failure_count));
	return (first_failure == FT_ERR_SUCCESS ? 0 : 1);
}

int LightingTestHarness::validate_lifecycle() noexcept
{
	static const uint32_t lifecycle_cycles = 3U;
	World world;
	uint32_t cycle;
	uint32_t scenario_count = 0U;
	uint32_t scenario_failure_count = 0U;
	int32_t first_failure = FT_ERR_SUCCESS;
	LightingHarnessWatchdogScope watchdog(LIGHT_HARNESS_WATCHDOG_REPORT);

	if (watchdog.start() != FT_ERR_SUCCESS)
		return (1);
	watchdog.heartbeat("lifecycle_initialize", 0U, 0U);

	std::fprintf(stderr,
		"[Validator] lighting-lifecycle: begin cycles=%u\n",
		static_cast<unsigned int>(lifecycle_cycles));
	cycle = 0U;
	while (cycle < lifecycle_cycles)
	{
		uint64_t frame = 0U;
		double surface = 0.0;
		lighting_watchdog_heartbeat("lifecycle_cycle", frame, cycle);
		int32_t error_code = world.initialize("lighting-harness-lifecycle");

		if (error_code == FT_ERR_SUCCESS)
			error_code = wait_for_startup(world, frame);
		if (error_code == FT_ERR_SUCCESS
			&& !world.surface_top_at(4, 4, &surface))
			error_code = FT_ERR_NOT_FOUND;
		if (error_code == FT_ERR_SUCCESS)
		{
			const int32_t world_y = static_cast<int32_t>(surface + 1.0);

			error_code = world.place_block_at(4, world_y, 4,
				LIGHT_HARNESS_STONE);
		}
		if (error_code == FT_ERR_SUCCESS)
		{
			/* Destroy while an edit/remesh may still be queued.  The next
			 * iteration reinitializes the same object, which also catches
			 * workers or retired buffers leaking across sessions. */
			world.destroy();
			if (world.find_chunk(0, 0) != nullptr)
				error_code = FT_ERR_INTERNAL;
			if (error_code == FT_ERR_SUCCESS)
				world.destroy();
		}
		else
			world.destroy();
		std::fprintf(stderr,
			"lighting-harness: lifecycle cycle=%u result=%d\n",
			static_cast<unsigned int>(cycle), error_code);
		scenario_count += 1U;
		if (error_code != FT_ERR_SUCCESS)
			scenario_failure_count += 1U;
		retain_first_failure(error_code, "lifecycle-shutdown", first_failure);
		cycle += 1U;
	}

	{
		uint64_t frame = 0U;
		const int32_t recentered_x = 2 * GAME_VOXEL_CHUNK_WIDTH;
		const int32_t recentered_z = 2 * GAME_VOXEL_CHUNK_DEPTH;
		int32_t error_code = world.initialize("lighting-harness-recenter");
		bool recentered_ready = false;

		if (error_code == FT_ERR_SUCCESS)
		{
			while (frame < LIGHT_HARNESS_MAX_STARTUP_FRAMES)
			{
				const WorldChunk *chunk = world.find_chunk(2, 2);

				lighting_watchdog_heartbeat("recenter_update", frame, 0U);
				if (chunk_is_ready_for_oracle(chunk))
				{
					recentered_ready = true;
					break ;
				}
				error_code = world.update_around(
					static_cast<double>(recentered_x),
					static_cast<double>(recentered_z), 0,
					WorldCoordinates::MIN_RENDER_DISTANCE);
				if (error_code != FT_ERR_SUCCESS)
					break ;
				frame += 1U;
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			if (error_code == FT_ERR_SUCCESS && !recentered_ready)
				error_code = FT_ERR_TIMEOUT;
			if (error_code == FT_ERR_SUCCESS)
				error_code = compare_light_to_oracle(world, 2, 2,
					"recentered startup");
		}
		world.destroy();
		scenario_count += 1U;
		if (error_code != FT_ERR_SUCCESS)
			scenario_failure_count += 1U;
		retain_first_failure(error_code, "lifecycle-recenter", first_failure);
	}

	{
		uint64_t frame = 0U;
		const int32_t far_chunk = WorldCoordinates::CACHE_CHUNK_RADIUS + 2;
		const int32_t far_x = far_chunk * GAME_VOXEL_CHUNK_WIDTH;
		WorldGenerationPipeline::WorldChunkSnapshot stale_snapshot;
		int32_t error_code = world.initialize(
			"lighting-harness-eviction");
		bool old_chunk_missing = false;
		bool stale_snapshot_rejected = false;

		if (error_code == FT_ERR_SUCCESS)
			error_code = wait_for_startup(world, frame);
		if (error_code == FT_ERR_SUCCESS)
		{
			lighting_watchdog_heartbeat("eviction_update", frame, 0U);
			error_code = world.update_around(
				static_cast<double>(far_x), 0.0, 0,
				WorldCoordinates::MIN_RENDER_DISTANCE);
			old_chunk_missing = world.find_chunk(0, 0) == nullptr;
			if (!old_chunk_missing)
				error_code = FT_ERR_INTERNAL;
			if (error_code == FT_ERR_SUCCESS)
			{
				const int32_t snapshot_error =
					world.capture_remesh_snapshot(0, 0,
						stale_snapshot);

				stale_snapshot_rejected = snapshot_error != FT_ERR_SUCCESS;
				if (!stale_snapshot_rejected)
					error_code = FT_ERR_INTERNAL;
			}
		}
		if (error_code == FT_ERR_SUCCESS)
		{
			frame = 0U;
			error_code = wait_for_startup(world, frame);
			if (error_code == FT_ERR_SUCCESS)
				error_code = compare_light_to_oracle(world, 0, 0,
					"evicted-neighbor-regenerated");
		}
		world.destroy();
		std::fprintf(stderr,
			"lighting-harness: eviction/reload result=%d old_missing=%d "
			"snapshot_rejected=%d\n", error_code, old_chunk_missing ? 1 : 0,
			stale_snapshot_rejected ? 1 : 0);
		scenario_count += 1U;
		if (error_code != FT_ERR_SUCCESS)
			scenario_failure_count += 1U;
		retain_first_failure(error_code, "lifecycle-eviction-reload",
			first_failure);
	}
	std::fprintf(stderr,
		"[Validator] lighting-lifecycle: %s scenarios=%u "
		"scenario_failures=%u\n",
		first_failure == FT_ERR_SUCCESS ? "passed" : "failed",
		static_cast<unsigned int>(scenario_count),
		static_cast<unsigned int>(scenario_failure_count));
	return (first_failure == FT_ERR_SUCCESS ? 0 : 1);
}

int LightingTestHarness::validate() const
{
	int error_code;

	std::fprintf(stderr, "[Validator] lighting-harness: begin\n");
	error_code = LightingTestHarness::validate_initial_light_oracle();
	if (error_code == 0)
		error_code = LightingTestHarness::validate_edit_matrix();
	std::fprintf(stderr, "[Validator] lighting-harness: %s\n",
		error_code == 0 ? "passed" : "failed");
	return (error_code);
}
