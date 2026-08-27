#include "../../src/menu/MainMenuScene.hpp"

const int MainMenuScene::Layout::BTN_W = 280;
const int MainMenuScene::Layout::BTN_H = 44;
const int MainMenuScene::Layout::BTN_X = (MenuCanvas::W - BTN_W) / 2;
const int MainMenuScene::Layout::BTN_Y0 = 210;
const int MainMenuScene::Layout::BTN_STRIDE = BTN_H + 12 + MenuCanvas::LINE_GAP;

const char *MainMenuScene::BTN_LABELS[4] = {"START", "SETTINGS", "CREDITS",
	"EXIT"};

MainMenuScene::MainMenuScene() : MenuScene(), selected_(0)
{
}
MainMenuScene::MainMenuScene(const MainMenuScene &other) : MenuScene(),
	selected_(other.selected_)
{
}
MainMenuScene::~MainMenuScene()
{
}
MainMenuScene &MainMenuScene::operator=(const MainMenuScene &other)
{
	(void)other;
	return (*this);
}

void MainMenuScene::handle_button_action(int index, MenuController &ctl)
{
	switch (index)
	{
	case 0:
		ctl.request_start("");
		break ;
	case 1:
		ctl.request_settings();
		break ;
	case 2:
		ctl.request_credits();
		break ;
	case 3:
		ctl.request_exit();
		break ;
	default:
		break ;
	}
}

void MainMenuScene::update(const MenuCanvas::MenuInput &in, MenuController &ctl)
{
	int	by;

	if (in.key_up)
	{
		--selected_;
		if (selected_ < 0)
			selected_ = 3;
	}
	if (in.key_down)
	{
		++selected_;
		if (selected_ > 3)
			selected_ = 0;
	}
	for (int i = 0; i < 4; ++i)
	{
		by = Layout::BTN_Y0 + i * Layout::BTN_STRIDE;
		if (ctl.canvas().is_hovered(Layout::BTN_X, by, Layout::BTN_W,
				Layout::BTN_H, in))
			selected_ = i;
		if (ctl.canvas().was_clicked(Layout::BTN_X, by, Layout::BTN_W,
				Layout::BTN_H, in))
			MainMenuScene::handle_button_action(i, ctl);
	}
	if (in.key_enter)
		MainMenuScene::handle_button_action(selected_, ctl);
}

void MainMenuScene::render(MenuCanvas &c) const
{
	const char *nav_hint = "USE ARROWS OR MOUSE TO NAVIGATE  ENTER TO SELECT";

	c.clear(MenuCanvas::Colors::BG);
	c.draw_text_centered(MenuCanvas::W / 2, 80, "FT_VOX", 6,
		MenuCanvas::Colors::TITLE);
	for (int i = 0; i < 4; ++i)
	{
		int by = Layout::BTN_Y0 + i * Layout::BTN_STRIDE;
		bool sel = (i == selected_);
		uint32_t bg = sel ? MenuCanvas::Colors::BTN_A : MenuCanvas::Colors::BTN_N;
		uint32_t bd = sel ? MenuCanvas::Colors::ACCENT : MenuCanvas::Colors::TEXT_DIM;
		c.fill_rect(Layout::BTN_X, by, Layout::BTN_W, Layout::BTN_H, bg);
		c.draw_border(Layout::BTN_X, by, Layout::BTN_W, Layout::BTN_H, bd);
		c.draw_text_centered(Layout::BTN_X + Layout::BTN_W / 2, by
			+ (Layout::BTN_H - 20) / 2, BTN_LABELS[i], 3,
			MenuCanvas::Colors::TEXT);
	}
	c.draw_text_centered(MenuCanvas::W / 2, MenuCanvas::H - 24, nav_hint, 1,
		MenuCanvas::Colors::TEXT_DIM);
}
