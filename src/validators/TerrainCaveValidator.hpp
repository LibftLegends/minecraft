#ifndef TERRAIN_CAVE_VALIDATOR_HPP
# define TERRAIN_CAVE_VALIDATOR_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif

# include "../../src/validators/IValidator.hpp"
# include "../ft_vox.hpp"

class TerrainCaveValidator : public IValidator
{
  private:
	static bool chunk_has_cave(const game_voxel_chunk &chunk);
	static int validate_origin(int32_t world_x, int32_t world_z,
		bool *found_cave);

  public:
	TerrainCaveValidator();
	TerrainCaveValidator(const TerrainCaveValidator &other);
	~TerrainCaveValidator();
	TerrainCaveValidator &operator=(const TerrainCaveValidator &other);

	virtual int validate() const override;
};

#endif
