#include "../../src/gpur/GlslLoader.hpp"

const char *GlslLoader::SHADER_DIR = "src/gpur/shaders/";

GlslLoader::GlslLoader()
{
}
GlslLoader::GlslLoader(const GlslLoader &other)
{
	(void)other;
}
GlslLoader::~GlslLoader()
{
}
GlslLoader &GlslLoader::operator=(const GlslLoader &other)
{
	(void)other;
	return (*this);
}

std::string GlslLoader::load(const char *path)
{
	FILE	*f;
	long	sz;
	size_t	read;

	f = std::fopen(path, "rb");
	if (!f)
		return (std::string());
	if (std::fseek(f, 0, SEEK_END) != 0)
	{
		std::fclose(f);
		return (std::string());
	}
	sz = std::ftell(f);
	if (sz <= 0)
	{
		std::fclose(f);
		return (std::string());
	}
	if (std::fseek(f, 0, SEEK_SET) != 0)
	{
		std::fclose(f);
		return (std::string());
	}
	std::string src(static_cast<size_t>(sz), '\0');
	read = std::fread(src.data(), 1, static_cast<size_t>(sz), f);
	std::fclose(f);
	if (read != static_cast<size_t>(sz))
		return (std::string());
	return (src);
}
