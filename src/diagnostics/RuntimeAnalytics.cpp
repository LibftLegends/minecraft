#include "RuntimeAnalytics.hpp"

#if defined(LIBFT_ENABLE_ANALYTICS)

# include "../../Libft/Modules/Analytics/analytics.hpp"
# include "../../Libft/Modules/Basic/class_nullptr.hpp"
# include <cstdio>

namespace
{
    static const uint32_t ANALYTICS_REGION_COUNT =
        static_cast<uint32_t>(RuntimeAnalyticsScope::COUNT);
    analytics_session g_session;
    uint32_t g_region_ids[ANALYTICS_REGION_COUNT] = {};
    uint64_t g_frame_number = 0U;
    ft_bool g_initialised = FT_FALSE;
    ft_bool g_world_initialised = FT_FALSE;

    const char *analytics_region_name(uint32_t region_id) noexcept
    {
        static const char *names[ANALYTICS_REGION_COUNT] = {
            "input", "world_update", "render", "frame_present",
            "application_render", "game_session_render",
            "voxel_render_world", "voxel_render_gpu",
            "voxel_render_software", "game_update", "player_motion",
            "world_stream_update", "block_interaction", "gpu_clear_sky",
            "gpu_batch_collect", "gpu_solid_flush", "gpu_water_flush",
            "gpu_overlay", "software_prepare", "software_meshes",
            "software_postprocess", "world_stream_drain",
            "world_stream_dispatch", "gpu_draw_crosshair", "gpu_mesh_upload",
            "world_stream_commit", "world_stream_deferred_edits",
            "gpu_batch_signature", "gpu_batch_cache_sync",
            "gpu_batch_visibility_scan", "world_stream_recenter",
            "input_device_poll"};
        static_assert(sizeof(names) / sizeof(names[0])
            == ANALYTICS_REGION_COUNT, "analytics region table mismatch");

        if (region_id >= ANALYTICS_REGION_COUNT)
            return ("unknown");
        return (names[region_id]);
    }

    int32_t register_regions(analytics_session &session,
        uint32_t *region_ids) noexcept
    {
        uint32_t index;
        int32_t error_code;

        index = 0U;
        while (index < ANALYTICS_REGION_COUNT)
        {
            error_code = session.register_region(analytics_region_name(index),
                "minecraft", &region_ids[index]);
            if (error_code != FT_ERR_SUCCESS)
                return (error_code);
            index += 1U;
        }
        return (FT_ERR_SUCCESS);
    }
}

int32_t RuntimeAnalytics::initialize(ft_bool start_exporter,
    ft_bool enable_recording) noexcept
{
    analytics_session_config configuration;
    int32_t error_code;

    if (g_initialised != FT_FALSE)
        return (FT_ERR_ALREADY_INITIALISED);
    if (enable_recording == FT_FALSE)
        return (FT_ERR_SUCCESS);
    error_code = analytics_default_session_config(&configuration);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    configuration.output_path = "minecraft_analytics.jsonl";
    configuration.world_output_path = "minecraft_world_analytics.jsonl";
    configuration.output_format = analytics_output_format::JSONL;
    /* Keep frame/region accounting continuous, but sample expensive trace
     * serialization. Detailed traces remain available on selected frames. */
    configuration.trace_frame_interval = 120U;
    configuration.frame_export_interval = 120U;
    configuration.start_exporter = start_exporter;
    error_code = g_session.initialize(configuration);
    if (error_code == FT_ERR_SUCCESS)
        error_code = register_regions(g_session, g_region_ids);
    if (error_code != FT_ERR_SUCCESS)
    {
        int32_t destroy_error;

        destroy_error = g_session.destroy();
        if (error_code == FT_ERR_SUCCESS && destroy_error != FT_ERR_SUCCESS)
            error_code = destroy_error;
        return (error_code);
    }
    g_frame_number = 0U;
    g_world_initialised = FT_FALSE;
    g_initialised = FT_TRUE;
    return (FT_ERR_SUCCESS);
}

int32_t RuntimeAnalytics::begin_world_session() noexcept
{
    int32_t error_code;

    if (g_initialised == FT_FALSE || g_world_initialised != FT_FALSE)
        return (FT_ERR_SUCCESS);
    error_code = g_session.set_world_active(FT_TRUE);
    if (error_code == FT_ERR_SUCCESS)
        g_world_initialised = FT_TRUE;
    return (error_code);
}

int32_t RuntimeAnalytics::end_world_session() noexcept
{
    int32_t error_code;

    if (g_world_initialised == FT_FALSE)
        return (FT_ERR_SUCCESS);
    error_code = g_session.set_world_active(FT_FALSE);
    if (error_code == FT_ERR_SUCCESS)
        g_world_initialised = FT_FALSE;
    return (error_code);
}

int32_t RuntimeAnalytics::shutdown() noexcept
{
    int32_t error_code;
    int32_t world_error;

    if (g_initialised == FT_FALSE)
        return (FT_ERR_SUCCESS);
    world_error = RuntimeAnalytics::end_world_session();
    error_code = g_session.destroy();
    if (error_code == FT_ERR_SUCCESS && world_error != FT_ERR_SUCCESS)
        error_code = world_error;
    g_initialised = FT_FALSE;
    return (error_code);
}

int32_t RuntimeAnalytics::begin_frame() noexcept
{
    if (g_initialised == FT_FALSE)
        return (FT_ERR_SUCCESS);
    g_frame_number += 1U;
    return (analytics_begin_frame(&g_session, g_frame_number));
}

int32_t RuntimeAnalytics::end_frame() noexcept
{
    uint64_t finalize_start;
    uint64_t finalize_end;
    uint64_t dropped_scopes;
    uint64_t dropped_traces;
    uint64_t dropped_frames;
    uint32_t export_queue_depth;
    uint64_t oldest_queue_age;
    uint32_t active_frame_count;
    uint32_t active_trace_count;
    ft_bool slow_finalize;
    int32_t error_code;

    if (g_initialised == FT_FALSE)
        return (FT_ERR_SUCCESS);
    finalize_start = g_session.now_nanoseconds();
    error_code = analytics_end_frame(&g_session);
    finalize_end = g_session.now_nanoseconds();
    if (error_code == FT_ERR_SUCCESS)
        error_code = g_session.get_export_error();
    slow_finalize = finalize_end >= finalize_start
        && finalize_end - finalize_start >= 1000000U ? FT_TRUE : FT_FALSE;
    if (slow_finalize != FT_FALSE)
    {
        dropped_scopes = g_session.get_dropped_scope_count();
        dropped_traces = g_session.get_dropped_trace_count();
        dropped_frames = g_session.get_dropped_frame_export_count();
        export_queue_depth = g_session.get_export_queue_depth();
        oldest_queue_age =
            g_session.get_oldest_export_queue_age_nanoseconds();
        active_frame_count = g_session.get_active_frame_count();
        active_trace_count = g_session.get_active_trace_count();
        std::fprintf(stderr,
            "[Analytics] frame_finalize_ns=%llu dropped_scopes=%llu "
            "dropped_traces=%llu dropped_frames=%llu queue_buffers=%u "
            "oldest_queue_age_ns=%llu active_frames=%u active_traces=%u\n",
            finalize_end - finalize_start, dropped_scopes, dropped_traces,
            dropped_frames, export_queue_depth,
            static_cast<unsigned long long>(oldest_queue_age),
            active_frame_count, active_trace_count);
    }
    return (error_code);
}

int32_t RuntimeAnalytics::begin_scope(RuntimeAnalyticsScope scope) noexcept
{
    uint32_t scope_index;
    uint64_t timestamp;

    if (g_initialised == FT_FALSE)
        return (FT_ERR_SUCCESS);
    scope_index = static_cast<uint32_t>(scope);
    if (scope_index >= ANALYTICS_REGION_COUNT)
        return (FT_ERR_INVALID_ARGUMENT);
    timestamp = g_session.now_nanoseconds();
    return (analytics_begin_scope_at(&g_session, g_region_ids[scope_index],
        timestamp));
}

int32_t RuntimeAnalytics::end_scope() noexcept
{
    uint64_t timestamp;

    if (g_initialised == FT_FALSE)
        return (FT_ERR_SUCCESS);
    timestamp = g_session.now_nanoseconds();
    return (analytics_end_scope_at(&g_session, timestamp));
}

#endif
