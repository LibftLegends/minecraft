#include "../../src/world/WorldRevisionRegenerator.hpp"

WorldRevisionRegenerator::WorldRevisionRegenerator()
{
}

WorldRevisionRegenerator::WorldRevisionRegenerator(const WorldRevisionRegenerator &other)
{
	(void)other;
}

WorldRevisionRegenerator::~WorldRevisionRegenerator()
{
}

WorldRevisionRegenerator &WorldRevisionRegenerator::operator=(const WorldRevisionRegenerator &other)
{
	(void)other;
	return (*this);
}

int32_t WorldRevisionRegenerator::capture_chunk_snapshot(WorldRevisionManager &manager,
	World &world, WorldChunk &chunk,
	WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
	const WorldGenerationPipeline::WorldChunkSnapshot **source_snapshot) noexcept
{
	int32_t error_code;

	*source_snapshot = nullptr;
	if (manager.mode_ == World::REGEN_FULL)
		return (FT_ERR_SUCCESS);
	error_code = world.chunk_streamer.pipeline().capture_snapshot(chunk,
			nullptr, nullptr, nullptr, nullptr, snapshot);
	if (error_code != FT_ERR_SUCCESS)
	{
		manager.progress_.record_error(error_code);
		manager.progress_.finish();
		return (error_code);
	}
	*source_snapshot = &snapshot;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionRegenerator::submit_chunk_regeneration(WorldRevisionManager &manager,
	World &world, const WorldRevisionManager::RevisionChunk &entry) noexcept
{
	WorldChunk *chunk;
	WorldGenerationPipeline::WorldChunkSnapshot snapshot;
	const WorldGenerationPipeline::WorldChunkSnapshot *source_snapshot;
	uint64_t request_id;
	int32_t error_code;

	chunk = world.find_chunk_mutable(entry.chunk_x, entry.chunk_z);
	if (chunk == nullptr || manager.chunk_state(entry.chunk_x,
			entry.chunk_z) != World::REVISION_SELECTED)
	{
		manager.progress_.record_skipped();
		return (FT_ERR_SUCCESS);
	}
	error_code = WorldRevisionRegenerator::capture_chunk_snapshot(manager,
			world, *chunk, snapshot, &source_snapshot);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	request_id = world.chunk_streamer.allocate_request_id();
	error_code = world.chunk_streamer.pipeline().submit_generation(request_id,
			world.chunk_streamer.world_epoch(),
			manager.progress_.generation_epoch(), manager.revision_id_,
			entry.chunk_x, entry.chunk_z, world.seed, manager.config_,
			manager.stage_mask_,
			WorldGenerationPipeline::WorldGenerationOperation::REGENERATE,
			source_snapshot);
	if (error_code != FT_ERR_SUCCESS)
	{
		manager.progress_.record_error(error_code);
		manager.progress_.finish();
		world.chunk_streamer.pipeline().cancel_queued();
		return (error_code);
	}
	manager.progress_.set_job_count(manager.progress_.job_count() + 1U);
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionRegenerator::start(WorldRevisionManager &manager,
	World &world) noexcept
{
	int32_t error_code;

	if (!manager.pending_ || manager.progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	manager.progress_.begin(world.chunk_streamer.allocate_request_id());
	for (const WorldRevisionManager::RevisionChunk &entry : manager.selected_)
	{
		error_code = WorldRevisionRegenerator::submit_chunk_regeneration(manager,
				world, entry);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
	}
	if (manager.progress_.job_count() == 0U)
		return (WorldRevisionRegenerator::finish(manager, world));
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionRegenerator::commit_terrain_config(WorldRevisionManager &manager,
	World &world) noexcept
{
	int32_t error_code;

	error_code = world.terrain_config.initialize(manager.config_);
	if (error_code == FT_ERR_SUCCESS)
	{
		(void)world.terrain_context.destroy();
		error_code = terrain_generation_context_initialize(world.terrain_context,
				world.terrain_config);
	}
	return (error_code);
}

int32_t WorldRevisionRegenerator::finish(WorldRevisionManager &manager,
	World &world) noexcept
{
	int32_t generation_error;
	int32_t error_code;

	if (!manager.progress_.active())
		return (manager.progress_.error());
	generation_error = manager.progress_.error();
	manager.progress_.finish();
	if (generation_error != FT_ERR_SUCCESS)
	{
		manager.pending_ = false;
		manager.selected_.clear();
		return (generation_error);
	}
	error_code = WorldRevisionRegenerator::commit_terrain_config(manager,
			world);
	if (error_code != FT_ERR_SUCCESS)
	{
		manager.pending_ = false;
		manager.selected_.clear();
		return (error_code);
	}
	manager.revision_id_ += 1U;
	manager.pending_ = false;
	manager.selected_.clear();
	world.chunk_streamer.pipeline().cancel_queued();
	world.chunk_streamer.bump_generation_revision();
	world.chunk_streamer.reset_candidates_after_regeneration();
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionRegenerator::regenerate_selected_chunks(WorldRevisionManager &manager,
	World &world, int32_t *regenerated_count, int32_t *skipped_count) noexcept
{
	int32_t error_code;

	if (regenerated_count == nullptr || skipped_count == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = WorldRevisionRegenerator::start(manager, world);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	while (manager.progress_.active())
	{
		error_code = WorldGenerationResultCommitter::drain(world.chunk_streamer,
				world);
		if (error_code != FT_ERR_SUCCESS)
			break ;
		if (manager.progress_.active())
			std::this_thread::yield();
	}
	*regenerated_count = manager.progress_.regenerated_count();
	*skipped_count = manager.progress_.skipped_count();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (manager.progress_.error());
}
