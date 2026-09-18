#ifndef GAME_SESSION_HPP
# define GAME_SESSION_HPP

#include "../ft_vox.hpp"
#include "../../src/camera/Camera.hpp"
#include "../../src/world/World.hpp"
#include "../../src/debug/RenderDebug.hpp"
#include "../../src/diagnostics/SystemMemoryInfo.hpp"
#include "../../src/platform/InputReader.hpp"
#include "../../src/platform/ApplicationWindow.hpp"
#include "../../src/render/VoxelRenderer.hpp"
#include "../../src/player/PlayerController.hpp"
#include "../../src/physics/PlayerCollision.hpp"
#include "../../src/interaction/BlockInteractor.hpp"
#include "../../src/policy/RenderDistanceStrategy.hpp"
#include "../../src/settings/Settings.hpp"
#include "../../Libft/Modules/Game/game_character.hpp"
#include <vector>

class GameSession
{
  public:
	static const char *BIOME_NAMES[5];

	enum class Action
	{
		CONTINUE,
		OPEN_SETTINGS,
		EXIT_TO_MENU,
		FAILED
	};

	GameSession();
	GameSession(const GameSession &other);
	~GameSession();
	GameSession &operator=(const GameSession &other);

	int start(const std::string &seed, ApplicationWindow &window,
		VoxelRenderer &renderer);
	void set_voxel_generation_config(const voxel_generation_config &config);
	void stop();
	bool is_active() const;
	bool is_ready_to_play() const;
	void activate_configured_render_distance();
	int error_code() const;

	int loading_tick(const RenderDistanceStrategy &strategy);
	Action update(double delta_seconds, const RenderDistanceStrategy &strategy,
		ApplicationWindow &window);
	void render(ApplicationWindow &window, VoxelRenderer &renderer,
		bool with_overlay);

  private:
	Camera camera_;
	game_character player_character_;
	World world_;
	RenderDebug render_debug_;

    int32_t active_render_distance_;
    double fps_accumulator_;
    double rd_accumulator_;
    double display_fps_;
    double frame_ms_;
    double performance_frame_ms_;
    uint64_t fps_frame_count_;
    uint32_t selected_block_id_;
    bool boost_enabled_;
    bool active_;
    bool revision_preview_visible_;
    uint64_t render_debug_frame_;
    uint32_t cached_ram_mb_;
    uint32_t cached_vram_mb_;
    int32_t cached_biome_world_x_;
    int32_t cached_biome_world_z_;
    bool cached_biome_valid_;
    char cached_biome_name_[24];
    int32_t revision_preview_center_x_;
    int32_t revision_preview_center_z_;
    uint32_t revision_preview_identifier_;
    bool revision_preview_cache_valid_;
    std::vector<World::RevisionPreviewEntry> revision_preview_cache_;
    int32_t error_code_;
    char seed_[32];

	Action handle_navigation(ApplicationWindow &window);
	Action tick_world(double delta_seconds,
		const RenderDistanceStrategy &strategy);
	void tick_fps_counter(double delta_seconds,
		const RenderDistanceStrategy &strategy);
	void build_render_debug(VoxelRenderer &renderer);
	void sync_player_character_location();
	void reset_session_state();
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	void report_loading_gaps() const;
#endif
};

#endif
