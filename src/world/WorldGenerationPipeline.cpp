#include "../../src/world/WorldGenerationPipeline.hpp"
#include <chrono>
#include <cstdio>

WorldGenerationPipeline::WorldGenerationPipeline() noexcept : requests_(),
	results_(), retired_results_(), mutex_(), results_mutex_(), condition_(), pipeline_epoch_(1U),
	remesh_in_flight_(0U), active_requests_(0U), stopping_(false), workers_(), maximum_queued_(0U),
	initialized_(false)
{
}

WorldGenerationPipeline::Result::~Result() noexcept
{
	if (this->mesh != nullptr)
		(void)chunk_mesh_destroy(*this->mesh);
	if (this->retired_mesh != nullptr)
		(void)chunk_mesh_destroy(*this->retired_mesh);
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
	std::lock_guard<std::mutex> lock(this->results_mutex_);
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
		this->initialized_ = false;
	}
	{
		std::lock_guard<std::mutex> lock(this->results_mutex_);

		this->results_.clear();
	}
	{
		std::lock_guard<std::mutex> lock(this->mutex_);

		this->retired_results_.clear();
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
		{
		#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
			std::fprintf(stderr,
				"[WorldGen] submit generation rejected initialized=%d stopping=%d "
				"epoch=%ju queued=%zu active=%zu\n",
				this->initialized_ ? 1 : 0, this->stopping_ ? 1 : 0,
				this->pipeline_epoch_.load(),
				this->requests_.size(), this->active_requests_.load());
		#endif
			return (FT_ERR_INVALID_STATE);
		}
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
	uint64_t voxel_revision, uint64_t light_revision,
	WorldChunkSnapshot &&snapshot,
	const voxel_light_update_config *light_update_config) noexcept
{
	std::unique_ptr<Request> request;
	int32_t error_code;
	voxel_light_update_config resolved_light_config;

	voxel_light_update_config_defaults(resolved_light_config);
	if (light_update_config != nullptr)
		resolved_light_config = *light_update_config;
	error_code = WorldGenerationRequestBuilder::build_remesh(request,
			request_id, this->pipeline_epoch_.load(), world_epoch,
			relevance_epoch, generation_revision, chunk_x, chunk_z,
			voxel_revision, light_revision, std::move(snapshot),
			resolved_light_config);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	{
		std::lock_guard<std::mutex> lock(this->mutex_);

		if (!this->initialized_ || this->stopping_)
		{
		#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
			std::fprintf(stderr,
				"[WorldGen] submit remesh rejected initialized=%d stopping=%d "
				"epoch=%ju queued=%zu active=%zu\n",
				this->initialized_ ? 1 : 0, this->stopping_ ? 1 : 0,
				this->pipeline_epoch_.load(),
				this->requests_.size(), this->active_requests_.load());
		#endif
			return (FT_ERR_INVALID_STATE);
		}
		if (this->remesh_in_flight_.load() >= 2U)
			return (FT_ERR_FULL);
		/* Keep capacity available for the bounded remesh window. Interactive
		 * edits must not wait behind the initial generation burst. */
		if (this->requests_.size() >= this->maximum_queued_ + 1U)
			return (FT_ERR_FULL);
		this->remesh_in_flight_.fetch_add(1U);
		this->requests_.push_front(std::move(request));
	}
	this->condition_.notify_one();
	return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationPipeline::poll(std::unique_ptr<Result> &result) noexcept
{
	result.reset();
	std::lock_guard<std::mutex> lock(this->results_mutex_);

	if (this->results_.empty())
		return (FT_ERR_NOT_FOUND);
	result = std::move(this->results_.front());
	this->results_.pop_front();
	return (FT_ERR_SUCCESS);
}

void WorldGenerationPipeline::retire_result(
	std::unique_ptr<Result> result) noexcept
{
	if (result == nullptr)
		return ;
	{
		std::lock_guard<std::mutex> lock(this->mutex_);
		this->retired_results_.push_back(std::move(result));
	}
	this->condition_.notify_one();
}

int32_t WorldGenerationPipeline::retire_chunk(
	std::unique_ptr<WorldChunk> chunk) noexcept
{
	std::unique_ptr<Result> result(new (std::nothrow) Result());

	if (result == nullptr || chunk == nullptr)
		return (result == nullptr ? FT_ERR_NO_MEMORY : FT_ERR_INVALID_ARGUMENT);
	result->chunk = std::move(chunk);
	this->retire_result(std::move(result));
	return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationPipeline::capture_snapshot(const WorldChunk &target,
	const WorldChunk *west, const WorldChunk *east, const WorldChunk *north,
	const WorldChunk *south, const WorldChunk *northwest,
	const WorldChunk *northeast, const WorldChunk *southwest,
	const WorldChunk *southeast, WorldChunkSnapshot &snapshot) const noexcept
{
	return (WorldChunkSnapshotCapture::capture(target, west, east, north, south,
		northwest, northeast, southwest, southeast, snapshot));
}

void WorldGenerationPipeline::cancel_queued() noexcept
{
	std::size_t cancelled_remesh_count;
	std::size_t observed_count;
	std::size_t replacement_count;

	std::lock_guard<std::mutex> lock(this->mutex_);

	this->pipeline_epoch_.fetch_add(1U);
	cancelled_remesh_count = 0U;
	for (const std::unique_ptr<Request> &request : this->requests_)
	{
		if (request != nullptr
			&& request->operation == WorldGenerationOperation::REMESH)
			cancelled_remesh_count += 1U;
	}
	this->requests_.clear();
	while (cancelled_remesh_count > 0U)
	{
		observed_count = this->remesh_in_flight_.load();
		replacement_count = observed_count >= cancelled_remesh_count
			? observed_count - cancelled_remesh_count : 0U;
		if (this->remesh_in_flight_.compare_exchange_weak(observed_count,
			replacement_count))
			break ;
	}
}

std::size_t WorldGenerationPipeline::queued_count() const noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);

	return (this->requests_.size());
}

std::size_t WorldGenerationPipeline::completed_count() const noexcept
{
	std::lock_guard<std::mutex> lock(this->results_mutex_);

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

void WorldGenerationPipeline::release_remesh_slot() noexcept
{
	std::size_t observed_count;

	observed_count = this->remesh_in_flight_.load();
	while (observed_count != 0U
		&& !this->remesh_in_flight_.compare_exchange_weak(observed_count,
			observed_count - 1U))
		continue ;
}

bool WorldGenerationPipeline::is_initialized() const noexcept
{
	return (this->initialized_);
}
