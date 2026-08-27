#ifndef APPLICATION_WINDOW_HPP
# define APPLICATION_WINDOW_HPP

# include "../../src/diagnostics/ApplicationError.hpp"
# include "../ft_vox.hpp"

class ApplicationWindow
{
  public:
	ApplicationWindow();
	ApplicationWindow(const ApplicationWindow &other);
	~ApplicationWindow();
	ApplicationWindow &operator=(const ApplicationWindow &other);

	int initialize(bool headless_mode, int width, int height, bool fullscreen,
		bool allow_gpu = true);
	int poll_events();
	int present();
	int destroy();
	bool should_close() const;
	bool is_headless() const;
	bool is_gpu_mode() const;
	const ft_gpu_window *get_gpu_window() const;
	ft_render_framebuffer &framebuffer();
	int mouse_x() const;
	int mouse_y() const;
	bool was_mouse_clicked() const;
	void set_cursor_visible(bool visible);
	bool was_settings_key_pressed() const;

  private:
	ft_render_window sw_window;
	ft_uniqueptr<ft_gpu_window> gpu_window;
	ft_render_framebuffer active_framebuffer;
	std::vector<uint32_t> headless_pixels;
	bool headless;
	bool opened;

	int initialize_headless_mode(int width, int height);
	int initialize_gpu_mode(int width, int height, bool fullscreen);
	int initialize_software_mode(int width, int height, bool fullscreen);
};

#endif
