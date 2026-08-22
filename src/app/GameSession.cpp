#include "../../src/app/GameSession.hpp"

#if defined(__APPLE__)
# include <mach/mach.h>
# include <mach/task.h>
#endif

const char *GameSession::BIOME_NAMES[5] = {"PLAINS", "HILLS", "DESERT", "SNOW", "MOUNTAINS"};

GameSession::GameSession()
    : camera_(), player_character_(), world_(), render_debug_(),
      active_render_distance_(WorldCoordinates::REQUIRED_VISIBLE_DISTANCE), fps_accumulator_(0.0),
      rd_accumulator_(0.0), display_fps_(0.0), frame_ms_(0.0), performance_frame_ms_(16.67),
      fps_frame_count_(0U),
      selected_block_id_(TERRAIN_GENERATOR_DIRT_BLOCK), boost_enabled_(false), active_(false),
      revision_preview_visible_(false), error_code_(FT_ERR_SUCCESS)
{
    seed_[0] = '\0';
}

GameSession::GameSession(const GameSession &other)
    : camera_(), player_character_(), world_(), render_debug_(),
      active_render_distance_(WorldCoordinates::REQUIRED_VISIBLE_DISTANCE), fps_accumulator_(0.0),
      rd_accumulator_(0.0), display_fps_(0.0), frame_ms_(0.0), performance_frame_ms_(16.67),
      fps_frame_count_(0U),
      selected_block_id_(TERRAIN_GENERATOR_DIRT_BLOCK), boost_enabled_(false), active_(false),
      revision_preview_visible_(false), error_code_(FT_ERR_SUCCESS)
{
    (void)other;
    seed_[0] = '\0';
}

GameSession::~GameSession()
{
    if (active_)
        stop();
}

GameSession &GameSession::operator=(const GameSession &other)
{
    (void)other;
    return *this;
}

uint32_t GameSession::system_ram_mb()
{
#if defined(__APPLE__)
    struct task_basic_info info;
    mach_msg_type_number_t sz = TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &sz) ==
        KERN_SUCCESS)
        return static_cast<uint32_t>(info.resident_size / (1024UL * 1024UL));
#elif !defined(_WIN32)
    FILE *f = std::fopen("/proc/self/status", "r");
    if (f)
    {
        char line[256];
        while (std::fgets(line, sizeof(line), f))
        {
            if (std::strncmp(line, "VmRSS:", 6) == 0)
            {
                uint32_t kb = 0U;
                std::sscanf(line + 6, "%u", &kb);
                std::fclose(f);
                return kb / 1024U;
            }
        }
        std::fclose(f);
    }
#endif
    return 0U;
}

int GameSession::start(const std::string &seed, ApplicationWindow &window, VoxelRenderer &renderer)
{
    error_code_ = world_.initialize(seed.c_str());
    if (error_code_ != FT_ERR_SUCCESS)
        return error_code_;
    error_code_ = player_character_.initialize();
    if (error_code_ != FT_ERR_SUCCESS)
    {
        world_.destroy();
        return error_code_;
    }
    std::strncpy(seed_, seed.c_str(), sizeof(seed_) - 1);
    seed_[sizeof(seed_) - 1] = '\0';
    camera_.initialize();
    PlayerController::spawn_player_on_ground(&camera_, world_);
    sync_player_character_location();
    active_render_distance_ = Settings::instance().render_distance();
    boost_enabled_ = false;
    revision_preview_visible_ = false;
    selected_block_id_ = TERRAIN_GENERATOR_DIRT_BLOCK;
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

void GameSession::set_terrain_generation_config(
    const terrain_generation_config &config)
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
    return active_;
}
bool GameSession::is_ready_to_play() const
{
    if (!active_)
        return false;
    const int32_t radius =
        WorldCoordinates::render_distance_to_chunk_radius(active_render_distance_);
    const int32_t radius_sq = radius * radius;
    int32_t required_chunks = 0;
    for (int32_t z = -radius; z <= radius; ++z)
    {
        for (int32_t x = -radius; x <= radius; ++x)
        {
            if ((x * x) + (z * z) <= radius_sq)
                required_chunks += 1;
        }
    }
    return world_.loaded_chunk_count >= required_chunks;
}
int GameSession::error_code() const
{
    return error_code_;
}

int GameSession::loading_tick(const RenderDistanceStrategy &strategy)
{
    if (!active_)
        return FT_ERR_INVALID_ARGUMENT;
    (void)strategy;
    const int32_t gen = 2;
    error_code_ = world_.update_around(static_cast<double>(player_character_.get_x()),
                                       static_cast<double>(player_character_.get_z()), gen,
                                       active_render_distance_);
    return error_code_;
}

GameSession::Action GameSession::handle_navigation(ApplicationWindow &window)
{
    (void)window;
    ft_dumb_controls_poll();
    if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_BACK) == FT_TRUE)
        return Action::EXIT_TO_MENU;
    if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_CONFIRM) == FT_TRUE)
        return Action::OPEN_SETTINGS;
    if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_BOOST) == FT_TRUE)
        boost_enabled_ = !boost_enabled_;
    if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_SETTINGS) == FT_TRUE)
        revision_preview_visible_ = !revision_preview_visible_;
    return Action::CONTINUE;
}

GameSession::Action GameSession::tick_world(double delta_seconds,
                                            const RenderDistanceStrategy &strategy)
{
    CameraInput input = InputReader::read_camera_input(boost_enabled_);
    PlayerController::update_player_vertical_motion(&camera_, input, world_, delta_seconds);
    PlayerController::update_player_horizontal_motion(&camera_, input, world_, delta_seconds);
    sync_player_character_location();

    active_render_distance_ =
        std::min(active_render_distance_, Settings::instance().render_distance());
    int gen = strategy.generation_budget_for_frame(performance_frame_ms_, boost_enabled_);
    error_code_ = world_.update_around(static_cast<double>(player_character_.get_x()),
                                       static_cast<double>(player_character_.get_z()), gen,
                                       active_render_distance_);
    if (error_code_ != FT_ERR_SUCCESS)
        return Action::FAILED;

    if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_MOUSE_PRIMARY) == FT_TRUE)
        BlockInteractor::try_delete_target_block(&world_, camera_);
    if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_MOUSE_TERTIARY) == FT_TRUE)
        BlockInteractor::try_pick_target_block(&world_, camera_, &selected_block_id_);
    if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_MOUSE_SECONDARY) == FT_TRUE)
        BlockInteractor::try_place_selected_block(&world_, camera_, selected_block_id_);
    return Action::CONTINUE;
}

void GameSession::tick_fps_counter(double delta_seconds, const RenderDistanceStrategy &strategy)
{
    rd_accumulator_ += delta_seconds;
    if (strategy.should_update_render_distance(rd_accumulator_))
    {
        active_render_distance_ =
            strategy.update_render_distance(active_render_distance_, performance_frame_ms_,
                                            boost_enabled_);
        active_render_distance_ =
            std::min(active_render_distance_, Settings::instance().render_distance());
        rd_accumulator_ = 0.0;
    }
    frame_ms_ = delta_seconds * 1000.0;
    const double bounded_sample = std::min(frame_ms_, 100.0);
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
                                        const RenderDistanceStrategy &strategy,
                                        ApplicationWindow &window)
{
    if (!active_)
        return Action::FAILED;
    Action nav = handle_navigation(window);
    if (nav != Action::CONTINUE)
        return nav;
    Action world_result = tick_world(delta_seconds, strategy);
    if (world_result != Action::CONTINUE)
        return world_result;
    tick_fps_counter(delta_seconds, strategy);
    return Action::CONTINUE;
}

void GameSession::build_render_debug(VoxelRenderer &renderer)
{
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
    render_debug_.ram_mb = system_ram_mb();
    render_debug_.vram_approx_mb =
        renderer.get_gpu_renderer() ? renderer.get_gpu_renderer()->gpu_mb_approx() : 0U;
    std::strncpy(render_debug_.seed, seed_, sizeof(render_debug_.seed) - 1);
    render_debug_.seed[sizeof(render_debug_.seed) - 1] = '\0';

    uint32_t biome_index = terrain_get_biome_index(
        world_.terrain_generation_settings(), player_character_.get_x(),
        player_character_.get_z(), seed_);
    const char *bname = (biome_index < 5U) ? BIOME_NAMES[biome_index] : nullptr;
    if (bname != nullptr)
        std::strncpy(render_debug_.biome_name, bname,
            sizeof(render_debug_.biome_name) - 1);
    else
        std::snprintf(render_debug_.biome_name, sizeof(render_debug_.biome_name),
            "CUSTOM_%u", biome_index);
    render_debug_.biome_name[sizeof(render_debug_.biome_name) - 1] = '\0';
    render_debug_.revision_preview_visible = revision_preview_visible_;
    if (!revision_preview_visible_)
        return;
    render_debug_.revision_pending = world_.world_revision().pending;
    std::vector<World::RevisionPreviewEntry> preview;
    const int32_t center_x = WorldCoordinates::floor_divide(
        static_cast<int32_t>(std::floor(camera_.x)), GAME_VOXEL_CHUNK_WIDTH);
    const int32_t center_z = WorldCoordinates::floor_divide(
        static_cast<int32_t>(std::floor(camera_.z)), GAME_VOXEL_CHUNK_DEPTH);
    if (world_.build_revision_preview(center_x, center_z, 6, preview) != FT_ERR_SUCCESS)
        return;
    render_debug_.revision_map_radius = 6;
    render_debug_.revision_protected_count = 0U;
    render_debug_.revision_selected_count = 0U;
    render_debug_.revision_transition_count = 0U;
    render_debug_.revision_unchanged_count = 0U;
    for (const World::RevisionPreviewEntry &entry : preview)
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

void GameSession::render(ApplicationWindow &window, VoxelRenderer &renderer, bool with_overlay)
{
    if (!active_)
        return;
    build_render_debug(renderer);
    const RenderDebug *dbg =
        (with_overlay && (Settings::instance().fps_overlay() || revision_preview_visible_))
        ? &render_debug_ : nullptr;
    renderer.render_world(window.framebuffer(), camera_, world_, dbg);
}
