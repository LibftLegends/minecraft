#include "../../src/world/World.hpp"

static const uint8_t WORLD_STREAM_CANDIDATE_ABSENT = 0U;
static const uint8_t WORLD_STREAM_CANDIDATE_QUEUED = 1U;
static const uint8_t WORLD_STREAM_CANDIDATE_GENERATING = 2U;
static const uint8_t WORLD_STREAM_CANDIDATE_GENERATED = 3U;
static const uint8_t WORLD_STREAM_CANDIDATE_MESHING = 4U;
static const uint8_t WORLD_STREAM_CANDIDATE_READY = 5U;
static const uint8_t WORLD_STREAM_CANDIDATE_FAILED_RETRYABLE = 6U;

World::World(const World &other) : stream_candidates_radius_(-1),
    stream_candidate_cursor_(0U), generation_credit_(0), stream_last_error_(0),
    stream_retryable_count_(0), stream_relevance_epoch_(1U),
    generation_revision_(1U), stream_frame_(0U), stream_progress_frame_(0U)
{ (void)other; }
World &World::operator=(const World &other)
{ (void)other; return (*this); }


World::World()
{
    this->chunk_count = WorldCoordinates::CHUNK_COUNT;
    this->loaded_chunk_count = 0;
    this->center_chunk_x = 0;
    this->center_chunk_z = 0;
    this->chunk_index_center_x = 0;
    this->chunk_index_center_z = 0;
    this->active_render_distance = WorldCoordinates::REQUIRED_VISIBLE_DISTANCE;
    this->stream_candidates_radius_ = -1;
    this->stream_candidate_cursor_ = 0U;
    this->generation_credit_ = 0;
    this->stream_last_error_ = FT_ERR_SUCCESS;
    this->stream_retryable_count_ = 0;
    this->stream_relevance_epoch_ = 1U;
    this->generation_revision_ = 1U;
    this->stream_frame_ = 0U;
    this->stream_progress_frame_ = 0U;
    this->seed[0] = '\0';
    terrain_default_generation_config(this->terrain_config);
    this->terrain_generation_started = false;
    this->clear_chunk_index();
}

World::~World()
{
    this->destroy();
}

void World::copy_seed(const char *seed_value)
{
    if (seed_value == nullptr)
        seed_value = "";
    std::strncpy(this->seed, seed_value, sizeof(this->seed) - 1U);
    this->seed[sizeof(this->seed) - 1U] = '\0';
}

void World::clear_chunk_index()
{
    int32_t index;

    index = 0;
    while (index < WorldCoordinates::CHUNK_COUNT)
    {
        this->chunk_index[index] = nullptr;
        index = index + 1;
    }
    this->chunk_index_valid = false;
}

void World::rebuild_chunk_index()
{
    int32_t index;
    int32_t slot_x;
    int32_t slot_z;
    int32_t grid_x;
    int32_t grid_z;
    int32_t grid_width;

    this->clear_chunk_index();
    grid_width = WorldCoordinates::CACHE_CHUNK_RADIUS * 2 + 1;
    index = 0;
    while (index < this->chunk_count)
    {
        if (this->chunks[index].initialized == true)
        {
            slot_x = this->chunks[index].chunk_x - this->center_chunk_x;
            slot_z = this->chunks[index].chunk_z - this->center_chunk_z;
            if (slot_x >= -WorldCoordinates::CACHE_CHUNK_RADIUS &&
                slot_x <= WorldCoordinates::CACHE_CHUNK_RADIUS &&
                slot_z >= -WorldCoordinates::CACHE_CHUNK_RADIUS &&
                slot_z <= WorldCoordinates::CACHE_CHUNK_RADIUS)
            {
                grid_x = slot_x + WorldCoordinates::CACHE_CHUNK_RADIUS;
                grid_z = slot_z + WorldCoordinates::CACHE_CHUNK_RADIUS;
                this->chunk_index[(grid_z * grid_width) + grid_x] = &this->chunks[index];
            }
        }
        index = index + 1;
    }
    this->chunk_index_center_x = this->center_chunk_x;
    this->chunk_index_center_z = this->center_chunk_z;
    this->chunk_index_valid = true;
}

void World::register_chunk_index(const WorldChunk &chunk)
{
    int32_t slot_x;
    int32_t slot_z;
    int32_t grid_width;

    if (this->chunk_index_valid == false)
        return;
    slot_x = chunk.chunk_x - this->chunk_index_center_x;
    slot_z = chunk.chunk_z - this->chunk_index_center_z;
    if (slot_x < -WorldCoordinates::CACHE_CHUNK_RADIUS ||
        slot_x > WorldCoordinates::CACHE_CHUNK_RADIUS ||
        slot_z < -WorldCoordinates::CACHE_CHUNK_RADIUS ||
        slot_z > WorldCoordinates::CACHE_CHUNK_RADIUS)
        return;
    grid_width = WorldCoordinates::CACHE_CHUNK_RADIUS * 2 + 1;
    this->chunk_index[((slot_z + WorldCoordinates::CACHE_CHUNK_RADIUS) * grid_width) +
                      (slot_x + WorldCoordinates::CACHE_CHUNK_RADIUS)] =
        const_cast<WorldChunk *>(&chunk);
}

static void world_remesh_loaded_neighbor(World *world, int32_t chunk_x, int32_t chunk_z)
{
    if (world == nullptr)
        return;
    if (world->find_chunk(chunk_x, chunk_z) == nullptr)
        return;
    (void)WorldChunkLoader::remesh_chunk(world->chunks, world->chunk_count, chunk_x, chunk_z, true);
}

int32_t World::initialize(const char *seed_value)
{
    return (this->initialize(seed_value, nullptr));
}

int32_t World::initialize(const char *seed_value,
                          const char *terrain_config_file_path)
{
    int32_t index;
    int32_t error_code;

    this->destroy();
    this->chunk_count = WorldCoordinates::CHUNK_COUNT;
    this->loaded_chunk_count = 0;
    this->center_chunk_x = 0;
    this->center_chunk_z = 0;
    this->active_render_distance = WorldCoordinates::REQUIRED_VISIBLE_DISTANCE;
    this->generation_credit_ = 0;
    this->stream_last_error_ = FT_ERR_SUCCESS;
    this->stream_retryable_count_ = 0;
    this->stream_frame_ = 0U;
    this->stream_progress_frame_ = 0U;
    this->clear_chunk_index();
    this->copy_seed(seed_value);
    if (terrain_config_file_path != nullptr)
    {
        error_code = this->load_terrain_config(terrain_config_file_path);
        if (error_code != FT_ERR_SUCCESS)
            return (error_code);
    }
    error_code = terrain_generation_context_initialize(this->terrain_context,
                                                       this->terrain_config);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    this->terrain_generation_started = true;
    index = 0;
    while (index < this->chunk_count)
    {
        this->chunks[index].reset_coordinates();
        this->chunks[index].initialized = false;
        index = index + 1;
    }
    // Keep first launch bounded. The game loop keeps streaming chunks after init,
    // so we only need a small starter set here instead of filling the whole ring.
    error_code =
        this->update_around(0.0, 0.0, 64, this->active_render_distance);
    if (error_code != FT_ERR_SUCCESS)
        this->destroy();
    return (error_code);
}

void World::prepare_stream_candidates(int32_t stream_radius)
{
    if (this->stream_candidates_radius_ == stream_radius)
        return;
    this->stream_candidates_.clear();
    this->stream_candidate_cursor_ = 0U;
    this->stream_candidates_.reserve(static_cast<size_t>((stream_radius * 2 + 1) *
                                                         (stream_radius * 2 + 1)));
    const int32_t radius_sq = stream_radius * stream_radius;
    for (int32_t z = -stream_radius; z <= stream_radius; ++z)
    {
        for (int32_t x = -stream_radius; x <= stream_radius; ++x)
        {
            const int32_t dist_sq = (x * x) + (z * z);
            if (dist_sq <= radius_sq)
                this->stream_candidates_.push_back({x, z, dist_sq,
                    WORLD_STREAM_CANDIDATE_ABSENT, 0U, 0, FT_ERR_SUCCESS,
                    this->stream_relevance_epoch_, this->generation_revision_,
                    0U});
        }
    }
    std::sort(this->stream_candidates_.begin(), this->stream_candidates_.end(),
              [](const StreamCandidate &a, const StreamCandidate &b) -> bool
              {
                  if (a.dist_sq != b.dist_sq)
                      return a.dist_sq < b.dist_sq;
                  const int32_t a_manhattan = std::abs(a.offset_x) + std::abs(a.offset_z);
                  const int32_t b_manhattan = std::abs(b.offset_x) + std::abs(b.offset_z);
                  if (a_manhattan != b_manhattan)
                      return a_manhattan < b_manhattan;
                  if (a.offset_z != b.offset_z)
                      return a.offset_z < b.offset_z;
                  return a.offset_x < b.offset_x;
              });
    this->stream_candidates_radius_ = stream_radius;
    this->stream_retryable_count_ = 0;
    this->stream_relevance_epoch_ += 1U;
    this->generation_revision_ += 1U;
}

void World::set_terrain_config(const terrain_generation_config &config)
{
    if (this->terrain_generation_started)
        return;
    this->terrain_config.initialize(config);
}

const terrain_generation_config &World::terrain_generation_settings() const
{
    return (this->terrain_config);
}

int32_t World::load_terrain_config(const char *file_path)
{
    if (file_path == nullptr)
        return (FT_ERR_INVALID_ARGUMENT);
    if (this->terrain_generation_started)
        return (FT_ERR_INVALID_OPERATION);
    return (terrain_generation_config_load_file(file_path,
                                                this->terrain_config));
}

int32_t World::save_terrain_config(const char *file_path) const
{
    const terrain_generation_config *config_to_save;

    if (file_path == nullptr)
        return (FT_ERR_INVALID_ARGUMENT);
    config_to_save = &this->terrain_config;
    if (this->terrain_generation_started)
        config_to_save = &this->terrain_context.config();
    return (terrain_generation_config_save_file(file_path,
                                                *config_to_save));
}

void World::destroy()
{
    int32_t index;

    index = 0;
    while (index < this->chunk_count)
    {
        this->chunks[index].destroy();
        index = index + 1;
    }
    this->loaded_chunk_count = 0;
    this->clear_chunk_index();
    this->stream_candidates_radius_ = -1;
    this->stream_candidate_cursor_ = 0U;
    this->generation_credit_ = 0;
    this->stream_last_error_ = FT_ERR_SUCCESS;
    this->stream_retryable_count_ = 0;
    (void)this->terrain_context.destroy();
    this->terrain_generation_started = false;
}

int32_t World::update_around(double camera_x, double camera_z, int32_t generation_budget)
{
    return (this->update_around(camera_x, camera_z, generation_budget,
                                WorldCoordinates::REQUIRED_VISIBLE_DISTANCE));
}

int32_t World::try_load_chunk_at(int32_t chunk_x, int32_t chunk_z)
{
    WorldChunk *slot;
    int32_t error_code;

    if (this->find_chunk(chunk_x, chunk_z) != nullptr)
        return (0);
    slot = WorldChunkStore::find_free_chunk_slot(this->chunks, this->chunk_count);
    if (slot == nullptr)
        return (FT_ERR_NO_MEMORY);
    error_code = WorldChunkLoader::initialize_chunk(slot, chunk_x, chunk_z, this->seed,
                                                    this->chunks, this->chunk_count,
                                                    this->terrain_context.config());
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    this->loaded_chunk_count = this->loaded_chunk_count + 1;
    this->register_chunk_index(*slot);
    world_remesh_loaded_neighbor(this, chunk_x - 1, chunk_z);
    world_remesh_loaded_neighbor(this, chunk_x + 1, chunk_z);
    world_remesh_loaded_neighbor(this, chunk_x, chunk_z - 1);
    world_remesh_loaded_neighbor(this, chunk_x, chunk_z + 1);
    return (1);
}

int32_t World::stream_chunks(int32_t stream_radius, int32_t budget, int32_t *generated)
{
    int32_t result;
    this->prepare_stream_candidates(stream_radius);
    size_t candidate_count;
    size_t scanned_count;

    candidate_count = this->stream_candidates_.size();
    scanned_count = 0U;
    while (scanned_count < candidate_count)
    {
        StreamCandidate &candidate = this->stream_candidates_[
            this->stream_candidate_cursor_];
        this->stream_candidate_cursor_ = (this->stream_candidate_cursor_ + 1U)
            % candidate_count;
        scanned_count += 1U;
        if (candidate.retry_frames > 0)
        {
            candidate.retry_frames -= 1;
            continue;
        }
        if (candidate.state == WORLD_STREAM_CANDIDATE_READY)
            continue;
        if (candidate.relevance_epoch != this->stream_relevance_epoch_
            || candidate.generation_revision != this->generation_revision_)
        {
            candidate.state = WORLD_STREAM_CANDIDATE_ABSENT;
            candidate.retry_count = 0U;
            candidate.retry_frames = 0;
            candidate.last_error = FT_ERR_SUCCESS;
            candidate.relevance_epoch = this->stream_relevance_epoch_;
            candidate.generation_revision = this->generation_revision_;
            candidate.queued_frame = 0U;
        }
        candidate.state = WORLD_STREAM_CANDIDATE_QUEUED;
        if (candidate.queued_frame == 0U)
            candidate.queued_frame = this->stream_frame_;
        candidate.state = WORLD_STREAM_CANDIDATE_GENERATING;
        result = this->try_load_chunk_at(this->center_chunk_x + candidate.offset_x,
                                         this->center_chunk_z + candidate.offset_z);
        if (result < 0)
        {
            candidate.state = WORLD_STREAM_CANDIDATE_FAILED_RETRYABLE;
            candidate.retry_count += 1U;
            candidate.last_error = result;
            candidate.retry_frames = 1 << std::min(candidate.retry_count, 6U);
            this->stream_last_error_ = result;
            this->stream_retryable_count_ += 1;
            continue;
        }
        if (result > 0)
        {
            candidate.state = WORLD_STREAM_CANDIDATE_GENERATED;
            candidate.state = WORLD_STREAM_CANDIDATE_MESHING;
            candidate.state = WORLD_STREAM_CANDIDATE_READY;
            candidate.retry_count = 0U;
            candidate.retry_frames = 0;
            candidate.last_error = FT_ERR_SUCCESS;
            candidate.queued_frame = 0U;
            this->stream_progress_frame_ = this->stream_frame_;
            *generated = *generated + 1;
            if (budget > 0 && *generated >= budget)
                return (FT_ERR_SUCCESS);
        }
    }
    return (FT_ERR_SUCCESS);
}

int32_t World::stream_last_error() const
{
    return (this->stream_last_error_);
}

int32_t World::stream_retryable_count() const
{
    return (this->stream_retryable_count_);
}

World::StreamDiagnostics World::stream_diagnostics() const
{
    StreamDiagnostics diagnostics;
    size_t candidate_index;

    diagnostics.frame = this->stream_frame_;
    diagnostics.progress_frame = this->stream_progress_frame_;
    diagnostics.candidate_count = this->stream_candidates_.size();
    diagnostics.ready_count = 0U;
    diagnostics.pending_count = 0U;
    diagnostics.retryable_count = 0U;
    diagnostics.failed_count = 0U;
    diagnostics.oldest_pending_age = 0U;
    diagnostics.last_error = this->stream_last_error_;
    candidate_index = 0U;
    while (candidate_index < this->stream_candidates_.size())
    {
        const StreamCandidate &candidate =
            this->stream_candidates_[candidate_index];
        if (candidate.state == WORLD_STREAM_CANDIDATE_READY)
            diagnostics.ready_count++;
        else if (candidate.state == WORLD_STREAM_CANDIDATE_FAILED_RETRYABLE)
        {
            diagnostics.retryable_count++;
            diagnostics.pending_count++;
        }
        else if (candidate.state != WORLD_STREAM_CANDIDATE_ABSENT)
            diagnostics.pending_count++;
        if (candidate.queued_frame != 0U
            && this->stream_frame_ >= candidate.queued_frame)
        {
            uint64_t age = this->stream_frame_ - candidate.queued_frame;
            if (age > diagnostics.oldest_pending_age)
                diagnostics.oldest_pending_age = age;
        }
        if (candidate.last_error != FT_ERR_SUCCESS
            && candidate.state == WORLD_STREAM_CANDIDATE_FAILED_RETRYABLE)
            diagnostics.failed_count++;
        candidate_index++;
    }
    return (diagnostics);
}

int32_t World::update_around(double camera_x, double camera_z, int32_t generation_budget,
                             int32_t render_distance)
{
    int32_t stream_radius;
    int32_t generated;

    this->stream_frame_++;
    this->center_chunk_x = WorldCoordinates::floor_divide(
        static_cast<int32_t>(std::floor(camera_x)), GAME_VOXEL_CHUNK_WIDTH);
    this->center_chunk_z = WorldCoordinates::floor_divide(
        static_cast<int32_t>(std::floor(camera_z)), GAME_VOXEL_CHUNK_DEPTH);
    this->active_render_distance =
        WorldCoordinates::clamp_int(render_distance, WorldCoordinates::MIN_RENDER_DISTANCE,
                                    WorldCoordinates::CACHE_CHUNK_RADIUS * GAME_VOXEL_CHUNK_WIDTH);
    const bool center_changed = this->chunk_index_center_x != this->center_chunk_x ||
        this->chunk_index_center_z != this->center_chunk_z;
    if (this->chunk_index_valid == false || center_changed)
    {
        WorldChunkStore::evict_far_chunks(this->chunks, this->chunk_count,
                                          &this->loaded_chunk_count, this->center_chunk_x,
                                          this->center_chunk_z);
        this->rebuild_chunk_index();
    }
    if (center_changed)
    {
        this->stream_relevance_epoch_ += 1U;
        this->stream_candidate_cursor_ = 0U;
        for (StreamCandidate &candidate : this->stream_candidates_)
        {
            candidate.state = WORLD_STREAM_CANDIDATE_ABSENT;
            candidate.retry_count = 0U;
            candidate.retry_frames = 0;
            candidate.last_error = FT_ERR_SUCCESS;
            candidate.relevance_epoch = this->stream_relevance_epoch_;
            candidate.generation_revision = this->generation_revision_;
            candidate.queued_frame = 0U;
        }
        this->stream_retryable_count_ = 0;
    }
    stream_radius = WorldCoordinates::render_distance_to_chunk_radius(this->active_render_distance);
    generated = 0;
    if (generation_budget <= 0)
    {
        if (this->generation_credit_ < 4)
            this->generation_credit_ = this->generation_credit_ + 1;
        if (this->generation_credit_ < 4)
            return (this->stream_chunks(stream_radius, 0, &generated));
        this->generation_credit_ = 0;
        generation_budget = 1;
    }
    else
        this->generation_credit_ = 0;
    return (this->stream_chunks(stream_radius, generation_budget, &generated));
}

const WorldChunk *World::find_chunk(int32_t chunk_x, int32_t chunk_z) const
{
    int32_t slot_x;
    int32_t slot_z;
    int32_t grid_width;

    slot_x = chunk_x - this->center_chunk_x;
    slot_z = chunk_z - this->center_chunk_z;
    if (slot_x < -WorldCoordinates::CACHE_CHUNK_RADIUS ||
        slot_x > WorldCoordinates::CACHE_CHUNK_RADIUS ||
        slot_z < -WorldCoordinates::CACHE_CHUNK_RADIUS ||
        slot_z > WorldCoordinates::CACHE_CHUNK_RADIUS)
        return (nullptr);
    grid_width = WorldCoordinates::CACHE_CHUNK_RADIUS * 2 + 1;
    return (this->chunk_index[((slot_z + WorldCoordinates::CACHE_CHUNK_RADIUS) * grid_width) +
                              (slot_x + WorldCoordinates::CACHE_CHUNK_RADIUS)]);
}

WorldChunk *World::find_chunk_mutable(int32_t chunk_x, int32_t chunk_z)
{
    return (
        const_cast<WorldChunk *>(static_cast<const World *>(this)->find_chunk(chunk_x, chunk_z)));
}

bool World::validate_visible_distance(double camera_x, double camera_z, double yaw,
                                      int32_t required_distance) const
{
    return (WorldVisibilityValidator::validate_visible_distance(*this, camera_x, camera_z, yaw,
                                                                required_distance));
}

bool World::surface_top_at(int32_t world_x, int32_t world_z, double *surface_top) const
{
    return (WorldBlockQuery::surface_top_at(*this, world_x, world_z, surface_top));
}

bool World::solid_block_at(int32_t world_x, int32_t world_y, int32_t world_z) const
{
    return (WorldBlockQuery::solid_block_at(*this, world_x, world_y, world_z));
}

bool World::block_id_at(int32_t world_x, int32_t world_y, int32_t world_z, uint32_t *block_id) const
{
    return (WorldBlockQuery::block_id_at(*this, world_x, world_y, world_z, block_id));
}

int32_t World::delete_block_at(int32_t world_x, int32_t world_y, int32_t world_z)
{
    return (WorldBlockEditor::delete_block_at(*this, world_x, world_y, world_z));
}

int32_t World::place_block_at(int32_t world_x, int32_t world_y, int32_t world_z, uint32_t block_id)
{
    return (WorldBlockEditor::place_block_at(*this, world_x, world_y, world_z, block_id));
}

int32_t World::raycast_solid(double origin_x, double origin_y, double origin_z, double direction_x,
                             double direction_y, double direction_z, double max_distance,
                             int32_t *block_x, int32_t *block_y, int32_t *block_z) const
{
    return (WorldRaycaster::raycast_solid(*this, origin_x, origin_y, origin_z, direction_x,
                                          direction_y, direction_z, max_distance, block_x, block_y,
                                          block_z));
}

int32_t World::raycast_edit_target(double origin_x, double origin_y, double origin_z,
                                   double direction_x, double direction_y, double direction_z,
                                   double max_distance, int32_t *hit_block_x, int32_t *hit_block_y,
                                   int32_t *hit_block_z, int32_t *place_block_x,
                                   int32_t *place_block_y, int32_t *place_block_z,
                                   uint32_t *hit_block_id) const
{
    return (WorldRaycaster::raycast_edit_target(*this, origin_x, origin_y, origin_z, direction_x,
                                                direction_y, direction_z, max_distance, hit_block_x,
                                                hit_block_y, hit_block_z, place_block_x,
                                                place_block_y, place_block_z, hit_block_id));
}
