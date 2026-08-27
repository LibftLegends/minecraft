#ifndef LOADINGSCENE_HPP
# define LOADINGSCENE_HPP

# include "../../src/menu/MenuController.hpp"
# include "../../src/menu/MenuScene.hpp"

class LoadingScene : public MenuScene
{
  public:
	LoadingScene();
	LoadingScene(const LoadingScene &other);
	~LoadingScene() override;
	LoadingScene &operator=(const LoadingScene &other);

	void update(const MenuCanvas::MenuInput &in, MenuController &ctl) override;
	void render(MenuCanvas &c) const override;
	void on_enter() override;

  private:
	static const char *DOTS[4];
	int tick_;
};

#endif
