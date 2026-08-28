#ifndef MENU_CONTROLLER_HPP
#define MENU_CONTROLLER_HPP

#include "../ft_vox.hpp"
#include "../../src/gpur/GpuRenderer.hpp"
#include "../../src/menu/MenuCanvas.hpp"
#include "../../src/menu/MenuScene.hpp"

class MainMenuScene;
class SettingsScene;
class CreditsScene;
class LoadingScene;
class InGameSettingsScene;

class MenuController
{
  public:
    enum class Phase
    {
        MAIN_MENU,
        SETTINGS,
        CREDITS,
        LOADING,
        IN_GAME_SETTINGS
    };

    MenuController();
    MenuController(const MenuController &other);
    ~MenuController();
    MenuController &operator=(const MenuController &other);

    void show_main_menu();
    void show_settings();
    void show_credits();
    void show_loading();
    void show_in_game_settings();

    void update(const MenuCanvas::MenuInput &in);
    void render(GpuRenderer *gpu);
    void render_overlay(GpuRenderer *gpu);
    void render(ft_render_framebuffer &framebuffer);
    void render_overlay(ft_render_framebuffer &framebuffer);

    bool wants_start() const;
    bool wants_exit() const;
    bool dismissed_in_game() const;
    bool wants_main_menu() const;

    const std::string &start_seed() const;
    void clear_flags();

    void request_start(const std::string &seed);
    void request_exit();
    void request_settings();
    void request_credits();
    void request_back();
    void request_dismiss_in_game();
    void request_main_menu_from_game();

    MenuCanvas &canvas();

  private:
    MenuCanvas canvas_;
    Phase phase_;

    ft_uniqueptr<MainMenuScene> main_scene_;
    ft_uniqueptr<SettingsScene> settings_scene_;
    ft_uniqueptr<CreditsScene> credits_scene_;
    ft_uniqueptr<LoadingScene> loading_scene_;
    ft_uniqueptr<InGameSettingsScene> in_game_scene_;

    bool wants_start_;
    bool wants_exit_;
    bool dismissed_in_game_;
    bool wants_main_menu_;
    bool canvas_dirty_;
    bool overlay_dirty_;
    bool has_last_input_;
    MenuCanvas::MenuInput last_input_;
    std::string start_seed_;

    MenuScene *current_scene();
    static std::string make_seed();
};

#endif
