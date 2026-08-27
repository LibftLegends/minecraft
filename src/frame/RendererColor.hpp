#ifndef RENDERER_COLOR_HPP
# define RENDERER_COLOR_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../ft_vox.hpp"

class RendererColor
{
  public:
	RendererColor();
	RendererColor(const RendererColor &other);
	~RendererColor();
	RendererColor &operator=(const RendererColor &other);

	static uint32_t rgba(uint8_t red, uint8_t green, uint8_t blue);
	static uint32_t block_color(uint32_t block_id, uint8_t face);
	static uint32_t shade_color(uint32_t color, double shade);
	static double face_shade(uint8_t face);

  private:
	struct					BlockColor
	{
		uint32_t			id;
		uint8_t				r;
		uint8_t				g;
		uint8_t				b;
	};

	static const BlockColor	TABLE[];
	static const int		TABLE_COUNT;

	static uint8_t shade_channel(uint8_t channel, double shade);
	static void base_color_for_block(uint32_t block_id, uint8_t &r, uint8_t &g,
		uint8_t &b);
};

#endif
