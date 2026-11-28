#include "../../src/queries/WorldRaycaster.hpp"
#include "../../src/world/World.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    struct RayHit
    {
        bool found;
        int32_t x;
        int32_t y;
        int32_t z;
    };

    struct RayBlockLookup
    {
        const World &world;
        const WorldChunk *chunk = nullptr;
        int32_t first_x = 0;
        int32_t first_z = 0;
        bool have_column = false;

        bool block_at(int32_t x, int32_t y, int32_t z, uint32_t &block_id)
        {
            if (y < 0 || y >= GAME_VOXEL_CHUNK_HEIGHT)
                return (false);
            /* One ray can cross many cells before leaving the chunk. Reuse
             * its lookup, including missing chunks, under the caller's lock. */
            if (!have_column || x < first_x || x > first_x + (GAME_VOXEL_CHUNK_WIDTH - 1)
                || z < first_z || z > first_z + (GAME_VOXEL_CHUNK_DEPTH - 1))
            {
                const int32_t chunk_x = WorldCoordinates::floor_divide(x, GAME_VOXEL_CHUNK_WIDTH);
                const int32_t chunk_z = WorldCoordinates::floor_divide(z, GAME_VOXEL_CHUNK_DEPTH);
                first_x = chunk_x * GAME_VOXEL_CHUNK_WIDTH;
                first_z = chunk_z * GAME_VOXEL_CHUNK_DEPTH;
                chunk = world.find_chunk(chunk_x, chunk_z);
                have_column = true;
            }
            return (chunk != nullptr && chunk->chunk.read_block(x - first_x, y,
                z - first_z, &block_id) == FT_ERR_SUCCESS);
        }
    };

    bool valid_ray(double ox, double oy, double oz, double dx, double dy, double dz,
                   double max_distance)
    {
        return (std::isfinite(ox) && std::isfinite(oy) && std::isfinite(oz)
            && std::isfinite(dx) && std::isfinite(dy) && std::isfinite(dz)
            && std::isfinite(max_distance) && max_distance > 0.0
            && !(std::fpclassify(dx) == FP_ZERO && std::fpclassify(dy) == FP_ZERO
                && std::fpclassify(dz) == FP_ZERO));
    }

    RayHit empty_hit()
    {
        RayHit result = {false, 0, 0, 0};
        return (result);
    }

    RayHit raycast_solid_range(const World &world, double ox, double oy, double oz,
                               double dx, double dy, double dz, double max_distance)
    {
        RayHit result = empty_hit();
        int32_t tx = static_cast<int32_t>(std::floor(ox));
        int32_t ty = static_cast<int32_t>(std::floor(oy));
        int32_t tz = static_cast<int32_t>(std::floor(oz));
        const int32_t step_x = dx < 0.0 ? -1 : 1;
        const int32_t step_y = dy < 0.0 ? -1 : 1;
        const int32_t step_z = dz < 0.0 ? -1 : 1;
        const bool zero_x = std::fpclassify(dx) == FP_ZERO;
        const bool zero_y = std::fpclassify(dy) == FP_ZERO;
        const bool zero_z = std::fpclassify(dz) == FP_ZERO;
        const double inf = std::numeric_limits<double>::infinity();
        const double delta_x = zero_x ? inf : std::abs(1.0 / dx);
        const double delta_y = zero_y ? inf : std::abs(1.0 / dy);
        const double delta_z = zero_z ? inf : std::abs(1.0 / dz);
        double next_x = dx < 0.0 ? (ox - static_cast<double>(tx)) * delta_x
                                 : (static_cast<double>(tx + 1) - ox) * delta_x;
        double next_y = dy < 0.0 ? (oy - static_cast<double>(ty)) * delta_y
                                 : (static_cast<double>(ty + 1) - oy) * delta_y;
        double next_z = dz < 0.0 ? (oz - static_cast<double>(tz)) * delta_z
                                 : (static_cast<double>(tz + 1) - oz) * delta_z;
        double distance = 0.0;
        uint32_t block_id;
        RayBlockLookup lookup{world};

        while (distance <= max_distance)
        {
            if (lookup.block_at(tx, ty, tz, block_id)
                && block_id != GAME_VOXEL_AIR_BLOCK)
            {
                result.found = true;
                result.x = tx;
                result.y = ty;
                result.z = tz;
                return (result);
            }
            const double next_distance = std::min(next_x, std::min(next_y, next_z));
            const double tie_epsilon = 1.0e-12;
            const bool step_x_now = next_x <= next_distance + tie_epsilon;
            const bool step_y_now = next_y <= next_distance + tie_epsilon;
            const bool step_z_now = next_z <= next_distance + tie_epsilon;
            distance = next_distance;
            if (step_x_now)
            {
                tx += step_x;
                next_x += delta_x;
            }
            if (step_y_now)
            {
                ty += step_y;
                next_y += delta_y;
            }
            if (step_z_now)
            {
                tz += step_z;
                next_z += delta_z;
            }
        }
        return (result);
    }
}

WorldRaycaster::WorldRaycaster()
{
}

WorldRaycaster::WorldRaycaster(const WorldRaycaster &other)
{
	*this = other;
}

WorldRaycaster::~WorldRaycaster()
{
}

WorldRaycaster &WorldRaycaster::operator=(const WorldRaycaster &other)
{
	(void)other;
	return (*this);
}

int32_t WorldRaycaster::raycast_solid(const World &world, double origin_x, double origin_y,
                                      double origin_z, double direction_x, double direction_y,
                                      double direction_z, double max_distance, int32_t *block_x,
                                      int32_t *block_y, int32_t *block_z)
{
    if (block_x == nullptr || block_y == nullptr || block_z == nullptr
        || !valid_ray(origin_x, origin_y, origin_z, direction_x, direction_y, direction_z,
                      max_distance))
        return (FT_ERR_INVALID_ARGUMENT);
    // Walk in hit order and stop at the first obstruction. Short visibility
    // and interaction rays do only a few dozen steps even when they miss.
    const RayHit hit = raycast_solid_range(world, origin_x, origin_y, origin_z,
        direction_x, direction_y, direction_z, max_distance);
    if (!hit.found)
        return (FT_ERR_INVALID_ARGUMENT);
    *block_x = hit.x;
    *block_y = hit.y;
    *block_z = hit.z;
    return (FT_ERR_SUCCESS);
}

int32_t WorldRaycaster::raycast_edit_target(const World &world, double origin_x,
	double origin_y, double origin_z, double direction_x, double direction_y,
	double direction_z, double max_distance, int32_t *hit_x, int32_t *hit_y,
	int32_t *hit_z, int32_t *place_x, int32_t *place_y, int32_t *place_z,
	uint32_t *hit_id)
{
    if (hit_x == nullptr || hit_y == nullptr || hit_z == nullptr || place_x == nullptr
        || place_y == nullptr || place_z == nullptr || hit_id == nullptr
        || !valid_ray(origin_x, origin_y, origin_z, direction_x, direction_y, direction_z,
                      max_distance))
        return (FT_ERR_INVALID_ARGUMENT);
    int32_t tx = static_cast<int32_t>(std::floor(origin_x));
    int32_t ty = static_cast<int32_t>(std::floor(origin_y));
    int32_t tz = static_cast<int32_t>(std::floor(origin_z));
    int32_t prev_x = tx;
    int32_t prev_y = ty;
    int32_t prev_z = tz;
    const int32_t step_x = direction_x < 0.0 ? -1 : 1;
    const int32_t step_y = direction_y < 0.0 ? -1 : 1;
    const int32_t step_z = direction_z < 0.0 ? -1 : 1;
    const bool zero_x = std::fpclassify(direction_x) == FP_ZERO;
    const bool zero_y = std::fpclassify(direction_y) == FP_ZERO;
    const bool zero_z = std::fpclassify(direction_z) == FP_ZERO;
    const double inf = std::numeric_limits<double>::infinity();
    const double delta_x = zero_x ? inf : std::abs(1.0 / direction_x);
    const double delta_y = zero_y ? inf : std::abs(1.0 / direction_y);
    const double delta_z = zero_z ? inf : std::abs(1.0 / direction_z);
    double next_x = direction_x < 0.0 ? (origin_x - static_cast<double>(tx)) * delta_x
                                      : (static_cast<double>(tx + 1) - origin_x) * delta_x;
    double next_y = direction_y < 0.0 ? (origin_y - static_cast<double>(ty)) * delta_y
                                      : (static_cast<double>(ty + 1) - origin_y) * delta_y;
    double next_z = direction_z < 0.0 ? (origin_z - static_cast<double>(tz)) * delta_z
                                      : (static_cast<double>(tz + 1) - origin_z) * delta_z;
    double distance = 0.0;
    uint32_t block_id;
    RayBlockLookup lookup{world};

    while (distance <= max_distance)
    {
        if (!(tx == prev_x && ty == prev_y && tz == prev_z)
            && lookup.block_at(tx, ty, tz, block_id)
            && block_id != GAME_VOXEL_AIR_BLOCK)
        {
            *hit_x = tx;
            *hit_y = ty;
            *hit_z = tz;
            *place_x = prev_x;
            *place_y = prev_y;
            *place_z = prev_z;
            *hit_id = block_id;
            return (FT_ERR_SUCCESS);
        }
        prev_x = tx;
        prev_y = ty;
        prev_z = tz;
        const double next_distance = std::min(next_x, std::min(next_y, next_z));
        const double tie_epsilon = 1.0e-12;
        const bool step_x_now = next_x <= next_distance + tie_epsilon;
        const bool step_y_now = next_y <= next_distance + tie_epsilon;
        const bool step_z_now = next_z <= next_distance + tie_epsilon;
        distance = next_distance;
        if (step_x_now)
        {
            tx += step_x;
            next_x += delta_x;
        }
        if (step_y_now)
        {
            ty += step_y;
            next_y += delta_y;
        }
        if (step_z_now)
        {
            tz += step_z;
            next_z += delta_z;
        }
    }
    return (FT_ERR_NOT_FOUND);
}
