#ifndef APPLICATION_ERROR_HPP
# define APPLICATION_ERROR_HPP

# include "../ft_vox.hpp"

class ApplicationError
{
  public:
	ApplicationError();
	ApplicationError(const ApplicationError &other);
	~ApplicationError();
	ApplicationError &operator=(const ApplicationError &other);

	static int fail(const char *operation, int32_t error_code);
};

#endif
