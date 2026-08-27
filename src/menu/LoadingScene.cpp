#include "../../src/menu/LoadingScene.hpp"

const char *LoadingScene::DOTS[4] = {".", "..", "...", "...."};

LoadingScene::LoadingScene() : MenuScene(), tick_(0)
{
}
LoadingScene::LoadingScene(const LoadingScene &other) : MenuScene(),
	tick_(other.tick_)
{
}
LoadingScene::~LoadingScene()
{
}
LoadingScene &LoadingScene::operator=(const LoadingScene &other)
{
	(void)other;
	return (*this);
}

void LoadingScene::on_enter()
{
	tick_ = 0;
}

void LoadingScene::update(const MenuCanvas::MenuInput &in, MenuController &ctl)
{
	(void)in;
	(void)ctl;
	++tick_;
}

void LoadingScene::render(MenuCanvas &c) const
{
	int bar_w = MenuCanvas::W - 160;
	int bar_x = 80;
	int bar_y = MenuCanvas::H / 2 + 110;

	c.clear(MenuCanvas::Colors::BG);
	c.draw_text_centered(MenuCanvas::W / 2, MenuCanvas::H / 2 - 60,
		"GENERATING WORLD", 4, MenuCanvas::Colors::TITLE);
	c.draw_text_centered(MenuCanvas::W / 2, MenuCanvas::H / 2 + 10
		+ MenuCanvas::LINE_GAP, DOTS[(tick_ / 12) % 4], 4,
		MenuCanvas::Colors::ACCENT);
	c.draw_text_centered(MenuCanvas::W / 2, MenuCanvas::H / 2 + 70
		+ MenuCanvas::LINE_GAP * 2, "PLEASE WAIT", 2,
		MenuCanvas::Colors::TEXT_DIM);
	c.fill_rect(bar_x, bar_y, bar_w, 10, MenuCanvas::Colors::PANEL);
	c.draw_border(bar_x, bar_y, bar_w, 10, MenuCanvas::Colors::TEXT_DIM);
	c.fill_rect(bar_x, bar_y, (tick_ % 60) * bar_w / 60, 10,
		MenuCanvas::Colors::SLIDER);
}
