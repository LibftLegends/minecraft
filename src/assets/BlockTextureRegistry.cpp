#include "../../src/assets/BlockTextureRegistry.hpp"

BlockTextureRegistry::BlockTextureRegistry()
{
}
BlockTextureRegistry::BlockTextureRegistry(const BlockTextureRegistry &o)
{
    (void)o;
}
BlockTextureRegistry::~BlockTextureRegistry()
{
}
BlockTextureRegistry &BlockTextureRegistry::operator=(const BlockTextureRegistry &o)
{
    (void)o;
    return *this;
}

const int BlockTextureRegistry::DEFAULT_X = 2;
const int BlockTextureRegistry::DEFAULT_Y = 2;

const BlockTextureRegistry::Entry BlockTextureRegistry::ENTRIES[] = {
    {TERRAIN_GENERATOR_GRASS_BLOCK, 1, 0, 0, 2},
    {TERRAIN_GENERATOR_DIRT_BLOCK, 2, 0, -1, -1},
    {TERRAIN_GENERATOR_STONE_BLOCK, 3, 0, -1, -1},
    {TERRAIN_GENERATOR_COARSE_DIRT_BLOCK, 4, 0, -1, -1},
    {TERRAIN_GENERATOR_PODZOL_BLOCK, 5, 0, -1, -1},
    {TERRAIN_GENERATOR_MUD_BLOCK, 6, 0, -1, -1},
    {TERRAIN_GENERATOR_GRAVEL_BLOCK, 7, 0, -1, -1},

    {TERRAIN_GENERATOR_WATER_BLOCK, 0, 1, -1, -1},
    {TERRAIN_GENERATOR_SNOW_BLOCK, 1, 1, -1, -1},
    {TERRAIN_GENERATOR_OAK_LOG_BLOCK, 2, 1, -1, -1},
    {TERRAIN_GENERATOR_OAK_LEAVES_BLOCK, 3, 1, -1, -1},
    {TERRAIN_GENERATOR_GRANITE_BLOCK, 4, 1, -1, -1},
    {TERRAIN_GENERATOR_ANDESITE_BLOCK, 5, 1, -1, -1},
    {TERRAIN_GENERATOR_DIORITE_BLOCK, 6, 1, -1, -1},
    {TERRAIN_GENERATOR_CLAY_BLOCK, 7, 1, -1, -1},

    {TERRAIN_GENERATOR_CACTUS_BLOCK, 0, 2, -1, -1},
    {TERRAIN_GENERATOR_SHRUB_BLOCK, 1, 2, -1, -1},
    {TERRAIN_GENERATOR_BEDROCK_BLOCK, 2, 2, -1, -1},
    {TERRAIN_GENERATOR_PERMAFROST_BLOCK, 3, 2, -1, -1},
    {TERRAIN_GENERATOR_OBSIDIAN_BLOCK, 4, 2, -1, -1},
    {TERRAIN_GENERATOR_MOSSY_STONE_BLOCK, 5, 2, -1, -1},
    {TERRAIN_GENERATOR_CRACKED_STONE_BLOCK, 6, 2, -1, -1},
    {TERRAIN_GENERATOR_LIMESTONE_BLOCK, 7, 2, -1, -1},

    {TERRAIN_GENERATOR_MOSS_ROCK_BLOCK, 0, 3, -1, -1},
    {TERRAIN_GENERATOR_SAND_BLOCK, 1, 3, -1, -1},
    {TERRAIN_GENERATOR_CANYON_ROCK_BLOCK, 2, 3, -1, -1},
    {TERRAIN_GENERATOR_SLATE_BLOCK, 3, 3, -1, -1},
    {TERRAIN_GENERATOR_BASALT_BLOCK, 4, 3, -1, -1},
    {TERRAIN_GENERATOR_VOLCANIC_ROCK_BLOCK, 5, 3, -1, -1},
    {TERRAIN_GENERATOR_CHALK_BLOCK, 6, 3, -1, -1},
    {TERRAIN_GENERATOR_FROZEN_STONE_BLOCK, 7, 3, -1, -1},

    {TERRAIN_GENERATOR_COAL_ORE_BLOCK, 0, 4, -1, -1},
    {TERRAIN_GENERATOR_IRON_ORE_BLOCK, 1, 4, -1, -1},
    {TERRAIN_GENERATOR_GOLD_ORE_BLOCK, 2, 4, -1, -1},
    {TERRAIN_GENERATOR_DIAMOND_ORE_BLOCK, 3, 4, -1, -1},
    {TERRAIN_GENERATOR_EMERALD_ORE_BLOCK, 4, 4, -1, -1},
    {TERRAIN_GENERATOR_COPPER_ORE_BLOCK, 5, 4, -1, -1},
    {TERRAIN_GENERATOR_AMETHYST_BLOCK, 6, 4, -1, -1},
    {TERRAIN_GENERATOR_QUARTZ_BLOCK, 7, 4, -1, -1},

    {TERRAIN_GENERATOR_PINE_LOG_BLOCK, 0, 5, -1, -1},
    {TERRAIN_GENERATOR_PINE_LEAVES_BLOCK, 1, 5, -1, -1},
    {TERRAIN_GENERATOR_BIRCH_LOG_BLOCK, 2, 5, -1, -1},
    {TERRAIN_GENERATOR_BIRCH_LEAVES_BLOCK, 3, 5, -1, -1},
    {TERRAIN_GENERATOR_ICE_BLOCK, 4, 5, -1, -1},
    {TERRAIN_GENERATOR_PACKED_ICE_BLOCK, 5, 5, -1, -1},
    {TERRAIN_GENERATOR_RED_SAND_BLOCK, 6, 5, -1, -1},
    {TERRAIN_GENERATOR_TERRACOTTA_BLOCK, 7, 5, -1, -1},

    {TERRAIN_GENERATOR_RED_FLOWER_BLOCK, 0, 6, -1, -1},
    {TERRAIN_GENERATOR_YELLOW_FLOWER_BLOCK, 1, 6, -1, -1},
    {TERRAIN_GENERATOR_TALL_GRASS_BLOCK, 2, 6, -1, -1},
    {TERRAIN_GENERATOR_FERN_BLOCK, 3, 6, -1, -1},
    {TERRAIN_GENERATOR_DEAD_BUSH_BLOCK, 4, 6, -1, -1},
    {TERRAIN_GENERATOR_RED_MUSHROOM_BLOCK, 5, 6, -1, -1},
    {TERRAIN_GENERATOR_BROWN_MUSHROOM_BLOCK, 6, 6, -1, -1},
    {TERRAIN_GENERATOR_MUSHROOM_STEM_BLOCK, 7, 6, -1, -1},

    {TERRAIN_GENERATOR_LILY_PAD_BLOCK, 0, 7, -1, -1},
    {TERRAIN_GENERATOR_SEAGRASS_BLOCK, 1, 7, -1, -1},
    {TERRAIN_GENERATOR_SALT_BLOCK, 2, 7, -1, -1},
    {TERRAIN_GENERATOR_AMBER_BLOCK, 3, 7, -1, -1},
    {TERRAIN_GENERATOR_PACKED_SNOW_BLOCK, 4, 7, -1, -1},
    {TERRAIN_GENERATOR_WET_SAND_BLOCK, 5, 7, -1, -1},
    {TERRAIN_GENERATOR_FROST_CRYSTAL_BLOCK, 6, 7, -1, -1},
    {TERRAIN_GENERATOR_SHIMMER_STONE_BLOCK, 7, 7, -1, -1},
};

const int BlockTextureRegistry::ENTRY_COUNT =
    static_cast<int>(sizeof(ENTRIES) / sizeof(ENTRIES[0]));

void BlockTextureRegistry::tile_for_block(uint32_t block_id, uint8_t face, int *tile_x, int *tile_y)
{
    if (!tile_x || !tile_y)
        return;
    *tile_x = DEFAULT_X;
    *tile_y = DEFAULT_Y;
    for (int i = 0; i < ENTRY_COUNT; ++i)
    {
        if (ENTRIES[i].block_id != block_id)
            continue;
        *tile_y = ENTRIES[i].tile_y;
        if (face == CHUNK_MESH_FACE_UP && ENTRIES[i].up_x >= 0)
            *tile_x = ENTRIES[i].up_x;
        else if (face == CHUNK_MESH_FACE_DOWN && ENTRIES[i].down_x >= 0)
            *tile_x = ENTRIES[i].down_x;
        else
            *tile_x = ENTRIES[i].def_x;
        return;
    }
}
