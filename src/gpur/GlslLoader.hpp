#ifndef GLSL_LOADER_HPP
# define GLSL_LOADER_HPP

# include "../ft_vox.hpp"
class GlslLoader
{
  public:
	GlslLoader();
	GlslLoader(const GlslLoader &other);
	~GlslLoader();
	GlslLoader &operator=(const GlslLoader &other);

	static std::string load(const char *path);

  private:
	static const char *SHADER_DIR;
};

#endif
