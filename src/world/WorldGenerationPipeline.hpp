#ifndef WORLD_GENERATION_PIPELINE_HPP
# define WORLD_GENERATION_PIPELINE_HPP

# include "../../Libft/Modules/Voxel/voxel_api.hpp"
# include "../../src/chunks/WorldChunk.hpp"
# include <atomic>
# include <array>
# include <condition_variable>
# include <cstdint>
# include <deque>
# include <functional>
# include <memory>
# include <mutex>
# include <new>
# include <string>
# include <thread>
# include <vector>

class WorldGenerationWorkerLoop;

class WorldGenerationPipeline
{
  public:
	static constexpr int32_t LIGHT_SNAPSHOT_HALO = 15;
	enum class WorldGenerationOperation : uint8_t
	{
		STREAM = 0,
		REMESH = 1,
		REGENERATE = 2
	};

	struct							WorldDeferredBlockEdit
	{
		int32_t						world_x;
		int32_t						world_y;
		int32_t						world_z;
		uint32_t					block_id;
		uint64_t					request_id;
		uint64_t					sequence;
	};

	struct							WorldChunkSnapshot
	{
		int32_t						chunk_x;
		int32_t						chunk_z;
		game_voxel_generation_metadata generation_metadata;
		std::vector<uint32_t> blocks;
		std::vector<uint32_t> west_border;
		std::vector<uint32_t> east_border;
		std::vector<uint32_t> north_border;
		std::vector<uint32_t> south_border;
		ft_bool west_border_valid;
		ft_bool east_border_valid;
		ft_bool north_border_valid;
		ft_bool south_border_valid;
		std::vector<uint32_t> lighting_blocks;
		std::vector<uint8_t> lighting_existing_light;
		std::vector<uint32_t> lighting_ring_offsets;
		std::vector<uint8_t> existing_light;
		ft_bool existing_light_valid;
		ft_bool lighting_halo_valid;
		ft_bool lighting_cardinal_halo_valid;
		ft_bool lighting_halo_blocked;
		ft_bool dependency_valid;
		int32_t dependency_chunk_x;
		int32_t dependency_chunk_z;
		uint64_t dependency_voxel_revision;
		uint64_t dependency_light_revision;
		uint16_t dependency_content_version;
		uint16_t dependency_light_input_version;
	};

	struct							IncrementalLightSeed
	{
		ft_bool						additive;
		ft_bool						removal;
		ft_bool						external_source;
		int32_t						local_x;
		int32_t						local_y;
		int32_t						local_z;
		uint32_t					old_block_id;
		uint32_t					new_block_id;
		uint8_t						old_source_light;
	};

	struct IncrementalLightNode
	{
		int32_t x;
		int32_t y;
		int32_t z;
	};

	struct IncrementalRemovalNode
	{
		int32_t x;
		int32_t y;
		int32_t z;
		uint8_t channel;
		uint8_t level;
	};

	struct IncrementalLightDelta
	{
		/* The logical record is five bytes: x, y (little-endian), z, and
		 * packed light.  Keep it explicitly byte-packed instead of relying on
		 * compiler struct padding while it crosses the worker result queue. */
		uint8_t encoded[5];

		IncrementalLightDelta() noexcept
			: encoded{0U, 0U, 0U, 0U, 0U}
		{
		}

		void set(uint8_t local_x, uint16_t local_y, uint8_t local_z,
			uint8_t packed_light) noexcept
		{
			this->encoded[0] = local_x;
			this->encoded[1] = static_cast<uint8_t>(local_y & 0xffU);
			this->encoded[2] = static_cast<uint8_t>((local_y >> 8U) & 0xffU);
			this->encoded[3] = local_z;
			this->encoded[4] = packed_light;
		}

		uint8_t local_x() const noexcept
		{
			return (this->encoded[0]);
		}

		uint16_t local_y() const noexcept
		{
			return (static_cast<uint16_t>(this->encoded[1])
				| (static_cast<uint16_t>(this->encoded[2]) << 8U));
		}

		uint8_t local_z() const noexcept
		{
			return (this->encoded[3]);
		}

		uint8_t packed_light() const noexcept
		{
			return (this->encoded[4]);
		}
	};

	static_assert(sizeof(IncrementalLightDelta) == 5U,
		"incremental light deltas must remain compact queue records");

	struct							Result
	{
		static constexpr uint32_t STAGE_GEOMETRY_ONLY = 1U << 0;
		static constexpr uint32_t STAGE_GEOMETRY_FINAL = 1U << 1;
		uint64_t					request_id;
		uint64_t					world_epoch;
		uint64_t					relevance_epoch;
		uint32_t					generation_revision;
		uint32_t					configuration_signature;
		uint32_t					stage_mask;
		uint64_t					voxel_revision;
		uint64_t					light_revision;
		uint16_t					content_version;
		uint16_t					light_input_version;
		voxel_light_update_config	light_update_config;
		uint64_t					completed_at_nanoseconds;
		uint64_t					generation_duration_nanoseconds;
		uint64_t					mesh_duration_nanoseconds;
		uint64_t					light_scanned_cells;
		uint64_t					light_propagated_cells;
		uint64_t					light_queue_peak;
		ft_bool					incremental_light;
		ft_bool					incremental_light_deltas_complete;
		ft_bool					interactive_remesh;
		ft_bool					lighting_halo_valid;
		ft_bool					lighting_cardinal_halo_valid;
		ft_bool					lighting_halo_blocked;
		ft_bool					dependency_valid;
		int32_t					dependency_chunk_x;
		int32_t					dependency_chunk_z;
		uint64_t				dependency_voxel_revision;
		uint64_t				dependency_light_revision;
		uint16_t				dependency_content_version;
		uint16_t				dependency_light_input_version;
		int32_t						chunk_x;
		int32_t						chunk_z;
		WorldGenerationOperation	operation;
		int32_t						error_code;
		std::unique_ptr<WorldChunk> chunk;
		std::unique_ptr<chunk_mesh> mesh;
		std::unique_ptr<chunk_mesh> retired_mesh;
		std::unique_ptr<voxel_light_chunk> light;
		std::vector<IncrementalLightDelta> incremental_light_deltas;
		std::vector<WorldDeferredBlockEdit> deferred_edits;

		~Result() noexcept;
	};

	struct							Request
	{
		/*
		 * A Request is the complete ownership boundary for worker execution.
		 * Everything below this point is copied or moved into the request before
		 * it is queued.  Worker code must not add World, WorldChunkStreamer,
		 * live WorldChunk pointers, renderer objects, or mutexes here.  The
		 * compute worker is deliberately able to run without touching the live
		 * world or waiting for the world's lock.
		 */
					uint64_t					request_id;
					uint64_t					cancellation_epoch;
					uint64_t					background_remesh_epoch;
		uint64_t					world_epoch;
		uint64_t					relevance_epoch;
		uint32_t					generation_revision;
		uint32_t					configuration_signature;
		uint32_t					stage_mask;
		uint64_t					voxel_revision;
		uint64_t					light_revision;
		uint16_t					content_version;
		uint16_t					light_input_version;
		int32_t						chunk_x;
		int32_t						chunk_z;
		WorldGenerationOperation	operation;
		std::string seed;
		voxel_generation_config	config;
		voxel_light_update_config	light_update_config;
		std::unique_ptr<voxel_light_build_operation> remesh_light_operation;
		std::unique_ptr<voxel_light_chunk> remesh_light;
		std::unique_ptr<game_voxel_chunk> remesh_target;
		ft_bool remesh_in_progress;
		ft_bool remesh_geometry_published;
		ft_bool remesh_interactive;
		ft_bool remesh_geometry_only;
		ft_bool incremental_additive_light;
		ft_bool incremental_removal_light;
		int32_t incremental_local_x;
		int32_t incremental_local_y;
		int32_t incremental_local_z;
		uint32_t incremental_old_block_id;
		uint32_t incremental_new_block_id;
		std::vector<IncrementalLightSeed> incremental_light_seeds;
		std::vector<IncrementalLightNode> incremental_additive_queue;
		std::size_t incremental_additive_queue_index;
		std::vector<IncrementalRemovalNode> incremental_removal_queue;
		std::vector<IncrementalRemovalNode> incremental_removal_addition_queue;
		std::size_t incremental_removal_queue_index;
		std::size_t incremental_removal_addition_queue_index;
		ft_bool incremental_removal_state_initialized;
		ft_bool incremental_removal_addition_started;
		uint8_t incremental_removal_external_new_channel[2];
		std::size_t incremental_seed_index;
		ft_bool incremental_additive_state_initialized;
		uint64_t incremental_processed_cells;
		std::shared_ptr<std::atomic<uint64_t>> cancellation_token;
		uint64_t cancellation_token_value;
		std::unique_ptr<WorldChunkSnapshot> snapshot;
		std::vector<WorldDeferredBlockEdit> deferred_edits;
	};

	WorldGenerationPipeline() noexcept;
	WorldGenerationPipeline(const WorldGenerationPipeline &other) noexcept;
	~WorldGenerationPipeline() noexcept;
	WorldGenerationPipeline &operator=(const WorldGenerationPipeline &other) noexcept;

	int32_t initialize(std::size_t worker_count,
		std::size_t maximum_queued) noexcept;
	int32_t destroy() noexcept;
	int32_t submit_generation(uint64_t request_id, uint64_t world_epoch,
		uint64_t relevance_epoch, uint32_t generation_revision, int32_t chunk_x,
		int32_t chunk_z, const char *seed,
		const voxel_generation_config &config, uint32_t stage_mask,
		WorldGenerationOperation operation,
		const WorldChunkSnapshot *source_snapshot = nullptr) noexcept;
	int32_t submit_remesh(uint64_t request_id, uint64_t world_epoch,
		uint64_t relevance_epoch, uint32_t generation_revision, int32_t chunk_x,
		int32_t chunk_z, uint64_t voxel_revision, uint64_t light_revision,
		uint16_t content_version, uint16_t light_input_version,
		WorldChunkSnapshot &&snapshot,
		const voxel_light_update_config *light_update_config = nullptr,
		ft_bool interactive = FT_FALSE,
		ft_bool geometry_only = FT_FALSE,
		ft_bool incremental_additive_light = FT_FALSE,
		ft_bool incremental_removal_light = FT_FALSE,
		int32_t incremental_local_x = 0, int32_t incremental_local_y = 0,
		int32_t incremental_local_z = 0,
		uint32_t incremental_old_block_id = GAME_VOXEL_AIR_BLOCK,
		uint32_t incremental_new_block_id = GAME_VOXEL_AIR_BLOCK,
		const std::vector<IncrementalLightSeed> *incremental_light_seeds = nullptr,
		const std::shared_ptr<std::atomic<uint64_t>> &cancellation_token =
			std::shared_ptr<std::atomic<uint64_t>>(),
		uint64_t cancellation_token_value = 0U) noexcept;
	int32_t poll(std::unique_ptr<Result> &result) noexcept;
	void retire_result(std::unique_ptr<Result> result) noexcept;
	int32_t retire_chunk(std::unique_ptr<WorldChunk> chunk) noexcept;
	int32_t capture_snapshot(const WorldChunk &target, const WorldChunk *west,
		const WorldChunk *east, const WorldChunk *north,
		const WorldChunk *south, const WorldChunk *northwest,
		const WorldChunk *northeast, const WorldChunk *southwest,
		const WorldChunk *southeast,
		WorldChunkSnapshot &snapshot) const noexcept;
	void cancel_queued() noexcept;
	void cancel_queued_background_remeshes() noexcept;
	void cancel_active_background_remeshes() noexcept;
	std::size_t queued_count() const noexcept;
	std::size_t completed_count() const noexcept;
	std::size_t active_count() const noexcept;
	std::size_t active_generation_count() const noexcept;
	std::size_t remesh_in_flight_count() const noexcept;
	bool has_request_or_result(uint64_t request_id) const noexcept;
	bool has_remesh_for_chunk(int32_t chunk_x, int32_t chunk_z) const noexcept;
	void release_remesh_slot() noexcept;
	uint64_t oldest_completed_result_age_nanoseconds() const noexcept;
	bool is_initialized() const noexcept;

	std::deque<std::unique_ptr<Request>> requests_;
	std::deque<std::unique_ptr<Result>> results_;
	std::deque<std::unique_ptr<Result>> retired_results_;
	mutable std::mutex mutex_;
	mutable std::mutex results_mutex_;
	std::condition_variable condition_;
	std::atomic<uint64_t> pipeline_epoch_;
	std::atomic<uint64_t> background_remesh_epoch_;
	std::atomic<std::size_t> remesh_in_flight_;
	std::atomic<std::size_t> active_requests_;
	std::atomic<std::size_t> active_generation_requests_;
	std::atomic<bool>				stopping_;
	std::array<uint64_t, 8U> active_request_ids_;
	std::array<int32_t, 8U> active_request_chunk_x_;
	std::array<int32_t, 8U> active_request_chunk_z_;
	std::size_t active_request_id_count_;

  private:
	friend class WorldGenerationWorkerLoop;
	std::vector<std::thread> workers_;
	std::size_t maximum_queued_;
	std::size_t generation_worker_limit_;
	bool							initialized_;

	static std::size_t resolve_worker_count(std::size_t worker_count) noexcept;
};

# include "../../src/world/WorldChunkGenerationWorker.hpp"
# include "../../src/world/WorldChunkSnapshotCapture.hpp"
# include "../../src/world/WorldGenerationRequestBuilder.hpp"
# include "../../src/world/WorldGenerationWorkerLoop.hpp"

#endif
