#include "RuntimeAnalytics.hpp"

#if defined(LIBFT_ENABLE_ANALYTICS)

# include "../../Libft/Modules/Analytics/analytics.hpp"
# include "../../Libft/Modules/Basic/class_nullptr.hpp"
# include <atomic>
# include <cstdio>
# include <condition_variable>
# include <mutex>
# include <thread>

namespace
{
    static const uint32_t ANALYTICS_REGION_COUNT = 9U;
    analytics_session g_session;
    std::FILE *g_output = ft_nullptr;
    analytics_session g_world_session;
    std::FILE *g_world_output = ft_nullptr;
    uint32_t g_region_ids[ANALYTICS_REGION_COUNT] = {};
    uint32_t g_world_region_ids[ANALYTICS_REGION_COUNT] = {};
    uint64_t g_frame_number = 0U;
    uint64_t g_world_frame_number = 0U;
    uint32_t g_world_scope_depth = 0U;
    ft_bool g_initialised = FT_FALSE;
    ft_bool g_world_initialised = FT_FALSE;
    std::atomic<int32_t> g_output_error(FT_ERR_SUCCESS);
    std::mutex g_export_mutex;
    std::condition_variable g_export_condition;
    std::thread g_export_thread;
    ft_bool g_export_stop = FT_FALSE;
    ft_bool g_export_requested = FT_FALSE;

    const char *region_name(uint32_t region_id) noexcept
    {
        static const char *names[ANALYTICS_REGION_COUNT] = {
            "input", "world_update", "render", "frame_present",
            "application_render", "game_session_render",
            "voxel_render_world", "voxel_render_gpu",
            "voxel_render_software"};

        if (region_id >= ANALYTICS_REGION_COUNT)
            return ("unknown");
        return (names[region_id]);
    }

    void note_output_result(int result) noexcept
    {
        int32_t expected;

        expected = FT_ERR_SUCCESS;
        if (result < 0)
            g_output_error.compare_exchange_strong(expected, FT_ERR_IO);
    }

    void note_export_result(int32_t result) noexcept
    {
        int32_t expected;

        expected = FT_ERR_SUCCESS;
        if (result != FT_ERR_SUCCESS)
            g_output_error.compare_exchange_strong(expected, result);
    }

    void write_frame(const analytics_frame_statistics &frame,
        void *user_data) noexcept
    {
        std::FILE *output;
        uint32_t index;

        output = static_cast<std::FILE *>(user_data);
        if (output == ft_nullptr)
            return ;
        note_output_result(std::fprintf(output,
            "{\"type\":\"frame\",\"frame\":%llu,"
            "\"duration_ns\":%llu,\"mean_ns\":%llu,"
            "\"p95_ns\":%llu,\"p99_ns\":%llu,"
            "\"uninstrumented_ns\":%llu,\"scopes\":%llu,"
            "\"dropped_scopes\":%llu,\"breakdown\":[",
            static_cast<unsigned long long>(frame.frame_number),
            static_cast<unsigned long long>(frame.duration_nanoseconds),
            static_cast<unsigned long long>(frame.mean_duration_nanoseconds),
            static_cast<unsigned long long>(frame.percentile_95_nanoseconds),
            static_cast<unsigned long long>(frame.percentile_99_nanoseconds),
            static_cast<unsigned long long>(frame.uninstrumented_nanoseconds),
            static_cast<unsigned long long>(frame.completed_scope_count),
            static_cast<unsigned long long>(frame.dropped_scope_count)));
        index = 0U;
        while (index < frame.breakdown_count)
        {
            if (index != 0U)
                note_output_result(std::fputc(',', output));
            note_output_result(std::fprintf(output,
                "{\"region\":%u,\"name\":\"%s\",\"invocations\":%llu,"
                "\"inclusive_ns\":%llu,\"exclusive_ns\":%llu}",
                frame.breakdown[index].region_id,
                region_name(frame.breakdown[index].region_id),
                static_cast<unsigned long long>(
                    frame.breakdown[index].invocation_count),
                static_cast<unsigned long long>(
                    frame.breakdown[index].inclusive_nanoseconds),
                static_cast<unsigned long long>(
                    frame.breakdown[index].exclusive_nanoseconds)));
            index += 1U;
        }
        note_output_result(std::fprintf(output, "]}\n"));
    }

    void write_trace(const analytics_trace_event &event,
        void *user_data) noexcept
    {
        std::FILE *output;

        output = static_cast<std::FILE *>(user_data);
        if (output == ft_nullptr)
            return ;
        note_output_result(std::fprintf(output,
            "{\"type\":\"trace\",\"frame\":%llu,"
            "\"flow\":%llu,\"region\":%u,\"name\":\"%s\",\"start_ns\":%llu,"
            "\"duration_ns\":%llu,\"exclusive_ns\":%llu,"
            "\"thread\":%u}\n",
            static_cast<unsigned long long>(event.frame_number),
            static_cast<unsigned long long>(event.flow_id), event.region_id,
            region_name(event.region_id),
            static_cast<unsigned long long>(event.start_nanoseconds),
            static_cast<unsigned long long>(event.duration_nanoseconds),
            static_cast<unsigned long long>(event.exclusive_nanoseconds),
            event.thread_id));
    }

    int32_t register_regions(analytics_session &session, uint32_t *region_ids)
        noexcept
    {
        static const char *names[ANALYTICS_REGION_COUNT] = {
            "input", "world_update", "render", "frame_present",
            "application_render", "game_session_render",
            "voxel_render_world", "voxel_render_gpu",
            "voxel_render_software"};
        uint32_t index;
        int32_t error_code;

        index = 0U;
        while (index < ANALYTICS_REGION_COUNT)
        {
            error_code = session.register_region(names[index], "minecraft",
                &region_ids[index]);
            if (error_code != FT_ERR_SUCCESS)
                return (error_code);
            index += 1U;
        }
        return (FT_ERR_SUCCESS);
    }

    void export_pending_data() noexcept
    {
        std::lock_guard<std::mutex> lock(g_export_mutex);

        if (g_initialised != FT_FALSE)
            note_export_result(g_session.flush_exports());
        if (g_world_initialised != FT_FALSE)
            note_export_result(g_world_session.flush_exports());
        if (g_output != ft_nullptr && std::fflush(g_output) != 0)
            note_output_result(-1);
        if (g_world_output != ft_nullptr
            && std::fflush(g_world_output) != 0)
            note_output_result(-1);
    }

    void export_worker_main() noexcept
    {
        std::unique_lock<std::mutex> lock(g_export_mutex);

        while (g_export_stop == FT_FALSE)
        {
            g_export_condition.wait(lock, []() {
                return (g_export_stop != FT_FALSE
                    || g_export_requested != FT_FALSE);
            });
            if (g_export_stop != FT_FALSE)
                break;
            g_export_requested = FT_FALSE;
            lock.unlock();
            export_pending_data();
            lock.lock();
        }
        lock.unlock();
        export_pending_data();
    }

    void request_export() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(g_export_mutex);
            g_export_requested = FT_TRUE;
        }
        g_export_condition.notify_one();
    }
}

int32_t RuntimeAnalytics::initialize() noexcept
{
    int32_t error_code;

    if (g_initialised != FT_FALSE)
        return (FT_ERR_ALREADY_INITIALISED);
    g_output = std::fopen("minecraft_analytics.jsonl", "wb");
    if (g_output == ft_nullptr)
        return (FT_ERR_FILE_OPEN_FAILED);
    error_code = g_session.initialize();
    if (error_code == FT_ERR_SUCCESS)
        error_code = g_session.set_export_callback(write_frame, g_output);
    if (error_code == FT_ERR_SUCCESS)
        error_code = g_session.set_trace_callback(write_trace, g_output);
    if (error_code == FT_ERR_SUCCESS)
        error_code = register_regions(g_session, g_region_ids);
    if (error_code != FT_ERR_SUCCESS)
    {
        int32_t destroy_error;

        destroy_error = g_session.destroy();
        if (destroy_error != FT_ERR_SUCCESS)
            error_code = destroy_error;
        if (std::fclose(g_output) != 0 && error_code == FT_ERR_SUCCESS)
            error_code = FT_ERR_IO;
        g_output = ft_nullptr;
        return (error_code);
    }
    g_output_error.store(FT_ERR_SUCCESS);
    note_output_result(std::fprintf(g_output,
        "{\"type\":\"header\",\"version\":1}\n"));
    g_frame_number = 0U;
    g_initialised = FT_TRUE;
    {
        std::lock_guard<std::mutex> lock(g_export_mutex);

        g_export_stop = FT_FALSE;
        g_export_requested = FT_FALSE;
    }
    try
    {
        g_export_thread = std::thread(export_worker_main);
    }
    catch (...)
    {
        int32_t cleanup_error;
        int close_result;

        g_initialised = FT_FALSE;
        cleanup_error = g_session.destroy();
        close_result = std::fclose(g_output);
        g_output = ft_nullptr;
        if (cleanup_error != FT_ERR_SUCCESS)
            std::fprintf(stderr, "Analytics: worker startup cleanup failed (%d)\n",
                cleanup_error);
        if (close_result != 0)
            std::fprintf(stderr, "Analytics: worker startup file close failed\n");
        return (FT_ERR_NO_MEMORY);
    }
    return (FT_ERR_SUCCESS);
}

int32_t RuntimeAnalytics::begin_world_session() noexcept
{
    int32_t error_code;

    if (g_initialised == FT_FALSE || g_world_initialised != FT_FALSE)
        return (FT_ERR_SUCCESS);
    g_world_output = std::fopen("minecraft_world_analytics.jsonl", "wb");
    if (g_world_output == ft_nullptr)
        return (FT_ERR_FILE_OPEN_FAILED);
    error_code = g_world_session.initialize();
    if (error_code == FT_ERR_SUCCESS)
        error_code = g_world_session.set_export_callback(write_frame,
            g_world_output);
    if (error_code == FT_ERR_SUCCESS)
        error_code = g_world_session.set_trace_callback(write_trace,
            g_world_output);
    if (error_code == FT_ERR_SUCCESS)
        error_code = register_regions(g_world_session, g_world_region_ids);
    if (error_code != FT_ERR_SUCCESS)
    {
        int32_t cleanup_error;

        cleanup_error = g_world_session.destroy();
        if (cleanup_error != FT_ERR_SUCCESS)
            error_code = cleanup_error;
        if (std::fclose(g_world_output) != 0 && error_code == FT_ERR_SUCCESS)
            error_code = FT_ERR_IO;
        g_world_output = ft_nullptr;
        return (error_code);
    }
    if (std::fprintf(g_world_output,
            "{\"type\":\"header\",\"version\":1,\"session\":\"world\"}\n") < 0)
        note_output_result(-1);
    g_world_frame_number = 0U;
    g_world_scope_depth = 0U;
    g_world_initialised = FT_TRUE;
    {
        int32_t output_error = g_output_error.load();

        return (output_error == FT_ERR_SUCCESS ? FT_ERR_SUCCESS : output_error);
    }
}

int32_t RuntimeAnalytics::end_world_session() noexcept
{
    int32_t error_code;
    int32_t cleanup_error;

    if (g_world_initialised == FT_FALSE)
        return (FT_ERR_SUCCESS);
    {
        std::lock_guard<std::mutex> lock(g_export_mutex);

        error_code = g_world_session.flush_exports();
        if (std::fflush(g_world_output) != 0 && error_code == FT_ERR_SUCCESS)
            error_code = FT_ERR_IO;
        if (std::fclose(g_world_output) != 0 && error_code == FT_ERR_SUCCESS)
            error_code = FT_ERR_IO;
        g_world_output = ft_nullptr;
        cleanup_error = g_world_session.destroy();
        if (cleanup_error != FT_ERR_SUCCESS && error_code == FT_ERR_SUCCESS)
            error_code = cleanup_error;
        g_world_initialised = FT_FALSE;
        g_world_scope_depth = 0U;
    }
    return (error_code);
}

int32_t RuntimeAnalytics::shutdown() noexcept
{
    int32_t error_code;
    int32_t destroy_error;

    if (g_initialised == FT_FALSE)
        return (FT_ERR_SUCCESS);
    {
        std::lock_guard<std::mutex> lock(g_export_mutex);

        g_export_stop = FT_TRUE;
    }
    g_export_condition.notify_one();
    if (g_export_thread.joinable())
        g_export_thread.join();
    if (g_world_initialised != FT_FALSE)
    {
        int32_t world_error;

        world_error = RuntimeAnalytics::end_world_session();
        if (world_error != FT_ERR_SUCCESS)
            std::fprintf(stderr, "Analytics: world report close failed (%d)\n",
                world_error);
    }
    error_code = g_session.flush_exports();
    if (g_output_error.load() != FT_ERR_SUCCESS && error_code == FT_ERR_SUCCESS)
        error_code = g_output_error.load();
    if (std::fflush(g_output) != 0 && error_code == FT_ERR_SUCCESS)
        error_code = FT_ERR_IO;
    if (std::fclose(g_output) != 0 && error_code == FT_ERR_SUCCESS)
        error_code = FT_ERR_IO;
    g_output = ft_nullptr;
    destroy_error = g_session.destroy();
    if (destroy_error != FT_ERR_SUCCESS && error_code == FT_ERR_SUCCESS)
        error_code = destroy_error;
    g_initialised = FT_FALSE;
    return (error_code);
}

int32_t RuntimeAnalytics::begin_frame() noexcept
{
    if (g_initialised == FT_FALSE)
        return (FT_ERR_SUCCESS);
    g_frame_number += 1U;
    {
        int32_t error_code;

        error_code = analytics_begin_frame(&g_session, g_frame_number);
        if (g_world_initialised != FT_FALSE)
        {
            int32_t world_error;

            g_world_frame_number += 1U;
            world_error = analytics_begin_frame(&g_world_session,
                g_world_frame_number);
            if (world_error != FT_ERR_SUCCESS && error_code == FT_ERR_SUCCESS)
                error_code = world_error;
        }
        return (error_code);
    }
}

int32_t RuntimeAnalytics::end_frame() noexcept
{
    int32_t error_code;

    if (g_initialised == FT_FALSE)
        return (FT_ERR_SUCCESS);
    error_code = analytics_end_frame(&g_session);
    if (g_world_initialised != FT_FALSE)
    {
        int32_t world_error;

        world_error = analytics_end_frame(&g_world_session);
        if (world_error != FT_ERR_SUCCESS && error_code == FT_ERR_SUCCESS)
            error_code = world_error;
    }
    if (g_frame_number % 120U == 0U)
        request_export();
    if (g_output_error.load() != FT_ERR_SUCCESS && error_code == FT_ERR_SUCCESS)
        error_code = g_output_error.load();
    return (error_code);
}

int32_t RuntimeAnalytics::begin_scope(RuntimeAnalyticsScope scope) noexcept
{
    uint32_t scope_index;
    uint64_t timestamp;
    int32_t error_code;

    if (g_initialised == FT_FALSE)
        return (FT_ERR_SUCCESS);
    scope_index = static_cast<uint32_t>(scope);
    if (scope_index >= ANALYTICS_REGION_COUNT)
        return (FT_ERR_INVALID_ARGUMENT);
    error_code = analytics_now_nanoseconds(&timestamp);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = analytics_begin_scope_at(&g_session,
        g_region_ids[scope_index], timestamp);
    if (g_world_initialised != FT_FALSE && scope_index >= 4U)
    {
        int32_t world_error;

        world_error = analytics_begin_scope_at(&g_world_session,
            g_world_region_ids[scope_index], timestamp);
        if (world_error != FT_ERR_SUCCESS && error_code == FT_ERR_SUCCESS)
            error_code = world_error;
        else if (world_error == FT_ERR_SUCCESS)
            g_world_scope_depth += 1U;
    }
    return (error_code);
}

int32_t RuntimeAnalytics::end_scope() noexcept
{
    uint64_t timestamp;
    int32_t error_code;

    if (g_initialised == FT_FALSE)
        return (FT_ERR_SUCCESS);
    error_code = analytics_now_nanoseconds(&timestamp);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = analytics_end_scope_at(&g_session, timestamp);
    if (g_world_initialised != FT_FALSE && g_world_scope_depth > 0U)
    {
        int32_t world_error;

        world_error = analytics_end_scope_at(&g_world_session, timestamp);
        if (world_error != FT_ERR_SUCCESS && error_code == FT_ERR_SUCCESS)
            error_code = world_error;
        if (g_world_scope_depth > 0U)
            g_world_scope_depth -= 1U;
    }
    return (error_code);
}

#else

int32_t RuntimeAnalytics::initialize() noexcept { return (FT_ERR_SUCCESS); }
int32_t RuntimeAnalytics::shutdown() noexcept { return (FT_ERR_SUCCESS); }
int32_t RuntimeAnalytics::begin_world_session() noexcept { return (FT_ERR_SUCCESS); }
int32_t RuntimeAnalytics::end_world_session() noexcept { return (FT_ERR_SUCCESS); }
int32_t RuntimeAnalytics::begin_frame() noexcept { return (FT_ERR_SUCCESS); }
int32_t RuntimeAnalytics::end_frame() noexcept { return (FT_ERR_SUCCESS); }
int32_t RuntimeAnalytics::begin_scope(RuntimeAnalyticsScope) noexcept
{
    return (FT_ERR_SUCCESS);
}
int32_t RuntimeAnalytics::end_scope() noexcept { return (FT_ERR_SUCCESS); }

#endif
