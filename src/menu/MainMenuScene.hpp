#ifndef MAINMENUSCENE_HPP
# define MAINMENUSCENE_HPP

# include "../../src/menu/MenuController.hpp"
# include "../../src/menu/MenuScene.hpp"

class MainMenuScene : public MenuScene
{
  public:
	struct					Layout
	{
		static const int	BTN_W;
		static const int	BTN_H;
		static const int	BTN_X;
		static const int	BTN_Y0;
		static const int	BTN_STRIDE;
	};

	MainMenuScene();
	MainMenuScene(const MainMenuScene &other);
	~MainMenuScene() override;
	MainMenuScene &operator=(const MainMenuScene &other);

	void update(const MenuCanvas::MenuInput &in, MenuController &ctl) override;
	void render(MenuCanvas &c) const override;

  private:
	static const char		*BTN_LABELS[4];
	int						selected_;

	static void handle_button_action(int index, MenuController &ctl);
};

#endif
