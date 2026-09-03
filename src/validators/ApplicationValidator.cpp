#include "../../src/validators/ApplicationValidator.hpp"

#include <cstdio>

ApplicationValidator::ApplicationValidator()
{
}

ApplicationValidator::ApplicationValidator(const ApplicationValidator &other)
{
	*this = other;
}

ApplicationValidator::~ApplicationValidator()
{
}

ApplicationValidator &ApplicationValidator::operator=(const ApplicationValidator &other)
{
	(void)other;
	return (*this);
}

int ApplicationValidator::validate_camera_speed()
{
	return (CameraSpeedValidator().validate());
}

int ApplicationValidator::validate_collision()
{
	return (CollisionValidator().validate());
}

int ApplicationValidator::validate_block_edit()
{
	return (BlockEditValidator().validate());
}

int ApplicationValidator::validate_visible_distance()
{
	return (WorldVisibilityValidator().validate());
}

int ApplicationValidator::validate_voxel_determinism()
{
	return (TerrainDeterminismValidator().validate());
}

int ApplicationValidator::validate_world_scale()
{
	return (WorldScaleValidator().validate());
}

int ApplicationValidator::validate_caves()
{
	return (TerrainCaveValidator().validate());
}

int ApplicationValidator::validate_voxel_configuration()
{
	return (TerrainConfigValidator().validate());
}

int ApplicationValidator::validate_world_revision()
{
	return (WorldRevisionValidator().validate());
}

int ApplicationValidator::validate_async_generation()
{
	return (WorldAsyncGenerationValidator().validate());
}

int ApplicationValidator::validate_all()
{
	int error_code;
	int failure_count;

	failure_count = 0;
	std::fprintf(stderr, "[Validator] validate-all: begin\n");
	error_code = ApplicationValidator::validate_camera_speed();
	if (error_code != 0)
	{
		std::fprintf(stderr, "[Validator] camera-speed failed: %d\n", error_code);
		failure_count += 1;
	}
	error_code = ApplicationValidator::validate_collision();
	if (error_code != 0)
	{
		std::fprintf(stderr, "[Validator] collision failed: %d\n", error_code);
		failure_count += 1;
	}
	error_code = ApplicationValidator::validate_block_edit();
	if (error_code != 0)
	{
		std::fprintf(stderr, "[Validator] block-edit failed: %d\n", error_code);
		failure_count += 1;
	}
	error_code = ApplicationValidator::validate_visible_distance();
	if (error_code != 0)
	{
		std::fprintf(stderr, "[Validator] visible-distance failed: %d\n", error_code);
		failure_count += 1;
	}
	error_code = ApplicationValidator::validate_voxel_determinism();
	if (error_code != 0)
	{
		std::fprintf(stderr, "[Validator] terrain-determinism failed: %d\n", error_code);
		failure_count += 1;
	}
	error_code = ApplicationValidator::validate_world_scale();
	if (error_code != 0)
	{
		std::fprintf(stderr, "[Validator] world-scale failed: %d\n", error_code);
		failure_count += 1;
	}
	error_code = ApplicationValidator::validate_caves();
	if (error_code != 0)
	{
		std::fprintf(stderr, "[Validator] caves failed: %d\n", error_code);
		failure_count += 1;
	}
	error_code = ApplicationValidator::validate_voxel_configuration();
	if (error_code != 0)
	{
		std::fprintf(stderr, "[Validator] terrain-configuration failed: %d\n", error_code);
		failure_count += 1;
	}
	error_code = ApplicationValidator::validate_world_revision();
	if (error_code != 0)
	{
		std::fprintf(stderr, "[Validator] world-revision failed: %d\n", error_code);
		failure_count += 1;
	}
	error_code = ApplicationValidator::validate_async_generation();
	if (error_code != 0)
	{
		std::fprintf(stderr, "[Validator] async-generation failed: %d\n", error_code);
		failure_count += 1;
	}
	std::fprintf(stderr, "[Validator] validate-all: %s failures=%d\n",
		failure_count == 0 ? "passed" : "failed", failure_count);
	return (failure_count == 0 ? 0 : 1);
}
