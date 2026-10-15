#include "../../src/menu/MenuController.hpp"

void MenuController::blit_canvas_to_framebuffer(const MenuCanvas &canvas,
	ft_render_framebuffer &framebuffer)
{
    const uint32_t *src = canvas.pixels();
    if (src == nullptr || framebuffer.pixels == nullptr || framebuffer.width <= 0 ||
        framebuffer.height <= 0)
        return;
    for (int32_t y = 0; y < framebuffer.height; ++y)
    {
        int32_t src_y = (y * canvas.height()) / framebuffer.height;
        for (int32_t x = 0; x < framebuffer.width; ++x)
        {
            int32_t src_x = (x * canvas.width()) / framebuffer.width;
            framebuffer.pixels[y * framebuffer.width + x] =
                src[static_cast<size_t>(src_y * canvas.width() + src_x)];
        }
    }
}

static bool menu_input_changed(const MenuCanvas::MenuInput &left,
                        const MenuCanvas::MenuInput &right)
{
    return left.mouse_x != right.mouse_x || left.mouse_y != right.mouse_y
        || left.mouse_clicked != right.mouse_clicked
        || left.key_up != right.key_up || left.key_down != right.key_down
        || left.key_enter != right.key_enter
        || left.key_escape != right.key_escape;
}

std::string MenuController::make_seed()
{
	static uint64_t	counter = 0U;
	uint64_t		t;
	char			buf[17];

	t = static_cast<uint64_t>(std::time(nullptr));
	t ^= ++counter * UINT64_C(6364136223846793005);
	t ^= t >> 33;
	t *= UINT64_C(0xff51afd7ed558ccd);
	t ^= t >> 33;
	std::snprintf(buf, sizeof(buf), "%016llx",
		static_cast<unsigned long long>(t));
	return (std::string(buf));
}

MenuController::MenuController()
    : canvas_(), phase_(Phase::MAIN_MENU), main_scene_(new MainMenuScene()),
      settings_scene_(new SettingsScene()), credits_scene_(new CreditsScene()),
      loading_scene_(new LoadingScene()), in_game_scene_(new InGameSettingsScene()),
      wants_start_(false), wants_exit_(false), dismissed_in_game_(false), wants_main_menu_(false),
      canvas_dirty_(true), overlay_dirty_(true), has_last_input_(false), last_input_()
{
}

MenuController::MenuController(const MenuController &other)
    : canvas_(), phase_(Phase::MAIN_MENU), main_scene_(new MainMenuScene()),
      settings_scene_(new SettingsScene()), credits_scene_(new CreditsScene()),
      loading_scene_(new LoadingScene()), in_game_scene_(new InGameSettingsScene()),
      wants_start_(false), wants_exit_(false), dismissed_in_game_(false), wants_main_menu_(false),
      canvas_dirty_(true), overlay_dirty_(true), has_last_input_(false), last_input_()
{
	(void)other;
}

MenuController::~MenuController()
{
}

MenuController &MenuController::operator=(const MenuController &other)
{
	(void)other;
	return (*this);
}

MenuScene *MenuController::current_scene()
{
	switch (phase_)
	{
	case Phase::SETTINGS:
		return (settings_scene_.get());
	case Phase::CREDITS:
		return (credits_scene_.get());
	case Phase::LOADING:
		return (loading_scene_.get());
	case Phase::IN_GAME_SETTINGS:
		return (in_game_scene_.get());
	default:
		return (main_scene_.get());
	}
}

void MenuController::show_main_menu()
{
    MenuScene *prev = current_scene();
    if (prev)
        prev->on_exit();
    phase_ = Phase::MAIN_MENU;
    main_scene_->on_enter();
    canvas_dirty_ = true;
    overlay_dirty_ = true;
    clear_flags();
}

void MenuController::show_settings()
{
    phase_ = Phase::SETTINGS;
    settings_scene_->on_enter();
    canvas_dirty_ = true;
    overlay_dirty_ = true;
}

void MenuController::show_credits()
{
    phase_ = Phase::CREDITS;
    credits_scene_->on_enter();
    canvas_dirty_ = true;
    overlay_dirty_ = true;
}

void MenuController::show_loading()
{
    phase_ = Phase::LOADING;
    loading_scene_->on_enter();
    canvas_dirty_ = true;
    overlay_dirty_ = true;
}

void MenuController::show_in_game_settings()
{
    phase_ = Phase::IN_GAME_SETTINGS;
    in_game_scene_->on_enter();
    canvas_dirty_ = true;
    overlay_dirty_ = true;
    clear_flags();
}

void MenuController::update(const MenuCanvas::MenuInput &in)
{
    if (!has_last_input_ || menu_input_changed(last_input_, in))
    {
        canvas_dirty_ = true;
        overlay_dirty_ = true;
    }
    if (phase_ == Phase::LOADING)
        canvas_dirty_ = true;
    last_input_ = in;
    has_last_input_ = true;
    MenuScene *s = current_scene();
    if (s)
        s->update(in, *this);
}

void MenuController::render(GpuRenderer *gpu)
{
    if (!gpu)
        return;
    if (canvas_dirty_)
    {
        gpu->clear_screen();
        canvas_.clear(MenuCanvas::Colors::BG);
        MenuScene *s = current_scene();
        if (s)
            s->render(canvas_);
        gpu->upload_menu(canvas_.pixels(), canvas_.width(), canvas_.height());
        canvas_dirty_ = false;
    }
    gpu->draw_menu(1.0f);
}

void MenuController::render_overlay(GpuRenderer *gpu)
{
    if (!gpu)
        return;
    gpu->draw_tint(0.0f, 0.0f, 0.0f, 0.55f);
    if (overlay_dirty_)
    {
        canvas_.clear(0U);
        MenuScene *s = current_scene();
        if (s)
            s->render(canvas_);
        gpu->upload_menu(canvas_.pixels(), canvas_.width(), canvas_.height());
        overlay_dirty_ = false;
    }
    gpu->draw_menu(0.95f);
    canvas_dirty_ = true;
}

void MenuController::render(ft_render_framebuffer &framebuffer)
{
    if (canvas_dirty_)
    {
        canvas_.clear(MenuCanvas::Colors::BG);
        MenuScene *s = current_scene();
        if (s)
            s->render(canvas_);
        canvas_dirty_ = false;
    }
    blit_canvas_to_framebuffer(canvas_, framebuffer);
}

void MenuController::render_overlay(ft_render_framebuffer &framebuffer)
{
    if (overlay_dirty_)
    {
        canvas_.clear(0U);
        MenuScene *s = current_scene();
        if (s)
            s->render(canvas_);
        overlay_dirty_ = false;
    }
    blit_canvas_to_framebuffer(canvas_, framebuffer);
    canvas_dirty_ = true;
}

void MenuController::clear_flags()
{
	wants_start_ = false;
	wants_exit_ = false;
	dismissed_in_game_ = false;
	wants_main_menu_ = false;
}

void MenuController::request_start(const std::string &seed)
{
	wants_start_ = true;
	start_seed_ = seed.empty() ? make_seed() : seed;
}

void MenuController::request_exit()
{
	wants_exit_ = true;
}
void MenuController::request_settings()
{
	show_settings();
}
void MenuController::request_credits()
{
	show_credits();
}
void MenuController::request_back()
{
	show_main_menu();
}
void MenuController::request_dismiss_in_game()
{
	dismissed_in_game_ = true;
}
void MenuController::request_main_menu_from_game()
{
	wants_main_menu_ = true;
}

bool MenuController::wants_start() const
{
	return (wants_start_);
}
bool MenuController::wants_exit() const
{
	return (wants_exit_);
}
bool MenuController::dismissed_in_game() const
{
	return (dismissed_in_game_);
}
bool MenuController::wants_main_menu() const
{
	return (wants_main_menu_);
}
const std::string &MenuController::start_seed() const
{
	return (start_seed_);
}
MenuCanvas &MenuController::canvas()
{
	return (canvas_);
}
