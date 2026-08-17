#ifndef GPU_WORLD_RENDERER_HPP
#define GPU_WORLD_RENDERER_HPP

#ifndef GAME_USE_VOXEL_REGION_BACKEND
#define GAME_USE_VOXEL_REGION_BACKEND
#endif
#include "../ft_vox.hpp"
#include "../../src/assets/TextureAtlas.hpp"
#include "../../src/camera/Camera.hpp"
#include "../../src/gpur/GlslLoader.hpp"
#include "../../src/gpur/GpuGeometryBatch.hpp"
#include "../../src/gpur/GpuMvpBuilder.hpp"
#include "../../src/gpur/GpuTextureAtlas.hpp"
#include "../../src/world/World.hpp"

class GpuWorldRenderer
{
  public:
    using MegaVertex = GpuGeometryBatch::Vertex;

    GpuWorldRenderer();
    GpuWorldRenderer(const GpuWorldRenderer &other);
    ~GpuWorldRenderer();
    GpuWorldRenderer &operator=(const GpuWorldRenderer &other);

    bool initialize(int width, int height, GLuint sky_vao, GLuint crosshair_vao,
                    GLuint crosshair_vbo, const std::string &shader_dir);
    void destroy();
    void resize(int width, int height);
    void render(const Camera &camera, const World &world);
    size_t gpu_bytes() const;

  private:
    ft_gpu_shader _world_shader;
    ft_gpu_shader _sky_shader;
    ft_gpu_shader _crosshair_shader;
    GpuTextureAtlas _atlas;
    GpuGeometryBatch _batch;

    GLuint _sky_vao;
    GLuint _crosshair_vao;
    GLuint _crosshair_vbo;
    int _width;
    int _height;

    float _tile_uvs[384 * 4];
    float _fallback_colors[64 * 3];

    GLint _u_mvp;
    GLint _u_chunk_offset;
    GLint _u_atlas;
    GLint _u_atlas_loaded;
    GLint _u_tile_uvs;
    GLint _u_fallback;
    GLint _u_sky_size;
    GLint _u_crosshair_color;

    bool compile_shaders(const std::string &shader_dir);
    void cache_uniforms();
    void upload_atlas_uniforms();
    void update_crosshair_geometry();
    void draw_sky() const;
    void draw_crosshair() const;

    static bool compile_shader_from_file(ft_gpu_shader &shader, const char *vert_path,
                                         const char *frag_path);
};

#endif
