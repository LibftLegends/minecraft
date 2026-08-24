#ifndef PLATFORM_LAUNCH_SUPPORT_HPP
# define PLATFORM_LAUNCH_SUPPORT_HPP

# include "../ft_vox.hpp"

class PlatformLaunchSupport
{
  public:
	PlatformLaunchSupport();
	PlatformLaunchSupport(const PlatformLaunchSupport &other);
	~PlatformLaunchSupport();
	PlatformLaunchSupport &operator=(const PlatformLaunchSupport &other);

	static ft_dumb_keyboard_layout detect_system_layout();
	static void wait_for_escape_release();
	static void clear_pending_quit_messages();
};

#endif
