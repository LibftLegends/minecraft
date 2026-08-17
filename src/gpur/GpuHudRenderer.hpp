#ifndef GPU_HUD_RENDERER_HPP
# define GPU_HUD_RENDERER_HPP

#include "../../Libft/Modules/GPGR/ft_gpu_shader.hpp"
#include "../../Libft/Modules/GPGR/gpgr_gl_funcs.hpp"
#include "../ft_vox.hpp"

class GpuHudRenderer
{
    ft_gpu_shader _overlay_shader;
    ft_gpu_shader _overlay_tex_shader;

    GLuint  _fullscreen_vao;
    GLuint  _crosshair_vao;
    GLuint  _crosshair_vbo;

    GLuint  _overlay_tex;
    int     _overlay_w;
    int     _overlay_h;
    std::vector<uint8_t> _overlay_rgba;

    GLuint  _menu_tex;
    int     _menu_tex_w;
    int     _menu_tex_h;
    std::vector<uint8_t> _menu_rgba;

    GLint   _u_overlay_color;
    GLint   _u_overlay_sampler;
    GLint   _u_overlay_ndc_br;

    void upload_tex(GLuint &tex, int &stored_w, int &stored_h,
        std::vector<uint8_t> &rgba_buf,
        const uint32_t *pixels, int w, int h,
        bool opaque_alpha) noexcept;
    void draw_tex_quad(GLuint tex, float ndc_br_x, float ndc_br_y) noexcept;

    public:
        GpuHudRenderer();
        GpuHudRenderer(const GpuHudRenderer &other);
        ~GpuHudRenderer();
        GpuHudRenderer &operator=(const GpuHudRenderer &other);

        bool initialize(const std::string &shader_dir) noexcept;
        void destroy() noexcept;

        void draw_crosshair(int width, int height) const noexcept;

        void upload_overlay(const uint32_t *pixels, int w, int h) noexcept;
        void draw_overlay(int width, int height) noexcept;

        void upload_menu(const uint32_t *pixels, int w, int h) noexcept;
        void draw_menu() noexcept;

        size_t gpu_bytes() const noexcept;
};

#endif
