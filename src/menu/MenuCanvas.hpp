#ifndef MENU_CANVAS_HPP
# define MENU_CANVAS_HPP

# include "../../src/font/FontRenderer.hpp"
# include "../ft_vox.hpp"

class MenuCanvas
{
  public:
	struct						MenuInput
	{
		int						mouse_x;
		int						mouse_y;
		bool					mouse_clicked;
		bool					key_up;
		bool					key_down;
		bool					key_enter;
		bool					key_escape;
	};

	struct						Colors
	{
		static const uint32_t	BG;
		static const uint32_t	PANEL;
		static const uint32_t	BTN_N;
		static const uint32_t	BTN_H;
		static const uint32_t	BTN_A;
		static const uint32_t	TEXT;
		static const uint32_t	TEXT_DIM;
		static const uint32_t	TITLE;
		static const uint32_t	ACCENT;
		static const uint32_t	GREEN;
		static const uint32_t	SLIDER;
		static const uint32_t	WHITE;
	};

	static const int			W;
	static const int			H;
	static const int			LINE_GAP;

	MenuCanvas();
	MenuCanvas(const MenuCanvas &other);
	~MenuCanvas();
	MenuCanvas &operator=(const MenuCanvas &other);

	void clear(uint32_t color = Colors::BG);
	void fill_rect(int x, int y, int w, int h, uint32_t color);
	void draw_border(int x, int y, int w, int h, uint32_t color,
		int thickness = 1);
	void draw_text(int x, int y, const char *text, int scale, uint32_t color);
	void draw_text_centered(int cx, int y, const char *text, int scale,
		uint32_t color);

	bool button(int x, int y, int w, int h, const char *label, int scale,
		const MenuInput &in, bool selected = false);
	bool checkbox(int x, int y, bool checked, const char *label, int scale,
		const MenuInput &in);
	float slider(int x, int y, int w, float value, float vmin, float vmax,
		const char *label, int scale, const MenuInput &in);

	bool is_hovered(int x, int y, int w, int h, const MenuInput &in) const;
	bool was_clicked(int x, int y, int w, int h, const MenuInput &in) const;

	const uint32_t *pixels() const;
	int width() const;
	int height() const;

	ft_render_framebuffer &framebuffer();

  private:
	std::vector<uint32_t> pixels_;
	ft_render_framebuffer		fb_;

	void set_pixel(int x, int y, uint32_t color);
};

#endif
