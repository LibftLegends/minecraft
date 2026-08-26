#include "../../src/diagnostics/ApplicationError.hpp"

ApplicationError::ApplicationError()
{
}

ApplicationError::ApplicationError(const ApplicationError &other)
{
	*this = other;
}

ApplicationError::~ApplicationError()
{
}

ApplicationError &ApplicationError::operator=(const ApplicationError &other)
{
	(void)other;
	return (*this);
}

int ApplicationError::fail(const char *operation, int32_t error_code)
{
	std::fprintf(stderr, "Application: %s failed: %s (%d)\n", operation,
		ft_strerror(error_code), error_code);
	return (1);
}
