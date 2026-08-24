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
<<<<<<< Updated upstream
    (void)other;
    return *this;
}

ft_dumb_keyboard_layout Application::detect_system_layout()
{
#if defined(__APPLE__)
    CFStringRef id_ref = nullptr;
    TISInputSourceRef src = TISCopyCurrentKeyboardLayoutInputSource();
    if (src)
        id_ref =
            static_cast<CFStringRef>(TISGetInputSourceProperty(src, kTISPropertyInputSourceID));
    bool is_azerty = false;
    if (id_ref)
    {
        char buf[128] = {};
        CFStringGetCString(id_ref, buf, sizeof(buf), kCFStringEncodingUTF8);
        is_azerty =
            (std::strstr(buf, "French") != nullptr || std::strstr(buf, "AZERTY") != nullptr ||
             std::strstr(buf, "azerty") != nullptr);
    }
    if (src)
        CFRelease(src);
    if (is_azerty)
        return FT_DUMB_KEYBOARD_LAYOUT_AZERTY;
#elif !defined(_WIN32)
    const char *lang = std::getenv("LANG");
    if (!lang)
        lang = std::getenv("LC_ALL");
    if (!lang)
        lang = std::getenv("LC_MESSAGES");
    if (lang && std::strncmp(lang, "fr_", 3) == 0)
        return FT_DUMB_KEYBOARD_LAYOUT_AZERTY;
#endif
    return FT_DUMB_KEYBOARD_LAYOUT_QWERTY;
}

void Application::wait_for_escape_release()
{
#if defined(_WIN32)
    while ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0)
        Sleep(16);
#endif
}

void Application::clear_pending_quit_messages()
{
#if defined(_WIN32)
    MSG msg;
    while (PeekMessageA(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != 0)
    {
    }
#endif
}

MenuCanvas::MenuInput Application::collect_menu_input(ApplicationWindow &window)
{
    MenuCanvas::MenuInput in = {};
    int gw = (window.is_gpu_mode() && window.get_gpu_window())
                 ? window.get_gpu_window()->get_width()
                 : MenuCanvas::W;
    int gh = (window.is_gpu_mode() && window.get_gpu_window())
                 ? window.get_gpu_window()->get_height()
                 : MenuCanvas::H;
    in.mouse_x = window.mouse_x() * MenuCanvas::W / (gw > 0 ? gw : 1);
    in.mouse_y = window.mouse_y() * MenuCanvas::H / (gh > 0 ? gh : 1);
    in.mouse_clicked = window.was_mouse_clicked();
    in.key_up = ft_dumb_control_was_pressed(FT_DUMB_CONTROL_UP) == FT_TRUE;
    in.key_down = ft_dumb_control_was_pressed(FT_DUMB_CONTROL_DOWN) == FT_TRUE;
    in.key_enter = ft_dumb_control_was_pressed(FT_DUMB_CONTROL_CONFIRM) == FT_TRUE;
    in.key_escape = ft_dumb_control_was_pressed(FT_DUMB_CONTROL_BACK) == FT_TRUE;
    return in;
}

int Application::run_validators(const ApplicationOptions &options)
{
    if (options.validate_camera_speed_mode)
        return ApplicationValidator::validate_camera_speed();
    if (options.validate_collision_mode)
        return ApplicationValidator::validate_collision();
    if (options.validate_block_edit_mode)
        return ApplicationValidator::validate_block_edit();
    if (options.validate_visible_distance_mode)
        return ApplicationValidator::validate_visible_distance();
    if (options.validate_terrain_determinism_mode)
        return ApplicationValidator::validate_terrain_determinism();
    if (options.validate_world_scale_mode)
        return ApplicationValidator::validate_world_scale();
    if (options.validate_caves_mode)
        return ApplicationValidator::validate_caves();
    if (options.validate_terrain_configuration_mode)
        return ApplicationValidator::validate_terrain_configuration();
    if (options.validate_world_revision_mode)
        return ApplicationValidator::validate_world_revision();
    if (options.validate_async_generation_mode)
        return ApplicationValidator::validate_async_generation();
    return -1;
}

int Application::setup_launch_config(ApplicationOptions &options, LaunchSettings &launch_settings)
{
    const char *cfg = "ft_vox_config.json";
    if (!options.perf_test_mode && !options.perf_headless_mode)
    {
        if (launch_settings.load(cfg) != 0)
        {
            if (LaunchSettingsMenu::run(launch_settings) != 0)
                return 1;
            if (launch_settings.save(cfg) != 0)
                std::fprintf(stderr, "Application: unable to save settings to %s\n", cfg);
            wait_for_escape_release();
            clear_pending_quit_messages();
        }
    }
    if (launch_settings.keyboard_layout == FT_DUMB_KEYBOARD_LAYOUT_QWERTY)
    {
        ft_dumb_keyboard_layout detected = detect_system_layout();
        if (detected != FT_DUMB_KEYBOARD_LAYOUT_QWERTY)
            launch_settings.keyboard_layout = detected;
    }
    ft_dumb_controls_set_keyboard_layout(launch_settings.keyboard_layout);
    Settings::instance().set_keyboard_layout(launch_settings.keyboard_layout);
    return 0;
}

int Application::setup_window(ApplicationWindow &window, VoxelRenderer &renderer,
                              const LaunchSettings &launch_settings)
{
    if (window.initialize(false, launch_settings.resolution_width(),
                          launch_settings.resolution_height(), launch_settings.fullscreen,
                          true) != 0)
        return 1;
    FontRenderer::instance().load(22.0f);
    if (window.is_gpu_mode())
    {
        int32_t error_code = renderer.initialize_gpu(window.get_gpu_window()->get_width(),
                                                     window.get_gpu_window()->get_height());
        if (error_code != FT_ERR_SUCCESS)
        {
            window.destroy();
            return ApplicationError::fail("GPU renderer initialization", error_code);
        }
    }
    renderer.preload_assets();
    window.set_cursor_visible(true);
    return 0;
}

Application::Phase Application::tick_menu(Phase phase, ApplicationWindow &window,
                                          MenuController &menu, GameSession &session,
                                          VoxelRenderer &renderer, int &loading_frames,
                                          const RenderDistanceStrategy &strategy)
{
    MenuCanvas::MenuInput in = collect_menu_input(window);
    menu.update(in);
    if (phase == Phase::MAIN_MENU)
    {
        if (menu.wants_start() &&
            session.start(menu.start_seed(), window, renderer) == FT_ERR_SUCCESS)
        {
            menu.show_loading();
            menu.clear_flags();
            loading_frames = 0;
            return Phase::LOADING;
        }
    }
    else
    {
        if (session.loading_tick(strategy) != FT_ERR_SUCCESS)
        {
            session.stop();
            menu.show_main_menu();
            return Phase::MAIN_MENU;
        }
        if (session.is_ready_to_play() && ++loading_frames >= 30)
        {
            window.set_cursor_visible(false);
            return Phase::IN_GAME;
        }
    }
    return phase;
}

Application::Phase Application::tick_in_game(ApplicationWindow &window, MenuController &menu,
                                             GameSession &session, VoxelRenderer &renderer,
                                             double dt, const RenderDistanceStrategy &strategy)
{
    (void)renderer;
    GameSession::Action action = session.update(dt, strategy, window);
    if (action == GameSession::Action::OPEN_SETTINGS)
    {
        menu.show_in_game_settings();
        window.set_cursor_visible(true);
        return Phase::IN_GAME_SETTINGS;
    }
    if (action == GameSession::Action::EXIT_TO_MENU || action == GameSession::Action::FAILED)
    {
        session.stop();
        menu.show_main_menu();
        window.set_cursor_visible(true);
        return Phase::MAIN_MENU;
    }
    return Phase::IN_GAME;
}

Application::Phase Application::tick_in_game_settings(ApplicationWindow &window,
                                                      MenuController &menu, GameSession &session)
{
    MenuCanvas::MenuInput in = collect_menu_input(window);
    menu.update(in);
    ft_dumb_controls_set_keyboard_layout(Settings::instance().keyboard_layout());
    if (menu.dismissed_in_game())
    {
        menu.clear_flags();
        window.set_cursor_visible(false);
        return Phase::IN_GAME;
    }
    if (menu.wants_main_menu())
    {
        session.stop();
        menu.show_main_menu();
        menu.clear_flags();
        window.set_cursor_visible(true);
        return Phase::MAIN_MENU;
    }
    return Phase::IN_GAME_SETTINGS;
}

void Application::render_frame(Phase phase, ApplicationWindow &window, MenuController &menu,
                               GameSession &session, VoxelRenderer &renderer)
{
    GpuRenderer *gpu = renderer.get_gpu_renderer();
    if (window.is_gpu_mode() && gpu != nullptr)
    {
        int32_t window_width = window.get_gpu_window()->get_width();
        int32_t window_height = window.get_gpu_window()->get_height();
        if (window_width > 0 && window_height > 0)
            renderer.resize_gpu(window_width, window_height);
        if (phase == Phase::MAIN_MENU || phase == Phase::LOADING)
        {
            menu.render(gpu);
        }
        else if (phase == Phase::IN_GAME)
        {
            session.render(window, renderer, true);
        }
        else if (phase == Phase::IN_GAME_SETTINGS)
        {
            session.render(window, renderer, false);
            menu.render_overlay(gpu);
        }
        return;
    }
    if (phase == Phase::MAIN_MENU || phase == Phase::LOADING)
    {
        menu.render(window.framebuffer());
    }
    else if (phase == Phase::IN_GAME)
    {
        session.render(window, renderer, true);
    }
    else if (phase == Phase::IN_GAME_SETTINGS)
    {
        session.render(window, renderer, false);
        menu.render_overlay(window.framebuffer());
    }
}

Application::Phase Application::tick_phase(Phase phase, ApplicationWindow &window,
                                           MenuController &menu, GameSession &session,
                                           VoxelRenderer &renderer, double dt, int &loading_frames,
                                           const RenderDistanceStrategy &strategy)
{
    if (phase == Phase::MAIN_MENU || phase == Phase::LOADING)
        return tick_menu(phase, window, menu, session, renderer, loading_frames, strategy);
    if (phase == Phase::IN_GAME)
        return tick_in_game(window, menu, session, renderer, dt, strategy);
    return tick_in_game_settings(window, menu, session);
=======
	(void)other;
	return (*this);
>>>>>>> Stashed changes
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
