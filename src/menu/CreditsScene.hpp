#ifndef CREDITSSCENE_HPP
# define CREDITSSCENE_HPP

# include "../../src/menu/MenuController.hpp"
# include "../../src/menu/MenuScene.hpp"

class CreditsScene : public MenuScene
{
  public:
	CreditsScene();
	CreditsScene(const CreditsScene &other);
	~CreditsScene() override;
	CreditsScene &operator=(const CreditsScene &other);
	void update(const MenuCanvas::MenuInput &in, MenuController &ctl) override;
	void render(MenuCanvas &c) const override;
};

#endif
