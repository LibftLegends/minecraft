#include "../../src/meshes/PerspectiveTriangleRasterizer.hpp"

PerspectiveTriangleRasterizer::PerspectiveTriangleRasterizer()
{
}
PerspectiveTriangleRasterizer::PerspectiveTriangleRasterizer(
	const PerspectiveTriangleRasterizer &other) : TriangleRasterizer(other)
{
	(void)other;
}
PerspectiveTriangleRasterizer::~PerspectiveTriangleRasterizer()
{
}
PerspectiveTriangleRasterizer &PerspectiveTriangleRasterizer::operator=(
	const PerspectiveTriangleRasterizer &other)
{
	(void)other;
	return (*this);
}

double PerspectiveTriangleRasterizer::edge(const ScreenVertex &a,
	const ScreenVertex &b, double x, double y)
{
	return ((x - a.x) * (b.y - a.y)) - ((y - a.y) * (b.x - a.x));
}

double PerspectiveTriangleRasterizer::edge_step_x(const ScreenVertex &a,
	const ScreenVertex &b)
{
	return (b.y - a.y);
}

double PerspectiveTriangleRasterizer::edge_step_y(const ScreenVertex &a,
	const ScreenVertex &b)
{
	return (a.x - b.x);
}

bool PerspectiveTriangleRasterizer::compute_bounds(const ScreenVertex vertices[3],
	const ft_render_framebuffer &fb, int32_t &sx, int32_t &ex, int32_t &sy,
	int32_t &ey)
{
	double	min_x;
	double	max_x;
	double	min_y;
	double	max_y;

	min_x = std::min(vertices[0].x, std::min(vertices[1].x, vertices[2].x));
	max_x = std::max(vertices[0].x, std::max(vertices[1].x, vertices[2].x));
	min_y = std::min(vertices[0].y, std::min(vertices[1].y, vertices[2].y));
	max_y = std::max(vertices[0].y, std::max(vertices[1].y, vertices[2].y));
	sx = std::max(0, static_cast<int32_t>(std::floor(min_x)));
	ex = std::min(fb.width - 1, static_cast<int32_t>(std::ceil(max_x)));
	sy = std::max(0, static_cast<int32_t>(std::floor(min_y)));
	ey = std::min(fb.height - 1, static_cast<int32_t>(std::ceil(max_y)));
	return (sx <= ex && sy <= ey);
}

void PerspectiveTriangleRasterizer::compute_steps(Interpolants &it,
	double inv_area)
{
	it.step_x_id = (it.inv_depth[0] * it.sx[0] + it.inv_depth[1] * it.sx[1]
			+ it.inv_depth[2] * it.sx[2]) * inv_area;
	it.step_y_id = (it.inv_depth[0] * it.sy[0] + it.inv_depth[1] * it.sy[1]
			+ it.inv_depth[2] * it.sy[2]) * inv_area;
	it.step_x_u = (it.u_over_d[0] * it.sx[0] + it.u_over_d[1] * it.sx[1]
			+ it.u_over_d[2] * it.sx[2]) * inv_area;
	it.step_y_u = (it.u_over_d[0] * it.sy[0] + it.u_over_d[1] * it.sy[1]
			+ it.u_over_d[2] * it.sy[2]) * inv_area;
	it.step_x_v = (it.v_over_d[0] * it.sx[0] + it.v_over_d[1] * it.sx[1]
			+ it.v_over_d[2] * it.sx[2]) * inv_area;
	it.step_y_v = (it.v_over_d[0] * it.sy[0] + it.v_over_d[1] * it.sy[1]
			+ it.v_over_d[2] * it.sy[2]) * inv_area;
}

void PerspectiveTriangleRasterizer::compute_row_starts(const ScreenVertex vertices[3],
	Interpolants &it, double inv_area, int32_t start_x, int32_t start_y)
{
	double	e0;
	double	e1;
	double	e2;

	e0 = edge(vertices[1], vertices[2], start_x + 0.5, start_y + 0.5);
	e1 = edge(vertices[2], vertices[0], start_x + 0.5, start_y + 0.5);
	e2 = edge(vertices[0], vertices[1], start_x + 0.5, start_y + 0.5);
	it.row_id = (it.inv_depth[0] * e0 + it.inv_depth[1] * e1 + it.inv_depth[2]
			* e2) * inv_area;
	it.row_u = (it.u_over_d[0] * e0 + it.u_over_d[1] * e1 + it.u_over_d[2] * e2)
		* inv_area;
	it.row_v = (it.v_over_d[0] * e0 + it.v_over_d[1] * e1 + it.v_over_d[2] * e2)
		* inv_area;
}

void PerspectiveTriangleRasterizer::compute_interpolants(const ScreenVertex vertices[3],
	double inv_area, int32_t start_x, int32_t start_y, Interpolants &it)
{
	for (int i = 0; i < 3; ++i)
	{
		it.inv_depth[i] = 1.0 / vertices[i].depth;
		it.u_over_d[i] = vertices[i].texture_u * it.inv_depth[i];
		it.v_over_d[i] = vertices[i].texture_v * it.inv_depth[i];
	}
	it.sx[0] = edge_step_x(vertices[1], vertices[2]);
	it.sx[1] = edge_step_x(vertices[2], vertices[0]);
	it.sx[2] = edge_step_x(vertices[0], vertices[1]);
	it.sy[0] = edge_step_y(vertices[1], vertices[2]);
	it.sy[1] = edge_step_y(vertices[2], vertices[0]);
	it.sy[2] = edge_step_y(vertices[0], vertices[1]);
	compute_steps(it, inv_area);
	compute_row_starts(vertices, it, inv_area, start_x, start_y);
}

void PerspectiveTriangleRasterizer::write_pixel(uint32_t *pixel,
	double &depth_entry, double inv_depth, double u, double v,
	const TriangleTexture &texture)
{
	double	d;

	if (inv_depth <= 0.0)
		return ;
	d = 1.0 / inv_depth;
	if (d <= 0.0 || d >= depth_entry)
		return ;
	depth_entry = d;
	*pixel = RendererColor::shade_color(TextureAtlas::sample_triangle_texture(texture,
				u / inv_depth, v / inv_depth), texture.shade);
}

void PerspectiveTriangleRasterizer::rasterize(ft_render_framebuffer &framebuffer,
	std::vector<double> &depth_buffer, const ScreenVertex vertices[3],
	int32_t start_x, int32_t end_x, int32_t start_y, int32_t end_y,
	const Interpolants &it, const TriangleTexture &texture) const
{
	size_t fw = static_cast<size_t>(framebuffer.width);
	size_t row_base = static_cast<size_t>(start_y) * fw
		+ static_cast<size_t>(start_x);
	double e0r = edge(vertices[1], vertices[2], start_x + 0.5, start_y + 0.5);
	double e1r = edge(vertices[2], vertices[0], start_x + 0.5, start_y + 0.5);
	double e2r = edge(vertices[0], vertices[1], start_x + 0.5, start_y + 0.5);
	double id_r = it.row_id, u_r = it.row_u, v_r = it.row_v;
	for (int32_t y = start_y; y <= end_y; ++y)
	{
		double e0 = e0r, e1 = e1r, e2 = e2r, id = id_r, u = u_r, v = v_r;
		size_t pixel_index = row_base;
		for (int32_t x = start_x; x <= end_x; ++x, ++pixel_index)
		{
			bool inside = (e0 >= 0.0 && e1 >= 0.0 && e2 >= 0.0) || (e0 <= 0.0
					&& e1 <= 0.0 && e2 <= 0.0);
			if (inside)
				write_pixel(&framebuffer.pixels[pixel_index],
					depth_buffer[pixel_index], id, u, v, texture);
			e0 += it.sx[0];
			e1 += it.sx[1];
			e2 += it.sx[2];
			id += it.step_x_id;
			u += it.step_x_u;
			v += it.step_x_v;
		}
		e0r += it.sy[0];
		e1r += it.sy[1];
		e2r += it.sy[2];
		id_r += it.step_y_id;
		u_r += it.step_y_u;
		v_r += it.step_y_v;
		row_base += fw;
	}
}

void PerspectiveTriangleRasterizer::draw_triangle(ft_render_framebuffer &framebuffer,
	std::vector<double> &depth_buffer, const ScreenVertex vertices[3],
	uint32_t block_id, uint8_t face) const
{
	double area = edge(vertices[0], vertices[1], vertices[2].x, vertices[2].y);
	if (std::fabs(area) < 0.000001)
		return ;
	int32_t sx, ex, sy, ey;
	if (!compute_bounds(vertices, framebuffer, sx, ex, sy, ey))
		return ;
	Interpolants it;
	compute_interpolants(vertices, 1.0 / area, sx, sy, it);
	TriangleTexture texture = TextureAtlas::prepare_triangle_texture(block_id,
			face);
	rasterize(framebuffer, depth_buffer, vertices, sx, ex, sy, ey, it, texture);
}
