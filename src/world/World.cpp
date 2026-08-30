#include "../../src/world/World.hpp"

World::World() : chunk_streamer_storage_(new WorldChunkStreamer(*this)),
	revision_manager_storage_(new WorldRevisionManager(*this)),
	chunk_streamer(*chunk_streamer_storage_.get()),
	revision_manager(*revision_manager_storage_.get())
{
	this->chunk_count = WorldCoordinates::CHUNK_COUNT;
	this->loaded_chunk_count = 0;
	this->center_chunk_x = 0;
	this->center_chunk_z = 0;
	this->chunk_index_center_x = 0;
	this->chunk_index_center_z = 0;
	this->active_render_distance = WorldCoordinates::REQUIRED_VISIBLE_DISTANCE;
	this->seed[0] = '\0';
	terrain_default_generation_config(this->terrain_config);
	this->terrain_generation_started = false;
	this->current_tick = 0U;
	this->clear_chunk_index();
}

World::World(const World &other) : chunk_streamer_storage_(new WorldChunkStreamer(*this)),
	revision_manager_storage_(new WorldRevisionManager(*this)),
	chunk_streamer(*chunk_streamer_storage_.get()),
	revision_manager(*revision_manager_storage_.get())
{
	(void)other;
}

World::~World()
{
	this->destroy();
}

World &World::operator=(const World &other)
{
	(void)other;
	return (*this);
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
	int32_t	index;

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
	int32_t	index;
	int32_t	slot_x;
	int32_t	slot_z;
	int32_t	grid_x;
	int32_t	grid_z;
	int32_t	grid_width;

	this->clear_chunk_index();
	grid_width = WorldCoordinates::CACHE_CHUNK_RADIUS * 2 + 1;
	index = 0;
	while (index < this->chunk_count)
	{
		if (this->chunks[index].initialized == true)
		{
			slot_x = this->chunks[index].chunk_x - this->center_chunk_x;
			slot_z = this->chunks[index].chunk_z - this->center_chunk_z;
			if (slot_x >= -WorldCoordinates::CACHE_CHUNK_RADIUS
				&& slot_x <= WorldCoordinates::CACHE_CHUNK_RADIUS && slot_z >=
				-WorldCoordinates::CACHE_CHUNK_RADIUS
				&& slot_z <= WorldCoordinates::CACHE_CHUNK_RADIUS)
			{
				grid_x = slot_x + WorldCoordinates::CACHE_CHUNK_RADIUS;
				grid_z = slot_z + WorldCoordinates::CACHE_CHUNK_RADIUS;
				this->chunk_index[(grid_z * grid_width)
					+ grid_x] = &this->chunks[index];
			}
		}
		index = index + 1;
	}
	this->chunk_index_center_x = this->center_chunk_x;
	this->chunk_index_center_z = this->center_chunk_z;
	this->chunk_index_valid = true;
}

int32_t World::initialize(const char *seed_value)
{
	return (this->initialize(seed_value, nullptr));
}

int32_t World::seed_first_chunk_and_stream()
{
	int32_t	index;
	int32_t	error_code;
	int32_t	initial_radius;
	int32_t	initial_generated;

	index = 0;
	while (index < this->chunk_count)
	{
		this->chunks[index].reset_coordinates();
		this->chunks[index].initialized = false;
		index = index + 1;
	}
	error_code = WorldChunkLoader::initialize_chunk(&this->chunks[0], 0, 0,
			this->seed, this->chunks, this->chunk_count,
			this->terrain_context.config());
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->loaded_chunk_count = 1;
	this->rebuild_chunk_index();
	initial_radius = WorldCoordinates::render_distance_to_chunk_radius(this->active_render_distance);
	initial_generated = 0;
	return (this->chunk_streamer.seed_initial_stream(initial_radius, 64,
			&initial_generated));
}

int32_t World::initialize(const char *seed_value,
	const char *terrain_config_file_path)
{
	int32_t	error_code;

	this->destroy();
	this->chunk_count = WorldCoordinates::CHUNK_COUNT;
	this->loaded_chunk_count = 0;
	this->center_chunk_x = 0;
	this->center_chunk_z = 0;
	this->active_render_distance = WorldCoordinates::REQUIRED_VISIBLE_DISTANCE;
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
	error_code = this->chunk_streamer.initialize_pipeline();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->terrain_generation_started = true;
	error_code = this->seed_first_chunk_and_stream();
	if (error_code != FT_ERR_SUCCESS)
		this->destroy();
	return (error_code);
}

void World::set_terrain_config(const terrain_generation_config &config)
{
	if (this->terrain_generation_started)
		return ;
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
	return (terrain_generation_config_save_file(file_path, *config_to_save));
}

void World::destroy()
{
    {
        std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
        this->world_epoch_ += 1U;
    }
    (void)this->generation_pipeline_.destroy();
    std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
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
    this->revision_pending_ = false;
    this->revision_regeneration_active_ = false;
    this->revision_job_count_ = 0U;
    this->revision_completed_job_count_ = 0U;
    this->revision_regenerated_count_ = 0;
    this->revision_skipped_count_ = 0;
    this->revision_generation_error_ = FT_ERR_SUCCESS;
    this->revision_selected_.clear();
    this->revision_manual_protected_.clear();
    this->deferred_edits_.clear();
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

int32_t World::stream_chunks_sync(int32_t stream_radius, int32_t budget, int32_t *generated)
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
        if (this->find_chunk(this->center_chunk_x + candidate.offset_x,
                             this->center_chunk_z + candidate.offset_z) != nullptr)
        {
            candidate.state = WORLD_STREAM_CANDIDATE_READY;
            candidate.request_id = 0U;
            continue;
        }
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

World::StreamCandidate *World::find_stream_candidate(int32_t chunk_x,
                                                     int32_t chunk_z)
{
    for (StreamCandidate &candidate : this->stream_candidates_)
    {
        if (this->center_chunk_x + candidate.offset_x == chunk_x
            && this->center_chunk_z + candidate.offset_z == chunk_z)
            return (&candidate);
    }
    return (nullptr);
}

static int32_t world_move_mesh(chunk_mesh &destination, chunk_mesh &source)
{
    int32_t error_code = destination.vertices.move(source.vertices);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = destination.indices.move(source.indices);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    destination.bounds = source.bounds;
    destination.occupied_bounds = source.occupied_bounds;
    destination.has_occupied_bounds = source.has_occupied_bounds;
    return (FT_ERR_SUCCESS);
}

int32_t World::queue_chunk_remesh(WorldChunk &chunk)
{
    if (!chunk.initialized || !chunk.mesh_dirty
        || chunk.pending_mesh_request_id != 0U)
        return (FT_ERR_SUCCESS);
    if (this->generation_pipeline_.remesh_in_flight_count() >= 1U)
        return (FT_ERR_FULL);
    if (this->generation_pipeline_.queued_count() >= 8U)
        return (FT_ERR_FULL);
    WorldChunkSnapshot snapshot;
    int32_t error_code = this->generation_pipeline_.capture_snapshot(chunk,
        this->find_chunk(chunk.chunk_x - 1, chunk.chunk_z),
        this->find_chunk(chunk.chunk_x + 1, chunk.chunk_z),
        this->find_chunk(chunk.chunk_x, chunk.chunk_z - 1),
        this->find_chunk(chunk.chunk_x, chunk.chunk_z + 1), snapshot);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    const uint64_t request_id = this->next_request_id_++;
    error_code = this->generation_pipeline_.submit_remesh(request_id,
        this->world_epoch_, this->stream_relevance_epoch_, this->generation_revision_,
        chunk.chunk_x, chunk.chunk_z, chunk.voxel_revision, snapshot);
    if (error_code == FT_ERR_SUCCESS)
        chunk.pending_mesh_request_id = request_id;
    return (error_code);
}

int32_t World::queue_neighbor_remeshes(int32_t chunk_x, int32_t chunk_z)
{
    const int32_t coordinates[5][2] = {
        {chunk_x, chunk_z}, {chunk_x - 1, chunk_z}, {chunk_x + 1, chunk_z},
        {chunk_x, chunk_z - 1}, {chunk_x, chunk_z + 1}
    };
    int32_t index = 0;
    while (index < 5)
    {
        WorldChunk *chunk = this->find_chunk_mutable(coordinates[index][0],
            coordinates[index][1]);
        if (chunk != nullptr)
        {
            chunk->mesh_dirty = true;
            const int32_t error_code = this->queue_chunk_remesh(*chunk);
            if (error_code != FT_ERR_SUCCESS && error_code != FT_ERR_FULL)
                return (error_code);
        }
        index += 1;
    }
    return (FT_ERR_SUCCESS);
}

int32_t World::apply_deferred_edits(
    const std::chrono::steady_clock::time_point &deadline)
{
    if (this->deferred_edits_.empty())
        return (FT_ERR_SUCCESS);
    std::sort(this->deferred_edits_.begin(), this->deferred_edits_.end(),
        [](const WorldDeferredBlockEdit &left, const WorldDeferredBlockEdit &right)
        {
            if (left.world_x != right.world_x)
                return (left.world_x < right.world_x);
            if (left.world_y != right.world_y)
                return (left.world_y < right.world_y);
            if (left.world_z != right.world_z)
                return (left.world_z < right.world_z);
            if (left.request_id != right.request_id)
                return (left.request_id < right.request_id);
            return (left.sequence < right.sequence);
        });
    std::vector<WorldDeferredBlockEdit> pending;
    pending.reserve(this->deferred_edits_.size());
    std::size_t index = 0U;
    while (index < this->deferred_edits_.size())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            pending.insert(pending.end(), this->deferred_edits_.begin() + index,
                this->deferred_edits_.end());
            break;
        }
        const WorldDeferredBlockEdit &edit = this->deferred_edits_[index];
        const int32_t chunk_x = WorldCoordinates::floor_divide(
            edit.world_x, GAME_VOXEL_CHUNK_WIDTH);
        const int32_t chunk_z = WorldCoordinates::floor_divide(
            edit.world_z, GAME_VOXEL_CHUNK_DEPTH);
        WorldChunk *chunk = this->find_chunk_mutable(chunk_x, chunk_z);
        if (chunk == nullptr || !chunk->initialized)
        {
            pending.push_back(edit);
            index += 1U;
            continue;
        }
        const int32_t local_x = WorldCoordinates::positive_modulo(
            edit.world_x, GAME_VOXEL_CHUNK_WIDTH);
        const int32_t local_z = WorldCoordinates::positive_modulo(
            edit.world_z, GAME_VOXEL_CHUNK_DEPTH);
        if (chunk->chunk.write_generated_block(local_x, edit.world_y, local_z,
                                               edit.block_id) != FT_ERR_SUCCESS)
            return (FT_ERR_INVALID_OPERATION);
        chunk->voxel_revision += 1U;
        chunk->mesh_dirty = true;
        (void)this->queue_chunk_remesh(*chunk);
        index += 1U;
    }
    this->deferred_edits_.swap(pending);
    return (FT_ERR_SUCCESS);
}

int32_t World::commit_generation_result(
    std::unique_ptr<WorldGenerationPipeline::Result> &result) noexcept
{
    if (result == nullptr)
        return (FT_ERR_INVALID_ARGUMENT);
    if (result->world_epoch != this->world_epoch_)
        return (FT_ERR_SUCCESS);
    if (result->operation == WorldGenerationOperation::REGENERATE)
    {
        if (!this->revision_regeneration_active_
            || result->relevance_epoch != this->revision_generation_epoch_)
            return (FT_ERR_SUCCESS);
        this->revision_completed_job_count_ += 1U;
        if (result->error_code != FT_ERR_SUCCESS || result->chunk == nullptr)
        {
            if (this->revision_generation_error_ == FT_ERR_SUCCESS)
                this->revision_generation_error_ = result->error_code;
        }
        else
        {
            WorldChunk *chunk = this->find_chunk_mutable(result->chunk_x,
                result->chunk_z);
            if (chunk == nullptr || !chunk->initialized)
                this->revision_skipped_count_ += 1;
            else
            {
                chunk->destroy();
                if (chunk->chunk.move(result->chunk->chunk) != FT_ERR_SUCCESS
                    || chunk_mesh_initialize(chunk->mesh) != FT_ERR_SUCCESS
                    || world_move_mesh(chunk->mesh, result->chunk->mesh)
                        != FT_ERR_SUCCESS)
                {
                    chunk->destroy();
                    if (this->revision_generation_error_ == FT_ERR_SUCCESS)
                        this->revision_generation_error_ = FT_ERR_NO_MEMORY;
                }
                else
                {
                    chunk->chunk_x = result->chunk_x;
                    chunk->chunk_z = result->chunk_z;
                    chunk->world_x = result->chunk_x * GAME_VOXEL_CHUNK_WIDTH;
                    chunk->world_z = result->chunk_z * GAME_VOXEL_CHUNK_DEPTH;
                    chunk->initialized = true;
                    chunk->mesh_revision += 1U;
                    chunk->voxel_revision += 1U;
                    chunk->pending_mesh_request_id = 0U;
                    chunk->mesh_dirty = true;
                    this->revision_regenerated_count_ += 1;
                    (void)this->queue_neighbor_remeshes(result->chunk_x,
                        result->chunk_z);
                    this->deferred_edits_.insert(this->deferred_edits_.end(),
                        result->deferred_edits.begin(), result->deferred_edits.end());
                }
            }
        }
        if (this->revision_completed_job_count_ >= this->revision_job_count_)
            return (this->finish_revision_regeneration());
        return (FT_ERR_SUCCESS);
    }
    if (result->operation == WorldGenerationOperation::REMESH)
    {
        WorldChunk *chunk = this->find_chunk_mutable(result->chunk_x,
            result->chunk_z);
        if (chunk == nullptr || !chunk->initialized
            || chunk->pending_mesh_request_id != result->request_id
            || chunk->voxel_revision != result->voxel_revision)
            return (FT_ERR_SUCCESS);
        chunk->pending_mesh_request_id = 0U;
        if (result->error_code != FT_ERR_SUCCESS || result->mesh == nullptr)
            return (FT_ERR_SUCCESS);
        (void)chunk_mesh_destroy(chunk->mesh);
        if (chunk_mesh_initialize(chunk->mesh) != FT_ERR_SUCCESS
            || world_move_mesh(chunk->mesh, *result->mesh) != FT_ERR_SUCCESS)
            return (FT_ERR_NO_MEMORY);
        chunk->mesh_revision += 1U;
        chunk->mesh_dirty = false;
        return (FT_ERR_SUCCESS);
    }
    StreamCandidate *candidate = this->find_stream_candidate(result->chunk_x,
        result->chunk_z);
    if (candidate == nullptr || candidate->request_id != result->request_id
        || candidate->relevance_epoch != result->relevance_epoch
        || candidate->generation_revision != result->generation_revision)
        return (FT_ERR_SUCCESS);
    if (result->error_code != FT_ERR_SUCCESS || result->chunk == nullptr)
    {
        candidate->state = WORLD_STREAM_CANDIDATE_FAILED_RETRYABLE;
        candidate->retry_count += 1U;
        candidate->last_error = result->error_code;
        candidate->retry_frames = 1 << std::min(candidate->retry_count, 6U);
        this->stream_last_error_ = result->error_code;
        this->stream_retryable_count_ += 1;
        return (FT_ERR_SUCCESS);
    }
    if (this->find_chunk(result->chunk_x, result->chunk_z) != nullptr)
    {
        candidate->state = WORLD_STREAM_CANDIDATE_READY;
        return (FT_ERR_SUCCESS);
    }
    WorldChunk *slot = WorldChunkStore::find_free_chunk_slot(this->chunks,
        this->chunk_count);
    if (slot == nullptr)
    {
        candidate->state = WORLD_STREAM_CANDIDATE_FAILED_RETRYABLE;
        candidate->last_error = FT_ERR_NO_MEMORY;
        candidate->retry_frames = 1;
        return (FT_ERR_SUCCESS);
    }
    if (slot->chunk.move(result->chunk->chunk) != FT_ERR_SUCCESS
        || chunk_mesh_initialize(slot->mesh) != FT_ERR_SUCCESS
        || world_move_mesh(slot->mesh, result->chunk->mesh) != FT_ERR_SUCCESS)
    {
        slot->destroy();
        candidate->state = WORLD_STREAM_CANDIDATE_FAILED_RETRYABLE;
        candidate->last_error = FT_ERR_NO_MEMORY;
        candidate->retry_frames = 1;
        return (FT_ERR_SUCCESS);
    }
    slot->chunk_x = result->chunk_x;
    slot->chunk_z = result->chunk_z;
    slot->world_x = result->chunk_x * GAME_VOXEL_CHUNK_WIDTH;
    slot->world_z = result->chunk_z * GAME_VOXEL_CHUNK_DEPTH;
    slot->initialized = true;
    slot->mesh_revision = 1U;
    slot->voxel_revision = 1U;
    slot->pending_mesh_request_id = 0U;
    slot->mesh_dirty = true;
    this->loaded_chunk_count += 1;
    this->register_chunk_index(*slot);
    candidate->state = WORLD_STREAM_CANDIDATE_READY;
    candidate->retry_count = 0U;
    candidate->retry_frames = 0;
    candidate->last_error = FT_ERR_SUCCESS;
    candidate->queued_frame = 0U;
    this->stream_progress_frame_ = this->stream_frame_;
    this->deferred_edits_.insert(this->deferred_edits_.end(),
        result->deferred_edits.begin(), result->deferred_edits.end());
    (void)this->queue_neighbor_remeshes(result->chunk_x, result->chunk_z);
    return (FT_ERR_SUCCESS);
}

int32_t World::drain_generation_results() noexcept
{
    std::unique_ptr<WorldGenerationPipeline::Result> result;
    int32_t processed = 0;
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
    while (processed < 2
        && std::chrono::steady_clock::now() < deadline
        && this->generation_pipeline_.poll(result) == FT_ERR_SUCCESS)
    {
        const int32_t error_code = this->commit_generation_result(result);
        if (error_code != FT_ERR_SUCCESS)
            return (error_code);
        result.reset();
        processed += 1;
    }
    return (this->apply_deferred_edits(deadline));
}

int32_t World::stream_chunks_async(int32_t stream_radius, int32_t budget,
                                   int32_t *generated)
{
    (void)generated;
    int32_t submitted = 0;
    int32_t scanned = 0;
    const int32_t candidate_count = static_cast<int32_t>(
        this->stream_candidates_.size());
    while (scanned < candidate_count)
    {
        StreamCandidate &candidate = this->stream_candidates_[
            this->stream_candidate_cursor_];
        this->stream_candidate_cursor_ = (this->stream_candidate_cursor_ + 1U)
            % this->stream_candidates_.size();
        scanned += 1;
        if (candidate.state == WORLD_STREAM_CANDIDATE_READY
            || candidate.state == WORLD_STREAM_CANDIDATE_GENERATING
            || candidate.state == WORLD_STREAM_CANDIDATE_MESHING
            || candidate.state == WORLD_STREAM_CANDIDATE_QUEUED)
            continue;
        if (candidate.retry_frames > 0)
        {
            candidate.retry_frames -= 1;
            continue;
        }
        if (candidate.relevance_epoch != this->stream_relevance_epoch_
            || candidate.generation_revision != this->generation_revision_)
        {
            candidate.state = WORLD_STREAM_CANDIDATE_ABSENT;
            candidate.retry_count = 0U;
            candidate.last_error = FT_ERR_SUCCESS;
            candidate.relevance_epoch = this->stream_relevance_epoch_;
            candidate.generation_revision = this->generation_revision_;
            candidate.queued_frame = 0U;
            candidate.request_id = 0U;
        }
        const int32_t chunk_x = this->center_chunk_x + candidate.offset_x;
        const int32_t chunk_z = this->center_chunk_z + candidate.offset_z;
        if (this->find_chunk(chunk_x, chunk_z) != nullptr)
        {
            candidate.state = WORLD_STREAM_CANDIDATE_READY;
            continue;
        }
        const uint64_t request_id = this->next_request_id_++;
        const int32_t error_code = this->generation_pipeline_.submit_generation(
            request_id, this->world_epoch_, this->stream_relevance_epoch_,
            this->generation_revision_, chunk_x, chunk_z, this->seed,
            this->terrain_context.config(), TERRAIN_STAGE_BASE_TERRAIN
            | TERRAIN_STAGE_CAVES | TERRAIN_STAGE_FLUIDS
            | TERRAIN_STAGE_DECORATION | TERRAIN_STAGE_STRUCTURES
            | TERRAIN_STAGE_ORES, WorldGenerationOperation::STREAM);
        if (error_code == FT_ERR_FULL)
            break;
        if (error_code != FT_ERR_SUCCESS)
        {
            candidate.state = WORLD_STREAM_CANDIDATE_FAILED_RETRYABLE;
            candidate.last_error = error_code;
            candidate.retry_frames = 1;
            continue;
        }
        candidate.state = WORLD_STREAM_CANDIDATE_GENERATING;
        candidate.queued_frame = this->stream_frame_;
        candidate.request_id = request_id;
        submitted += 1;
        if (budget > 0 && submitted >= budget)
            break;
    }
    int32_t dirty_index = 0;
    while (dirty_index < this->chunk_count
        && this->generation_pipeline_.queued_count() < 8U)
    {
        if (this->chunks[dirty_index].initialized && this->chunks[dirty_index].mesh_dirty)
            (void)this->queue_chunk_remesh(this->chunks[dirty_index]);
        dirty_index += 1;
    }
    (void)stream_radius;
    return (FT_ERR_SUCCESS);
}

int32_t World::update_around(double camera_x, double camera_z, int32_t generation_budget,
                             int32_t render_distance)
{
    int32_t stream_radius;
    int32_t generated;

    {
        std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
        this->stream_frame_++;
        this->center_chunk_x = WorldCoordinates::floor_divide(
            static_cast<int32_t>(std::floor(camera_x)), GAME_VOXEL_CHUNK_WIDTH);
        this->center_chunk_z = WorldCoordinates::floor_divide(
            static_cast<int32_t>(std::floor(camera_z)), GAME_VOXEL_CHUNK_DEPTH);
        this->active_render_distance =
            WorldCoordinates::clamp_int(render_distance, WorldCoordinates::MIN_RENDER_DISTANCE,
                                        WorldCoordinates::CACHE_CHUNK_RADIUS * GAME_VOXEL_CHUNK_WIDTH);
    }
    bool center_changed;
    {
        std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);
        center_changed = this->chunk_index_center_x != this->center_chunk_x ||
            this->chunk_index_center_z != this->center_chunk_z;
    }
    if (this->chunk_index_valid == false || center_changed)
    {
        std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
        WorldChunkStore::evict_far_chunks(this->chunks, this->chunk_count,
                                          &this->loaded_chunk_count, this->center_chunk_x,
                                          this->center_chunk_z);
        this->rebuild_chunk_index();
    }
    if (center_changed)
    {
        this->generation_pipeline_.cancel_queued();
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
            candidate.request_id = 0U;
        }
        this->stream_retryable_count_ = 0;
    }
    int32_t drain_error;
    {
        std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
        drain_error = this->drain_generation_results();
    }
    if (drain_error != FT_ERR_SUCCESS)
        return (drain_error);
    stream_radius = WorldCoordinates::render_distance_to_chunk_radius(this->active_render_distance);
    generated = 0;
    if (generation_budget >= WorldCoordinates::CHUNK_COUNT)
    {
        this->generation_pipeline_.cancel_queued();
        for (StreamCandidate &candidate : this->stream_candidates_)
        {
            if (candidate.state != WORLD_STREAM_CANDIDATE_READY)
            {
                candidate.state = WORLD_STREAM_CANDIDATE_ABSENT;
                candidate.request_id = 0U;
            }
        }
        std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
        return (this->stream_chunks_sync(stream_radius, generation_budget, &generated));
    }
    if (generation_budget <= 0)
    {
        if (this->generation_credit_ < 4)
            this->generation_credit_ = this->generation_credit_ + 1;
        if (this->generation_credit_ < 4)
        {
            std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
            return (this->stream_chunks_async(stream_radius, 0, &generated));
        }
        this->generation_credit_ = 0;
        generation_budget = 1;
    }
    else
        this->generation_credit_ = 0;
    std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
    return (this->stream_chunks_async(stream_radius, generation_budget, &generated));
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
    std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);
    return (WorldBlockQuery::surface_top_at(*this, world_x, world_z, surface_top));
}

bool World::solid_block_at(int32_t world_x, int32_t world_y, int32_t world_z) const
{
    std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);
    return (WorldBlockQuery::solid_block_at(*this, world_x, world_y, world_z));
}

bool World::block_id_at(int32_t world_x, int32_t world_y, int32_t world_z, uint32_t *block_id) const
{
    std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);
    return (WorldBlockQuery::block_id_at(*this, world_x, world_y, world_z, block_id));
}

int32_t World::delete_block_at(int32_t world_x, int32_t world_y, int32_t world_z)
{
    std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
    return (WorldBlockEditor::delete_block_at(*this, world_x, world_y, world_z));
}

int32_t World::place_block_at(int32_t world_x, int32_t world_y, int32_t world_z, uint32_t block_id)
{
    std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
    return (WorldBlockEditor::place_block_at(*this, world_x, world_y, world_z, block_id));
}

int32_t World::raycast_solid(double origin_x, double origin_y, double origin_z, double direction_x,
                             double direction_y, double direction_z, double max_distance,
                             int32_t *block_x, int32_t *block_y, int32_t *block_z) const
{
    std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);
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
    std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);
    return (WorldRaycaster::raycast_edit_target(*this, origin_x, origin_y, origin_z, direction_x,
                                                direction_y, direction_z, max_distance, hit_block_x,
                                                hit_block_y, hit_block_z, place_block_x,
                                                place_block_y, place_block_z, hit_block_id));
}
