#include "../../src/coordinates/WorldCoordinates.hpp"

const int WorldCoordinates::STREAM_CHUNK_RADIUS = 10;
const int WorldCoordinates::CACHE_CHUNK_RADIUS = 12;
const int WorldCoordinates::CHUNK_RADIUS = 12;
const int WorldCoordinates::MIN_RENDER_DISTANCE = 14;
const int WorldCoordinates::REQUIRED_VISIBLE_DISTANCE = 160;

WorldCoordinates::WorldCoordinates()
{
}
WorldCoordinates::WorldCoordinates(const WorldCoordinates &other)
{
    (void)other;
}
WorldCoordinates::~WorldCoordinates()
{
}
WorldCoordinates &WorldCoordinates::operator=(const WorldCoordinates &other)
{
    (void)other;
    return *this;
}

int32_t WorldCoordinates::floor_divide(int32_t value, int32_t divisor)
{
    if (value >= 0)
        return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

int32_t WorldCoordinates::positive_modulo(int32_t value, int32_t divisor)
{
    int32_t result = value % divisor;
    if (result < 0)
        result += divisor;
    return result;
}

int32_t WorldCoordinates::clamp_int(int32_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

int32_t WorldCoordinates::chunk_distance_squared(int32_t chunk_x, int32_t chunk_z,
                                                 int32_t center_chunk_x, int32_t center_chunk_z)
{
    int32_t dx = chunk_x - center_chunk_x;
    int32_t dz = chunk_z - center_chunk_z;
    return (dx * dx) + (dz * dz);
}

int32_t WorldCoordinates::render_distance_to_chunk_radius(int32_t render_distance)
{
    int32_t clamped = clamp_int(render_distance, MIN_RENDER_DISTANCE,
                                CACHE_CHUNK_RADIUS * GAME_VOXEL_CHUNK_WIDTH);
    int32_t chunk_radius = (clamped + (GAME_VOXEL_CHUNK_WIDTH / 2) +
                            GAME_VOXEL_CHUNK_WIDTH - 1) /
                           GAME_VOXEL_CHUNK_WIDTH;
    if (chunk_radius < 1)
        chunk_radius = 1;
    if (chunk_radius > CACHE_CHUNK_RADIUS)
        chunk_radius = CACHE_CHUNK_RADIUS;
    return chunk_radius;
}
