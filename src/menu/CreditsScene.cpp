#include "../../src/menu/CreditsScene.hpp"

CreditsScene::CreditsScene() : MenuScene()
{
}
CreditsScene::CreditsScene(const CreditsScene &other) : MenuScene()
{
	(void)other;
}
CreditsScene::~CreditsScene()
{
}
CreditsScene &CreditsScene::operator=(const CreditsScene &other)
{
	(void)other;
	return (*this);
}

void CreditsScene::update(const MenuCanvas::MenuInput &in, MenuController &ctl)
{
	if (in.key_escape || ctl.canvas().was_clicked((MenuCanvas::W - 200) / 2,
			MenuCanvas::H - 70, 200, 36, in))
		ctl.request_back();
}

void CreditsScene::render(MenuCanvas &c) const
{
	int	bx;

	bx = (MenuCanvas::W - 200) / 2;
	c.clear(MenuCanvas::Colors::BG);
	c.draw_text_centered(MenuCanvas::W / 2, 50, "CREDITS", 4,
		MenuCanvas::Colors::TITLE);
	c.fill_rect(60, 115, MenuCanvas::W - 120, 1, MenuCanvas::Colors::TEXT_DIM);
	c.draw_text_centered(MenuCanvas::W / 2, 145 + MenuCanvas::LINE_GAP,
		"MADE BY", 3, MenuCanvas::Colors::TEXT_DIM);
	c.draw_text_centered(MenuCanvas::W / 2, 200 + MenuCanvas::LINE_GAP * 2,
		"RPEREZ-T", 4, MenuCanvas::Colors::ACCENT);
	c.draw_text_centered(MenuCanvas::W / 2, 260 + MenuCanvas::LINE_GAP * 3,
		"AND", 2, MenuCanvas::Colors::TEXT_DIM);
	c.draw_text_centered(MenuCanvas::W / 2, 295 + MenuCanvas::LINE_GAP * 4,
		"BVANGENE", 4, MenuCanvas::Colors::ACCENT);
	c.fill_rect(60, 365, MenuCanvas::W - 120, 1, MenuCanvas::Colors::TEXT_DIM);
	c.fill_rect(bx, MenuCanvas::H - 70, 200, 36, MenuCanvas::Colors::BTN_N);
	c.draw_border(bx, MenuCanvas::H - 70, 200, 36,
		MenuCanvas::Colors::TEXT_DIM);
	c.draw_text(bx + 56, MenuCanvas::H - 58, "BACK", 3,
		MenuCanvas::Colors::TEXT);
}
