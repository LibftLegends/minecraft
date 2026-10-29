#ifndef WORLD_LIGHT_VERSION_HPP
# define WORLD_LIGHT_VERSION_HPP

# include <cstdint>

namespace world_light_version
{
	static const uint16_t INVALID_VERSION = 0U;

	inline uint16_t next(uint16_t version) noexcept
	{
		version = static_cast<uint16_t>(version + 1U);
		if (version == INVALID_VERSION)
			version = 1U;
		return (version);
	}

	inline bool matches(uint16_t left, uint16_t right) noexcept
	{
		return (left != INVALID_VERSION && left == right);
	}
}

#endif
