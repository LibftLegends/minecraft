#include "../../src/platform/InputReader.hpp"

InputReader::InputReader()
{
}

InputReader::InputReader(const InputReader &other)
{
	*this = other;
}

InputReader::~InputReader()
{
}

InputReader &InputReader::operator=(const InputReader &other)
{
	(void)other;
	return (*this);
}

CameraInput InputReader::read_camera_input(bool boost_enabled)
{
	CameraInput			input;
	ft_dumb_mouse_delta	mouse_delta;

	input.move_forward = (ft_dumb_control_is_down(FT_DUMB_CONTROL_UP) == FT_TRUE);
	input.move_backward = (ft_dumb_control_is_down(FT_DUMB_CONTROL_DOWN) == FT_TRUE);
	input.move_left = (ft_dumb_control_is_down(FT_DUMB_CONTROL_LEFT) == FT_TRUE);
	input.move_right = (ft_dumb_control_is_down(FT_DUMB_CONTROL_RIGHT) == FT_TRUE);
	input.turn_left = false;
	input.turn_right = false;
	input.look_up = false;
	input.look_down = false;
	input.speed_boost = boost_enabled;
	input.jump_pressed = (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_JUMP) == FT_TRUE);
	input.jump_held = (ft_dumb_control_is_down(FT_DUMB_CONTROL_JUMP) == FT_TRUE);
	mouse_delta = ft_dumb_controls_mouse_delta();
	input.mouse_delta_x = static_cast<double>(mouse_delta.x);
	input.mouse_delta_y = static_cast<double>(mouse_delta.y);
	return (input);
}

CameraInput InputReader::empty_camera_input()
{
	CameraInput	input;

	input.move_forward = false;
	input.move_backward = false;
	input.move_left = false;
	input.move_right = false;
	input.turn_left = false;
	input.turn_right = false;
	input.look_up = false;
	input.look_down = false;
	input.speed_boost = false;
	input.jump_pressed = false;
	input.mouse_delta_x = 0.0;
	input.mouse_delta_y = 0.0;
	return (input);
}
