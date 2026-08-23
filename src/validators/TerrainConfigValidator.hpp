#ifndef TERRAIN_CONFIG_VALIDATOR_HPP
# define TERRAIN_CONFIG_VALIDATOR_HPP

#include "../../src/ft_vox.hpp"
#include "../../Libft/Modules/Voxel/terrain_api.hpp"
#include "IValidator.hpp"

class TerrainConfigValidator : public IValidator
{
  public:
    TerrainConfigValidator();
    TerrainConfigValidator(const TerrainConfigValidator &other);
    ~TerrainConfigValidator();
    TerrainConfigValidator &operator=(const TerrainConfigValidator &other);
    int validate() const override;
};

#endif
