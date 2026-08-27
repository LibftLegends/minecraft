#ifndef CLIP_VERTEX_HPP
# define CLIP_VERTEX_HPP

class ClipVertex
{
  public:
	double view_x;
	double view_y;
	double view_z;
	double texture_u;
	double texture_v;

	ClipVertex();
	ClipVertex(const ClipVertex &other);
	~ClipVertex();
	ClipVertex &operator=(const ClipVertex &other);
};

#endif
