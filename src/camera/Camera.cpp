#include "../../src/camera/Camera.hpp"

const double Camera::MOUSE_SENSITIVITY = 0.0025;
const double Camera::TURN_SPEED = 1.8;
const double Camera::LOOK_SPEED = 1.2;
const double Camera::PITCH_LIMIT = 1.2;
const double Camera::BOOST_MULTIPLIER = 20.0;
const double Camera::INV_SQRT_2 = 0.7071067811865475;

Camera::Camera() : x(0.0), y(0.0), z(0.0), yaw(0.0), pitch(0.0), speed(0.0),
	vertical_velocity(0.0), on_ground(false)
{
	this->initialize();
}

Camera::Camera(const Camera &other) : x(0.0), y(0.0), z(0.0), yaw(0.0),
	pitch(0.0), speed(0.0), vertical_velocity(0.0), on_ground(false)
{
	*this = other;
}

Camera::~Camera()
{
}

Camera &Camera::operator=(const Camera &other)
{
	if (this != &other)
	{
		this->x = other.x;
		this->y = other.y;
		this->z = other.z;
		this->yaw = other.yaw;
		this->pitch = other.pitch;
		this->speed = other.speed;
		this->vertical_velocity = other.vertical_velocity;
		this->on_ground = other.on_ground;
	}
	return (*this);
}

double Camera::clamp(double value, double minimum, double maximum)
{
	if (value < minimum)
		return (minimum);
	if (value > maximum)
		return (maximum);
	return (value);
}

void Camera::initialize()
{
	this->x = 0.0;
	this->y = 78.62;
	this->z = -8.0;
	this->yaw = 0.0;
	this->pitch = 0.0;
	this->speed = 4.0;
	this->vertical_velocity = 0.0;
	this->on_ground = false;
}

#ifdef DEBUG

void Camera::update_orientation(const CameraInput &input, double delta_seconds)
{
	if (delta_seconds <= 0.0)
		return ;
	std::fprintf(stderr,
					"[camera-debug] before yaw=%.4f pitch=%.4f mouse=%.2f,%.2f dt=%.4f "
					"turn=%d,%d look=%d,%d\n",
					this->yaw,
					this->pitch,
					input.mouse_delta_x,
					input.mouse_delta_y,
					delta_seconds,
					input.turn_left ? 1 : 0,
					input.turn_right ? 1 : 0,
					input.look_up ? 1 : 0,
					input.look_down ? 1 : 0);
	this->yaw += input.mouse_delta_x * MOUSE_SENSITIVITY;
	this->pitch -= input.mouse_delta_y * MOUSE_SENSITIVITY;
	if (input.turn_left)
		this->yaw -= TURN_SPEED * delta_seconds;
	if (input.turn_right)
		this->yaw += TURN_SPEED * delta_seconds;
	if (input.look_up)
		this->pitch += LOOK_SPEED * delta_seconds;
	if (input.look_down)
		this->pitch -= LOOK_SPEED * delta_seconds;
	this->pitch = Camera::clamp(this->pitch, -PITCH_LIMIT, PITCH_LIMIT);
	std::fprintf(stderr, "[camera-debug] after  yaw=%.4f pitch=%.4f\n",
		this->yaw, this->pitch);
}

#else

void Camera::update_orientation(const CameraInput &input, double delta_seconds)
{
	if (delta_seconds <= 0.0)
		return ;
	this->yaw += input.mouse_delta_x * MOUSE_SENSITIVITY;
	this->pitch -= input.mouse_delta_y * MOUSE_SENSITIVITY;
	if (input.turn_left)
		this->yaw -= TURN_SPEED * delta_seconds;
	if (input.turn_right)
		this->yaw += TURN_SPEED * delta_seconds;
	if (input.look_up)
		this->pitch += LOOK_SPEED * delta_seconds;
	if (input.look_down)
		this->pitch -= LOOK_SPEED * delta_seconds;
	this->pitch = Camera::clamp(this->pitch, -PITCH_LIMIT, PITCH_LIMIT);
}

#endif

void Camera::apply_movement(const CameraInput &input, double delta_seconds)
{
	double	local_x;
	double	local_z;
	double	current_speed;
	double	distance;
	double	yaw_cos;
	double	yaw_sin;

	local_x = 0.0;
	local_z = 0.0;
	if (input.move_forward)
		local_z += 1.0;
	if (input.move_backward)
		local_z -= 1.0;
	if (input.move_right)
		local_x += 1.0;
	if (input.move_left)
		local_x -= 1.0;
	if ((local_x * local_x + local_z * local_z) > 1.0)
	{
		local_x *= INV_SQRT_2;
		local_z *= INV_SQRT_2;
	}
	current_speed = input.speed_boost ? this->speed
		* BOOST_MULTIPLIER : this->speed;
	distance = current_speed * delta_seconds;
	yaw_cos = std::cos(this->yaw);
	yaw_sin = std::sin(this->yaw);
	this->x += (local_x * yaw_cos + local_z * yaw_sin) * distance;
	this->z += (local_z * yaw_cos - local_x * yaw_sin) * distance;
}

void Camera::update(const CameraInput &input, double delta_seconds)
{
	if (delta_seconds <= 0.0)
		return ;
	this->update_orientation(input, delta_seconds);
	this->apply_movement(input, delta_seconds);
}
