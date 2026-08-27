#include "../../src/gpur/GpuMvpBuilder.hpp"

const float GpuMvpBuilder::FOV_X = 0.83909963118f;
const float GpuMvpBuilder::HEIGHT_SCALE = 0.52f;

GpuMvpBuilder::GpuMvpBuilder()
{
}

GpuMvpBuilder::GpuMvpBuilder(const GpuMvpBuilder &other)
{
	(void)other;
}

GpuMvpBuilder::~GpuMvpBuilder()
{
}

GpuMvpBuilder &GpuMvpBuilder::operator=(const GpuMvpBuilder &other)
{
	(void)other;
	return (*this);
}

void GpuMvpBuilder::view_matrix(float view[16], const Camera &camera)
{
	double	cy;
	double	sy;
	double	cp;
	double	sp;
	double	tx;
	double	ty;
	double	tz;
	double	vx;
	double	vz;
	double	vy2;
	double	vz2;

	cy = std::cos(camera.yaw);
	sy = std::sin(camera.yaw);
	cp = std::cos(camera.pitch);
	sp = std::sin(camera.pitch);
	tx = -camera.x;
	ty = -camera.y;
	tz = -camera.z;
	vx = tx * cy - tz * sy;
	vz = tx * sy + tz * cy;
	vy2 = ty * cp - vz * sp;
	vz2 = ty * sp + vz * cp;
	std::memset(view, 0, 16 * sizeof(float));
	view[0] = static_cast<float>(cy);
	view[1] = static_cast<float>(-sy * sp);
	view[2] = static_cast<float>(sy * cp);
	view[5] = static_cast<float>(cp);
	view[6] = static_cast<float>(sp);
	view[8] = static_cast<float>(-sy);
	view[9] = static_cast<float>(-cy * sp);
	view[10] = static_cast<float>(cy * cp);
	view[12] = static_cast<float>(vx);
	view[13] = static_cast<float>(vy2);
	view[14] = static_cast<float>(vz2);
	view[15] = 1.0f;
}

void GpuMvpBuilder::proj_matrix(float proj[16], int width, int height,
	float near_z, float far_z)
{
	float	fx;
	float	fy;
	float	fn;
	float	fd;

	fx = 1.0f / FOV_X;
	fy = fx * (static_cast<float>(width) * 0.5f) / (static_cast<float>(height)
			* HEIGHT_SCALE);
	fn = far_z + near_z;
	fd = far_z - near_z;
	std::memset(proj, 0, 16 * sizeof(float));
	proj[0] = fx;
	proj[5] = fy;
	proj[10] = fn / fd;
	proj[11] = 1.0f;
	proj[14] = -2.0f * near_z * far_z / fd;
}

void GpuMvpBuilder::multiply(float out[16], const float proj[16],
	const float view[16])
{
	float	s;

	for (int col = 0; col < 4; ++col)
		for (int row = 0; row < 4; ++row)
		{
			s = 0.0f;
			for (int k = 0; k < 4; ++k)
				s += proj[k * 4 + row] * view[col * 4 + k];
			out[col * 4 + row] = s;
		}
}

void GpuMvpBuilder::build(float out[16], const Camera &camera, int width,
	int height, float near_z, float far_z)
{
	float	view[16], proj[16];

	view_matrix(view, camera);
	proj_matrix(proj, width, height, near_z, far_z);
	multiply(out, proj, view);
}
