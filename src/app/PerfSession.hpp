#ifndef PERF_SESSION_HPP
#define PERF_SESSION_HPP

#include "../ft_vox.hpp"
#include "../../src/config/ApplicationOptions.hpp"
#include "../../src/config/LaunchSettings.hpp"
#include "../../src/platform/ApplicationWindow.hpp"
#include "../../src/render/VoxelRenderer.hpp"
#include "../../src/camera/Camera.hpp"
#include "../../src/world/World.hpp"
#include "../../src/player/PlayerController.hpp"
#include "../../src/interaction/BlockInteractor.hpp"
#include "../../src/platform/InputReader.hpp"
#include "../../src/diagnostics/FramebufferHasher.hpp"
#include "../../src/policy/RenderDistanceStrategy.hpp"
#include "../../src/diagnostics/ApplicationError.hpp"
#include <vector>

class PerfSession
{
  public:
    struct LoopState
    {
        double fps_acc;
        double rd_acc;
        double frame_ms;
        double display_fps;
        uint64_t frames;
        uint64_t fps_frames;
        uint64_t perf_hash;
        int32_t active_rd;
        int32_t error_code;
        uint32_t sel_block;
        bool boost;
        std::vector<double> frame_samples_ms;
    };

    PerfSession();
    PerfSession(const PerfSession &other);
    ~PerfSession();
    PerfSession &operator=(const PerfSession &other);

    int run(const ApplicationOptions &options, const LaunchSettings &launch_settings,
            const RenderDistanceStrategy &strategy);

  private:
    static int init_session(const ApplicationOptions &options,
                            const LaunchSettings &launch_settings, ApplicationWindow &window,
                            Camera &camera, World &world, VoxelRenderer &renderer);
    static bool tick_frame(const ApplicationOptions &options,
                           const RenderDistanceStrategy &strategy, ApplicationWindow &window,
                           Camera &camera, World &world, VoxelRenderer &renderer, double dt,
                           LoopState &s);
    static bool poll_frame_input(const ApplicationOptions &options, ApplicationWindow &window,
                                 LoopState &s, CameraInput &out_input);
    static void tick_world_and_render(const ApplicationOptions &options,
                                      const RenderDistanceStrategy &strategy,
                                      ApplicationWindow &window, Camera &camera, World &world,
                                      VoxelRenderer &renderer, double dt, LoopState &s);
    static void run_loop(const ApplicationOptions &options, const RenderDistanceStrategy &strategy,
                         ApplicationWindow &window, Camera &camera, World &world,
                         VoxelRenderer &renderer, LoopState &s);
    static void print_results(uint64_t frames, double elapsed, uint64_t perf_hash,
                              const std::vector<double> &frame_samples_ms);
};

#endif
