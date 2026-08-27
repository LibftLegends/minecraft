#ifndef MENU_SCENE_HPP
# define MENU_SCENE_HPP

# include "../../src/menu/MenuCanvas.hpp"

class	MenuController;

class MenuScene
{
  public:
	MenuScene();
	MenuScene(const MenuScene &other);
	virtual ~MenuScene();
	MenuScene &operator=(const MenuScene &other);

	virtual void on_enter();
	virtual void on_exit();
	virtual void update(const MenuCanvas::MenuInput &in,
			MenuController &ctl) = 0;
	virtual void render(MenuCanvas &c) const = 0;
};

#endif
