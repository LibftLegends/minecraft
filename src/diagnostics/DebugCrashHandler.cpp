#include "../../src/diagnostics/DebugCrashHandler.hpp"
#include <csignal>
#include <cstdlib>

DebugCrashHandler::DebugCrashHandler()
{
}

DebugCrashHandler::DebugCrashHandler(const DebugCrashHandler &other)
{
	*this = other;
}

DebugCrashHandler::~DebugCrashHandler()
{
}

DebugCrashHandler &DebugCrashHandler::operator=(const DebugCrashHandler &other)
{
	(void)other;
	return (*this);
}

#ifdef DEBUG

static void debug_abort_on_sigint(int signal_number)
{
	(void)signal_number;
	std::abort();
}

int DebugCrashHandler::enable()
{
	int32_t	error_code;

	error_code = dbg_enable_crash_stack_traces();
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("debug crash stack trace setup",
				error_code));
	if (std::getenv("FT_VOX_ABORT_ON_SIGINT") != nullptr)
		std::signal(SIGINT, &debug_abort_on_sigint);
	return (0);
}

#else

int DebugCrashHandler::enable()
{
	return (0);
}

#endif
