#ifndef RENDER_CACHE_HPP
# define RENDER_CACHE_HPP

# include "../../src/camera/Camera.hpp"
# include "../ft_vox.hpp"

class RenderCache
{
  public:
    struct FrustumPlane
    {
        double x;
        double y;
        double z;
        double offset;
    };

    static const int FRUSTUM_PLANE_COUNT = 6;
    double yaw_cos;
    double yaw_sin;
    double pitch_cos;
    double pitch_sin;
    double fov_tangent;
    double aspect_ratio;
    double focal_length;
    double half_width;
    double half_height;
    double render_distance;
    FrustumPlane frustum_planes[FRUSTUM_PLANE_COUNT];

	RenderCache();
	RenderCache(const RenderCache &other);
	~RenderCache();
	RenderCache &operator=(const RenderCache &other);

	void configure(const Camera &camera, int32_t width, int32_t height,
		int32_t active_render_distance);

  private:
	static const double FOV_DEGREES;
	static const double DEG_TO_RAD;
	static const double HEIGHT_SCALE;
};

#endif
