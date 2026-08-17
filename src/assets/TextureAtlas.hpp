#ifndef TEXTURE_ATLAS_HPP
#define TEXTURE_ATLAS_HPP

#include "../../src/assets/BlockTextureRegistry.hpp"
#include "../../src/frame/RendererColor.hpp"
#include "../../src/geometry/TriangleTexture.hpp"
#ifndef GAME_USE_VOXEL_REGION_BACKEND
#define GAME_USE_VOXEL_REGION_BACKEND
#endif
#include "../ft_vox.hpp"

class TextureAtlas
{
  public:
    TextureAtlas();
    TextureAtlas(const TextureAtlas &other);
    ~TextureAtlas();
    TextureAtlas &operator=(const TextureAtlas &other);

    static const TextureAtlas &instance();
    static void atlas_tile_for_block(uint32_t block_id, uint8_t face, int32_t *tile_x,
                                     int32_t *tile_y);
    static TriangleTexture prepare_triangle_texture(uint32_t block_id, uint8_t face);
    static uint32_t sample_triangle_texture(const TriangleTexture &texture, double texture_u,
                                            double texture_v);

    int32_t get_width() const;
    int32_t get_height() const;
    int32_t get_tile_width() const;
    int32_t get_tile_height() const;
    bool is_loaded() const;
    uint32_t pixel_at(int32_t x, int32_t y) const;

  private:
    struct BmpHeader
    {
        uint32_t data_offset;
        int32_t width;
        int32_t height;
        int32_t height_signed;
        uint16_t bits_per_pixel;
        uint32_t compression;
    };

    static const int TEXTURE_CACHE_BLOCKS = 64;
    static const int TEXTURE_CACHE_FACES = 6;
    static const int ATLAS_GRID_COLUMNS = 8;
    static const int ATLAS_GRID_ROWS = 8;

    std::vector<uint32_t> pixels;
    int32_t width;
    int32_t height;
    int32_t tile_width;
    int32_t tile_height;
    bool loaded;
    bool attempted_load;

    static uint16_t read_le_u16(const uint8_t *data);
    static uint32_t read_le_u32(const uint8_t *data);
    static int32_t read_le_i32(const uint8_t *data);
    static bool read_bmp_header(FILE *file, BmpHeader &hdr);
    static bool read_bmp_pixels(FILE *file, TextureAtlas *atlas, const BmpHeader &hdr);
    static bool load_bmp_texture_atlas(TextureAtlas *atlas, const char *path);
    static double fraction(double value);
    static uint32_t sample_clamped(const TriangleTexture &tex, double u, double v);
};

#endif
