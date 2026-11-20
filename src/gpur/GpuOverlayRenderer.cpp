#include "../../src/gpur/GpuOverlayRenderer.hpp"
#include <algorithm>

GpuOverlayRenderer::GpuOverlayRenderer() : _overlay_tex(0), _overlay_w(0),
	_overlay_h(0), _menu_tex(0), _menu_tex_w(0), _menu_tex_h(0), _sky_vao(0),
	_u_overlay_color(-1), _u_overlay_sampler(-1), _u_overlay_ndc_br(-1),
	_u_tint_color(-1)
{
}
GpuOverlayRenderer::GpuOverlayRenderer(const GpuOverlayRenderer &other)
	: _overlay_tex(0), _overlay_w(0), _overlay_h(0), _menu_tex(0),
	_menu_tex_w(0), _menu_tex_h(0), _sky_vao(0), _u_overlay_color(-1),
	_u_overlay_sampler(-1), _u_overlay_ndc_br(-1), _u_tint_color(-1)
{
	(void)other;
}
GpuOverlayRenderer::~GpuOverlayRenderer()
{
	destroy();
}
GpuOverlayRenderer &GpuOverlayRenderer::operator=(const GpuOverlayRenderer &other)
{
	(void)other;
	return (*this);
}

bool GpuOverlayRenderer::initialize(GLuint sky_vao,
	const std::string &shader_dir)
{
	_sky_vao = sky_vao;
	auto load = [&](ft_gpu_shader &shader, const char *v, const char *f)
	{
		std::string vs = GlslLoader::load((shader_dir + v).c_str());
		std::string fs = GlslLoader::load((shader_dir + f).c_str());
		return (!vs.empty() && !fs.empty() && shader.initialize(vs.c_str(),
				fs.c_str()) == FT_ERR_SUCCESS);
	};
	if (!load(_overlay_shader, "overlay.vert.glsl", "overlay.frag.glsl"))
		return (false);
	if (!load(_overlay_tex_shader, "overlay_tex.vert.glsl",
			"overlay_tex.frag.glsl"))
		return (false);
	if (!load(_tint_shader, "sky.vert.glsl", "tint.frag.glsl"))
		return (false);
	if (!load(_item_marker_shader, "item_marker.vert.glsl",
			"item_marker.frag.glsl"))
		return (false);
	_u_overlay_color = _overlay_shader.uniform("u_overlay_color");
	_u_overlay_sampler = _overlay_tex_shader.uniform("u_overlay_sampler");
	_u_overlay_ndc_br = _overlay_tex_shader.uniform("u_overlay_ndc_br");
	_u_tint_color = _tint_shader.uniform("u_tint_color");
	_u_item_marker_bounds = _item_marker_shader.uniform("u_marker_bounds[0]");
	_u_item_marker_colors = _item_marker_shader.uniform("u_marker_colors[0]");
	_u_item_marker_canvas = _item_marker_shader.uniform("u_canvas_viewport");
	return (true);
}

void GpuOverlayRenderer::destroy()
{
	if (_overlay_tex != 0)
	{
		glDeleteTextures(1, &_overlay_tex);
		_overlay_tex = 0;
	}
	if (_menu_tex != 0)
	{
		glDeleteTextures(1, &_menu_tex);
		_menu_tex = 0;
	}
	_overlay_w = 0;
	_overlay_h = 0;
	_overlay_rgba.clear();
	_menu_tex_w = 0;
	_menu_tex_h = 0;
	_menu_rgba.clear();
	(void)_overlay_shader.destroy();
	(void)_overlay_tex_shader.destroy();
	(void)_tint_shader.destroy();
	(void)_item_marker_shader.destroy();
}

void GpuOverlayRenderer::pixels_to_rgba(const uint32_t *src, int width,
	int height, int stride, std::vector<uint8_t> &dst, bool opaque_non_black)
{
	const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4U;
	if (dst.size() < bytes)
		dst.resize(bytes);
	uint8_t *output = dst.data();
	for (int row = 0; row < height; ++row)
	{
		const uint32_t *input = src + static_cast<size_t>(row) * stride;
		for (int column = 0; column < width; ++column)
		{
			const uint32_t pixel = input[column];
			const uint8_t red = static_cast<uint8_t>((pixel >> 16) & 0xFFU);
			const uint8_t green = static_cast<uint8_t>((pixel >> 8) & 0xFFU);
			const uint8_t blue = static_cast<uint8_t>(pixel & 0xFFU);
			*output++ = red;
			*output++ = green;
			*output++ = blue;
			*output++ = opaque_non_black ? std::max(red, std::max(green, blue))
				: static_cast<uint8_t>(pixel == 0U ? 0U : 255U);
		}
	}
}

void GpuOverlayRenderer::upload_texture(GLuint &tex, int &tex_w, int &tex_h,
	const uint8_t *rgba, int w, int h)
{
	bool	realloc;

	realloc = (tex == 0 || w != tex_w || h != tex_h);
	if (tex == 0)
		glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	if (realloc)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
			GL_UNSIGNED_BYTE, rgba);
		tex_w = w;
		tex_h = h;
	}
	else
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE,
			rgba);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void GpuOverlayRenderer::upload_overlay(const uint32_t *pixels, int w, int h)
{
	if (!pixels || w <= 0 || h <= 0)
		return ;
	pixels_to_rgba(pixels, w, h, w, _overlay_rgba, true);
	upload_texture(_overlay_tex, _overlay_w, _overlay_h, _overlay_rgba.data(),
		w, h);
}

void GpuOverlayRenderer::upload_overlay_region(const uint32_t *pixels, int w,
	int h, int x, int y, int region_width, int region_height)
{
	if (!pixels || w <= 0 || h <= 0 || x < 0 || y < 0
		|| region_width <= 0 || region_height <= 0
		|| x > w - region_width || y > h - region_height)
		return ;
	if (_overlay_tex == 0 || _overlay_w != w || _overlay_h != h)
	{
		upload_overlay(pixels, w, h);
		return ;
	}
	pixels_to_rgba(pixels + static_cast<size_t>(y) * w + x,
		region_width, region_height, w, _overlay_rgba, true);
	glBindTexture(GL_TEXTURE_2D, _overlay_tex);
	glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, region_width, region_height,
		GL_RGBA, GL_UNSIGNED_BYTE, _overlay_rgba.data());
	glBindTexture(GL_TEXTURE_2D, 0);
}

void GpuOverlayRenderer::draw_fullscreen_quad()
{
	glBindVertexArray(_sky_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
}

void GpuOverlayRenderer::bind_tex_shader(GLuint tex, float ndc_x, float ndc_y)
{
	_overlay_tex_shader.use();
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, tex);
	glUniform1i(_u_overlay_sampler, 1);
	glUniform2f(_u_overlay_ndc_br, ndc_x, ndc_y);
}

void GpuOverlayRenderer::draw_overlay(int viewport_w, int viewport_h)
{
	float	nx;
	float	ny;

	if (_overlay_tex == 0 || _overlay_w <= 0 || _overlay_h <= 0)
		return ;
	nx = static_cast<float>(_overlay_w) * 2.0f / static_cast<float>(viewport_w)
		- 1.0f;
	ny = 1.0f - static_cast<float>(_overlay_h) * 2.0f
		/ static_cast<float>(viewport_h);
	bind_tex_shader(_overlay_tex, nx, ny);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	draw_fullscreen_quad();
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
}

void GpuOverlayRenderer::draw_item_markers(const ItemMarker *markers,
	std::size_t count, int canvas_w, int canvas_h, int viewport_w, int viewport_h)
{
	if (markers == nullptr || count == 0U || count > MAX_ITEM_MARKERS
		|| canvas_w <= 0 || canvas_h <= 0 || viewport_w <= 0 || viewport_h <= 0)
		return ;
	float bounds[MAX_ITEM_MARKERS * 4U];
	float colors[MAX_ITEM_MARKERS * 3U];
	for (std::size_t index = 0U; index < count; ++index)
	{
		const ItemMarker &marker = markers[index];
		bounds[index * 4U] = static_cast<float>(marker.x - marker.radius);
		bounds[index * 4U + 1U] = static_cast<float>(marker.y - marker.radius);
		bounds[index * 4U + 2U] = static_cast<float>(marker.x + marker.radius + 1);
		bounds[index * 4U + 3U] = static_cast<float>(marker.y + marker.radius + 1);
		colors[index * 3U] = static_cast<float>((marker.color >> 16U) & 0xffU)
			/ 255.0f;
		colors[index * 3U + 1U] = static_cast<float>((marker.color >> 8U) & 0xffU)
			/ 255.0f;
		colors[index * 3U + 2U] = static_cast<float>(marker.color & 0xffU)
			/ 255.0f;
	}
	_item_marker_shader.use();
	glUniform4fv(_u_item_marker_bounds, static_cast<GLsizei>(count), bounds);
	glUniform3fv(_u_item_marker_colors, static_cast<GLsizei>(count), colors);
	glUniform4f(_u_item_marker_canvas, static_cast<float>(canvas_w),
		static_cast<float>(canvas_h), static_cast<float>(viewport_w),
		static_cast<float>(viewport_h));
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	glBindVertexArray(_sky_vao);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count * 6U));
	glBindVertexArray(0);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
}

void GpuOverlayRenderer::upload_menu(const uint32_t *pixels, int w, int h)
{
	if (!pixels || w <= 0 || h <= 0)
		return ;
	pixels_to_rgba(pixels, w, h, w, _menu_rgba, false);
	upload_texture(_menu_tex, _menu_tex_w, _menu_tex_h, _menu_rgba.data(), w,
		h);
}

void GpuOverlayRenderer::draw_menu()
{
	if (_menu_tex == 0)
		return ;
	bind_tex_shader(_menu_tex, 1.0f, -1.0f);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	draw_fullscreen_quad();
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
}

void GpuOverlayRenderer::draw_tint(float r, float g, float b, float a)
{
	_tint_shader.use();
	glUniform4f(_u_tint_color, r, g, b, a);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	draw_fullscreen_quad();
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

void GpuOverlayRenderer::clear_screen()
{
	glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

GLint GpuOverlayRenderer::uniform_overlay_color() const
{
	return (_u_overlay_color);
}
GLint GpuOverlayRenderer::uniform_overlay_sampler() const
{
	return (_u_overlay_sampler);
}
GLint GpuOverlayRenderer::uniform_overlay_ndc_br() const
{
	return (_u_overlay_ndc_br);
}
GLint GpuOverlayRenderer::uniform_tint_color() const
{
	return (_u_tint_color);
}
const ft_gpu_shader &GpuOverlayRenderer::overlay_shader() const
{
	return (_overlay_shader);
}
const ft_gpu_shader &GpuOverlayRenderer::overlay_tex_shader() const
{
	return (_overlay_tex_shader);
}
const ft_gpu_shader &GpuOverlayRenderer::tint_shader() const
{
	return (_tint_shader);
}
