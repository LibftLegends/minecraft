#ifndef GPUR_RENDERER_HPP
# define GPUR_RENDERER_HPP

# include "../../src/camera/Camera.hpp"
# include "../../src/gpur/GpuOverlayRenderer.hpp"
# include "../../src/gpur/GpuWorldRenderer.hpp"
# include "../../src/world/World.hpp"
# include "../ft_vox.hpp"

class GpuRenderer
{
  public:
	GpuRenderer();
	GpuRenderer(const GpuRenderer &other);
	~GpuRenderer();
	GpuRenderer &operator=(const GpuRenderer &other);

	bool initialize(int width, int height);
	void destroy();
	void resize(int width, int height);
	void render(const Camera &camera, const World &world);
	void upload_overlay(const uint32_t *pixels, int w, int h);
	void draw_overlay();
	void draw_tint(float r, float g, float b, float a);
	void clear_screen();
	void upload_menu(const uint32_t *pixels, int w, int h);
	void draw_menu(float alpha);
	bool is_ready() const;
	uint32_t gpu_mb_approx() const;

  private:
	GpuWorldRenderer _world;
	GpuOverlayRenderer _overlay;

	GLuint _sky_vao;
	GLuint _crosshair_vao;
	GLuint _crosshair_vbo;
	bool _ready;
	int _width;
	int _height;

	static const char *SHADER_DIR;
};

#endif
