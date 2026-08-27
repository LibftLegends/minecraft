#include "../../src/world/WorldRevisionManager.hpp"

WorldRevisionManager::WorldRevisionManager(World &world) : world_(world),
	pending_(false), revision_id_(1U), stage_mask_(0U),
	mode_(World::REGEN_FULL), config_(), selected_(), manual_protected_(),
	progress_()
{
}

WorldRevisionManager::WorldRevisionManager(const WorldRevisionManager &other) : world_(other.world_),
	pending_(false), revision_id_(1U), stage_mask_(0U),
	mode_(World::REGEN_FULL), config_(), selected_(), manual_protected_(),
	progress_()
{
	(void)other;
}

WorldRevisionManager::~WorldRevisionManager()
{
}

WorldRevisionManager &WorldRevisionManager::operator=(const WorldRevisionManager &other)
{
	(void)other;
	return (*this);
}

void WorldRevisionManager::reset() noexcept
{
	this->pending_ = false;
	this->progress_.reset();
	this->selected_.clear();
	this->manual_protected_.clear();
}

uint32_t WorldRevisionManager::stage_mask_for_mode(int32_t mode) noexcept
{
	if (mode == World::REGEN_DECORATION_REFRESH)
		return (TERRAIN_STAGE_DECORATION | TERRAIN_STAGE_STRUCTURES);
	if (mode == World::REGEN_UNDERGROUND_REFRESH)
		return (TERRAIN_STAGE_CAVES | TERRAIN_STAGE_ORES);
	if (mode == World::REGEN_TERRAIN_RESHAPING)
		return (TERRAIN_STAGE_BASE_TERRAIN | TERRAIN_STAGE_FLUIDS | TERRAIN_STAGE_DECORATION | TERRAIN_STAGE_STRUCTURES);
	return (TERRAIN_STAGE_BASE_TERRAIN | TERRAIN_STAGE_CAVES | TERRAIN_STAGE_FLUIDS | TERRAIN_STAGE_DECORATION | TERRAIN_STAGE_STRUCTURES | TERRAIN_STAGE_ORES);
}

int32_t WorldRevisionManager::begin(const terrain_generation_config &config,
	int32_t mode) noexcept
{
	if (!this->world_.terrain_generation_started || this->pending_
		|| this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	if (mode < World::REGEN_DECORATION_REFRESH || mode > World::REGEN_FULL)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->config_.initialize(config) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_ARGUMENT);
	if (terrain_generation_config_is_valid(this->config_) == FT_FALSE)
		return (FT_ERR_INVALID_ARGUMENT);
	this->pending_ = true;
	this->mode_ = mode;
	this->stage_mask_ = WorldRevisionManager::stage_mask_for_mode(mode);
	this->selected_.clear();
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::cancel() noexcept
{
	if (!this->pending_)
		return (FT_ERR_INVALID_OPERATION);
	if (this->progress_.active())
	{
		this->world_.chunk_streamer.pipeline().cancel_queued();
		this->progress_.cancel(this->progress_.generation_epoch() + 1U);
	}
	this->pending_ = false;
	this->selected_.clear();
	return (FT_ERR_SUCCESS);
}

uint32_t WorldRevisionManager::identifier() const noexcept
{
	return (this->revision_id_);
}

uint32_t WorldRevisionManager::stage_mask() const noexcept
{
	return (this->stage_mask_);
}

int32_t WorldRevisionManager::mode() const noexcept
{
	return (this->mode_);
}

bool WorldRevisionManager::pending() const noexcept
{
	return (this->pending_);
}

bool WorldRevisionManager::regenerating() const noexcept
{
	return (this->progress_.active());
}

std::size_t WorldRevisionManager::selected_count() const noexcept
{
	return (this->selected_.size());
}

std::size_t WorldRevisionManager::manually_protected_count() const noexcept
{
	return (this->manual_protected_.size());
}

int32_t WorldRevisionManager::select_chunk(int32_t chunk_x, int32_t chunk_z,
	bool selected) noexcept
{
	if (!this->pending_)
		return (FT_ERR_INVALID_OPERATION);
	if (selected && this->is_chunk_protected(chunk_x, chunk_z))
		return (FT_ERR_INVALID_OPERATION);
	WorldRevisionChunkSet::set_membership(this->selected_, chunk_x, chunk_z,
		selected);
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::set_chunk_protected(int32_t chunk_x,
	int32_t chunk_z, bool protected_state) noexcept
{
	WorldRevisionChunkSet::set_membership(this->manual_protected_, chunk_x,
		chunk_z, protected_state);
	if (protected_state)
		WorldRevisionChunkSet::set_membership(this->selected_, chunk_x, chunk_z,
			false);
	return (FT_ERR_SUCCESS);
}

bool WorldRevisionManager::is_chunk_protected(int32_t chunk_x,
	int32_t chunk_z) const noexcept
{
	const WorldChunk *chunk;

	chunk = this->world_.find_chunk(chunk_x, chunk_z);
	if (chunk != nullptr && chunk->chunk.is_generation_protected() == FT_TRUE)
		return (true);
	for (const RevisionChunk &entry : this->manual_protected_)
	{
		if (std::abs(entry.chunk_x - chunk_x) <= 1 && std::abs(entry.chunk_z
				- chunk_z) <= 1)
			return (true);
	}
	return (false);
}

int32_t WorldRevisionManager::chunk_state(int32_t chunk_x,
	int32_t chunk_z) const noexcept
{
	if (this->is_chunk_protected(chunk_x, chunk_z))
		return (World::REVISION_PROTECTED);
	if (WorldRevisionChunkSet::contains(this->selected_, chunk_x, chunk_z))
		return (World::REVISION_SELECTED);
	if (this->pending_)
	{
		for (const RevisionChunk &entry : this->selected_)
		{
			if (std::abs(entry.chunk_x - chunk_x) <= 1 && std::abs(entry.chunk_z
					- chunk_z) <= 1)
				return (World::REVISION_TRANSITION);
		}
	}
	return (World::REVISION_UNCHANGED);
}

int32_t WorldRevisionManager::save_metadata(const char *file_path) const noexcept
{
	return (WorldRevisionMetadataStore::save(file_path, this->revision_id_,
			this->manual_protected_));
}

int32_t WorldRevisionManager::load_metadata(const char *file_path) noexcept
{
	return (WorldRevisionMetadataStore::load(file_path, this->revision_id_,
			this->manual_protected_));
}

bool WorldRevisionManager::is_regenerating_for(uint64_t relevance_epoch) const noexcept
{
	return (this->progress_.is_active_for(relevance_epoch));
}

void WorldRevisionManager::record_regeneration_completed() noexcept
{
	this->progress_.record_completed();
}

void WorldRevisionManager::record_regeneration_error(int32_t error_code) noexcept
{
	this->progress_.record_error(error_code);
}

void WorldRevisionManager::record_regeneration_skipped() noexcept
{
	this->progress_.record_skipped();
}

void WorldRevisionManager::record_regeneration_success() noexcept
{
	this->progress_.record_success();
}

bool WorldRevisionManager::all_regeneration_jobs_done() const noexcept
{
	return (this->progress_.all_jobs_done());
}

int32_t WorldRevisionManager::finish_regeneration() noexcept
{
	return (WorldRevisionRegenerator::finish(*this, this->world_));
}

int32_t WorldRevisionManager::start_regeneration() noexcept
{
	return (WorldRevisionRegenerator::start(*this, this->world_));
}

int32_t WorldRevisionManager::regenerate_selected_chunks(int32_t *regenerated_count,
	int32_t *skipped_count) noexcept
{
	return (WorldRevisionRegenerator::regenerate_selected_chunks(*this,
			this->world_, regenerated_count, skipped_count));
}

int32_t WorldRevisionManager::apply_request(const terrain_generation_config &config,
	int32_t mode, uint32_t stage_mask,
	const std::vector<RevisionChunk> &selected_chunks,
	const std::vector<RevisionChunk> &protected_chunks,
	int32_t *regenerated_count, int32_t *skipped_count) noexcept
{
	return (WorldRevisionRequestApplier::apply(*this, this->world_, config,
			mode, stage_mask, selected_chunks, protected_chunks,
			regenerated_count, skipped_count));
}
