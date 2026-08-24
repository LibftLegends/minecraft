#ifndef CAMERA_INPUT_HPP
# define CAMERA_INPUT_HPP

class CameraInput
{
  public:
	bool move_forward;
	bool move_backward;
	bool move_left;
	bool move_right;
	bool turn_left;
	bool turn_right;
	bool look_up;
	bool look_down;
	bool speed_boost;
	bool jump_pressed;
	bool jump_held;
	double mouse_delta_x;
	double mouse_delta_y;

	CameraInput();
	CameraInput(const CameraInput &other);
	~CameraInput();
	CameraInput &operator=(const CameraInput &other);

	void clear();
};

#endif
