#include "../../src/validators/CameraSpeedValidator.hpp"

CameraSpeedValidator::CameraSpeedValidator()
{
}

CameraSpeedValidator::CameraSpeedValidator(const CameraSpeedValidator &other)
{
	*this = other;
}

CameraSpeedValidator::~CameraSpeedValidator()
{
}

CameraSpeedValidator &CameraSpeedValidator::operator=(const CameraSpeedValidator &other)
{
	(void)other;
	return (*this);
}

int CameraSpeedValidator::validate() const
{
	Camera camera;
	CameraInput input;
	double normal_start_z;
	double normal_distance;
	double boost_start_z;
	double boost_distance;

	camera.initialize();
	input = InputReader::empty_camera_input();
	input.move_forward = true;
	normal_start_z = camera.z;
	camera.update(input, 1.0);
	normal_distance = std::fabs(camera.z - normal_start_z);
	input.speed_boost = true;
	boost_start_z = camera.z;
	camera.update(input, 1.0);
	boost_distance = std::fabs(camera.z - boost_start_z);
	std::printf("camera-speed: normal=%.3f boost=%.3f\n", normal_distance,
		boost_distance);
	if (std::fabs(normal_distance - camera.speed) > 0.05
		|| std::fabs(boost_distance - (camera.speed * 20.0)) > 0.5)
		return (1);
	return (0);
}
