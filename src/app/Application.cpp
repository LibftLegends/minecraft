#include "../../src/app/Application.hpp"

Application::Application()
{
}
Application::Application(const Application &other)
{
	(void)other;
}
Application::~Application()
{
}
Application &Application::operator=(const Application &other)
{
	(void)other;
	return (*this);
}

void Application::run_game_loop(ApplicationWindow &window, MenuController &menu,
	GameSession &session, VoxelRenderer &renderer,
	const RenderDistanceStrategy &strategy)
{
	int		loading_frames;
	double	dt;

	const std::chrono::duration<double> target_frame_time(1.0 / 120.0);
	std::chrono::steady_clock::time_point frame_start;
	std::chrono::steady_clock::time_point frame_deadline;
	std::chrono::steady_clock::time_point now;
	std::chrono::steady_clock::time_point prev_time;
	ApplicationPhaseController::Phase phase;
	phase = ApplicationPhaseController::Phase::MAIN_MENU;
	loading_frames = 0;
	prev_time = std::chrono::steady_clock::now();
	while (!window.should_close() && !menu.wants_exit())
	{
		frame_start = std::chrono::steady_clock::now();
		now = std::chrono::steady_clock::now();
		dt = std::min(std::chrono::duration<double>(now - prev_time).count(),
				0.1);
		prev_time = now;
		window.poll_events();
		if (window.should_close())
			break ;
		if (phase != ApplicationPhaseController::Phase::IN_GAME)
		{
			ft_dumb_controls_set_mouse_captured(FT_FALSE);
			ft_dumb_controls_poll();
			ft_dumb_controls_set_mouse_captured(FT_TRUE);
		}
		phase = ApplicationPhaseController::tick_phase(phase, window, menu,
				session, renderer, dt, loading_frames, strategy);
		ApplicationPhaseController::render_frame(phase, window, menu, session,
			renderer);
		window.present();
		frame_deadline = frame_start
			+ std::chrono::duration_cast<std::chrono::steady_clock::duration>(target_frame_time);
		std::this_thread::sleep_until(frame_deadline);
	}
}

int Application::run_game(ApplicationOptions & /*options*/,
	LaunchSettings &launch_settings, const RenderDistanceStrategy &strategy)
{
	ApplicationWindow	window;
	VoxelRenderer		renderer;
	MenuController		menu_ctl;
	GameSession			game_session;

	if (ApplicationBootstrap::setup_window(window, renderer,
			launch_settings) != 0)
		return (1);
	menu_ctl.show_main_menu();
	run_game_loop(window, menu_ctl, game_session, renderer, strategy);
	if (game_session.is_active())
		game_session.stop();
	window.set_cursor_visible(true);
	window.destroy();
	return (0);
}

int Application::run(int argc, char **argv)
{
	ApplicationOptions				options;
	LaunchSettings					launch_settings;
	int								validator_result;
	const RenderDistanceStrategy	*strategy;
	PerfSession						perf;

	if (DebugCrashHandler::enable() != 0)
		return (1);
	if (options.parse(argc, argv) != 0)
		return (1);
	validator_result = ApplicationBootstrap::run_validators(options);
	if (validator_result >= 0)
		return (validator_result);
	if (ApplicationBootstrap::setup_launch_config(options,
			launch_settings) != 0)
		return (1);
	strategy = &RenderStrategyFactory::select(options.perf_headless_mode);
	if (options.perf_headless_mode || options.perf_test_mode)
	{
		return (perf.run(options, launch_settings, *strategy));
	}
	return (run_game(options, launch_settings, *strategy));
}
