#ifndef LAUNCH_SETTINGS_HPP
# define LAUNCH_SETTINGS_HPP

# include "../ft_vox.hpp"

class LaunchSettings
{
  public:
	bool fullscreen;
	int32_t resolution_preset;
	ft_dumb_keyboard_layout keyboard_layout;

	static const int32_t RESOLUTION_WIDTHS[3];
	static const int32_t RESOLUTION_HEIGHTS[3];
	static const char *RESOLUTION_LABELS[3];

	LaunchSettings();
	LaunchSettings(const LaunchSettings &other);
	~LaunchSettings();
	LaunchSettings &operator=(const LaunchSettings &other);

	int load(const char *path);
	int save(const char *path) const;

	int32_t resolution_width() const;
	int32_t resolution_height() const;
	const char *resolution_label() const;
	const char *display_mode_label() const;
	const char *keyboard_layout_label() const;

  private:
	static int32_t clamp_resolution_preset(int32_t preset);
	static ft_dumb_keyboard_layout clamp_keyboard_layout(int32_t layout);
	static int read_file_content(const char *path, std::string &out);
	static void parse_json_settings(LaunchSettings &s,
		json_group *settings_group);
};

#endif
