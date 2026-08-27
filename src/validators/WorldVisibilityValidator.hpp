#ifndef WORLD_VISIBILITY_VALIDATOR_HPP
# define WORLD_VISIBILITY_VALIDATOR_HPP

# include "../../src/camera/Camera.hpp"
# include "../../src/coordinates/WorldCoordinates.hpp"
# include "../../src/player/PlayerController.hpp"
# include "../../src/validators/IValidator.hpp"
# include "../../src/world/World.hpp"
# include "../ft_vox.hpp"

class WorldVisibilityValidator : public IValidator
{
  public:
	WorldVisibilityValidator();
	WorldVisibilityValidator(const WorldVisibilityValidator &other);
	~WorldVisibilityValidator();
	WorldVisibilityValidator &operator=(const WorldVisibilityValidator &other);

	virtual int validate() const override;
	static bool validate_visible_distance(const World &world, double camera_x,
		double camera_z, double yaw, int32_t required_distance);
};

#endif
