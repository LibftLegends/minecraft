#ifndef RENDER_STRATEGY_FACTORY_HPP
# define RENDER_STRATEGY_FACTORY_HPP

# include "../../src/policy/AdaptiveRenderStrategy.hpp"
# include "../../src/policy/FixedRenderStrategy.hpp"
# include "../ft_vox.hpp"

class RenderStrategyFactory
{
  public:
	RenderStrategyFactory();
	RenderStrategyFactory(const RenderStrategyFactory &other);
	~RenderStrategyFactory();
	RenderStrategyFactory &operator=(const RenderStrategyFactory &other);

	static const RenderDistanceStrategy &select(bool headless_mode);
};

#endif
