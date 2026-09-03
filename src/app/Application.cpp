#include "../../src/app/Application.hpp"
#include <cstdio>

namespace
{
	void finish_worldgen_probe(World &world)
	{
		const int32_t analytics_error = RuntimeAnalytics::end_world_session();
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr,
				"Worldgen probe: analytics world session shutdown failed (%d)\n",
				analytics_error);
		world.destroy();
	}
}

int Application::run_worldgen_probe()
{
	World world;
	const int32_t max_iterations = 5000;
	int32_t iteration = 0;
	bool candidate_expanded = false;
	const std::chrono::steady_clock::time_point start =
		std::chrono::steady_clock::now();

	std::fprintf(stderr, "Worldgen probe: initializing world\n");
	if (world.initialize("worldgen-probe") != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "Worldgen probe: world initialization failed\n");
		return (1);
	}
	if (RuntimeAnalytics::begin_world_session() != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"Worldgen probe: analytics world session initialization failed\n");
		finish_worldgen_probe(world);
		return (1);
	}
	while (iteration < max_iterations)
	{
		if (RuntimeAnalytics::begin_frame() != FT_ERR_SUCCESS)
		{
			std::fprintf(stderr,
				"Worldgen probe: analytics frame start failed at iteration=%d\n",
				iteration);
			finish_worldgen_probe(world);
			return (1);
		}
		const int32_t update_error = world.update_around(0.0, 0.0, 4,
			WorldCoordinates::REQUIRED_VISIBLE_DISTANCE);
		World::StreamDiagnostics diagnostics = world.stream_diagnostics();
		const int32_t frame_error = RuntimeAnalytics::end_frame();
		if (update_error != FT_ERR_SUCCESS)
		{
			std::fprintf(stderr,
				"Worldgen probe: update failed iteration=%d error=%d "
				"pending=%zu ready=%zu drawable=%zu/%zu active=%zu failed=%zu "
				"retry=%zu last_error=%d\n", iteration, update_error,
				diagnostics.pending_count, diagnostics.ready_count,
				diagnostics.playable_drawable_count,
				diagnostics.playable_required_count,
				diagnostics.active_generation_count, diagnostics.failed_count,
				diagnostics.retryable_count, diagnostics.last_error);
			finish_worldgen_probe(world);
			return (1);
		}
		if (frame_error != FT_ERR_SUCCESS)
		{
			std::fprintf(stderr,
				"Worldgen probe: analytics frame end failed at iteration=%d "
				"error=%d\n", iteration, frame_error);
			finish_worldgen_probe(world);
			return (1);
		}
		if (iteration % 100 == 0)
			std::fprintf(stderr,
				"Worldgen probe: iteration=%d loaded=%d pending=%zu ready=%zu "
				"drawable=%zu/%zu active=%zu failed=%zu retry=%zu\n", iteration,
				world.loaded_chunk_count, diagnostics.pending_count,
				diagnostics.ready_count, diagnostics.playable_drawable_count,
				diagnostics.playable_required_count,
				diagnostics.active_generation_count, diagnostics.failed_count,
				diagnostics.retryable_count);
		if (diagnostics.candidate_count > diagnostics.playable_required_count)
			candidate_expanded = true;
		if (diagnostics.playable_required_count != 0U
			&& diagnostics.playable_drawable_count
				== diagnostics.playable_required_count
			&& candidate_expanded
			&& world.loaded_chunk_count
				> static_cast<int32_t>(diagnostics.playable_required_count))
		{
			const uint64_t elapsed_ms = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - start).count());
			std::fprintf(stderr,
				"Worldgen probe: playable area ready and stream expanded after "
				"%d iterations (%llu ms), candidates=%zu loaded=%d\n", iteration,
				static_cast<unsigned long long>(elapsed_ms),
				diagnostics.candidate_count, world.loaded_chunk_count);
			finish_worldgen_probe(world);
			return (0);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		iteration += 1;
	}
	std::fprintf(stderr, "Worldgen probe: timeout after %d iterations\n",
		max_iterations);
	finish_worldgen_probe(world);
	return (1);
}

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

void Application::run_single_frame(ApplicationWindow &window,
	MenuController &menu, GameSession &session, VoxelRenderer &renderer,
	const RenderDistanceStrategy &strategy,
	ApplicationPhaseController::Phase &phase, int &loading_frames,
	std::chrono::steady_clock::time_point &prev_time)
{
	double	dt;

#if defined(LIBFT_ENABLE_ANALYTICS)
	int32_t analytics_error;

	analytics_error = RuntimeAnalytics::begin_frame();
	if (analytics_error != FT_ERR_SUCCESS)
	{
		analytics_error = RuntimeAnalytics::shutdown();
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr, "Analytics: frame start recovery failed (%d)\n",
				analytics_error);
	}
	analytics_error = RuntimeAnalytics::begin_scope(RuntimeAnalyticsScope::INPUT_PHASE);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: input scope start failed (%d)\n",
			analytics_error);
#endif
	const std::chrono::duration<double> target_frame_time(1.0 / 120.0);
	std::chrono::steady_clock::time_point frame_start;
	std::chrono::steady_clock::time_point frame_deadline;
	std::chrono::steady_clock::time_point now;
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	std::chrono::steady_clock::time_point phase_start;
	std::chrono::steady_clock::time_point render_start;
	uint64_t frame_number;
	static uint64_t diagnostic_frame_number = 0U;
	#endif
	frame_start = std::chrono::steady_clock::now();
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	frame_number = ++diagnostic_frame_number;
	#endif
	now = frame_start;
	dt = std::min(std::chrono::duration<double>(now - prev_time).count(), 0.1);
	prev_time = now;
	window.poll_events();
	#if defined(LIBFT_ENABLE_ANALYTICS)
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: input scope end failed (%d)\n",
			analytics_error);
	#endif
	if (window.should_close())
	{
	#if defined(LIBFT_ENABLE_ANALYTICS)
		analytics_error = RuntimeAnalytics::end_frame();
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr, "Analytics: frame end failed (%d)\n",
				analytics_error);
	#endif
		return ;
	}
	if (phase != ApplicationPhaseController::Phase::IN_GAME)
	{
		ft_dumb_controls_set_mouse_captured(FT_FALSE);
		ft_dumb_controls_poll();
		ft_dumb_controls_set_mouse_captured(FT_TRUE);
	}
	#if defined(LIBFT_ENABLE_ANALYTICS)
	analytics_error = RuntimeAnalytics::begin_scope(RuntimeAnalyticsScope::WORLD_UPDATE_PHASE);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: world scope start failed (%d)\n",
			analytics_error);
	#endif
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	phase_start = std::chrono::steady_clock::now();
	#endif
	phase = ApplicationPhaseController::tick_phase(phase, window, menu, session,
			renderer, dt, loading_frames, strategy);
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	{
		const uint64_t phase_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - phase_start).count());
		if (phase_us >= 500000U)
			std::fprintf(stderr,
				"[Startup] slow phase frame=%llu phase=%d duration_us=%llu\n",
				static_cast<unsigned long long>(frame_number),
				static_cast<int>(phase),
				static_cast<unsigned long long>(phase_us));
	}
	#endif
	#if defined(LIBFT_ENABLE_ANALYTICS)
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: world scope end failed (%d)\n",
			analytics_error);
	#endif
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	render_start = std::chrono::steady_clock::now();
	#endif
	ApplicationPhaseController::render_frame(phase, window, menu, session,
		renderer);
	window.present();
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	{
		const uint64_t render_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - render_start).count());
		if (render_us >= 500000U)
			std::fprintf(stderr,
				"[Startup] slow render/present frame=%llu phase=%d duration_us=%llu\n",
				static_cast<unsigned long long>(frame_number),
				static_cast<int>(phase),
				static_cast<unsigned long long>(render_us));
	}
	#endif
#if defined(LIBFT_ENABLE_ANALYTICS)
	analytics_error = RuntimeAnalytics::end_frame();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: frame end failed (%d)\n",
			analytics_error);
#endif
	frame_deadline = frame_start
		+ std::chrono::duration_cast<std::chrono::steady_clock::duration>(target_frame_time);
	std::this_thread::sleep_until(frame_deadline);
}

void Application::run_game_loop(ApplicationWindow &window, MenuController &menu,
	GameSession &session, VoxelRenderer &renderer,
	const RenderDistanceStrategy &strategy)
{
	int	loading_frames;

	ApplicationPhaseController::Phase phase;
	std::chrono::steady_clock::time_point prev_time;
	phase = ApplicationPhaseController::Phase::MAIN_MENU;
	loading_frames = 0;
	prev_time = std::chrono::steady_clock::now();
	while (!window.should_close() && !menu.wants_exit())
		run_single_frame(window, menu, session, renderer, strategy, phase,
			loading_frames, prev_time);
}

int Application::run_game(ApplicationOptions &options,
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
	if (options.auto_start)
		menu_ctl.request_start("");
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
	int								result;
	int32_t							analytics_error;
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
	analytics_error = RuntimeAnalytics::initialize(
		options.analytics_no_exporter == false ? FT_TRUE : FT_FALSE,
		options.analytics_no_instrumentation == false ? FT_TRUE : FT_FALSE);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: unable to initialize (%d)\n",
			analytics_error);
	if (options.worldgen_probe_mode)
		result = run_worldgen_probe();
	else if (options.perf_headless_mode || options.perf_test_mode)
		result = perf.run(options, launch_settings, *strategy);
	else
		result = run_game(options, launch_settings, *strategy);
	analytics_error = RuntimeAnalytics::shutdown();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: unable to save report (%d)\n",
			analytics_error);
	return (result);
}
