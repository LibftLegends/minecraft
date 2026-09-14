#include "../../src/world/WorldGenerationWorkerLoop.hpp"
#include <chrono>

WorldGenerationWorkerLoop::WorldGenerationWorkerLoop()
{
}

WorldGenerationWorkerLoop::WorldGenerationWorkerLoop(const WorldGenerationWorkerLoop &other)
{
	(void)other;
}

WorldGenerationWorkerLoop::~WorldGenerationWorkerLoop()
{
}

WorldGenerationWorkerLoop &WorldGenerationWorkerLoop::operator=(const WorldGenerationWorkerLoop &other)
{
	(void)other;
	return (*this);
}

bool WorldGenerationWorkerLoop::request_is_cancelled(const WorldGenerationPipeline &pipeline,
	const WorldGenerationPipeline::Request &request) noexcept
{
	if (request.cancellation_epoch != pipeline.pipeline_epoch_.load())
		return (true);
	return (request.operation
			== WorldGenerationPipeline::WorldGenerationOperation::REMESH
		&& request.remesh_interactive == FT_FALSE
		&& request.background_remesh_epoch
			!= pipeline.background_remesh_epoch_.load());
}

std::unique_ptr<WorldGenerationPipeline::Result> WorldGenerationWorkerLoop::process_request(WorldGenerationPipeline &pipeline,
	std::unique_ptr<WorldGenerationPipeline::Request> &request) noexcept
{
	if (request == nullptr)
		return (nullptr);
	if (WorldGenerationWorkerLoop::request_is_cancelled(pipeline, *request))
		return (nullptr);
	if (request->operation == WorldGenerationPipeline::WorldGenerationOperation::REMESH)
		return (WorldChunkGenerationWorker::process_remesh(*request));
	return (WorldChunkGenerationWorker::process_generation(*request));
}

void WorldGenerationWorkerLoop::run(WorldGenerationPipeline &pipeline) noexcept
{
	uint32_t generations_since_remesh;

	/* Loaded-chunk edits and their bounded lighting/remesh work are serviced
	 * before new-chunk generation.  The generation escape prevents a sustained
	 * stream of edits from starving the world as a whole. */
	generations_since_remesh = 0U;
	while (true)
	{
		std::unique_ptr<WorldGenerationPipeline::Request> request;
		std::unique_ptr<WorldGenerationPipeline::Result> retired_result;
		bool is_remesh;
		std::unique_ptr<WorldGenerationPipeline::Result> result;

		{
			std::unique_lock<std::mutex> lock(pipeline.mutex_);

			pipeline.condition_.wait(lock,
				[&pipeline]()
			{
				if (pipeline.stopping_ || !pipeline.retired_results_.empty())
					return (true);
				for (const std::unique_ptr<WorldGenerationPipeline::Request> &item
					: pipeline.requests_)
				{
					if (item != nullptr
						&& item->operation
							== WorldGenerationPipeline::WorldGenerationOperation::REMESH)
						return (true);
				}
				return (!pipeline.requests_.empty()
					&& pipeline.active_generation_requests_.load()
						< pipeline.generation_worker_limit_);
				});
			if (pipeline.stopping_ && pipeline.requests_.empty()
				&& pipeline.retired_results_.empty())
				return ;
			if (!pipeline.retired_results_.empty())
			{
				retired_result = std::move(pipeline.retired_results_.front());
				pipeline.retired_results_.pop_front();
			}
				else
			{
				std::deque<std::unique_ptr<WorldGenerationPipeline::Request>>::iterator
					selected_request;
				std::deque<std::unique_ptr<WorldGenerationPipeline::Request>>::iterator
					first_generation;
				std::deque<std::unique_ptr<WorldGenerationPipeline::Request>>::iterator
					first_remesh;
				std::deque<std::unique_ptr<WorldGenerationPipeline::Request>>::iterator
					first_interactive_remesh;

				/* Start with no selection.  When the generation quota is full and
				 * only a background remesh is eligible at a later service point,
				 * falling back to requests_.begin() would silently bypass
				 * generation_worker_limit_ and let every worker consume terrain
				 * work, starving lighting/remesh capacity. */
				selected_request = pipeline.requests_.end();
				first_generation = pipeline.requests_.end();
				first_remesh = pipeline.requests_.end();
				first_interactive_remesh = pipeline.requests_.end();
				for (std::deque<std::unique_ptr<WorldGenerationPipeline::Request>>::iterator
					iterator = pipeline.requests_.begin();
					iterator != pipeline.requests_.end(); ++iterator)
				{
					if (*iterator != nullptr
						&& (*iterator)->operation
							== WorldGenerationPipeline::WorldGenerationOperation::REMESH
						&& first_remesh == pipeline.requests_.end())
						first_remesh = iterator;
					if (*iterator != nullptr
						&& (*iterator)->operation
							== WorldGenerationPipeline::WorldGenerationOperation::REMESH
						&& (*iterator)->remesh_interactive != FT_FALSE
						&& first_interactive_remesh == pipeline.requests_.end())
						first_interactive_remesh = iterator;
					if (*iterator != nullptr
						&& (*iterator)->operation
							!= WorldGenerationPipeline::WorldGenerationOperation::REMESH
						&& first_generation == pipeline.requests_.end())
						first_generation = iterator;
				}
				if (first_interactive_remesh != pipeline.requests_.end()
					&& (first_generation == pipeline.requests_.end()
						|| generations_since_remesh < 8U))
				{
					selected_request = first_interactive_remesh;
					generations_since_remesh = 0U;
				}
				/* Background light/mesh maintenance gets a bounded service point
				 * during generation.  Waiting until the entire generation queue was
				 * empty let border lighting remain dirty for many seconds and made
				 * the renderer appear black or stale.  Interactive edits still win
				 * through the branch above; ordinary generation gets at most eight
				 * selections before one background remesh is serviced. */
				else if (first_remesh != pipeline.requests_.end()
					&& (first_generation == pipeline.requests_.end()
						|| generations_since_remesh >= 8U))
				{
					selected_request = first_remesh;
					generations_since_remesh = 0U;
				}
				else if (first_generation != pipeline.requests_.end())
				{
					if (pipeline.active_generation_requests_.load()
						< pipeline.generation_worker_limit_)
					{
						selected_request = first_generation;
						generations_since_remesh += 1U;
					}
				}
				if (selected_request == pipeline.requests_.end())
					continue ;
				request = std::move(*selected_request);
				pipeline.requests_.erase(selected_request);
				if (pipeline.active_request_id_count_
					< pipeline.active_request_ids_.size())
				{
					pipeline.active_request_ids_[pipeline.active_request_id_count_]
						= request->request_id;
					pipeline.active_request_chunk_x_[pipeline.active_request_id_count_]
						= request->chunk_x;
					pipeline.active_request_chunk_z_[pipeline.active_request_id_count_]
						= request->chunk_z;
					pipeline.active_request_id_count_ += 1U;
				}
				pipeline.active_requests_.fetch_add(1U);
				if (request->operation
					!= WorldGenerationPipeline::WorldGenerationOperation::REMESH)
					pipeline.active_generation_requests_.fetch_add(1U);
			}
		}
		/* Destruction of completed chunk payloads can release large CMA/vector
		 * allocations. Keep that work off the gameplay thread. */
		if (retired_result != nullptr)
			continue ;
		is_remesh = request->operation == WorldGenerationPipeline::WorldGenerationOperation::REMESH;
		const bool is_generation = !is_remesh;
		result = WorldGenerationWorkerLoop::process_request(pipeline, request);
		/* A pipeline epoch change cancels requests that were already active when
		 * a stream recenter/reset occurred.  process_request() deliberately
		 * returns no result for those requests, but that must not be confused
		 * with a bounded remesh slice.  Requeueing a canceled request would keep
		 * remesh_in_progress set forever and strand the chunk's pending marker. */
		const bool request_cancelled =
			WorldGenerationWorkerLoop::request_is_cancelled(pipeline, *request);
		if (request_cancelled)
			request->remesh_in_progress = FT_FALSE;
		/* An active remesh can be cancelled after it has left the pipeline
		 * queue.  Do not silently drop that request: the streamer owns a
		 * pending_mesh_request_id for it and needs the normal result-commit
		 * path to clear that marker and requeue the current revision.  The
		 * cancellation result contains metadata only; it owns no snapshot,
		 * mesh, light data, or live World pointers. */
		if (request_cancelled && is_remesh)
		{
			result.reset();
			result.reset(new (std::nothrow) WorldGenerationPipeline::Result());
			if (result != nullptr)
			{
				result->request_id = request->request_id;
				result->world_epoch = request->world_epoch;
				result->relevance_epoch = request->relevance_epoch;
				result->generation_revision = request->generation_revision;
				result->voxel_revision = request->voxel_revision;
				result->light_revision = request->light_revision;
				result->content_version = request->content_version;
				result->light_input_version = request->light_input_version;
				result->chunk_x = request->chunk_x;
				result->chunk_z = request->chunk_z;
				result->operation =
					WorldGenerationPipeline::WorldGenerationOperation::REMESH;
				result->interactive_remesh = request->remesh_interactive;
				result->error_code = FT_ERR_INVALID_STATE;
			}
		}
		if (request != nullptr && request->remesh_in_progress != FT_FALSE
			&& !request_cancelled && !pipeline.stopping_.load())
		{
			if (result != nullptr)
			{
				std::lock_guard<std::mutex> result_lock(pipeline.results_mutex_);
				result->completed_at_nanoseconds = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now().time_since_epoch()).count());
				pipeline.results_.push_back(std::move(result));
			}
			pipeline.active_requests_.fetch_sub(1U);
			if (is_generation)
				pipeline.active_generation_requests_.fetch_sub(1U);
			{
				std::lock_guard<std::mutex> lock(pipeline.mutex_);
				for (std::size_t index = 0U;
					index < pipeline.active_request_id_count_; ++index)
				{
					if (pipeline.active_request_ids_[index]
						== request->request_id)
					{
						pipeline.active_request_ids_[index] =
							pipeline.active_request_ids_[pipeline.active_request_id_count_
								- 1U];
						pipeline.active_request_chunk_x_[index] =
							pipeline.active_request_chunk_x_[pipeline.active_request_id_count_
								- 1U];
						pipeline.active_request_chunk_z_[index] =
							pipeline.active_request_chunk_z_[pipeline.active_request_id_count_
								- 1U];
						pipeline.active_request_id_count_ -= 1U;
						break ;
					}
				}
				if (request->remesh_interactive != FT_FALSE)
					pipeline.requests_.push_front(std::move(request));
				else
					pipeline.requests_.push_back(std::move(request));
			}
			pipeline.condition_.notify_one();
			continue ;
		}
		if (is_remesh)
			pipeline.release_remesh_slot();
		pipeline.active_requests_.fetch_sub(1U);
		if (is_generation)
			pipeline.active_generation_requests_.fetch_sub(1U);
		{
			std::lock_guard<std::mutex> lock(pipeline.mutex_);
			for (std::size_t index = 0U;
				index < pipeline.active_request_id_count_; ++index)
			{
				if (pipeline.active_request_ids_[index] == request->request_id)
				{
					pipeline.active_request_ids_[index] =
						pipeline.active_request_ids_[pipeline.active_request_id_count_
							- 1U];
					pipeline.active_request_chunk_x_[index] =
						pipeline.active_request_chunk_x_[pipeline.active_request_id_count_
							- 1U];
					pipeline.active_request_chunk_z_[index] =
						pipeline.active_request_chunk_z_[pipeline.active_request_id_count_
							- 1U];
					pipeline.active_request_id_count_ -= 1U;
					break ;
				}
			}
		}
		if (result == nullptr)
			continue ;
		{
			std::lock_guard<std::mutex> lock(pipeline.results_mutex_);

			if (!pipeline.stopping_.load())
			{
				result->completed_at_nanoseconds = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now().time_since_epoch()).count());
				pipeline.results_.push_back(std::move(result));
			}
		}
	}
}
