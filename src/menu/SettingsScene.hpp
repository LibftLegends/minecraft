#ifndef SETTINGSSCENE_HPP
# define SETTINGSSCENE_HPP

# include "../../src/menu/MenuController.hpp"
# include "../../src/menu/MenuScene.hpp"
# include "../../src/settings/Settings.hpp"

class SettingsScene : public MenuScene
{
  public:
	struct					Layout
	{
		static const int	TOG_X;
		static const int	TOG_Y;
		static const int	SLIDER_X;
		static const int	SLIDER_Y;
		static const int	SLIDER_W;
		static const int	Q_X;
		static const int	Q_Y;
		static const int	A_X;
		static const int	A_Y;
		static const int	BACK_X;
		static const int	BACK_Y;
	};

	SettingsScene();
	SettingsScene(const SettingsScene &other);
	~SettingsScene() override;
	SettingsScene &operator=(const SettingsScene &other);

	void update(const MenuCanvas::MenuInput &in, MenuController &ctl) override;
	void render(MenuCanvas &c) const override;

  private:
	void render_fps_toggle(MenuCanvas &c, const Settings &s) const;
	void render_render_distance(MenuCanvas &c, const Settings &s) const;
	void render_keyboard(MenuCanvas &c, const Settings &s) const;
};

#endif
