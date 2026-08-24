#ifndef LAUNCH_SETTINGS_MENU_HPP
# define LAUNCH_SETTINGS_MENU_HPP

# include "../../src/config/LaunchSettings.hpp"
# include "../../src/font/FontRenderer.hpp"
# include "../../src/gpur/GpuRenderer.hpp"
# include "../../src/platform/ApplicationWindow.hpp"
# include "../ft_vox.hpp"

class LaunchSettingsMenu
{
  public:
	LaunchSettingsMenu();
	LaunchSettingsMenu(const LaunchSettingsMenu &other);
	~LaunchSettingsMenu();
	LaunchSettingsMenu &operator=(const LaunchSettingsMenu &other);

	static int run(LaunchSettings &settings);

  private:
	struct			Image
	{
		int32_t		width;
		int32_t		height;
		uint32_t	*pixels;
	};

	static uint32_t make_color(uint32_t r, uint32_t g, uint32_t b);
	static void draw_rectangle(Image &img, int32_t x, int32_t y, int32_t w,
		int32_t h, uint32_t color);
	static void draw_text(Image &img, int32_t x, int32_t y, const char *text,
		int32_t scale, uint32_t color);
	static void draw_background(Image &img);
	static void draw_row(Image &img, const LaunchSettings &settings,
		int32_t row, int32_t row_y, int32_t selected_row);
	static void draw_menu(Image &img, const LaunchSettings &settings,
		int32_t selected_row);

	static int init_renderer(ApplicationWindow &window, GpuRenderer &renderer,
		int32_t &width, int32_t &height);
	static void handle_input(LaunchSettings &settings, int32_t &selected_row,
		bool &should_exit);
	static int render_frame(ApplicationWindow &window, GpuRenderer &renderer,
		Image &img, const LaunchSettings &settings, int32_t selected_row,
		std::vector<uint32_t> &pixels);
	static int run_loop(ApplicationWindow &window, GpuRenderer &renderer,
		LaunchSettings &settings, int32_t width, int32_t height);
};

#endif
