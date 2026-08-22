#include "../../src/policy/FixedRenderStrategy.hpp"

FixedRenderStrategy::FixedRenderStrategy() : RenderDistanceStrategy()
{
}

FixedRenderStrategy::FixedRenderStrategy(const FixedRenderStrategy &other)
    : RenderDistanceStrategy()
{
    (void)other;
}

FixedRenderStrategy::~FixedRenderStrategy()
{
}

FixedRenderStrategy &FixedRenderStrategy::operator=(const FixedRenderStrategy &other)
{
    (void)other;
    return (*this);
}

int32_t FixedRenderStrategy::update_render_distance(int32_t current_distance, double frame_ms,
                                                    bool boost_enabled) const
{
    (void)frame_ms;
    (void)boost_enabled;
    return (current_distance);
}

int32_t FixedRenderStrategy::generation_budget_for_frame(double frame_ms, bool boost_enabled) const
{
    (void)frame_ms;
    (void)boost_enabled;
    return (1);
}

bool FixedRenderStrategy::should_update_render_distance(double elapsed_since_update) const
{
    (void)elapsed_since_update;
    return (false);
}
