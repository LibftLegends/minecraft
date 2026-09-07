#include "../../src/validators/CameraInteractionValidator.hpp"
#include <cmath>
#include <cstdio>

CameraInteractionValidator::CameraInteractionValidator()
{
}

CameraInteractionValidator::CameraInteractionValidator(
	const CameraInteractionValidator &other) : IValidator(other)
{
	*this = other;
}

CameraInteractionValidator::~CameraInteractionValidator()
{
}

CameraInteractionValidator &CameraInteractionValidator::operator=(
	const CameraInteractionValidator &other)
{
	(void)other;
	return (*this);
}

int CameraInteractionValidator::validate() const
{
	World world;
	Camera camera;
	CameraInput input;
	double surface_top;
	double direction_x;
	double direction_y;
	double direction_z;
	int32_t hit_x;
	int32_t hit_y;
	int32_t hit_z;
	int32_t place_x;
	int32_t place_y;
	int32_t place_z;
	uint32_t hit_id;
	int32_t error_code;
	const double epsilon = 1.0e-9;

	error_code = world.initialize("camera-interaction-validator");
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("camera interaction world initialization",
			error_code));
	/* Drive the public camera update path far past the limit, rather than
	 * assigning the final value directly. */
	input.clear();
	input.look_down = true;
	camera.update(input, 10.0);
	if (std::fabs(camera.pitch + Camera::max_pitch()) > epsilon)
	{
		std::fprintf(stderr, "camera-interaction: pitch clamp failed pitch=%.17g\n",
			camera.pitch);
		world.destroy();
		return (1);
	}
	if (!world.surface_top_at(2, -6, &surface_top))
	{
		std::fprintf(stderr, "camera-interaction: surface lookup failed\n");
		world.destroy();
		return (1);
	}
	camera.x = 2.5;
	camera.z = -5.5;
	/* Keep the camera several blocks above the surface so the placement cell
	 * returned by a near-vertical ray is not rejected as intersecting the
	 * player's own body. */
	camera.y = surface_top + 4.0;
	camera.pitch = -Camera::max_pitch();
	camera.yaw = 0.0;
	PlayerController::camera_forward(camera, &direction_x, &direction_y,
		&direction_z);
	error_code = world.raycast_solid(camera.x, camera.y, camera.z,
		direction_x, direction_y, direction_z, 10.0, &hit_x, &hit_y, &hit_z);
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"camera-interaction: downward solid ray failed error=%d direction=(%.6f,%.6f,%.6f)\n",
			error_code, direction_x, direction_y, direction_z);
		world.destroy();
		return (1);
	}
	error_code = world.raycast_edit_target(camera.x, camera.y, camera.z,
		direction_x, direction_y, direction_z, 10.0, &hit_x, &hit_y, &hit_z,
		&place_x, &place_y, &place_z, &hit_id);
	if (error_code != FT_ERR_SUCCESS || place_y != hit_y + 1
		|| PlayerController::block_overlaps_player(camera, place_x, place_y,
			place_z))
	{
		std::fprintf(stderr,
			"camera-interaction: edit target invalid error=%d hit=(%d,%d,%d) place=(%d,%d,%d) overlap=%d\n",
			error_code, hit_x, hit_y, hit_z, place_x, place_y, place_z,
			PlayerController::block_overlaps_player(camera, place_x, place_y,
				place_z) ? 1 : 0);
		world.destroy();
		return (1);
	}
	std::printf("camera-interaction: ok pitch=%.6f hit=(%d,%d,%d) place=(%d,%d,%d)\n",
		camera.pitch, hit_x, hit_y, hit_z, place_x, place_y, place_z);
	world.destroy();
	return (0);
}
