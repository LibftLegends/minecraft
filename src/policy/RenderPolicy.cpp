#include "../../src/policy/RenderPolicy.hpp"

RenderPolicy::RenderPolicy()
{
}

RenderPolicy::RenderPolicy(const RenderPolicy &other)
{
	*this = other;
}

RenderPolicy::~RenderPolicy()
{
}

RenderPolicy &RenderPolicy::operator=(const RenderPolicy &other)
{
	(void)other;
	return (*this);
}

int32_t RenderPolicy::update_render_distance(int32_t current_distance,
	double frame_ms, bool boost_enabled)
{
	return (RenderStrategyFactory::select(false).update_render_distance(current_distance,
			frame_ms, boost_enabled));
}

int32_t RenderPolicy::generation_budget_for_frame(double frame_ms,
	bool boost_enabled)
{
	return (RenderStrategyFactory::select(false).generation_budget_for_frame(frame_ms,
			boost_enabled));
}
