#include "../../src/meshes/TriangleRasterizer.hpp"

TriangleRasterizer::TriangleRasterizer()
{
}

TriangleRasterizer::TriangleRasterizer(const TriangleRasterizer &other)
{
	*this = other;
}

TriangleRasterizer::~TriangleRasterizer()
{
}

TriangleRasterizer &TriangleRasterizer::operator=(const TriangleRasterizer &other)
{
	(void)other;
	return (*this);
}
