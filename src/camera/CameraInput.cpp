#include "../../src/camera/CameraInput.hpp"

CameraInput::CameraInput() : move_forward(false), move_backward(false),
	move_left(false), move_right(false), turn_left(false), turn_right(false),
	look_up(false), look_down(false), speed_boost(false), jump_pressed(false),
	jump_held(false), mouse_delta_x(0.0), mouse_delta_y(0.0)
{
}

CameraInput::CameraInput(const CameraInput &other) : move_forward(false),
	move_backward(false), move_left(false), move_right(false), turn_left(false),
	turn_right(false), look_up(false), look_down(false), speed_boost(false),
	jump_pressed(false), jump_held(false), mouse_delta_x(0.0),
	mouse_delta_y(0.0)
{
	*this = other;
}

CameraInput::~CameraInput()
{
}

CameraInput &CameraInput::operator=(const CameraInput &other)
{
	if (this != &other)
	{
		move_forward = other.move_forward;
		move_backward = other.move_backward;
		move_left = other.move_left;
		move_right = other.move_right;
		turn_left = other.turn_left;
		turn_right = other.turn_right;
		look_up = other.look_up;
		look_down = other.look_down;
		speed_boost = other.speed_boost;
		jump_pressed = other.jump_pressed;
		jump_held = other.jump_held;
		mouse_delta_x = other.mouse_delta_x;
		mouse_delta_y = other.mouse_delta_y;
	}
	return (*this);
}

void CameraInput::clear()
{
	move_forward = false;
	move_backward = false;
	move_left = false;
	move_right = false;
	turn_left = false;
	turn_right = false;
	look_up = false;
	look_down = false;
	speed_boost = false;
	jump_pressed = false;
	jump_held = false;
	mouse_delta_x = 0.0;
	mouse_delta_y = 0.0;
}
