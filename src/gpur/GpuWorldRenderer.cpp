#include "../../src/gpur/GpuWorldRenderer.hpp"
#include "../../src/assets/TextureAtlas.hpp"

GpuWorldRenderer::GpuWorldRenderer()
    : _sky_vao(0), _crosshair_vao(0), _crosshair_vbo(0), _width(0), _height(0), _u_mvp(-1),
      _u_chunk_offset(-1),
      _u_atlas(-1), _u_atlas_loaded(-1), _u_tile_uvs(-1), _u_fallback(-1), _u_sky_size(-1),
      _u_crosshair_color(-1)
{
    std::memset(_tile_uvs, 0, sizeof(_tile_uvs));
    std::memset(_fallback_colors, 0, sizeof(_fallback_colors));
}

GpuWorldRenderer::GpuWorldRenderer(const GpuWorldRenderer &other)
    : _sky_vao(0), _crosshair_vao(0), _crosshair_vbo(0), _width(0), _height(0), _u_mvp(-1),
      _u_chunk_offset(-1),
      _u_atlas(-1), _u_atlas_loaded(-1), _u_tile_uvs(-1), _u_fallback(-1), _u_sky_size(-1),
      _u_crosshair_color(-1)
{
    (void)other;
    std::memset(_tile_uvs, 0, sizeof(_tile_uvs));
    std::memset(_fallback_colors, 0, sizeof(_fallback_colors));
}

GpuWorldRenderer::~GpuWorldRenderer()
{
    destroy();
}

GpuWorldRenderer &GpuWorldRenderer::operator=(const GpuWorldRenderer &other)
{
    (void)other;
    return *this;
}

bool GpuWorldRenderer::compile_shader_from_file(ft_gpu_shader &shader, const char *vert_path,
                                                const char *frag_path)
{
    std::string vert = GlslLoader::load(vert_path);
    std::string frag = GlslLoader::load(frag_path);
    if (vert.empty() || frag.empty())
        return false;
    return shader.initialize(vert.c_str(), frag.c_str()) == FT_ERR_SUCCESS;
}

bool GpuWorldRenderer::compile_shaders(const std::string &d)
{
    const std::string world_vert = d + "world.vert.glsl";
    const std::string world_frag = d + "world.frag.glsl";
    const std::string sky_vert = d + "sky.vert.glsl";
    const std::string sky_frag = d + "sky.frag.glsl";
    const std::string cross_vert = d + "overlay.vert.glsl";
    const std::string cross_frag = d + "overlay.frag.glsl";

    if (!compile_shader_from_file(_world_shader, world_vert.c_str(), world_frag.c_str()))
        return false;
    if (!compile_shader_from_file(_sky_shader, sky_vert.c_str(), sky_frag.c_str()))
        return false;
    if (!compile_shader_from_file(_crosshair_shader, cross_vert.c_str(), cross_frag.c_str()))
        return false;
    return true;
}

void GpuWorldRenderer::cache_uniforms()
{
    _u_mvp = _world_shader.uniform("u_mvp");
    _u_chunk_offset = _world_shader.uniform("u_chunk_offset");
    _u_atlas = _world_shader.uniform("u_atlas");
    _u_atlas_loaded = _world_shader.uniform("u_atlas_loaded");
    _u_tile_uvs = _world_shader.uniform("u_tile_uvs");
    _u_fallback = _world_shader.uniform("u_fallback_colors");
    _u_sky_size = _sky_shader.uniform("u_sky_size");
    _u_crosshair_color = _crosshair_shader.uniform("u_overlay_color");
}

void GpuWorldRenderer::upload_atlas_uniforms()
{
    _world_shader.use();
    glUniform1i(_u_atlas, 0);
    glUniform1i(_u_atlas_loaded, _atlas.is_loaded() ? 1 : 0);
    glUniform4fv(_u_tile_uvs, 384, _tile_uvs);
    glUniform3fv(_u_fallback, 64, _fallback_colors);
}

bool GpuWorldRenderer::initialize(int width, int height, GLuint sky_vao, GLuint crosshair_vao,
                                  GLuint crosshair_vbo, const std::string &shader_dir)
{
    destroy();
    _sky_vao = sky_vao;
    _crosshair_vao = crosshair_vao;
    _crosshair_vbo = crosshair_vbo;
    if (!compile_shaders(shader_dir))
        return false;
    cache_uniforms();
    if (!_batch.initialize(0))
        return false;
    const TextureAtlas &cpu_atlas = TextureAtlas::instance();
    _atlas.upload(cpu_atlas);
    _atlas.fill_tile_uvs(cpu_atlas, _tile_uvs);
    _atlas.fill_fallback_colors(_fallback_colors);
    upload_atlas_uniforms();
    _width = width;
    _height = height;
    update_crosshair_geometry();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    return true;
}

void GpuWorldRenderer::destroy()
{
    _atlas.destroy();
    _batch.destroy();
    (void)_world_shader.destroy();
    (void)_sky_shader.destroy();
    (void)_crosshair_shader.destroy();
}

void GpuWorldRenderer::resize(int width, int height)
{
    if (_width == width && _height == height)
        return;
    _width = width;
    _height = height;
    update_crosshair_geometry();
}

void GpuWorldRenderer::update_crosshair_geometry()
{
    if (_width <= 0 || _height <= 0)
        return;
    const float sx = 8.0f / static_cast<float>(_width);
    const float sy = 8.0f / static_cast<float>(_height);
    const float verts[] = {-sx, 0.0f, sx, 0.0f, 0.0f, -sy, 0.0f, sy};
    glBindVertexArray(_crosshair_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _crosshair_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(verts)), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(2 * sizeof(float)),
                          nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void GpuWorldRenderer::draw_sky() const
{
    _sky_shader.use();
    glUniform2f(_u_sky_size, static_cast<float>(_width), static_cast<float>(_height));
    glBindVertexArray(_sky_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void GpuWorldRenderer::draw_crosshair() const
{
    glBindVertexArray(_crosshair_vao);
    const float color[4] = {0.96f, 0.96f, 0.92f, 1.0f};
    _crosshair_shader.use();
    glUniform4fv(_u_crosshair_color, 1, color);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_LINES, 0, 4);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

void GpuWorldRenderer::render(const Camera &camera, const World &world)
{
    glViewport(0, 0, _width, _height);
    glClear(GL_DEPTH_BUFFER_BIT);
    draw_sky();
    float mvp[16];
    float far_z = static_cast<float>(world.active_render_distance) + 64.0f;
    GpuMvpBuilder::build(mvp, camera, _width, _height, 0.08f, far_z);
    _batch.collect(camera, world, _width, _height);
    _world_shader.use();
    _batch.flush_solid(0, _u_mvp, _u_chunk_offset, mvp, _atlas);
    _batch.flush_water(0, _u_mvp, _u_chunk_offset, mvp, _atlas);
    draw_crosshair();
}

size_t GpuWorldRenderer::gpu_bytes() const
{
    size_t b = _batch.gpu_bytes();
    b += static_cast<size_t>(_atlas.is_loaded() ? 384 * 384 * 4 : 0);
    return b;
}
