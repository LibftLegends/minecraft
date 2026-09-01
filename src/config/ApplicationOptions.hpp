#ifndef APPLICATION_OPTIONS_HPP
# define APPLICATION_OPTIONS_HPP

# include "../../src/config/CommandLine.hpp"
# include "../ft_vox.hpp"

class ApplicationOptions
{
  public:
	bool perf_test_mode;
	bool perf_headless_mode;
	bool perf_move_mode;
	bool perf_boost_mode;
	bool validate_camera_speed_mode;
	bool validate_collision_mode;
	bool validate_block_edit_mode;
	bool validate_visible_distance_mode;
	bool validate_voxel_determinism_mode;
	bool validate_world_scale_mode;
	bool validate_caves_mode;
	bool validate_voxel_configuration_mode;
	bool validate_world_revision_mode;
	bool validate_async_generation_mode;
	double perf_seconds_limit;

	ApplicationOptions();
	ApplicationOptions(const ApplicationOptions &other);
	~ApplicationOptions();
	ApplicationOptions &operator=(const ApplicationOptions &other);

	int parse(int argc, char **argv);
};

#endif
