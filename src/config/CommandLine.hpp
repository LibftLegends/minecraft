#ifndef COMMAND_LINE_HPP
# define COMMAND_LINE_HPP

# include "../ft_vox.hpp"
class CommandLine
{
  public:
	CommandLine();
	CommandLine(const CommandLine &other);
	~CommandLine();
	CommandLine &operator=(const CommandLine &other);

	static bool has_flag(int argc, char **argv, const char *flag);
	static int parse_perf_seconds(int argc, char **argv, double *seconds);
};

#endif
