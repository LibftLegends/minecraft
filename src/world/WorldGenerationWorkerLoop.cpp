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
	std::unique_ptr<WorldGenerationPipeline::Request> request) noexcept
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
				request = std::move(pipeline.requests_.front());
				pipeline.requests_.pop_front();
				pipeline.active_requests_.fetch_add(1U);
			}
		}
		/* Destruction of completed chunk payloads can release large CMA/vector
		 * allocations. Keep that work off the gameplay thread. */
		if (retired_result != nullptr)
			continue ;
		is_remesh = request->operation == WorldGenerationPipeline::WorldGenerationOperation::REMESH;
		result = WorldGenerationWorkerLoop::process_request(pipeline,
				std::move(request));
		if (is_remesh)
			pipeline.remesh_in_flight_.fetch_sub(1U);
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
