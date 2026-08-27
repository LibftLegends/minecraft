#ifndef TRIANGLE_TEXTURE_HPP
# define TRIANGLE_TEXTURE_HPP

# include "../ft_vox.hpp"
class	TextureAtlas;

class TriangleTexture
{
  public:
	const TextureAtlas *atlas;
	uint32_t fallback_color;
	int32_t base_x;
	int32_t base_y;
	int32_t sample_width;
	int32_t sample_height;
	double shade;
	bool loaded;

	TriangleTexture();
	TriangleTexture(const TriangleTexture &other);
	~TriangleTexture();
	TriangleTexture &operator=(const TriangleTexture &other);
};

#endif
