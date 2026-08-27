#include "../../src/geometry/TriangleTexture.hpp"

TriangleTexture::TriangleTexture() : atlas(nullptr), fallback_color(0U),
	base_x(0), base_y(0), sample_width(1), sample_height(1), shade(1.0),
	loaded(false)
{
}

TriangleTexture::TriangleTexture(const TriangleTexture &other) : atlas(nullptr),
	fallback_color(0U), base_x(0), base_y(0), sample_width(1), sample_height(1),
	shade(1.0), loaded(false)
{
	*this = other;
}

TriangleTexture::~TriangleTexture()
{
}

TriangleTexture &TriangleTexture::operator=(const TriangleTexture &other)
{
	if (this != &other)
	{
		atlas = other.atlas;
		fallback_color = other.fallback_color;
		base_x = other.base_x;
		base_y = other.base_y;
		sample_width = other.sample_width;
		sample_height = other.sample_height;
		shade = other.shade;
		loaded = other.loaded;
	}
	return (*this);
}
