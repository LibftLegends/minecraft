#include "../../src/config/LaunchSettings.hpp"

const int32_t LaunchSettings::RESOLUTION_WIDTHS[3] = {1280, 1920, 3840};
const int32_t LaunchSettings::RESOLUTION_HEIGHTS[3] = {720, 1080, 2160};
const char *LaunchSettings::RESOLUTION_LABELS[3] = {"720P", "1080P",
	"4K WIDESCREEN"};

int32_t LaunchSettings::clamp_resolution_preset(int32_t preset)
{
	if (preset < 0)
		return (0);
	if (preset >= 3)
		return (2);
	return (preset);
}

ft_dumb_keyboard_layout LaunchSettings::clamp_keyboard_layout(int32_t layout)
{
	if (layout == static_cast<int32_t>(FT_DUMB_KEYBOARD_LAYOUT_AZERTY))
		return (FT_DUMB_KEYBOARD_LAYOUT_AZERTY);
	return (FT_DUMB_KEYBOARD_LAYOUT_QWERTY);
}

LaunchSettings::LaunchSettings()
{
	this->fullscreen = true;
	this->resolution_preset = 0;
	this->keyboard_layout = FT_DUMB_KEYBOARD_LAYOUT_AZERTY;
}

LaunchSettings::LaunchSettings(const LaunchSettings &other)
{
	*this = other;
}

LaunchSettings::~LaunchSettings()
{
}

LaunchSettings &LaunchSettings::operator=(const LaunchSettings &other)
{
	if (this != &other)
	{
		this->fullscreen = other.fullscreen;
		this->resolution_preset = other.resolution_preset;
		this->keyboard_layout = other.keyboard_layout;
	}
	return (*this);
}

int LaunchSettings::read_file_content(const char *path, std::string &out)
{
	long	sz;
	size_t	file_size;

	std::FILE *file;
	file = std::fopen(path, "rb");
	if (!file)
		return (1);
	if (std::fseek(file, 0, SEEK_END) != 0)
	{
		std::fclose(file);
		return (1);
	}
	sz = std::ftell(file);
	if (sz < 0)
	{
		std::fclose(file);
		return (1);
	}
	if (std::fseek(file, 0, SEEK_SET) != 0)
	{
		std::fclose(file);
		return (1);
	}
	file_size = static_cast<size_t>(sz);
	out.resize(file_size);
	if (file_size > 0U && std::fread(&out[0], 1, file_size, file) != file_size)
	{
		std::fclose(file);
		return (1);
	}
	std::fclose(file);
	out.push_back('\0');
	return (0);
}

void LaunchSettings::parse_json_settings(LaunchSettings &s,
	json_group *settings_group)
{
	json_item	*item;
	char		*end_ptr;
	long		val;

	item = json_find_item(settings_group, "fullscreen");
	if (item && item->value)
		s.fullscreen = (std::strcmp(item->value, "true") == 0);
	item = json_find_item(settings_group, "resolution_preset");
	if (item && item->value)
	{
		val = std::strtol(item->value, &end_ptr, 10);
		if (end_ptr != item->value && *end_ptr == '\0')
			s.resolution_preset = static_cast<int32_t>(val);
	}
	item = json_find_item(settings_group, "keyboard_layout");
	if (item && item->value)
		s.keyboard_layout = (std::strcmp(item->value,
					"azerty") == 0) ? FT_DUMB_KEYBOARD_LAYOUT_AZERTY : FT_DUMB_KEYBOARD_LAYOUT_QWERTY;
	s.resolution_preset = clamp_resolution_preset(s.resolution_preset);
	s.keyboard_layout = clamp_keyboard_layout(static_cast<int32_t>(s.keyboard_layout));
}

int LaunchSettings::load(const char *path)
{
	json_group	*groups;
	json_group	*settings_group;

	std::string content;
	if (!path)
		return (1);
	if (read_file_content(path, content) != 0)
		return (1);
	groups = json_read_from_string(content.c_str());
	if (!groups)
		return (1);
	settings_group = json_find_group(groups, "settings");
	if (!settings_group)
	{
		json_free_groups(groups);
		return (1);
	}
	parse_json_settings(*this, settings_group);
	json_free_groups(groups);
	return (0);
}

int LaunchSettings::save(const char *path) const
{
	json_group *groups;
	json_item *fullscreen_item;
	json_item *resolution_item;
	json_item *keyboard_item;
	int32_t error_code;

	if (path == nullptr)
		return (1);
	groups = json_create_json_group("settings");
	if (groups == nullptr)
		return (1);
	fullscreen_item = json_create_item("fullscreen",
			this->fullscreen ? FT_TRUE : FT_FALSE);
	resolution_item = json_create_item("resolution_preset",
			clamp_resolution_preset(this->resolution_preset));
	keyboard_item = json_create_item("keyboard_layout",
			this->keyboard_layout == FT_DUMB_KEYBOARD_LAYOUT_AZERTY ? "azerty" : "qwerty");
	if (!fullscreen_item || !resolution_item || !keyboard_item)
	{
		if (fullscreen_item)
			json_free_items(fullscreen_item);
		if (resolution_item)
			json_free_items(resolution_item);
		if (keyboard_item)
			json_free_items(keyboard_item);
		json_free_groups(groups);
		return (1);
	}
	json_add_item_to_group(groups, fullscreen_item);
	json_add_item_to_group(groups, resolution_item);
	json_add_item_to_group(groups, keyboard_item);
	error_code = json_write_to_file(path, groups);
	json_free_groups(groups);
	return (error_code == 0 ? 0 : 1);
}

int32_t LaunchSettings::resolution_width() const
{
	return (RESOLUTION_WIDTHS[clamp_resolution_preset(this->resolution_preset)]);
}

int32_t LaunchSettings::resolution_height() const
{
	return (RESOLUTION_HEIGHTS[clamp_resolution_preset(this->resolution_preset)]);
}

const char *LaunchSettings::resolution_label() const
{
	return (RESOLUTION_LABELS[clamp_resolution_preset(this->resolution_preset)]);
}

const char *LaunchSettings::display_mode_label() const
{
	return (this->fullscreen ? "FULLSCREEN" : "WINDOWED");
}

const char *LaunchSettings::keyboard_layout_label() const
{
	return ((this->keyboard_layout == FT_DUMB_KEYBOARD_LAYOUT_AZERTY) ? "AZERTY" : "QWERTY");
}
