#include "../../src/world/WorldGenerationResultCommitter.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"
#include "../../Libft/Modules/Basic/limits.hpp"
#include <cstdio>
#if defined(LIBFT_ENABLE_ANALYTICS)
# include <chrono>
#endif

namespace
{
	/* Keep result ownership transfer and deferred generated edits bounded on
	 * the gameplay thread. A completed worker result remains queued when the
	 * budget is exhausted and is committed on a later frame. */
	static const int32_t WORLD_STREAM_MAX_COMMITS_PER_FRAME = 1;
	static const int32_t WORLD_STREAM_MAX_INTERACTIVE_COMMITS_PER_FRAME = 4;
	static const uint32_t WORLD_STREAM_DEFERRED_EDIT_BUDGET_MS = 1U;
	static const uint64_t WORLD_STREAM_BORDER_SOURCE_MAX_AGE_FRAMES = 4U;

#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	static void mesh_light_range(const chunk_mesh &mesh, uint8_t *minimum,
		uint8_t *maximum, std::size_t *nonzero) noexcept
	{
		uint8_t min_value = 255U;
		uint8_t max_value = 0U;
		std::size_t nonzero_count = 0U;
		for (std::size_t index = 0U; index < mesh.vertices.size(); ++index)
		{
			const uint8_t value = mesh.vertices[index].packed_light;
			if (value < min_value)
				min_value = value;
			if (value > max_value)
				max_value = value;
			if (value != 0U)
				nonzero_count += 1U;
		}
		if (mesh.vertices.empty())
			min_value = 0U;
		*minimum = min_value;
		*maximum = max_value;
		*nonzero = nonzero_count;
	}

#endif

	static bool mesh_has_nonzero_light(const chunk_mesh &mesh) noexcept
	{
		for (std::size_t index = 0U; index < mesh.vertices.size(); ++index)
		{
			if (mesh.vertices[index].packed_light != 0U)
				return (true);
		}
		return (false);
	}

	static bool mesh_light_is_severely_regressed(const chunk_mesh &previous,
		const chunk_mesh &replacement) noexcept
	{
		std::size_t previous_nonzero = 0U;
		std::size_t replacement_nonzero = 0U;

		for (const chunk_mesh_vertex &vertex : previous.vertices)
		{
			if (vertex.packed_light != 0U)
				previous_nonzero += 1U;
		}
		for (const chunk_mesh_vertex &vertex : replacement.vertices)
		{
			if (vertex.packed_light != 0U)
				replacement_nonzero += 1U;
		}
		/* A valid local edit may change a few vertices, but it must not turn a
		 * visibly lit mesh into an almost entirely black replacement. */
		return (previous_nonzero >= 16U
			&& replacement_nonzero * 4U < previous_nonzero);
	}

	static bool light_field_has_nonzero(
		const voxel_light_chunk &light) noexcept
	{
		for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
			for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
				for (int32_t x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
					if (light.get(x, y, z) != 0U)
						return (true);
		return (false);
	}

	/* Prepare the sections touched by a compact incremental result before the
	 * live chunk is changed.  voxel_light_section::set() can allocate when a
	 * uniform section becomes non-uniform, so applying deltas directly to the
	 * authoritative buffer is not transactional: a later allocation failure
	 * would leave only a prefix of the frontier visible.  Copy only affected
	 * sections; untouched sections remain in the live publication. */
	static int32_t prepare_incremental_light_sections(
		const voxel_light_chunk &current,
		const std::vector<WorldGenerationPipeline::IncrementalLightDelta> &deltas,
		voxel_light_section prepared[16], bool prepared_valid[16]) noexcept
	{
		std::size_t delta_index;
		uint8_t section_index;
		uint16_t cell_index;
		uint8_t value;
		const voxel_light_section *source_section;
		int32_t error_code;

		for (uint8_t index = 0U; index < 16U; ++index)
			prepared_valid[index] = false;
		delta_index = 0U;
		while (delta_index < deltas.size())
		{
			section_index = static_cast<uint8_t>(
				deltas[delta_index].local_y() >> 4U);
			if (!prepared_valid[section_index])
			{
				source_section = &current.get_section(section_index);
				error_code = prepared[section_index].initialize(
					source_section->is_uniform() != FT_FALSE
						? source_section->get_uniform() : 0U);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				if (source_section->is_uniform() == FT_FALSE)
				{
					cell_index = 0U;
					while (cell_index < 4096U)
					{
						error_code = prepared[section_index].set(cell_index,
							source_section->get(cell_index));
						if (error_code != FT_ERR_SUCCESS)
							return (error_code);
						cell_index += 1U;
					}
				}
				prepared_valid[section_index] = true;
			}
			value = deltas[delta_index].packed_light();
			cell_index = static_cast<uint16_t>(
				(deltas[delta_index].local_y() & 15U) * 256U
				+ deltas[delta_index].local_z() * 16U
				+ deltas[delta_index].local_x());
			error_code = prepared[section_index].set(cell_index, value);
			if (error_code != FT_ERR_SUCCESS)
			return (error_code);
			delta_index += 1U;
		}
		return (FT_ERR_SUCCESS);
	}

	static void commit_incremental_light_sections(voxel_light_chunk &destination,
		voxel_light_section prepared[16], const bool prepared_valid[16]) noexcept
	{
		for (uint8_t index = 0U; index < 16U; ++index)
		{
			if (prepared_valid[index])
			{
				/* Preparation completed all allocations.  Moving a section only
				 * releases the old section and transfers the prepared storage; it
				 * cannot fail or expose an intermediate live value. */
				(void)destination.get_section(index).move(prepared[index]);
			}
		}
	}

	static bool result_has_light_publication(
		const WorldGenerationPipeline::Result &result) noexcept
	{
		const bool geometry_only = (result.stage_mask
			& WorldGenerationPipeline::Result::STAGE_GEOMETRY_ONLY) != 0U;
		const bool geometry_final = (result.stage_mask
			& WorldGenerationPipeline::Result::STAGE_GEOMETRY_FINAL) != 0U;
		const bool compact_incremental_patch =
			result.incremental_light != FT_FALSE
			&& result.incremental_light_deltas_complete != FT_FALSE;
		return (!geometry_only || geometry_final)
			&& (result.light != nullptr || compact_incremental_patch);
	}

	static bool should_stage_border_source(const World &world,
		const WorldGenerationPipeline::Result &result) noexcept
	{
		if (result.operation != WorldGenerationPipeline::WorldGenerationOperation::REMESH
			|| !result_has_light_publication(result))
			return (false);
		for (int32_t index = 0; index < world.chunk_count; ++index)
		{
			const WorldChunk &candidate = world.chunks[index];
			if (candidate.initialized
				&& candidate.waits_for_neighbor_light
				&& candidate.light_dependency_chunk_x == result.chunk_x
				&& candidate.light_dependency_chunk_z == result.chunk_z
				&& candidate.pending_mesh_request_id != 0U)
				return (true);
		}
		return (false);
	}

	static bool result_depends_on(const WorldGenerationPipeline::Result &result,
		const WorldGenerationPipeline::Result &source) noexcept
	{
		return (result.operation
			== WorldGenerationPipeline::WorldGenerationOperation::REMESH
			&& result.dependency_valid != FT_FALSE
			&& result.dependency_chunk_x == source.chunk_x
			&& result.dependency_chunk_z == source.chunk_z
			&& result_has_light_publication(result));
	}
}

WorldGenerationResultCommitter::WorldGenerationResultCommitter()
{
}

WorldGenerationResultCommitter::WorldGenerationResultCommitter(const WorldGenerationResultCommitter &other)
{
	(void)other;
}

WorldGenerationResultCommitter::~WorldGenerationResultCommitter()
{
}

WorldGenerationResultCommitter &WorldGenerationResultCommitter::operator=(const WorldGenerationResultCommitter &other)
{
	(void)other;
	return (*this);
}

int32_t WorldGenerationResultCommitter::move_mesh(chunk_mesh &destination,
	chunk_mesh &source) noexcept
{
	int32_t error_code;

	error_code = destination.vertices.move(source.vertices);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = destination.indices.move(source.indices);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = destination.solid_indices.move(source.solid_indices);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = destination.water_indices.move(source.water_indices);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	destination.bounds = source.bounds;
	destination.occupied_bounds = source.occupied_bounds;
	destination.has_occupied_bounds = source.has_occupied_bounds;
	return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationResultCommitter::commit_remesh_result(World &world,
	WorldGenerationPipeline::Result &result) noexcept
{
	WorldChunk *chunk;
	chunk_mesh replacement_mesh;
	std::unique_ptr<chunk_mesh> retired_mesh;
	int32_t error_code;
	bool geometry_only;
	bool geometry_final;
	bool dependency_stale;
	bool pending_stale;
	bool revision_stale;
	bool light_commit_deferred;
	bool compact_incremental_patch;
	voxel_light_section prepared_incremental_sections[16];
	bool prepared_incremental_sections_valid[16];
#if defined(LIBFT_ENABLE_ANALYTICS)
	const auto commit_start = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point phase_start;
	uint64_t replacement_move_us;
	uint64_t retired_move_us;
	uint64_t destination_initialize_us;
	uint64_t destination_move_us;
	uint64_t light_move_us;
#endif
	world.chunk_streamer.remesh_scanned_cells_ += result.light_scanned_cells;
	world.chunk_streamer.remesh_propagated_cells_ +=
		result.light_propagated_cells;
	if (result.light_queue_peak > world.chunk_streamer.remesh_light_queue_peak_)
		world.chunk_streamer.remesh_light_queue_peak_ = result.light_queue_peak;
	world.chunk_streamer.remesh_completed_count_ += 1U;
	geometry_only = (result.stage_mask
		& WorldGenerationPipeline::Result::STAGE_GEOMETRY_ONLY) != 0U;
	geometry_final = (result.stage_mask
		& WorldGenerationPipeline::Result::STAGE_GEOMETRY_FINAL) != 0U;
	compact_incremental_patch = result.incremental_light != FT_FALSE
		&& result.incremental_light_deltas_complete != FT_FALSE;
	dependency_stale = false;
	pending_stale = false;
	revision_stale = false;
	light_commit_deferred = false;

	chunk = world.find_chunk_mutable(result.chunk_x, result.chunk_z);
	if (result.dependency_valid != FT_FALSE)
	{
		const WorldChunk *dependency = world.find_chunk(
			result.dependency_chunk_x, result.dependency_chunk_z);
		if (dependency == nullptr || !dependency->initialized
			|| dependency->voxel_revision != result.dependency_voxel_revision
			|| dependency->light_revision != result.dependency_light_revision
			|| dependency->content_version != result.dependency_content_version
			|| dependency->light_input_version
				!= result.dependency_light_input_version)
			dependency_stale = true;
	}
	if (chunk == nullptr || !chunk->initialized)
		revision_stale = true;
	else
	{
		pending_stale = chunk->pending_mesh_request_id != result.request_id;
		revision_stale = chunk->voxel_revision != result.voxel_revision
			|| chunk->light_revision != result.light_revision
			|| chunk->content_version != result.content_version
			|| chunk->light_input_version != result.light_input_version;
	}
	if (chunk == nullptr || !chunk->initialized || dependency_stale
		|| pending_stale || revision_stale)
	{
		world.chunk_streamer.stale_result_count_ += 1U;
		world.chunk_streamer.stale_remesh_result_count_ += 1U;
		if (dependency_stale)
			world.chunk_streamer.stale_remesh_dependency_count_ += 1U;
		if (pending_stale)
			world.chunk_streamer.stale_remesh_pending_count_ += 1U;
		if (revision_stale)
			world.chunk_streamer.stale_remesh_revision_count_ += 1U;
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		if (chunk != nullptr && chunk->initialized
			&& (dependency_stale
				|| chunk->light_revision != result.light_revision)
			&& world.chunk_streamer.stale_remesh_result_count_ % 64U == 0U)
		{
			std::fprintf(stderr,
				"[WorldGen] stale light result request="
				FT_UINT64_DECIMAL_FORMAT " chunk=(%d,%d) result_light="
				FT_UINT64_DECIMAL_FORMAT " current_light="
				FT_UINT64_DECIMAL_FORMAT " dependency_stale=%d\n",
				result.request_id,
				result.chunk_x, result.chunk_z,
				result.light_revision, chunk->light_revision,
				dependency_stale ? 1 : 0);
		}
#endif
		if (chunk != nullptr && chunk->initialized
			&& chunk->pending_mesh_request_id == result.request_id)
		{
			chunk->clear_pending_remesh_request();
			chunk->mesh_dirty = true;
			/* The stale result has no authority to describe the replacement
			 * request.  Leave scheduling to the dirty/priority queues so a newer
			 * edit's incremental light seeds cannot be replaced by an empty full
			 * remesh notification. */
		}
		return (FT_ERR_SUCCESS);
	}
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	const bool old_light_valid = chunk->light_buffer_is_valid();
#endif
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (!geometry_only && !geometry_final
		&& result.incremental_light == FT_FALSE
		&& chunk->voxel_revision > 1U)
		std::fprintf(stderr,
			"[WorldGen] non-incremental edited remesh request=%llu "
			"chunk=(%d,%d) voxel=%llu content=%u light_input=%u "
			"interactive=%d pending=%llu\n",
			static_cast<unsigned long long>(result.request_id), result.chunk_x,
			result.chunk_z,
			static_cast<unsigned long long>(chunk->voxel_revision),
			static_cast<unsigned int>(result.content_version),
			static_cast<unsigned int>(result.light_input_version),
			result.interactive_remesh != FT_FALSE ? 1 : 0,
			static_cast<unsigned long long>(chunk->pending_mesh_request_id));
#endif
	if (result.error_code != FT_ERR_SUCCESS || result.mesh == nullptr
		|| (!geometry_only && !geometry_final && result.light == nullptr
			&& !compact_incremental_patch))
	{
		if (result.error_code == FT_ERR_INVALID_STATE)
			world.chunk_streamer.remesh_canceled_count_ += 1U;
		/* A result can finish after its chunk was evicted or its slot was
		 * reused.  The stale-result path above intentionally leaves no live
		 * chunk to repair; do not dereference that absent slot here. */
		if (chunk == nullptr || !chunk->initialized)
			return (FT_ERR_SUCCESS);
		chunk->clear_pending_remesh_request();
		chunk->mesh_dirty = true;
		/* Retry through the normal scheduler.  If this was an edit, its priority
		 * entry still owns the incremental seed; manufacturing a seedless entry
		 * here would force a full relight on the next pass. */
		return (FT_ERR_SUCCESS);
	}
	/* Validate the complete incremental delta before replacing the mesh or
	 * touching the live light buffer.  Applying the list while discovering an
	 * invalid coordinate would otherwise leave a prefix of the frontier visible
	 * and the chunk in a partially updated light state. */
	if (!geometry_only && !geometry_final
		&& result.incremental_light != FT_FALSE
		&& result.incremental_light_deltas_complete != FT_FALSE)
	{
		for (const WorldGenerationPipeline::IncrementalLightDelta &delta
			: result.incremental_light_deltas)
		{
			if (delta.local_x() >= GAME_VOXEL_CHUNK_WIDTH
				|| delta.local_y() >= GAME_VOXEL_CHUNK_HEIGHT
				|| delta.local_z() >= GAME_VOXEL_CHUNK_DEPTH)
			{
				chunk->clear_pending_remesh_request();
				chunk->mesh_dirty = true;
				return (FT_ERR_INVALID_ARGUMENT);
			}
		}
		if (!chunk->light_buffer_is_valid())
		{
			chunk->clear_pending_remesh_request();
			chunk->mesh_dirty = true;
			return (FT_ERR_INVALID_STATE);
		}
		error_code = prepare_incremental_light_sections(chunk->light,
			result.incremental_light_deltas, prepared_incremental_sections,
			prepared_incremental_sections_valid);
		if (error_code != FT_ERR_SUCCESS)
		{
			chunk->clear_pending_remesh_request();
			chunk->mesh_dirty = true;
			return (error_code);
		}
	}
	/* The geometry-only result uses the last valid light snapshot. Publishing
	 * it removes the block immediately without exposing a black mesh. The
	 * request remains pending until the final light-aware result arrives. */
	error_code = chunk_mesh_initialize(replacement_mesh);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	#if defined(LIBFT_ENABLE_ANALYTICS)
	phase_start = std::chrono::steady_clock::now();
	#endif
	error_code = WorldGenerationResultCommitter::move_mesh(replacement_mesh,
		*result.mesh);
	#if defined(LIBFT_ENABLE_ANALYTICS)
	replacement_move_us = static_cast<uint64_t>(std::chrono::duration_cast<
		std::chrono::microseconds>(std::chrono::steady_clock::now()
			- phase_start).count());
	phase_start = std::chrono::steady_clock::now();
	#endif
	if (error_code != FT_ERR_SUCCESS)
	{
		if (chunk_mesh_destroy(replacement_mesh) != FT_ERR_SUCCESS)
			return (FT_ERR_NO_MEMORY);
		return (error_code);
	}
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	{
		uint8_t result_light_min;
		uint8_t result_light_max;
		std::size_t result_light_nonzero;
		mesh_light_range(replacement_mesh, &result_light_min, &result_light_max,
			&result_light_nonzero);
		if (!replacement_mesh.vertices.empty() && result_light_nonzero == 0U)
		{
			uint8_t field_min = 0U;
			uint8_t field_max = 0U;
			std::size_t field_nonzero = 0U;
			if (result.light != nullptr)
			{
				field_min = 255U;
				for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
					for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
						for (int32_t x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
						{
							const uint8_t value = result.light->get(x, y, z);
							if (value < field_min)
								field_min = value;
							if (value > field_max)
								field_max = value;
							if (value != 0U)
								field_nonzero += 1U;
						}
			}
			std::fprintf(stderr,
				"[WorldLight] zero-light remesh result chunk=(%d,%d) request=%llu "
				"geometry_only=%d geometry_final=%d incremental=%d content=%u "
				"light_input=%u field_min=%u field_max=%u field_nonzero=%zu\n",
				result.chunk_x, result.chunk_z,
				static_cast<unsigned long long>(result.request_id),
				geometry_only ? 1 : 0, geometry_final ? 1 : 0,
				result.incremental_light != FT_FALSE ? 1 : 0,
				result.content_version, result.light_input_version,
				static_cast<unsigned int>(field_min),
				static_cast<unsigned int>(field_max), field_nonzero);
		}
	}
	#endif
	if (geometry_only && geometry_final
		&& !mesh_has_nonzero_light(replacement_mesh))
	{
		/* A geometry-only result must never publish an all-zero light field.  The
		 * snapshot may have been labelled valid while its source was still in the
		 * initialization/publication window, and a newly arrived chunk may not
		 * have an older lit mesh to protect.  Keep the previous publication,
		 * invalidate the light readiness state, and let the normal dirty scheduler
		 * submit a full worker-side light solve. */
		(void)chunk_mesh_destroy(replacement_mesh);
		chunk->clear_pending_remesh_request();
		chunk->mesh_dirty = true;
		chunk->light_ready_for_render = false;
		chunk->mark_light_input_changed();
		return (FT_ERR_SUCCESS);
	}
	if (!geometry_only && !geometry_final
		&& result.incremental_light != FT_FALSE
		&& (mesh_light_is_severely_regressed(chunk->mesh, replacement_mesh)
			|| (!mesh_has_nonzero_light(replacement_mesh)
				&& light_field_has_nonzero(chunk->light))))
	{
		/* An incremental frontier is allowed to change nearby cells, but it is
		 * never allowed to turn an already-lit publication into an almost-black
		 * mesh.  Keep the previous complete publication intact, invalidate the
		 * light input revision, and let the normal dirty scheduler submit a full
		 * solve. */
		if (chunk_mesh_destroy(replacement_mesh) != FT_ERR_SUCCESS)
			return (FT_ERR_INTERNAL);
		chunk->clear_pending_remesh_request();
		chunk->mesh_dirty = true;
		chunk->mark_light_input_changed();
		return (FT_ERR_SUCCESS);
	}
	/* Geometry-only remeshes deliberately do not replace the authoritative
	 * light buffer.  Their worker mesh may have been built from an older
	 * immutable snapshot, however, so publishing its packed-light bytes would
	 * let stale/zero light values overwrite a valid live baseline as chunks
	 * enter the render range.  Reapply the current live field to the prepared
	 * mesh before transferring ownership to the chunk. */
	if (geometry_only
		&& chunk->light_version != world_light_version::INVALID_VERSION
		&& chunk->computed_light_input_version
			!= world_light_version::INVALID_VERSION
		&& light_field_has_nonzero(chunk->light))
	{
		error_code = chunk_mesh_apply_light(replacement_mesh, chunk->light);
		if (error_code != FT_ERR_SUCCESS)
		{
			(void)chunk_mesh_destroy(replacement_mesh);
			chunk->clear_pending_remesh_request();
			chunk->mesh_dirty = true;
			return (error_code);
		}
	}
	retired_mesh.reset(new (std::nothrow) chunk_mesh());
	if (retired_mesh == nullptr)
	{
		if (chunk_mesh_destroy(replacement_mesh) != FT_ERR_SUCCESS)
			return (FT_ERR_NO_MEMORY);
		return (FT_ERR_NO_MEMORY);
	}
	error_code = WorldGenerationResultCommitter::move_mesh(*retired_mesh,
		chunk->mesh);
	#if defined(LIBFT_ENABLE_ANALYTICS)
	retired_move_us = static_cast<uint64_t>(std::chrono::duration_cast<
		std::chrono::microseconds>(std::chrono::steady_clock::now()
			- phase_start).count());
	phase_start = std::chrono::steady_clock::now();
	#endif
	if (error_code != FT_ERR_SUCCESS)
	{
		if (chunk_mesh_destroy(replacement_mesh) != FT_ERR_SUCCESS)
			return (FT_ERR_NO_MEMORY);
		return (error_code);
	}
	error_code = chunk_mesh_initialize(chunk->mesh);
	#if defined(LIBFT_ENABLE_ANALYTICS)
	destination_initialize_us = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - phase_start).count());
	phase_start = std::chrono::steady_clock::now();
	#endif
	if (error_code != FT_ERR_SUCCESS)
	{
		(void)WorldGenerationResultCommitter::move_mesh(chunk->mesh,
			*retired_mesh);
		if (chunk_mesh_destroy(replacement_mesh) != FT_ERR_SUCCESS)
			return (FT_ERR_NO_MEMORY);
		return (error_code);
	}
	error_code = WorldGenerationResultCommitter::move_mesh(chunk->mesh,
		replacement_mesh);
	#if defined(LIBFT_ENABLE_ANALYTICS)
	destination_move_us = static_cast<uint64_t>(std::chrono::duration_cast<
		std::chrono::microseconds>(std::chrono::steady_clock::now()
			- phase_start).count());
	phase_start = std::chrono::steady_clock::now();
	#endif
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (!geometry_only && !geometry_final && !light_commit_deferred)
	{
		chunk->clear_pending_remesh_request();
		if (result.incremental_light != FT_FALSE)
		{
			/* Incremental workers carry a complete light view so mesh generation
			 * can use a consistent snapshot, but the authoritative live buffer
			 * must not be replaced wholesale.  Merge only the cells changed by
			 * the bounded frontier.  This keeps unrelated light values and the
			 * previous frame's visible lighting intact while an edge update is
			 * being published. */
			error_code = FT_ERR_SUCCESS;
			if (result.incremental_light_deltas_complete != FT_FALSE)
			{
				commit_incremental_light_sections(chunk->light,
					prepared_incremental_sections,
					prepared_incremental_sections_valid);
				error_code = FT_ERR_SUCCESS;
			}
			else
				error_code = chunk->light.move(*result.light);
		}
		else
			error_code = chunk->light.move(*result.light);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		chunk->light_version = result.content_version;
		chunk->computed_light_input_version = result.light_input_version;
		chunk->light_ready_for_render = true;
		chunk->last_light_remesh_incremental = result.incremental_light;
		if (result.incremental_light != FT_FALSE)
		{
			chunk->last_incremental_light_content_version =
				result.content_version;
			chunk->last_incremental_light_voxel_revision =
				result.voxel_revision;
		}
		if (result.incremental_light != FT_FALSE)
			chunk->incremental_light_protection_until_frame =
				world.chunk_streamer.stream_frame_ + 4U;
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		if (old_light_valid && !chunk->light_buffer_is_valid())
			std::fprintf(stderr,
				"[WorldLight] remesh invalidated live light chunk=(%d,%d) "
				"request=%llu incremental=%d result_light=%s\n",
				chunk->chunk_x, chunk->chunk_z,
				static_cast<unsigned long long>(result.request_id),
				result.incremental_light != FT_FALSE ? 1 : 0,
				result.light == nullptr ? "null" : "present");
	#endif
	#if defined(DEBUG)
		std::fprintf(stderr,
			"[WorldGen] remesh light commit request=%llu chunk=(%d,%d) "
			"incremental=%d geometry_only=%d geometry_final=%d\n",
			static_cast<unsigned long long>(result.request_id), result.chunk_x,
			result.chunk_z, result.incremental_light != FT_FALSE ? 1 : 0,
			geometry_only ? 1 : 0, geometry_final ? 1 : 0);
		#endif
	}
	if (light_commit_deferred)
	{
		/* The geometry is authoritative, but the anomalous light result was not.
		 * Keep the old light versions so the normal dirty scheduler submits a
		 * complete solve instead of allowing a black intermediate to persist. */
		chunk->clear_pending_remesh_request();
		chunk->mesh_dirty = true;
	}
	result.retired_mesh = std::move(retired_mesh);
	#if defined(LIBFT_ENABLE_ANALYTICS)
	light_move_us = static_cast<uint64_t>(std::chrono::duration_cast<
		std::chrono::microseconds>(std::chrono::steady_clock::now()
			- phase_start).count());
	if (replacement_move_us + retired_move_us + destination_initialize_us
		+ destination_move_us + light_move_us >= 8000U)
		std::fprintf(stderr,
			"[Analytics][World] remesh_commit_parts chunk=(%d,%d) "
			"total_us=" FT_UINT64_DECIMAL_FORMAT " replacement_move_us="
			FT_UINT64_DECIMAL_FORMAT " retired_move_us="
			FT_UINT64_DECIMAL_FORMAT " destination_initialize_us="
			FT_UINT64_DECIMAL_FORMAT " destination_move_us="
			FT_UINT64_DECIMAL_FORMAT " light_move_us="
			FT_UINT64_DECIMAL_FORMAT "\n", result.chunk_x, result.chunk_z,
			static_cast<uint64_t>(std::chrono::duration_cast<
				std::chrono::microseconds>(std::chrono::steady_clock::now()
					- commit_start).count()),
			replacement_move_us, retired_move_us, destination_initialize_us,
			destination_move_us, light_move_us);
	#endif
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (!geometry_only || geometry_final)
	{
		if (result.incremental_light != FT_FALSE)
			world.chunk_streamer.remesh_incremental_completed_count_ += 1U;
		else
			world.chunk_streamer.remesh_full_completed_count_ += 1U;
	}
	else
		world.chunk_streamer.remesh_geometry_only_count_ += 1U;
	chunk->mesh_revision += 1U;
	chunk->mesh_dirty = geometry_only && !geometry_final;
	if (!geometry_only || geometry_final)
	{
		chunk->last_mesh_publication_interactive = result.interactive_remesh;
		chunk->last_mesh_publication_voxel_revision = result.voxel_revision;
	}
	if (!geometry_only || geometry_final)
		chunk->clear_pending_remesh_request();
	world.mark_geometry_changed();
	error_code = chunk->publish_read_state();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (chunk->voxel_revision > 1U
		&& world.chunk_streamer.remesh_completed_count_ % 64U == 0U)
		std::fprintf(stderr,
			"[RendererTrace] remesh committed chunk=(%d,%d) voxel="
			FT_UINT64_DECIMAL_FORMAT " mesh=" FT_UINT64_DECIMAL_FORMAT
			" light=" FT_UINT64_DECIMAL_FORMAT "\n",
			chunk->chunk_x, chunk->chunk_z,
			chunk->voxel_revision, chunk->mesh_revision, chunk->light_revision);
	#endif
	return (FT_ERR_SUCCESS);
}

void WorldGenerationResultCommitter::populate_chunk_slot(WorldChunk &slot,
	const WorldGenerationPipeline::Result &result,
	uint64_t geometry_revision) noexcept
{
	slot.chunk_x = result.chunk_x;
	slot.chunk_z = result.chunk_z;
	slot.world_x = result.chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	slot.world_z = result.chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	slot.initialized = true;
	/* The world geometry revision is unique across slot reuse and pipeline
	 * resets. Keep it in the renderer-visible identity so reloading the same
	 * coordinates cannot be mistaken for the old GPU mesh. */
	slot.mesh_revision = geometry_revision == 0U ? 1U : geometry_revision;
	slot.voxel_revision = 1U;
	slot.light_revision = 1U;
	slot.content_version = 1U;
	slot.light_version = 1U;
	slot.light_input_version = 1U;
	slot.computed_light_input_version = 1U;
	slot.last_mesh_publication_interactive = FT_FALSE;
	slot.last_mesh_publication_voxel_revision = slot.voxel_revision;
	slot.last_light_remesh_incremental = FT_FALSE;
	slot.last_incremental_light_content_version = 0U;
	slot.last_incremental_light_voxel_revision = 0U;
	slot.clear_pending_remesh_request();
	slot.mesh_dirty = true;
}

int32_t WorldGenerationResultCommitter::create_chunk_from_stream_result(WorldChunkStreamer &streamer,
	World &world, WorldGenerationPipeline::Result &result,
	WorldChunkStreamer::StreamCandidate &candidate) noexcept
{
	WorldChunk *slot;
	int32_t error_code;
	int32_t cleanup_error;
#if defined(LIBFT_ENABLE_ANALYTICS)
	std::chrono::steady_clock::time_point phase_start;
	uint64_t transfer_us;
	uint64_t index_us;
	uint64_t deferred_us;
	uint64_t neighbor_us;
#endif

	slot = WorldChunkStore::find_free_chunk_slot(world.chunks,
			world.chunk_count);
	if (slot == nullptr)
	{
		candidate.state = WorldChunkStreamer::CANDIDATE_FAILED_RETRYABLE;
		candidate.last_error = FT_ERR_NO_MEMORY;
		candidate.retry_frames = 1;
		return (FT_ERR_SUCCESS);
	}
	#if defined(LIBFT_ENABLE_ANALYTICS)
	phase_start = std::chrono::steady_clock::now();
	#endif
	if (slot->chunk.move(result.chunk->chunk) != FT_ERR_SUCCESS
		|| slot->light.move(result.chunk->light) != FT_ERR_SUCCESS
		|| chunk_mesh_initialize(slot->mesh) != FT_ERR_SUCCESS
		|| WorldGenerationResultCommitter::move_mesh(slot->mesh,
			result.chunk->mesh) != FT_ERR_SUCCESS)
	{
		/* The slot is not marked initialized until the complete payload has
		 * transferred, so WorldChunk::destroy() would otherwise skip cleanup of
		 * a voxel chunk moved before the mesh failed. */
		cleanup_error = chunk_mesh_destroy(slot->mesh);
		error_code = slot->chunk.destroy();
		if (cleanup_error == FT_ERR_SUCCESS)
			cleanup_error = error_code;
		slot->reset_coordinates();
		candidate.state = WorldChunkStreamer::CANDIDATE_FAILED_RETRYABLE;
		candidate.last_error = cleanup_error == FT_ERR_SUCCESS
			? FT_ERR_NO_MEMORY : cleanup_error;
		candidate.retry_frames = 1;
		return (FT_ERR_SUCCESS);
	}
	#if defined(LIBFT_ENABLE_ANALYTICS)
	transfer_us = static_cast<uint64_t>(std::chrono::duration_cast<
		std::chrono::microseconds>(std::chrono::steady_clock::now()
			- phase_start).count());
	phase_start = std::chrono::steady_clock::now();
	#endif
	world.loaded_chunk_count += 1;
	world.mark_geometry_changed();
	/* The generation worker publishes a mesh paired with its completed local
	 * light field.  Preserve that readiness through the slot transfer so the
	 * renderer can use the baseline immediately while border refinement runs. */
	slot->light_ready_for_render = result.chunk->light_ready_for_render;
	WorldGenerationResultCommitter::populate_chunk_slot(*slot, result,
		world.geometry_revision);
	#if defined(LIBFT_ENABLE_ANALYTICS)
	if (world.loaded_chunk_count <= 16)
	{
		uint8_t light_min;
		uint8_t light_max;
		std::size_t light_nonzero;
		mesh_light_range(slot->mesh, &light_min, &light_max, &light_nonzero);
		std::fprintf(stderr,
			"[Analytics][World] generated_mesh_light chunk=(%d,%d) "
			"vertices=%zu min=%u max=%u nonzero=%zu ready=%d\n",
			slot->chunk_x, slot->chunk_z, slot->mesh.vertices.size(),
			static_cast<unsigned int>(light_min),
			static_cast<unsigned int>(light_max), light_nonzero,
			slot->light_ready_for_render ? 1 : 0);
	}
	#endif
	error_code = slot->publish_read_state();
	if (error_code != FT_ERR_SUCCESS)
	{
		slot->destroy();
		candidate.state = WorldChunkStreamer::CANDIDATE_FAILED_RETRYABLE;
		candidate.last_error = error_code;
		candidate.retry_frames = 1;
		return (FT_ERR_SUCCESS);
	}
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (slot->light_ready_for_render
		&& slot->mesh.has_occupied_bounds != FT_FALSE
		&& !mesh_has_nonzero_light(slot->mesh))
	{
		std::fprintf(stderr,
			"[WorldGen] zero-light generated publication chunk=(%d,%d) "
			"content=%u light=%u input=%u computed=%u vertices=%zu\n",
			slot->chunk_x, slot->chunk_z, slot->content_version,
			slot->light_version, slot->light_input_version,
			slot->computed_light_input_version, slot->mesh.vertices.size());
	}
	#endif
	world.register_chunk_index(*slot);
	#if defined(LIBFT_ENABLE_ANALYTICS)
	index_us = static_cast<uint64_t>(std::chrono::duration_cast<
		std::chrono::microseconds>(std::chrono::steady_clock::now()
			- phase_start).count());
	phase_start = std::chrono::steady_clock::now();
	#endif
	candidate.state = WorldChunkStreamer::CANDIDATE_READY;
	candidate.retry_count = 0U;
	candidate.retry_frames = 0;
	candidate.last_error = FT_ERR_SUCCESS;
	candidate.queued_frame = 0U;
	streamer.stream_progress_frame_ = streamer.stream_frame_;
	streamer.deferred_edits_.insert(streamer.deferred_edits_.end(),
		result.deferred_edits.begin(), result.deferred_edits.end());
	if (!result.deferred_edits.empty())
	{
		if (streamer.deferred_edits_sorted_)
		streamer.deferred_sorted_end_ = streamer.deferred_edits_.size()
				- result.deferred_edits.size();
		streamer.deferred_edits_sorted_ = false;
	}
	#if defined(LIBFT_ENABLE_ANALYTICS)
	deferred_us = static_cast<uint64_t>(std::chrono::duration_cast<
		std::chrono::microseconds>(std::chrono::steady_clock::now()
			- phase_start).count());
	phase_start = std::chrono::steady_clock::now();
	#endif
	streamer.mark_neighbor_remeshes(result.chunk_x, result.chunk_z, false, true);
	error_code = FT_ERR_SUCCESS;
	#if defined(LIBFT_ENABLE_ANALYTICS)
	neighbor_us = static_cast<uint64_t>(std::chrono::duration_cast<
		std::chrono::microseconds>(std::chrono::steady_clock::now()
			- phase_start).count());
	if (transfer_us + index_us + deferred_us + neighbor_us >= 8000U)
		std::fprintf(stderr,
			"[Analytics][World] commit_parts chunk=(%d,%d) transfer_us="
			FT_UINT64_DECIMAL_FORMAT " index_us=" FT_UINT64_DECIMAL_FORMAT
			" deferred_us=" FT_UINT64_DECIMAL_FORMAT " neighbor_us="
			FT_UINT64_DECIMAL_FORMAT "\n",
			result.chunk_x, result.chunk_z,
			transfer_us, index_us, deferred_us, neighbor_us);
	#endif
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (FT_ERR_SUCCESS);
}

int32_t WorldGenerationResultCommitter::commit_stream_result(WorldChunkStreamer &streamer,
	World &world, WorldGenerationPipeline::Result &result) noexcept
{
	WorldChunkStreamer::StreamCandidate *candidate;

	candidate = WorldChunkCandidateScanner::find_stream_candidate(streamer,
			result.chunk_x, result.chunk_z);
	if (candidate == nullptr || candidate->request_id != result.request_id
		|| candidate->relevance_epoch != result.relevance_epoch
		|| candidate->generation_revision != result.generation_revision)
	{
		streamer.stale_result_count_ += 1U;
		streamer.stale_stream_result_count_ += 1U;
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		if (streamer.stale_stream_result_count_ % 64U == 0U)
		{
			std::fprintf(stderr,
			"[WorldGen] stale result request=" FT_UINT64_DECIMAL_FORMAT
			" chunk=(%d,%d) candidate=%s candidate_request="
			FT_UINT64_DECIMAL_FORMAT " result_epoch="
			FT_UINT64_DECIMAL_FORMAT " candidate_epoch="
			FT_UINT64_DECIMAL_FORMAT
			" result_revision=%u candidate_revision=%u\n",
			result.request_id, result.chunk_x,
			result.chunk_z, candidate == nullptr ? "missing" : "mismatch",
			candidate == nullptr ? UINT64_C(0) : candidate->request_id,
			result.relevance_epoch,
			candidate == nullptr ? UINT64_C(0) : candidate->relevance_epoch,
			result.generation_revision,
			candidate == nullptr ? 0U : candidate->generation_revision);
		}
#endif
		return (FT_ERR_SUCCESS);
	}
	if (result.error_code != FT_ERR_SUCCESS || result.chunk == nullptr)
	{
		candidate->state = WorldChunkStreamer::CANDIDATE_FAILED_RETRYABLE;
		candidate->retry_count += 1U;
		candidate->last_error = result.error_code;
		candidate->retry_frames = 1 << std::min(candidate->retry_count, 6U);
		streamer.stream_last_error_ = result.error_code;
		streamer.stream_retryable_count_ += 1;
		return (FT_ERR_SUCCESS);
	}
	if (world.find_chunk(result.chunk_x, result.chunk_z) != nullptr)
	{
		candidate->state = WorldChunkStreamer::CANDIDATE_READY;
		return (FT_ERR_SUCCESS);
	}
	return (WorldGenerationResultCommitter::create_chunk_from_stream_result(streamer,
			world, result, *candidate));
}

int32_t WorldGenerationResultCommitter::commit(WorldChunkStreamer &streamer,
	World &world, WorldGenerationPipeline::Result &result) noexcept
{
	if (result.operation == WorldGenerationPipeline::WorldGenerationOperation::REGENERATE)
		return (WorldRegenerationResultApplier::commit(streamer, world,
				result));
	if (result.operation == WorldGenerationPipeline::WorldGenerationOperation::REMESH)
		return (WorldGenerationResultCommitter::commit_remesh_result(world,
				result));
	return (WorldGenerationResultCommitter::commit_stream_result(streamer,
			world, result));
}

int32_t WorldGenerationResultCommitter::drain(WorldChunkStreamer &streamer,
	World &world) noexcept
{
	std::unique_ptr<WorldGenerationPipeline::Result> result;
	int32_t processed;
	int32_t commit_limit;
	std::chrono::steady_clock::time_point deadline;
	int32_t error_code;
	int32_t analytics_error;
	int32_t poll_error;
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	std::size_t queued_before;
	std::size_t completed_before;
	std::size_t queued_after;
	std::size_t completed_after;
	uint64_t poll_us;
	uint64_t cleanup_us;
	uint64_t last_poll_us;
	uint64_t last_cleanup_us;
	uint64_t last_commit_us;
	uint64_t drain_start_us;
#endif

	processed = 0;
	/* A normal streaming frame commits one result to keep publication work
	 * bounded.  Once an interactive remesh is queued, allowing a small bounded
	 * drain prevents stale generation results from delaying the player's edit
	 * for one result per frame.  This is still a fixed limit; it is not an
	 * unbounded catch-up loop on the gameplay thread. */
	commit_limit = streamer.priority_remesh_pending_
		? WORLD_STREAM_MAX_INTERACTIVE_COMMITS_PER_FRAME
		: WORLD_STREAM_MAX_COMMITS_PER_FRAME;
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	queued_before = streamer.generation_pipeline_.queued_count();
	completed_before = streamer.generation_pipeline_.completed_count();
	last_poll_us = 0U;
	last_cleanup_us = 0U;
	last_commit_us = 0U;
	drain_start_us = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
#if !defined(LIBFT_ENABLE_ANALYTICS)
	(void)last_poll_us;
	(void)last_cleanup_us;
	(void)last_commit_us;
	(void)drain_start_us;
#endif
#endif
	deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
	while (processed < commit_limit
		&& (processed == 0 || std::chrono::steady_clock::now() < deadline))
	{
		/* A source result is held only while its explicitly dependent border
		 * result has a chance to arrive.  The fallback is checked before polling
		 * so a result already in the pipeline cannot be consumed and then lost
		 * while the source is committed. */
		if (streamer.staged_border_source_result_ != nullptr
			&& streamer.stream_frame_
				>= streamer.staged_border_source_frame_
				+ WORLD_STREAM_BORDER_SOURCE_MAX_AGE_FRAMES)
		{
			error_code = WorldGenerationResultCommitter::commit(streamer, world,
				*streamer.staged_border_source_result_);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			streamer.generation_pipeline_.retire_result(
				std::move(streamer.staged_border_source_result_));
			streamer.staged_border_source_frame_ = 0U;
			processed += 1;
			continue ;
		}
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		const auto poll_start = std::chrono::steady_clock::now();
#endif
		poll_error = streamer.generation_pipeline_.poll(result);
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		poll_us = static_cast<uint64_t>(std::chrono::duration_cast<
			std::chrono::microseconds>(std::chrono::steady_clock::now()
				- poll_start).count());
		last_poll_us = poll_us;
#endif
		if (poll_error != FT_ERR_SUCCESS)
			break ;
		/* Border lighting is computed against a source snapshot.  Publishing
		 * the source and the result that consumed that snapshot in the same
		 * drain pass prevents a visible one-frame invalidation at the boundary.
		 * Both commits still perform their normal revision/dependency checks. */
		if (streamer.staged_border_source_result_ != nullptr
			&& result_depends_on(*result,
				*streamer.staged_border_source_result_))
		{
			error_code = WorldGenerationResultCommitter::commit(streamer, world,
				*streamer.staged_border_source_result_);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			streamer.generation_pipeline_.retire_result(
				std::move(streamer.staged_border_source_result_));
			streamer.staged_border_source_frame_ = 0U;
			error_code = WorldGenerationResultCommitter::commit(streamer, world,
				*result);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			streamer.generation_pipeline_.retire_result(std::move(result));
			processed += 1;
			continue ;
		}
		if (streamer.staged_border_source_result_ == nullptr
			&& should_stage_border_source(world, *result))
		{
			streamer.staged_border_source_result_ = std::move(result);
			streamer.staged_border_source_frame_ = streamer.stream_frame_;
			continue ;
		}
#if defined(LIBFT_ENABLE_ANALYTICS)
		const uint64_t result_request_id = result->request_id;
		const int32_t result_chunk_x = result->chunk_x;
		const int32_t result_chunk_z = result->chunk_z;
		const std::size_t result_deferred_count = result->deferred_edits.size();
		const uint8_t result_operation =
			static_cast<uint8_t>(result->operation);
		const uint64_t result_generation_ns =
			result->generation_duration_nanoseconds;
		const uint64_t result_mesh_ns = result->mesh_duration_nanoseconds;
		const auto commit_start = std::chrono::steady_clock::now();
#endif
		analytics_error = RuntimeAnalytics::begin_scope(
			RuntimeAnalyticsScope::WORLD_STREAM_COMMIT);
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr,
				"Analytics: stream commit scope start failed (%d)\n",
				analytics_error);
		error_code = WorldGenerationResultCommitter::commit(streamer, world,
				*result);
		analytics_error = RuntimeAnalytics::end_scope();
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr,
				"Analytics: stream commit scope end failed (%d)\n",
				analytics_error);
#if defined(LIBFT_ENABLE_ANALYTICS)
		const uint64_t commit_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - commit_start).count());
		if (commit_us >= 8000U)
			std::fprintf(stderr,
				"[Analytics][World] slow commit request="
				FT_UINT64_DECIMAL_FORMAT " operation=%u chunk=(%d,%d) "
				"deferred_edits=%zu duration_us="
				FT_UINT64_DECIMAL_FORMAT "\n",
				result_request_id,
				static_cast<unsigned int>(result_operation), result_chunk_x,
				result_chunk_z, result_deferred_count,
				commit_us);
		last_commit_us = commit_us;
		if (result_generation_ns + result_mesh_ns >= 8000000U
			&& result_request_id % 32U == 0U)
			std::fprintf(stderr,
				"[Analytics][World] slow worker request="
				FT_UINT64_DECIMAL_FORMAT " chunk=(%d,%d) generation_us="
				FT_UINT64_DECIMAL_FORMAT " mesh_us="
				FT_UINT64_DECIMAL_FORMAT "\n",
				result_request_id,
				result_chunk_x, result_chunk_z,
				result_generation_ns / 1000U,
				result_mesh_ns / 1000U);
#endif
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		const auto cleanup_start = std::chrono::steady_clock::now();
	#endif
		streamer.generation_pipeline_.retire_result(std::move(result));
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		cleanup_us = static_cast<uint64_t>(std::chrono::duration_cast<
			std::chrono::microseconds>(std::chrono::steady_clock::now()
				- cleanup_start).count());
		last_cleanup_us = cleanup_us;
	#endif
		processed += 1;
	}
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::WORLD_STREAM_DEFERRED_EDITS);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"Analytics: deferred-edit scope start failed (%d)\n",
			analytics_error);
#if defined(LIBFT_ENABLE_ANALYTICS)
	const std::size_t deferred_before = streamer.deferred_edits_.size();
	const auto deferred_start = std::chrono::steady_clock::now();
#endif
	error_code = WorldDeferredEditApplier::apply(streamer, world, 16U,
		WORLD_STREAM_DEFERRED_EDIT_BUDGET_MS);
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"Analytics: deferred-edit scope end failed (%d)\n",
			analytics_error);
#if defined(LIBFT_ENABLE_ANALYTICS)
	const uint64_t deferred_us = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - deferred_start).count());
	if (deferred_us >= 8000U)
		std::fprintf(stderr,
			"[Analytics][World] slow deferred edits before=%zu after=%zu "
			"duration_us=" FT_UINT64_DECIMAL_FORMAT "\n", deferred_before,
			streamer.deferred_edits_.size(), deferred_us);
	const uint64_t drain_us = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count())
		- drain_start_us;
	if (drain_us >= 8000U)
		std::fprintf(stderr,
			"[Analytics][World] drain_parts total_us="
			FT_UINT64_DECIMAL_FORMAT " poll_us=" FT_UINT64_DECIMAL_FORMAT
			" commit_us=" FT_UINT64_DECIMAL_FORMAT " cleanup_us="
			FT_UINT64_DECIMAL_FORMAT " deferred_us="
			FT_UINT64_DECIMAL_FORMAT " processed=%d\n", drain_us,
			last_poll_us, last_commit_us, last_cleanup_us, deferred_us,
			processed);
#endif
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	queued_after = streamer.generation_pipeline_.queued_count();
	completed_after = streamer.generation_pipeline_.completed_count();
	if (streamer.stream_frame_ % 120U == 0U
		&& (queued_before != 0U || completed_before != 0U
			|| queued_after != 0U || completed_after != 0U))
		std::fprintf(stderr,
			"[WorldGen] commit frame=" FT_UINT64_DECIMAL_FORMAT
			" queued=%zu->%zu completed=%zu->%zu processed=%d "
			"oldest_result_ns=" FT_UINT64_DECIMAL_FORMAT "\n",
			streamer.stream_frame_,
			queued_before, queued_after, completed_before, completed_after,
			processed,
			streamer.generation_pipeline_.oldest_completed_result_age_nanoseconds());
#endif
	return (error_code);
}
