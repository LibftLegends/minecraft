#ifndef GPU_OVERLAY_RENDERER_HPP
# define GPU_OVERLAY_RENDERER_HPP

# include "../../src/gpur/GlslLoader.hpp"
# include "../ft_vox.hpp"

class GpuOverlayRenderer
{
  public:
	GpuOverlayRenderer();
	GpuOverlayRenderer(const GpuOverlayRenderer &other);
	~GpuOverlayRenderer();
	GpuOverlayRenderer &operator=(const GpuOverlayRenderer &other);

	bool initialize(GLuint sky_vao, const std::string &shader_dir);
	void destroy();

	void upload_overlay(const uint32_t *pixels, int w, int h);
	void draw_overlay(int viewport_w, int viewport_h);
	void upload_menu(const uint32_t *pixels, int w, int h);
	void draw_menu();
	void draw_tint(float r, float g, float b, float a);
	void clear_screen();

	GLint uniform_overlay_color() const;
	GLint uniform_overlay_sampler() const;
	GLint uniform_overlay_ndc_br() const;
	GLint uniform_tint_color() const;
	const ft_gpu_shader &overlay_shader() const;
	const ft_gpu_shader &overlay_tex_shader() const;
	const ft_gpu_shader &tint_shader() const;

  private:
	ft_gpu_shader _overlay_shader;
	ft_gpu_shader _overlay_tex_shader;
	ft_gpu_shader _tint_shader;

	GLuint _overlay_tex;
	int _overlay_w;
	int _overlay_h;
	GLuint _menu_tex;
	int _menu_tex_w;
	int _menu_tex_h;
	GLuint _sky_vao;

	std::vector<uint8_t> _overlay_rgba;
	std::vector<uint8_t> _menu_rgba;

	GLint _u_overlay_color;
	GLint _u_overlay_sampler;
	GLint _u_overlay_ndc_br;
	GLint _u_tint_color;

	static void pixels_to_rgba(const uint32_t *src, size_t count,
		std::vector<uint8_t> &dst, bool opaque_non_black);
	static void upload_texture(GLuint &tex, int &tex_w, int &tex_h,
		const uint8_t *rgba, int w, int h);
	void draw_fullscreen_quad();
	void bind_tex_shader(GLuint tex, float ndc_x, float ndc_y);
};

#endif
