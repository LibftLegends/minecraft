#ifndef GPU_OVERLAY_RENDERER_HPP
# define GPU_OVERLAY_RENDERER_HPP

# include "../../src/gpur/GlslLoader.hpp"
# include "../ft_vox.hpp"

class GpuOverlayRenderer
{
  public:
	/* Matches the uniform array sizes in item_marker.vert.glsl. */
	static constexpr std::size_t MAX_ITEM_MARKERS = 48U;

	struct ItemMarker
	{
		int32_t x;
		int32_t y;
		int32_t radius;
		uint32_t color;
	};

	GpuOverlayRenderer();
	GpuOverlayRenderer(const GpuOverlayRenderer &other);
	~GpuOverlayRenderer();
	GpuOverlayRenderer &operator=(const GpuOverlayRenderer &other);

	bool initialize(GLuint sky_vao, const std::string &shader_dir);
	void destroy();

	void upload_overlay(const uint32_t *pixels, int w, int h);
	void upload_overlay_region(const uint32_t *pixels, int w, int h,
		int x, int y, int region_width, int region_height);
	void draw_overlay(int viewport_w, int viewport_h);
	void draw_item_markers(const ItemMarker *markers, std::size_t count,
		int canvas_w, int canvas_h, int viewport_w, int viewport_h);
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
	ft_gpu_shader _item_marker_shader;

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
	GLint _u_item_marker_bounds = -1;
	GLint _u_item_marker_colors = -1;
	GLint _u_item_marker_canvas = -1;

	static void pixels_to_rgba(const uint32_t *src, int width, int height,
		int stride, std::vector<uint8_t> &dst, bool opaque_non_black);
	static void upload_texture(GLuint &tex, int &tex_w, int &tex_h,
		const uint8_t *rgba, int w, int h);
	void draw_fullscreen_quad();
	void bind_tex_shader(GLuint tex, float ndc_x, float ndc_y);
};

#endif
