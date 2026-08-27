#include "../../src/menu/InGameSettingsScene.hpp"

const int InGameSettingsScene::Layout::W = 400;
const int InGameSettingsScene::Layout::H = 320;
const int InGameSettingsScene::Layout::X = (MenuCanvas::W - W) / 2;
const int InGameSettingsScene::Layout::Y = (MenuCanvas::H - H) / 2;

InGameSettingsScene::InGameSettingsScene() : MenuScene()
{
}
InGameSettingsScene::InGameSettingsScene(const InGameSettingsScene &other) : MenuScene()
{
	(void)other;
}
InGameSettingsScene::~InGameSettingsScene()
{
}
InGameSettingsScene &InGameSettingsScene::operator=(const InGameSettingsScene &other)
{
	(void)other;
	return (*this);
}

void InGameSettingsScene::update(const MenuCanvas::MenuInput &in,
	MenuController &ctl)
{
	const int	togx = Layout::X + Layout::W - 80, togy = Layout::Y + 50;
	const int	slx = Layout::X + 20, sly = Layout::Y + 110
			+ MenuCanvas::LINE_GAP, slw = Layout::W - 40;
	const int	qbx = Layout::X + 20, qby = Layout::Y + 185
			+ MenuCanvas::LINE_GAP * 2;
	const int	abx = Layout::X + 140, aby = Layout::Y + 185
			+ MenuCanvas::LINE_GAP * 2;
	const int	backbx = Layout::X + 20, backby = Layout::Y + Layout::H - 50;
	const int	menubx = Layout::X + Layout::W / 2, menuby = Layout::Y
			+ Layout::H - 50;
	float		f;

	Settings &s = Settings::instance();
	if (ctl.canvas().was_clicked(togx, togy, 60, 20, in))
		s.set_fps_overlay(!s.fps_overlay());
	if (ctl.canvas().is_hovered(slx, sly, slw, 16, in) && in.mouse_clicked)
	{
		f = static_cast<float>(in.mouse_x - slx) / static_cast<float>(slw);
		if (f < 0.0f)
			f = 0.0f;
		if (f > 1.0f)
			f = 1.0f;
		s.set_render_distance(14 + static_cast<int>(f * static_cast<float>(160
					- 14)));
	}
	if (ctl.canvas().was_clicked(qbx, qby, 110, 28, in))
		s.set_keyboard_layout(FT_DUMB_KEYBOARD_LAYOUT_QWERTY);
	if (ctl.canvas().was_clicked(abx, aby, 110, 28, in))
		s.set_keyboard_layout(FT_DUMB_KEYBOARD_LAYOUT_AZERTY);
	if (in.key_escape || ctl.canvas().was_clicked(backbx, backby, Layout::W / 2
			- 30, 30, in))
		ctl.request_dismiss_in_game();
	if (ctl.canvas().was_clicked(menubx, menuby, Layout::W / 2 - 10, 30, in))
		ctl.request_main_menu_from_game();
}

void InGameSettingsScene::render_fps_toggle(MenuCanvas &c,
	const Settings &s) const
{
	const int tx = Layout::X + Layout::W - 80, ty = Layout::Y + 50;
	bool fps = s.fps_overlay();
	uint32_t bg = fps ? MenuCanvas::Colors::GREEN : MenuCanvas::Colors::BTN_N;
	uint32_t bd = fps ? MenuCanvas::Colors::GREEN : MenuCanvas::Colors::TEXT_DIM;
	c.draw_text(Layout::X + 20, Layout::Y + 52, "FPS OVERLAY", 2,
		MenuCanvas::Colors::TEXT);
	c.fill_rect(tx, ty, 60, 20, bg);
	c.draw_border(tx, ty, 60, 20, bd);
	c.draw_text(tx + 12, ty + 4, fps ? "ON" : "OFF", 2,
		MenuCanvas::Colors::TEXT);
}

void InGameSettingsScene::render_render_distance(MenuCanvas &c,
	const Settings &s) const
{
	const int slx = Layout::X + 20, sly = Layout::Y + 110
		+ MenuCanvas::LINE_GAP, slw = Layout::W - 40;
	int rd = s.render_distance();
	float f = static_cast<float>(rd - 14) / static_cast<float>(160 - 14);
	char rb[32];
	std::snprintf(rb, sizeof(rb), "%d", rd);
	c.draw_text(Layout::X + 20, Layout::Y + 92 + MenuCanvas::LINE_GAP,
		"RENDER DISTANCE", 2, MenuCanvas::Colors::TEXT);
	c.fill_rect(slx, sly, slw, 14, MenuCanvas::Colors::BTN_N);
	c.draw_border(slx, sly, slw, 14, MenuCanvas::Colors::TEXT_DIM);
	c.fill_rect(slx, sly, static_cast<int>(f * static_cast<float>(slw)), 14,
		MenuCanvas::Colors::SLIDER);
	c.draw_text(slx + slw + 8, sly + 2, rb, 2, MenuCanvas::Colors::TEXT);
}

void InGameSettingsScene::render_keyboard(MenuCanvas &c,
	const Settings &s) const
{
	const int qbx = Layout::X + 20, qby = Layout::Y + 185 + MenuCanvas::LINE_GAP
		* 2;
	const int abx = Layout::X + 140, aby = Layout::Y + 185
		+ MenuCanvas::LINE_GAP * 2;
	bool qsel = (s.keyboard_layout() == FT_DUMB_KEYBOARD_LAYOUT_QWERTY);
	c.draw_text(Layout::X + 20, Layout::Y + 164 + MenuCanvas::LINE_GAP * 2,
		"KEYBOARD", 2, MenuCanvas::Colors::TEXT);
	c.fill_rect(qbx, qby, 110, 28,
		qsel ? MenuCanvas::Colors::BTN_A : MenuCanvas::Colors::BTN_N);
	c.draw_border(qbx, qby, 110, 28,
		qsel ? MenuCanvas::Colors::ACCENT : MenuCanvas::Colors::TEXT_DIM);
	c.draw_text(qbx + 14, qby + 7, "QWERTY", 2, MenuCanvas::Colors::TEXT);
	c.fill_rect(abx, aby, 110, 28,
		!qsel ? MenuCanvas::Colors::BTN_A : MenuCanvas::Colors::BTN_N);
	c.draw_border(abx, aby, 110, 28,
		!qsel ? MenuCanvas::Colors::ACCENT : MenuCanvas::Colors::TEXT_DIM);
	c.draw_text(abx + 14, aby + 7, "AZERTY", 2, MenuCanvas::Colors::TEXT);
}

void InGameSettingsScene::render_buttons(MenuCanvas &c) const
{
	const int backbx = Layout::X + 20, backby = Layout::Y + Layout::H - 50;
	const int menubx = Layout::X + Layout::W / 2, menuby = Layout::Y + Layout::H
		- 50;
	c.fill_rect(backbx, backby, Layout::W / 2 - 30, 30,
		MenuCanvas::Colors::BTN_N);
	c.draw_border(backbx, backby, Layout::W / 2 - 30, 30,
		MenuCanvas::Colors::TEXT_DIM);
	c.draw_text(backbx + 8, backby + 7, "RESUME", 2, MenuCanvas::Colors::TEXT);
	c.fill_rect(menubx, menuby, Layout::W / 2 - 10, 30,
		MenuCanvas::Colors::BTN_N);
	c.draw_border(menubx, menuby, Layout::W / 2 - 10, 30,
		MenuCanvas::Colors::TEXT_DIM);
	c.draw_text(menubx + 8, menuby + 7, "MAIN MENU", 2,
		MenuCanvas::Colors::TEXT);
}

void InGameSettingsScene::render(MenuCanvas &c) const
{
	Settings &s = Settings::instance();
	c.fill_rect(Layout::X - 3, Layout::Y - 3, Layout::W + 6, Layout::H + 6,
		MenuCanvas::Colors::ACCENT);
	c.fill_rect(Layout::X, Layout::Y, Layout::W, Layout::H,
		MenuCanvas::Colors::PANEL);
	c.draw_text_centered(Layout::X + Layout::W / 2, Layout::Y + 8, "SETTINGS",
		3, MenuCanvas::Colors::TITLE);
	c.fill_rect(Layout::X + 10, Layout::Y + 40, Layout::W - 20, 1,
		MenuCanvas::Colors::TEXT_DIM);
	render_fps_toggle(c, s);
	render_render_distance(c, s);
	render_keyboard(c, s);
	render_buttons(c);
}
