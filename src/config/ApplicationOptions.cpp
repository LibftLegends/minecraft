#include "../../src/config/ApplicationOptions.hpp"

ApplicationOptions::ApplicationOptions() : perf_test_mode(false),
	perf_headless_mode(false), perf_move_mode(false), perf_boost_mode(false),
	analytics_no_exporter(false), analytics_no_instrumentation(false),
	auto_start(false),
	worldgen_probe_mode(false),
	validate_camera_speed_mode(false), validate_collision_mode(false),
	validate_block_edit_mode(false), validate_visible_distance_mode(false),
	validate_voxel_determinism_mode(false), validate_world_scale_mode(false),
	validate_caves_mode(false), validate_voxel_configuration_mode(false),
	validate_world_revision_mode(false), validate_async_generation_mode(false),
	validate_all_mode(false),
	perf_seconds_limit(10.0)
{
}

ApplicationOptions::ApplicationOptions(const ApplicationOptions &other) : perf_test_mode(false),
	perf_headless_mode(false), perf_move_mode(false), perf_boost_mode(false),
	analytics_no_exporter(false), analytics_no_instrumentation(false),
	auto_start(false),
	worldgen_probe_mode(false),
	validate_camera_speed_mode(false), validate_collision_mode(false),
	validate_block_edit_mode(false), validate_visible_distance_mode(false),
	validate_voxel_determinism_mode(false), validate_world_scale_mode(false),
	validate_caves_mode(false), validate_voxel_configuration_mode(false),
	validate_world_revision_mode(false), validate_async_generation_mode(false),
	validate_all_mode(false),
	perf_seconds_limit(10.0)
{
	*this = other;
}

ApplicationOptions::~ApplicationOptions()
{
}

ApplicationOptions &ApplicationOptions::operator=(const ApplicationOptions &other)
{
	if (this != &other)
	{
		perf_test_mode = other.perf_test_mode;
		perf_headless_mode = other.perf_headless_mode;
		perf_move_mode = other.perf_move_mode;
		perf_boost_mode = other.perf_boost_mode;
		analytics_no_exporter = other.analytics_no_exporter;
		analytics_no_instrumentation = other.analytics_no_instrumentation;
		auto_start = other.auto_start;
		worldgen_probe_mode = other.worldgen_probe_mode;
		validate_camera_speed_mode = other.validate_camera_speed_mode;
		validate_collision_mode = other.validate_collision_mode;
		validate_block_edit_mode = other.validate_block_edit_mode;
		validate_visible_distance_mode = other.validate_visible_distance_mode;
		validate_voxel_determinism_mode = other.validate_voxel_determinism_mode;
		validate_world_scale_mode = other.validate_world_scale_mode;
		validate_caves_mode = other.validate_caves_mode;
		validate_voxel_configuration_mode = other.validate_voxel_configuration_mode;
		validate_world_revision_mode = other.validate_world_revision_mode;
		validate_async_generation_mode = other.validate_async_generation_mode;
		validate_all_mode = other.validate_all_mode;
		perf_seconds_limit = other.perf_seconds_limit;
	}
	return (*this);
}

int ApplicationOptions::parse(int argc, char **argv)
{
	int	result;

	perf_headless_mode = CommandLine::has_flag(argc, argv,
			"--perf-test-headless");
	perf_test_mode = CommandLine::has_flag(argc, argv, "--perf-test")
		|| perf_headless_mode;
	perf_move_mode = CommandLine::has_flag(argc, argv, "--perf-move");
	perf_boost_mode = CommandLine::has_flag(argc, argv, "--perf-boost");
	analytics_no_exporter = CommandLine::has_flag(argc, argv,
			"--analytics-no-exporter");
	analytics_no_instrumentation = CommandLine::has_flag(argc, argv,
			"--analytics-no-instrumentation");
	auto_start = CommandLine::has_flag(argc, argv, "--auto-start");
	worldgen_probe_mode = CommandLine::has_flag(argc, argv,
		"--worldgen-probe");
	validate_camera_speed_mode = CommandLine::has_flag(argc, argv,
			"--validate-camera-speed");
	validate_collision_mode = CommandLine::has_flag(argc, argv,
			"--validate-collision");
	validate_block_edit_mode = CommandLine::has_flag(argc, argv,
			"--validate-block-edit");
	validate_visible_distance_mode = CommandLine::has_flag(argc, argv,
			"--validate-visible-distance");
	validate_voxel_determinism_mode = CommandLine::has_flag(argc, argv,
			"--validate-terrain-determinism");
	validate_world_scale_mode = CommandLine::has_flag(argc, argv,
			"--validate-world-scale");
	validate_caves_mode = CommandLine::has_flag(argc, argv, "--validate-caves");
	validate_voxel_configuration_mode = CommandLine::has_flag(argc, argv,
			"--validate-terrain-configuration");
	validate_world_revision_mode = CommandLine::has_flag(argc, argv,
			"--validate-world-revision");
	validate_async_generation_mode = CommandLine::has_flag(argc, argv,
			"--validate-async-generation");
	validate_all_mode = CommandLine::has_flag(argc, argv, "--validate-all");
	result = CommandLine::parse_perf_seconds(argc, argv, &perf_seconds_limit);
	if (result < 0)
	{
		std::fputs("Application: invalid --perf-seconds value\n", stderr);
		return (1);
	}
	return (0);
}
