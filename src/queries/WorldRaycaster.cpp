#include "../../src/queries/WorldRaycaster.hpp"
#include "../../src/world/World.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
    struct RayHit
    {
        bool found;
        double distance;
        int32_t x;
        int32_t y;
        int32_t z;
        uint32_t block_id;
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
        RayHit result = {false, std::numeric_limits<double>::infinity(), 0, 0, 0, 0U};
        return (result);
    }

    RayHit raycast_solid_range(const World &world, double ox, double oy, double oz,
                               double dx, double dy, double dz, double start_distance,
                               double end_distance)
    {
        RayHit result = empty_hit();
        int32_t tx = static_cast<int32_t>(std::floor(ox + dx * start_distance));
        int32_t ty = static_cast<int32_t>(std::floor(oy + dy * start_distance));
        int32_t tz = static_cast<int32_t>(std::floor(oz + dz * start_distance));
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
        const double point_x = ox + dx * start_distance;
        const double point_y = oy + dy * start_distance;
        const double point_z = oz + dz * start_distance;
        double next_x = dx < 0.0 ? (point_x - static_cast<double>(tx)) * delta_x
                                 : (static_cast<double>(tx + 1) - point_x) * delta_x;
        double next_y = dy < 0.0 ? (point_y - static_cast<double>(ty)) * delta_y
                                 : (static_cast<double>(ty + 1) - point_y) * delta_y;
        double next_z = dz < 0.0 ? (point_z - static_cast<double>(tz)) * delta_z
                                 : (static_cast<double>(tz + 1) - point_z) * delta_z;
        double distance = start_distance;
        uint32_t block_id;

        while (distance <= end_distance)
        {
            if (WorldBlockQuery::block_id_at(world, tx, ty, tz, &block_id)
                && block_id != GAME_VOXEL_AIR_BLOCK)
            {
                result.found = true;
                result.distance = distance;
                result.x = tx;
                result.y = ty;
                result.z = tz;
                result.block_id = block_id;
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

    class RaycastWorkerPool
    {
      private:
        struct Batch
        {
            std::mutex mutex;
            std::condition_variable condition;
            std::size_t remaining;
        };

        struct Job
        {
            const World *world;
            double origin_x;
            double origin_y;
            double origin_z;
            double direction_x;
            double direction_y;
            double direction_z;
            double start_distance;
            double end_distance;
            RayHit *result;
            Batch *batch;
        };

        std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<Job> jobs_;
        std::vector<std::thread> workers_;
        bool stopping_;

        void worker_entry()
        {
            while (true)
            {
                Job job;
                {
                    std::unique_lock<std::mutex> lock(this->mutex_);
                    this->condition_.wait(lock, [this]()
                    {
                        return (this->stopping_ || !this->jobs_.empty());
                    });
                    if (this->stopping_ && this->jobs_.empty())
                        return ;
                    job = this->jobs_.front();
                    this->jobs_.pop_front();
                }
                *job.result = raycast_solid_range(*job.world, job.origin_x, job.origin_y,
                    job.origin_z, job.direction_x, job.direction_y, job.direction_z,
                    job.start_distance, job.end_distance);
                {
                    std::lock_guard<std::mutex> lock(job.batch->mutex);
                    job.batch->remaining -= 1U;
                }
                job.batch->condition.notify_one();
            }
        }

        void ensure_started()
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            if (!this->workers_.empty())
                return ;
            std::size_t index = 0U;
            while (index < 4U)
            {
                this->workers_.emplace_back(&RaycastWorkerPool::worker_entry, this);
                index += 1U;
            }
        }

      public:
        RaycastWorkerPool() : mutex_(), condition_(), jobs_(), workers_(), stopping_(false)
        {
        }

        ~RaycastWorkerPool()
        {
            {
                std::lock_guard<std::mutex> lock(this->mutex_);
                this->stopping_ = true;
            }
            this->condition_.notify_all();
            for (std::thread &worker : this->workers_)
            {
                if (worker.joinable())
                    worker.join();
            }
        }

        void run(const World &world, double origin_x, double origin_y, double origin_z,
                 double direction_x, double direction_y, double direction_z,
                 double max_distance, std::array<RayHit, 4U> &results)
        {
            this->ensure_started();
            Batch batch;
            batch.remaining = 4U;
            std::size_t index = 0U;
            {
                std::lock_guard<std::mutex> lock(this->mutex_);
                while (index < 4U)
                {
                    const double start = max_distance * static_cast<double>(index) / 4.0;
                    const double end = max_distance * static_cast<double>(index + 1U) / 4.0;
                    results[index] = empty_hit();
                    this->jobs_.push_back({&world, origin_x, origin_y, origin_z,
                        direction_x, direction_y, direction_z, start, end,
                        &results[index], &batch});
                    index += 1U;
                }
            }
            this->condition_.notify_all();
            std::unique_lock<std::mutex> lock(batch.mutex);
            batch.condition.wait(lock, [&batch]()
            {
                return (batch.remaining == 0U);
            });
        }
    };

    RaycastWorkerPool &raycast_worker_pool()
    {
        static RaycastWorkerPool pool;
        return (pool);
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
    std::array<RayHit, 4U> hits;
    if (max_distance >= 4.0)
        raycast_worker_pool().run(world, origin_x, origin_y, origin_z, direction_x, direction_y,
                                  direction_z, max_distance, hits);
    else
        hits[0] = raycast_solid_range(world, origin_x, origin_y, origin_z, direction_x,
                                      direction_y, direction_z, 0.0, max_distance);
    const std::size_t worker_count = max_distance >= 4.0 ? 4U : 1U;
    std::size_t index = 0U;
    RayHit best = empty_hit();
    index = 0U;
    while (index < worker_count)
    {
        if (hits[index].found && (!best.found || hits[index].distance < best.distance))
            best = hits[index];
        index += 1U;
    }
    if (!best.found)
        return (FT_ERR_INVALID_ARGUMENT);
    *block_x = best.x;
    *block_y = best.y;
    *block_z = best.z;
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

    while (distance <= max_distance)
    {
        if (!(tx == prev_x && ty == prev_y && tz == prev_z)
            && WorldBlockQuery::block_id_at(world, tx, ty, tz, &block_id)
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
