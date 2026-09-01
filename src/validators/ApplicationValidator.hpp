#ifndef APPLICATION_VALIDATOR_HPP
# define APPLICATION_VALIDATOR_HPP

# include "../../src/validators/BlockEditValidator.hpp"
# include "../../src/validators/CameraSpeedValidator.hpp"
# include "../../src/validators/CollisionValidator.hpp"
# include "../../src/validators/TerrainCaveValidator.hpp"
# include "../../src/validators/TerrainConfigValidator.hpp"
# include "../../src/validators/TerrainDeterminismValidator.hpp"
# include "../../src/validators/WorldAsyncGenerationValidator.hpp"
# include "../../src/validators/WorldRevisionValidator.hpp"
# include "../../src/validators/WorldScaleValidator.hpp"
# include "../../src/validators/WorldVisibilityValidator.hpp"

class ApplicationValidator
{
  public:
	ApplicationValidator();
	ApplicationValidator(const ApplicationValidator &other);
	~ApplicationValidator();
	ApplicationValidator &operator=(const ApplicationValidator &other);

	static int validate_camera_speed();
	static int validate_collision();
	static int validate_block_edit();
	static int validate_visible_distance();
	static int validate_voxel_determinism();
	static int validate_world_scale();
	static int validate_caves();
	static int validate_voxel_configuration();
	static int validate_world_revision();
	static int validate_async_generation();
};

#endif
