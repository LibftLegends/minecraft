#include "../../src/world/World.hpp"
#include "../../src/cards/RegenerationWorldRules.hpp"
#include <limits>
#include <new>

int32_t World::begin_world_revision(const voxel_generation_config &config,
	RegenerationMode mode)
{
	return (this->revision_manager.begin(config, static_cast<int32_t>(mode)));
}

int32_t World::begin_world_revision(const voxel_generation_config &config,
	const RegenerationWorldRules &rules, RegenerationMode mode)
{
	return (this->revision_manager.begin(config, static_cast<int32_t>(mode),
			rules));
}

int32_t World::update_pending_revision_rules(
	const RegenerationWorldRules &rules)
{
	return (this->revision_manager.update_pending_rules(rules));
}

int32_t World::cancel_world_revision()
{
	return (this->revision_manager.cancel());
}

World::WorldRevision World::world_revision() const
{
	WorldRevision result;

	result.identifier = this->revision_manager.identifier();
	result.selection_revision = this->revision_manager.selection_revision();
	result.cost_policy_revision =
		this->revision_manager.cost_policy_revision();
	result.stage_mask = this->revision_manager.stage_mask();
	result.mode = static_cast<RegenerationMode>(this->revision_manager.mode());
	result.pending = this->revision_manager.pending();
	result.regenerating = this->revision_manager.regenerating();
	result.selected_count = this->revision_manager.selected_count();
	result.manually_protected_count = this->revision_manager.manually_protected_count();
	result.completed_jobs =
		this->revision_manager.completed_regeneration_job_count();
	result.total_jobs = this->revision_manager.regeneration_job_count();
	result.regenerated_chunks =
		this->revision_manager.regenerated_chunk_count();
	result.skipped_chunks = this->revision_manager.skipped_chunk_count();
	result.regeneration_error = this->revision_manager.regeneration_error();
	return (result);
}

int32_t World::copy_selected_revision_chunks(
	std::vector<RevisionChunkCoordinate> &chunks_out) const noexcept
{
	return (this->revision_manager.copy_selected_chunks(chunks_out));
}

int32_t World::revision_feature_reservation_upper_bound(
	uint32_t *count_out) const noexcept
{
	return (this->revision_manager.feature_reservation_upper_bound(count_out));
}

int32_t World::copy_selected_paid_revision_chunks(
	std::vector<RevisionChunkCoordinate> &chunks_out) const noexcept
{
	return (this->revision_manager.copy_selected_paid_chunks(chunks_out));
}

int32_t World::select_revision_chunk(int32_t chunk_x, int32_t chunk_z,
	bool selected)
{
	return (this->revision_manager.select_chunk(chunk_x, chunk_z, selected));
}

int32_t World::update_revision_chunk_selection(
	uint64_t expected_selection_revision, uint32_t maximum_selected_chunks,
	int32_t chunk_x, int32_t chunk_z, bool selected,
	uint64_t *current_selection_revision)
{
	return (this->revision_manager.select_chunk(expected_selection_revision,
			maximum_selected_chunks, chunk_x, chunk_z, selected,
			current_selection_revision));
}

int32_t World::update_revision_chunk_selection(
	uint64_t expected_selection_revision, int32_t chunk_x, int32_t chunk_z,
	bool selected, uint64_t *current_selection_revision)
{
	return (this->revision_manager.select_chunk(expected_selection_revision,
			chunk_x, chunk_z, selected, current_selection_revision));
}

int32_t World::select_revision_bonus_chunk(
	uint64_t expected_selection_revision, uint32_t maximum_selected_chunks,
	int32_t chunk_x, int32_t chunk_z,
	uint64_t *current_selection_revision)
{
	return (this->revision_manager.select_bonus_chunk(
		expected_selection_revision, maximum_selected_chunks, chunk_x, chunk_z,
		current_selection_revision));
}

int32_t World::validate_revision_bonus_chunk(
	uint64_t expected_selection_revision, uint32_t maximum_selected_chunks,
	int32_t chunk_x, int32_t chunk_z) const
{
	return (this->revision_manager.validate_bonus_chunk(
		expected_selection_revision, maximum_selected_chunks, chunk_x, chunk_z));
}

int32_t World::unselect_revision_chunk(int32_t chunk_x, int32_t chunk_z)
{
	return (this->revision_manager.unselect_chunk(chunk_x, chunk_z));
}

int32_t World::configure_revision_cost_policy(
	uint64_t expected_policy_revision, const WorldRevisionCostPolicy &policy,
	uint64_t *current_policy_revision)
{
	return (this->revision_manager.configure_cost_policy(
		expected_policy_revision, policy, current_policy_revision));
}

int32_t World::query_revision_start_cost(uint64_t expected_selection_revision,
	uint64_t expected_policy_revision, uint32_t opening_card_count,
	WorldRevisionCostQuote *quote) const
{
	return (this->revision_manager.query_start_cost(expected_selection_revision,
			expected_policy_revision, opening_card_count, quote));
}

int32_t World::query_revision_start_cost(uint32_t opening_card_count,
	const WorldRevisionCostPolicy &policy, WorldRevisionCostQuote *quote) const
{
	return (this->revision_manager.query_start_cost(opening_card_count, policy,
			quote));
}

int32_t World::query_revision_start_cost(uint64_t expected_selection_revision,
	uint32_t opening_card_count, const WorldRevisionCostPolicy &policy,
	WorldRevisionCostQuote *quote) const
{
	return (this->revision_manager.query_start_cost(expected_selection_revision,
			opening_card_count, policy, quote));
}

int32_t World::update_revision_chunk_protection(
	uint64_t expected_selection_revision, int32_t chunk_x, int32_t chunk_z,
	bool protected_state, uint64_t *current_selection_revision)
{
	return (this->revision_manager.set_chunk_protected(
		expected_selection_revision, chunk_x, chunk_z, protected_state,
		current_selection_revision));
}

int32_t World::set_revision_occupied_chunks(
	const std::vector<RevisionChunkCoordinate> &occupied_chunks,
	uint64_t *current_selection_revision)
{
	std::vector<WorldRevisionManager::OccupiedChunk> staged;

	if (current_selection_revision == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		staged.reserve(occupied_chunks.size());
		for (const RevisionChunkCoordinate &coordinate : occupied_chunks)
			staged.push_back({coordinate.chunk_x, coordinate.chunk_z});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (this->revision_manager.set_occupied_chunks(staged,
		current_selection_revision));
}

int32_t World::set_revision_barriers_active(bool active) noexcept
{
	return (this->revision_manager.set_barriers_active(active));
}

bool World::revision_barriers_active() const noexcept
{
	return (this->revision_manager.barriers_active());
}

int32_t World::set_revision_decision_deadline_epoch(
	uint64_t deadline_epoch) noexcept
{
	return (this->revision_manager.set_decision_deadline_epoch(
		deadline_epoch));
}

int32_t World::clear_revision_decision_deadline_epoch() noexcept
{
	return (this->revision_manager.clear_decision_deadline_epoch());
}

uint64_t World::revision_decision_deadline_epoch() const noexcept
{
	return (this->revision_manager.decision_deadline_epoch());
}

bool World::revision_barrier_blocks_chunk(int32_t chunk_x,
	int32_t chunk_z) const noexcept
{
	return (this->revision_manager.barrier_blocks_chunk(chunk_x, chunk_z));
}

int32_t World::configure_loom_station(uint64_t expected_selection_revision,
	int32_t chunk_x, int32_t chunk_z,
	uint64_t *current_selection_revision)
{
	return (this->revision_manager.configure_loom_station(
		expected_selection_revision, chunk_x, chunk_z,
		current_selection_revision));
}

int32_t World::configure_loom_station_block(
	uint64_t expected_selection_revision, int32_t block_x, int32_t block_y,
	int32_t block_z, uint32_t block_id,
	uint64_t *current_selection_revision)
{
	return (this->revision_manager.configure_loom_station_block(
		expected_selection_revision, block_x, block_y, block_z, block_id,
		current_selection_revision));
}

bool World::has_loom_station() const noexcept
{
	return (this->revision_manager.has_loom_station());
}

int32_t World::get_loom_station_chunk(int32_t *chunk_x_out,
	int32_t *chunk_z_out) const noexcept
{
	return (this->revision_manager.get_loom_station_chunk(chunk_x_out,
		chunk_z_out));
}

int32_t World::get_loom_station_block(bool *configured_out,
	int32_t *block_x_out, int32_t *block_y_out, int32_t *block_z_out,
	uint32_t *block_id_out) const noexcept
{
	return (this->revision_manager.get_loom_station_block(configured_out,
		block_x_out, block_y_out, block_z_out, block_id_out));
}

int32_t World::invalidate_loom_station() noexcept
{
	return (this->revision_manager.invalidate_loom_station());
}

int32_t World::invalidate_loom_station_if_block_changed(int32_t block_x,
	int32_t block_y, int32_t block_z, uint32_t new_block_id) noexcept
{
	return (this->revision_manager.invalidate_loom_station_if_block_changed(
		block_x, block_y, block_z, new_block_id));
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

int32_t World::get_revision_protection_info(int32_t chunk_x, int32_t chunk_z,
	RevisionProtectionInfo *info_out) const noexcept
{
	return (this->revision_manager.get_protection_info(chunk_x, chunk_z,
		info_out));
}

World::ChunkRevisionState World::revision_state(int32_t chunk_x,
	int32_t chunk_z) const
{
	return (static_cast<ChunkRevisionState>(this->revision_manager.chunk_state(chunk_x,
				chunk_z)));
}

bool World::revision_chunk_is_published(int32_t chunk_x,
	int32_t chunk_z) const noexcept
{
	return (this->revision_manager.has_published_chunk(chunk_x, chunk_z));
}

int32_t World::build_revision_preview(int32_t preview_center_x,
	int32_t preview_center_z, int32_t radius,
	std::vector<RevisionPreviewEntry> &preview) const
{
	std::vector<RevisionPreviewEntry> staged;
	RevisionPreviewEntry entry;

	if (radius < 0 || radius > WorldCoordinates::CACHE_CHUNK_RADIUS)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		staged.reserve(static_cast<size_t>((radius * 2 + 1)
			* (radius * 2 + 1)));
		for (int32_t z = -radius; z <= radius; ++z)
		{
			for (int32_t x = -radius; x <= radius; ++x)
			{
				if (x * x + z * z > radius * radius)
					continue ;
				const int64_t chunk_x = static_cast<int64_t>(preview_center_x) + x;
				const int64_t chunk_z = static_cast<int64_t>(preview_center_z) + z;
				if (chunk_x < std::numeric_limits<int32_t>::min()
					|| chunk_x > std::numeric_limits<int32_t>::max()
					|| chunk_z < std::numeric_limits<int32_t>::min()
					|| chunk_z > std::numeric_limits<int32_t>::max())
					continue ;
				entry.chunk_x = static_cast<int32_t>(chunk_x);
				entry.chunk_z = static_cast<int32_t>(chunk_z);
				entry.state = this->revision_state(entry.chunk_x, entry.chunk_z);
				if (this->get_revision_protection_info(entry.chunk_x,
						entry.chunk_z, &entry.protection_info) != FT_ERR_SUCCESS)
					return (FT_ERR_INVALID_STATE);
				staged.push_back(entry);
			}
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	preview.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t World::start_revision_regeneration()
{
	return (this->revision_manager.start_regeneration());
}

int32_t World::set_revision_generation_requested(bool requested) noexcept
{
	return (this->revision_manager.set_generation_requested(requested));
}

bool World::revision_generation_requested() const noexcept
{
	return (this->revision_manager.generation_requested());
}

bool World::revision_replaying_completed_generation() const noexcept
{
	return (this->revision_manager.replaying_completed_revision());
}

bool World::revision_completed_rules_pending() const noexcept
{
	return (this->revision_manager.completed_revision_rules_pending());
}

int32_t World::rehydrate_completed_revision_rules(
	const RegenerationWorldRules &rules) noexcept
{
	return (this->revision_manager.rehydrate_completed_revision_rules(rules));
}

int32_t World::retry_failed_revision_regeneration()
{
	return (this->revision_manager.retry_failed_regeneration());
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
		selected.push_back({entry.chunk_x, entry.chunk_z, false});
	for (const RevisionChunkCoordinate &entry : request.protected_chunks)
		protected_chunks.push_back({entry.chunk_x, entry.chunk_z, false});
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

int32_t World::serialize_revision_metadata(
	std::vector<uint8_t> &bytes_out) const
{
	return (this->revision_manager.serialize_metadata(bytes_out));
}

int32_t World::deserialize_revision_metadata(const uint8_t *bytes,
	std::size_t byte_count)
{
	return (this->revision_manager.deserialize_metadata(bytes, byte_count));
}

int32_t World::restore_committed_revision(
	const RegenerationWorldRules &rules)
{
	return (this->revision_manager.restore_committed_revision(rules));
}
