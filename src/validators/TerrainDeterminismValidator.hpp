#ifndef VOXEL_DETERMINISM_VALIDATOR_HPP
# define VOXEL_DETERMINISM_VALIDATOR_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif

# include "../../src/validators/IValidator.hpp"
# include "../ft_vox.hpp"

class TerrainDeterminismValidator : public IValidator
{
  private:
	static int compare_chunks(const game_voxel_chunk &left,
		const game_voxel_chunk &right);
	static int generate_pair(int32_t world_x, int32_t world_z,
		const char *seed);

  public:
	TerrainDeterminismValidator();
	TerrainDeterminismValidator(const TerrainDeterminismValidator &other);
	~TerrainDeterminismValidator();
	TerrainDeterminismValidator &operator=(const TerrainDeterminismValidator &other);

	virtual int validate() const override;
};

#endif
