#include "../../src/world/WorldGenerationResultCommitter.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"
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
	static const uint32_t WORLD_STREAM_DEFERRED_EDIT_BUDGET_MS = 1U;
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
	int32_t error_code;

	chunk = world.find_chunk_mutable(result.chunk_x, result.chunk_z);
	if (chunk == nullptr || !chunk->initialized
		|| chunk->pending_mesh_request_id != result.request_id
		|| chunk->voxel_revision != result.voxel_revision)
		return (FT_ERR_SUCCESS);
	chunk->pending_mesh_request_id = 0U;
	if (result.error_code != FT_ERR_SUCCESS || result.mesh == nullptr)
		return (FT_ERR_SUCCESS);
	error_code = chunk_mesh_initialize(replacement_mesh);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = WorldGenerationResultCommitter::move_mesh(replacement_mesh,
		*result.mesh);
	if (error_code != FT_ERR_SUCCESS)
	{
		if (chunk_mesh_destroy(replacement_mesh) != FT_ERR_SUCCESS)
			return (FT_ERR_NO_MEMORY);
		return (error_code);
	}
	error_code = chunk_mesh_destroy(chunk->mesh);
	if (error_code != FT_ERR_SUCCESS)
	{
		if (chunk_mesh_destroy(replacement_mesh) != FT_ERR_SUCCESS)
			return (FT_ERR_NO_MEMORY);
		return (error_code);
	}
	error_code = chunk_mesh_initialize(chunk->mesh);
	if (error_code != FT_ERR_SUCCESS)
	{
		if (chunk_mesh_destroy(replacement_mesh) != FT_ERR_SUCCESS)
			return (FT_ERR_NO_MEMORY);
		return (error_code);
	}
	error_code = WorldGenerationResultCommitter::move_mesh(chunk->mesh,
		replacement_mesh);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	chunk->mesh_revision += 1U;
	chunk->mesh_dirty = false;
	world.mark_geometry_changed();
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
	slot.pending_mesh_request_id = 0U;
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
	WorldGenerationResultCommitter::populate_chunk_slot(*slot, result,
		world.geometry_revision);
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
	streamer.mark_neighbor_remeshes(result.chunk_x, result.chunk_z);
	error_code = FT_ERR_SUCCESS;
	#if defined(LIBFT_ENABLE_ANALYTICS)
	neighbor_us = static_cast<uint64_t>(std::chrono::duration_cast<
		std::chrono::microseconds>(std::chrono::steady_clock::now()
			- phase_start).count());
	if (transfer_us + index_us + deferred_us + neighbor_us >= 8000U)
		std::fprintf(stderr,
			"[Analytics][World] commit_parts chunk=(%d,%d) transfer_us=%llu "
			"index_us=%llu deferred_us=%llu neighbor_us=%llu\n",
			result.chunk_x, result.chunk_z,
			static_cast<unsigned long long>(transfer_us),
			static_cast<unsigned long long>(index_us),
			static_cast<unsigned long long>(deferred_us),
			static_cast<unsigned long long>(neighbor_us));
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
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		std::fprintf(stderr,
			"[WorldGen] stale result request=%llu chunk=(%d,%d) "
			"candidate=%s candidate_request=%llu result_epoch=%llu "
			"candidate_epoch=%llu result_revision=%u candidate_revision=%u\n",
			static_cast<unsigned long long>(result.request_id), result.chunk_x,
			result.chunk_z, candidate == nullptr ? "missing" : "mismatch",
			candidate == nullptr ? 0ULL
				: static_cast<unsigned long long>(candidate->request_id),
			static_cast<unsigned long long>(result.relevance_epoch),
			candidate == nullptr ? 0ULL
				: static_cast<unsigned long long>(candidate->relevance_epoch),
			result.generation_revision,
			candidate == nullptr ? 0U : candidate->generation_revision);
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
	while (processed < WORLD_STREAM_MAX_COMMITS_PER_FRAME
		&& (processed == 0 || std::chrono::steady_clock::now() < deadline))
	{
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
				"[Analytics][World] slow commit request=%llu operation=%u "
				"chunk=(%d,%d) deferred_edits=%zu duration_us=%llu\n",
				static_cast<unsigned long long>(result_request_id),
				static_cast<unsigned int>(result_operation), result_chunk_x,
				result_chunk_z, result_deferred_count,
				static_cast<unsigned long long>(commit_us));
		last_commit_us = commit_us;
		if (result_generation_ns + result_mesh_ns >= 8000000U)
			std::fprintf(stderr,
				"[Analytics][World] slow worker request=%llu chunk=(%d,%d) "
				"generation_us=%llu mesh_us=%llu\n",
				static_cast<unsigned long long>(result_request_id),
				result_chunk_x, result_chunk_z,
				static_cast<unsigned long long>(result_generation_ns / 1000U),
				static_cast<unsigned long long>(result_mesh_ns / 1000U));
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
			"duration_us=%llu\n", deferred_before,
			streamer.deferred_edits_.size(),
			static_cast<unsigned long long>(deferred_us));
	const uint64_t drain_us = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count())
		- drain_start_us;
	if (drain_us >= 8000U)
		std::fprintf(stderr,
			"[Analytics][World] drain_parts total_us=%llu poll_us=%llu "
			"commit_us=%llu cleanup_us=%llu deferred_us=%llu processed=%d\n",
			static_cast<unsigned long long>(drain_us),
			static_cast<unsigned long long>(last_poll_us),
			static_cast<unsigned long long>(last_commit_us),
			static_cast<unsigned long long>(last_cleanup_us),
			static_cast<unsigned long long>(deferred_us), processed);
#endif
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	queued_after = streamer.generation_pipeline_.queued_count();
	completed_after = streamer.generation_pipeline_.completed_count();
	if (streamer.stream_frame_ % 120U == 0U
		&& (queued_before != 0U || completed_before != 0U
			|| queued_after != 0U || completed_after != 0U))
		std::fprintf(stderr,
			"[WorldGen] commit frame=%llu queued=%zu->%zu "
			"completed=%zu->%zu processed=%d oldest_result_ns=%llu\n",
			static_cast<unsigned long long>(streamer.stream_frame_),
			queued_before, queued_after, completed_before, completed_after,
			processed,
			static_cast<unsigned long long>(
				streamer.generation_pipeline_.oldest_completed_result_age_nanoseconds()));
#endif
	return (error_code);
}
