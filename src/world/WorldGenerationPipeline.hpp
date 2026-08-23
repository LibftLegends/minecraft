#ifndef WORLD_GENERATION_PIPELINE_HPP
#define WORLD_GENERATION_PIPELINE_HPP

#include "../../src/chunks/WorldChunk.hpp"
#include "../../Libft/Modules/Voxel/terrain_api.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class WorldGenerationOperation : uint8_t
{
    STREAM = 0,
    REMESH = 1,
    REGENERATE = 2
};

struct WorldDeferredBlockEdit
{
    int32_t world_x;
    int32_t world_y;
    int32_t world_z;
    uint32_t block_id;
    uint64_t request_id;
    uint64_t sequence;
};

struct WorldChunkSnapshot
{
    int32_t chunk_x;
    int32_t chunk_z;
    std::vector<uint32_t> blocks;
    std::vector<uint32_t> west_border;
    std::vector<uint32_t> east_border;
    std::vector<uint32_t> north_border;
    std::vector<uint32_t> south_border;
};

class WorldGenerationPipeline
{
  public:
    struct Result
    {
        uint64_t request_id;
        uint64_t world_epoch;
        uint64_t relevance_epoch;
        uint32_t generation_revision;
        uint32_t configuration_signature;
        uint32_t stage_mask;
        uint64_t voxel_revision;
        int32_t chunk_x;
        int32_t chunk_z;
        WorldGenerationOperation operation;
        int32_t error_code;
        std::unique_ptr<WorldChunk> chunk;
        std::unique_ptr<chunk_mesh> mesh;
        std::vector<WorldDeferredBlockEdit> deferred_edits;
    };

    WorldGenerationPipeline() noexcept;
    WorldGenerationPipeline(const WorldGenerationPipeline &other) = delete;
    WorldGenerationPipeline(WorldGenerationPipeline &&other) = delete;
    ~WorldGenerationPipeline() noexcept;
    WorldGenerationPipeline &operator=(const WorldGenerationPipeline &other) = delete;
    WorldGenerationPipeline &operator=(WorldGenerationPipeline &&other) = delete;

    int32_t initialize(std::size_t worker_count, std::size_t maximum_queued) noexcept;
    int32_t destroy() noexcept;
    int32_t submit_generation(uint64_t request_id, uint64_t world_epoch,
        uint64_t relevance_epoch, uint32_t generation_revision,
        int32_t chunk_x, int32_t chunk_z, const char *seed,
        const terrain_generation_config &config, uint32_t stage_mask,
        WorldGenerationOperation operation,
        const WorldChunkSnapshot *source_snapshot = nullptr) noexcept;
    int32_t submit_remesh(uint64_t request_id, uint64_t world_epoch,
        uint64_t relevance_epoch, uint32_t generation_revision,
        int32_t chunk_x, int32_t chunk_z, uint64_t voxel_revision,
        const WorldChunkSnapshot &snapshot) noexcept;
    int32_t poll(std::unique_ptr<Result> &result) noexcept;
    int32_t capture_snapshot(const WorldChunk &target, const WorldChunk *west,
        const WorldChunk *east, const WorldChunk *north,
        const WorldChunk *south, WorldChunkSnapshot &snapshot) const noexcept;
    void cancel_queued() noexcept;
    std::size_t queued_count() const noexcept;
    std::size_t completed_count() const noexcept;
    std::size_t remesh_in_flight_count() const noexcept;
    bool is_initialized() const noexcept;

  private:
    struct Request
    {
        uint64_t request_id;
        uint64_t cancellation_epoch;
        uint64_t world_epoch;
        uint64_t relevance_epoch;
        uint32_t generation_revision;
        uint32_t configuration_signature;
        uint32_t stage_mask;
        uint64_t voxel_revision;
        int32_t chunk_x;
        int32_t chunk_z;
        WorldGenerationOperation operation;
        std::string seed;
        terrain_generation_config config;
        std::unique_ptr<WorldChunkSnapshot> snapshot;
        std::vector<WorldDeferredBlockEdit> deferred_edits;
    };

    std::vector<std::thread> workers_;
    std::deque<std::unique_ptr<Request>> requests_;
    std::deque<std::unique_ptr<Result>> results_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<uint64_t> pipeline_epoch_;
    std::atomic<std::size_t> remesh_in_flight_;
    std::size_t maximum_queued_;
    bool stopping_;
    bool initialized_;

    static int32_t deferred_writer(int32_t world_x, int32_t world_y,
        int32_t world_z, uint32_t block_id, void *user_data) noexcept;
    static int32_t lookup_snapshot_block(void *user_data, int32_t world_x,
        int32_t world_y, int32_t world_z, uint32_t *block_id) noexcept;
    void worker_entry() noexcept;
    std::unique_ptr<Result> process_request(std::unique_ptr<Request> request) noexcept;
    std::unique_ptr<Result> process_generation(Request &request) noexcept;
    std::unique_ptr<Result> process_remesh(Request &request) noexcept;
    static int32_t initialize_chunk_for_generation(WorldChunk &chunk,
        int32_t chunk_x, int32_t chunk_z, const char *seed,
        terrain_generation_config &config, uint32_t stage_mask,
        std::vector<WorldDeferredBlockEdit> &deferred_edits,
        const WorldChunkSnapshot *source_snapshot) noexcept;
    static int32_t initialize_snapshot_chunk(game_voxel_chunk &chunk,
        const WorldChunkSnapshot &snapshot) noexcept;
    static int32_t move_mesh(chunk_mesh &destination, chunk_mesh &source) noexcept;
    bool request_is_cancelled(const Request &request) const noexcept;
};

#endif
