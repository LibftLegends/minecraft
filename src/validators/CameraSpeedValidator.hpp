#ifndef CAMERA_SPEED_VALIDATOR_HPP
# define CAMERA_SPEED_VALIDATOR_HPP

# include "../../src/camera/Camera.hpp"
# include "../../src/platform/InputReader.hpp"
# include "../../src/validators/IValidator.hpp"
# include "../ft_vox.hpp"

class CameraSpeedValidator : public IValidator
{
  public:
	CameraSpeedValidator();
	CameraSpeedValidator(const CameraSpeedValidator &other);
	~CameraSpeedValidator();
	CameraSpeedValidator &operator=(const CameraSpeedValidator &other);

	virtual int validate() const override;
};

#endif
