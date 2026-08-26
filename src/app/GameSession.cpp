#include "../../src/app/GameSession.hpp"

GameSession::GameSession() : camera_(), player_character_(), world_(),
	render_debug_(),
	active_render_distance_(WorldCoordinates::REQUIRED_VISIBLE_DISTANCE),
	fps_accumulator_(0.0), rd_accumulator_(0.0), display_fps_(0.0),
	frame_ms_(0.0), performance_frame_ms_(16.67), fps_frame_count_(0U),
	selected_block_id_(TERRAIN_GENERATOR_DIRT_BLOCK), boost_enabled_(false),
	active_(false), error_code_(FT_ERR_SUCCESS)
{
	seed_[0] = '\0';
}

GameSession::GameSession(const GameSession &other) : GameSession()
{
	(void)other;
}

GameSession::~GameSession()
{
	if (active_)
		stop();
}

GameSession &GameSession::operator=(const GameSession &other)
{
	(void)other;
	return (*this);
}

void GameSession::reset_session_state()
{
	active_render_distance_ = Settings::instance().render_distance();
	boost_enabled_ = false;
	selected_block_id_ = TERRAIN_GENERATOR_DIRT_BLOCK;
	fps_accumulator_ = 0.0;
	fps_frame_count_ = 0U;
	rd_accumulator_ = 0.0;
	display_fps_ = 0.0;
	frame_ms_ = 0.0;
	performance_frame_ms_ = 16.67;
	error_code_ = FT_ERR_SUCCESS;
	active_ = true;
}

int GameSession::start(const std::string &seed, ApplicationWindow &window,
	VoxelRenderer &renderer)
{
	error_code_ = world_.initialize(seed.c_str());
	if (error_code_ != FT_ERR_SUCCESS)
		return (error_code_);
	error_code_ = player_character_.initialize();
	if (error_code_ != FT_ERR_SUCCESS)
	{
		world_.destroy();
		return (error_code_);
	}
	std::strncpy(seed_, seed.c_str(), sizeof(seed_) - 1);
	seed_[sizeof(seed_) - 1] = '\0';
	camera_.initialize();
	PlayerController::spawn_player_on_ground(&camera_, world_);
	sync_player_character_location();
	reset_session_state();
	if (window.is_gpu_mode() && window.get_gpu_window()
		&& renderer.get_gpu_renderer() == nullptr)
	{
		error_code_ = renderer.initialize_gpu(window.get_gpu_window()->get_width(),
				window.get_gpu_window()->get_height());
		if (error_code_ != FT_ERR_SUCCESS)
		{
			stop();
			return (error_code_);
		}
	}
	renderer.preload_assets();
	return (FT_ERR_SUCCESS);
}

void GameSession::set_terrain_generation_config(const terrain_generation_config &config)
{
	this->world_.set_terrain_config(config);
}

void GameSession::stop()
{
	if (active_)
		world_.destroy();
	player_character_.destroy();
	active_ = false;
}

bool GameSession::is_active() const
{
	return (active_);
}

bool GameSession::is_ready_to_play() const
{
	const int32_t radius = WorldCoordinates::render_distance_to_chunk_radius(active_render_distance_);
	const int32_t radius_sq = radius * radius;
	int32_t required_chunks = 0;

	if (!active_)
		return (false);
	for (int32_t z = -radius; z <= radius; ++z)
	{
		for (int32_t x = -radius; x <= radius; ++x)
		{
			if ((x * x) + (z * z) <= radius_sq)
				required_chunks += 1;
		}
	}
	return (world_.loaded_chunk_count >= required_chunks);
}

int GameSession::error_code() const
{
	return (error_code_);
}

int GameSession::loading_tick(const RenderDistanceStrategy &strategy)
{
	const int32_t	gen = 2;

	(void)strategy;
	if (!active_)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code_ = world_.update_around(static_cast<double>(player_character_.get_x()),
			static_cast<double>(player_character_.get_z()), gen,
			active_render_distance_);
	return (error_code_);
}

GameSession::Action GameSession::handle_navigation(ApplicationWindow &window)
{
	(void)window;
	ft_dumb_controls_poll();
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_BACK) == FT_TRUE)
		return (Action::EXIT_TO_MENU);
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_CONFIRM) == FT_TRUE)
		return (Action::OPEN_SETTINGS);
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_BOOST) == FT_TRUE)
		boost_enabled_ = !boost_enabled_;
	return (Action::CONTINUE);
}

GameSession::Action GameSession::tick_world(double delta_seconds,
	const RenderDistanceStrategy &strategy)
{
	CameraInput	input;
	int			gen;

	world_.advance_tick();
	input = InputReader::read_camera_input(boost_enabled_);
	PlayerController::update_player_vertical_motion(&camera_, input, world_,
		delta_seconds);
	PlayerController::update_player_horizontal_motion(&camera_, input, world_,
		delta_seconds);
	sync_player_character_location();
	active_render_distance_ = std::min(active_render_distance_,
			Settings::instance().render_distance());
	gen = strategy.generation_budget_for_frame(performance_frame_ms_,
			boost_enabled_);
	error_code_ = world_.update_around(static_cast<double>(player_character_.get_x()),
			static_cast<double>(player_character_.get_z()), gen,
			active_render_distance_);
	if (error_code_ != FT_ERR_SUCCESS)
		return (Action::FAILED);
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_MOUSE_PRIMARY) == FT_TRUE)
		BlockInteractor::try_delete_target_block(&world_, camera_);
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_MOUSE_TERTIARY) == FT_TRUE)
		BlockInteractor::try_pick_target_block(&world_, camera_,
			&selected_block_id_);
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_MOUSE_SECONDARY) == FT_TRUE)
		BlockInteractor::try_place_selected_block(&world_, camera_,
			selected_block_id_);
	return (Action::CONTINUE);
}

void GameSession::tick_fps_counter(double delta_seconds,
	const RenderDistanceStrategy &strategy)
{
	const double	bounded_sample = std::min(delta_seconds * 1000.0, 100.0);

	rd_accumulator_ += delta_seconds;
	if (strategy.should_update_render_distance(rd_accumulator_))
	{
		active_render_distance_ = strategy.update_render_distance(active_render_distance_,
				performance_frame_ms_, boost_enabled_);
		active_render_distance_ = std::min(active_render_distance_,
				Settings::instance().render_distance());
		rd_accumulator_ = 0.0;
	}
	frame_ms_ = delta_seconds * 1000.0;
	performance_frame_ms_ += (bounded_sample - performance_frame_ms_) * 0.1;
	fps_accumulator_ += delta_seconds;
	fps_frame_count_ += 1U;
	if (fps_accumulator_ >= 0.25)
	{
		display_fps_ = static_cast<double>(fps_frame_count_) / fps_accumulator_;
		fps_accumulator_ = 0.0;
		fps_frame_count_ = 0U;
	}
}

GameSession::Action GameSession::update(double delta_seconds,
	const RenderDistanceStrategy &strategy, ApplicationWindow &window)
{
	Action	nav;
	Action	world_result;

	if (!active_)
		return (Action::FAILED);
	nav = handle_navigation(window);
	if (nav != Action::CONTINUE)
		return (nav);
	world_result = tick_world(delta_seconds, strategy);
	if (world_result != Action::CONTINUE)
		return (world_result);
	tick_fps_counter(delta_seconds, strategy);
	return (Action::CONTINUE);
}

void GameSession::build_render_debug(VoxelRenderer &renderer)
{
	GameSessionDebugInfo::build(render_debug_, camera_, world_,
		player_character_, renderer, display_fps_, frame_ms_,
		selected_block_id_, seed_);
}

void GameSession::sync_player_character_location()
{
	player_character_.set_x(static_cast<int32_t>(std::floor(camera_.x)));
	player_character_.set_y(static_cast<int32_t>(std::floor(camera_.y)));
	player_character_.set_z(static_cast<int32_t>(std::floor(camera_.z)));
}

void GameSession::render(ApplicationWindow &window, VoxelRenderer &renderer,
	bool with_overlay)
{
	const RenderDebug	*dbg;

	if (!active_)
		return ;
	build_render_debug(renderer);
	dbg = (with_overlay
			&& Settings::instance().fps_overlay()) ? &render_debug_ : nullptr;
	renderer.render_world(window.framebuffer(), camera_, world_, dbg);
}
