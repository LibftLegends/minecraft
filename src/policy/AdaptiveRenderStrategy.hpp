#ifndef ADAPTIVE_RENDER_STRATEGY_HPP
# define ADAPTIVE_RENDER_STRATEGY_HPP

# include "../../src/policy/RenderDistanceStrategy.hpp"
# include "../../src/world/World.hpp"
# include "../ft_vox.hpp"

class AdaptiveRenderStrategy : public RenderDistanceStrategy
{
  private:
	static int32_t clamp_int(int32_t value, int32_t minimum, int32_t maximum);

  public:
	AdaptiveRenderStrategy();
	AdaptiveRenderStrategy(const AdaptiveRenderStrategy &other);
	~AdaptiveRenderStrategy() override;
	AdaptiveRenderStrategy &operator=(const AdaptiveRenderStrategy &other);

	int32_t update_render_distance(int32_t current_distance, double frame_ms,
		bool boost_enabled) const override;
	int32_t generation_budget_for_frame(double frame_ms,
		bool boost_enabled) const override;
	bool should_update_render_distance(double elapsed_since_update) const override;
};

#endif
