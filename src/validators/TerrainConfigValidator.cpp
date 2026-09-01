#include "../../src/validators/TerrainConfigValidator.hpp"

TerrainConfigValidator::TerrainConfigValidator()
{
}

TerrainConfigValidator::TerrainConfigValidator(const TerrainConfigValidator &other)
	: IValidator(other)
{
	(void)other;
}

TerrainConfigValidator::~TerrainConfigValidator()
{
}

TerrainConfigValidator &TerrainConfigValidator::operator=(const TerrainConfigValidator &other)
{
	(void)other;
	return (*this);
}

int TerrainConfigValidator::validate_biome_size_overrides(voxel_generation_config &config) noexcept
{
	if (config.enable_biome_size_control != FT_TRUE
		|| config.biome_size_min != VOXEL_BIOME_ZONE_WIDTH
		|| config.biome_size_max != VOXEL_BIOME_ZONE_WIDTH
		|| config.set_biome_size_range(1024, 2048) != FT_ERR_SUCCESS
		|| config.set_biome_size_range(2048, 1024) == FT_ERR_SUCCESS
		|| config.set_biome_size_range_for_biome(VOXEL_BIOME_MOUNTAINS, 16,
			64) != FT_ERR_SUCCESS
		|| config.set_biome_size_override_enabled(VOXEL_BIOME_MOUNTAINS,
			FT_FALSE) != FT_ERR_SUCCESS
		|| config.set_biome_size_override_enabled(VOXEL_BIOME_MOUNTAINS,
			FT_TRUE) != FT_ERR_SUCCESS)
		return (1);
	config.set_biome_size_override_enabled(VOXEL_BIOME_MOUNTAINS, FT_TRUE);
	return (0);
}

int TerrainConfigValidator::validate_biome_zone_widths(voxel_generation_config &config) noexcept
{
	uint64_t validator_seed;
	int32_t biome_width;
	int32_t mountain_width;

	validator_seed = UINT64_C(0xC0FFEE1234567890);
	biome_width = voxel_get_biome_zone_width(config, validator_seed);
	mountain_width = voxel_get_biome_zone_width_for_biome(config,
			validator_seed, VOXEL_BIOME_MOUNTAINS);
	if (biome_width < 1024 || biome_width > 2048 || mountain_width < 16
		|| mountain_width > 64
		|| config.set_biome_size_control_enabled(FT_FALSE) != FT_ERR_SUCCESS
		|| voxel_get_biome_zone_width(config, validator_seed) != 128
		|| config.set_biome_size_control_enabled(FT_TRUE) != FT_ERR_SUCCESS
		|| voxel_select_biome(config, validator_seed, 0,
			0) != voxel_select_biome(config, validator_seed, 0, 0))
		return (1);
	return (0);
}

void TerrainConfigValidator::configure_single_biome(voxel_generation_config &config) noexcept
{
	voxel_biome_profile profile;

	config.set_biome_count(1U);
	config.set_sea_level(0);
	config.set_water_chance_percent(0U);
	profile.surface_height = 32;
	profile.height_variation = 0;
	profile.topsoil_depth = 0;
	config.biomes[0].set_profile(profile);
	config.biomes[0].set_block_palette(VOXEL_GENERATOR_SAND_BLOCK,
		VOXEL_GENERATOR_SAND_BLOCK, VOXEL_GENERATOR_STONE_BLOCK);
	config.biomes[0].set_decoration_policy(FT_FALSE, FT_FALSE, 6U, 18U);
}

int TerrainConfigValidator::verify_generated_block(const voxel_generation_config &config) noexcept
{
	game_voxel_chunk chunk;
	uint32_t block_id;

	if (chunk.initialize() != FT_ERR_SUCCESS)
		return (1);
	if (voxel_generate_chunk(chunk, 0, 0, "config-validator",
			config) != FT_ERR_SUCCESS)
	{
		(void)chunk.destroy();
		return (1);
	}
	if (chunk.read_block(0, 32, 0, &block_id) != FT_ERR_SUCCESS
		|| block_id != VOXEL_GENERATOR_SAND_BLOCK)
	{
		(void)chunk.destroy();
		return (1);
	}
	(void)chunk.destroy();
	return (0);
}

int TerrainConfigValidator::validate() const
{
	voxel_generation_config config;

	voxel_default_generation_config(config);
	if (TerrainConfigValidator::validate_biome_size_overrides(config) != 0
		|| TerrainConfigValidator::validate_biome_zone_widths(config) != 0)
		return (1);
	TerrainConfigValidator::configure_single_biome(config);
	return (TerrainConfigValidator::verify_generated_block(config));
}
