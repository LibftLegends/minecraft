#include "../../src/world/WorldRevisionRequestApplier.hpp"

WorldRevisionRequestApplier::WorldRevisionRequestApplier()
{
}

WorldRevisionRequestApplier::WorldRevisionRequestApplier(const WorldRevisionRequestApplier &other)
{
	(void)other;
}

WorldRevisionRequestApplier::~WorldRevisionRequestApplier()
{
}

WorldRevisionRequestApplier &WorldRevisionRequestApplier::operator=(const WorldRevisionRequestApplier &other)
{
	(void)other;
	return (*this);
}

int32_t WorldRevisionRequestApplier::validate_request_chunks(WorldRevisionManager &manager,
	uint32_t stage_mask,
	const std::vector<WorldRevisionManager::RevisionChunk> &selected_chunks,
	const std::vector<WorldRevisionManager::RevisionChunk> &protected_chunks) noexcept
{
	const uint32_t valid_stage_mask = VOXEL_STAGE_BASE_TERRAIN | VOXEL_STAGE_CAVES | VOXEL_STAGE_FLUIDS | VOXEL_STAGE_DECORATION | VOXEL_STAGE_STRUCTURES | VOXEL_STAGE_ORES;

	if ((stage_mask & ~valid_stage_mask) != 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (manager.pending_)
		return (FT_ERR_INVALID_OPERATION);
	for (const WorldRevisionManager::RevisionChunk &selected : selected_chunks)
	{
		if (manager.is_chunk_protected(selected.chunk_x, selected.chunk_z))
			return (FT_ERR_INVALID_OPERATION);
		for (const WorldRevisionManager::RevisionChunk &protected_entry : protected_chunks)
		{
			if (std::abs(selected.chunk_x - protected_entry.chunk_x) <= 1
				&& std::abs(selected.chunk_z - protected_entry.chunk_z) <= 1)
				return (FT_ERR_INVALID_OPERATION);
		}
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionRequestApplier::apply_protection_and_selection(WorldRevisionManager &manager,
	const std::vector<WorldRevisionManager::RevisionChunk> &selected_chunks,
	const std::vector<WorldRevisionManager::RevisionChunk> &protected_chunks) noexcept
{
	int32_t error_code;

	for (const WorldRevisionManager::RevisionChunk &entry : protected_chunks)
	{
		error_code = manager.set_chunk_protected(entry.chunk_x, entry.chunk_z,
				true);
		if (error_code != FT_ERR_SUCCESS)
		{
			(void)manager.cancel();
			return (error_code);
		}
	}
	for (const WorldRevisionManager::RevisionChunk &entry : selected_chunks)
	{
		error_code = manager.select_chunk(entry.chunk_x, entry.chunk_z, true);
		if (error_code != FT_ERR_SUCCESS)
		{
			(void)manager.cancel();
			return (error_code);
		}
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionRequestApplier::apply(WorldRevisionManager &manager,
	World &world, const voxel_generation_config &config, int32_t mode,
	uint32_t stage_mask,
	const std::vector<WorldRevisionManager::RevisionChunk> &selected_chunks,
	const std::vector<WorldRevisionManager::RevisionChunk> &protected_chunks,
	int32_t *regenerated_count, int32_t *skipped_count) noexcept
{
	int32_t error_code;

	if (mode < World::REGEN_DECORATION_REFRESH || mode > World::REGEN_FULL)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = WorldRevisionRequestApplier::validate_request_chunks(manager,
			stage_mask, selected_chunks, protected_chunks);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = manager.begin(config, mode);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (stage_mask != 0U)
		manager.stage_mask_ = stage_mask;
	error_code = WorldRevisionRequestApplier::apply_protection_and_selection(manager,
			selected_chunks, protected_chunks);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (WorldRevisionRegenerator::regenerate_selected_chunks(manager, world,
			regenerated_count, skipped_count));
}
