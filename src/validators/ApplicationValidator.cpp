#include "../../src/validators/ApplicationValidator.hpp"

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
