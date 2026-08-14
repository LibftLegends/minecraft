#include "../../src/gpur/GpuSkyRenderer.hpp"
#include "../../src/gpur/GlslLoader.hpp"

GpuSkyRenderer::GpuSkyRenderer()
    : _fullscreen_vao(0), _u_sky_size(-1), _u_tint_color(-1)
{}
GpuSkyRenderer::GpuSkyRenderer(const GpuSkyRenderer &other) { (void)other; }
GpuSkyRenderer::~GpuSkyRenderer() { destroy(); }
GpuSkyRenderer &GpuSkyRenderer::operator=(const GpuSkyRenderer &other)
{ (void)other; return *this; }

bool GpuSkyRenderer::initialize(const std::string &shader_dir) noexcept
{
    std::string sky_vert = GlslLoader::load((shader_dir + "sky.vert.glsl").c_str());
    std::string sky_frag = GlslLoader::load((shader_dir + "sky.frag.glsl").c_str());
    std::string tint_frag = GlslLoader::load((shader_dir + "tint.frag.glsl").c_str());
    if (sky_vert.empty() || sky_frag.empty() || tint_frag.empty())
        return false;
    if (_sky_shader.initialize(sky_vert.c_str(), sky_frag.c_str()) != 0)
        return false;
    if (_tint_shader.initialize(sky_vert.c_str(), tint_frag.c_str()) != 0)
        return false;
    _u_sky_size   = _sky_shader.uniform("u_sky_size");
    _u_tint_color = _tint_shader.uniform("u_tint_color");
    glGenVertexArrays(1, &_fullscreen_vao);
    return true;
}

void GpuSkyRenderer::destroy() noexcept
{
    if (_fullscreen_vao != 0)
    { glDeleteVertexArrays(1, &_fullscreen_vao); _fullscreen_vao = 0; }
    (void)_sky_shader.destroy();
    (void)_tint_shader.destroy();
}

void GpuSkyRenderer::draw_fullscreen() const noexcept
{
    glBindVertexArray(_fullscreen_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void GpuSkyRenderer::draw_sky(int width, int height) const noexcept
{
    _sky_shader.use();
    glUniform2f(_u_sky_size,
        static_cast<float>(width), static_cast<float>(height));
    draw_fullscreen();
}

void GpuSkyRenderer::draw_tint(float r, float g, float b, float a) const noexcept
{
    _tint_shader.use();
    glUniform4f(_u_tint_color, r, g, b, a);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    draw_fullscreen();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}
