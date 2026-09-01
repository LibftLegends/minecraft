#ifndef APPLICATION_HPP
# define APPLICATION_HPP

# include "../../src/app/ApplicationBootstrap.hpp"
# include "../../src/app/ApplicationPhaseController.hpp"
# include "../../src/app/GameSession.hpp"
# include "../../src/app/PerfSession.hpp"
# include "../../src/config/ApplicationOptions.hpp"
# include "../../src/config/LaunchSettings.hpp"
# include "../../src/diagnostics/DebugCrashHandler.hpp"
# include "../../src/diagnostics/RuntimeAnalytics.hpp"
# include "../../src/menu/MenuController.hpp"
# include "../../src/platform/ApplicationWindow.hpp"
# include "../../src/policy/RenderDistanceStrategy.hpp"
# include "../../src/policy/RenderStrategyFactory.hpp"
# include "../../src/render/VoxelRenderer.hpp"
# include "../ft_vox.hpp"

class Application
{
  public:
	Application();
	Application(const Application &other);
	~Application();
	Application &operator=(const Application &other);

	static int run(int argc, char **argv);

  private:
	static void run_single_frame(ApplicationWindow &window,
		MenuController &menu, GameSession &session, VoxelRenderer &renderer,
		const RenderDistanceStrategy &strategy,
		ApplicationPhaseController::Phase &phase, int &loading_frames,
		std::chrono::steady_clock::time_point &prev_time);
	static void run_game_loop(ApplicationWindow &window, MenuController &menu,
		GameSession &session, VoxelRenderer &renderer,
		const RenderDistanceStrategy &strategy);
	static int run_game(ApplicationOptions &options,
		LaunchSettings &launch_settings,
		const RenderDistanceStrategy &strategy);
};

#endif
