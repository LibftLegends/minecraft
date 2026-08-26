#ifndef BITMAP_FONT_HPP
# define BITMAP_FONT_HPP

# include "../ft_vox.hpp"

class BitmapFont
{
  public:
	BitmapFont();
	BitmapFont(const BitmapFont &other);
	~BitmapFont();
	BitmapFont &operator=(const BitmapFont &other);

	static int text_width(const char *text, float pixel_height);
	static void draw_text(uint32_t *pixels, int buf_w, int buf_h, int x, int y,
		const char *text, uint32_t colour, float pixel_height);

  private:
	struct				Glyph
	{
		char			character;
		const char		*rows[7];
	};

	static const Glyph	GLYPHS[];

	static const Glyph *find_glyph(char character);
	static void draw_glyph_cell(uint32_t *pixels, int buf_w, int buf_h,
		int origin_x, int origin_y, int scale, uint32_t colour);
	static void draw_glyph(uint32_t *pixels, int buf_w, int buf_h, int origin_x,
		int origin_y, const Glyph *glyph, int scale, uint32_t colour);
};

#endif
