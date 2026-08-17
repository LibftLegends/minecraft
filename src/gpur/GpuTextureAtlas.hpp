#ifndef GPUR_TEXTURE_ATLAS_HPP
#define GPUR_TEXTURE_ATLAS_HPP

#ifndef GAME_USE_VOXEL_REGION_BACKEND
#define GAME_USE_VOXEL_REGION_BACKEND
#endif
#include "../ft_vox.hpp"
#include "../../src/assets/TextureAtlas.hpp"
#include "../../src/frame/RendererColor.hpp"

class GpuTextureAtlas
{
  public:
    GpuTextureAtlas();
    GpuTextureAtlas(const GpuTextureAtlas &other);
    ~GpuTextureAtlas();
    GpuTextureAtlas &operator=(const GpuTextureAtlas &other);

    bool upload(const TextureAtlas &atlas);
    void bind(int unit) const;
    void destroy();
    bool is_loaded() const;

    void fill_tile_uvs(const TextureAtlas &atlas, float uv_out[384 * 4]) const;
    void fill_fallback_colors(float colors_out[64 * 3]) const;

  private:
    GLuint _tex;
    bool _loaded;

    static const uint32_t BLOCK_IDS[63];
    static const int BLOCK_COUNT;
};

#endif
