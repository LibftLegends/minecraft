#ifndef APPLICATION_BOOTSTRAP_HPP
# define APPLICATION_BOOTSTRAP_HPP

# include "../../src/config/ApplicationOptions.hpp"
# include "../../src/config/LaunchSettings.hpp"
# include "../../src/config/LaunchSettingsMenu.hpp"
# include "../../src/diagnostics/ApplicationError.hpp"
# include "../../src/font/FontRenderer.hpp"
# include "../../src/platform/ApplicationWindow.hpp"
# include "../../src/platform/PlatformLaunchSupport.hpp"
# include "../../src/render/VoxelRenderer.hpp"
# include "../../src/settings/Settings.hpp"
# include "../../src/validators/ApplicationValidator.hpp"
# include "../ft_vox.hpp"

class ApplicationBootstrap
{
  public:
	ApplicationBootstrap();
	ApplicationBootstrap(const ApplicationBootstrap &other);
	~ApplicationBootstrap();
	ApplicationBootstrap &operator=(const ApplicationBootstrap &other);

	static int run_validators(const ApplicationOptions &options);
	static int setup_launch_config(ApplicationOptions &options,
		LaunchSettings &launch_settings);
	static int setup_window(ApplicationWindow &window, VoxelRenderer &renderer,
		const LaunchSettings &launch_settings);
};

#endif
