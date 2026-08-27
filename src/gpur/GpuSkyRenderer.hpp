#ifndef GPU_SKY_RENDERER_HPP
# define GPU_SKY_RENDERER_HPP

# include "../../Libft/Modules/GPGR/ft_gpu_shader.hpp"
# include "../../Libft/Modules/GPGR/gpgr_gl_funcs.hpp"
# include "../../src/gpur/GlslLoader.hpp"
# include "../ft_vox.hpp"

class GpuSkyRenderer
{
	ft_gpu_shader _sky_shader;
	ft_gpu_shader _tint_shader;
	GLuint _fullscreen_vao;
	GLint _u_sky_size;
	GLint _u_tint_color;

  public:
	GpuSkyRenderer();
	GpuSkyRenderer(const GpuSkyRenderer &other);
	~GpuSkyRenderer();
	GpuSkyRenderer &operator=(const GpuSkyRenderer &other);

	bool initialize(const std::string &shader_dir) noexcept;
	void destroy() noexcept;

	void draw_sky(int width, int height) const noexcept;
	void draw_tint(float r, float g, float b, float a) const noexcept;
	void draw_fullscreen() const noexcept;
};

#endif
