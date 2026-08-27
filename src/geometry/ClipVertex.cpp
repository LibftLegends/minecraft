#include "../../src/geometry/ClipVertex.hpp"

ClipVertex::ClipVertex() : view_x(0.0), view_y(0.0), view_z(0.0),
	texture_u(0.0), texture_v(0.0)
{
}

ClipVertex::ClipVertex(const ClipVertex &other) : view_x(0.0), view_y(0.0),
	view_z(0.0), texture_u(0.0), texture_v(0.0)
{
	*this = other;
}

ClipVertex::~ClipVertex()
{
}

ClipVertex &ClipVertex::operator=(const ClipVertex &other)
{
	if (this != &other)
	{
		view_x = other.view_x;
		view_y = other.view_y;
		view_z = other.view_z;
		texture_u = other.texture_u;
		texture_v = other.texture_v;
	}
	return (*this);
}
