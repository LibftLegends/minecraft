#ifndef MESH_CLIPPER_HPP
# define MESH_CLIPPER_HPP

# include "../../src/geometry/ClipVertex.hpp"
# include "../ft_vox.hpp"

class MeshClipper
{
  public:
	MeshClipper();
	MeshClipper(const MeshClipper &other);
	~MeshClipper();
	MeshClipper &operator=(const MeshClipper &other);

	static size_t clip_near(const ClipVertex input[3], ClipVertex output[4]);

  private:
	static constexpr double NEAR_PLANE = 0.08;

	static ClipVertex lerp(const ClipVertex &from, const ClipVertex &to,
		double amount);
};

#endif
