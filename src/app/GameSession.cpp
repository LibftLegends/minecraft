#include "../../src/app/GameSession.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"
#include <cstdio>

#if defined(__APPLE__)
# include <mach/mach.h>
# include <mach/task.h>
#endif

const char *GameSession::BIOME_NAMES[5] = {"PLAINS", "HILLS", "DESERT", "SNOW", "MOUNTAINS"};

namespace
{
	bool game_session_mesh_is_drawable(const chunk_mesh &mesh)
	{
		return (WorldChunk::mesh_is_drawable(mesh));
	}
}

GameSession::GameSession()
    : camera_(), player_character_(), world_(), render_debug_(),
      active_render_distance_(WorldCoordinates::REQUIRED_VISIBLE_DISTANCE), fps_accumulator_(0.0),
      rd_accumulator_(0.0), display_fps_(0.0), frame_ms_(0.0), performance_frame_ms_(16.67),
      fps_frame_count_(0U),
      selected_block_id_(VOXEL_GENERATOR_DIRT_BLOCK), boost_enabled_(false), active_(false),
      revision_preview_visible_(false), render_debug_frame_(0U), cached_ram_mb_(0U),
      cached_vram_mb_(0U), cached_biome_world_x_(0), cached_biome_world_z_(0),
      cached_biome_valid_(false), revision_preview_center_x_(0), revision_preview_center_z_(0),
      revision_preview_identifier_(0U), revision_preview_cache_valid_(false),
      revision_preview_cache_(), error_code_(FT_ERR_SUCCESS)
{
	seed_[0] = '\0';
	cached_biome_name_[0] = '\0';
}

GameSession::GameSession(const GameSession &other)
    : camera_(), player_character_(), world_(), render_debug_(),
      active_render_distance_(WorldCoordinates::REQUIRED_VISIBLE_DISTANCE), fps_accumulator_(0.0),
      rd_accumulator_(0.0), display_fps_(0.0), frame_ms_(0.0), performance_frame_ms_(16.67),
      fps_frame_count_(0U),
      selected_block_id_(VOXEL_GENERATOR_DIRT_BLOCK), boost_enabled_(false), active_(false),
      revision_preview_visible_(false), render_debug_frame_(0U), cached_ram_mb_(0U),
      cached_vram_mb_(0U), cached_biome_world_x_(0), cached_biome_world_z_(0),
      cached_biome_valid_(false), revision_preview_center_x_(0), revision_preview_center_z_(0),
      revision_preview_identifier_(0U), revision_preview_cache_valid_(false),
      revision_preview_cache_(), error_code_(FT_ERR_SUCCESS)
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
	selected_block_id_ = VOXEL_GENERATOR_DIRT_BLOCK;
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
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	std::fprintf(stderr, "World startup: initializing world\n");
	#endif
    error_code_ = world_.initialize(seed.c_str());
    if (error_code_ != FT_ERR_SUCCESS)
    {
        return error_code_;
    }
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	std::fprintf(stderr, "World startup: world initialized\n");
	#endif
    error_code_ = player_character_.initialize();
    if (error_code_ != FT_ERR_SUCCESS)
    {
        world_.destroy();
        return error_code_;
    }
    {
        int32_t analytics_error;

        analytics_error = RuntimeAnalytics::begin_world_session();
        if (analytics_error != FT_ERR_SUCCESS)
            std::fprintf(stderr, "Analytics: unable to open world report (%d)\n",
                analytics_error);
    }
    std::strncpy(seed_, seed.c_str(), sizeof(seed_) - 1);
    seed_[sizeof(seed_) - 1] = '\0';
    camera_.initialize();
    PlayerController::spawn_player_on_ground(&camera_, world_);
    sync_player_character_location();
    /* Only the minimum playable envelope is needed while loading. Streaming
     * the full configured distance here competes with the chunks that gate
     * startup and can make the loading screen appear hung. */
    active_render_distance_ = WorldCoordinates::MIN_RENDER_DISTANCE;
    boost_enabled_ = false;
    revision_preview_visible_ = false;
    render_debug_frame_ = 0U;
    cached_ram_mb_ = 0U;
    cached_vram_mb_ = 0U;
    cached_biome_valid_ = false;
    cached_biome_name_[0] = '\0';
    revision_preview_cache_valid_ = false;
    revision_preview_cache_.clear();
    selected_block_id_ = VOXEL_GENERATOR_DIRT_BLOCK;
    fps_accumulator_ = 0.0;
    fps_frame_count_ = 0U;
    rd_accumulator_ = 0.0;
    display_fps_ = 0.0;
    frame_ms_ = 0.0;
    performance_frame_ms_ = 16.67;
    error_code_ = FT_ERR_SUCCESS;
    active_ = true;
    if (window.is_gpu_mode() && window.get_gpu_window() && renderer.get_gpu_renderer() == nullptr)
    {
        error_code_ = renderer.initialize_gpu(window.get_gpu_window()->get_width(),
                                              window.get_gpu_window()->get_height());
        if (error_code_ != FT_ERR_SUCCESS)
        {
            stop();
            return error_code_;
        }
    }
    renderer.preload_assets();
    return FT_ERR_SUCCESS;
}

void GameSession::set_voxel_generation_config(const voxel_generation_config &config)
{
	this->world_.set_voxel_config(config);
}

void GameSession::stop()
{
	if (active_)
		world_.destroy();
	player_character_.destroy();
	if (active_)
	{
		int32_t analytics_error = RuntimeAnalytics::end_world_session();
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr, "Analytics: world report close failed (%d)\n",
				analytics_error);
	}
	active_ = false;
}

bool GameSession::is_active() const
{
	return (active_);
}

void GameSession::activate_configured_render_distance()
{
	if (!active_)
		return ;
	active_render_distance_ = Settings::instance().render_distance();
}

bool GameSession::is_ready_to_play() const
{
	/* Loading readiness is deliberately based on the minimum playable area,
	 * not the complete render-distance envelope. The persistent stream keeps
	 * filling distant chunks after gameplay begins. */
	const int32_t radius = WorldCoordinates::render_distance_to_chunk_radius(
		WorldCoordinates::MIN_RENDER_DISTANCE);
	const int32_t radius_sq = radius * radius;
	int32_t ready_chunks = 0;
	int32_t required_chunks = 0;

	if (!active_)
		return (false);
	for (int32_t z = -radius; z <= radius; ++z)
	{
		for (int32_t x = -radius; x <= radius; ++x)
		{
			if ((x * x) + (z * z) <= radius_sq)
			{
				const WorldChunk *chunk = world_.find_chunk(
					world_.center_chunk_x + x,
					world_.center_chunk_z + z);
				required_chunks += 1;
				/* A published chunk is not playable until its worker-produced
				 * mesh is complete. Counting only the chunk object lets loading
				 * finish while the renderer still has no geometry to draw. */
				if (chunk != nullptr
					&& game_session_mesh_is_drawable(chunk->mesh))
					ready_chunks += 1;
			}
		}
	}
	return (ready_chunks == required_chunks);
}

int GameSession::error_code() const
{
	return (error_code_);
}

int GameSession::loading_tick(const RenderDistanceStrategy &strategy)
{
	const int32_t gen = strategy.generation_budget_for_frame(
		performance_frame_ms_, boost_enabled_);
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	World::StreamDiagnostics diagnostics;
	#endif

	if (!active_)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code_ = world_.update_around(static_cast<double>(player_character_.get_x()),
			static_cast<double>(player_character_.get_z()), gen,
			active_render_distance_);
	if (error_code_ == FT_ERR_SUCCESS
		&& world_.stream_diagnostics().playable_failed_count != 0U)
	{
		std::fprintf(stderr,
			"World loading failed in the playable area; last stream error=%d\n",
			world_.stream_last_error());
		return (FT_ERR_GAME_GENERAL_ERROR);
	}
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	diagnostics = world_.stream_diagnostics();
	if (error_code_ == FT_ERR_SUCCESS && diagnostics.frame % 120U == 0U
		&& !this->is_ready_to_play())
	{
		std::fprintf(stderr,
			"World loading: frame=%llu progress=%llu loaded=%d pending=%zu ready=%zu "
			"drawable=%zu/%zu active=%zu retry=%zu failed=%zu last_error=%d "
			"result_age_ns=%llu\n",
			static_cast<unsigned long long>(diagnostics.frame),
			static_cast<unsigned long long>(diagnostics.progress_frame),
			world_.loaded_chunk_count, diagnostics.pending_count,
			diagnostics.ready_count,
			diagnostics.playable_drawable_count,
			diagnostics.playable_required_count,
			diagnostics.active_generation_count,
			diagnostics.retryable_count, diagnostics.failed_count,
			diagnostics.last_error,
			static_cast<unsigned long long>(
				diagnostics.oldest_result_age_nanoseconds));
		this->report_loading_gaps();
	}
	#endif
	return (error_code_);
}

#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
void GameSession::report_loading_gaps() const
{
	const int32_t radius = WorldCoordinates::render_distance_to_chunk_radius(
		WorldCoordinates::MIN_RENDER_DISTANCE);
	const int32_t radius_squared = radius * radius;
	int32_t offset_z;
	int32_t gap_count;
	int32_t reported;

	gap_count = 0;
	reported = 0;
	offset_z = -radius;
	while (offset_z <= radius)
	{
		int32_t offset_x = -radius;
		while (offset_x <= radius)
		{
			if (offset_x * offset_x + offset_z * offset_z <= radius_squared)
			{
				const int32_t chunk_x = world_.center_chunk_x + offset_x;
				const int32_t chunk_z = world_.center_chunk_z + offset_z;
				const WorldChunk *chunk = world_.find_chunk(chunk_x, chunk_z);
				const bool drawable = chunk != nullptr
					&& game_session_mesh_is_drawable(chunk->mesh);
				if (!drawable)
				{
					gap_count += 1;
					if (reported < 4)
					{
						std::fprintf(stderr,
							"World loading: %s playable chunk=(%d,%d)\n",
							chunk == nullptr ? "missing" : "non-drawable",
							chunk_x, chunk_z);
						reported += 1;
					}
				}
			}
			offset_x += 1;
		}
		offset_z += 1;
	}
	if (gap_count > reported)
		std::fprintf(stderr,
			"World loading: additional_playable_gaps=%d\n",
			gap_count - reported);
}
#endif

GameSession::Action GameSession::handle_navigation(ApplicationWindow &window)
{
	int32_t analytics_error;

	(void)window;
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::INPUT_DEVICE_POLL);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: input poll scope start failed (%d)\n",
			analytics_error);
	ft_dumb_controls_poll();
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: input poll scope end failed (%d)\n",
			analytics_error);
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
	int32_t analytics_error;

	world_.advance_tick();
	input = InputReader::read_camera_input(boost_enabled_);
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::PLAYER_MOTION);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: player motion scope start failed (%d)\n",
			analytics_error);
	PlayerController::update_player_vertical_motion(&camera_, input, world_,
		delta_seconds);
	PlayerController::update_player_horizontal_motion(&camera_, input, world_,
		delta_seconds);
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: player motion scope end failed (%d)\n",
			analytics_error);
	sync_player_character_location();
	active_render_distance_ = std::min(active_render_distance_,
			Settings::instance().render_distance());
	gen = strategy.generation_budget_for_frame(performance_frame_ms_,
			boost_enabled_);
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::GAME_UPDATE);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: stream scope start failed (%d)\n",
			analytics_error);
	error_code_ = world_.update_around(static_cast<double>(player_character_.get_x()),
			static_cast<double>(player_character_.get_z()), gen,
			active_render_distance_);
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: stream scope end failed (%d)\n",
			analytics_error);
	if (error_code_ != FT_ERR_SUCCESS)
		return (Action::FAILED);
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::BLOCK_INTERACTION);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: interaction scope start failed (%d)\n",
			analytics_error);
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_MOUSE_PRIMARY) == FT_TRUE)
		BlockInteractor::try_delete_target_block(&world_, camera_);
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_MOUSE_TERTIARY) == FT_TRUE)
		BlockInteractor::try_pick_target_block(&world_, camera_,
			&selected_block_id_);
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_MOUSE_SECONDARY) == FT_TRUE)
		BlockInteractor::try_place_selected_block(&world_, camera_,
			selected_block_id_);
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: interaction scope end failed (%d)\n",
			analytics_error);
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
    render_debug_frame_ += 1U;
    render_debug_.fps = display_fps_;
    render_debug_.frame_ms = frame_ms_;
    render_debug_.visible_chunks = 0;
    render_debug_.loaded_chunks = world_.loaded_chunk_count;
    render_debug_.render_distance = world_.active_render_distance;
    render_debug_.selected_block_id = selected_block_id_;
    render_debug_.camera_x = camera_.x;
    render_debug_.camera_y = camera_.y;
    render_debug_.camera_z = camera_.z;
    render_debug_.camera_speed = camera_.speed;
    render_debug_.boost_speed = camera_.speed * 20.0;
    if (render_debug_frame_ == 1U || render_debug_frame_ % 30U == 0U)
    {
        cached_ram_mb_ = SystemMemoryInfo::resident_set_mb();
        cached_vram_mb_ = renderer.get_gpu_renderer()
            ? renderer.get_gpu_renderer()->gpu_mb_approx() : 0U;
    }
    render_debug_.ram_mb = cached_ram_mb_;
    render_debug_.vram_approx_mb = cached_vram_mb_;
    std::strncpy(render_debug_.seed, seed_, sizeof(render_debug_.seed) - 1);
    render_debug_.seed[sizeof(render_debug_.seed) - 1] = '\0';

    const int32_t biome_world_x = player_character_.get_x();
    const int32_t biome_world_z = player_character_.get_z();
    if (!cached_biome_valid_
        || cached_biome_world_x_ != biome_world_x
        || cached_biome_world_z_ != biome_world_z)
    {
        const uint32_t biome_index = voxel_get_biome_index(
            world_.voxel_generation_settings(), biome_world_x,
            biome_world_z, seed_);
        const char *bname = (biome_index < 5U)
            ? BIOME_NAMES[biome_index] : nullptr;
        if (bname != nullptr)
            std::strncpy(cached_biome_name_, bname,
                sizeof(cached_biome_name_) - 1);
        else
            std::snprintf(cached_biome_name_, sizeof(cached_biome_name_),
                "CUSTOM_%u", biome_index);
        cached_biome_name_[sizeof(cached_biome_name_) - 1] = '\0';
        cached_biome_world_x_ = biome_world_x;
        cached_biome_world_z_ = biome_world_z;
        cached_biome_valid_ = true;
    }
    std::strncpy(render_debug_.biome_name, cached_biome_name_,
        sizeof(render_debug_.biome_name) - 1);
    render_debug_.biome_name[sizeof(render_debug_.biome_name) - 1] = '\0';
    render_debug_.revision_preview_visible = revision_preview_visible_;
    if (!revision_preview_visible_)
        return;
    render_debug_.revision_pending = world_.world_revision().pending;
    const int32_t center_x = WorldCoordinates::floor_divide(
        static_cast<int32_t>(std::floor(camera_.x)), GAME_VOXEL_CHUNK_WIDTH);
    const int32_t center_z = WorldCoordinates::floor_divide(
        static_cast<int32_t>(std::floor(camera_.z)), GAME_VOXEL_CHUNK_DEPTH);
    const World::WorldRevision revision = world_.world_revision();
    if (!revision_preview_cache_valid_
        || center_x != revision_preview_center_x_
        || center_z != revision_preview_center_z_
        || revision.identifier != revision_preview_identifier_
        || render_debug_frame_ % 15U == 0U)
    {
        revision_preview_cache_.clear();
        if (world_.build_revision_preview(center_x, center_z, 6,
                revision_preview_cache_) != FT_ERR_SUCCESS)
            return;
        revision_preview_center_x_ = center_x;
        revision_preview_center_z_ = center_z;
        revision_preview_identifier_ = revision.identifier;
        revision_preview_cache_valid_ = true;
    }
    render_debug_.revision_map_radius = 6;
    render_debug_.revision_protected_count = 0U;
    render_debug_.revision_selected_count = 0U;
    render_debug_.revision_transition_count = 0U;
    render_debug_.revision_unchanged_count = 0U;
    for (const World::RevisionPreviewEntry &entry : revision_preview_cache_)
    {
        const int32_t local_x = entry.chunk_x - center_x + 6;
        const int32_t local_z = entry.chunk_z - center_z + 6;
        const int32_t map_index = local_z * 13 + local_x;
        render_debug_.revision_map[map_index] =
            static_cast<uint8_t>(entry.state);
        if (entry.state == World::REVISION_PROTECTED)
            render_debug_.revision_protected_count += 1U;
        else if (entry.state == World::REVISION_SELECTED)
            render_debug_.revision_selected_count += 1U;
        else if (entry.state == World::REVISION_TRANSITION)
            render_debug_.revision_transition_count += 1U;
        else
            render_debug_.revision_unchanged_count += 1U;
    }
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
    int32_t analytics_error = RuntimeAnalytics::begin_scope(
        RuntimeAnalyticsScope::GAME_SESSION_RENDER);
    if (analytics_error != FT_ERR_SUCCESS)
        std::fprintf(stderr, "Analytics: game render scope start failed (%d)\n",
            analytics_error);
    if (!active_)
    {
        analytics_error = RuntimeAnalytics::end_scope();
        if (analytics_error != FT_ERR_SUCCESS)
            std::fprintf(stderr, "Analytics: game render scope end failed (%d)\n",
                analytics_error);
        return;
    }
    const bool debug_enabled = with_overlay
        && (Settings::instance().fps_overlay() || revision_preview_visible_);
    if (!debug_enabled)
    {
        renderer.render_world(window.framebuffer(), camera_, world_,
            static_cast<const RenderDebug *>(nullptr));
        analytics_error = RuntimeAnalytics::end_scope();
        if (analytics_error != FT_ERR_SUCCESS)
            std::fprintf(stderr, "Analytics: game render scope end failed (%d)\n",
                analytics_error);
        return;
    }
    build_render_debug(renderer);
    const RenderDebug *dbg = &render_debug_;
    renderer.render_world(window.framebuffer(), camera_, world_, dbg);
    analytics_error = RuntimeAnalytics::end_scope();
    if (analytics_error != FT_ERR_SUCCESS)
        std::fprintf(stderr, "Analytics: game render scope end failed (%d)\n",
            analytics_error);
}
