#include "../../src/diagnostics/FramebufferHasher.hpp"

const uint64_t FramebufferHasher::FNV_OFFSET_BASIS = UINT64_C(1469598103934665603);
const uint64_t FramebufferHasher::FNV_PRIME = UINT64_C(1099511628211);

FramebufferHasher::FramebufferHasher()
{
}

FramebufferHasher::FramebufferHasher(const FramebufferHasher &other)
{
	(void)other;
}

FramebufferHasher::~FramebufferHasher()
{
}

FramebufferHasher &FramebufferHasher::operator=(const FramebufferHasher &other)
{
	(void)other;
	return (*this);
}

uint64_t FramebufferHasher::hash_framebuffer(const ft_render_framebuffer &framebuffer)
{
	uint64_t	hash;
	size_t		pixel_count;

	if (!framebuffer.pixels || framebuffer.width <= 0
		|| framebuffer.height <= 0)
		return (UINT64_C(0));
	hash = FNV_OFFSET_BASIS;
	pixel_count = static_cast<size_t>(framebuffer.width)
		* static_cast<size_t>(framebuffer.height);
	for (size_t i = 0; i < pixel_count; ++i)
	{
		hash ^= static_cast<uint64_t>(framebuffer.pixels[i]);
		hash *= FNV_PRIME;
	}
	return (hash);
}
