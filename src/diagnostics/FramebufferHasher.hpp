#ifndef FRAMEBUFFER_HASHER_HPP
# define FRAMEBUFFER_HASHER_HPP

# include "../ft_vox.hpp"

class FramebufferHasher
{
  public:
	FramebufferHasher();
	FramebufferHasher(const FramebufferHasher &other);
	~FramebufferHasher();
	FramebufferHasher &operator=(const FramebufferHasher &other);

	static uint64_t hash_framebuffer(const ft_render_framebuffer &framebuffer);

  private:
	static const uint64_t FNV_OFFSET_BASIS;
	static const uint64_t FNV_PRIME;
};

#endif
