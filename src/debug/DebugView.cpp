#include "../../src/debug/DebugView.hpp"

DebugView::DebugView()
{
}

DebugView::DebugView(const DebugView &other)
{
	*this = other;
}

DebugView::~DebugView()
{
}

DebugView &DebugView::operator=(const DebugView &other)
{
	(void)other;
	return (*this);
}

uint32_t DebugView::rgba(uint8_t red, uint8_t green, uint8_t blue)
{
	return ((static_cast<uint32_t>(red) << 24U)
		| (static_cast<uint32_t>(green) << 16U)
		| (static_cast<uint32_t>(blue) << 8U) | 0xFFU);
}

int32_t DebugView::camera_offset(double value)
{
	int32_t	scaled;
	int32_t	offset;

	scaled = static_cast<int32_t>(value * 64.0);
	offset = scaled % 96;
	if (offset < 0)
		offset = offset + 96;
	return (offset);
}

uint32_t DebugView::background_color(int32_t y, int32_t horizon)
{
	uint8_t	shade;

	if (y < horizon)
	{
		shade = static_cast<uint8_t>(120 + ((horizon - y) % 80));
		return (DebugView::rgba(42U, shade, 206U));
	}
	shade = static_cast<uint8_t>(70 + ((y - horizon) % 50));
	return (DebugView::rgba(38U, shade, 54U));
}

void DebugView::draw_crosshair(ft_render_framebuffer &framebuffer)
{
	int32_t	center_x;
	int32_t	center_y;
	int32_t	index;

	center_x = framebuffer.width / 2;
	center_y = framebuffer.height / 2;
	index = -10;
	while (index <= 10)
	{
		if (center_x + index >= 0 && center_x + index < framebuffer.width)
			framebuffer.pixels[(center_y * framebuffer.width) + center_x
				+ index] = DebugView::rgba(245U, 245U, 235U);
		if (center_y + index >= 0 && center_y + index < framebuffer.height)
			framebuffer.pixels[((center_y + index) * framebuffer.width)
				+ center_x] = DebugView::rgba(245U, 245U, 235U);
		index = index + 1;
	}
}

void DebugView::draw(ft_render_framebuffer &framebuffer, const Camera &camera)
{
	int32_t		x;
	int32_t		y;
	int32_t		horizon;
	int32_t		offset_x;
	int32_t		offset_z;
	uint32_t	color;

	if (framebuffer.pixels == nullptr || framebuffer.width <= 0
		|| framebuffer.height <= 0)
		return ;
	horizon = framebuffer.height / 2;
	offset_x = DebugView::camera_offset(camera.x);
	offset_z = DebugView::camera_offset(camera.z);
	y = 0;
	while (y < framebuffer.height)
	{
		x = 0;
		while (x < framebuffer.width)
		{
			color = DebugView::background_color(y, horizon);
			if (y >= horizon && (((x + offset_x) % 96) == 0 || ((y + offset_z)
						% 48) == 0))
				color = DebugView::rgba(88U, 122U, 82U);
			framebuffer.pixels[(y * framebuffer.width) + x] = color;
			x = x + 1;
		}
		y = y + 1;
	}
	DebugView::draw_crosshair(framebuffer);
}
