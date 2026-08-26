#include "../../src/font/FreeTypeFontBackend.hpp"
#if defined(FT_VOX_HAVE_FREETYPE)
# include <ft2build.h>
# include FT_FREETYPE_H
#endif

FreeTypeFontBackend::FreeTypeFontBackend() : impl_(ft_nullptr)
{
}

FreeTypeFontBackend::FreeTypeFontBackend(const FreeTypeFontBackend &other)
	: impl_(ft_nullptr)
{
	(void)other;
}

FreeTypeFontBackend::~FreeTypeFontBackend()
{
	destroy();
}

FreeTypeFontBackend &FreeTypeFontBackend::operator=(
	const FreeTypeFontBackend &other)
{
	(void)other;
	return (*this);
}

const char *FreeTypeFontBackend::COMIC_CANDIDATES[] = {
	"C:\\Windows\\Fonts\\comic.ttf",
	"C:\\Windows\\Fonts\\Comic Sans MS.ttf",
	"/System/Library/Fonts/Supplemental/Comic Sans MS.ttf",
	"/Library/Fonts/Comic Sans MS.ttf",
	"/usr/share/fonts/truetype/comic-neue/ComicNeue-Regular.ttf",
	"/usr/share/fonts/truetype/comicneue/ComicNeue-Regular.ttf",
	"/usr/share/fonts/comic-neue/ComicNeue-Regular.ttf",
	ft_nullptr,
};

const char *FreeTypeFontBackend::find_comic_sans()
{
	size_t	index;
	FILE	*file;

	index = 0U;
	while (COMIC_CANDIDATES[index] != ft_nullptr)
	{
		file = std::fopen(COMIC_CANDIDATES[index], "rb");
		if (file != ft_nullptr)
		{
			std::fclose(file);
			return (COMIC_CANDIDATES[index]);
		}
		++index;
	}
	return (ft_nullptr);
}

#if defined(FT_VOX_HAVE_FREETYPE)

void FreeTypeFontBackend::blend_glyph(uint32_t *pixels, int buf_w, int buf_h,
	int draw_x, int draw_y, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha)
{
	uint32_t	existing;
	uint32_t	bg_r;
	uint32_t	bg_g;
	uint32_t	bg_b;
	uint32_t	a;
	uint32_t	out_r;
	uint32_t	out_g;
	uint32_t	out_b;

	if (draw_x < 0 || draw_x >= buf_w || draw_y < 0 || draw_y >= buf_h)
		return ;
	existing = pixels[draw_y * buf_w + draw_x];
	bg_r = (existing >> 16) & 0xFFU;
	bg_g = (existing >> 8) & 0xFFU;
	bg_b = existing & 0xFFU;
	a = static_cast<uint32_t>(alpha);
	out_r = (static_cast<uint32_t>(r) * a + bg_r * (255U - a)) / 255U;
	out_g = (static_cast<uint32_t>(g) * a + bg_g * (255U - a)) / 255U;
	out_b = (static_cast<uint32_t>(b) * a + bg_b * (255U - a)) / 255U;
	pixels[draw_y * buf_w + draw_x] = (out_r << 16) | (out_g << 8) | out_b;
}

struct FreeTypeFontBackend::Impl
{
	FT_Library	library;
	FT_Face		face;
};

bool FreeTypeFontBackend::initialize()
{
	const char	*font_path;
	Impl		*impl;

	font_path = find_comic_sans();
	if (font_path == ft_nullptr)
		return (false);
	impl = new (std::nothrow) Impl();
	if (impl == ft_nullptr)
		return (false);
	if (FT_Init_FreeType(&impl->library) != 0)
	{
		delete impl;
		return (false);
	}
	if (FT_New_Face(impl->library, font_path, 0, &impl->face) != 0)
	{
		(void)FT_Done_FreeType(impl->library);
		delete impl;
		return (false);
	}
	impl_ = impl;
	return (true);
}

void FreeTypeFontBackend::destroy()
{
	if (impl_ != ft_nullptr)
	{
		(void)FT_Done_Face(impl_->face);
		(void)FT_Done_FreeType(impl_->library);
		delete impl_;
		impl_ = ft_nullptr;
	}
}

int FreeTypeFontBackend::text_width(const char *text,
	float pixel_height) const
{
	int			width;
	size_t		index;
	FT_ULong	codepoint;

	(void)FT_Set_Pixel_Sizes(impl_->face, 0,
		static_cast<FT_UInt>(std::lround(static_cast<double>(pixel_height))));
	width = 0;
	index = 0U;
	while (text[index] != '\0')
	{
		codepoint = static_cast<FT_ULong>(
				static_cast<unsigned char>(text[index]));
		if (FT_Load_Char(impl_->face, codepoint, FT_LOAD_DEFAULT) == 0)
			width += static_cast<int>(impl_->face->glyph->advance.x >> 6);
		++index;
	}
	return (width);
}

void FreeTypeFontBackend::draw_glyph_bitmap(uint32_t *pixels, int buf_w,
	int buf_h, void *glyph_slot, int cursor_x, int baseline_y, uint8_t red,
	uint8_t green, uint8_t blue)
{
	FT_GlyphSlot	slot;
	int				row;
	int				col;
	uint8_t			alpha;

	slot = static_cast<FT_GlyphSlot>(glyph_slot);
	row = 0;
	while (row < static_cast<int>(slot->bitmap.rows))
	{
		col = 0;
		while (col < static_cast<int>(slot->bitmap.width))
		{
			alpha = slot->bitmap.buffer[static_cast<size_t>(row)
					* static_cast<size_t>(slot->bitmap.pitch)
				+ static_cast<size_t>(col)];
			if (alpha != 0U)
				blend_glyph(pixels, buf_w, buf_h,
					cursor_x + slot->bitmap_left + col,
					baseline_y - slot->bitmap_top + row, red, green, blue,
					alpha);
			++col;
		}
		++row;
	}
}

void FreeTypeFontBackend::draw_text(uint32_t *pixels, int buf_w, int buf_h,
	int x, int y, const char *text, uint32_t colour,
	float pixel_height) const
{
	uint8_t		red;
	uint8_t		green;
	uint8_t		blue;
	int			cursor_x;
	int			baseline_y;
	size_t		index;
	FT_ULong	codepoint;

	(void)FT_Set_Pixel_Sizes(impl_->face, 0,
		static_cast<FT_UInt>(std::lround(static_cast<double>(pixel_height))));
	red = static_cast<uint8_t>((colour >> 16) & 0xFFU);
	green = static_cast<uint8_t>((colour >> 8) & 0xFFU);
	blue = static_cast<uint8_t>(colour & 0xFFU);
	cursor_x = x;
	baseline_y = y + static_cast<int>(impl_->face->size->metrics.ascender
			>> 6);
	index = 0U;
	while (text[index] != '\0')
	{
		codepoint = static_cast<FT_ULong>(
				static_cast<unsigned char>(text[index]));
		if (FT_Load_Char(impl_->face, codepoint, FT_LOAD_RENDER) == 0)
		{
			draw_glyph_bitmap(pixels, buf_w, buf_h, impl_->face->glyph,
				cursor_x, baseline_y, red, green, blue);
			cursor_x += static_cast<int>(impl_->face->glyph->advance.x >> 6);
		}
		++index;
	}
}

#else

bool FreeTypeFontBackend::initialize()
{
	return (false);
}

void FreeTypeFontBackend::destroy()
{
	impl_ = ft_nullptr;
}

int FreeTypeFontBackend::text_width(const char *text,
	float pixel_height) const
{
	(void)text;
	(void)pixel_height;
	return (0);
}

void FreeTypeFontBackend::draw_text(uint32_t *pixels, int buf_w, int buf_h,
	int x, int y, const char *text, uint32_t colour,
	float pixel_height) const
{
	(void)pixels;
	(void)buf_w;
	(void)buf_h;
	(void)x;
	(void)y;
	(void)text;
	(void)colour;
	(void)pixel_height;
}

#endif
