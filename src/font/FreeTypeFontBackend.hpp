#ifndef FREE_TYPE_FONT_BACKEND_HPP
# define FREE_TYPE_FONT_BACKEND_HPP

# include "../ft_vox.hpp"

class FreeTypeFontBackend
{
  public:
	FreeTypeFontBackend();
	FreeTypeFontBackend(const FreeTypeFontBackend &other);
	~FreeTypeFontBackend();
	FreeTypeFontBackend &operator=(const FreeTypeFontBackend &other);

	bool initialize();
	void destroy();

	int text_width(const char *text, float pixel_height) const;
	void draw_text(uint32_t *pixels, int buf_w, int buf_h, int x, int y,
		const char *text, uint32_t colour, float pixel_height) const;

  private:
	struct Impl;

	Impl *impl_;

	static const char *find_comic_sans();
	static const char *COMIC_CANDIDATES[];
	static void blend_glyph(uint32_t *pixels, int buf_w, int buf_h, int draw_x,
		int draw_y, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
	static void draw_glyph_bitmap(uint32_t *pixels, int buf_w, int buf_h,
		void *glyph_slot, int cursor_x, int baseline_y, uint8_t red,
		uint8_t green, uint8_t blue);
};

#endif
