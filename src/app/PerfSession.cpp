#include "../../src/app/PerfSession.hpp"

PerfSession::PerfSession()
{
}
PerfSession::PerfSession(const PerfSession &other)
{
	(void)other;
}
PerfSession::~PerfSession()
{
}
PerfSession &PerfSession::operator=(const PerfSession &other)
{
	(void)other;
	return (*this);
}

int PerfSession::init_session(const ApplicationOptions &options,
	const LaunchSettings &launch_settings, ApplicationWindow &window,
	Camera &camera, World &world, VoxelRenderer &renderer)
{
	int32_t	error_code;

	error_code = world.initialize("integration-seed");
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("voxel world initialization",
				error_code));
	if (window.initialize(options.perf_headless_mode,
			launch_settings.resolution_width(),
			launch_settings.resolution_height(), launch_settings.fullscreen,
			true) != 0)
	{
		world.destroy();
		return (1);
	}
	camera.initialize();
	PlayerController::spawn_player_on_ground(&camera, world);
	if (window.is_gpu_mode())
	{
		error_code = renderer.initialize_gpu(window.get_gpu_window()->get_width(),
				window.get_gpu_window()->get_height());
		if (error_code != FT_ERR_SUCCESS)
		{
			world.destroy();
			window.destroy();
			return (ApplicationError::fail("GPU renderer initialization",
					error_code));
		}
	}
	renderer.preload_assets();
	return (0);
}

bool PerfSession::poll_frame_input(const ApplicationOptions &options,
	ApplicationWindow &window, LoopState &s, CameraInput &out_input)
{
	out_input = InputReader::empty_camera_input();
	if (options.perf_move_mode)
	{
		out_input.move_forward = true;
		out_input.speed_boost = options.perf_boost_mode;
	}
	if (options.perf_headless_mode)
		return (true);
	window.poll_events();
	if (window.should_close())
		return (false);
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_BOOST) == FT_TRUE)
		s.boost = !s.boost;
	ft_dumb_controls_poll();
	if (!options.perf_test_mode
		&& ft_dumb_control_was_pressed(FT_DUMB_CONTROL_BACK) == FT_TRUE)
		return (false);
	out_input = InputReader::read_camera_input(s.boost);
	return (true);
}

void PerfSession::tick_world_and_render(const ApplicationOptions &options,
	const RenderDistanceStrategy &strategy, ApplicationWindow &window,
	Camera &camera, World &world, VoxelRenderer &renderer, double dt,
	LoopState &s)
{
	if (!options.perf_headless_mode)
	{
		if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_MOUSE_PRIMARY))
			BlockInteractor::try_delete_target_block(&world, camera);
		if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_MOUSE_TERTIARY))
			BlockInteractor::try_pick_target_block(&world, camera,
				&s.sel_block);
		if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_MOUSE_SECONDARY))
			BlockInteractor::try_place_selected_block(&world, camera,
				s.sel_block);
	}
	s.frame_ms = dt * 1000.0;
	s.rd_acc += dt;
	if (strategy.should_update_render_distance(s.rd_acc))
	{
		s.active_rd = strategy.update_render_distance(s.active_rd, s.frame_ms,
				s.boost || options.perf_boost_mode);
		s.rd_acc = 0.0;
	}
	s.fps_acc += dt;
	++s.fps_frames;
	if (s.fps_acc >= 0.25)
	{
		s.display_fps = static_cast<double>(s.fps_frames) / s.fps_acc;
		s.fps_acc = 0.0;
		s.fps_frames = 0U;
	}
	(void)s.display_fps;
	renderer.render_world(window.framebuffer(), camera, world, nullptr);
	if (!options.perf_headless_mode)
		window.present();
	++s.frames;
}

bool PerfSession::tick_frame(const ApplicationOptions &options,
	const RenderDistanceStrategy &strategy, ApplicationWindow &window,
	Camera &camera, World &world, VoxelRenderer &renderer, double dt,
	LoopState &s)
{
	CameraInput	input;
	int			gen;

	if (!poll_frame_input(options, window, s, input))
		return (false);
	PlayerController::update_player_vertical_motion(&camera, input, world, dt);
	PlayerController::update_player_horizontal_motion(&camera, input, world,
		dt);
	gen = strategy.generation_budget_for_frame(s.frame_ms, s.boost
			|| options.perf_boost_mode);
	s.error_code = world.update_around(camera.x, camera.z, gen, s.active_rd);
	if (s.error_code != FT_ERR_SUCCESS)
		return (false);
	tick_world_and_render(options, strategy, window, camera, world, renderer,
		dt, s);
	return (true);
}

void PerfSession::print_results(uint64_t frames, double elapsed,
	uint64_t perf_hash)
{
	if (elapsed > 0.0)
	{
		std::printf("perf-test: frames=%llu elapsed=%.3f sec fps=%.2f "
					"frame_ms=%.3f hash=%016llx\n",
					static_cast<unsigned long long>(frames),
					elapsed,
					static_cast<double>(frames) / elapsed,
					elapsed * 1000.0
						/ static_cast<double>(frames == 0 ? 1 : frames),
					static_cast<unsigned long long>(perf_hash));
	}
	else
	{
		std::printf("perf-test: frames=%llu elapsed=0.000 sec fps=0.00 "
					"frame_ms=0.000 hash=%016llx\n",
					static_cast<unsigned long long>(frames),
					static_cast<unsigned long long>(perf_hash));
	}
}

void PerfSession::run_loop(const ApplicationOptions &options,
	const RenderDistanceStrategy &strategy, ApplicationWindow &window,
	Camera &camera, World &world, VoxelRenderer &renderer, LoopState &s)
{
	double	dt;
	double	elapsed;

	std::chrono::steady_clock::time_point prev_time;
	std::chrono::steady_clock::time_point perf_start;
	std::chrono::steady_clock::time_point now;
	prev_time = std::chrono::steady_clock::now();
	perf_start = prev_time;
	while (options.perf_headless_mode || !window.should_close())
	{
		now = std::chrono::steady_clock::now();
		dt = std::min(std::chrono::duration<double>(now - prev_time).count(),
				0.1);
		prev_time = now;
		if (!tick_frame(options, strategy, window, camera, world, renderer, dt,
				s))
			break ;
		if (options.perf_test_mode)
		{
			elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now()
					- perf_start).count();
			if (elapsed >= options.perf_seconds_limit)
				break ;
		}
	}
	if (options.perf_test_mode)
	{
		elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now()
				- perf_start).count();
		s.perf_hash = FramebufferHasher::hash_framebuffer(window.framebuffer());
		print_results(s.frames, elapsed, s.perf_hash);
	}
}

int PerfSession::run(const ApplicationOptions &options,
	const LaunchSettings &launch_settings,
	const RenderDistanceStrategy &strategy)
{
	Camera				camera;
	World				world;
	ApplicationWindow	window;
	VoxelRenderer		renderer;
	int32_t				init_err;
	LoopState			s;

	init_err = init_session(options, launch_settings, window, camera, world,
			renderer);
	if (init_err != 0)
		return (init_err);
	s = {};
	s.active_rd = WorldCoordinates::REQUIRED_VISIBLE_DISTANCE;
	s.sel_block = TERRAIN_GENERATOR_DIRT_BLOCK;
	s.error_code = FT_ERR_SUCCESS;
	run_loop(options, strategy, window, camera, world, renderer, s);
	world.destroy();
	if (!options.perf_headless_mode)
		window.destroy();
	if (s.error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("main loop", s.error_code));
	return (0);
}
