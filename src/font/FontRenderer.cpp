#include "../../src/font/FontRenderer.hpp"

FontRenderer::FontRenderer() : real_font_(), pixel_height_(0.0f),
	loaded_(false), use_real_font_(false)
{
}

FontRenderer::FontRenderer(const FontRenderer &other) : real_font_(),
	pixel_height_(0.0f), loaded_(false), use_real_font_(false)
{
	(void)other;
}

FontRenderer::~FontRenderer()
{
	destroy();
}

FontRenderer &FontRenderer::operator=(const FontRenderer &other)
{
	(void)other;
	return (*this);
}

bool FontRenderer::load(float pixel_height)
{
	destroy();
	pixel_height_ = pixel_height;
	loaded_ = true;
	use_real_font_ = real_font_.initialize();
	return (true);
}

void FontRenderer::destroy()
{
	real_font_.destroy();
	use_real_font_ = false;
	loaded_ = false;
}

bool FontRenderer::is_loaded() const
{
	return (loaded_);
}

bool FontRenderer::is_using_real_font() const
{
	return (use_real_font_);
}

float FontRenderer::effective_pixel_height(float pixel_height_override) const
{
	if (pixel_height_override > 0.0f)
		return (pixel_height_override);
	return (pixel_height_);
}

int FontRenderer::line_height(float pixel_height_override) const
{
	if (!loaded_)
		return (0);
	return (static_cast<int>(std::ceil(static_cast<double>(
		effective_pixel_height(pixel_height_override)))));
}

int FontRenderer::text_width(const char *text,
	float pixel_height_override) const
{
	float height;

	if (!loaded_ || text == ft_nullptr)
		return (0);
	height = effective_pixel_height(pixel_height_override);
	if (use_real_font_)
		return (real_font_.text_width(text, height));
	return (BitmapFont::text_width(text, height));
}

void FontRenderer::draw_text(uint32_t *pixels, int buf_w, int buf_h, int x,
	int y, const char *text, uint32_t colour, float pixel_height_override) const
{
	float height;

	if (!loaded_ || pixels == ft_nullptr || text == ft_nullptr)
		return ;
	height = effective_pixel_height(pixel_height_override);
	if (use_real_font_)
		real_font_.draw_text(pixels, buf_w, buf_h, x, y, text, colour, height);
	else
		BitmapFont::draw_text(pixels, buf_w, buf_h, x, y, text, colour, height);
}

void FontRenderer::draw_text_centered(uint32_t *pixels, int buf_w, int buf_h,
	int cx, int y, const char *text, uint32_t colour,
	float pixel_height_override) const
{
	float height;
	int width;

	if (!loaded_ || text == ft_nullptr)
		return ;
	height = effective_pixel_height(pixel_height_override);
	width = text_width(text, height);
	draw_text(pixels, buf_w, buf_h, cx - width / 2, y, text, colour, height);
}

FontRenderer &FontRenderer::instance()
{
	static FontRenderer	fr;

	return (fr);
}
