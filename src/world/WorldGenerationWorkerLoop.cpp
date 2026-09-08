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
	return (request.cancellation_epoch != pipeline.pipeline_epoch_.load());
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
	uint32_t remeshes_since_generation;

	/* Loaded-chunk edits and their bounded lighting/remesh work are serviced
	 * before new-chunk generation.  The generation escape prevents a sustained
	 * stream of edits from starving the world as a whole. */
	remeshes_since_generation = 0U;
	while (true)
	{
		std::unique_ptr<WorldGenerationPipeline::Request> request;
		std::unique_ptr<WorldGenerationPipeline::Result> retired_result;
		bool is_remesh;
		std::unique_ptr<WorldGenerationPipeline::Result> result;

		{
			std::unique_lock<std::mutex> lock(pipeline.mutex_);

			pipeline.condition_.wait(lock,
				[&pipeline]() { return (pipeline.stopping_
					|| !pipeline.requests_.empty()
					|| !pipeline.retired_results_.empty()); });
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

				selected_request = pipeline.requests_.begin();
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
						|| remeshes_since_generation < 8U))
				{
					selected_request = first_interactive_remesh;
					remeshes_since_generation += 1U;
				}
				else if (first_remesh != pipeline.requests_.end()
					&& (first_generation == pipeline.requests_.end()
						|| remeshes_since_generation < 8U))
				{
					selected_request = first_remesh;
					remeshes_since_generation += 1U;
				}
				else if (first_generation != pipeline.requests_.end())
				{
					selected_request = first_generation;
					remeshes_since_generation = 0U;
				}
				request = std::move(*selected_request);
				pipeline.requests_.erase(selected_request);
				pipeline.active_requests_.fetch_add(1U);
			}
		}
		/* Destruction of completed chunk payloads can release large CMA/vector
		 * allocations. Keep that work off the gameplay thread. */
		if (retired_result != nullptr)
			continue ;
		is_remesh = request->operation == WorldGenerationPipeline::WorldGenerationOperation::REMESH;
		result = WorldGenerationWorkerLoop::process_request(pipeline, request);
		if (request != nullptr && request->remesh_in_progress != FT_FALSE
			&& !pipeline.stopping_.load())
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
			{
				std::lock_guard<std::mutex> lock(pipeline.mutex_);
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
