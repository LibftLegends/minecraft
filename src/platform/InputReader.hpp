#ifndef INPUT_READER_HPP
# define INPUT_READER_HPP

# include "../../src/camera/CameraInput.hpp"
# include "../ft_vox.hpp"

class InputReader
{
  public:
	InputReader();
	InputReader(const InputReader &other);
	~InputReader();
	InputReader &operator=(const InputReader &other);

	static CameraInput read_camera_input(bool boost_enabled);
	static CameraInput empty_camera_input();
};

#endif
