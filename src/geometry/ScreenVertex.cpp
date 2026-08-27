#include "../../src/geometry/ScreenVertex.hpp"

ScreenVertex::ScreenVertex() : x(0.0), y(0.0), depth(0.0), texture_u(0.0),
	texture_v(0.0)
{
}

ScreenVertex::ScreenVertex(const ScreenVertex &other) : x(0.0), y(0.0),
	depth(0.0), texture_u(0.0), texture_v(0.0)
{
	*this = other;
}

ScreenVertex::~ScreenVertex()
{
}

ScreenVertex &ScreenVertex::operator=(const ScreenVertex &other)
{
	if (this != &other)
	{
		x = other.x;
		y = other.y;
		depth = other.depth;
		texture_u = other.texture_u;
		texture_v = other.texture_v;
	}
	return (*this);
}
