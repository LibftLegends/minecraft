#ifndef RENDER_POLICY_HPP
# define RENDER_POLICY_HPP

# include "../../src/policy/RenderStrategyFactory.hpp"
# include "../ft_vox.hpp"

class RenderPolicy
{
  public:
	RenderPolicy();
	RenderPolicy(const RenderPolicy &other);
	~RenderPolicy();
	RenderPolicy &operator=(const RenderPolicy &other);

	static int32_t update_render_distance(int32_t current_distance,
		double frame_ms, bool boost_enabled);
	static int32_t generation_budget_for_frame(double frame_ms,
		bool boost_enabled);
};

#endif
