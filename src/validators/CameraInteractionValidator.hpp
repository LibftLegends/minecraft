#ifndef CAMERA_INTERACTION_VALIDATOR_HPP
# define CAMERA_INTERACTION_VALIDATOR_HPP

# include "../../src/camera/Camera.hpp"
# include "../../src/diagnostics/ApplicationError.hpp"
# include "../../src/player/PlayerController.hpp"
# include "../../src/validators/IValidator.hpp"
# include "../../src/world/World.hpp"

class CameraInteractionValidator : public IValidator
{
  public:
	CameraInteractionValidator();
	CameraInteractionValidator(const CameraInteractionValidator &other);
	~CameraInteractionValidator();
	CameraInteractionValidator &operator=(
		const CameraInteractionValidator &other);

	virtual int validate() const override;
};

#endif
