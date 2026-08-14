#include "../../src/frame/RendererColor.hpp"

RendererColor::RendererColor()
{
}
RendererColor::RendererColor(const RendererColor &other)
{
    (void)other;
}
RendererColor::~RendererColor()
{
}
RendererColor &RendererColor::operator=(const RendererColor &other)
{
    (void)other;
    return *this;
}

uint32_t RendererColor::rgba(uint8_t red, uint8_t green, uint8_t blue)
{
    return (static_cast<uint32_t>(red) << 16U) | (static_cast<uint32_t>(green) << 8U) |
           static_cast<uint32_t>(blue);
}

uint8_t RendererColor::shade_channel(uint8_t channel, double shade)
{
    int32_t value = static_cast<int32_t>(static_cast<double>(channel) * shade);
    if (value < 0)
        value = 0;
    if (value > 255)
        value = 255;
    return static_cast<uint8_t>(value);
}

void RendererColor::base_color_for_block(uint32_t block_id, uint8_t &r, uint8_t &g, uint8_t &b)
{
    struct BlockColor
    {
        uint32_t id;
        uint8_t r, g, b;
    };
    static const BlockColor TABLE[] = {
        {TERRAIN_GENERATOR_GRASS_BLOCK, 91U, 184U, 141U},
        {TERRAIN_GENERATOR_DIRT_BLOCK, 169U, 91U, 96U},
        {TERRAIN_GENERATOR_STONE_BLOCK, 72U, 91U, 122U},
        {TERRAIN_GENERATOR_SHRUB_BLOCK, 190U, 151U, 74U},
        {TERRAIN_GENERATOR_OAK_LOG_BLOCK, 151U, 86U, 98U},
        {TERRAIN_GENERATOR_OAK_LEAVES_BLOCK, 66U, 151U, 132U},
        {TERRAIN_GENERATOR_CACTUS_BLOCK, 67U, 151U, 135U},
        {TERRAIN_GENERATOR_WATER_BLOCK, 30U, 100U, 200U},
        {TERRAIN_GENERATOR_BEDROCK_BLOCK, 99U, 83U, 112U},
        {TERRAIN_GENERATOR_SAND_BLOCK, 187U, 160U, 110U},
        {TERRAIN_GENERATOR_SNOW_BLOCK, 158U, 195U, 205U},
        {TERRAIN_GENERATOR_PERMAFROST_BLOCK, 74U, 58U, 105U},
        {TERRAIN_GENERATOR_CANYON_ROCK_BLOCK, 118U, 76U, 71U},
        {TERRAIN_GENERATOR_SLATE_BLOCK, 39U, 89U, 92U},
        {TERRAIN_GENERATOR_MOSS_ROCK_BLOCK, 57U, 141U, 165U},
        {TERRAIN_GENERATOR_COAL_ORE_BLOCK, 95U, 96U, 100U},
        {TERRAIN_GENERATOR_IRON_ORE_BLOCK, 150U, 121U, 101U},
        {TERRAIN_GENERATOR_GOLD_ORE_BLOCK, 181U, 151U, 91U},
        {TERRAIN_GENERATOR_GRANITE_BLOCK, 151U, 111U, 101U},
        {TERRAIN_GENERATOR_ANDESITE_BLOCK, 131U, 131U, 131U},
        {TERRAIN_GENERATOR_DIORITE_BLOCK, 191U, 191U, 191U},
        {TERRAIN_GENERATOR_GRAVEL_BLOCK, 121U, 116U, 111U},
        {TERRAIN_GENERATOR_CLAY_BLOCK, 141U, 146U, 156U},
        {TERRAIN_GENERATOR_OBSIDIAN_BLOCK, 36U, 26U, 46U},
        {TERRAIN_GENERATOR_MOSSY_STONE_BLOCK, 95U, 121U, 96U},
        {TERRAIN_GENERATOR_CRACKED_STONE_BLOCK, 101U, 96U, 101U},
        {TERRAIN_GENERATOR_LIMESTONE_BLOCK, 201U, 191U, 166U},
        {TERRAIN_GENERATOR_BASALT_BLOCK, 71U, 76U, 86U},
        {TERRAIN_GENERATOR_DIAMOND_ORE_BLOCK, 111U, 191U, 201U},
        {TERRAIN_GENERATOR_EMERALD_ORE_BLOCK, 91U, 181U, 121U},
        {TERRAIN_GENERATOR_COPPER_ORE_BLOCK, 181U, 121U, 81U},
        {TERRAIN_GENERATOR_PINE_LOG_BLOCK, 96U, 71U, 56U},
        {TERRAIN_GENERATOR_PINE_LEAVES_BLOCK, 41U, 96U, 61U},
        {TERRAIN_GENERATOR_BIRCH_LOG_BLOCK, 211U, 201U, 176U},
        {TERRAIN_GENERATOR_BIRCH_LEAVES_BLOCK, 141U, 181U, 91U},
        {TERRAIN_GENERATOR_ICE_BLOCK, 151U, 201U, 231U},
        {TERRAIN_GENERATOR_PACKED_ICE_BLOCK, 111U, 171U, 221U},
        {TERRAIN_GENERATOR_RED_FLOWER_BLOCK, 181U, 61U, 71U},
        {TERRAIN_GENERATOR_YELLOW_FLOWER_BLOCK, 211U, 191U, 71U},
        {TERRAIN_GENERATOR_TALL_GRASS_BLOCK, 101U, 171U, 81U},
        {TERRAIN_GENERATOR_FERN_BLOCK, 71U, 141U, 81U},
        {TERRAIN_GENERATOR_DEAD_BUSH_BLOCK, 141U, 111U, 61U},
        {TERRAIN_GENERATOR_RED_MUSHROOM_BLOCK, 171U, 61U, 56U},
        {TERRAIN_GENERATOR_BROWN_MUSHROOM_BLOCK, 121U, 91U, 71U},
        {TERRAIN_GENERATOR_MUSHROOM_STEM_BLOCK, 211U, 201U, 181U},
        {TERRAIN_GENERATOR_LILY_PAD_BLOCK, 61U, 141U, 71U},
        {TERRAIN_GENERATOR_SEAGRASS_BLOCK, 51U, 131U, 101U},
        {TERRAIN_GENERATOR_COARSE_DIRT_BLOCK, 121U, 86U, 66U},
        {TERRAIN_GENERATOR_PODZOL_BLOCK, 91U, 66U, 61U},
        {TERRAIN_GENERATOR_MUD_BLOCK, 71U, 56U, 46U},
        {TERRAIN_GENERATOR_FROZEN_STONE_BLOCK, 141U, 161U, 176U},
        {TERRAIN_GENERATOR_CHALK_BLOCK, 221U, 216U, 206U},
        {TERRAIN_GENERATOR_RED_SAND_BLOCK, 191U, 111U, 71U},
        {TERRAIN_GENERATOR_TERRACOTTA_BLOCK, 171U, 91U, 61U},
        {TERRAIN_GENERATOR_SALT_BLOCK, 231U, 226U, 221U},
        {TERRAIN_GENERATOR_VOLCANIC_ROCK_BLOCK, 61U, 36U, 36U},
        {TERRAIN_GENERATOR_QUARTZ_BLOCK, 226U, 221U, 216U},
        {TERRAIN_GENERATOR_AMETHYST_BLOCK, 141U, 91U, 181U},
        {TERRAIN_GENERATOR_PACKED_SNOW_BLOCK, 201U, 216U, 226U},
        {TERRAIN_GENERATOR_WET_SAND_BLOCK, 161U, 141U, 96U},
        {TERRAIN_GENERATOR_AMBER_BLOCK, 201U, 141U, 51U},
        {TERRAIN_GENERATOR_FROST_CRYSTAL_BLOCK, 181U, 221U, 226U},
        {TERRAIN_GENERATOR_SHIMMER_STONE_BLOCK, 171U, 181U, 191U},
    };
    r = 82U;
    g = 72U;
    b = 105U;
    for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); ++i)
    {
        if (TABLE[i].id == block_id)
        {
            r = TABLE[i].r;
            g = TABLE[i].g;
            b = TABLE[i].b;
            return;
        }
    }
}

uint32_t RendererColor::block_color(uint32_t block_id, uint8_t face)
{
    uint8_t r, g, b;
    base_color_for_block(block_id, r, g, b);
    double shade = face_shade(face);
    return rgba(shade_channel(r, shade), shade_channel(g, shade), shade_channel(b, shade));
}

uint32_t RendererColor::shade_color(uint32_t color, double shade)
{
    return rgba(shade_channel(static_cast<uint8_t>((color >> 16U) & 0xFFU), shade),
                shade_channel(static_cast<uint8_t>((color >> 8U) & 0xFFU), shade),
                shade_channel(static_cast<uint8_t>(color & 0xFFU), shade));
}

double RendererColor::face_shade(uint8_t face)
{
    if (face == CHUNK_MESH_FACE_UP)
        return 1.0;
    if (face == CHUNK_MESH_FACE_NORTH || face == CHUNK_MESH_FACE_WEST)
        return 0.58;
    if (face == CHUNK_MESH_FACE_EAST)
        return 0.82;
    return 0.72;
}
