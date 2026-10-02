#include "../../src/world/WorldRevisionManager.hpp"

#include <limits>

WorldRevisionManager::WorldRevisionManager(World &world) : world_(world),
	pending_(false), revision_id_(1U), selection_revision_(1U), stage_mask_(0U),
	mode_(World::REGEN_FULL), config_(), selected_(), manual_protected_(),
	progress_()
{
}

WorldRevisionManager::WorldRevisionManager(const WorldRevisionManager &other) : world_(other.world_),
	pending_(false), revision_id_(1U), selection_revision_(1U), stage_mask_(0U),
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
		return (VOXEL_STAGE_DECORATION | VOXEL_STAGE_STRUCTURES);
	if (mode == World::REGEN_UNDERGROUND_REFRESH)
		return (VOXEL_STAGE_CAVES | VOXEL_STAGE_ORES);
	if (mode == World::REGEN_VOXEL_RESHAPING)
		return (VOXEL_STAGE_BASE_TERRAIN | VOXEL_STAGE_FLUIDS | VOXEL_STAGE_DECORATION | VOXEL_STAGE_STRUCTURES);
	return (VOXEL_STAGE_BASE_TERRAIN | VOXEL_STAGE_CAVES | VOXEL_STAGE_FLUIDS | VOXEL_STAGE_DECORATION | VOXEL_STAGE_STRUCTURES | VOXEL_STAGE_ORES);
}

int32_t WorldRevisionManager::begin(const voxel_generation_config &config,
	int32_t mode) noexcept
{
	if (!this->world_.voxel_generation_started || this->pending_
		|| this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	if (mode < World::REGEN_DECORATION_REFRESH || mode > World::REGEN_FULL)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->selection_revision_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	if (this->config_.initialize(config) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_ARGUMENT);
	if (voxel_generation_config_is_valid(this->config_) == FT_FALSE)
		return (FT_ERR_INVALID_ARGUMENT);
	this->pending_ = true;
	this->mode_ = mode;
	this->stage_mask_ = WorldRevisionManager::stage_mask_for_mode(mode);
	this->selected_.clear();
	this->selection_revision_ += 1U;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::cancel() noexcept
{
	if (!this->pending_)
		return (FT_ERR_INVALID_OPERATION);
	if (this->progress_.active())
	{
		this->world_.chunk_streamer.cancel_pending_remeshes();
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

uint64_t WorldRevisionManager::selection_revision() const noexcept
{
	return (this->selection_revision_);
}

std::size_t WorldRevisionManager::manually_protected_count() const noexcept
{
	return (this->manual_protected_.size());
}

int32_t WorldRevisionManager::select_chunk(int32_t chunk_x, int32_t chunk_z,
	bool selected) noexcept
{
	uint64_t current_selection_revision;

	return (this->select_chunk(this->selection_revision_,
			std::numeric_limits<uint32_t>::max(), chunk_x, chunk_z, selected,
			&current_selection_revision));
}

int32_t WorldRevisionManager::select_chunk(
	uint64_t expected_selection_revision, int32_t chunk_x, int32_t chunk_z,
	bool selected, uint64_t *current_selection_revision) noexcept
{
	return (this->select_chunk(expected_selection_revision,
			std::numeric_limits<uint32_t>::max(), chunk_x, chunk_z, selected,
			current_selection_revision));
}

int32_t WorldRevisionManager::select_chunk(
	uint64_t expected_selection_revision, uint32_t maximum_selected_chunks,
	int32_t chunk_x, int32_t chunk_z, bool selected,
	uint64_t *current_selection_revision) noexcept
{
	const bool already_selected = WorldRevisionChunkSet::contains(
		this->selected_, chunk_x, chunk_z);
	const WorldChunk *chunk;

	if (current_selection_revision == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*current_selection_revision = this->selection_revision_;
	if (expected_selection_revision != this->selection_revision_)
		return (FT_ERR_INVALID_STATE);
	if (!this->pending_)
		return (FT_ERR_INVALID_OPERATION);
	if (selected && maximum_selected_chunks == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (selected && this->is_chunk_protected(chunk_x, chunk_z))
		return (FT_ERR_INVALID_OPERATION);
	if (selected && !already_selected)
	{
		if (this->selected_.size() >= maximum_selected_chunks)
			return (FT_ERR_OUT_OF_RANGE);
		chunk = this->world_.find_chunk(chunk_x, chunk_z);
		if (chunk == nullptr || !chunk->initialized)
			return (FT_ERR_NOT_FOUND);
	}
	if (already_selected != selected)
	{
		if (this->selection_revision_
			== std::numeric_limits<uint64_t>::max())
			return (FT_ERR_OUT_OF_RANGE);
		WorldRevisionChunkSet::set_membership(this->selected_, chunk_x, chunk_z,
			selected);
		this->selection_revision_ += 1U;
	}
	*current_selection_revision = this->selection_revision_;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::unselect_chunk(int32_t chunk_x,
	int32_t chunk_z) noexcept
{
	return (this->select_chunk(chunk_x, chunk_z, false));
}

int32_t WorldRevisionManager::query_start_cost(uint32_t opening_card_count,
	const WorldRevisionCostPolicy &policy,
	WorldRevisionCostQuote *quote) const noexcept
{
	WorldRevisionCostQuote result;
	uint64_t selected_count;
	uint64_t opening_card_cost;
	uint64_t total_cost;
	uint32_t extra_card_index;

	if (quote == nullptr || policy.maximum_selected_chunks == 0U
		|| policy.currency_item_id == 0U
		|| opening_card_count < 4U || opening_card_count > 7U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->pending_ || this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	if (this->selected_.empty())
		return (FT_ERR_INVALID_OPERATION);
	if (this->selected_.size() > policy.maximum_selected_chunks
		|| this->selected_.size() > std::numeric_limits<uint32_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	selected_count = this->selected_.size();
	if (policy.currency_units_per_chunk != 0U
		&& selected_count > std::numeric_limits<uint64_t>::max()
			/ policy.currency_units_per_chunk)
		return (FT_ERR_OUT_OF_RANGE);
	result.chunk_currency_cost = selected_count
		* policy.currency_units_per_chunk;
	opening_card_cost = policy.four_card_opening_cost;
	extra_card_index = 4U;
	while (extra_card_index < opening_card_count)
	{
		if (opening_card_cost > std::numeric_limits<uint64_t>::max() / 2U)
			return (FT_ERR_OUT_OF_RANGE);
		opening_card_cost *= 2U;
		extra_card_index += 1U;
	}
	if (result.chunk_currency_cost > std::numeric_limits<uint64_t>::max()
		- opening_card_cost)
		return (FT_ERR_OUT_OF_RANGE);
	total_cost = result.chunk_currency_cost + opening_card_cost;
	if (total_cost > policy.maximum_total_currency_cost)
		return (FT_ERR_OUT_OF_RANGE);
	result.revision_identifier = this->revision_id_;
	result.selection_revision = this->selection_revision_;
	result.selected_paid_chunks = static_cast<uint32_t>(selected_count);
	result.opening_card_count = opening_card_count;
	result.currency_item_id = policy.currency_item_id;
	result.opening_card_currency_cost = opening_card_cost;
	result.total_currency_cost = total_cost;
	*quote = result;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::query_start_cost(
	uint64_t expected_selection_revision, uint32_t opening_card_count,
	const WorldRevisionCostPolicy &policy,
	WorldRevisionCostQuote *quote) const noexcept
{
	if (expected_selection_revision != this->selection_revision_)
		return (FT_ERR_INVALID_STATE);
	return (this->query_start_cost(opening_card_count, policy, quote));
}

int32_t WorldRevisionManager::set_chunk_protected(int32_t chunk_x,
	int32_t chunk_z, bool protected_state) noexcept
{
	const bool already_protected = WorldRevisionChunkSet::contains(
		this->manual_protected_, chunk_x, chunk_z);
	bool selection_changed;

	selection_changed = protected_state
		&& WorldRevisionChunkSet::contains(this->selected_, chunk_x, chunk_z);
	if (already_protected == protected_state && !selection_changed)
		return (FT_ERR_SUCCESS);
	if (this->selection_revision_
		== std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	WorldRevisionChunkSet::set_membership(this->manual_protected_, chunk_x,
		chunk_z, protected_state);
	if (selection_changed)
		WorldRevisionChunkSet::set_membership(this->selected_, chunk_x, chunk_z,
			false);
	this->selection_revision_ += 1U;
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

int32_t WorldRevisionManager::apply_request(const voxel_generation_config &config,
	int32_t mode, uint32_t stage_mask,
	const std::vector<RevisionChunk> &selected_chunks,
	const std::vector<RevisionChunk> &protected_chunks,
	int32_t *regenerated_count, int32_t *skipped_count) noexcept
{
	return (WorldRevisionRequestApplier::apply(*this, this->world_, config,
			mode, stage_mask, selected_chunks, protected_chunks,
			regenerated_count, skipped_count));
}
