#include "../../src/policy/AdaptiveRenderStrategy.hpp"

AdaptiveRenderStrategy::AdaptiveRenderStrategy() : RenderDistanceStrategy()
{
}

AdaptiveRenderStrategy::AdaptiveRenderStrategy(const AdaptiveRenderStrategy &other)
    : RenderDistanceStrategy()
{
    (void)other;
}

AdaptiveRenderStrategy::~AdaptiveRenderStrategy()
{
}

AdaptiveRenderStrategy &AdaptiveRenderStrategy::operator=(const AdaptiveRenderStrategy &other)
{
    (void)other;
    return (*this);
}

int32_t AdaptiveRenderStrategy::clamp_int(int32_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum)
        return (minimum);
    if (value > maximum)
        return (maximum);
    return (value);
}

int32_t AdaptiveRenderStrategy::update_render_distance(int32_t current_distance, double frame_ms,
                                                       bool boost_enabled) const
{
    int32_t next_distance;

    next_distance = current_distance;
    if (frame_ms > 30.0 || boost_enabled == true)
        next_distance = next_distance - 16;
    else if (frame_ms > 22.0)
        next_distance = next_distance - 16;
    else if (frame_ms < 11.0)
        next_distance = next_distance + 16;
    return (clamp_int(next_distance, WorldCoordinates::MIN_RENDER_DISTANCE,
                      WorldCoordinates::REQUIRED_VISIBLE_DISTANCE));
}

int32_t AdaptiveRenderStrategy::generation_budget_for_frame(double frame_ms,
                                                            bool boost_enabled) const
{
    if (frame_ms > 30.0)
        return (1);
    if (frame_ms > 20.0)
        return (1);
    if (boost_enabled == true)
        return (3);
    return (2);
}

bool AdaptiveRenderStrategy::should_update_render_distance(double elapsed_since_update) const
{
    return (elapsed_since_update >= 1.0);
}
