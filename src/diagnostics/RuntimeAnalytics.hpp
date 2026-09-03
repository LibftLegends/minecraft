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
    VOXEL_RENDER_SOFTWARE = 8U,
    GAME_UPDATE = 9U,
    PLAYER_MOTION = 10U,
    WORLD_STREAM_UPDATE = 11U,
    BLOCK_INTERACTION = 12U,
    GPU_CLEAR_SKY = 13U,
    GPU_BATCH_COLLECT = 14U,
    GPU_SOLID_FLUSH = 15U,
    GPU_WATER_FLUSH = 16U,
    GPU_OVERLAY = 17U,
    SOFTWARE_PREPARE = 18U,
    SOFTWARE_MESHES = 19U,
    SOFTWARE_POSTPROCESS = 20U,
    WORLD_STREAM_DRAIN = 21U,
    WORLD_STREAM_DISPATCH = 22U,
    GPU_DRAW_CROSSHAIR = 23U,
    GPU_MESH_UPLOAD = 24U,
    WORLD_STREAM_COMMIT = 25U,
    WORLD_STREAM_DEFERRED_EDITS = 26U,
    GPU_BATCH_SIGNATURE = 27U,
    GPU_BATCH_CACHE_SYNC = 28U,
    GPU_BATCH_VISIBILITY_SCAN = 29U,
    WORLD_STREAM_RECENTER = 30U,
    INPUT_DEVICE_POLL = 31U,
    COUNT = 32U
};

class RuntimeAnalytics
{
  public:
#if defined(LIBFT_ENABLE_ANALYTICS)
    static int32_t initialize(ft_bool start_exporter = FT_TRUE,
        ft_bool enable_recording = FT_TRUE) noexcept;
    static int32_t shutdown() noexcept;
    static int32_t begin_world_session() noexcept;
    static int32_t end_world_session() noexcept;
    static int32_t begin_frame() noexcept;
    static int32_t end_frame() noexcept;
    static int32_t begin_scope(RuntimeAnalyticsScope scope) noexcept;
    static int32_t end_scope() noexcept;
#else
    static int32_t initialize(ft_bool = FT_TRUE, ft_bool = FT_TRUE) noexcept
    {
        return (FT_ERR_SUCCESS);
    }
    static int32_t shutdown() noexcept { return (FT_ERR_SUCCESS); }
    static int32_t begin_world_session() noexcept { return (FT_ERR_SUCCESS); }
    static int32_t end_world_session() noexcept { return (FT_ERR_SUCCESS); }
    static int32_t begin_frame() noexcept { return (FT_ERR_SUCCESS); }
    static int32_t end_frame() noexcept { return (FT_ERR_SUCCESS); }
    static int32_t begin_scope(RuntimeAnalyticsScope) noexcept
    {
        return (FT_ERR_SUCCESS);
    }
    static int32_t end_scope() noexcept { return (FT_ERR_SUCCESS); }
#endif
};

#endif
