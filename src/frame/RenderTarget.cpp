#include "../../src/frame/RenderTarget.hpp"

RenderTarget::RenderTarget()
{
	target_framebuffer.width = 0;
	target_framebuffer.height = 0;
	target_framebuffer.pixels = nullptr;
}

RenderTarget::RenderTarget(const RenderTarget &other)
{
	*this = other;
}

RenderTarget::~RenderTarget()
{
}

RenderTarget &RenderTarget::operator=(const RenderTarget &other)
{
	if (this != &other)
	{
		target_framebuffer = other.target_framebuffer;
		color_buffer = other.color_buffer;
		target_depth_buffer = other.target_depth_buffer;
		target_framebuffer.pixels = color_buffer.empty()
			? nullptr : color_buffer.data();
	}
	return (*this);
}

ft_render_framebuffer &RenderTarget::prepare(const ft_render_framebuffer &output)
{
	size_t	sz;

	target_framebuffer.width = output.width;
	target_framebuffer.height = output.height;
	sz = static_cast<size_t>(target_framebuffer.width)
		* static_cast<size_t>(target_framebuffer.height);
	if (target_depth_buffer.size() != sz)
		target_depth_buffer.resize(sz);
	if (output.pixels != nullptr && sz != 0U)
		target_framebuffer.pixels = output.pixels;
	else
	{
		if (color_buffer.size() != sz)
			color_buffer.resize(sz);
		target_framebuffer.pixels = color_buffer.data();
	}
	return (target_framebuffer);
}

std::vector<double> &RenderTarget::depth_buffer()
{
	return (target_depth_buffer);
}

void RenderTarget::blit_same_size(ft_render_framebuffer &dst) const
{
	size_t n = static_cast<size_t>(dst.width) * static_cast<size_t>(dst.height);
	for (size_t i = 0; i < n; ++i)
		dst.pixels[i] = target_framebuffer.pixels[i];
}

void RenderTarget::blit_scaled(ft_render_framebuffer &dst) const
{
	for (int32_t y = 0; y < dst.height; ++y)
	{
		int32_t sy = (y * target_framebuffer.height) / dst.height;
		for (int32_t x = 0; x < dst.width; ++x)
		{
			int32_t sx = (x * target_framebuffer.width) / dst.width;
			dst.pixels[y * dst.width + x] = target_framebuffer.pixels[sy
				* target_framebuffer.width + sx];
		}
	}
}

void RenderTarget::blit_to(ft_render_framebuffer &destination) const
{
	if (!destination.pixels || !target_framebuffer.pixels)
		return ;
	if (destination.pixels == target_framebuffer.pixels)
		return ;
	if (destination.width == target_framebuffer.width
		&& destination.height == target_framebuffer.height)
		blit_same_size(destination);
	else
		blit_scaled(destination);
}
