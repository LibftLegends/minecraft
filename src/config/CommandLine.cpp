#include "../../src/config/CommandLine.hpp"

CommandLine::CommandLine()
{
}

CommandLine::CommandLine(const CommandLine &other)
{
	*this = other;
}

CommandLine::~CommandLine()
{
}

CommandLine &CommandLine::operator=(const CommandLine &other)
{
	(void)other;
	return (*this);
}

bool CommandLine::has_flag(int argc, char **argv, const char *flag)
{
	int	index;

	index = 1;
	while (index < argc)
	{
		if (argv[index] != nullptr && std::strcmp(argv[index], flag) == 0)
			return (true);
		index += 1;
	}
	return (false);
}

int CommandLine::parse_perf_seconds(int argc, char **argv, double *seconds_out)
{
	int			index;
	const char	*argument;
	const char	*value_string;
	char		*end_pointer;
	double		parsed_value;

	if (seconds_out == nullptr)
		return (-1);
	index = 1;
	while (index < argc)
	{
		argument = argv[index];
		if (argument != nullptr && std::strncmp(argument, "--perf-seconds",
				14) == 0)
		{
			value_string = argument + 14;
			if (*value_string == '=')
				value_string += 1;
			else if (*value_string == '\0')
			{
				if (index + 1 >= argc || argv[index + 1] == nullptr)
					return (0);
				value_string = argv[index + 1];
			}
			if (*value_string == '\0')
				return (-1);
			parsed_value = std::strtod(value_string, &end_pointer);
			if (end_pointer == value_string || *end_pointer != '\0'
				|| parsed_value <= 0.0)
				return (-1);
			*seconds_out = parsed_value;
			return (1);
		}
		index += 1;
	}
	return (0);
}
