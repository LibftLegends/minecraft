#ifndef WORLD_GENERATION_WORKER_LOOP_HPP
# define WORLD_GENERATION_WORKER_LOOP_HPP

# include "../../src/world/WorldGenerationPipeline.hpp"

class WorldGenerationWorkerLoop
{
  public:
	WorldGenerationWorkerLoop();
	WorldGenerationWorkerLoop(const WorldGenerationWorkerLoop &other);
	~WorldGenerationWorkerLoop();
	WorldGenerationWorkerLoop &operator=(const WorldGenerationWorkerLoop &other);

	static void run(WorldGenerationPipeline &pipeline) noexcept;

  private:
	static std::unique_ptr<WorldGenerationPipeline::Result> process_request(WorldGenerationPipeline &pipeline,
		std::unique_ptr<WorldGenerationPipeline::Request> request) noexcept;
	static bool request_is_cancelled(const WorldGenerationPipeline &pipeline,
		const WorldGenerationPipeline::Request &request) noexcept;
};

#endif
