#include "../../src/world/WorldRevisionManager.hpp"
#include "../../src/cards/RegenerationWorldRules.hpp"
#include "../../Libft/Modules/File/file_utils.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <new>

static uint64_t world_revision_hash_bytes(const char *domain,
	const uint8_t *bytes, std::size_t byte_count) noexcept
{
	uint64_t hash = 14695981039346656037ULL;
	std::size_t index = 0U;
	while (domain[index] != '\0')
	{
		hash ^= static_cast<uint8_t>(domain[index]);
		hash *= 1099511628211ULL;
		++index;
	}
	index = 0U;
	while (index < byte_count)
	{
		hash ^= bytes[index];
		hash *= 1099511628211ULL;
		++index;
	}
	return (hash == 0U ? 1U : hash);
}

static void world_revision_lifecycle_append_u32(
	std::vector<uint8_t> &bytes, uint32_t value)
{
	for (uint32_t shift = 0U; shift < 32U; shift += 8U)
		bytes.push_back(static_cast<uint8_t>(value >> shift));
}

static void world_revision_lifecycle_append_u64(
	std::vector<uint8_t> &bytes, uint64_t value)
{
	for (uint32_t shift = 0U; shift < 64U; shift += 8U)
		bytes.push_back(static_cast<uint8_t>(value >> shift));
}

static bool world_revision_lifecycle_read_u32(const uint8_t *bytes,
	std::size_t byte_count, std::size_t *offset, uint32_t *value) noexcept
{
	if (bytes == nullptr || offset == nullptr || value == nullptr
		|| *offset > byte_count || byte_count - *offset < 4U)
		return (false);
	*value = static_cast<uint32_t>(bytes[*offset])
		| (static_cast<uint32_t>(bytes[*offset + 1U]) << 8U)
		| (static_cast<uint32_t>(bytes[*offset + 2U]) << 16U)
		| (static_cast<uint32_t>(bytes[*offset + 3U]) << 24U);
	*offset += 4U;
	return (true);
}

static bool world_revision_lifecycle_read_u64(const uint8_t *bytes,
	std::size_t byte_count, std::size_t *offset, uint64_t *value) noexcept
{
	uint32_t low;
	uint32_t high;
	if (!world_revision_lifecycle_read_u32(bytes, byte_count, offset, &low)
		|| !world_revision_lifecycle_read_u32(bytes, byte_count, offset, &high))
		return (false);
	*value = static_cast<uint64_t>(low)
		| (static_cast<uint64_t>(high) << 32U);
	return (true);
}

static uint64_t world_revision_lifecycle_hash(const uint8_t *bytes,
	std::size_t byte_count) noexcept
{
	uint64_t hash = 14695981039346656037ULL;
	for (std::size_t index = 0U; index < byte_count; ++index)
	{
		hash ^= bytes[index];
		hash *= 1099511628211ULL;
	}
	return (hash);
}

static int32_t world_revision_rules_hash(
	const RegenerationWorldRules &rules, uint64_t *hash_out) noexcept
{
	std::vector<uint8_t> bytes;
	int32_t error_code;
	if (hash_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = rules.serialize(bytes);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	*hash_out = world_revision_hash_bytes("MinecraftWorldRulesV1",
		bytes.data(), bytes.size());
	return (FT_ERR_SUCCESS);
}

static bool world_revision_contains_chunk(
	const std::vector<WorldRevisionManager::RevisionChunk> &chunks,
	int32_t chunk_x, int32_t chunk_z) noexcept
{
	for (const WorldRevisionManager::RevisionChunk &entry : chunks)
	{
		if (entry.chunk_x == chunk_x && entry.chunk_z == chunk_z)
			return (true);
	}
	return (false);
}

WorldRevisionManager::WorldRevisionManager(World &world) : world_(world),
	pending_(false), revision_id_(1U), selection_revision_(1U),
	rules_hash_(0U),
	cost_policy_revision_(0U), cost_policy_(), cost_policy_configured_(false),
	barriers_active_(false),
	stage_mask_(0U),
	mode_(World::REGEN_FULL), config_(), biome_base_config_(),
	base_biome_selector_(nullptr), base_biome_selector_user_data_(nullptr),
	biome_rule_count_(0U), base_biome_selector_captured_(false), selected_(),
	restored_selected_(), restored_selection_revision_(0U),
	restored_deadline_epoch_(0U), decision_deadline_epoch_(0U),
	restored_mode_(0),
	restored_stage_mask_(0U), restored_barrier_state_(false),
	lifecycle_restore_pending_(false), rules_target_chunks_(), feature_plans_(),
	manual_protected_(), loom_station_configured_(false),
	loom_station_chunk_x_(0), loom_station_chunk_z_(0),
	loom_station_block_configured_(false), loom_station_block_x_(0),
	loom_station_block_y_(0), loom_station_block_z_(0),
	loom_station_block_id_(0U), occupied_chunks_(),
	progress_()
{
	uint32_t index = 0U;
	while (index < VOXEL_MAX_CUSTOM_BIOMES)
	{
		this->biome_enabled_[index] = true;
		this->biome_weight_adjustments_[index] = 0;
		++index;
	}
}

WorldRevisionManager::WorldRevisionManager(const WorldRevisionManager &other) : world_(other.world_),
	pending_(false), revision_id_(1U), selection_revision_(1U),
	rules_hash_(0U),
	cost_policy_revision_(other.cost_policy_revision_),
	cost_policy_(other.cost_policy_),
	cost_policy_configured_(other.cost_policy_configured_),
	barriers_active_(false), stage_mask_(0U),
	mode_(World::REGEN_FULL), config_(), biome_base_config_(),
	base_biome_selector_(nullptr), base_biome_selector_user_data_(nullptr),
	biome_rule_count_(0U), base_biome_selector_captured_(false), selected_(),
	restored_selected_(), restored_selection_revision_(0U),
	restored_deadline_epoch_(0U), decision_deadline_epoch_(0U),
	restored_mode_(0),
	restored_stage_mask_(0U), restored_barrier_state_(false),
	lifecycle_restore_pending_(false), rules_target_chunks_(), feature_plans_(),
	manual_protected_(), loom_station_configured_(false),
	loom_station_chunk_x_(0), loom_station_chunk_z_(0),
	loom_station_block_configured_(false), loom_station_block_x_(0),
	loom_station_block_y_(0), loom_station_block_z_(0),
	loom_station_block_id_(0U), occupied_chunks_(),
	progress_()
{
	(void)other;
	uint32_t index = 0U;
	while (index < VOXEL_MAX_CUSTOM_BIOMES)
	{
		this->biome_enabled_[index] = true;
		this->biome_weight_adjustments_[index] = 0;
		++index;
	}
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
	this->barriers_active_ = false;
	this->rules_hash_ = 0U;
	this->progress_.reset();
	this->selected_.clear();
	this->restored_selected_.clear();
	this->restored_selection_revision_ = 0U;
	this->restored_deadline_epoch_ = 0U;
	this->decision_deadline_epoch_ = 0U;
	this->restored_mode_ = 0;
	this->restored_stage_mask_ = 0U;
	this->restored_barrier_state_ = false;
	this->lifecycle_restore_pending_ = false;
	this->rules_target_chunks_.clear();
	this->feature_plans_.clear();
	this->manual_protected_.clear();
	this->loom_station_configured_ = false;
	this->loom_station_chunk_x_ = 0;
	this->loom_station_chunk_z_ = 0;
	this->loom_station_block_configured_ = false;
	this->loom_station_block_x_ = 0;
	this->loom_station_block_y_ = 0;
	this->loom_station_block_z_ = 0;
	this->loom_station_block_id_ = 0U;
	this->occupied_chunks_.clear();
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
		|| this->barriers_active_
		|| this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	if (mode < World::REGEN_DECORATION_REFRESH || mode > World::REGEN_FULL)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->revision_id_ == std::numeric_limits<uint32_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	if (this->selection_revision_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	if (this->config_.initialize(config) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_ARGUMENT);
	if (voxel_generation_config_is_valid(this->config_) == FT_FALSE)
		return (FT_ERR_INVALID_ARGUMENT);
	uint32_t config_signature = voxel_generation_config_signature(
		this->config_);
	const uint8_t config_signature_bytes[] = {
		static_cast<uint8_t>(config_signature & 0xffU),
		static_cast<uint8_t>((config_signature >> 8U) & 0xffU),
		static_cast<uint8_t>((config_signature >> 16U) & 0xffU),
		static_cast<uint8_t>((config_signature >> 24U) & 0xffU)};
	this->rules_hash_ = world_revision_hash_bytes(
		"MinecraftWorldConfigV1",
		config_signature_bytes, sizeof(config_signature_bytes));
	this->pending_ = true;
	this->mode_ = mode;
	this->stage_mask_ = WorldRevisionManager::stage_mask_for_mode(mode);
	this->selected_.clear();
	this->rules_target_chunks_.clear();
	this->feature_plans_.clear();
	this->selection_revision_ += 1U;
	return (FT_ERR_SUCCESS);
}

uint32_t WorldRevisionManager::select_biome_with_rules(uint64_t seed_value,
	int32_t world_block_x, int32_t world_block_z, uint32_t biome_count,
	void *user_data) noexcept
{
	WorldRevisionManager *manager =
		static_cast<WorldRevisionManager *>(user_data);
	uint32_t base_biome;
	uint32_t index;
	uint64_t hash;
	uint64_t total_weight = 0U;
	uint64_t selected_weight;
	int32_t zone_width;
	int32_t zone_x;
	int32_t zone_z;
	bool has_compass_adjustment = false;

	if (manager == nullptr || biome_count == 0U)
		return (0U);
	base_biome = voxel_select_biome(manager->biome_base_config_, seed_value,
		world_block_x, world_block_z);
	index = 0U;
	while (index < manager->biome_rule_count_)
	{
		if (manager->biome_enabled_[index])
		{
			if (manager->biome_weight_adjustments_[index] != 0)
				has_compass_adjustment = true;
			const int64_t adjusted = 1LL
				+ manager->biome_weight_adjustments_[index];
			total_weight += static_cast<uint64_t>(adjusted > 0 ? adjusted : 1LL);
		}
		++index;
	}
	if (!has_compass_adjustment && base_biome < manager->biome_rule_count_
		&& manager->biome_enabled_[base_biome])
		return (base_biome);
	if (total_weight == 0U)
		return (base_biome % biome_count);
	zone_width = voxel_get_biome_zone_width(manager->biome_base_config_,
		seed_value);
	if (zone_width <= 0)
		zone_width = 1;
	zone_x = world_block_x / zone_width;
	zone_z = world_block_z / zone_width;
	if (world_block_x < 0 && world_block_x % zone_width != 0)
		--zone_x;
	if (world_block_z < 0 && world_block_z % zone_width != 0)
		--zone_z;
	hash = seed_value ^ (static_cast<uint64_t>(
		static_cast<uint32_t>(zone_x)) * 0x9E3779B185EBCA87ULL);
	hash ^= static_cast<uint64_t>(static_cast<uint32_t>(zone_z))
		* 0xC2B2AE3D27D4EB4FULL;
	hash ^= hash >> 30;
	hash *= 0xBF58476D1CE4E5B9ULL;
	hash ^= hash >> 27;
	hash *= 0x94D049BB133111EBULL;
	hash ^= hash >> 31;
	selected_weight = hash % total_weight;
	index = 0U;
	while (index < manager->biome_rule_count_)
	{
		if (manager->biome_enabled_[index])
		{
			const int64_t adjusted = 1LL
				+ manager->biome_weight_adjustments_[index];
			const uint64_t weight = static_cast<uint64_t>(
				adjusted > 0 ? adjusted : 1LL);

			if (selected_weight < weight)
				return (index % biome_count);
			selected_weight -= weight;
		}
		++index;
	}
	return (base_biome % biome_count);
}

int32_t WorldRevisionManager::begin(const voxel_generation_config &config,
	int32_t mode, const RegenerationWorldRules &rules) noexcept
{
	uint32_t index;
	bool enabled;
	int32_t weight_adjustment;
	int32_t error_code;
	uint64_t staged_rules_hash;
	uint64_t previous_selection_revision;
	uint64_t previous_rules_hash;

	if (!this->world_.voxel_generation_started || this->pending_
		|| this->barriers_active_
		|| this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	if (mode < World::REGEN_DECORATION_REFRESH || mode > World::REGEN_FULL)
		return (FT_ERR_INVALID_ARGUMENT);
	if (mode != World::REGEN_FULL)
		return (FT_ERR_INVALID_OPERATION);
	if (this->revision_id_ == std::numeric_limits<uint32_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	if (this->selection_revision_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	previous_selection_revision = this->selection_revision_;
	previous_rules_hash = this->rules_hash_;
	if (config.biome_selector
		!= &WorldRevisionManager::select_biome_with_rules)
	{
		this->base_biome_selector_ = config.biome_selector;
		this->base_biome_selector_user_data_ =
			config.biome_selector_user_data;
		this->base_biome_selector_captured_ = true;
	}
	if (!this->base_biome_selector_captured_)
		return (FT_ERR_INVALID_STATE);
	if (this->biome_base_config_.initialize(config) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = this->biome_base_config_.set_biome_selector(
		this->base_biome_selector_, this->base_biome_selector_user_data_);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (this->config_.initialize(config) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = rules.apply_ore_scales(this->config_);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->biome_rule_count_ = config.biome_count;
	index = 0U;
	while (index < this->biome_rule_count_)
	{
		error_code = rules.get_biome_state(index + 1U, &enabled,
			&weight_adjustment);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		this->biome_enabled_[index] = enabled;
		this->biome_weight_adjustments_[index] = weight_adjustment;
		++index;
	}
	error_code = this->config_.set_biome_selector(
		&WorldRevisionManager::select_biome_with_rules, this);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (voxel_generation_config_is_valid(this->config_) == FT_FALSE)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = world_revision_rules_hash(rules, &staged_rules_hash);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->rules_hash_ = staged_rules_hash;
	this->pending_ = true;
	this->mode_ = mode;
	this->stage_mask_ = WorldRevisionManager::stage_mask_for_mode(mode);
	this->selected_.clear();
	this->rules_target_chunks_.clear();
	error_code = this->update_pending_rules(rules);
	if (error_code != FT_ERR_SUCCESS)
	{
		this->pending_ = false;
		this->selected_.clear();
		this->rules_target_chunks_.clear();
		this->feature_plans_.clear();
		this->selection_revision_ = previous_selection_revision;
		this->rules_hash_ = previous_rules_hash;
		return (error_code);
	}
	this->selection_revision_ = previous_selection_revision + 1U;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::update_pending_rules(
	const RegenerationWorldRules &rules) noexcept
{
	voxel_generation_config staged_config;
	bool staged_enabled[VOXEL_MAX_CUSTOM_BIOMES];
	int32_t staged_weights[VOXEL_MAX_CUSTOM_BIOMES];
	uint32_t index;
	bool enabled;
	int32_t weight_adjustment;
	int32_t error_code;
	uint64_t staged_rules_hash;
	std::vector<RegenerationTargetChunk> target_chunks;
	std::vector<RegenerationWorldFeaturePlan> rules_feature_plans;
	std::vector<WorldGenerationPipeline::GenerationFeaturePlan> staged_feature_plans;
	std::vector<RevisionChunk> staged_selected;
	std::vector<RevisionChunk> staged_rule_targets;
	uint32_t maximum_selected_chunks;
	bool selection_changed = false;
	bool target_ownership_changed = false;

	if (!this->pending_ || this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	if (!this->biome_base_config_.is_initialised())
		return (FT_ERR_INVALID_STATE);
	error_code = staged_config.initialize(this->biome_base_config_);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = rules.apply_ore_scales(staged_config);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	index = 0U;
	while (index < this->biome_rule_count_)
	{
		error_code = rules.get_biome_state(index + 1U, &enabled,
			&weight_adjustment);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		staged_enabled[index] = enabled;
		staged_weights[index] = weight_adjustment;
		++index;
	}
	error_code = staged_config.set_biome_selector(
		&WorldRevisionManager::select_biome_with_rules, this);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (voxel_generation_config_is_valid(staged_config) == FT_FALSE)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = world_revision_rules_hash(rules, &staged_rules_hash);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = rules.copy_added_target_chunks(target_chunks);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = rules.copy_feature_plans(rules_feature_plans);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	try
	{
		staged_feature_plans.reserve(rules_feature_plans.size());
		for (const RegenerationWorldFeaturePlan &plan : rules_feature_plans)
			staged_feature_plans.push_back({
				static_cast<uint32_t>(plan.kind),
				plan.host_intent_sequence, plan.ordinal, 0, 0,
				plan.footprint_chunks, {}, {}});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	maximum_selected_chunks = this->cost_policy_configured_
		? this->cost_policy_.maximum_selected_chunks
		: WorldRevisionManager::HARD_MAX_SELECTED_CHUNKS;
	try
	{
		staged_selected = this->selected_;
		staged_rule_targets = this->rules_target_chunks_;
		staged_selected.reserve(staged_selected.size() + target_chunks.size());
		staged_rule_targets.reserve(staged_rule_targets.size()
			+ target_chunks.size());
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (std::vector<RevisionChunk>::iterator target_it =
		staged_rule_targets.begin(); target_it != staged_rule_targets.end();)
	{
		bool found = false;
		for (const RegenerationTargetChunk &target : target_chunks)
		{
			if (target.chunk_x == target_it->chunk_x
				&& target.chunk_z == target_it->chunk_z)
			{
				found = true;
				break ;
			}
		}
		if (found)
		{
			++target_it;
			continue ;
		}
		const int32_t remove_x = target_it->chunk_x;
		const int32_t remove_z = target_it->chunk_z;
		const std::size_t old_size = staged_selected.size();
		staged_selected.erase(std::remove_if(staged_selected.begin(),
			staged_selected.end(), [remove_x, remove_z](
				const RevisionChunk &entry)
			{
				return (entry.chunk_x == remove_x && entry.chunk_z == remove_z);
			}), staged_selected.end());
		selection_changed = selection_changed
			|| staged_selected.size() != old_size;
		target_it = staged_rule_targets.erase(target_it);
		target_ownership_changed = true;
	}
	for (const RegenerationTargetChunk &target : target_chunks)
	{
		const bool was_rule_target = world_revision_contains_chunk(
			this->rules_target_chunks_, target.chunk_x, target.chunk_z);
		if (was_rule_target
			&& WorldRevisionChunkSet::contains(staged_selected,
				target.chunk_x, target.chunk_z))
			continue ;
		if (!was_rule_target
			&& WorldRevisionChunkSet::contains(staged_selected, target.chunk_x,
				target.chunk_z))
		{
			bool selected_as_bonus = false;
			for (const RevisionChunk &entry : staged_selected)
			{
				if (entry.chunk_x == target.chunk_x
					&& entry.chunk_z == target.chunk_z)
				{
					selected_as_bonus = entry.bonus;
					break ;
				}
			}
			if (!selected_as_bonus)
				return (FT_ERR_ALREADY_EXISTS);
			try
			{
				staged_rule_targets.push_back(
					{target.chunk_x, target.chunk_z, true});
			}
			catch (...)
			{
				return (FT_ERR_NO_MEMORY);
			}
			target_ownership_changed = true;
			continue ;
		}
		if (staged_selected.size() >= maximum_selected_chunks)
			return (FT_ERR_OUT_OF_RANGE);
		if (this->is_chunk_protected(target.chunk_x, target.chunk_z))
			return (FT_ERR_INVALID_OPERATION);
		const WorldChunk *target_chunk = this->world_.find_chunk(
			target.chunk_x, target.chunk_z);
		if (target_chunk == nullptr || !target_chunk->initialized)
			return (FT_ERR_NOT_FOUND);
		try
		{
			staged_selected.push_back({target.chunk_x, target.chunk_z, true});
			if (!was_rule_target)
				staged_rule_targets.push_back(
					{target.chunk_x, target.chunk_z, true});
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		if (!was_rule_target)
			target_ownership_changed = true;
		selection_changed = true;
	}
	if (selection_changed
		&& this->selection_revision_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	error_code = this->config_.initialize(staged_config);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	index = 0U;
	while (index < this->biome_rule_count_)
	{
		this->biome_enabled_[index] = staged_enabled[index];
		this->biome_weight_adjustments_[index] = staged_weights[index];
		++index;
	}
	if (selection_changed)
	{
		this->selected_.swap(staged_selected);
		this->selection_revision_ += 1U;
	}
	if (target_ownership_changed)
		this->rules_target_chunks_.swap(staged_rule_targets);
	this->feature_plans_.swap(staged_feature_plans);
	this->rules_hash_ = staged_rules_hash;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::cancel() noexcept
{
	if (!this->pending_ || this->barriers_active_
		|| this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	if (!this->selected_.empty()
		&& this->selection_revision_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	this->pending_ = false;
	if (!this->selected_.empty())
		this->selection_revision_ += 1U;
	this->selected_.clear();
	this->restored_selected_.clear();
	this->rules_target_chunks_.clear();
	this->feature_plans_.clear();
	this->decision_deadline_epoch_ = 0U;
	this->restored_selection_revision_ = 0U;
	this->restored_deadline_epoch_ = 0U;
	this->restored_mode_ = 0;
	this->restored_stage_mask_ = 0U;
	this->restored_barrier_state_ = false;
	this->lifecycle_restore_pending_ = false;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::set_barriers_active(bool active) noexcept
{
	if (active && (!this->pending_ || this->selected_.empty()))
		return (FT_ERR_INVALID_STATE);
	this->barriers_active_ = active;
	if (!active)
		this->decision_deadline_epoch_ = 0U;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::set_decision_deadline_epoch(
	uint64_t deadline_epoch) noexcept
{
	if (!this->pending_ || !this->barriers_active_ || deadline_epoch == 0U)
		return (FT_ERR_INVALID_STATE);
	if (this->decision_deadline_epoch_ != 0U
		&& this->decision_deadline_epoch_ != deadline_epoch)
		return (FT_ERR_INVALID_STATE);
	this->decision_deadline_epoch_ = deadline_epoch;
	return (FT_ERR_SUCCESS);
}

uint64_t WorldRevisionManager::decision_deadline_epoch() const noexcept
{
	return (this->decision_deadline_epoch_);
}

bool WorldRevisionManager::barriers_active() const noexcept
{
	return (this->barriers_active_);
}

bool WorldRevisionManager::barrier_blocks_chunk(int32_t chunk_x,
	int32_t chunk_z) const noexcept
{
	return (this->barriers_active_
		&& world_revision_contains_chunk(this->selected_, chunk_x, chunk_z));
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

int32_t WorldRevisionManager::feature_reservation_upper_bound(
	uint32_t *count_out) const noexcept
{
	uint64_t reservation_count = 0U;
	if (count_out == ft_nullptr || !this->pending_)
		return (FT_ERR_INVALID_ARGUMENT);
	for (const WorldGenerationPipeline::GenerationFeaturePlan &plan
		: this->feature_plans_)
	{
		if (plan.kind
			== WorldGenerationPipeline::GenerationFeaturePlan::VOLCANO)
		{
			const uint64_t footprint = std::max(
				plan.volcano_footprint_chunks, 1U);
			reservation_count += footprint * footprint;
		}
		else if (plan.kind
			== WorldGenerationPipeline::GenerationFeaturePlan::VILLAGE
			|| plan.kind == WorldGenerationPipeline::GenerationFeaturePlan::
				SURFACE_MAGMA_POOL
			|| plan.kind == WorldGenerationPipeline::GenerationFeaturePlan::
				RUINED_VILLAGE
			|| plan.kind == WorldGenerationPipeline::GenerationFeaturePlan::
				SHIPWRECK
			|| plan.kind == WorldGenerationPipeline::GenerationFeaturePlan::
				DESERT_PYRAMID
			|| plan.kind == WorldGenerationPipeline::GenerationFeaturePlan::
				ANCIENT_CANOPY
			|| plan.kind == WorldGenerationPipeline::GenerationFeaturePlan::
				SPIDER_NEST)
			++reservation_count;
	}
	*count_out = reservation_count > UINT32_MAX ? UINT32_MAX
		: static_cast<uint32_t>(reservation_count);
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::copy_selected_chunks(
	std::vector<World::RevisionChunkCoordinate> &chunks_out) const noexcept
{
	std::vector<World::RevisionChunkCoordinate> staged;
	try
	{
		staged.reserve(this->selected_.size());
		for (const RevisionChunk &chunk : this->selected_)
			staged.push_back({chunk.chunk_x, chunk.chunk_z});
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	chunks_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::copy_selected_paid_chunks(
	std::vector<World::RevisionChunkCoordinate> &chunks_out) const noexcept
{
	std::vector<World::RevisionChunkCoordinate> staged;
	try
	{
		staged.reserve(this->selected_.size());
		for (const RevisionChunk &chunk : this->selected_)
		{
			if (!chunk.bonus)
				staged.push_back({chunk.chunk_x, chunk.chunk_z});
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	chunks_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

uint64_t WorldRevisionManager::selection_revision() const noexcept
{
	return (this->selection_revision_);
}

uint64_t WorldRevisionManager::rules_hash() const noexcept
{
	return (this->rules_hash_);
}

uint64_t WorldRevisionManager::cost_policy_revision() const noexcept
{
	return (this->cost_policy_revision_);
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
	if (!this->pending_ || this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	if (!selected && world_revision_contains_chunk(this->rules_target_chunks_,
		chunk_x, chunk_z))
		return (FT_ERR_INVALID_OPERATION);
	if (selected && maximum_selected_chunks == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (maximum_selected_chunks > HARD_MAX_SELECTED_CHUNKS)
		maximum_selected_chunks = HARD_MAX_SELECTED_CHUNKS;
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
		if (this->selection_revision_ == std::numeric_limits<uint64_t>::max())
			return (FT_ERR_OUT_OF_RANGE);
		WorldRevisionChunkSet::set_membership(this->selected_, chunk_x, chunk_z,
			selected);
		this->selection_revision_ += 1U;
	}
	*current_selection_revision = this->selection_revision_;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::select_bonus_chunk(
	uint64_t expected_selection_revision, uint32_t maximum_selected_chunks,
	int32_t chunk_x, int32_t chunk_z,
	uint64_t *current_selection_revision) noexcept
{
	int32_t result;

	if (current_selection_revision == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*current_selection_revision = this->selection_revision_;
	if (expected_selection_revision != this->selection_revision_)
		return (FT_ERR_INVALID_STATE);
	if (!this->pending_ || this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	if (this->cost_policy_configured_
		&& maximum_selected_chunks
			> this->cost_policy_.maximum_selected_chunks)
		maximum_selected_chunks = this->cost_policy_.maximum_selected_chunks;
	result = this->validate_bonus_chunk(expected_selection_revision,
		maximum_selected_chunks, chunk_x, chunk_z);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->select_chunk(expected_selection_revision,
		maximum_selected_chunks, chunk_x, chunk_z, true,
		current_selection_revision);
	if (result != FT_ERR_SUCCESS)
		return (result);
	for (RevisionChunk &entry : this->selected_)
	{
		if (entry.chunk_x == chunk_x && entry.chunk_z == chunk_z)
		{
			entry.bonus = true;
			return (FT_ERR_SUCCESS);
		}
	}
	return (FT_ERR_INVALID_STATE);
}

int32_t WorldRevisionManager::validate_bonus_chunk(
	uint64_t expected_selection_revision, uint32_t maximum_selected_chunks,
	int32_t chunk_x, int32_t chunk_z) const noexcept
{
	const WorldChunk *chunk;

	if (expected_selection_revision != this->selection_revision_)
		return (FT_ERR_INVALID_STATE);
	if (!this->pending_ || this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	if (this->cost_policy_configured_
		&& maximum_selected_chunks
			> this->cost_policy_.maximum_selected_chunks)
		maximum_selected_chunks = this->cost_policy_.maximum_selected_chunks;
	if (WorldRevisionChunkSet::contains(this->selected_, chunk_x, chunk_z))
		return (FT_ERR_ALREADY_EXISTS);
	if (maximum_selected_chunks == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (maximum_selected_chunks > HARD_MAX_SELECTED_CHUNKS)
		maximum_selected_chunks = HARD_MAX_SELECTED_CHUNKS;
	if (this->selected_.size() >= maximum_selected_chunks)
		return (FT_ERR_OUT_OF_RANGE);
	if (this->is_chunk_protected(chunk_x, chunk_z))
		return (FT_ERR_INVALID_OPERATION);
	chunk = this->world_.find_chunk(chunk_x, chunk_z);
	if (chunk == nullptr || !chunk->initialized)
		return (FT_ERR_NOT_FOUND);
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::unselect_chunk(int32_t chunk_x,
	int32_t chunk_z) noexcept
{
	uint64_t current_selection_revision;

	return (this->select_chunk(this->selection_revision_,
		std::numeric_limits<uint32_t>::max(), chunk_x, chunk_z, false,
		&current_selection_revision));
}

int32_t WorldRevisionManager::configure_cost_policy(
	uint64_t expected_policy_revision, const WorldRevisionCostPolicy &policy,
	uint64_t *current_policy_revision) noexcept
{
	if (current_policy_revision == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*current_policy_revision = this->cost_policy_revision_;
	if (expected_policy_revision != this->cost_policy_revision_)
		return (FT_ERR_INVALID_STATE);
	if (policy.maximum_selected_chunks == 0U
		|| policy.maximum_selected_chunks > HARD_MAX_SELECTED_CHUNKS
		|| policy.currency_item_id == 0U
		|| policy.maximum_total_currency_cost == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->cost_policy_revision_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	this->cost_policy_ = policy;
	this->cost_policy_configured_ = true;
	this->cost_policy_revision_ += 1U;
	*current_policy_revision = this->cost_policy_revision_;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::query_start_cost(
	uint64_t expected_selection_revision,
	uint64_t expected_policy_revision, uint32_t opening_card_count,
	WorldRevisionCostQuote *quote) const noexcept
{
	WorldRevisionCostQuote result = {};
	uint64_t selected_count;
	uint64_t bonus_count = 0U;
	uint64_t paid_count;
	uint64_t opening_card_cost;
	uint32_t card_index;

	if (quote == nullptr || opening_card_count < 4U
		|| opening_card_count > 7U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (expected_selection_revision != this->selection_revision_)
		return (FT_ERR_INVALID_STATE);
	if (!this->cost_policy_configured_)
		return (FT_ERR_INVALID_STATE);
	if (expected_policy_revision != this->cost_policy_revision_)
		return (FT_ERR_INVALID_STATE);
	if (!this->pending_ || this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	if (this->selected_.empty())
		return (FT_ERR_INVALID_OPERATION);
	if (this->selected_.size() > this->cost_policy_.maximum_selected_chunks
		|| this->selected_.size() > std::numeric_limits<uint32_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	for (const RevisionChunk &entry : this->selected_)
	{
		const WorldChunk *chunk = this->world_.find_chunk(entry.chunk_x,
			entry.chunk_z);

		if (chunk == nullptr || !chunk->initialized
			|| this->is_chunk_protected(entry.chunk_x, entry.chunk_z))
			return (FT_ERR_INVALID_STATE);
	}
	selected_count = this->selected_.size();
	for (const RevisionChunk &entry : this->selected_)
	{
		if (entry.bonus)
			bonus_count += 1U;
	}
	if (bonus_count > selected_count
		|| bonus_count > std::numeric_limits<uint32_t>::max())
		return (FT_ERR_INVALID_STATE);
	paid_count = selected_count - bonus_count;
	if (this->cost_policy_.currency_units_per_chunk != 0U
		&& paid_count > std::numeric_limits<uint64_t>::max()
			/ this->cost_policy_.currency_units_per_chunk)
		return (FT_ERR_OUT_OF_RANGE);
	result.chunk_currency_cost = paid_count
		* this->cost_policy_.currency_units_per_chunk;
	opening_card_cost = this->cost_policy_.four_card_opening_cost;
	card_index = 4U;
	while (card_index < opening_card_count)
	{
		if (opening_card_cost > std::numeric_limits<uint64_t>::max() / 2U)
			return (FT_ERR_OUT_OF_RANGE);
		opening_card_cost *= 2U;
		++card_index;
	}
	if (result.chunk_currency_cost > std::numeric_limits<uint64_t>::max()
		- opening_card_cost)
		return (FT_ERR_OUT_OF_RANGE);
	result.total_currency_cost = result.chunk_currency_cost + opening_card_cost;
	if (result.total_currency_cost
		> this->cost_policy_.maximum_total_currency_cost)
		return (FT_ERR_OUT_OF_RANGE);
	result.revision_identifier = this->revision_id_;
	result.selection_revision = this->selection_revision_;
	result.cost_policy_revision = this->cost_policy_revision_;
	result.selected_paid_chunks = static_cast<uint32_t>(paid_count);
	result.selected_bonus_chunks = static_cast<uint32_t>(bonus_count);
	result.opening_card_count = opening_card_count;
	result.currency_item_id = this->cost_policy_.currency_item_id;
	result.opening_card_currency_cost = opening_card_cost;
	*quote = result;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::set_chunk_protected(int32_t chunk_x,
	int32_t chunk_z, bool protected_state) noexcept
{
	uint64_t current_selection_revision;

	return (this->set_chunk_protected(this->selection_revision_, chunk_x,
		chunk_z, protected_state, &current_selection_revision));
}

int32_t WorldRevisionManager::set_chunk_protected(
	uint64_t expected_selection_revision, int32_t chunk_x, int32_t chunk_z,
	bool protected_state, uint64_t *current_selection_revision) noexcept
{
	const bool already_protected = WorldRevisionChunkSet::contains(
		this->manual_protected_, chunk_x, chunk_z);
	bool selected_will_be_removed = false;
	std::vector<RevisionChunk>::const_iterator selected_entry;

	if (current_selection_revision == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*current_selection_revision = this->selection_revision_;
	if (expected_selection_revision != this->selection_revision_)
		return (FT_ERR_INVALID_STATE);
	if (protected_state)
	{
		selected_entry = this->selected_.begin();
		while (selected_entry != this->selected_.end())
		{
			if (WorldRevisionChunkSet::within_protection_ring(
					selected_entry->chunk_x, selected_entry->chunk_z,
					chunk_x, chunk_z))
			{
				selected_will_be_removed = true;
				break ;
			}
			++selected_entry;
		}
	}

	if (already_protected == protected_state && !selected_will_be_removed)
		return (FT_ERR_SUCCESS);
	if (this->selection_revision_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	WorldRevisionChunkSet::set_membership(this->manual_protected_, chunk_x,
		chunk_z, protected_state);
	if (protected_state)
	{
		std::vector<RevisionChunk>::iterator entry = this->selected_.begin();

		while (entry != this->selected_.end())
		{
			if (WorldRevisionChunkSet::within_protection_ring(entry->chunk_x,
					entry->chunk_z, chunk_x, chunk_z))
				entry = this->selected_.erase(entry);
			else
				++entry;
		}
	}
	this->selection_revision_ += 1U;
	*current_selection_revision = this->selection_revision_;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::set_occupied_chunks(
	const std::vector<OccupiedChunk> &occupied_chunks,
	uint64_t *current_selection_revision) noexcept
{
	std::vector<OccupiedChunk> staged;
	bool occupancy_changed = false;
	bool selection_removed = false;

	if (current_selection_revision == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*current_selection_revision = this->selection_revision_;
	if (occupied_chunks.size()
		> WorldRevisionManager::MAXIMUM_OCCUPIED_CHUNKS)
		return (FT_ERR_OUT_OF_RANGE);
	try
	{
		staged.reserve(occupied_chunks.size());
		for (const OccupiedChunk &candidate : occupied_chunks)
			staged.push_back(candidate);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	std::sort(staged.begin(), staged.end(),
		[](const OccupiedChunk &left, const OccupiedChunk &right)
		{
			if (left.chunk_x != right.chunk_x)
				return (left.chunk_x < right.chunk_x);
			return (left.chunk_z < right.chunk_z);
		});
	staged.erase(std::unique(staged.begin(), staged.end(),
		[](const OccupiedChunk &left, const OccupiedChunk &right)
		{
			return (left.chunk_x == right.chunk_x
				&& left.chunk_z == right.chunk_z);
		}), staged.end());
	occupancy_changed = staged.size() != this->occupied_chunks_.size();
	if (!occupancy_changed)
	{
		for (std::size_t index = 0U; index < staged.size(); ++index)
		{
			if (staged[index].chunk_x
					!= this->occupied_chunks_[index].chunk_x
				|| staged[index].chunk_z
					!= this->occupied_chunks_[index].chunk_z)
			{
				occupancy_changed = true;
				break ;
			}
		}
	}
	for (const RevisionChunk &selected : this->selected_)
	{
		for (const OccupiedChunk &occupied : staged)
		{
			if (WorldRevisionChunkSet::within_protection_ring(
					selected.chunk_x, selected.chunk_z,
					occupied.chunk_x, occupied.chunk_z))
			{
				selection_removed = true;
				break ;
			}
		}
		if (selection_removed)
			break ;
	}
	if (!occupancy_changed && !selection_removed)
		return (FT_ERR_SUCCESS);
	if (this->barriers_active_)
	{
		if (selection_removed)
			return (FT_ERR_INVALID_OPERATION);
		this->occupied_chunks_.swap(staged);
		return (FT_ERR_SUCCESS);
	}
	if (this->selection_revision_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	this->occupied_chunks_.swap(staged);
	std::vector<RevisionChunk>::iterator selected = this->selected_.begin();
	while (selected != this->selected_.end())
	{
		bool occupied_ring = false;
		for (const OccupiedChunk &occupied : this->occupied_chunks_)
		{
			if (WorldRevisionChunkSet::within_protection_ring(
					selected->chunk_x, selected->chunk_z,
					occupied.chunk_x, occupied.chunk_z))
			{
				occupied_ring = true;
				break ;
			}
		}
		if (occupied_ring)
			selected = this->selected_.erase(selected);
		else
			++selected;
	}
	this->selection_revision_ += 1U;
	*current_selection_revision = this->selection_revision_;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::configure_loom_station(
	uint64_t expected_selection_revision, int32_t chunk_x, int32_t chunk_z,
	uint64_t *current_selection_revision) noexcept
{
	if (current_selection_revision == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*current_selection_revision = this->selection_revision_;
	if (expected_selection_revision != this->selection_revision_)
		return (FT_ERR_INVALID_STATE);
	if (this->pending_ || this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	if (this->loom_station_configured_
		&& !this->loom_station_block_configured_
		&& this->loom_station_chunk_x_ == chunk_x
		&& this->loom_station_chunk_z_ == chunk_z)
		return (FT_ERR_SUCCESS);
	if (this->selection_revision_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	this->loom_station_configured_ = true;
	this->loom_station_chunk_x_ = chunk_x;
	this->loom_station_chunk_z_ = chunk_z;
	this->loom_station_block_configured_ = false;
	this->loom_station_block_x_ = 0;
	this->loom_station_block_y_ = 0;
	this->loom_station_block_z_ = 0;
	this->loom_station_block_id_ = 0U;
	std::vector<RevisionChunk>::iterator selected = this->selected_.begin();
	while (selected != this->selected_.end())
	{
		if (WorldRevisionChunkSet::within_protection_square(
				selected->chunk_x, selected->chunk_z, chunk_x, chunk_z,
				WorldRevisionManager::LOOM_SAFE_ZONE_CHUNK_RADIUS))
			selected = this->selected_.erase(selected);
		else
			++selected;
	}
	this->selection_revision_ += 1U;
	*current_selection_revision = this->selection_revision_;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::configure_loom_station_block(
	uint64_t expected_selection_revision, int32_t block_x, int32_t block_y,
	int32_t block_z, uint32_t block_id,
	uint64_t *current_selection_revision) noexcept
{
	int32_t chunk_x = block_x / GAME_VOXEL_CHUNK_WIDTH;
	int32_t chunk_z = block_z / GAME_VOXEL_CHUNK_DEPTH;
	uint32_t live_block_id = 0U;

	if (current_selection_revision == nullptr || block_id == 0U
		|| block_y < 0 || block_y >= GAME_VOXEL_CHUNK_HEIGHT)
		return (FT_ERR_INVALID_ARGUMENT);
	if (block_x < 0 && block_x % GAME_VOXEL_CHUNK_WIDTH != 0)
		--chunk_x;
	if (block_z < 0 && block_z % GAME_VOXEL_CHUNK_DEPTH != 0)
		--chunk_z;
	if (!this->world_.block_id_at(block_x, block_y, block_z, &live_block_id))
		return (FT_ERR_NOT_FOUND);
	if (live_block_id != block_id)
		return (FT_ERR_INVALID_ARGUMENT);
	*current_selection_revision = this->selection_revision_;
	if (expected_selection_revision != this->selection_revision_)
		return (FT_ERR_INVALID_STATE);
	if (this->pending_ || this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	if (this->loom_station_configured_ && this->loom_station_block_configured_
		&& this->loom_station_block_x_ == block_x
		&& this->loom_station_block_y_ == block_y
		&& this->loom_station_block_z_ == block_z
		&& this->loom_station_block_id_ == block_id)
		return (FT_ERR_SUCCESS);
	if (this->selection_revision_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	this->loom_station_configured_ = true;
	this->loom_station_chunk_x_ = chunk_x;
	this->loom_station_chunk_z_ = chunk_z;
	this->loom_station_block_configured_ = true;
	this->loom_station_block_x_ = block_x;
	this->loom_station_block_y_ = block_y;
	this->loom_station_block_z_ = block_z;
	this->loom_station_block_id_ = block_id;
	std::vector<RevisionChunk>::iterator selected = this->selected_.begin();
	while (selected != this->selected_.end())
	{
		if (WorldRevisionChunkSet::within_protection_square(
				selected->chunk_x, selected->chunk_z, chunk_x, chunk_z,
				WorldRevisionManager::LOOM_SAFE_ZONE_CHUNK_RADIUS))
			selected = this->selected_.erase(selected);
		else
			++selected;
	}
	this->selection_revision_ += 1U;
	*current_selection_revision = this->selection_revision_;
	return (FT_ERR_SUCCESS);
}

bool WorldRevisionManager::has_loom_station() const noexcept
{
	return (this->loom_station_configured_);
}

int32_t WorldRevisionManager::get_loom_station_chunk(int32_t *chunk_x_out,
	int32_t *chunk_z_out) const noexcept
{
	if (chunk_x_out == nullptr || chunk_z_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->loom_station_configured_)
		return (FT_ERR_CONFIGURATION);
	*chunk_x_out = this->loom_station_chunk_x_;
	*chunk_z_out = this->loom_station_chunk_z_;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::get_loom_station_block(bool *configured_out,
	int32_t *block_x_out, int32_t *block_y_out, int32_t *block_z_out,
	uint32_t *block_id_out) const noexcept
{
	if (configured_out == nullptr || block_x_out == nullptr
		|| block_y_out == nullptr || block_z_out == nullptr
		|| block_id_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*configured_out = this->loom_station_block_configured_;
	*block_x_out = this->loom_station_block_x_;
	*block_y_out = this->loom_station_block_y_;
	*block_z_out = this->loom_station_block_z_;
	*block_id_out = this->loom_station_block_id_;
	return (this->loom_station_configured_ ? FT_ERR_SUCCESS
		: FT_ERR_CONFIGURATION);
}

int32_t WorldRevisionManager::invalidate_loom_station() noexcept
{
	int32_t result;
	uint64_t required_revision_steps = 1U;

	if (!this->loom_station_configured_)
		return (FT_ERR_SUCCESS);
	if (this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	if (this->pending_ && !this->selected_.empty())
		required_revision_steps += 1U;
	if (this->selection_revision_ > std::numeric_limits<uint64_t>::max()
			- required_revision_steps)
		return (FT_ERR_OUT_OF_RANGE);
	if (this->pending_)
	{
		result = this->cancel();
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	this->loom_station_configured_ = false;
	this->loom_station_chunk_x_ = 0;
	this->loom_station_chunk_z_ = 0;
	this->loom_station_block_configured_ = false;
	this->loom_station_block_x_ = 0;
	this->loom_station_block_y_ = 0;
	this->loom_station_block_z_ = 0;
	this->loom_station_block_id_ = 0U;
	this->selection_revision_ += 1U;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::invalidate_loom_station_if_block_changed(
	int32_t block_x, int32_t block_y, int32_t block_z,
	uint32_t new_block_id) noexcept
{
	if (!this->loom_station_configured_
		|| !this->loom_station_block_configured_
		|| this->loom_station_block_x_ != block_x
		|| this->loom_station_block_y_ != block_y
		|| this->loom_station_block_z_ != block_z
		|| this->loom_station_block_id_ == new_block_id)
		return (FT_ERR_SUCCESS);
	return (this->invalidate_loom_station());
}

int32_t WorldRevisionManager::get_protection_info(int32_t chunk_x,
	int32_t chunk_z, World::RevisionProtectionInfo *info_out) const noexcept
{
	const WorldChunk *chunk;

	if (info_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*info_out = {};
	chunk = this->world_.find_chunk(chunk_x, chunk_z);
	if (chunk != nullptr && chunk->chunk.is_generation_protected() == FT_TRUE)
		info_out->generation_protected = true;
	if (this->loom_station_configured_
		&& WorldRevisionChunkSet::within_protection_square(chunk_x, chunk_z,
			this->loom_station_chunk_x_, this->loom_station_chunk_z_,
			WorldRevisionManager::LOOM_SAFE_ZONE_CHUNK_RADIUS))
		info_out->loom_safe_zone = true;
	for (const RevisionChunk &entry : this->manual_protected_)
	{
		if (WorldRevisionChunkSet::within_protection_ring(chunk_x, chunk_z,
				entry.chunk_x, entry.chunk_z))
		{
			info_out->manually_protected = true;
			break ;
		}
	}
	for (const OccupiedChunk &entry : this->occupied_chunks_)
	{
		if (WorldRevisionChunkSet::within_protection_ring(chunk_x, chunk_z,
				entry.chunk_x, entry.chunk_z))
		{
			info_out->occupied_player_exclusion = true;
			break ;
		}
	}
	return (FT_ERR_SUCCESS);
}

bool WorldRevisionManager::is_chunk_protected(int32_t chunk_x,
	int32_t chunk_z) const noexcept
{
	World::RevisionProtectionInfo info = {};

	if (this->get_protection_info(chunk_x, chunk_z, &info)
		!= FT_ERR_SUCCESS)
		return (true);
	return (info.generation_protected || info.loom_safe_zone
		|| info.manually_protected || info.occupied_player_exclusion);
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
			if (WorldRevisionChunkSet::within_protection_ring(chunk_x, chunk_z,
					entry.chunk_x, entry.chunk_z))
				return (World::REVISION_TRANSITION);
		}
	}
	return (World::REVISION_UNCHANGED);
}

int32_t WorldRevisionManager::serialize_metadata(
	std::vector<uint8_t> &bytes_out) const noexcept
{
	std::vector<uint8_t> bytes;
	int32_t error_code = WorldRevisionMetadataStore::serialize(this->revision_id_,
		this->manual_protected_, this->cost_policy_,
		this->cost_policy_revision_, this->cost_policy_configured_,
		this->loom_station_configured_, this->loom_station_chunk_x_,
		this->loom_station_chunk_z_, this->loom_station_block_configured_,
		this->loom_station_block_x_, this->loom_station_block_y_,
		this->loom_station_block_z_, this->loom_station_block_id_, bytes);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (this->barriers_active_ && (!this->pending_
		|| this->selected_.empty()
		|| this->selected_.size() > HARD_MAX_SELECTED_CHUNKS))
		return (FT_ERR_INVALID_STATE);
	try
	{
		const std::size_t extension_start = bytes.size();
		world_revision_lifecycle_append_u32(bytes, 0x314C5257U);
		world_revision_lifecycle_append_u32(bytes, 2U);
		world_revision_lifecycle_append_u32(bytes,
			this->barriers_active_ ? 1U : 0U);
		world_revision_lifecycle_append_u32(bytes,
			static_cast<uint32_t>(this->mode_));
		world_revision_lifecycle_append_u32(bytes, this->stage_mask_);
		world_revision_lifecycle_append_u64(bytes,
			this->selection_revision_);
		world_revision_lifecycle_append_u64(bytes,
			this->barriers_active_ ? this->decision_deadline_epoch_ : 0U);
		world_revision_lifecycle_append_u32(bytes,
			this->barriers_active_
				? static_cast<uint32_t>(this->selected_.size()) : 0U);
		if (this->barriers_active_)
		{
			for (const RevisionChunk &entry : this->selected_)
			{
				world_revision_lifecycle_append_u32(bytes,
					static_cast<uint32_t>(entry.chunk_x));
				world_revision_lifecycle_append_u32(bytes,
					static_cast<uint32_t>(entry.chunk_z));
				world_revision_lifecycle_append_u32(bytes,
					entry.bonus ? 1U : 0U);
			}
		}
		world_revision_lifecycle_append_u64(bytes,
			world_revision_lifecycle_hash(bytes.data() + extension_start,
				bytes.size() - extension_start));
		world_revision_lifecycle_append_u32(bytes,
			static_cast<uint32_t>(bytes.size() - extension_start + 4U));
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	bytes_out.swap(bytes);
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::save_metadata(const char *file_path) const noexcept
{
	std::vector<uint8_t> bytes;
	int32_t error_code;
	if (file_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = this->serialize_metadata(bytes);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (file_write_all_atomic(file_path,
		reinterpret_cast<const char *>(bytes.data()),
		bytes.size()));
}

int32_t WorldRevisionManager::load_metadata(const char *file_path) noexcept
{
	ft_string content;
	ft_size_t file_size = 0U;
	int32_t error_code;
	if (file_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->pending_ || this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	error_code = file_get_size(file_path, &file_size);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (file_size == 0U || file_size > 1024U * 1024U)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = content.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = file_read_all(file_path, content);
	if (error_code == FT_ERR_SUCCESS
		&& (content.size() == 0U || content.size() > 1024U * 1024U))
		error_code = FT_ERR_INVALID_ARGUMENT;
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->deserialize_metadata(
			reinterpret_cast<const uint8_t *>(content.data()), content.size());
	const int32_t destroy_error = content.destroy();
	if (error_code == FT_ERR_SUCCESS && destroy_error != FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldRevisionManager::deserialize_metadata(const uint8_t *bytes,
	std::size_t byte_count) noexcept
{
	std::vector<uint8_t> staged_bytes;
	uint32_t staged_revision_id;
	std::vector<RevisionChunk> staged_manual_protected;
	WorldRevisionCostPolicy staged_cost_policy = {};
	uint64_t staged_cost_policy_revision = 0U;
	bool staged_cost_policy_configured = false;
	bool staged_cost_policy_stored = false;
	bool staged_station_configured = false;
	bool staged_station_stored = false;
	int32_t staged_station_chunk_x = 0;
	int32_t staged_station_chunk_z = 0;
	bool staged_station_block_configured = false;
	int32_t staged_station_block_x = 0;
	int32_t staged_station_block_y = 0;
	int32_t staged_station_block_z = 0;
	uint32_t staged_station_block_id = 0U;
	std::vector<RevisionChunk> staged_restored_selected;
	uint64_t staged_restored_selection_revision = 0U;
	uint64_t staged_restored_deadline_epoch = 0U;
	int32_t staged_restored_mode = World::REGEN_FULL;
	uint32_t staged_restored_stage_mask = 0U;
	bool staged_restored_barrier_state = false;
	bool staged_lifecycle_extension = false;
	int32_t error_code;

	if (bytes == nullptr || byte_count == 0U
		|| byte_count > 1024U * 1024U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->pending_ || this->progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	try
	{
		staged_bytes.assign(bytes, bytes + byte_count);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (byte_count >= 44U)
	{
		std::size_t tail_offset = byte_count - 4U;
		uint32_t extension_bytes = 0U;
		if (world_revision_lifecycle_read_u32(bytes, byte_count,
				&tail_offset, &extension_bytes)
			&& extension_bytes >= 44U && extension_bytes <= byte_count)
		{
			const std::size_t extension_start = byte_count - extension_bytes;
			std::size_t offset = extension_start;
			uint32_t magic = 0U;
			uint32_t version = 0U;
			uint32_t flags = 0U;
			uint32_t mode = 0U;
			uint32_t stage_mask = 0U;
			uint32_t selected_count = 0U;
			uint64_t stored_checksum = 0U;
		if (world_revision_lifecycle_read_u32(bytes, byte_count,
					&offset, &magic)
				&& magic == 0x314C5257U
				&& world_revision_lifecycle_read_u32(bytes, byte_count,
					&offset, &version)
				&& (version == 1U || version == 2U)
				&& world_revision_lifecycle_read_u32(bytes, byte_count,
					&offset, &flags)
				&& flags <= 1U
				&& world_revision_lifecycle_read_u32(bytes, byte_count,
					&offset, &mode)
				&& world_revision_lifecycle_read_u32(bytes, byte_count,
					&offset, &stage_mask)
				&& world_revision_lifecycle_read_u64(bytes, byte_count,
					&offset, &staged_restored_selection_revision)
				&& (version == 1U
					|| world_revision_lifecycle_read_u64(bytes, byte_count,
						&offset, &staged_restored_deadline_epoch))
				&& world_revision_lifecycle_read_u32(bytes, byte_count,
					&offset, &selected_count)
				&& selected_count <= HARD_MAX_SELECTED_CHUNKS
				&& offset <= byte_count
				&& byte_count - offset >= 12U
				&& static_cast<uint64_t>(selected_count) * 12U
					== byte_count - offset - 12U)
			{
				try
				{
					staged_restored_selected.reserve(selected_count);
				}
				catch (...)
				{
					return (FT_ERR_NO_MEMORY);
				}
				bool valid_extension = true;
				for (uint32_t index = 0U; index < selected_count; ++index)
				{
					uint32_t x_value = 0U;
					uint32_t z_value = 0U;
					uint32_t bonus = 0U;
					if (!world_revision_lifecycle_read_u32(bytes, byte_count,
							&offset, &x_value)
						|| !world_revision_lifecycle_read_u32(bytes, byte_count,
							&offset, &z_value)
						|| !world_revision_lifecycle_read_u32(bytes, byte_count,
							&offset, &bonus)
						|| bonus > 1U)
					{
						valid_extension = false;
						break ;
					}
					const int32_t chunk_x = static_cast<int32_t>(x_value);
					const int32_t chunk_z = static_cast<int32_t>(z_value);
					if (world_revision_contains_chunk(staged_restored_selected,
							chunk_x, chunk_z))
					{
						valid_extension = false;
						break ;
					}
					staged_restored_selected.push_back(
						{chunk_x, chunk_z, bonus != 0U});
				}
			const std::size_t checksum_offset = offset;
			if (!valid_extension
				|| !world_revision_lifecycle_read_u64(bytes, byte_count,
					&offset, &stored_checksum)
				|| offset + 4U != byte_count
				|| stored_checksum != world_revision_lifecycle_hash(
					bytes + extension_start, checksum_offset - extension_start))
				return (FT_ERR_INVALID_ARGUMENT);
			if (flags == 1U
				&& (selected_count == 0U
					|| mode != static_cast<uint32_t>(World::REGEN_FULL)
					|| stage_mask != stage_mask_for_mode(
						static_cast<int32_t>(mode))
					|| staged_restored_selection_revision == 0U))
				return (FT_ERR_INVALID_ARGUMENT);
			if (flags == 0U && selected_count != 0U)
				return (FT_ERR_INVALID_ARGUMENT);
			staged_restored_barrier_state = flags == 1U;
			staged_restored_mode = static_cast<int32_t>(mode);
			staged_restored_stage_mask = stage_mask;
			staged_lifecycle_extension = true;
			staged_bytes.resize(extension_start);
		}
	}
	}
	error_code = WorldRevisionMetadataStore::deserialize(staged_bytes,
		staged_revision_id, staged_manual_protected, staged_cost_policy,
		staged_cost_policy_revision, staged_cost_policy_configured,
		staged_cost_policy_stored, staged_station_configured,
		staged_station_chunk_x, staged_station_chunk_z, staged_station_stored,
		staged_station_block_configured, staged_station_block_x,
		staged_station_block_y, staged_station_block_z,
		staged_station_block_id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->revision_id_ = staged_revision_id;
	this->manual_protected_.swap(staged_manual_protected);
	if (staged_cost_policy_stored)
	{
		this->cost_policy_ = staged_cost_policy;
		this->cost_policy_revision_ = staged_cost_policy_revision;
		this->cost_policy_configured_ = staged_cost_policy_configured;
	}
	if (staged_station_stored)
	{
		this->loom_station_configured_ = staged_station_configured;
		this->loom_station_chunk_x_ = staged_station_chunk_x;
		this->loom_station_chunk_z_ = staged_station_chunk_z;
		this->loom_station_block_configured_ = staged_station_block_configured;
		this->loom_station_block_x_ = staged_station_block_x;
		this->loom_station_block_y_ = staged_station_block_y;
		this->loom_station_block_z_ = staged_station_block_z;
		this->loom_station_block_id_ = staged_station_block_id;
	}
	this->restored_selected_.swap(staged_restored_selected);
	this->restored_selection_revision_ =
		staged_restored_selection_revision;
	this->restored_deadline_epoch_ = staged_restored_deadline_epoch;
	this->restored_mode_ = staged_restored_mode;
	this->restored_stage_mask_ = staged_restored_stage_mask;
	this->restored_barrier_state_ = staged_restored_barrier_state;
	this->lifecycle_restore_pending_ = staged_lifecycle_extension
		&& staged_restored_barrier_state;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionManager::restore_committed_revision(
	const RegenerationWorldRules &rules) noexcept
{
	int32_t error_code;
	uint32_t maximum_selected_chunks;
	if (!this->lifecycle_restore_pending_)
		return (FT_ERR_SUCCESS);
	if (!this->restored_barrier_state_ || this->pending_ || this->barriers_active_
		|| this->restored_selected_.empty())
		return (FT_ERR_INVALID_STATE);
	error_code = this->begin(this->world_.voxel_generation_settings(),
		this->restored_mode_, rules);
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: committed restore begin failed error=%d mode=%d\n",
			error_code, this->restored_mode_);
		return (error_code);
	}
	maximum_selected_chunks = this->cost_policy_configured_
		? this->cost_policy_.maximum_selected_chunks
		: WorldRevisionManager::HARD_MAX_SELECTED_CHUNKS;
	for (const RevisionChunk &saved : this->restored_selected_)
	{
		auto found = std::find_if(this->selected_.begin(), this->selected_.end(),
			[&saved](const RevisionChunk &entry)
			{
				return (entry.chunk_x == saved.chunk_x
					&& entry.chunk_z == saved.chunk_z);
			});
		if (found != this->selected_.end())
		{
			if (!saved.bonus && found->bonus)
				return (FT_ERR_CONFIGURATION);
			found->bonus = saved.bonus;
			continue ;
		}
		/* Saved reservations must be restored before chunk streaming starts. */
		if (this->selected_.size() >= maximum_selected_chunks)
		{
			std::fprintf(stderr,
				"world-revision: committed restore selection cap selected=%zu maximum=%u\n",
				this->selected_.size(), maximum_selected_chunks);
			return (FT_ERR_CONFIGURATION);
		}
		if (this->is_chunk_protected(saved.chunk_x, saved.chunk_z))
		{
			std::fprintf(stderr,
				"world-revision: committed restore selected protected chunk x=%d z=%d\n",
				saved.chunk_x, saved.chunk_z);
			return (FT_ERR_CONFIGURATION);
		}
		if (this->selection_revision_ == std::numeric_limits<uint64_t>::max())
			return (FT_ERR_OUT_OF_RANGE);
		try
		{
			this->selected_.push_back(saved);
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		++this->selection_revision_;
	}
	if (this->selected_.size() != this->restored_selected_.size())
	{
		std::fprintf(stderr,
			"world-revision: committed restore selection mismatch selected=%zu restored=%zu\n",
			this->selected_.size(), this->restored_selected_.size());
		return (FT_ERR_CONFIGURATION);
	}
	this->selection_revision_ = this->restored_selection_revision_;
	this->decision_deadline_epoch_ = this->restored_deadline_epoch_;
	this->stage_mask_ = this->restored_stage_mask_;
	this->barriers_active_ = true;
	this->lifecycle_restore_pending_ = false;
	return (FT_ERR_SUCCESS);
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
	const int32_t result = WorldRevisionRegenerator::finish(*this, this->world_);
	if (result == FT_ERR_SUCCESS)
	{
		this->barriers_active_ = false;
		this->decision_deadline_epoch_ = 0U;
	}
	return (result);
}

int32_t WorldRevisionManager::start_regeneration() noexcept
{
	return (WorldRevisionRegenerator::start(*this, this->world_));
}

int32_t WorldRevisionManager::retry_failed_regeneration() noexcept
{
	int32_t result;
	if (!this->barriers_active_ || this->pending_
		|| this->progress_.active() || this->selected_.empty())
		return (FT_ERR_INVALID_STATE);
	this->progress_.reset();
	this->pending_ = true;
	result = WorldRevisionRegenerator::start(*this, this->world_);
	if (result != FT_ERR_SUCCESS && !this->progress_.active())
		this->pending_ = false;
	return (result);
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
