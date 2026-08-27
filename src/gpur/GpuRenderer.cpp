#include "../../src/gpur/GpuRenderer.hpp"

const char *GpuRenderer::SHADER_DIR = "src/gpur/shaders/";

GpuRenderer::GpuRenderer() : _sky_vao(0), _crosshair_vao(0), _crosshair_vbo(0),
	_ready(false), _width(0), _height(0)
{
}

GpuRenderer::GpuRenderer(const GpuRenderer &other) : _sky_vao(0),
	_crosshair_vao(0), _crosshair_vbo(0), _ready(false), _width(0), _height(0)
{
	(void)other;
}

GpuRenderer::~GpuRenderer()
{
	destroy();
}

GpuRenderer &GpuRenderer::operator=(const GpuRenderer &other)
{
	(void)other;
	return (*this);
}

bool GpuRenderer::initialize(int width, int height)
{
	destroy();
	glGenVertexArrays(1, &_sky_vao);
	glGenVertexArrays(1, &_crosshair_vao);
	glGenBuffers(1, &_crosshair_vbo);
	std::string d = SHADER_DIR;
	if (!_overlay.initialize(_sky_vao, d))
		return (false);
	if (!_world.initialize(width, height, _sky_vao, _crosshair_vao,
			_crosshair_vbo, d))
		return (false);
	_width = width;
	_height = height;
	_ready = true;
	return (true);
}

void GpuRenderer::destroy()
{
	_world.destroy();
	_overlay.destroy();
	_ready = false;
	if (_sky_vao != 0)
	{
		glDeleteVertexArrays(1, &_sky_vao);
		_sky_vao = 0;
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
}

void GpuRenderer::resize(int width, int height)
{
	_width = width;
	_height = height;
	_world.resize(width, height);
	glViewport(0, 0, width, height);
}

void GpuRenderer::render(const Camera &camera, const World &world)
{
	if (!_ready)
		return ;
	_world.render(camera, world);
}

void GpuRenderer::upload_overlay(const uint32_t *pixels, int w, int h)
{
	_overlay.upload_overlay(pixels, w, h);
}

void GpuRenderer::draw_overlay()
{
	_overlay.draw_overlay(_width, _height);
}

void GpuRenderer::draw_tint(float r, float g, float b, float a)
{
	if (_ready)
		_overlay.draw_tint(r, g, b, a);
}

void GpuRenderer::clear_screen()
{
	_overlay.clear_screen();
}

void GpuRenderer::upload_menu(const uint32_t *pixels, int w, int h)
{
	_overlay.upload_menu(pixels, w, h);
}

void GpuRenderer::draw_menu(float alpha)
{
	(void)alpha;
	_overlay.draw_menu();
}

bool GpuRenderer::is_ready() const
{
	return (_ready);
}

uint32_t GpuRenderer::gpu_mb_approx() const
{
	return (static_cast<uint32_t>(_world.gpu_bytes() / (1024UL * 1024UL)));
}
