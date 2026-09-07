#include "../../src/world/World.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"
#include <cstdio>
#include <mutex>
#include <shared_mutex>

void World::register_chunk_index(const WorldChunk &chunk)
{
	int32_t	slot_x;
	int32_t	slot_z;
	int32_t	grid_width;

	if (this->chunk_index_valid == false)
		return ;
	slot_x = chunk.chunk_x - this->chunk_index_center_x;
	slot_z = chunk.chunk_z - this->chunk_index_center_z;
	if (slot_x < -WorldCoordinates::CACHE_CHUNK_RADIUS
		|| slot_x > WorldCoordinates::CACHE_CHUNK_RADIUS || slot_z <
		-WorldCoordinates::CACHE_CHUNK_RADIUS
		|| slot_z > WorldCoordinates::CACHE_CHUNK_RADIUS)
		return ;
	grid_width = WorldCoordinates::CACHE_CHUNK_RADIUS * 2 + 1;
	this->chunk_index[((slot_z + WorldCoordinates::CACHE_CHUNK_RADIUS)
			* grid_width) + (slot_x
			+ WorldCoordinates::CACHE_CHUNK_RADIUS)] = const_cast<WorldChunk *>(&chunk);
}

const WorldChunk *World::find_chunk(int32_t chunk_x, int32_t chunk_z) const
{
	int32_t slot_x;
	int32_t slot_z;
	int32_t grid_width;

	slot_x = chunk_x - this->center_chunk_x;
	slot_z = chunk_z - this->center_chunk_z;
	if (slot_x < -WorldCoordinates::CACHE_CHUNK_RADIUS
		|| slot_x > WorldCoordinates::CACHE_CHUNK_RADIUS || slot_z <
		-WorldCoordinates::CACHE_CHUNK_RADIUS
		|| slot_z > WorldCoordinates::CACHE_CHUNK_RADIUS)
		return (nullptr);
	grid_width = WorldCoordinates::CACHE_CHUNK_RADIUS * 2 + 1;
	return (this->chunk_index[((slot_z + WorldCoordinates::CACHE_CHUNK_RADIUS)
			* grid_width) + (slot_x + WorldCoordinates::CACHE_CHUNK_RADIUS)]);
}

WorldChunk *World::find_chunk_mutable(int32_t chunk_x, int32_t chunk_z)
{
	return (const_cast<WorldChunk *>(static_cast<const World *>(this)->find_chunk(chunk_x,
				chunk_z)));
}

int32_t World::update_around(double camera_x, double camera_z,
	int32_t generation_budget)
{
	return (this->update_around(camera_x, camera_z, generation_budget,
			WorldCoordinates::REQUIRED_VISIBLE_DISTANCE));
}

int32_t World::update_around(double camera_x, double camera_z,
	int32_t generation_budget, int32_t render_distance)
{
	bool	center_changed;
	int32_t	stream_radius;
	int32_t analytics_error;
	std::vector<int32_t> evicted_chunk_x;
	std::vector<int32_t> evicted_chunk_z;
	std::vector<std::unique_ptr<WorldChunk>> retired_chunks;
	std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
#if defined(LIBFT_ENABLE_ANALYTICS)
	int32_t loaded_before_recenter;
	int32_t loaded_after_recenter;
	const auto recenter_start = std::chrono::steady_clock::now();
#endif

	this->center_chunk_x = WorldCoordinates::floor_divide(static_cast<int32_t>(std::floor(camera_x)),
			GAME_VOXEL_CHUNK_WIDTH);
	this->center_chunk_z = WorldCoordinates::floor_divide(static_cast<int32_t>(std::floor(camera_z)),
			GAME_VOXEL_CHUNK_DEPTH);
	this->active_render_distance = WorldCoordinates::clamp_int(render_distance,
			WorldCoordinates::MIN_RENDER_DISTANCE,
			WorldCoordinates::CACHE_CHUNK_RADIUS * GAME_VOXEL_CHUNK_WIDTH);
	center_changed = this->chunk_index_center_x != this->center_chunk_x
		|| this->chunk_index_center_z != this->center_chunk_z;
	if (this->chunk_index_valid == false || center_changed)
	{
		if (center_changed)
			this->mark_geometry_changed();
#if defined(LIBFT_ENABLE_ANALYTICS)
		loaded_before_recenter = this->loaded_chunk_count;
#endif
		analytics_error = RuntimeAnalytics::begin_scope(
			RuntimeAnalyticsScope::WORLD_STREAM_RECENTER);
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr,
				"Analytics: stream recenter scope start failed (%d)\n",
				analytics_error);
		for (int32_t index = 0; index < this->chunk_count; ++index)
		{
			if (this->chunks[index].initialized
				&& WorldCoordinates::chunk_distance_squared(
					this->chunks[index].chunk_x, this->chunks[index].chunk_z,
					this->center_chunk_x, this->center_chunk_z)
					> WorldCoordinates::CACHE_CHUNK_RADIUS
						* WorldCoordinates::CACHE_CHUNK_RADIUS)
			{
				evicted_chunk_x.push_back(this->chunks[index].chunk_x);
				evicted_chunk_z.push_back(this->chunks[index].chunk_z);
			}
		}
		retired_chunks.reserve(evicted_chunk_x.size());
		for (int32_t index = 0; index < this->chunk_count; ++index)
		{
			if (this->chunks[index].initialized == false
				|| WorldCoordinates::chunk_distance_squared(
					this->chunks[index].chunk_x, this->chunks[index].chunk_z,
					this->center_chunk_x, this->center_chunk_z)
					<= WorldCoordinates::CACHE_CHUNK_RADIUS
						* WorldCoordinates::CACHE_CHUNK_RADIUS)
				continue ;
			std::unique_ptr<WorldChunk> retired(new (std::nothrow) WorldChunk());
			if (retired == nullptr
				|| retired->move(this->chunks[index]) != FT_ERR_SUCCESS)
			{
				this->chunks[index].destroy();
			}
			else
				retired_chunks.push_back(std::move(retired));
			if (this->loaded_chunk_count > 0)
				this->loaded_chunk_count -= 1;
		}
		for (std::unique_ptr<WorldChunk> &retired : retired_chunks)
			(void)this->chunk_streamer.pipeline().retire_chunk(std::move(retired));
		for (std::size_t index = 0U; index < evicted_chunk_x.size(); ++index)
			this->chunk_streamer.mark_neighbor_remeshes(evicted_chunk_x[index],
				evicted_chunk_z[index]);
		this->rebuild_chunk_index();
#if defined(LIBFT_ENABLE_ANALYTICS)
		loaded_after_recenter = this->loaded_chunk_count;
#endif
		analytics_error = RuntimeAnalytics::end_scope();
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr,
				"Analytics: stream recenter scope end failed (%d)\n",
				analytics_error);
#if defined(LIBFT_ENABLE_ANALYTICS)
		const uint64_t recenter_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - recenter_start).count());
		if (recenter_us >= 8000U)
			std::fprintf(stderr,
				"[Analytics][World] slow recenter center=(%d,%d) "
				"loaded_before=%d loaded_after=%d duration_us=%llu\n",
				this->center_chunk_x, this->center_chunk_z,
				loaded_before_recenter, loaded_after_recenter,
				static_cast<unsigned long long>(recenter_us));
#endif
	}
	stream_radius = WorldCoordinates::render_distance_to_chunk_radius(this->active_render_distance);
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::WORLD_STREAM_UPDATE);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: world stream scope start failed (%d)\n",
			analytics_error);
	{
		int32_t result = this->chunk_streamer.update(generation_budget,
			stream_radius, center_changed);
		analytics_error = RuntimeAnalytics::end_scope();
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr, "Analytics: world stream scope end failed (%d)\n",
			analytics_error);
		return (result);
	}
}

int32_t World::set_light_update_config(
	const voxel_light_update_config &config) noexcept
{
	return (this->chunk_streamer.set_light_update_config(config));
}

const voxel_light_update_config &World::light_update_config() const noexcept
{
	return (this->chunk_streamer.light_update_config());
}

int32_t World::stream_last_error() const
{
	return (this->chunk_streamer.stream_last_error());
}

int32_t World::stream_retryable_count() const
{
	return (this->chunk_streamer.stream_retryable_count());
}

World::StreamDiagnostics World::stream_diagnostics() const
{
	WorldChunkStreamer::Diagnostics source;
	StreamDiagnostics diagnostics;

	source = this->chunk_streamer.diagnostics();
	diagnostics.frame = source.frame;
	diagnostics.progress_frame = source.progress_frame;
	diagnostics.candidate_count = source.candidate_count;
	diagnostics.ready_count = source.ready_count;
	diagnostics.pending_count = source.pending_count;
	diagnostics.retryable_count = source.retryable_count;
	diagnostics.failed_count = source.failed_count;
	diagnostics.playable_failed_count = source.playable_failed_count;
	diagnostics.playable_required_count = source.playable_required_count;
	diagnostics.playable_drawable_count = source.playable_drawable_count;
	diagnostics.active_generation_count = source.active_generation_count;
	diagnostics.remesh_queue_peak = source.remesh_queue_peak;
	diagnostics.remesh_snapshot_bytes = source.remesh_snapshot_bytes;
	diagnostics.remesh_scanned_cells = source.remesh_scanned_cells;
	diagnostics.remesh_propagated_cells = source.remesh_propagated_cells;
	diagnostics.remesh_light_queue_peak = source.remesh_light_queue_peak;
	diagnostics.remesh_completed_count = source.remesh_completed_count;
	diagnostics.stale_result_count = source.stale_result_count;
	diagnostics.stale_stream_result_count = source.stale_stream_result_count;
	diagnostics.stale_remesh_result_count = source.stale_remesh_result_count;
	diagnostics.oldest_result_age_nanoseconds =
		source.oldest_result_age_nanoseconds;
	diagnostics.oldest_pending_age = source.oldest_pending_age;
	diagnostics.deferred_edit_count = source.deferred_edit_count;
	diagnostics.deferred_edit_cursor = source.deferred_edit_cursor;
	diagnostics.last_error = source.last_error;
	return (diagnostics);
}

bool World::validate_visible_distance(double camera_x, double camera_z,
	double yaw, int32_t required_distance) const
{
	return (WorldVisibilityValidator::validate_visible_distance(*this, camera_x,
			camera_z, yaw, required_distance));
}

bool World::surface_top_at(int32_t world_x, int32_t world_z,
	double *surface_top) const
{
	std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);
	return (WorldBlockQuery::surface_top_at(*this, world_x, world_z,
			surface_top));
}

bool World::solid_block_at(int32_t world_x, int32_t world_y,
	int32_t world_z) const
{
	std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);
	return (WorldBlockQuery::solid_block_at(*this, world_x, world_y, world_z));
}

bool World::block_id_at(int32_t world_x, int32_t world_y, int32_t world_z,
	uint32_t *block_id) const
{
	std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);
	return (WorldBlockQuery::block_id_at(*this, world_x, world_y, world_z,
			block_id));
}

int32_t World::delete_block_at(int32_t world_x, int32_t world_y,
	int32_t world_z)
{
	std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
	return (WorldBlockEditor::delete_block_at(*this, world_x, world_y,
			world_z));
}

int32_t World::place_block_at(int32_t world_x, int32_t world_y, int32_t world_z,
	uint32_t block_id)
{
	std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
	return (WorldBlockEditor::place_block_at(*this, world_x, world_y, world_z,
		block_id));
}

int32_t World::apply_authoritative_block_change(
	const game_block_change_request &request, game_block_delta *delta_out)
{
	std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
	WorldChunk *world_chunk;
	uint64_t previous_revision;
	uint64_t current_revision;
	game_block_edit_op edit;
	WorldEditHistory::Record record;
	int32_t error_code;

	if (delta_out == nullptr || request.local_x >= GAME_VOXEL_CHUNK_WIDTH
		|| request.local_y >= GAME_VOXEL_CHUNK_HEIGHT
		|| request.local_z >= GAME_VOXEL_CHUNK_DEPTH)
		return (FT_ERR_INVALID_ARGUMENT);
	world_chunk = this->find_chunk_mutable(request.chunk_x, request.chunk_z);
	if (world_chunk == nullptr)
		return (FT_ERR_NOT_FOUND);
	previous_revision = world_chunk->chunk.get_revision();
	error_code = world_chunk->chunk.apply_authoritative_block_change(request,
		delta_out);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	current_revision = world_chunk->chunk.get_revision();
	if (current_revision == previous_revision)
		return (FT_ERR_SUCCESS);
	world_chunk->voxel_revision = current_revision;
	this->mark_geometry_changed();
	this->chunk_streamer.mark_remesh_dirty(*world_chunk);
	world_chunk->pending_mesh_request_id = 0U;
	edit.world_x = request.chunk_x * GAME_VOXEL_CHUNK_WIDTH
		+ static_cast<int32_t>(request.local_x);
	edit.world_y = static_cast<int32_t>(request.local_y);
	edit.world_z = request.chunk_z * GAME_VOXEL_CHUNK_DEPTH
		+ static_cast<int32_t>(request.local_z);
	edit.block_type = request.requested_block_id;
	edit.tick = this->current_tick;
	record.edit = edit;
	record.previous_block_id = request.expected_block_id;
	this->edit_history.record(record);
	this->chunk_streamer.mark_neighbor_remeshes(request.chunk_x,
		request.chunk_z);
	this->chunk_streamer.prioritize_chunk_remesh(request.chunk_x,
		request.chunk_z);
	/* Authoritative edits follow the same immediate publication path as local
	 * edits. If the single remesh slot is occupied, the priority queue retries
	 * the request without losing the edit notification. */
	if (this->chunk_streamer.queue_chunk_remesh(*world_chunk) != FT_ERR_SUCCESS
		&& world_chunk->pending_mesh_request_id == 0U)
		this->chunk_streamer.prioritize_chunk_remesh(request.chunk_x,
			request.chunk_z);
	return (FT_ERR_SUCCESS);
}

void World::advance_tick()
{
	std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
	this->current_tick += 1U;
}

int32_t World::undo_last_edit()
{
	std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
	return (this->edit_history.undo(*this));
}

int32_t World::redo_last_edit()
{
	std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
	return (this->edit_history.redo(*this));
}

int32_t World::raycast_solid(double origin_x, double origin_y, double origin_z,
	double direction_x, double direction_y, double direction_z,
	double max_distance, int32_t *block_x, int32_t *block_y,
	int32_t *block_z) const
{
	std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);
	return (WorldRaycaster::raycast_solid(*this, origin_x, origin_y, origin_z,
			direction_x, direction_y, direction_z, max_distance, block_x,
			block_y, block_z));
}

int32_t World::raycast_edit_target(double origin_x, double origin_y,
	double origin_z, double direction_x, double direction_y, double direction_z,
	double max_distance, int32_t *hit_block_x, int32_t *hit_block_y,
	int32_t *hit_block_z, int32_t *place_block_x, int32_t *place_block_y,
	int32_t *place_block_z, uint32_t *hit_block_id) const
{
	std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);
	return (WorldRaycaster::raycast_edit_target(*this, origin_x, origin_y,
			origin_z, direction_x, direction_y, direction_z, max_distance,
			hit_block_x, hit_block_y, hit_block_z, place_block_x, place_block_y,
			place_block_z, hit_block_id));
}
