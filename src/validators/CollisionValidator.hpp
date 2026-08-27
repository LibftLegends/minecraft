#ifndef COLLISION_VALIDATOR_HPP
# define COLLISION_VALIDATOR_HPP

# include "../../src/diagnostics/ApplicationError.hpp"
# include "../../src/platform/InputReader.hpp"
# include "../../src/player/PlayerController.hpp"
# include "../../src/validators/IValidator.hpp"
# include "../ft_vox.hpp"

class			World;
class			Camera;

class CollisionValidator : public IValidator
{
  public:
	struct		Metrics
	{
		double	start_x;
		double	start_z;
		double	movement_delta_z;
		double	wall_delta_x;
		double	climb_delta_y;
		double	tunnel_delta_z;
		double	drop_delta_y;
	};

	CollisionValidator();
	CollisionValidator(const CollisionValidator &other);
	~CollisionValidator();
	CollisionValidator &operator=(const CollisionValidator &other);

	virtual int validate() const override;

  private:
	static int test_movement(World &world, Camera &camera, Metrics &m);
	static int test_wall(World &world, Camera &camera, Metrics &m);
};

# include "../../src/validators/CollisionTerrainScenarios.hpp"

#endif
