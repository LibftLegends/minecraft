#include "../../src/world/WorldRegenerationResultApplier.hpp"
#include <cstdio>

WorldRegenerationResultApplier::WorldRegenerationResultApplier()
{
}

WorldRegenerationResultApplier::WorldRegenerationResultApplier(const WorldRegenerationResultApplier &other)
{
	(void)other;
}

WorldRegenerationResultApplier::~WorldRegenerationResultApplier()
{
}

WorldRegenerationResultApplier &WorldRegenerationResultApplier::operator=(const WorldRegenerationResultApplier &other)
{
	(void)other;
	return (*this);
}

void WorldRegenerationResultApplier::apply_chunk(WorldChunkStreamer &streamer,
	World &world, WorldGenerationPipeline::Result &result) noexcept
{
	WorldChunk *chunk;
	WorldChunk replacement;
	int32_t transfer_error;

	chunk = world.find_chunk_mutable(result.chunk_x, result.chunk_z);
	if (chunk == nullptr || !chunk->initialized)
	{
		world.revision_manager.record_regeneration_skipped();
		return ;
	}
	/* Build the replacement off to the side.  Regeneration is allowed to fail
	 * (allocation, cancellation, or a malformed worker result); the currently
	 * published chunk, including its light field, must remain drawable until
	 * the complete replacement is ready. */
	transfer_error = replacement.chunk.move(result.chunk->chunk);
	if (transfer_error == FT_ERR_SUCCESS)
		transfer_error = replacement.light.move(result.chunk->light);
	if (transfer_error == FT_ERR_SUCCESS)
		transfer_error = chunk_mesh_initialize(replacement.mesh);
	if (transfer_error == FT_ERR_SUCCESS)
		transfer_error = WorldGenerationResultCommitter::move_mesh(
			replacement.mesh, result.chunk->mesh);
	if (transfer_error != FT_ERR_SUCCESS)
	{
		replacement.destroy();
		world.revision_manager.record_regeneration_error(FT_ERR_NO_MEMORY);
		return ;
	}
	replacement.chunk_x = result.chunk_x;
	replacement.chunk_z = result.chunk_z;
	replacement.world_x = result.chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	replacement.world_z = result.chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	replacement.initialized = true;
	/* The final ownership transfer is now the only operation that can replace
	 * the live chunk.  All worker-owned payloads have already been validated and
	 * transferred into a complete replacement. */
	if (chunk->move(replacement) != FT_ERR_SUCCESS)
	{
		replacement.destroy();
		world.revision_manager.record_regeneration_error(FT_ERR_NO_MEMORY);
		return ;
	}
	chunk->chunk_x = result.chunk_x;
	chunk->chunk_z = result.chunk_z;
	chunk->world_x = result.chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	chunk->world_z = result.chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	chunk->initialized = true;
	chunk->mesh_revision += 1U;
	chunk->voxel_revision += 1U;
	chunk->light_revision += 1U;
	chunk->content_version = 1U;
	/* The regeneration worker publishes a mesh paired with a complete local
	 * light field.  Keep that field authoritative immediately; invalidating it
	 * here made the renderer treat a valid regenerated chunk as black until a
	 * second remesh happened to complete. */
	chunk->light_version = 1U;
	chunk->light_input_version = 1U;
	chunk->computed_light_input_version = 1U;
	chunk->light_ready_for_render = result.chunk->light_ready_for_render;
	chunk->last_light_remesh_incremental = FT_FALSE;
	chunk->last_incremental_light_content_version = 0U;
	chunk->last_incremental_light_voxel_revision = 0U;
	chunk->clear_pending_remesh_request();
	chunk->mesh_dirty = true;
	if (chunk->publish_read_state() != FT_ERR_SUCCESS)
	{
		world.revision_manager.record_regeneration_error(FT_ERR_NO_MEMORY);
		return ;
	}
	world.mark_geometry_changed();
	world.revision_manager.record_regeneration_success();
	streamer.mark_neighbor_remeshes(result.chunk_x, result.chunk_z, false, true);
	streamer.deferred_edits_.insert(streamer.deferred_edits_.end(),
		result.deferred_edits.begin(), result.deferred_edits.end());
	if (!result.deferred_edits.empty())
	{
		if (streamer.deferred_edits_sorted_)
			streamer.deferred_sorted_end_ = streamer.deferred_edits_.size()
				- result.deferred_edits.size();
		streamer.deferred_edits_sorted_ = false;
	}
}

int32_t WorldRegenerationResultApplier::commit(WorldChunkStreamer &streamer,
	World &world, WorldGenerationPipeline::Result &result) noexcept
{
	if (!world.revision_manager.is_regenerating_for(result.relevance_epoch))
		return (FT_ERR_SUCCESS);
	world.revision_manager.record_regeneration_completed();
	if (result.error_code != FT_ERR_SUCCESS || result.chunk == nullptr)
	{
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		std::fprintf(stderr,
			"[WorldRevision] regeneration result failed request=%llu "
			"chunk=(%d,%d) error=%d has_chunk=%s relevance=%llu\n",
			static_cast<unsigned long long>(result.request_id), result.chunk_x,
			result.chunk_z, result.error_code,
			result.chunk == nullptr ? "false" : "true",
			static_cast<unsigned long long>(result.relevance_epoch));
	#endif
		world.revision_manager.record_regeneration_error(result.error_code);
	}
	else
		WorldRegenerationResultApplier::apply_chunk(streamer, world, result);
	if (world.revision_manager.all_regeneration_jobs_done())
		return (world.revision_manager.finish_regeneration());
	return (FT_ERR_SUCCESS);
}
