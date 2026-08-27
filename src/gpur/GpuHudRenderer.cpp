#include "../../src/gpur/GpuHudRenderer.hpp"

GpuHudRenderer::GpuHudRenderer() : _fullscreen_vao(0), _crosshair_vao(0),
	_crosshair_vbo(0), _overlay_tex(0), _overlay_w(0), _overlay_h(0),
	_menu_tex(0), _menu_tex_w(0), _menu_tex_h(0), _u_overlay_color(-1),
	_u_overlay_sampler(-1), _u_overlay_ndc_br(-1)
{
}

GpuHudRenderer::GpuHudRenderer(const GpuHudRenderer &other)
{
	(void)other;
}
GpuHudRenderer::~GpuHudRenderer()
{
	destroy();
}
GpuHudRenderer &GpuHudRenderer::operator=(const GpuHudRenderer &other)
{
	(void)other;
	return (*this);
}

bool GpuHudRenderer::initialize(const std::string &shader_dir) noexcept
{
	std::string overlay_vert = GlslLoader::load((shader_dir
				+ "overlay.vert.glsl").c_str());
	std::string overlay_frag = GlslLoader::load((shader_dir
				+ "overlay.frag.glsl").c_str());
	std::string overlay_tex_vert = GlslLoader::load((shader_dir
				+ "overlay_tex.vert.glsl").c_str());
	std::string overlay_tex_frag = GlslLoader::load((shader_dir
				+ "overlay_tex.frag.glsl").c_str());
	if (overlay_vert.empty() || overlay_frag.empty() || overlay_tex_vert.empty()
		|| overlay_tex_frag.empty())
		return (false);
	if (_overlay_shader.initialize(overlay_vert.c_str(),
			overlay_frag.c_str()) != 0)
		return (false);
	if (_overlay_tex_shader.initialize(overlay_tex_vert.c_str(),
			overlay_tex_frag.c_str()) != 0)
		return (false);
	_u_overlay_color = _overlay_shader.uniform("u_overlay_color");
	_u_overlay_sampler = _overlay_tex_shader.uniform("u_overlay_sampler");
	_u_overlay_ndc_br = _overlay_tex_shader.uniform("u_overlay_ndc_br");
	glGenVertexArrays(1, &_fullscreen_vao);
	glGenVertexArrays(1, &_crosshair_vao);
	glGenBuffers(1, &_crosshair_vbo);
	return (true);
}

void GpuHudRenderer::destroy() noexcept
{
	if (_fullscreen_vao != 0)
	{
		glDeleteVertexArrays(1, &_fullscreen_vao);
		_fullscreen_vao = 0;
	}
	if (_crosshair_vao != 0)
	{
		glDeleteVertexArrays(1, &_crosshair_vao);
		_crosshair_vao = 0;
	}
	if (_crosshair_vbo != 0)
	{
		glDeleteBuffers(1, &_crosshair_vbo);
		_crosshair_vbo = 0;
	}
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
	_overlay_rgba.clear();
	_menu_rgba.clear();
	(void)_overlay_shader.destroy();
	(void)_overlay_tex_shader.destroy();
}

void GpuHudRenderer::draw_crosshair(int width, int height) const noexcept
{
	float sx = 8.0f / static_cast<float>(width);
	float sy = 8.0f / static_cast<float>(height);
	float verts[] = {-sx, 0.0f, sx, 0.0f, 0.0f, -sy, 0.0f, sy};
	glBindVertexArray(_crosshair_vao);
	glBindBuffer(GL_ARRAY_BUFFER, _crosshair_vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(verts)), verts,
		GL_STREAM_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(2
			* sizeof(float)), nullptr);
	glEnableVertexAttribArray(0);
	_overlay_shader.use();
	const float col[4] = {0.96f, 0.96f, 0.92f, 1.0f};
	glUniform4fv(_u_overlay_color, 1, col);
	glDisable(GL_DEPTH_TEST);
	glDrawArrays(GL_LINES, 0, 4);
	glEnable(GL_DEPTH_TEST);
	glBindVertexArray(0);
}

void GpuHudRenderer::pixels_to_rgba(std::vector<uint8_t> &rgba_buf,
	const uint32_t *pixels, size_t count, bool opaque_alpha) noexcept
{
	uint32_t	p;
	uint8_t		r;
	uint8_t		g2;
	uint8_t		b;
	uint8_t		a;

	for (size_t i = 0; i < count; ++i)
	{
		p = pixels[i];
		r = static_cast<uint8_t>((p >> 16) & 0xFFU);
		g2 = static_cast<uint8_t>((p >> 8) & 0xFFU);
		b = static_cast<uint8_t>(p & 0xFFU);
		a = opaque_alpha ? ((p == 0U) ? 0U : 255U)
			: (r > g2 ? (r > b ? r : b) : (g2 > b ? g2 : b));
		rgba_buf[i * 4U + 0U] = r;
		rgba_buf[i * 4U + 1U] = g2;
		rgba_buf[i * 4U + 2U] = b;
		rgba_buf[i * 4U + 3U] = a;
	}
}

void GpuHudRenderer::upload_tex(GLuint &tex, int &stored_w, int &stored_h,
	std::vector<uint8_t> &rgba_buf, const uint32_t *pixels, int w, int h,
	bool opaque_alpha) noexcept
{
	if (!pixels || w <= 0 || h <= 0)
		return ;
	const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
	if (rgba_buf.size() != count * 4U)
		rgba_buf.resize(count * 4U);
	pixels_to_rgba(rgba_buf, pixels, count, opaque_alpha);
	bool realloc = (tex == 0 || w != stored_w || h != stored_h);
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
			GL_UNSIGNED_BYTE, rgba_buf.data());
		stored_w = w;
		stored_h = h;
	}
	else
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE,
			rgba_buf.data());
	glBindTexture(GL_TEXTURE_2D, 0);
}

void GpuHudRenderer::draw_tex_quad(GLuint tex, float ndc_br_x,
	float ndc_br_y) noexcept
{
	_overlay_tex_shader.use();
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, tex);
	glUniform1i(_u_overlay_sampler, 1);
	glUniform2f(_u_overlay_ndc_br, ndc_br_x, ndc_br_y);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	glBindVertexArray(_fullscreen_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
}

void GpuHudRenderer::upload_overlay(const uint32_t *pixels, int w,
	int h) noexcept
{
	upload_tex(_overlay_tex, _overlay_w, _overlay_h, _overlay_rgba, pixels, w,
		h, false);
}

void GpuHudRenderer::draw_overlay(int width, int height) noexcept
{
	if (_overlay_tex == 0 || _overlay_w <= 0 || _overlay_h <= 0)
		return ;
	float bx = static_cast<float>(_overlay_w) * 2.0f / static_cast<float>(width)
		- 1.0f;
	float by = 1.0f - static_cast<float>(_overlay_h) * 2.0f
		/ static_cast<float>(height);
	draw_tex_quad(_overlay_tex, bx, by);
}

void GpuHudRenderer::upload_menu(const uint32_t *pixels, int w, int h) noexcept
{
	upload_tex(_menu_tex, _menu_tex_w, _menu_tex_h, _menu_rgba, pixels, w, h,
		true);
}

void GpuHudRenderer::draw_menu() noexcept
{
	if (_menu_tex == 0)
		return ;
	draw_tex_quad(_menu_tex, 1.0f, -1.0f);
}

size_t GpuHudRenderer::gpu_bytes() const noexcept
{
	return (static_cast<size_t>(_overlay_w) * static_cast<size_t>(_overlay_h)
		* 4U + static_cast<size_t>(_menu_tex_w)
		* static_cast<size_t>(_menu_tex_h) * 4U);
}
