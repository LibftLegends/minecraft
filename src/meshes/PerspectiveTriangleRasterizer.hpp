#ifndef PERSPECTIVE_TRIANGLE_RASTERIZER_HPP
# define PERSPECTIVE_TRIANGLE_RASTERIZER_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../../src/assets/TextureAtlas.hpp"
# include "../../src/frame/RendererColor.hpp"
# include "../../src/meshes/TriangleRasterizer.hpp"
# include "../ft_vox.hpp"

class PerspectiveTriangleRasterizer : public TriangleRasterizer
{
  public:
	PerspectiveTriangleRasterizer();
	PerspectiveTriangleRasterizer(const PerspectiveTriangleRasterizer &other);
	~PerspectiveTriangleRasterizer();
	PerspectiveTriangleRasterizer &operator=(const PerspectiveTriangleRasterizer &other);

	void draw_triangle(ft_render_framebuffer &framebuffer,
		std::vector<double> &depth_buffer, const ScreenVertex vertices[3],
		uint32_t block_id, uint8_t face) const override;

  private:
	struct		Interpolants
	{
		double	inv_depth[3];
		double	u_over_d[3];
		double	v_over_d[3];
		double step_x_id, step_y_id;
		double step_x_u, step_y_u;
		double step_x_v, step_y_v;
		double row_id, row_u, row_v;
		double	sx[3], sy[3];
	};

	static double edge(const ScreenVertex &a, const ScreenVertex &b, double x,
		double y);
	static double edge_step_x(const ScreenVertex &a, const ScreenVertex &b);
	static double edge_step_y(const ScreenVertex &a, const ScreenVertex &b);
	static bool compute_bounds(const ScreenVertex vertices[3],
		const ft_render_framebuffer &fb, int32_t &sx, int32_t &ex, int32_t &sy,
		int32_t &ey);
	static void compute_interpolants(const ScreenVertex vertices[3],
		double inv_area, int32_t start_x, int32_t start_y,
		Interpolants &interp);
	static void compute_steps(Interpolants &it, double inv_area);
	static void compute_row_starts(const ScreenVertex vertices[3],
		Interpolants &it, double inv_area, int32_t start_x, int32_t start_y);
	void rasterize(ft_render_framebuffer &framebuffer,
		std::vector<double> &depth_buffer, const ScreenVertex vertices[3],
		int32_t start_x, int32_t end_x, int32_t start_y, int32_t end_y,
		const Interpolants &interp, const TriangleTexture &texture) const;
	static void write_pixel(uint32_t *pixel, double &depth_entry,
		double inv_depth, double u, double v, const TriangleTexture &texture);
};

#endif
