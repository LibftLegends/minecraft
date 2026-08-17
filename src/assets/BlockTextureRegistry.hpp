#ifndef BLOCK_TEXTURE_REGISTRY_HPP
#define BLOCK_TEXTURE_REGISTRY_HPP

#ifndef GAME_USE_VOXEL_REGION_BACKEND
#define GAME_USE_VOXEL_REGION_BACKEND
#endif
#include "../ft_vox.hpp"

class BlockTextureRegistry
{
  public:
    BlockTextureRegistry();
    BlockTextureRegistry(const BlockTextureRegistry &other);
    ~BlockTextureRegistry();
    BlockTextureRegistry &operator=(const BlockTextureRegistry &other);

    static void tile_for_block(uint32_t block_id, uint8_t face, int *tile_x, int *tile_y);

  private:
    struct Entry
    {
        uint32_t block_id;
        int def_x;
        int tile_y;
        int up_x;
        int down_x;
    };
    static const Entry ENTRIES[];
    static const int ENTRY_COUNT;
    static const int DEFAULT_X;
    static const int DEFAULT_Y;
};

#endif
