#include "../../src/world/World.hpp"
#include "../../src/coordinates/WorldCoordinates.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <utility>

namespace
{
	template <typename T>
	static bool fluid_override_less(const T &left, const T &right)
	{
		const int32_t left_chunk_x = WorldCoordinates::floor_divide(
			left.world_x, GAME_VOXEL_CHUNK_WIDTH);
		const int32_t right_chunk_x = WorldCoordinates::floor_divide(
			right.world_x, GAME_VOXEL_CHUNK_WIDTH);
		const int32_t left_chunk_z = WorldCoordinates::floor_divide(
			left.world_z, GAME_VOXEL_CHUNK_DEPTH);
		const int32_t right_chunk_z = WorldCoordinates::floor_divide(
			right.world_z, GAME_VOXEL_CHUNK_DEPTH);
		if (left_chunk_x != right_chunk_x)
			return (left_chunk_x < right_chunk_x);
		if (left_chunk_z != right_chunk_z)
			return (left_chunk_z < right_chunk_z);
		if (left.world_y != right.world_y)
			return (left.world_y < right.world_y);
		if (left.world_x != right.world_x)
			return (left.world_x < right.world_x);
		return (left.world_z < right.world_z);
	}

	template <typename T>
	static bool fluid_override_same_cell(const T &left, const T &right)
	{
		return (left.world_x == right.world_x && left.world_y == right.world_y
			&& left.world_z == right.world_z);
	}

	static void fluid_state_append_u32(std::vector<uint8_t> &bytes,
		uint32_t value)
	{
		for (uint32_t shift = 0U; shift < 32U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	static void fluid_state_append_i32(std::vector<uint8_t> &bytes,
		int32_t value)
	{
		fluid_state_append_u32(bytes, static_cast<uint32_t>(value));
	}

	static uint32_t fluid_state_read_u32(const uint8_t *bytes,
		std::size_t offset)
	{
		return (static_cast<uint32_t>(bytes[offset])
			| (static_cast<uint32_t>(bytes[offset + 1U]) << 8U)
			| (static_cast<uint32_t>(bytes[offset + 2U]) << 16U)
			| (static_cast<uint32_t>(bytes[offset + 3U]) << 24U));
	}

	static int32_t fluid_state_read_i32(const uint8_t *bytes,
		std::size_t offset)
	{
		return (static_cast<int32_t>(fluid_state_read_u32(bytes, offset)));
	}

	static uint64_t fluid_state_checksum(const uint8_t *bytes,
		std::size_t size)
	{
		uint64_t hash = 14695981039346656037ULL;
		for (std::size_t index = 0U; index < size; ++index)
		{
			hash ^= bytes[index];
			hash *= 1099511628211ULL;
		}
		return (hash);
	}

	static void fluid_state_append_u64(std::vector<uint8_t> &bytes,
		uint64_t value)
	{
		for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}
}

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

int32_t World::capture_remesh_snapshot(int32_t chunk_x, int32_t chunk_z,
	WorldGenerationPipeline::WorldChunkSnapshot &snapshot) const noexcept
{
	const WorldChunk *target;
	int32_t error_code;
	std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);

	target = this->find_chunk(chunk_x, chunk_z);
	if (target == nullptr || !target->initialized)
		return (FT_ERR_NOT_FOUND);
	error_code = this->chunk_streamer.pipeline().capture_snapshot(*target,
		this->find_chunk(chunk_x - 1, chunk_z),
		this->find_chunk(chunk_x + 1, chunk_z),
		this->find_chunk(chunk_x, chunk_z - 1),
		this->find_chunk(chunk_x, chunk_z + 1),
		this->find_chunk(chunk_x - 1, chunk_z - 1),
		this->find_chunk(chunk_x + 1, chunk_z - 1),
		this->find_chunk(chunk_x - 1, chunk_z + 1),
		this->find_chunk(chunk_x + 1, chunk_z + 1), snapshot);
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"[WorldGen] capture_remesh_snapshot result chunk=(%d,%d) error=%d\n",
			chunk_x, chunk_z, error_code);
#endif
	return (error_code);
}

bool World::remesh_capture_is_current(int32_t chunk_x, int32_t chunk_z,
	uint64_t request_id, uint64_t voxel_revision,
	uint64_t light_revision, uint16_t content_version,
	uint16_t light_input_version) const noexcept
{
	const WorldChunk *chunk;
	std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);

	chunk = this->find_chunk(chunk_x, chunk_z);
	if (chunk == nullptr || !chunk->initialized)
		return (false);
	if (chunk->pending_mesh_request_id != request_id)
		return (false);
	if (chunk->voxel_revision != voxel_revision)
		return (false);
	if (chunk->light_revision != light_revision)
		return (false);
	if (chunk->content_version != content_version
		|| chunk->light_input_version != light_input_version)
		return (false);
	return (true);
}

void World::clear_pending_remesh(int32_t chunk_x, int32_t chunk_z,
	uint64_t request_id) noexcept
{
	std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
	this->clear_pending_remesh_unlocked(chunk_x, chunk_z, request_id);
}

void World::clear_pending_remesh_unlocked(int32_t chunk_x, int32_t chunk_z,
	uint64_t request_id) noexcept
{
	WorldChunk *chunk = this->find_chunk_mutable(chunk_x, chunk_z);

	if (chunk != nullptr && chunk->pending_mesh_request_id == request_id)
	{
		chunk->clear_pending_remesh_request();
		chunk->mesh_dirty = true;
	}
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
				evicted_chunk_z[index], false, true);
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

int32_t World::set_interactive_light_update_config(
	const voxel_light_update_config &config) noexcept
{
	return (this->chunk_streamer.set_interactive_light_update_config(config));
}

const voxel_light_update_config &World::interactive_light_update_config()
	const noexcept
{
	return (this->chunk_streamer.interactive_light_update_config());
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
	diagnostics.remesh_starvation_promotions =
		source.remesh_starvation_promotions;
	diagnostics.remesh_priority_queue_depth = source.remesh_priority_queue_depth;
	diagnostics.interactive_remesh_queue_depth =
		source.interactive_remesh_queue_depth;
	diagnostics.oldest_remesh_queue_age = source.oldest_remesh_queue_age;
	diagnostics.remesh_snapshot_bytes = source.remesh_snapshot_bytes;
	diagnostics.remesh_capture_duration_nanoseconds =
		source.remesh_capture_duration_nanoseconds;
	diagnostics.remesh_capture_count = source.remesh_capture_count;
	diagnostics.remesh_scanned_cells = source.remesh_scanned_cells;
	diagnostics.remesh_propagated_cells = source.remesh_propagated_cells;
	diagnostics.remesh_light_queue_peak = source.remesh_light_queue_peak;
	diagnostics.remesh_completed_count = source.remesh_completed_count;
	diagnostics.remesh_incremental_completed_count =
		source.remesh_incremental_completed_count;
	diagnostics.remesh_full_completed_count = source.remesh_full_completed_count;
	diagnostics.remesh_geometry_only_count = source.remesh_geometry_only_count;
	diagnostics.remesh_canceled_count = source.remesh_canceled_count;
	diagnostics.stale_result_count = source.stale_result_count;
	diagnostics.stale_stream_result_count = source.stale_stream_result_count;
	diagnostics.stale_remesh_result_count = source.stale_remesh_result_count;
	diagnostics.stale_remesh_capture_count =
		source.stale_remesh_capture_count;
	diagnostics.stale_remesh_dependency_count =
		source.stale_remesh_dependency_count;
	diagnostics.stale_remesh_pending_count =
		source.stale_remesh_pending_count;
	diagnostics.stale_remesh_revision_count =
		source.stale_remesh_revision_count;
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

bool World::light_level_at(int32_t world_x, int32_t world_y, int32_t world_z,
	uint8_t *light_level) const
{
	std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);
	return (WorldBlockQuery::light_level_at(*this, world_x, world_y, world_z,
			light_level));
}

int32_t World::delete_block_at(int32_t world_x, int32_t world_y,
	int32_t world_z)
{
	std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
	std::vector<FluidCellOverride> staged;
	int32_t result;
	if (world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		staged = this->fluid_cell_overrides_;
		std::size_t index = 0U;
		for (; index < staged.size(); ++index)
			if (staged[index].world_x == world_x
				&& staged[index].world_y == world_y
				&& staged[index].world_z == world_z)
				break ;
		if (index == staged.size())
		{
			if (staged.size() >= 16384U)
				return (FT_ERR_FULL);
			staged.push_back({world_x, world_y, world_z, GAME_VOXEL_AIR_BLOCK});
		}
		else
			staged[index].block_id = GAME_VOXEL_AIR_BLOCK;
		std::sort(staged.begin(), staged.end(),
			[](const FluidCellOverride &left, const FluidCellOverride &right)
			{
				return (fluid_override_less(left, right));
			});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	result = WorldBlockEditor::delete_block_at(*this, world_x, world_y,
		world_z);
	if (result == FT_ERR_SUCCESS)
		this->fluid_cell_overrides_.swap(staged);
	return (result);
}

int32_t World::place_block_at(int32_t world_x, int32_t world_y, int32_t world_z,
	uint32_t block_id)
{
	std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
	std::vector<FluidCellOverride> staged;
	int32_t result;
	if (world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		staged = this->fluid_cell_overrides_;
		std::size_t index = 0U;
		for (; index < staged.size(); ++index)
			if (staged[index].world_x == world_x
				&& staged[index].world_y == world_y
				&& staged[index].world_z == world_z)
				break ;
		if (index == staged.size())
		{
			if (staged.size() >= 16384U)
				return (FT_ERR_FULL);
			staged.push_back({world_x, world_y, world_z, block_id});
		}
		else
			staged[index].block_id = block_id;
		std::sort(staged.begin(), staged.end(),
			[](const FluidCellOverride &left, const FluidCellOverride &right)
			{
				return (fluid_override_less(left, right));
			});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	result = WorldBlockEditor::place_block_at(*this, world_x, world_y,
		world_z, block_id);
	if (result == FT_ERR_SUCCESS)
		this->fluid_cell_overrides_.swap(staged);
	return (result);
}

int32_t World::replace_fluid_source_at(int32_t world_x, int32_t world_y,
	int32_t world_z, uint32_t expected_block_id,
	uint32_t replacement_block_id, uint64_t expected_voxel_revision)
{
	if ((expected_block_id != GAME_VOXEL_AIR_BLOCK
			&& voxel_block_is_fluid_source(expected_block_id) != FT_TRUE)
		|| (replacement_block_id != GAME_VOXEL_AIR_BLOCK
			&& voxel_block_is_fluid_source(replacement_block_id) != FT_TRUE))
		return (FT_ERR_INVALID_ARGUMENT);
	return (this->replace_block_state_at(world_x, world_y, world_z,
		expected_block_id, replacement_block_id, expected_voxel_revision));
}

int32_t World::replace_block_state_at(int32_t world_x, int32_t world_y,
	int32_t world_z, uint32_t expected_block_id,
	uint32_t replacement_block_id, uint64_t expected_voxel_revision)
{
	std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
	std::vector<FluidCellOverride> staged;
	std::size_t existing_index = 0U;
	bool found = false;
	int32_t result;

	if (world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		staged = this->fluid_cell_overrides_;
		while (existing_index < staged.size())
		{
			if (staged[existing_index].world_x == world_x
				&& staged[existing_index].world_y == world_y
				&& staged[existing_index].world_z == world_z)
			{
				staged[existing_index].block_id = replacement_block_id;
				found = true;
				break;
			}
			++existing_index;
		}
		if (!found)
		{
			if (staged.size() >= 16384U)
				return (FT_ERR_FULL);
			staged.push_back({world_x, world_y, world_z,
				replacement_block_id});
		}
		std::sort(staged.begin(), staged.end(),
			[](const FluidCellOverride &left, const FluidCellOverride &right)
			{
				return (fluid_override_less(left, right));
			});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	result = WorldBlockEditor::replace_block_state(*this, world_x, world_y,
		world_z, expected_block_id, replacement_block_id,
		expected_voxel_revision);
	if (result == FT_ERR_SUCCESS)
		this->fluid_cell_overrides_.swap(staged);
	return (result);
}

int32_t World::till_farmland_at(int32_t world_x, int32_t world_y,
	int32_t world_z, uint64_t expected_voxel_revision)
{
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const WorldChunk *chunk = this->find_chunk(chunk_x, chunk_z);
	uint32_t soil_block_id = GAME_VOXEL_AIR_BLOCK;
	if (world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT
		|| expected_voxel_revision == 0U || chunk == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (chunk->voxel_revision != expected_voxel_revision)
		return (FT_ERR_INVALID_STATE);
	if (!this->block_id_at(world_x, world_y, world_z, &soil_block_id))
		return (FT_ERR_INVALID_ARGUMENT);
	if (soil_block_id != VOXEL_GENERATOR_DIRT_BLOCK_ID
		&& soil_block_id != VOXEL_GENERATOR_GRASS_BLOCK_ID)
		return (FT_ERR_PERMISSION_DENIED);
	return (this->replace_block_state_at(world_x, world_y, world_z,
		soil_block_id, VOXEL_GENERATOR_FARMLAND_BLOCK_ID,
		expected_voxel_revision));
}

bool World::farmland_is_hydrated(int32_t world_x, int32_t world_y,
	int32_t world_z, uint8_t horizontal_radius) const
{
	std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);
	const int32_t radius = static_cast<int32_t>(horizontal_radius);
	uint32_t soil_block_id = GAME_VOXEL_AIR_BLOCK;
	if (horizontal_radius > 8U || world_y < 0
		|| world_y >= GAME_VOXEL_CHUNK_HEIGHT
		|| world_x < std::numeric_limits<int32_t>::min() + radius
		|| world_x > std::numeric_limits<int32_t>::max() - radius
		|| world_z < std::numeric_limits<int32_t>::min() + radius
		|| world_z > std::numeric_limits<int32_t>::max() - radius
		|| !WorldBlockQuery::block_id_at(*this, world_x, world_y, world_z,
			&soil_block_id)
		|| soil_block_id != VOXEL_GENERATOR_FARMLAND_BLOCK_ID)
		return (false);
	for (int32_t water_y = std::max(0, world_y - 1);
		water_y <= std::min(GAME_VOXEL_CHUNK_HEIGHT - 1, world_y + 1);
		++water_y)
	{
		for (int32_t water_z = world_z - radius; water_z <= world_z + radius;
			++water_z)
		{
			for (int32_t water_x = world_x - radius; water_x <= world_x + radius;
				++water_x)
			{
				uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
				if (WorldBlockQuery::block_id_at(*this, water_x, water_y,
					water_z, &block_id)
					&& block_id == VOXEL_GENERATOR_WATER_BLOCK_ID)
					return (true);
			}
		}
	}
	return (false);
}

bool World::has_water_source_near(int32_t world_x, int32_t soil_y,
	int32_t world_z, uint8_t horizontal_radius) const
{
	std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);
	const int32_t radius = static_cast<int32_t>(horizontal_radius);
	if (horizontal_radius > 8U || soil_y < 0
		|| soil_y >= GAME_VOXEL_CHUNK_HEIGHT
		|| world_x < std::numeric_limits<int32_t>::min() + radius
		|| world_x > std::numeric_limits<int32_t>::max() - radius
		|| world_z < std::numeric_limits<int32_t>::min() + radius
		|| world_z > std::numeric_limits<int32_t>::max() - radius)
		return (false);
	for (int32_t water_y = std::max(0, soil_y - 1);
		water_y <= std::min(GAME_VOXEL_CHUNK_HEIGHT - 1, soil_y + 1);
		++water_y)
	{
		for (int32_t water_z = world_z - radius; water_z <= world_z + radius;
			++water_z)
		{
			for (int32_t water_x = world_x - radius; water_x <= world_x + radius;
				++water_x)
			{
				uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
				if (WorldBlockQuery::block_id_at(*this, water_x, water_y,
					water_z, &block_id)
					&& block_id == VOXEL_GENERATOR_WATER_BLOCK_ID)
					return (true);
			}
		}
	}
	return (false);
}

int32_t World::set_block_state_override(int32_t world_x, int32_t world_y,
	int32_t world_z, uint32_t block_id) noexcept
{
	std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
	std::vector<FluidCellOverride> staged;
	std::size_t existing_index = 0U;
	bool found = false;
	if (world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT
		|| voxel_block_is_known(block_id) != FT_TRUE
		|| voxel_block_is_flowing_fluid(block_id) == FT_TRUE)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		staged = this->fluid_cell_overrides_;
		while (existing_index < staged.size())
		{
			if (staged[existing_index].world_x == world_x
				&& staged[existing_index].world_y == world_y
				&& staged[existing_index].world_z == world_z)
			{
				staged[existing_index].block_id = block_id;
				found = true;
				break ;
			}
			++existing_index;
		}
		if (!found)
		{
			if (staged.size() >= 16384U)
				return (FT_ERR_FULL);
			staged.push_back({world_x, world_y, world_z, block_id});
		}
		std::sort(staged.begin(), staged.end(),
			[](const FluidCellOverride &left, const FluidCellOverride &right)
			{
				return (fluid_override_less(left, right));
			});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	this->fluid_cell_overrides_.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t World::serialize_fluid_state(std::vector<uint8_t> &bytes_out) const noexcept
{
	std::shared_lock<std::shared_mutex> read_lock(this->world_data_mutex_);
	std::vector<uint8_t> staged;
	try
	{
		if (this->fluid_cell_overrides_.size() > 16384U)
			return (FT_ERR_FULL);
		staged.reserve(20U + this->fluid_cell_overrides_.size() * 16U);
		staged.push_back('F');
		staged.push_back('L');
		staged.push_back('S');
		staged.push_back('1');
		fluid_state_append_u32(staged, 2U);
		fluid_state_append_u32(staged,
			static_cast<uint32_t>(this->fluid_cell_overrides_.size()));
		for (const FluidCellOverride &entry : this->fluid_cell_overrides_)
		{
			fluid_state_append_i32(staged, entry.world_x);
			fluid_state_append_i32(staged, entry.world_y);
			fluid_state_append_i32(staged, entry.world_z);
			fluid_state_append_u32(staged, entry.block_id);
		}
		fluid_state_append_u64(staged,
			fluid_state_checksum(staged.data(), staged.size()));
		bytes_out.swap(staged);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t World::deserialize_fluid_state(const uint8_t *bytes,
	std::size_t byte_count) noexcept
{
	std::vector<FluidCellOverride> staged;
	uint32_t count;
	uint32_t version;
	std::size_t expected_size;
	uint64_t stored_checksum = 0U;
	uint64_t computed_checksum;

	if (bytes == nullptr && byte_count != 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (byte_count == 0U)
	{
		std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
		this->fluid_cell_overrides_.clear();
		return (FT_ERR_SUCCESS);
	}
	if (byte_count < 20U || byte_count > 1024U * 1024U
		|| std::memcmp(bytes, "FLS1", 4U) != 0)
		return (FT_ERR_INVALID_ARGUMENT);
	version = fluid_state_read_u32(bytes, 4U);
	if (version != 1U && version != 2U)
		return (FT_ERR_INVALID_ARGUMENT);
	count = fluid_state_read_u32(bytes, 8U);
	if (count > 16384U)
		return (FT_ERR_INVALID_ARGUMENT);
	expected_size = 12U + static_cast<std::size_t>(count) * 16U + 8U;
	if (byte_count != expected_size)
		return (FT_ERR_INVALID_ARGUMENT);
	for (uint32_t shift = 0U; shift < 64U; shift += 8U)
		stored_checksum |= static_cast<uint64_t>(bytes[byte_count - 8U
			+ shift / 8U]) << shift;
	computed_checksum = fluid_state_checksum(bytes, byte_count - 8U);
	if (stored_checksum != computed_checksum)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		staged.reserve(count);
		for (uint32_t index = 0U; index < count; ++index)
		{
			const std::size_t offset = 12U
				+ static_cast<std::size_t>(index) * 16U;
			FluidCellOverride entry = {fluid_state_read_i32(bytes, offset),
				fluid_state_read_i32(bytes, offset + 4U),
				fluid_state_read_i32(bytes, offset + 8U),
				fluid_state_read_u32(bytes, offset + 12U)};
			if (entry.world_y < 0 || entry.world_y >= GAME_VOXEL_CHUNK_HEIGHT
				|| voxel_block_is_known(entry.block_id) != FT_TRUE
				|| voxel_block_is_flowing_fluid(entry.block_id) == FT_TRUE
				|| (version == 1U
					&& entry.block_id != GAME_VOXEL_AIR_BLOCK
					&& voxel_block_is_fluid_source(entry.block_id) != FT_TRUE))
				return (FT_ERR_INVALID_ARGUMENT);
			staged.push_back(entry);
		}
		std::sort(staged.begin(), staged.end(),
			[](const FluidCellOverride &left, const FluidCellOverride &right)
			{
				return (fluid_override_less(left, right));
			});
		for (std::size_t index = 1U; index < staged.size(); ++index)
			if (fluid_override_same_cell(staged[index - 1U], staged[index]))
				return (FT_ERR_ALREADY_EXISTS);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
	this->fluid_cell_overrides_.swap(staged);
	for (int32_t index = 0; index < this->chunk_count; ++index)
	{
		bool changed = false;
		if (!this->chunks[index].initialized)
			continue ;
		const int32_t apply_result = this->apply_fluid_state_to_chunk(
			this->chunks[index], &changed);
		if (apply_result != FT_ERR_SUCCESS)
			return (apply_result);
		if (changed)
		{
			this->chunk_streamer.mark_remesh_dirty(this->chunks[index]);
			const int32_t queue_result = this->chunk_streamer.queue_chunk_remesh(
				this->chunks[index]);
			if (queue_result != FT_ERR_SUCCESS && queue_result != FT_ERR_FULL)
				this->chunk_streamer.prioritize_chunk_remesh(
					this->chunks[index].chunk_x, this->chunks[index].chunk_z);
		}
	}
	return (FT_ERR_SUCCESS);
}

int32_t World::apply_fluid_state_to_chunk(WorldChunk &chunk,
	bool *changed_out) noexcept
{
	bool changed = false;
	const int32_t target_chunk_x = chunk.chunk_x;
	const int32_t target_chunk_z = chunk.chunk_z;
	const auto first = std::lower_bound(this->fluid_cell_overrides_.begin(),
		this->fluid_cell_overrides_.end(),
		std::make_pair(target_chunk_x, target_chunk_z),
		[](const FluidCellOverride &entry,
			const std::pair<int32_t, int32_t> &target)
		{
			const int32_t entry_chunk_x = WorldCoordinates::floor_divide(
				entry.world_x, GAME_VOXEL_CHUNK_WIDTH);
			const int32_t entry_chunk_z = WorldCoordinates::floor_divide(
				entry.world_z, GAME_VOXEL_CHUNK_DEPTH);
			return (entry_chunk_x < target.first
				|| (entry_chunk_x == target.first
					&& entry_chunk_z < target.second));
		});
	if (changed_out != nullptr)
		*changed_out = false;
	for (auto current = first; current != this->fluid_cell_overrides_.end();
		++current)
	{
		const FluidCellOverride &entry = *current;
		if (WorldCoordinates::floor_divide(entry.world_x,
				GAME_VOXEL_CHUNK_WIDTH) != target_chunk_x
			|| WorldCoordinates::floor_divide(entry.world_z,
				GAME_VOXEL_CHUNK_DEPTH) != target_chunk_z)
			break ;
		const int64_t offset_x = static_cast<int64_t>(entry.world_x)
			- static_cast<int64_t>(chunk.world_x);
		const int64_t offset_z = static_cast<int64_t>(entry.world_z)
			- static_cast<int64_t>(chunk.world_z);
		uint32_t current_block = 0U;
		int32_t result;

		if (offset_x < 0 || offset_x >= GAME_VOXEL_CHUNK_WIDTH
			|| offset_z < 0 || offset_z >= GAME_VOXEL_CHUNK_DEPTH)
			continue ;
		result = chunk.chunk.read_block(static_cast<int32_t>(offset_x),
			entry.world_y, static_cast<int32_t>(offset_z), &current_block);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (current_block == entry.block_id)
			continue ;
		result = chunk.chunk.write_block(static_cast<int32_t>(offset_x),
			entry.world_y, static_cast<int32_t>(offset_z), entry.block_id);
		if (result != FT_ERR_SUCCESS)
			return (result);
		chunk.mark_content_changed();
		changed = true;
	}
	if (!changed)
		return (FT_ERR_SUCCESS);
	int32_t result = chunk.publish_read_state();
	if (result != FT_ERR_SUCCESS)
		return (result);
	chunk.cancel_remesh_work();
	if (changed_out != nullptr)
		*changed_out = true;
	return (FT_ERR_SUCCESS);
}

int32_t World::apply_authoritative_block_change(
	const game_block_change_request &request, game_block_delta *delta_out)
{
	std::unique_lock<std::shared_mutex> write_lock(this->world_data_mutex_);
	WorldChunk *world_chunk;
	uint64_t previous_revision;
	uint64_t current_revision;
	uint32_t existing_block_id;
	uint8_t existing_light;
	ft_bool incremental_additive_light;
	ft_bool incremental_removal_light;
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
	error_code = world_chunk->chunk.read_block(request.local_x,
		request.local_y, request.local_z, &existing_block_id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (request.requested_block_id != GAME_VOXEL_AIR_BLOCK
		&& voxel_block_is_known(request.requested_block_id) != FT_TRUE)
		return (FT_ERR_INVALID_ARGUMENT);
	if (voxel_block_is_liquid(existing_block_id) == FT_TRUE
		|| voxel_block_is_liquid(request.requested_block_id) == FT_TRUE)
		return (FT_ERR_PERMISSION_DENIED);
	existing_light = world_chunk->light.get(request.local_x, request.local_y,
		request.local_z);
	previous_revision = world_chunk->chunk.get_revision();
	error_code = world_chunk->chunk.apply_authoritative_block_change(request,
		delta_out);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	current_revision = world_chunk->chunk.get_revision();
	if (current_revision == previous_revision)
		return (FT_ERR_SUCCESS);
	world_chunk->voxel_revision = current_revision;
	world_chunk->content_version = world_light_version::next(
		world_chunk->content_version);
	world_chunk->mark_light_input_changed();
	this->mark_geometry_changed();
	this->chunk_streamer.mark_remesh_dirty(*world_chunk);
	error_code = world_chunk->publish_read_state_after_block_edit(
		static_cast<int32_t>(request.local_x),
		static_cast<int32_t>(request.local_y),
		static_cast<int32_t>(request.local_z), request.requested_block_id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	(void)this->invalidate_loom_station_if_block_changed(
		request.chunk_x * GAME_VOXEL_CHUNK_WIDTH
			+ static_cast<int32_t>(request.local_x),
		static_cast<int32_t>(request.local_y),
		request.chunk_z * GAME_VOXEL_CHUNK_DEPTH
			+ static_cast<int32_t>(request.local_z),
		request.requested_block_id);
	/* An accepted authoritative edit supersedes any capture or light solve
	 * based on the previous voxel revision.  Cancel it before submitting the
	 * new incremental request so its result cannot consume worker time or
	 * compete with the edit publication. */
	world_chunk->cancel_remesh_work();
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
	this->chunk_streamer.mark_edit_remeshes(request.chunk_x,
		request.chunk_z, static_cast<int32_t>(request.local_x),
		static_cast<int32_t>(request.local_y),
		static_cast<int32_t>(request.local_z));
	incremental_additive_light = FT_FALSE;
	incremental_removal_light = FT_FALSE;
	if (request.requested_block_id == GAME_VOXEL_AIR_BLOCK)
	{
		if (voxel_block_emitted_light_level(existing_block_id) == 0U)
			incremental_additive_light = FT_TRUE;
		else
			/* Removing an emitter is always a removal solve.  The old light
			 * value may contain sky light as well as block light; that must not
			 * downgrade this to a full rebuild (or leave the edit without an
			 * incremental path).  The removal frontier preserves/restores the
			 * sky channel independently while subtracting the emitter's block
			 * contribution. */
			incremental_removal_light = FT_TRUE;
	}
	else
		incremental_removal_light = FT_TRUE;
	this->chunk_streamer.prioritize_edit_border_remeshes(request.chunk_x,
		request.chunk_z, static_cast<int32_t>(request.local_x),
		static_cast<int32_t>(request.local_y),
		static_cast<int32_t>(request.local_z), incremental_additive_light,
		incremental_removal_light, existing_block_id,
		request.requested_block_id, existing_light);
	/* Authoritative edits follow the same immediate publication path as local
	 * edits. If the single remesh slot is occupied, the priority queue retries
	 * the request without losing the edit notification. */
	if (this->chunk_streamer.queue_chunk_remesh(*world_chunk,
		incremental_additive_light, incremental_removal_light,
		static_cast<int32_t>(request.local_x),
		static_cast<int32_t>(request.local_y),
		static_cast<int32_t>(request.local_z), existing_block_id,
		request.requested_block_id) != FT_ERR_SUCCESS
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
