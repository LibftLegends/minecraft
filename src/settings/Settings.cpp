#include "../../src/settings/Settings.hpp"

Settings::Settings(const Settings &other)
{
	(void)other;
}
Settings::~Settings()
{
}
Settings &Settings::operator=(const Settings &other)
{
	(void)other;
	return (*this);
}

bool Settings::fps_overlay() const
{
	return (fps_overlay_);
}
int Settings::render_distance() const
{
	return (render_distance_);
}
ft_dumb_keyboard_layout Settings::keyboard_layout() const
{
	return (layout_);
}

Settings &Settings::instance()
{
	static Settings	s;

	return (s);
}

Settings::Settings() : fps_overlay_(true), render_distance_(160),
	layout_(FT_DUMB_KEYBOARD_LAYOUT_QWERTY)
{
}

void Settings::set_fps_overlay(bool v)
{
	if (fps_overlay_ == v)
		return ;
	fps_overlay_ = v;
	notify();
}

void Settings::set_render_distance(int v)
{
	int	clamped;

	clamped = std::max(14, std::min(160, v));
	if (render_distance_ == clamped)
		return ;
	render_distance_ = clamped;
	notify();
}

void Settings::set_keyboard_layout(ft_dumb_keyboard_layout v)
{
	if (layout_ == v)
		return ;
	layout_ = v;
	ft_dumb_controls_set_keyboard_layout(v);
	notify();
}

void Settings::add_observer(ChangeCallback cb)
{
	observers_.push_back(cb);
}

void Settings::notify()
{
	for (auto &cb : observers_)
		cb();
}
