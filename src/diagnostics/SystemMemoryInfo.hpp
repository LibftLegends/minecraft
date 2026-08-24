#ifndef SYSTEM_MEMORY_INFO_HPP
# define SYSTEM_MEMORY_INFO_HPP

# include "../ft_vox.hpp"

class SystemMemoryInfo
{
  public:
	SystemMemoryInfo();
	SystemMemoryInfo(const SystemMemoryInfo &other);
	~SystemMemoryInfo();
	SystemMemoryInfo &operator=(const SystemMemoryInfo &other);

	static uint32_t resident_set_mb();
};

#endif
