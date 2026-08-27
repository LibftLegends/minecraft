#ifndef INGAMESETTINGSSCENE_HPP
# define INGAMESETTINGSSCENE_HPP

# include "../../src/menu/MenuController.hpp"
# include "../../src/menu/MenuScene.hpp"
# include "../../src/settings/Settings.hpp"

class InGameSettingsScene : public MenuScene
{
  public:
	struct					Layout
	{
		static const int	W;
		static const int	H;
		static const int	X;
		static const int	Y;
	};

	InGameSettingsScene();
	InGameSettingsScene(const InGameSettingsScene &other);
	~InGameSettingsScene() override;
	InGameSettingsScene &operator=(const InGameSettingsScene &other);

	void update(const MenuCanvas::MenuInput &in, MenuController &ctl) override;
	void render(MenuCanvas &c) const override;

  private:
	void render_fps_toggle(MenuCanvas &c, const Settings &s) const;
	void render_render_distance(MenuCanvas &c, const Settings &s) const;
	void render_keyboard(MenuCanvas &c, const Settings &s) const;
	void render_buttons(MenuCanvas &c) const;
};

#endif
