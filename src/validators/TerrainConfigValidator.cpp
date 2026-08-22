#include "../../src/validators/TerrainConfigValidator.hpp"

TerrainConfigValidator::TerrainConfigValidator() {}
TerrainConfigValidator::TerrainConfigValidator(const TerrainConfigValidator &other)
{
    (void)other;
}
TerrainConfigValidator::~TerrainConfigValidator() {}
TerrainConfigValidator &TerrainConfigValidator::operator=(const TerrainConfigValidator &other)
{
    (void)other;
    return (*this);
}

int TerrainConfigValidator::validate() const
{
    game_voxel_chunk chunk;
    terrain_generation_config config;

    terrain_default_generation_config(config);
    uint32_t block_id;

    if (config.biome_size_min != TERRAIN_BIOME_ZONE_WIDTH
        || config.biome_size_max != TERRAIN_BIOME_ZONE_WIDTH
        || config.set_biome_size_range(1024, 2048) != FT_ERR_SUCCESS
        || config.set_biome_size_range(2048, 1024) == FT_ERR_SUCCESS)
        return (1);
    uint64_t validator_seed = UINT64_C(0xC0FFEE1234567890);
    int32_t biome_width = terrain_get_biome_zone_width(config,
        validator_seed);
    if (biome_width < 1024 || biome_width > 2048
        || terrain_select_biome(config,
            validator_seed, 0, 0)
            != terrain_select_biome(config,
                validator_seed, biome_width - 1, 0))
        return (1);

    config.set_biome_count(1U);
    config.set_sea_level(0);
    config.set_water_chance_percent(0U);
    terrain_biome_profile profile;
    profile.surface_height = 32;
    profile.height_variation = 0;
    profile.topsoil_depth = 0;
    config.biomes[0].set_profile(profile);
    config.biomes[0].set_block_palette(TERRAIN_GENERATOR_SAND_BLOCK,
        TERRAIN_GENERATOR_SAND_BLOCK, TERRAIN_GENERATOR_STONE_BLOCK);
    config.biomes[0].set_decoration_policy(FT_FALSE, FT_FALSE, 6U, 18U);
    if (chunk.initialize() != FT_ERR_SUCCESS)
        return (1);
    if (terrain_generate_chunk(chunk, 0, 0, "config-validator", config)
        != FT_ERR_SUCCESS)
    {
        (void)chunk.destroy();
        return (1);
    }
    if (chunk.read_block(0, 32, 0, &block_id) != FT_ERR_SUCCESS
        || block_id != TERRAIN_GENERATOR_SAND_BLOCK)
    {
        (void)chunk.destroy();
        return (1);
    }
    (void)chunk.destroy();
    return (0);
}
