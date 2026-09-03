#include "../../src/world/WorldGenerationPipeline.hpp"
#include <chrono>

WorldGenerationPipeline::WorldGenerationPipeline() noexcept : requests_(),
	results_(), mutex_(), condition_(), pipeline_epoch_(1U),
	remesh_in_flight_(0U), active_requests_(0U), stopping_(false), workers_(), maximum_queued_(0U),
	initialized_(false)
{
}

WorldGenerationPipeline::WorldGenerationPipeline(const WorldGenerationPipeline &other) noexcept
{
	(void)other;
}

WorldGenerationPipeline::~WorldGenerationPipeline() noexcept
{
	(void)this->destroy();
}

uint64_t WorldGenerationPipeline::oldest_completed_result_age_nanoseconds()
	const noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	uint64_t oldest;
	uint64_t now;

	if (this->results_.empty())
		return (0U);
	oldest = 0U;
	for (const std::unique_ptr<Result> &result : this->results_)
	{
		if (result == nullptr || result->completed_at_nanoseconds == 0U)
			continue ;
		if (oldest == 0U || result->completed_at_nanoseconds < oldest)
			oldest = result->completed_at_nanoseconds;
	}
	if (oldest == 0U)
		return (0U);
	now = static_cast<uint64_t>(std::chrono::duration_cast<
		std::chrono::nanoseconds>(std::chrono::steady_clock::now()
		.time_since_epoch()).count());
	return (now >= oldest ? now - oldest : 0U);
}

WorldGenerationPipeline &WorldGenerationPipeline::operator=(const WorldGenerationPipeline &other) noexcept
{
	(void)other;
	return (*this);
}

std::size_t WorldGenerationPipeline::resolve_worker_count(std::size_t worker_count) noexcept
{
	unsigned int hardware_count;

	if (worker_count != 0U)
		return (worker_count);
	hardware_count = std::thread::hardware_concurrency();
	if (hardware_count <= 1U)
		worker_count = 1U;
	else
		worker_count = static_cast<std::size_t>(hardware_count - 1U);
	return (std::min<std::size_t>(worker_count, 4U));
}

int32_t WorldGenerationPipeline::initialize(std::size_t worker_count,
	std::size_t maximum_queued) noexcept
{
	std::size_t index;

	if (this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	worker_count = WorldGenerationPipeline::resolve_worker_count(worker_count);
	if (maximum_queued == 0U)
		maximum_queued = worker_count * 4U;
	this->maximum_queued_ = maximum_queued;
	this->stopping_ = false;
	this->pipeline_epoch_.fetch_add(1U);
	try
	{
		index = 0U;
		while (index < worker_count)
		{
			this->workers_.emplace_back(&WorldGenerationWorkerLoop::run,
				std::ref(*this));
			index += 1U;
		}
	}
	catch (...)
	{
		this->stopping_ = true;
		this->condition_.notify_all();
		for (std::thread &worker : this->workers_)
			if (worker.joinable())
				worker.join();
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
		if (worker.joinable())
			worker.join();
	this->workers_.clear();
	this->remesh_in_flight_.store(0U);
	this->active_requests_.store(0U);
	{
		std::lock_guard<std::mutex> lock(this->mutex_);

		this->requests_.clear();
		this->results_.clear();
		this->initialized_ = false;
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationPipeline::submit_generation(uint64_t request_id,
	uint64_t world_epoch, uint64_t relevance_epoch,
	uint32_t generation_revision, int32_t chunk_x, int32_t chunk_z,
	const char *seed, const voxel_generation_config &config,
	uint32_t stage_mask, WorldGenerationOperation operation,
	const WorldChunkSnapshot *source_snapshot) noexcept
{
	std::unique_ptr<Request> request;
	int32_t error_code;

	error_code = WorldGenerationRequestBuilder::build(request, request_id,
			this->pipeline_epoch_.load(), world_epoch, relevance_epoch,
			generation_revision, chunk_x, chunk_z, seed, config, stage_mask,
			operation, source_snapshot);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
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
	uint64_t world_epoch, uint64_t relevance_epoch,
	uint32_t generation_revision, int32_t chunk_x, int32_t chunk_z,
	uint64_t voxel_revision, const WorldChunkSnapshot &snapshot) noexcept
{
	std::unique_ptr<Request> request;
	int32_t error_code;

	error_code = WorldGenerationRequestBuilder::build_remesh(request,
			request_id, this->pipeline_epoch_.load(), world_epoch,
			relevance_epoch, generation_revision, chunk_x, chunk_z,
			voxel_revision, snapshot);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
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
    if (target.chunk.copy_blocks(snapshot.blocks.data(),
            static_cast<uint32_t>(snapshot.blocks.size())) != FT_ERR_SUCCESS)
        return (FT_ERR_INVALID_OPERATION);
    auto capture_border = [](const WorldChunk *source, std::vector<uint32_t> &border,
                             int32_t border_local_x, int32_t border_local_z) -> int32_t
    {
        uint32_t border_coordinate;

        if (source == nullptr || !source->initialized)
            return (FT_ERR_SUCCESS);
        if (border_local_x < 0 || border_local_x >= GAME_VOXEL_CHUNK_WIDTH)
        {
            border_coordinate = 0U;
            if (border_local_x < 0)
                border_coordinate = GAME_VOXEL_CHUNK_WIDTH - 1U;
            if (source->chunk.copy_x_border(border.data(),
                    static_cast<uint32_t>(border.size()), border_coordinate)
                != FT_ERR_SUCCESS)
                return (FT_ERR_INVALID_OPERATION);
            return (FT_ERR_SUCCESS);
        }
        border_coordinate = 0U;
        if (border_local_z < 0)
            border_coordinate = GAME_VOXEL_CHUNK_DEPTH - 1U;
        if (source->chunk.copy_z_border(border.data(),
                static_cast<uint32_t>(border.size()), border_coordinate)
            != FT_ERR_SUCCESS)
            return (FT_ERR_INVALID_OPERATION);
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
		if (request != nullptr
			&& request->operation == WorldGenerationOperation::REMESH)
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

std::size_t WorldGenerationPipeline::active_count() const noexcept
{
	return (this->active_requests_.load());
}

std::size_t WorldGenerationPipeline::remesh_in_flight_count() const noexcept
{
	return (this->remesh_in_flight_.load());
}

bool WorldGenerationPipeline::is_initialized() const noexcept
{
	return (this->initialized_);
}
