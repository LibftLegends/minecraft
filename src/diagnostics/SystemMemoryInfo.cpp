#include "../../src/diagnostics/SystemMemoryInfo.hpp"

SystemMemoryInfo::SystemMemoryInfo()
{
}

SystemMemoryInfo::SystemMemoryInfo(const SystemMemoryInfo &other)
{
	(void)other;
}

SystemMemoryInfo::~SystemMemoryInfo()
{
}

SystemMemoryInfo &SystemMemoryInfo::operator=(const SystemMemoryInfo &other)
{
	(void)other;
	return (*this);
}

#if defined(__APPLE__)

uint32_t SystemMemoryInfo::resident_set_mb()
{
	struct task_basic_info	info;
	mach_msg_type_number_t	sz;

	sz = TASK_BASIC_INFO_COUNT;
	if (task_info(mach_task_self(), TASK_BASIC_INFO,
			reinterpret_cast<task_info_t>(&info), &sz) == KERN_SUCCESS)
		return (static_cast<uint32_t>(info.resident_size / (1024UL * 1024UL)));
	return (0U);
}

#elif defined(_WIN32)

uint32_t SystemMemoryInfo::resident_set_mb()
{
	return (0U);
}

#else

uint32_t SystemMemoryInfo::resident_set_mb()
{
	FILE		*f;
	char		line[256];
	uint32_t	kb;

	f = std::fopen("/proc/self/status", "r");
	if (!f)
		return (0U);
	while (std::fgets(line, sizeof(line), f))
	{
		if (std::strncmp(line, "VmRSS:", 6) == 0)
		{
			kb = 0U;
			std::sscanf(line + 6, "%u", &kb);
			std::fclose(f);
			return (kb / 1024U);
		}
	}
	std::fclose(f);
	return (0U);
}

#endif
