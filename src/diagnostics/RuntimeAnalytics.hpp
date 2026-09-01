#ifndef RUNTIME_ANALYTICS_HPP
# define RUNTIME_ANALYTICS_HPP

# include <cstdint>
# include "../../Libft/Modules/Errno/errno.hpp"

enum class RuntimeAnalyticsScope : uint32_t
{
    INPUT_PHASE = 0U,
    WORLD_UPDATE_PHASE = 1U,
    RENDER_PHASE = 2U,
    FRAME_PRESENT_PHASE = 3U,
    APPLICATION_RENDER = 4U,
    GAME_SESSION_RENDER = 5U,
    VOXEL_RENDER_WORLD = 6U,
    VOXEL_RENDER_GPU = 7U,
    VOXEL_RENDER_SOFTWARE = 8U
};

class RuntimeAnalytics
{
  public:
    static int32_t initialize() noexcept;
    static int32_t shutdown() noexcept;
    static int32_t begin_world_session() noexcept;
    static int32_t end_world_session() noexcept;
    static int32_t begin_frame() noexcept;
    static int32_t end_frame() noexcept;
    static int32_t begin_scope(RuntimeAnalyticsScope scope) noexcept;
    static int32_t end_scope() noexcept;
};

#endif
