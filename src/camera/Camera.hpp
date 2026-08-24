#ifndef CAMERA_HPP
# define CAMERA_HPP

# include "../../src/camera/CameraInput.hpp"
# include "../ft_vox.hpp"

class Camera
{
  public:
	double x;
	double y;
	double z;
	double yaw;
	double pitch;
	double speed;
	double vertical_velocity;
	bool on_ground;

	Camera();
	Camera(const Camera &other);
	~Camera();
	Camera &operator=(const Camera &other);

	void initialize();
	void update_orientation(const CameraInput &input, double delta_seconds);
	void update(const CameraInput &input, double delta_seconds);

  private:
	static const double MOUSE_SENSITIVITY;
	static const double TURN_SPEED;
	static const double LOOK_SPEED;
	static const double PITCH_LIMIT;
	static const double BOOST_MULTIPLIER;
	static const double INV_SQRT_2;

	static double clamp(double value, double minimum, double maximum);
	void apply_movement(const CameraInput &input, double delta_seconds);
};

#endif
