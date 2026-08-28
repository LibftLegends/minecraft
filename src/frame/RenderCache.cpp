#include "../../src/frame/RenderCache.hpp"

const double RenderCache::FOV_DEGREES = 40.0;
const double RenderCache::DEG_TO_RAD = 0.017453292519943295;
const double RenderCache::HEIGHT_SCALE = 0.52;

RenderCache::RenderCache()
    : yaw_cos(1.0), yaw_sin(0.0), pitch_cos(1.0), pitch_sin(0.0), fov_tangent(1.0),
      aspect_ratio(1.0), focal_length(1.0), half_width(0.0), half_height(0.0),
      render_distance(0.0)
{
    for (int i = 0; i < FRUSTUM_PLANE_COUNT; ++i)
        frustum_planes[i] = {0.0, 0.0, 0.0, 0.0};
}

RenderCache::RenderCache(const RenderCache &other)
    : yaw_cos(1.0), yaw_sin(0.0), pitch_cos(1.0), pitch_sin(0.0), fov_tangent(1.0),
      aspect_ratio(1.0), focal_length(1.0), half_width(0.0), half_height(0.0),
      render_distance(0.0)
{
    for (int i = 0; i < FRUSTUM_PLANE_COUNT; ++i)
        frustum_planes[i] = {0.0, 0.0, 0.0, 0.0};
    *this = other;
}

RenderCache::~RenderCache()
{
}

RenderCache &RenderCache::operator=(const RenderCache &other)
{
    if (this != &other)
    {
        yaw_cos = other.yaw_cos;
        yaw_sin = other.yaw_sin;
        pitch_cos = other.pitch_cos;
        pitch_sin = other.pitch_sin;
        fov_tangent = other.fov_tangent;
        aspect_ratio = other.aspect_ratio;
        focal_length = other.focal_length;
        half_width = other.half_width;
        half_height = other.half_height;
        render_distance = other.render_distance;
        for (int i = 0; i < FRUSTUM_PLANE_COUNT; ++i)
            frustum_planes[i] = other.frustum_planes[i];
    }
    return *this;
}

void RenderCache::configure(const Camera &camera, int32_t width, int32_t height,
                            int32_t active_render_distance)
{
    double w = static_cast<double>(width);
    double h = static_cast<double>(height);
    yaw_cos = std::cos(camera.yaw);
    yaw_sin = std::sin(camera.yaw);
    pitch_cos = std::cos(camera.pitch);
    pitch_sin = std::sin(camera.pitch);
    fov_tangent = std::tan(FOV_DEGREES * DEG_TO_RAD);
    aspect_ratio = (w * 0.5) / (h * HEIGHT_SCALE);
    focal_length = (w * 0.5) / fov_tangent;
    half_width = w * 0.5;
    half_height = h * HEIGHT_SCALE;
    render_distance = static_cast<double>(active_render_distance);

    const double horizontal_tangent = fov_tangent * aspect_ratio;
    const double local_planes[FRUSTUM_PLANE_COUNT][3] = {
        {0.0, 0.0, 1.0}, {0.0, 0.0, -1.0},
        {1.0, 0.0, horizontal_tangent},
        {-1.0, 0.0, horizontal_tangent},
        {0.0, 1.0, fov_tangent},
        {0.0, -1.0, fov_tangent}
    };
    for (int i = 0; i < FRUSTUM_PLANE_COUNT; ++i)
    {
        const double local_x = local_planes[i][0];
        const double local_y = local_planes[i][1];
        const double local_z = local_planes[i][2];
        frustum_planes[i].x = local_x * yaw_cos
            - local_y * yaw_sin * pitch_sin
            + local_z * yaw_sin * pitch_cos;
        frustum_planes[i].y = local_y * pitch_cos + local_z * pitch_sin;
        frustum_planes[i].z = -local_x * yaw_sin
            - local_y * yaw_cos * pitch_sin
            + local_z * yaw_cos * pitch_cos;
        frustum_planes[i].offset = -(frustum_planes[i].x * camera.x
            + frustum_planes[i].y * camera.y
            + frustum_planes[i].z * camera.z);
        if (i == 1)
            frustum_planes[i].offset += render_distance;
    }
}
