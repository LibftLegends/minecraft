#ifndef WORLD_SCALE_VALIDATOR_HPP
# define WORLD_SCALE_VALIDATOR_HPP

# include "../../src/chunks/WorldChunkLoader.hpp"
# include "../../src/validators/IValidator.hpp"
# include "../ft_vox.hpp"

class WorldScaleValidator : public IValidator
{
  private:
	static int validate_chunk(int32_t chunk_x, int32_t chunk_z);

  public:
	WorldScaleValidator();
	WorldScaleValidator(const WorldScaleValidator &other);
	~WorldScaleValidator();
	WorldScaleValidator &operator=(const WorldScaleValidator &other);

	virtual int validate() const override;
};

#endif
