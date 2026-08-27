#include "../../src/menu/SettingsScene.hpp"

const int SettingsScene::Layout::TOG_X = MenuCanvas::W - 120;
const int SettingsScene::Layout::TOG_Y = 110;
const int SettingsScene::Layout::SLIDER_X = 60;
const int SettingsScene::Layout::SLIDER_Y = 150 + 24 + MenuCanvas::LINE_GAP;
const int SettingsScene::Layout::SLIDER_W = MenuCanvas::W - 120;
const int SettingsScene::Layout::Q_X = 60;
const int SettingsScene::Layout::Q_Y = 274 + 28 + MenuCanvas::LINE_GAP * 2;
const int SettingsScene::Layout::A_X = 180;
const int SettingsScene::Layout::A_Y = 274 + 28 + MenuCanvas::LINE_GAP * 2;
const int SettingsScene::Layout::BACK_X = 40;
const int SettingsScene::Layout::BACK_Y = MenuCanvas::H - 60;

SettingsScene::SettingsScene() : MenuScene()
{
}
SettingsScene::SettingsScene(const SettingsScene &other) : MenuScene()
{
	(void)other;
}
SettingsScene::~SettingsScene()
{
}
SettingsScene &SettingsScene::operator=(const SettingsScene &other)
{
	(void)other;
	return (*this);
}

void SettingsScene::update(const MenuCanvas::MenuInput &in, MenuController &ctl)
{
	float		frac;
	Settings	&s = Settings::instance();

	if (in.key_escape)
	{
		ctl.request_back();
		return ;
	}
	if (ctl.canvas().was_clicked(Layout::TOG_X, Layout::TOG_Y, 60, 20, in))
		s.set_fps_overlay(!s.fps_overlay());
	if (ctl.canvas().is_hovered(Layout::SLIDER_X, Layout::SLIDER_Y,
			Layout::SLIDER_W, 20, in) && in.mouse_clicked)
	{
		frac = static_cast<float>(in.mouse_x - Layout::SLIDER_X)
			/ static_cast<float>(Layout::SLIDER_W);
		frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
		s.set_render_distance(14 + static_cast<int>(frac
				* static_cast<float>(161 - 14)));
	}
	if (ctl.canvas().was_clicked(Layout::Q_X, Layout::Q_Y, 110, 32, in))
		s.set_keyboard_layout(FT_DUMB_KEYBOARD_LAYOUT_QWERTY);
	if (ctl.canvas().was_clicked(Layout::A_X, Layout::A_Y, 110, 32, in))
		s.set_keyboard_layout(FT_DUMB_KEYBOARD_LAYOUT_AZERTY);
	if (ctl.canvas().was_clicked(Layout::BACK_X, Layout::BACK_Y, 200, 36, in))
		ctl.request_back();
}

void SettingsScene::render_fps_toggle(MenuCanvas &c, const Settings &s) const
{
	bool fps = s.fps_overlay();
	uint32_t bg = fps ? MenuCanvas::Colors::GREEN : MenuCanvas::Colors::PANEL;
	uint32_t bd = fps ? MenuCanvas::Colors::GREEN : MenuCanvas::Colors::TEXT_DIM;
	c.draw_text(60, Layout::TOG_Y - 4, "FPS OVERLAY", 2,
		MenuCanvas::Colors::TEXT);
	c.fill_rect(Layout::TOG_X, Layout::TOG_Y, 60, 20, bg);
	c.draw_border(Layout::TOG_X, Layout::TOG_Y, 60, 20, bd);
	c.draw_text(Layout::TOG_X + 12, Layout::TOG_Y + 4, fps ? "ON" : "OFF", 2,
		MenuCanvas::Colors::TEXT);
}

void SettingsScene::render_render_distance(MenuCanvas &c,
	const Settings &s) const
{
	int rd = s.render_distance();
	float frac = static_cast<float>(rd - 14) / static_cast<float>(160 - 14);
	int fw = static_cast<int>(frac * static_cast<float>(Layout::SLIDER_W));
	char rbuf[32];
	std::snprintf(rbuf, sizeof(rbuf), "%d CHUNKS", rd);
	c.draw_text(60, 150 + MenuCanvas::LINE_GAP, "RENDER DISTANCE", 2,
		MenuCanvas::Colors::TEXT);
	c.fill_rect(Layout::SLIDER_X, Layout::SLIDER_Y, Layout::SLIDER_W, 12,
		MenuCanvas::Colors::PANEL);
	c.draw_border(Layout::SLIDER_X, Layout::SLIDER_Y, Layout::SLIDER_W, 12,
		MenuCanvas::Colors::TEXT_DIM);
	c.fill_rect(Layout::SLIDER_X, Layout::SLIDER_Y, fw, 12,
		MenuCanvas::Colors::SLIDER);
	c.fill_rect(Layout::SLIDER_X + fw - 4, Layout::SLIDER_Y - 4, 8, 20,
		MenuCanvas::Colors::ACCENT);
	c.draw_text(Layout::SLIDER_X, Layout::SLIDER_Y + 20, rbuf, 2,
		MenuCanvas::Colors::TEXT);
}

void SettingsScene::render_keyboard(MenuCanvas &c, const Settings &s) const
{
	bool qsel = (s.keyboard_layout() == FT_DUMB_KEYBOARD_LAYOUT_QWERTY);
	c.draw_text(60, 274 + MenuCanvas::LINE_GAP * 2, "KEYBOARD LAYOUT", 2,
		MenuCanvas::Colors::TEXT);
	c.fill_rect(Layout::Q_X, Layout::Q_Y, 110, 32,
		qsel ? MenuCanvas::Colors::BTN_A : MenuCanvas::Colors::BTN_N);
	c.draw_border(Layout::Q_X, Layout::Q_Y, 110, 32,
		qsel ? MenuCanvas::Colors::ACCENT : MenuCanvas::Colors::TEXT_DIM);
	c.draw_text(Layout::Q_X + 14, Layout::Q_Y + 8, "QWERTY", 2,
		MenuCanvas::Colors::TEXT);
	c.fill_rect(Layout::A_X, Layout::A_Y, 110, 32,
		!qsel ? MenuCanvas::Colors::BTN_A : MenuCanvas::Colors::BTN_N);
	c.draw_border(Layout::A_X, Layout::A_Y, 110, 32,
		!qsel ? MenuCanvas::Colors::ACCENT : MenuCanvas::Colors::TEXT_DIM);
	c.draw_text(Layout::A_X + 14, Layout::A_Y + 8, "AZERTY", 2,
		MenuCanvas::Colors::TEXT);
}

void SettingsScene::render(MenuCanvas &c) const
{
	Settings &s = Settings::instance();
	c.clear(MenuCanvas::Colors::BG);
	c.draw_text_centered(MenuCanvas::W / 2, 30, "SETTINGS", 4,
		MenuCanvas::Colors::TITLE);
	c.fill_rect(40, 88, MenuCanvas::W - 80, 1, MenuCanvas::Colors::TEXT_DIM);
	render_fps_toggle(c, s);
	render_render_distance(c, s);
	render_keyboard(c, s);
	c.fill_rect(Layout::BACK_X, Layout::BACK_Y, 200, 36,
		MenuCanvas::Colors::BTN_N);
	c.draw_border(Layout::BACK_X, Layout::BACK_Y, 200, 36,
		MenuCanvas::Colors::TEXT_DIM);
	c.draw_text(Layout::BACK_X + 56, Layout::BACK_Y + 8, "BACK", 3,
		MenuCanvas::Colors::TEXT);
}
