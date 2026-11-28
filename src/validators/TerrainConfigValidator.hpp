#ifndef VOXEL_CONFIG_VALIDATOR_HPP
# define VOXEL_CONFIG_VALIDATOR_HPP

# include "../../src/validators/IValidator.hpp"
# include "../ft_vox.hpp"

class TerrainConfigValidator : public IValidator
{
  private:
	static int validate_biome_size_overrides(voxel_generation_config &config) noexcept;
	static int validate_biome_zone_widths(voxel_generation_config &config) noexcept;
	static void configure_single_biome(voxel_generation_config &config) noexcept;
	static int verify_generated_block(const voxel_generation_config &config) noexcept;

  public:
	TerrainConfigValidator();
	TerrainConfigValidator(const TerrainConfigValidator &other);
	~TerrainConfigValidator();
	TerrainConfigValidator &operator=(const TerrainConfigValidator &other);

	int validate() const override;
};

#endif
