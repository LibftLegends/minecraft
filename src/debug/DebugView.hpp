#ifndef DEBUG_VIEW_HPP
# define DEBUG_VIEW_HPP

# include "../../src/camera/Camera.hpp"
# include "../ft_vox.hpp"

class DebugView
{
  public:
	DebugView();
	DebugView(const DebugView &other);
	~DebugView();
	DebugView &operator=(const DebugView &other);

	void draw(ft_render_framebuffer &framebuffer, const Camera &camera);

  private:
	static uint32_t rgba(uint8_t red, uint8_t green, uint8_t blue);
	static int32_t camera_offset(double value);
	static uint32_t background_color(int32_t y, int32_t horizon);
	static void draw_crosshair(ft_render_framebuffer &framebuffer);
};

#endif
