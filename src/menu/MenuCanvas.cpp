#include "../../src/menu/MenuCanvas.hpp"

const int MenuCanvas::W = 960;
const int MenuCanvas::H = 540;
const int MenuCanvas::LINE_GAP = 8; // ~2mm extra spacing between stacked lines

const uint32_t MenuCanvas::Colors::BG = 0x0D0D1AU;
const uint32_t MenuCanvas::Colors::PANEL = 0x151525U;
const uint32_t MenuCanvas::Colors::BTN_N = 0x1C2E50U;
const uint32_t MenuCanvas::Colors::BTN_H = 0x2A507AU;
const uint32_t MenuCanvas::Colors::BTN_A = 0x3A78B0U;
const uint32_t MenuCanvas::Colors::TEXT = 0xEEEEFFU;
const uint32_t MenuCanvas::Colors::TEXT_DIM = 0x8888AAU;
const uint32_t MenuCanvas::Colors::TITLE = 0xFFCC00U;
const uint32_t MenuCanvas::Colors::ACCENT = 0x00B4D8U;
const uint32_t MenuCanvas::Colors::GREEN = 0x4CAF50U;
const uint32_t MenuCanvas::Colors::SLIDER = 0x5DA3FAU;
const uint32_t MenuCanvas::Colors::WHITE = 0xFFFFFFU;

MenuCanvas::MenuCanvas(const MenuCanvas &other)
{
	*this = other;
}

MenuCanvas::~MenuCanvas()
{
}

MenuCanvas &MenuCanvas::operator=(const MenuCanvas &other)
{
	if (this != &other)
	{
		this->pixels_ = other.pixels_;
		this->fb_ = other.fb_;
		this->fb_.pixels = this->pixels_.data();
	}
	return (*this);
}

MenuCanvas::MenuCanvas() : pixels_(static_cast<size_t>(W * H),
	MenuCanvas::Colors::BG)
{
	fb_.width = W;
	fb_.height = H;
	fb_.pixels = pixels_.data();
}

void MenuCanvas::set_pixel(int x, int y, uint32_t color)
{
	if (x >= 0 && x < W && y >= 0 && y < H)
		pixels_[static_cast<size_t>(y * W + x)] = color;
}

void MenuCanvas::clear(uint32_t color)
{
	std::fill(pixels_.begin(), pixels_.end(), color);
}

void MenuCanvas::fill_rect(int x, int y, int w, int h, uint32_t color)
{
	int			x1;
	int			y1;
	int			x2;
	int			y2;
	uint32_t	*line;

	x1 = std::max(x, 0);
	y1 = std::max(y, 0);
	x2 = std::min(x + w, W);
	y2 = std::min(y + h, H);
	for (int row = y1; row < y2; ++row)
	{
		line = pixels_.data() + row * W;
		for (int col = x1; col < x2; ++col)
			line[col] = color;
	}
}

void MenuCanvas::draw_border(int x, int y, int w, int h, uint32_t color,
	int thickness)
{
	fill_rect(x, y, w, thickness, color);
	fill_rect(x, y + h - thickness, w, thickness, color);
	fill_rect(x, y, thickness, h, color);
	fill_rect(x + w - thickness, y, thickness, h, color);
}

void MenuCanvas::draw_text(int x, int y, const char *text, int /*scale*/,
	uint32_t color)
{
	if (!text)
		return ;
	FontRenderer &fr = FontRenderer::instance();
	if (fr.is_loaded())
		fr.draw_text(pixels_.data(), W, H, x, y, text, color);
}

void MenuCanvas::draw_text_centered(int cx, int y, const char *text, int
	/*scale*/, uint32_t color)
{
	if (!text)
		return ;
	FontRenderer &fr = FontRenderer::instance();
	if (fr.is_loaded())
		fr.draw_text_centered(pixels_.data(), W, H, cx, y, text, color);
}

bool MenuCanvas::is_hovered(int x, int y, int w, int h,
	const MenuInput &in) const
{
	return (in.mouse_x >= x && in.mouse_x < x + w && in.mouse_y >= y
		&& in.mouse_y < y + h);
}

bool MenuCanvas::was_clicked(int x, int y, int w, int h,
	const MenuInput &in) const
{
	return (in.mouse_clicked && is_hovered(x, y, w, h, in));
}

bool MenuCanvas::button(int x, int y, int w, int h, const char *label,
	int scale, const MenuInput &in, bool selected)
{
	bool		hov;
	uint32_t	bg;
	int			tw;
	int			tx;
	int			ty;

	hov = is_hovered(x, y, w, h, in);
	bg = selected ? MenuCanvas::Colors::BTN_A : (hov ? MenuCanvas::Colors::BTN_H
			: MenuCanvas::Colors::BTN_N);
	fill_rect(x, y, w, h, bg);
	draw_border(x, y, w, h,
		hov ? MenuCanvas::Colors::ACCENT : MenuCanvas::Colors::TEXT_DIM);
	tw = 0;
	for (const char *p = label; *p; ++p)
		tw += 4 * scale;
	tx = x + (w - tw) / 2;
	ty = y + (h - 5 * scale) / 2;
	draw_text(tx, ty, label, scale, MenuCanvas::Colors::TEXT);
	return (was_clicked(x, y, w, h, in));
}

bool MenuCanvas::checkbox(int x, int y, bool checked, const char *label,
	int scale, const MenuInput &in)
{
	int	box;
	int	click_w;

	box = scale * 6;
	fill_rect(x, y, box, box,
		checked ? MenuCanvas::Colors::GREEN : MenuCanvas::Colors::PANEL);
	draw_border(x, y, box, box,
		checked ? MenuCanvas::Colors::GREEN : MenuCanvas::Colors::TEXT_DIM);
	if (checked)
		draw_text(x + scale, y + scale, "X", scale, MenuCanvas::Colors::WHITE);
	draw_text(x + box + scale * 2, y + scale, label, scale,
		MenuCanvas::Colors::TEXT);
	click_w = box + scale * 2 + static_cast<int>(std::strlen(label)) * 4
		* scale;
	if (was_clicked(x, y, click_w, box, in))
		return (!checked);
	return (checked);
}

float MenuCanvas::slider(int x, int y, int w, float value, float vmin,
	float vmax, const char *label, int scale, const MenuInput &in)
{
	int		th;
	int		ty;
	float	frac;
	int		fw;
	int		kx;
	char	buf[64];
	float	nf;

	th = scale * 3;
	ty = y + scale * 2;
	fill_rect(x, ty, w, th, MenuCanvas::Colors::PANEL);
	draw_border(x, ty, w, th, MenuCanvas::Colors::TEXT_DIM);
	frac = (value - vmin) / (vmax - vmin);
	if (frac < 0.0f)
		frac = 0.0f;
	if (frac > 1.0f)
		frac = 1.0f;
	fw = static_cast<int>(frac * static_cast<float>(w));
	fill_rect(x, ty, fw, th, MenuCanvas::Colors::SLIDER);
	kx = x + fw - scale;
	fill_rect(kx, ty - scale, scale * 2, th + scale * 2,
		MenuCanvas::Colors::ACCENT);
	std::snprintf(buf, sizeof(buf), "%s  %d", label, static_cast<int>(value));
	draw_text(x, ty + th + scale * 2, buf, scale, MenuCanvas::Colors::TEXT);
	if (is_hovered(x, ty, w, th + scale * 4, in) && in.mouse_clicked)
	{
		nf = static_cast<float>(in.mouse_x - x) / static_cast<float>(w);
		nf = std::max(0.0f, std::min(1.0f, nf));
		return (vmin + nf * (vmax - vmin));
	}
	return (value);
}

const uint32_t *MenuCanvas::pixels() const
{
	return (pixels_.data());
}
int MenuCanvas::width() const
{
	return (W);
}
int MenuCanvas::height() const
{
	return (H);
}
ft_render_framebuffer &MenuCanvas::framebuffer()
{
	return (fb_);
}
