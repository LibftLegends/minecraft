#ifndef SKYBOX_RENDERER_HPP
# define SKYBOX_RENDERER_HPP

# include "../../src/camera/Camera.hpp"
# include "../../src/frame/RendererColor.hpp"
# include "../ft_vox.hpp"

class SkyboxRenderer
{
  public:
	SkyboxRenderer();
	SkyboxRenderer(const SkyboxRenderer &other);
	~SkyboxRenderer();
	SkyboxRenderer &operator=(const SkyboxRenderer &other);

	static void clear_frame(ft_render_framebuffer &framebuffer,
		std::vector<double> &depth_buffer, const Camera &camera);

  private:
	static uint32_t skybox_row_color(int32_t y, int32_t height);
	static const std::vector<uint32_t> &cached_row_colors(int32_t height);
	static void draw_skybox_sun(ft_render_framebuffer &framebuffer,
		const Camera &camera);
};

#endif
