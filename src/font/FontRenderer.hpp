#ifndef FONT_RENDERER_HPP
# define FONT_RENDERER_HPP

# include "../../src/font/BitmapFont.hpp"
# include "../../src/font/FreeTypeFontBackend.hpp"
# include "../ft_vox.hpp"

class FontRenderer
{
  public:
	FontRenderer();
	FontRenderer(const FontRenderer &other);
	~FontRenderer();
	FontRenderer &operator=(const FontRenderer &other);

	bool load(float pixel_height);
	void destroy();

	bool is_loaded() const;
	bool is_using_real_font() const;
	int line_height(float pixel_height_override = 0.0f) const;
	int text_width(const char *text, float pixel_height_override = 0.0f) const;

	void draw_text(uint32_t *pixels, int buf_w, int buf_h, int x, int y,
		const char *text, uint32_t colour,
		float pixel_height_override = 0.0f) const;
	void draw_text_centered(uint32_t *pixels, int buf_w, int buf_h, int cx,
		int y, const char *text, uint32_t colour,
		float pixel_height_override = 0.0f) const;

	static FontRenderer &instance();

  private:
	FreeTypeFontBackend real_font_;
	float pixel_height_;
	bool loaded_;
	bool use_real_font_;

	float effective_pixel_height(float pixel_height_override) const;
};

#endif
