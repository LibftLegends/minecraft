#ifndef FIXED_RENDER_STRATEGY_HPP
# define FIXED_RENDER_STRATEGY_HPP

# include "../../src/policy/RenderDistanceStrategy.hpp"
# include "../ft_vox.hpp"

class FixedRenderStrategy : public RenderDistanceStrategy
{
  public:
	FixedRenderStrategy();
	FixedRenderStrategy(const FixedRenderStrategy &other);
	~FixedRenderStrategy() override;
	FixedRenderStrategy &operator=(const FixedRenderStrategy &other);

	int32_t update_render_distance(int32_t current_distance, double frame_ms,
		bool boost_enabled) const override;
	int32_t generation_budget_for_frame(double frame_ms,
		bool boost_enabled) const override;
	bool should_update_render_distance(double elapsed_since_update) const override;
};

#endif
