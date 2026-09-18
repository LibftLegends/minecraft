#include "../../src/world/World.hpp"

int32_t World::begin_world_revision(const voxel_generation_config &config,
	RegenerationMode mode)
{
	return (this->revision_manager.begin(config, static_cast<int32_t>(mode)));
}

int32_t World::cancel_world_revision()
{
	return (this->revision_manager.cancel());
}

World::WorldRevision World::world_revision() const
{
	WorldRevision result;

	result.identifier = this->revision_manager.identifier();
	result.stage_mask = this->revision_manager.stage_mask();
	result.mode = static_cast<RegenerationMode>(this->revision_manager.mode());
	result.pending = this->revision_manager.pending();
	result.regenerating = this->revision_manager.regenerating();
	result.selected_count = this->revision_manager.selected_count();
	result.manually_protected_count = this->revision_manager.manually_protected_count();
	return (result);
}

int32_t World::select_revision_chunk(int32_t chunk_x, int32_t chunk_z,
	bool selected)
{
	return (this->revision_manager.select_chunk(chunk_x, chunk_z, selected));
}

int32_t World::set_chunk_protected(int32_t chunk_x, int32_t chunk_z,
	bool protected_state)
{
	return (this->revision_manager.set_chunk_protected(chunk_x, chunk_z,
			protected_state));
}

bool World::is_chunk_protected(int32_t chunk_x, int32_t chunk_z) const
{
	return (this->revision_manager.is_chunk_protected(chunk_x, chunk_z));
}

World::ChunkRevisionState World::revision_state(int32_t chunk_x,
	int32_t chunk_z) const
{
	return (static_cast<ChunkRevisionState>(this->revision_manager.chunk_state(chunk_x,
				chunk_z)));
}

int32_t World::build_revision_preview(int32_t preview_center_x,
	int32_t preview_center_z, int32_t radius,
	std::vector<RevisionPreviewEntry> &preview) const
{
	RevisionPreviewEntry entry;

	if (radius < 0 || radius > WorldCoordinates::CACHE_CHUNK_RADIUS)
		return (FT_ERR_INVALID_ARGUMENT);
	preview.clear();
	preview.reserve(static_cast<size_t>((radius * 2 + 1) * (radius * 2 + 1)));
	for (int32_t z = -radius; z <= radius; ++z)
	{
		for (int32_t x = -radius; x <= radius; ++x)
		{
			if (x * x + z * z > radius * radius)
				continue ;
			entry.chunk_x = preview_center_x + x;
			entry.chunk_z = preview_center_z + z;
			entry.state = this->revision_state(entry.chunk_x, entry.chunk_z);
			preview.push_back(entry);
		}
	}
	return (FT_ERR_SUCCESS);
}

int32_t World::start_revision_regeneration()
{
	return (this->revision_manager.start_regeneration());
}

int32_t World::regenerate_selected_chunks(int32_t *regenerated_count,
	int32_t *skipped_count)
{
	return (this->revision_manager.regenerate_selected_chunks(regenerated_count,
			skipped_count));
}

int32_t World::apply_revision_request(const RevisionRequest &request,
	RevisionRequestResult *result)
{
	int32_t	error_code;

	std::vector<WorldRevisionManager::RevisionChunk> selected;
	std::vector<WorldRevisionManager::RevisionChunk> protected_chunks;
	if (result == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	for (const RevisionChunkCoordinate &entry : request.selected_chunks)
		selected.push_back({entry.chunk_x, entry.chunk_z});
	for (const RevisionChunkCoordinate &entry : request.protected_chunks)
		protected_chunks.push_back({entry.chunk_x, entry.chunk_z});
	error_code = this->revision_manager.apply_request(request.config,
			static_cast<int32_t>(request.mode), request.stage_mask, selected,
			protected_chunks, &result->regenerated_count,
			&result->skipped_count);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	result->revision_identifier = this->revision_manager.identifier();
	result->stage_mask = this->revision_manager.stage_mask();
	return (FT_ERR_SUCCESS);
}

int32_t World::save_revision_metadata(const char *file_path) const
{
	return (this->revision_manager.save_metadata(file_path));
}

int32_t World::load_revision_metadata(const char *file_path)
{
	return (this->revision_manager.load_metadata(file_path));
}
