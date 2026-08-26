#include "../../src/font/BitmapFont.hpp"

BitmapFont::BitmapFont()
{
}

BitmapFont::BitmapFont(const BitmapFont &other)
{
	(void)other;
}

BitmapFont::~BitmapFont()
{
}

BitmapFont &BitmapFont::operator=(const BitmapFont &other)
{
	(void)other;
	return (*this);
}

// clang-format off
const BitmapFont::Glyph BitmapFont::GLYPHS[] = {
    {'A', {" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
    {'B', {"#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "}},
    {'C', {" ####", "#    ", "#    ", "#    ", "#    ", "#    ", " ####"}},
    {'D', {"#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "}},
    {'E', {"#####", "#    ", "#    ", "#####", "#    ", "#    ", "#####"}},
    {'F', {"#####", "#    ", "#    ", "#####", "#    ", "#    ", "#    "}},
    {'G', {" ####", "#    ", "#    ", "# ###", "#   #", "#   #", " ####"}},
    {'H', {"#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
    {'I', {"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "#####"}},
    {'J', {"#####", "   # ", "   # ", "   # ", "#  # ", "#  # ", " ##  "}},
    {'K', {"#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"}},
    {'L', {"#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"}},
    {'M', {"#   #", "## ##", "# # #", "#   #", "#   #", "#   #", "#   #"}},
    {'N', {"#   #", "##  #", "# # #", "#  ##", "#   #", "#   #", "#   #"}},
    {'O', {" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
    {'P', {"#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "}},
    {'Q', {" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"}},
    {'R', {"#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"}},
    {'S', {" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "}},
    {'T', {"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}},
    {'U', {"#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
    {'V', {"#   #", "#   #", "#   #", "#   #", " # # ", " # # ", "  #  "}},
    {'W', {"#   #", "#   #", "#   #", "# # #", "# # #", "## ##", "#   #"}},
    {'X', {"#   #", " # # ", "  #  ", "  #  ", "  #  ", " # # ", "#   #"}},
    {'Y', {"#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}},
    {'Z', {"#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"}},
    {'0', {" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "}},
    {'1', {"  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", "#####"}},
    {'2', {" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"}},
    {'3', {"#####", "    #", "   # ", "  ## ", "    #", "#   #", " ### "}},
    {'4', {"#   #", "#   #", "#   #", "#####", "    #", "    #", "    #"}},
    {'5', {"#####", "#    ", "#    ", "#### ", "    #", "    #", "#### "}},
    {'6', {" ### ", "#    ", "#    ", "#### ", "#   #", "#   #", " ### "}},
    {'7', {"#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "}},
    {'8', {" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "}},
    {'9', {" ### ", "#   #", "#   #", " ####", "    #", "    #", " ### "}},
    {' ', {"     ", "     ", "     ", "     ", "     ", "     ", "     "}},
    {'_', {"     ", "     ", "     ", "     ", "     ", "     ", "#####"}},
    {'-', {"     ", "     ", "     ", "#####", "     ", "     ", "     "}},
    {'.', {"     ", "     ", "     ", "     ", "     ", "  ## ", "  ## "}},
    {':', {"     ", "  ## ", "  ## ", "     ", "  ## ", "  ## ", "     "}},
    {'/', {"    #", "   # ", "   # ", "  #  ", " #   ", " #   ", "#    "}},
    {'%', {"##  #", "## # ", "  #  ", " # ##", "#  ##", "     ", "     "}}
};
// clang-format on

const BitmapFont::Glyph *BitmapFont::find_glyph(char character)
{
	size_t	index;

	if (character >= 'a' && character <= 'z')
		character = static_cast<char>(character - 'a' + 'A');
	index = 0U;
	while (index < sizeof(GLYPHS) / sizeof(GLYPHS[0]))
	{
		if (GLYPHS[index].character == character)
			return (&GLYPHS[index]);
		++index;
	}
	return (ft_nullptr);
}

void BitmapFont::draw_glyph_cell(uint32_t *pixels, int buf_w, int buf_h,
	int origin_x, int origin_y, int scale, uint32_t colour)
{
	int	sy;
	int	sx;
	int	px;
	int	py;

	sy = 0;
	while (sy < scale)
	{
		sx = 0;
		while (sx < scale)
		{
			px = origin_x + sx;
			py = origin_y + sy;
			if (px >= 0 && px < buf_w && py >= 0 && py < buf_h)
				pixels[py * buf_w + px] = colour;
			++sx;
		}
		++sy;
	}
}

void BitmapFont::draw_glyph(uint32_t *pixels, int buf_w, int buf_h,
	int origin_x, int origin_y, const Glyph *glyph, int scale,
	uint32_t colour)
{
	int			row_index;
	int			col_index;
	uint32_t	rgb;

	if (glyph == ft_nullptr || pixels == ft_nullptr || scale <= 0)
		return ;
	rgb = colour & 0x00FFFFFFU;
	row_index = 0;
	while (row_index < 7)
	{
		col_index = 0;
		while (col_index < 5)
		{
			if (glyph->rows[row_index][col_index] != ' ')
				draw_glyph_cell(pixels, buf_w, buf_h,
					origin_x + col_index * scale, origin_y + row_index * scale,
					scale, rgb);
			++col_index;
		}
		++row_index;
	}
}

int BitmapFont::text_width(const char *text, float pixel_height)
{
	int	glyph_scale;

	glyph_scale = std::max(1, static_cast<int>(std::lround(
				static_cast<double>(pixel_height) / 7.0)));
	return (static_cast<int>(std::strlen(text)) * glyph_scale * 6);
}

void BitmapFont::draw_text(uint32_t *pixels, int buf_w, int buf_h, int x,
	int y, const char *text, uint32_t colour, float pixel_height)
{
	int			glyph_scale;
	int			cursor_x;
	int			index;
	const Glyph	*glyph;

	glyph_scale = std::max(1, static_cast<int>(std::lround(
				static_cast<double>(pixel_height) / 7.0)));
	cursor_x = x;
	index = 0;
	while (text[index] != '\0')
	{
		glyph = find_glyph(text[index]);
		if (glyph != ft_nullptr)
			draw_glyph(pixels, buf_w, buf_h, cursor_x, y, glyph, glyph_scale,
				colour);
		cursor_x += glyph_scale * 6;
		++index;
	}
}
