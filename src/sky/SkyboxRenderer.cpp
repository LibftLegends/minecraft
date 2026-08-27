#include "../../src/sky/SkyboxRenderer.hpp"

SkyboxRenderer::SkyboxRenderer()
{
}
SkyboxRenderer::SkyboxRenderer(const SkyboxRenderer &other)
{
	(void)other;
}
SkyboxRenderer::~SkyboxRenderer()
{
}
SkyboxRenderer &SkyboxRenderer::operator=(const SkyboxRenderer &other)
{
	(void)other;
	return (*this);
}

uint32_t SkyboxRenderer::skybox_row_color(int32_t y, int32_t height)
{
	double	factor;
	uint8_t	r;
	uint8_t	g;
	uint8_t	b;

	if (height <= 1)
		return (RendererColor::rgba(94U, 164U, 225U));
	factor = static_cast<double>(y) / static_cast<double>(height - 1);
	r = static_cast<uint8_t>(82.0 + (62.0 * factor));
	g = static_cast<uint8_t>(154.0 + (44.0 * factor));
	b = static_cast<uint8_t>(224.0 + (22.0 * factor));
	if (y > (height / 2))
	{
		r = static_cast<uint8_t>(66.0 + (32.0 * factor));
		g = static_cast<uint8_t>(132.0 + (38.0 * factor));
		b = static_cast<uint8_t>(202.0 + (26.0 * factor));
	}
	return (RendererColor::rgba(r, g, b));
}

const std::vector<uint32_t> &SkyboxRenderer::cached_row_colors(int32_t height)
{
	static int32_t	cached_height = 0;
	static std::vector<uint32_t>	cache;

	if (cached_height != height)
	{
		cache.resize(height > 0 ? static_cast<size_t>(height) : 0U);
		for (int32_t row = 0; row < height; ++row)
			cache[static_cast<size_t>(row)] = skybox_row_color(row, height);
		cached_height = height;
	}
	return (cache);
}

void SkyboxRenderer::draw_skybox_sun(ft_render_framebuffer &framebuffer,
	const Camera &camera)
{
	const double	SUN_RADIUS = 38.0;
	double			sun_x;
	double			sun_y;
	int32_t			x0;
	int32_t			x1;
	int32_t			y0;
	int32_t			y1;
	double			dx;
	double			dy;
	double			d2;
	size_t			idx;

	sun_x = (static_cast<double>(framebuffer.width) * 0.5)
		+ (std::sin(camera.yaw) * static_cast<double>(framebuffer.width)
			* 0.32);
	sun_y = (static_cast<double>(framebuffer.height) * 0.22) + (camera.pitch
			* static_cast<double>(framebuffer.height) * 0.18);
	x0 = static_cast<int32_t>(std::floor(sun_x - SUN_RADIUS));
	x1 = static_cast<int32_t>(std::ceil(sun_x + SUN_RADIUS));
	y0 = static_cast<int32_t>(std::floor(sun_y - SUN_RADIUS));
	y1 = static_cast<int32_t>(std::ceil(sun_y + SUN_RADIUS));
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 >= framebuffer.width)
		x1 = framebuffer.width - 1;
	if (y1 >= framebuffer.height)
		y1 = framebuffer.height - 1;
	for (int32_t y = y0; y <= y1; ++y)
	{
		for (int32_t x = x0; x <= x1; ++x)
		{
			dx = static_cast<double>(x) - sun_x;
			dy = static_cast<double>(y) - sun_y;
			d2 = dx * dx + dy * dy;
			idx = static_cast<size_t>(y * framebuffer.width + x);
			if (d2 < 360.0)
				framebuffer.pixels[idx] = RendererColor::rgba(254U, 238U, 160U);
			else if (d2 < 1440.0)
				framebuffer.pixels[idx] = RendererColor::rgba(182U, 208U, 226U);
		}
	}
}

void SkyboxRenderer::clear_frame(ft_render_framebuffer &framebuffer,
	std::vector<double> &depth_buffer, const Camera &camera)
{
	size_t						row;
	const std::vector<uint32_t>	&row_colors = cached_row_colors(
			framebuffer.height);

	std::fill(depth_buffer.begin(), depth_buffer.end(), 1.0e30);
	for (int32_t y = 0; y < framebuffer.height; ++y)
	{
		row = static_cast<size_t>(y * framebuffer.width);
		std::fill_n(framebuffer.pixels + row, framebuffer.width,
			row_colors[static_cast<size_t>(y)]);
	}
	draw_skybox_sun(framebuffer, camera);
}
