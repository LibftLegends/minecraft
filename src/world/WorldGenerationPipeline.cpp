#include "WorldGenerationPipeline.hpp"

#include "../../src/coordinates/WorldCoordinates.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include "../../Libft/Modules/Game/game_voxel_chunk.hpp"
#include "../../Libft/Modules/Voxel/voxel_mesh.hpp"

#include <algorithm>
#include <new>

WorldGenerationPipeline::WorldGenerationPipeline() noexcept
    : workers_(), requests_(), results_(), mutex_(), condition_(),
      pipeline_epoch_(1U), remesh_in_flight_(0U), maximum_queued_(0U),
      stopping_(false), initialized_(false)
{
}

WorldGenerationPipeline::~WorldGenerationPipeline() noexcept
{
    (void)this->destroy();
}

int32_t WorldGenerationPipeline::initialize(std::size_t worker_count,
                                            std::size_t maximum_queued) noexcept
{
    if (this->initialized_)
        return (FT_ERR_INVALID_OPERATION);
    if (worker_count == 0U)
    {
        const unsigned int hardware_count = std::thread::hardware_concurrency();
        if (hardware_count <= 1U)
            worker_count = 1U;
        else
            worker_count = static_cast<std::size_t>(hardware_count - 1U);
        worker_count = std::min<std::size_t>(worker_count, 4U);
    }
    if (maximum_queued == 0U)
        maximum_queued = worker_count * 4U;
    this->maximum_queued_ = maximum_queued;
    this->stopping_ = false;
    this->pipeline_epoch_.fetch_add(1U);
    try
    {
        std::size_t index = 0U;
        while (index < worker_count)
        {
            this->workers_.emplace_back(&WorldGenerationPipeline::worker_entry, this);
            index += 1U;
        }
    }
    catch (...)
    {
        this->stopping_ = true;
        this->condition_.notify_all();
        for (std::thread &worker : this->workers_)
        {
            if (worker.joinable())
                worker.join();
        }
        this->workers_.clear();
        return (FT_ERR_NO_MEMORY);
    }
    this->initialized_ = true;
    return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationPipeline::destroy() noexcept
{
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->stopping_ = true;
        this->pipeline_epoch_.fetch_add(1U);
        this->requests_.clear();
    }
    this->condition_.notify_all();
    for (std::thread &worker : this->workers_)
    {
        if (worker.joinable())
            worker.join();
    }
    this->workers_.clear();
    this->remesh_in_flight_.store(0U);
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->requests_.clear();
        this->results_.clear();
        this->initialized_ = false;
    }
    return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationPipeline::submit_generation(uint64_t request_id,
    uint64_t world_epoch, uint64_t relevance_epoch, uint32_t generation_revision,
    int32_t chunk_x, int32_t chunk_z, const char *seed,
    const terrain_generation_config &config, uint32_t stage_mask,
    WorldGenerationOperation operation,
    const WorldChunkSnapshot *source_snapshot) noexcept
{
    std::unique_ptr<Request> request(new (std::nothrow) Request());
    if (request == nullptr)
        return (FT_ERR_NO_MEMORY);
    request->request_id = request_id;
    request->cancellation_epoch = this->pipeline_epoch_.load();
    request->world_epoch = world_epoch;
    request->relevance_epoch = relevance_epoch;
    request->generation_revision = generation_revision;
    request->configuration_signature = terrain_generation_config_signature(config);
    request->stage_mask = stage_mask;
    request->voxel_revision = 0U;
    request->chunk_x = chunk_x;
    request->chunk_z = chunk_z;
    request->operation = operation;
    request->seed = seed == nullptr ? "" : seed;
    if (request->config.initialize(config) != FT_ERR_SUCCESS)
        return (FT_ERR_NO_MEMORY);
    if (config.cross_chunk_block_writer != nullptr)
    {
        if (request->config.set_cross_chunk_writer(
                &WorldGenerationPipeline::deferred_writer, &request->deferred_edits)
            != FT_ERR_SUCCESS)
            return (FT_ERR_INVALID_OPERATION);
        if (request->config.set_cross_chunk_features_enabled(FT_TRUE) != FT_ERR_SUCCESS)
            return (FT_ERR_INVALID_OPERATION);
    }
    if (source_snapshot != nullptr)
    {
        request->snapshot.reset(new (std::nothrow) WorldChunkSnapshot(*source_snapshot));
        if (request->snapshot == nullptr)
            return (FT_ERR_NO_MEMORY);
    }
    request->deferred_edits.reserve(256U);
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (!this->initialized_ || this->stopping_)
            return (FT_ERR_INVALID_STATE);
        if (this->requests_.size() >= this->maximum_queued_)
            return (FT_ERR_FULL);
        this->requests_.push_back(std::move(request));
    }
    this->condition_.notify_one();
    return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationPipeline::submit_remesh(uint64_t request_id,
    uint64_t world_epoch, uint64_t relevance_epoch, uint32_t generation_revision,
    int32_t chunk_x, int32_t chunk_z, uint64_t voxel_revision,
    const WorldChunkSnapshot &snapshot) noexcept
{
    std::unique_ptr<Request> request(new (std::nothrow) Request());
    if (request == nullptr)
        return (FT_ERR_NO_MEMORY);
    request->request_id = request_id;
    request->cancellation_epoch = this->pipeline_epoch_.load();
    request->world_epoch = world_epoch;
    request->relevance_epoch = relevance_epoch;
    request->generation_revision = generation_revision;
    request->configuration_signature = 0U;
    request->stage_mask = 0U;
    request->voxel_revision = voxel_revision;
    request->chunk_x = chunk_x;
    request->chunk_z = chunk_z;
    request->operation = WorldGenerationOperation::REMESH;
    request->snapshot.reset(new (std::nothrow) WorldChunkSnapshot(snapshot));
    if (request->snapshot == nullptr)
        return (FT_ERR_NO_MEMORY);
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (!this->initialized_ || this->stopping_)
            return (FT_ERR_INVALID_STATE);
        if (this->remesh_in_flight_.load() >= 1U)
            return (FT_ERR_FULL);
        if (this->requests_.size() >= this->maximum_queued_)
            return (FT_ERR_FULL);
        this->remesh_in_flight_.fetch_add(1U);
        this->requests_.push_back(std::move(request));
    }
    this->condition_.notify_one();
    return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationPipeline::poll(std::unique_ptr<Result> &result) noexcept
{
    result.reset();
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->results_.empty())
        return (FT_ERR_NOT_FOUND);
    result = std::move(this->results_.front());
    this->results_.pop_front();
    return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationPipeline::capture_snapshot(const WorldChunk &target,
    const WorldChunk *west, const WorldChunk *east, const WorldChunk *north,
    const WorldChunk *south, WorldChunkSnapshot &snapshot) const noexcept
{
    snapshot.chunk_x = target.chunk_x;
    snapshot.chunk_z = target.chunk_z;
    snapshot.blocks.clear();
    snapshot.west_border.clear();
    snapshot.east_border.clear();
    snapshot.north_border.clear();
    snapshot.south_border.clear();
    try
    {
        snapshot.blocks.resize(static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
            * static_cast<std::size_t>(GAME_VOXEL_CHUNK_DEPTH)
            * static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT));
        snapshot.west_border.resize(static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
            * static_cast<std::size_t>(GAME_VOXEL_CHUNK_DEPTH), GAME_VOXEL_AIR_BLOCK);
        snapshot.east_border = snapshot.west_border;
        snapshot.north_border.resize(static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
            * static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH), GAME_VOXEL_AIR_BLOCK);
        snapshot.south_border = snapshot.north_border;
    }
    catch (...)
    {
        return (FT_ERR_NO_MEMORY);
    }
    int32_t local_z = 0;
    while (local_z < GAME_VOXEL_CHUNK_DEPTH)
    {
        int32_t local_y = 0;
        while (local_y < GAME_VOXEL_CHUNK_HEIGHT)
        {
            int32_t local_x = 0;
            while (local_x < GAME_VOXEL_CHUNK_WIDTH)
            {
                uint32_t block_id;
                if (target.chunk.read_block(local_x, local_y, local_z, &block_id)
                    != FT_ERR_SUCCESS)
                    return (FT_ERR_INVALID_OPERATION);
                const std::size_t index = (static_cast<std::size_t>(local_z)
                    * static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
                    + static_cast<std::size_t>(local_y))
                    * static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
                    + static_cast<std::size_t>(local_x);
                snapshot.blocks[index] = block_id;
                local_x += 1;
            }
            local_y += 1;
        }
        local_z += 1;
    }
    auto capture_border = [](const WorldChunk *source, std::vector<uint32_t> &border,
                             int32_t border_local_x, int32_t border_local_z) -> int32_t
    {
        if (source == nullptr || !source->initialized)
            return (FT_ERR_SUCCESS);
        int32_t y = 0;
        while (y < GAME_VOXEL_CHUNK_HEIGHT)
        {
            int32_t axis = 0;
            while (axis < GAME_VOXEL_CHUNK_WIDTH)
            {
                int32_t read_x;
                int32_t read_z;
                if (border_local_x < 0)
                    read_x = GAME_VOXEL_CHUNK_WIDTH - 1;
                else if (border_local_x >= GAME_VOXEL_CHUNK_WIDTH)
                    read_x = 0;
                else
                    read_x = axis;
                if (border_local_z < 0)
                    read_z = GAME_VOXEL_CHUNK_DEPTH - 1;
                else if (border_local_z >= GAME_VOXEL_CHUNK_DEPTH)
                    read_z = 0;
                else
                    read_z = axis;
                uint32_t block_id;
                if (source->chunk.read_block(read_x, y, read_z, &block_id)
                    != FT_ERR_SUCCESS)
                    return (FT_ERR_INVALID_OPERATION);
                if (border_local_x < 0 || border_local_x >= GAME_VOXEL_CHUNK_WIDTH)
                    border[static_cast<std::size_t>(y) * GAME_VOXEL_CHUNK_DEPTH
                        + static_cast<std::size_t>(axis)] = block_id;
                else
                    border[static_cast<std::size_t>(y) * GAME_VOXEL_CHUNK_WIDTH
                        + static_cast<std::size_t>(axis)] = block_id;
                axis += 1;
            }
            y += 1;
        }
        return (FT_ERR_SUCCESS);
    };
    if (capture_border(west, snapshot.west_border, -1, 0) != FT_ERR_SUCCESS
        || capture_border(east, snapshot.east_border, GAME_VOXEL_CHUNK_WIDTH, 0)
            != FT_ERR_SUCCESS
        || capture_border(north, snapshot.north_border, 0, -1) != FT_ERR_SUCCESS
        || capture_border(south, snapshot.south_border, 0, GAME_VOXEL_CHUNK_DEPTH)
            != FT_ERR_SUCCESS)
        return (FT_ERR_INVALID_OPERATION);
    return (FT_ERR_SUCCESS);
}

void WorldGenerationPipeline::cancel_queued() noexcept
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->pipeline_epoch_.fetch_add(1U);
    for (const std::unique_ptr<Request> &request : this->requests_)
    {
        if (request != nullptr && request->operation == WorldGenerationOperation::REMESH)
            this->remesh_in_flight_.fetch_sub(1U);
    }
    this->requests_.clear();
}

std::size_t WorldGenerationPipeline::queued_count() const noexcept
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    return (this->requests_.size());
}

std::size_t WorldGenerationPipeline::completed_count() const noexcept
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    return (this->results_.size());
}

std::size_t WorldGenerationPipeline::remesh_in_flight_count() const noexcept
{
    return (this->remesh_in_flight_.load());
}

bool WorldGenerationPipeline::is_initialized() const noexcept
{
    return (this->initialized_);
}

int32_t WorldGenerationPipeline::deferred_writer(int32_t world_x, int32_t world_y,
    int32_t world_z, uint32_t block_id, void *user_data) noexcept
{
    if (user_data == nullptr)
        return (FT_ERR_INVALID_ARGUMENT);
    std::vector<WorldDeferredBlockEdit> *edits =
        static_cast<std::vector<WorldDeferredBlockEdit> *>(user_data);
    try
    {
        edits->push_back({world_x, world_y, world_z, block_id,
            0U, edits->size()});
    }
    catch (...)
    {
        return (FT_ERR_NO_MEMORY);
    }
    return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationPipeline::lookup_snapshot_block(void *user_data,
    int32_t world_x, int32_t world_y, int32_t world_z, uint32_t *block_id) noexcept
{
    if (user_data == nullptr || block_id == nullptr)
        return (FT_ERR_INVALID_ARGUMENT);
    const WorldChunkSnapshot *snapshot =
        static_cast<const WorldChunkSnapshot *>(user_data);
    if (world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT)
    {
        *block_id = GAME_VOXEL_AIR_BLOCK;
        return (FT_ERR_SUCCESS);
    }
    const int32_t local_x = world_x - snapshot->chunk_x * GAME_VOXEL_CHUNK_WIDTH;
    const int32_t local_z = world_z - snapshot->chunk_z * GAME_VOXEL_CHUNK_DEPTH;
    if (local_x >= 0 && local_x < GAME_VOXEL_CHUNK_WIDTH
        && local_z >= 0 && local_z < GAME_VOXEL_CHUNK_DEPTH)
    {
        const std::size_t index = (static_cast<std::size_t>(local_z)
            * static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
            + static_cast<std::size_t>(world_y))
            * static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
            + static_cast<std::size_t>(local_x);
        *block_id = snapshot->blocks[index];
        return (FT_ERR_SUCCESS);
    }
    if (local_x == -1 && local_z >= 0 && local_z < GAME_VOXEL_CHUNK_DEPTH)
        *block_id = snapshot->west_border[static_cast<std::size_t>(world_y)
            * GAME_VOXEL_CHUNK_DEPTH + static_cast<std::size_t>(local_z)];
    else if (local_x == GAME_VOXEL_CHUNK_WIDTH && local_z >= 0
        && local_z < GAME_VOXEL_CHUNK_DEPTH)
        *block_id = snapshot->east_border[static_cast<std::size_t>(world_y)
            * GAME_VOXEL_CHUNK_DEPTH + static_cast<std::size_t>(local_z)];
    else if (local_z == -1 && local_x >= 0 && local_x < GAME_VOXEL_CHUNK_WIDTH)
        *block_id = snapshot->north_border[static_cast<std::size_t>(world_y)
            * GAME_VOXEL_CHUNK_WIDTH + static_cast<std::size_t>(local_x)];
    else if (local_z == GAME_VOXEL_CHUNK_DEPTH && local_x >= 0
        && local_x < GAME_VOXEL_CHUNK_WIDTH)
        *block_id = snapshot->south_border[static_cast<std::size_t>(world_y)
            * GAME_VOXEL_CHUNK_WIDTH + static_cast<std::size_t>(local_x)];
    else
        *block_id = GAME_VOXEL_AIR_BLOCK;
    return (FT_ERR_SUCCESS);
}

void WorldGenerationPipeline::worker_entry() noexcept
{
    while (true)
    {
        std::unique_ptr<Request> request;
        {
            std::unique_lock<std::mutex> lock(this->mutex_);
            this->condition_.wait(lock, [this]()
            {
                return (this->stopping_ || !this->requests_.empty());
            });
            if (this->stopping_ && this->requests_.empty())
                return ;
            request = std::move(this->requests_.front());
            this->requests_.pop_front();
        }
        const bool is_remesh = request->operation == WorldGenerationOperation::REMESH;
        std::unique_ptr<Result> result = this->process_request(std::move(request));
        if (is_remesh)
            this->remesh_in_flight_.fetch_sub(1U);
        if (result == nullptr)
            continue ;
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            if (!this->stopping_)
                this->results_.push_back(std::move(result));
        }
    }
}

bool WorldGenerationPipeline::request_is_cancelled(const Request &request) const noexcept
{
    return (request.cancellation_epoch != this->pipeline_epoch_.load());
}

std::unique_ptr<WorldGenerationPipeline::Result>
WorldGenerationPipeline::process_request(std::unique_ptr<Request> request) noexcept
{
    if (request == nullptr)
        return (nullptr);
    if (this->request_is_cancelled(*request))
        return (nullptr);
    if (request->operation == WorldGenerationOperation::REMESH)
        return (this->process_remesh(*request));
    return (this->process_generation(*request));
}

std::unique_ptr<WorldGenerationPipeline::Result>
WorldGenerationPipeline::process_generation(Request &request) noexcept
{
    std::unique_ptr<Result> result(new (std::nothrow) Result());
    if (result == nullptr)
        return (nullptr);
    result->request_id = request.request_id;
    result->world_epoch = request.world_epoch;
    result->relevance_epoch = request.relevance_epoch;
    result->generation_revision = request.generation_revision;
    result->configuration_signature = request.configuration_signature;
    result->stage_mask = request.stage_mask;
    result->voxel_revision = 0U;
    result->chunk_x = request.chunk_x;
    result->chunk_z = request.chunk_z;
    result->operation = request.operation;
    result->error_code = FT_ERR_SUCCESS;
    result->chunk.reset(new (std::nothrow) WorldChunk());
    if (result->chunk == nullptr)
    {
        result->error_code = FT_ERR_NO_MEMORY;
        return (result);
    }
    result->error_code = this->initialize_chunk_for_generation(*result->chunk,
        request.chunk_x, request.chunk_z, request.seed.c_str(), request.config,
        request.stage_mask, request.deferred_edits, request.snapshot.get());
    if (result->error_code != FT_ERR_SUCCESS)
    {
        result->chunk.reset();
        return (result);
    }
    game_voxel_generation_metadata metadata = result->chunk->chunk.get_generation_metadata();
    metadata.configuration_signature = request.configuration_signature;
    if (result->chunk->chunk.set_generation_metadata(metadata) != FT_ERR_SUCCESS)
    {
        result->error_code = FT_ERR_INVALID_OPERATION;
        result->chunk.reset();
        return (result);
    }
    result->deferred_edits = std::move(request.deferred_edits);
    for (WorldDeferredBlockEdit &edit : result->deferred_edits)
        edit.request_id = request.request_id;
    return (result);
}

std::unique_ptr<WorldGenerationPipeline::Result>
WorldGenerationPipeline::process_remesh(Request &request) noexcept
{
    std::unique_ptr<Result> result(new (std::nothrow) Result());
    if (result == nullptr)
        return (nullptr);
    result->request_id = request.request_id;
    result->world_epoch = request.world_epoch;
    result->relevance_epoch = request.relevance_epoch;
    result->generation_revision = request.generation_revision;
    result->configuration_signature = 0U;
    result->stage_mask = 0U;
    result->voxel_revision = request.voxel_revision;
    result->chunk_x = request.chunk_x;
    result->chunk_z = request.chunk_z;
    result->operation = request.operation;
    result->error_code = FT_ERR_SUCCESS;
    result->mesh.reset(new (std::nothrow) chunk_mesh());
    if (result->mesh == nullptr)
    {
        result->error_code = FT_ERR_NO_MEMORY;
        return (result);
    }
    if (chunk_mesh_initialize(*result->mesh) != FT_ERR_SUCCESS)
    {
        result->error_code = FT_ERR_NO_MEMORY;
        result->mesh.reset();
        return (result);
    }
    game_voxel_chunk target_chunk;
    result->error_code = this->initialize_snapshot_chunk(target_chunk, *request.snapshot);
    if (result->error_code == FT_ERR_SUCCESS)
        result->error_code = chunk_mesh_generate_from_chunk_with_neighbors(*result->mesh,
            target_chunk, request.chunk_x, request.chunk_z,
            &WorldGenerationPipeline::lookup_snapshot_block, request.snapshot.get());
    (void)target_chunk.destroy();
    if (result->error_code != FT_ERR_SUCCESS)
        result->mesh.reset();
    return (result);
}

int32_t WorldGenerationPipeline::initialize_chunk_for_generation(WorldChunk &chunk,
    int32_t chunk_x, int32_t chunk_z, const char *seed,
    terrain_generation_config &config, uint32_t stage_mask,
    std::vector<WorldDeferredBlockEdit> &deferred_edits,
    const WorldChunkSnapshot *source_snapshot) noexcept
{
    chunk.chunk_x = chunk_x;
    chunk.chunk_z = chunk_z;
    chunk.world_x = chunk_x * GAME_VOXEL_CHUNK_WIDTH;
    chunk.world_z = chunk_z * GAME_VOXEL_CHUNK_DEPTH;
    int32_t error_code;
    if (source_snapshot == nullptr)
        error_code = chunk.chunk.initialize();
    else
        error_code = WorldGenerationPipeline::initialize_snapshot_chunk(
            chunk.chunk, *source_snapshot);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    if (chunk_mesh_initialize(chunk.mesh) != FT_ERR_SUCCESS)
    {
        (void)chunk.chunk.destroy();
        return (FT_ERR_NO_MEMORY);
    }
    error_code = terrain_generate_chunk_with_stage_mask(chunk.chunk,
        chunk.world_x, chunk.world_z, seed, config, stage_mask);
    if (error_code == FT_ERR_SUCCESS)
        error_code = chunk_mesh_generate_from_chunk(chunk.mesh, chunk.chunk);
    if (error_code != FT_ERR_SUCCESS)
    {
        (void)chunk_mesh_destroy(chunk.mesh);
        (void)chunk.chunk.destroy();
        return (error_code);
    }
    chunk.initialized = true;
    (void)deferred_edits;
    return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationPipeline::initialize_snapshot_chunk(game_voxel_chunk &chunk,
    const WorldChunkSnapshot &snapshot) noexcept
{
    int32_t error_code = chunk.initialize();
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    std::size_t index = 0U;
    while (index < snapshot.blocks.size())
    {
        const std::size_t plane = static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
            * static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT);
        const std::size_t local_z = index / plane;
        const std::size_t remainder = index % plane;
        const std::size_t local_y = remainder / GAME_VOXEL_CHUNK_WIDTH;
        const std::size_t local_x = remainder % GAME_VOXEL_CHUNK_WIDTH;
        error_code = chunk.write_generated_block(static_cast<int32_t>(local_x),
            static_cast<int32_t>(local_y), static_cast<int32_t>(local_z),
            snapshot.blocks[index]);
        if (error_code != FT_ERR_SUCCESS)
        {
            (void)chunk.destroy();
            return (error_code);
        }
        index += 1U;
    }
    return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationPipeline::move_mesh(chunk_mesh &destination,
                                           chunk_mesh &source) noexcept
{
    int32_t error_code = destination.vertices.move(source.vertices);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = destination.indices.move(source.indices);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    destination.bounds = source.bounds;
    destination.occupied_bounds = source.occupied_bounds;
    destination.has_occupied_bounds = source.has_occupied_bounds;
    return (FT_ERR_SUCCESS);
}
