#include "../../src/world/WorldGenerationPipeline.hpp"
#include <chrono>
#include <cstdio>

namespace
{
}

WorldGenerationPipeline::WorldGenerationPipeline() noexcept : requests_(),
results_(), retired_results_(), mutex_(), results_mutex_(), condition_(), pipeline_epoch_(1U),
	background_remesh_epoch_(1U),
	remesh_in_flight_(0U), active_requests_(0U),
	active_generation_requests_(0U), stopping_(false), active_request_ids_(),
	active_request_chunk_x_(), active_request_chunk_z_(),
	active_request_id_count_(0U), workers_(),
	maximum_queued_(0U), generation_worker_limit_(0U), initialized_(false)
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
	else if (hardware_count == 2U)
		worker_count = 1U;
	else
	{
		/* The world pipeline is not the only persistent worker system.  The
		 * remesh snapshot worker, the render thread, and (in analytics builds)
		 * the exporter also need scheduling capacity.  Leaving only one CPU
		 * for those users lets generation workers preempt the render loop on
		 * small machines and turns even stale-result commits into long wall
		 * clock pauses. */
		worker_count = static_cast<std::size_t>(hardware_count - 2U);
	}
	return (std::min<std::size_t>(worker_count, 4U));
}

int32_t WorldGenerationPipeline::initialize(std::size_t worker_count,
	std::size_t maximum_queued) noexcept
{
	std::size_t index;

	if (this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	worker_count = WorldGenerationPipeline::resolve_worker_count(worker_count);
	/* Keep two workers available for remesh/light requests while terrain
	 * generation is active.  A single reserved worker was insufficient on
	 * machines where generation saturated the CPU: the capture worker could
	 * produce an edit snapshot, but the edit then waited behind several heavy
	 * generation tasks. */
	this->generation_worker_limit_ = worker_count > 2U
		? worker_count - 2U : 1U;
	if (maximum_queued == 0U)
		maximum_queued = worker_count * 4U;
	this->maximum_queued_ = maximum_queued;
	this->stopping_ = false;
	this->pipeline_epoch_.fetch_add(1U);
	this->background_remesh_epoch_.fetch_add(1U);
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
		this->background_remesh_epoch_.fetch_add(1U);
		this->requests_.clear();
	}
	this->condition_.notify_all();
	for (std::thread &worker : this->workers_)
		if (worker.joinable())
			worker.join();
	this->workers_.clear();
	this->remesh_in_flight_.store(0U);
	this->active_requests_.store(0U);
	this->active_generation_requests_.store(0U);
	{
		std::lock_guard<std::mutex> lock(this->mutex_);
		this->active_request_id_count_ = 0U;
		this->active_request_ids_.fill(0U);
		this->active_request_chunk_x_.fill(0);
		this->active_request_chunk_z_.fill(0);
	}
	this->generation_worker_limit_ = 0U;
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
		/* Remesh requests have their own bounded reservation through
		 * remesh_in_flight_.  Subtracting that reservation from the total queue
		 * here as well double-counts it: when the remesh side is empty, terrain
		 * generation is incorrectly capped below the actual queue capacity and
		 * absent stream candidates can remain CANDIDATE_ABSENT forever.  The
		 * worker loop separately enforces the generation concurrency quota, while
		 * this check only protects the shared queue's physical capacity. */
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
	uint16_t content_version, uint16_t light_input_version,
	WorldChunkSnapshot &&snapshot,
	const voxel_light_update_config *light_update_config,
	ft_bool interactive, ft_bool geometry_only,
	ft_bool incremental_additive_light,
	ft_bool incremental_removal_light,
	int32_t incremental_local_x, int32_t incremental_local_y,
	int32_t incremental_local_z,
	uint32_t incremental_old_block_id, uint32_t incremental_new_block_id,
	const std::vector<WorldGenerationPipeline::IncrementalLightSeed>
		*incremental_light_seeds,
	const std::shared_ptr<std::atomic<uint64_t>> &cancellation_token,
	uint64_t cancellation_token_value) noexcept
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
			voxel_revision, light_revision, content_version, light_input_version,
			std::move(snapshot),
			resolved_light_config, interactive, geometry_only,
			incremental_additive_light,
			incremental_removal_light, incremental_local_x,
			incremental_local_y, incremental_local_z, incremental_old_block_id,
			incremental_new_block_id, incremental_light_seeds,
			cancellation_token, cancellation_token_value);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	{
		std::lock_guard<std::mutex> lock(this->mutex_);
		std::deque<std::unique_ptr<Request>>::iterator iterator;

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
		iterator = this->requests_.begin();
		while (iterator != this->requests_.end())
		{
			if (*iterator != nullptr
				&& (*iterator)->operation == WorldGenerationOperation::REMESH
				&& (*iterator)->chunk_x == chunk_x
				&& (*iterator)->chunk_z == chunk_z)
			{
				iterator = this->requests_.erase(iterator);
				this->release_remesh_slot();
			}
			else
				++iterator;
		}
		if (this->remesh_in_flight_.load() >= 2U)
			return (FT_ERR_FULL);
		/* remesh_in_flight_ already bounds replacement work independently from
		 * terrain-generation queue occupancy.  Do not reserve queue entries by
		 * subtracting a second quota here: that can strand generation requests
		 * even when no remesh request is waiting for service. */
		if (this->requests_.size() >= this->maximum_queued_)
			return (FT_ERR_FULL);
		this->remesh_in_flight_.fetch_add(1U);
		if (request->remesh_interactive == FT_FALSE)
			request->background_remesh_epoch =
				this->background_remesh_epoch_.load();
		this->requests_.push_front(std::move(request));
	}
	this->condition_.notify_one();
	return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationPipeline::poll(std::unique_ptr<Result> &result) noexcept
{
	std::deque<std::unique_ptr<Result>>::iterator iterator;
	bool found_interactive_remesh;

	result.reset();
	std::lock_guard<std::mutex> lock(this->results_mutex_);

	if (this->results_.empty())
		return (FT_ERR_NOT_FOUND);
	/* A generation result can be large and numerous during startup.  Always
	 * commit an interactive remesh first, then another remesh, so a ready edit
	 * is not hidden behind background publication or the FIFO generation
	 * backlog.  Worker selection already provides a generation escape; this
	 * only affects publication order. */
	iterator = this->results_.begin();
	found_interactive_remesh = false;
	for (std::deque<std::unique_ptr<Result>>::iterator candidate =
		this->results_.begin(); candidate != this->results_.end(); ++candidate)
	{
		if (*candidate != nullptr
			&& (*candidate)->operation == WorldGenerationOperation::REMESH)
		{
			if ((*candidate)->interactive_remesh != FT_FALSE)
			{
				iterator = candidate;
				found_interactive_remesh = true;
				break ;
			}
			if (!found_interactive_remesh
				&& iterator == this->results_.begin())
				iterator = candidate;
		}
	}
	result = std::move(*iterator);
	this->results_.erase(iterator);
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

void WorldGenerationPipeline::cancel_queued_background_remeshes() noexcept
{
	std::size_t cancelled_count = 0U;

	{
		std::lock_guard<std::mutex> lock(this->mutex_);
		std::deque<std::unique_ptr<Request>>::iterator iterator =
			this->requests_.begin();
		while (iterator != this->requests_.end())
		{
			if (*iterator != nullptr
				&& (*iterator)->operation == WorldGenerationOperation::REMESH
				&& (*iterator)->remesh_interactive == FT_FALSE)
			{
				iterator = this->requests_.erase(iterator);
				cancelled_count += 1U;
			}
			else
				++iterator;
		}
	}
	while (cancelled_count > 0U)
	{
		std::size_t observed = this->remesh_in_flight_.load();
		const std::size_t replacement = observed > cancelled_count
			? observed - cancelled_count : 0U;
		if (this->remesh_in_flight_.compare_exchange_weak(observed,
			replacement))
			break ;
	}
	this->condition_.notify_all();
}

void WorldGenerationPipeline::cancel_active_background_remeshes() noexcept
{
	/* Background remeshes are disposable maintenance work.  An interactive
	 * edit must be able to invalidate both queued work and the current bounded
	 * worker slice without cancelling terrain generation. */
	this->background_remesh_epoch_.fetch_add(1U, std::memory_order_acq_rel);
	this->cancel_queued_background_remeshes();
	this->condition_.notify_all();
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

std::size_t WorldGenerationPipeline::active_generation_count() const noexcept
{
	return (this->active_generation_requests_.load());
}

std::size_t WorldGenerationPipeline::remesh_in_flight_count() const noexcept
{
	return (this->remesh_in_flight_.load());
}

bool WorldGenerationPipeline::has_request_or_result(uint64_t request_id)
	const noexcept
{
	{
		std::lock_guard<std::mutex> lock(this->mutex_);
		for (std::size_t index = 0U; index < this->active_request_id_count_; ++index)
			if (this->active_request_ids_[index] == request_id)
				return (true);
		for (const std::unique_ptr<Request> &request : this->requests_)
		{
			if (request != nullptr && request->request_id == request_id)
				return (true);
		}
	}
	{
		std::lock_guard<std::mutex> lock(this->results_mutex_);
		for (const std::unique_ptr<Result> &result : this->results_)
		{
			if (result != nullptr && result->request_id == request_id)
				return (true);
		}
	}
	return (false);
}

bool WorldGenerationPipeline::has_remesh_for_chunk(int32_t chunk_x,
	int32_t chunk_z) const noexcept
{
	{
		std::lock_guard<std::mutex> lock(this->mutex_);
		for (std::size_t index = 0U; index < this->active_request_id_count_; ++index)
			if (this->active_request_chunk_x_[index] == chunk_x
				&& this->active_request_chunk_z_[index] == chunk_z)
				return (true);
		for (const std::unique_ptr<Request> &request : this->requests_)
			if (request != nullptr
				&& request->operation == WorldGenerationOperation::REMESH
				&& request->chunk_x == chunk_x && request->chunk_z == chunk_z)
				return (true);
	}
	{
		std::lock_guard<std::mutex> lock(this->results_mutex_);
		for (const std::unique_ptr<Result> &result : this->results_)
			if (result != nullptr
				&& result->operation == WorldGenerationOperation::REMESH
				&& result->chunk_x == chunk_x && result->chunk_z == chunk_z)
				return (true);
	}
	return (false);
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
