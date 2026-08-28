#include "../../src/app/ApplicationBootstrap.hpp"

ApplicationBootstrap::ApplicationBootstrap()
{
}

ApplicationBootstrap::ApplicationBootstrap(const ApplicationBootstrap &other)
{
	(void)other;
}

ApplicationBootstrap::~ApplicationBootstrap()
{
}

ApplicationBootstrap &ApplicationBootstrap::operator=(const ApplicationBootstrap &other)
{
	(void)other;
	return (*this);
}

int ApplicationBootstrap::run_validators(const ApplicationOptions &options)
{
	if (options.validate_camera_speed_mode)
		return (ApplicationValidator::validate_camera_speed());
	if (options.validate_collision_mode)
		return (ApplicationValidator::validate_collision());
	if (options.validate_block_edit_mode)
		return (ApplicationValidator::validate_block_edit());
	if (options.validate_visible_distance_mode)
		return (ApplicationValidator::validate_visible_distance());
	if (options.validate_terrain_determinism_mode)
		return (ApplicationValidator::validate_terrain_determinism());
	if (options.validate_world_scale_mode)
		return (ApplicationValidator::validate_world_scale());
	if (options.validate_caves_mode)
		return (ApplicationValidator::validate_caves());
	if (options.validate_terrain_configuration_mode)
		return (ApplicationValidator::validate_terrain_configuration());
	if (options.validate_world_revision_mode)
		return (ApplicationValidator::validate_world_revision());
	if (options.validate_async_generation_mode)
		return (ApplicationValidator::validate_async_generation());
	return (-1);
}

int ApplicationBootstrap::setup_launch_config(ApplicationOptions &options,
	LaunchSettings &launch_settings)
{
	const char				*cfg = "ft_vox_config.json";
	ft_dumb_keyboard_layout	detected;

	if (!options.perf_test_mode && !options.perf_headless_mode)
	{
		if (launch_settings.load(cfg) != 0)
		{
			if (LaunchSettingsMenu::run(launch_settings) != 0)
				return (1);
			if (launch_settings.save(cfg) != 0)
				std::fprintf(stderr,
					"Application: unable to save settings to %s\n", cfg);
			PlatformLaunchSupport::wait_for_escape_release();
			PlatformLaunchSupport::clear_pending_quit_messages();
		}
	}
	if (launch_settings.keyboard_layout == FT_DUMB_KEYBOARD_LAYOUT_QWERTY)
	{
		detected = PlatformLaunchSupport::detect_system_layout();
		if (detected != FT_DUMB_KEYBOARD_LAYOUT_QWERTY)
			launch_settings.keyboard_layout = detected;
	}
	ft_dumb_controls_set_keyboard_layout(launch_settings.keyboard_layout);
	Settings::instance().set_keyboard_layout(launch_settings.keyboard_layout);
	return (0);
}

int ApplicationBootstrap::setup_window(ApplicationWindow &window,
	VoxelRenderer &renderer, const LaunchSettings &launch_settings)
{
	int32_t	error_code;

	if (window.initialize(false, launch_settings.resolution_width(),
			launch_settings.resolution_height(), launch_settings.fullscreen,
			true) != 0)
		return (1);
	FontRenderer::instance().load(22.0f);
	if (window.is_gpu_mode())
	{
		error_code = renderer.initialize_gpu(window.get_gpu_window()->get_width(),
				window.get_gpu_window()->get_height());
		if (error_code != FT_ERR_SUCCESS)
		{
			window.destroy();
			return (ApplicationError::fail("GPU renderer initialization",
					error_code));
		}
	}
	renderer.preload_assets();
	window.set_cursor_visible(true);
	return (0);
}
