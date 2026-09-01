#include "../../src/app/ApplicationPhaseController.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"
#include <cstdio>

ApplicationPhaseController::ApplicationPhaseController()
{
}

ApplicationPhaseController::ApplicationPhaseController(const ApplicationPhaseController &other)
{
	(void)other;
}

ApplicationPhaseController::~ApplicationPhaseController()
{
}

ApplicationPhaseController &ApplicationPhaseController::operator=(const ApplicationPhaseController &other)
{
	(void)other;
	return (*this);
}

MenuCanvas::MenuInput ApplicationPhaseController::collect_menu_input(ApplicationWindow &window)
{
	int	gw;
	int	gh;

	MenuCanvas::MenuInput in = {};
	gw = (window.is_gpu_mode()
			&& window.get_gpu_window()) ? window.get_gpu_window()->get_width() : MenuCanvas::W;
	gh = (window.is_gpu_mode()
			&& window.get_gpu_window()) ? window.get_gpu_window()->get_height() : MenuCanvas::H;
	in.mouse_x = window.mouse_x() * MenuCanvas::W / (gw > 0 ? gw : 1);
	in.mouse_y = window.mouse_y() * MenuCanvas::H / (gh > 0 ? gh : 1);
	in.mouse_clicked = window.was_mouse_clicked();
	in.key_up = ft_dumb_control_was_pressed(FT_DUMB_CONTROL_UP) == FT_TRUE;
	in.key_down = ft_dumb_control_was_pressed(FT_DUMB_CONTROL_DOWN) == FT_TRUE;
	in.key_enter = ft_dumb_control_was_pressed(FT_DUMB_CONTROL_CONFIRM) == FT_TRUE;
	in.key_escape = ft_dumb_control_was_pressed(FT_DUMB_CONTROL_BACK) == FT_TRUE;
	return (in);
}

ApplicationPhaseController::Phase ApplicationPhaseController::tick_menu(Phase phase,
	ApplicationWindow &window, MenuController &menu, GameSession &session,
	VoxelRenderer &renderer, int &loading_frames,
	const RenderDistanceStrategy &strategy)
{
	MenuCanvas::MenuInput in = collect_menu_input(window);
	menu.update(in);
	if (phase == Phase::MAIN_MENU)
	{
		if (menu.wants_start() && session.start(menu.start_seed(), window,
				renderer) == FT_ERR_SUCCESS)
		{
			menu.show_loading();
			menu.clear_flags();
			loading_frames = 0;
			return (Phase::LOADING);
		}
		return (phase);
	}
	if (session.loading_tick(strategy) != FT_ERR_SUCCESS)
	{
		session.stop();
		menu.show_main_menu();
		return (Phase::MAIN_MENU);
	}
	if (session.is_ready_to_play() && ++loading_frames >= 30)
	{
		window.set_cursor_visible(false);
		return (Phase::IN_GAME);
	}
	return (phase);
}

ApplicationPhaseController::Phase ApplicationPhaseController::tick_in_game(ApplicationWindow &window,
	MenuController &menu, GameSession &session, VoxelRenderer &renderer,
	double dt, const RenderDistanceStrategy &strategy)
{
	(void)renderer;
	GameSession::Action action = session.update(dt, strategy, window);
	if (action == GameSession::Action::OPEN_SETTINGS)
	{
		menu.show_in_game_settings();
		window.set_cursor_visible(true);
		return (Phase::IN_GAME_SETTINGS);
	}
	if (action == GameSession::Action::EXIT_TO_MENU
		|| action == GameSession::Action::FAILED)
	{
		session.stop();
		menu.show_main_menu();
		window.set_cursor_visible(true);
		return (Phase::MAIN_MENU);
	}
	return (Phase::IN_GAME);
}

ApplicationPhaseController::Phase ApplicationPhaseController::tick_in_game_settings(ApplicationWindow &window,
	MenuController &menu, GameSession &session)
{
	MenuCanvas::MenuInput in = collect_menu_input(window);
	menu.update(in);
	ft_dumb_controls_set_keyboard_layout(Settings::instance().keyboard_layout());
	if (menu.dismissed_in_game())
	{
		menu.clear_flags();
		window.set_cursor_visible(false);
		return (Phase::IN_GAME);
	}
	if (menu.wants_main_menu())
	{
		session.stop();
		menu.show_main_menu();
		menu.clear_flags();
		window.set_cursor_visible(true);
		return (Phase::MAIN_MENU);
	}
	return (Phase::IN_GAME_SETTINGS);
}

ApplicationPhaseController::Phase ApplicationPhaseController::tick_phase(Phase phase,
	ApplicationWindow &window, MenuController &menu, GameSession &session,
	VoxelRenderer &renderer, double dt, int &loading_frames,
	const RenderDistanceStrategy &strategy)
{
	if (phase == Phase::MAIN_MENU || phase == Phase::LOADING)
		return (tick_menu(phase, window, menu, session, renderer,
				loading_frames, strategy));
	if (phase == Phase::IN_GAME)
		return (tick_in_game(window, menu, session, renderer, dt, strategy));
	return (tick_in_game_settings(window, menu, session));
}

void ApplicationPhaseController::render_gpu_frame(Phase phase,
	ApplicationWindow &window, MenuController &menu, GameSession &session,
	VoxelRenderer &renderer)
{
	GpuRenderer	*gpu;
	int32_t		window_width;
	int32_t		window_height;

	gpu = renderer.get_gpu_renderer();
	window_width = window.get_gpu_window()->get_width();
	window_height = window.get_gpu_window()->get_height();
	if (window_width > 0 && window_height > 0)
		renderer.resize_gpu(window_width, window_height);
	if (phase == Phase::MAIN_MENU || phase == Phase::LOADING)
		menu.render(gpu);
	else if (phase == Phase::IN_GAME)
		session.render(window, renderer, true);
	else if (phase == Phase::IN_GAME_SETTINGS)
	{
		session.render(window, renderer, false);
		menu.render_overlay(gpu);
	}
}

void ApplicationPhaseController::render_cpu_frame(Phase phase,
	ApplicationWindow &window, MenuController &menu, GameSession &session,
	VoxelRenderer &renderer)
{
	if (phase == Phase::MAIN_MENU || phase == Phase::LOADING)
		menu.render(window.framebuffer());
	else if (phase == Phase::IN_GAME)
		session.render(window, renderer, true);
	else if (phase == Phase::IN_GAME_SETTINGS)
	{
		session.render(window, renderer, false);
		menu.render_overlay(window.framebuffer());
	}
}

void ApplicationPhaseController::render_frame(Phase phase,
	ApplicationWindow &window, MenuController &menu, GameSession &session,
	VoxelRenderer &renderer)
{
	GpuRenderer	*gpu;
	int32_t analytics_error;

	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::APPLICATION_RENDER);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: application render scope start failed (%d)\n",
			analytics_error);
	gpu = renderer.get_gpu_renderer();
	if (window.is_gpu_mode() && gpu != nullptr)
		render_gpu_frame(phase, window, menu, session, renderer);
	else
		render_cpu_frame(phase, window, menu, session, renderer);
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: application render scope end failed (%d)\n",
			analytics_error);
}
