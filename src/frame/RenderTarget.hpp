#ifndef RENDER_TARGET_HPP
# define RENDER_TARGET_HPP

# include "../ft_vox.hpp"

class RenderTarget
{
  public:
	RenderTarget();
	RenderTarget(const RenderTarget &other);
	~RenderTarget();
	RenderTarget &operator=(const RenderTarget &other);

	ft_render_framebuffer &prepare(const ft_render_framebuffer &output);
	std::vector<double> &depth_buffer();
	void blit_to(ft_render_framebuffer &destination) const;

  private:
	ft_render_framebuffer target_framebuffer;
	std::vector<uint32_t> color_buffer;
	std::vector<double> target_depth_buffer;

	void blit_same_size(ft_render_framebuffer &dst) const;
	void blit_scaled(ft_render_framebuffer &dst) const;
};

#endif
