#ifndef SCREEN_VERTEX_HPP
# define SCREEN_VERTEX_HPP

class ScreenVertex
{
  public:
	double x;
	double y;
	double depth;
	double texture_u;
	double texture_v;

	ScreenVertex();
	ScreenVertex(const ScreenVertex &other);
	~ScreenVertex();
	ScreenVertex &operator=(const ScreenVertex &other);
};

#endif
