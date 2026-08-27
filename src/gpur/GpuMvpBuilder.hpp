#ifndef GPU_MVP_BUILDER_HPP
# define GPU_MVP_BUILDER_HPP

# include "../../src/camera/Camera.hpp"
# include "../ft_vox.hpp"

class GpuMvpBuilder
{
  public:
	GpuMvpBuilder();
	GpuMvpBuilder(const GpuMvpBuilder &other);
	~GpuMvpBuilder();
	GpuMvpBuilder &operator=(const GpuMvpBuilder &other);

	static void build(float out[16], const Camera &camera, int width,
		int height, float near_z, float far_z);

  private:
	static const float FOV_X;
	static const float HEIGHT_SCALE;

	static void view_matrix(float view[16], const Camera &camera);
	static void proj_matrix(float proj[16], int width, int height, float near_z,
		float far_z);
	static void multiply(float out[16], const float proj[16],
		const float view[16]);
};

#endif
