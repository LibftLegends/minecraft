#ifndef RENDER_DISTANCE_STRATEGY_HPP
# define RENDER_DISTANCE_STRATEGY_HPP

# include "../ft_vox.hpp"

class RenderDistanceStrategy
{
  public:
	RenderDistanceStrategy();
	RenderDistanceStrategy(const RenderDistanceStrategy &other);
	virtual ~RenderDistanceStrategy();
	RenderDistanceStrategy &operator=(const RenderDistanceStrategy &other);

	virtual int32_t update_render_distance(int32_t current_distance,
			double frame_ms, bool boost_enabled) const = 0;
	virtual int32_t generation_budget_for_frame(double frame_ms,
			bool boost_enabled) const = 0;
	virtual bool should_update_render_distance(double elapsed_since_update) const = 0;
};

#endif
