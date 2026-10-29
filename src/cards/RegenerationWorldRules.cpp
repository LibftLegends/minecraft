#include "RegenerationWorldRules.hpp"
#include "../../Libft/Modules/File/file_utils.hpp"

#include <limits>
#include <new>
#include <algorithm>
#include <cstdio>

static void regeneration_rules_append_u32(std::vector<uint8_t> &bytes,
	uint32_t value)
{
	for (uint32_t index = 0U; index < 4U; ++index)
		bytes.push_back(static_cast<uint8_t>(value >> (index * 8U)));
}

static void regeneration_rules_append_u64(std::vector<uint8_t> &bytes,
	uint64_t value)
{
	for (uint32_t index = 0U; index < 8U; ++index)
		bytes.push_back(static_cast<uint8_t>(value >> (index * 8U)));
}

static uint32_t regeneration_rules_read_u32(const uint8_t *bytes,
	size_t offset) noexcept
{
	return (static_cast<uint32_t>(bytes[offset])
		| (static_cast<uint32_t>(bytes[offset + 1U]) << 8U)
		| (static_cast<uint32_t>(bytes[offset + 2U]) << 16U)
		| (static_cast<uint32_t>(bytes[offset + 3U]) << 24U));
}

static uint64_t regeneration_rules_read_u64(const uint8_t *bytes,
	size_t offset) noexcept
{
	uint64_t value = 0U;
	for (uint32_t index = 0U; index < 8U; ++index)
		value |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8U);
	return (value);
}

static uint64_t regeneration_rules_checksum(const uint8_t *bytes,
	size_t count) noexcept
{
	uint64_t hash = 14695981039346656037ULL;
	for (size_t index = 0U; index < count; ++index)
	{
		hash ^= bytes[index];
		hash *= 1099511628211ULL;
	}
	return (hash);
}

static void regeneration_rules_manifest_hash_byte(uint64_t *hash,
	uint8_t value) noexcept
{
	*hash ^= value;
	*hash *= 1099511628211ULL;
}

static void regeneration_rules_manifest_hash_u32(uint64_t *hash,
	uint32_t value) noexcept
{
	for (uint32_t shift = 0U; shift < 32U; shift += 8U)
		regeneration_rules_manifest_hash_byte(hash,
			static_cast<uint8_t>(value >> shift));
}

static void regeneration_rules_manifest_hash_u64(uint64_t *hash,
	uint64_t value) noexcept
{
	uint32_t index = 0U;

	while (index < 8U)
	{
		regeneration_rules_manifest_hash_byte(hash,
			static_cast<uint8_t>(value & 0xFFU));
		value >>= 8;
		++index;
	}
}

static uint32_t regeneration_world_rules_gcd(uint32_t left,
	uint32_t right) noexcept
{
	uint32_t remainder;

	while (right != 0U)
	{
		remainder = left % right;
		left = right;
		right = remainder;
	}
	return (left);
}

RegenerationWorldRules::RegenerationWorldRules()
	: initialized_(false), policy_(), biome_count_(0U), biome_enabled_(),
	biome_weight_adjustments_(), ore_count_(0U), ores_(),
	village_plan_count_(0U), surface_magma_pool_count_(0U),
	ruined_village_plan_count_(0U), shipwreck_plan_count_(0U),
	pyramid_plan_count_(0U), canopy_plan_count_(0U), spider_nest_plan_count_(0U),
	added_target_chunk_count_(0U),
	last_applied_sequence_(0U),
	applied_intents_()
{
}

RegenerationWorldRules::~RegenerationWorldRules()
{
	(void)this->destroy();
}

uint64_t RegenerationWorldRules::calculate_manifest_hash(
	const voxel_generation_config &base_config,
	const RegenerationWorldRulesPolicy &policy) noexcept
{
	static const char domain[] = "RegenerationWorldRulesManifestV2";
	uint64_t hash = 14695981039346656037ULL;
	uint32_t index = 0U;
	ft_byte_buffer serialized_config;
	const uint8_t *config_bytes = nullptr;
	int32_t error_code;

	if (voxel_generation_config_is_valid(base_config) != FT_TRUE
		|| base_config.biome_count > VOXEL_MAX_CUSTOM_BIOMES)
		return (0U);
	while (domain[index] != '\0')
	{
		regeneration_rules_manifest_hash_byte(&hash,
			static_cast<uint8_t>(domain[index]));
		++index;
	}
	regeneration_rules_manifest_hash_u32(&hash,
		voxel_generation_config_signature(base_config));
	error_code = serialized_config.initialize();
	if (error_code != FT_ERR_SUCCESS)
		return (0U);
	error_code = voxel_generation_config_serialize(base_config,
		serialized_config);
	if (error_code != FT_ERR_SUCCESS
		|| serialized_config.view(0U, serialized_config.size(),
			&config_bytes) != FT_ERR_SUCCESS || config_bytes == nullptr)
	{
		(void)serialized_config.destroy();
		return (0U);
	}
	regeneration_rules_manifest_hash_u64(&hash,
		serialized_config.size());
	index = 0U;
	while (index < serialized_config.size())
	{
		regeneration_rules_manifest_hash_byte(&hash, config_bytes[index]);
		++index;
	}
	if (serialized_config.destroy() != FT_ERR_SUCCESS)
		return (0U);
	regeneration_rules_manifest_hash_u32(&hash,
		RegenerationCardSession::CONTENT_VERSION);
	regeneration_rules_manifest_hash_u32(&hash,
		policy.surface_magma_pools_per_card);
	regeneration_rules_manifest_hash_u32(&hash, policy.maximum_village_plans);
	regeneration_rules_manifest_hash_u32(&hash,
		policy.maximum_surface_magma_pools);
	regeneration_rules_manifest_hash_u32(&hash,
		policy.maximum_added_target_chunks);
	regeneration_rules_manifest_hash_u32(&hash,
		policy.maximum_ore_scale_numerator);
	regeneration_rules_manifest_hash_u32(&hash,
		policy.maximum_ore_scale_denominator);
	/* Preserve the pre-delta manifest byte sequence when the optional policy is
	 * unset so existing scriptless and v1-v8 session snapshots still restore. */
	if (policy.ore_overgrowth_instability_delta > 0)
	{
		regeneration_rules_manifest_hash_u32(&hash, 0x494E5354U);
		regeneration_rules_manifest_hash_u32(&hash,
			static_cast<uint32_t>(policy.ore_overgrowth_instability_delta));
	}
	if (policy.ultimate_greed_instability_delta > 0)
	{
		regeneration_rules_manifest_hash_u32(&hash, 0x47524545U);
		regeneration_rules_manifest_hash_u32(&hash,
			static_cast<uint32_t>(policy.ultimate_greed_instability_delta));
	}
	if (policy.biome_rupture_instability_delta > 0)
	{
		regeneration_rules_manifest_hash_u32(&hash, 0x42525550U);
		regeneration_rules_manifest_hash_u32(&hash,
			static_cast<uint32_t>(policy.biome_rupture_instability_delta));
	}
	if (policy.volcano_mark_instability_delta > 0)
	{
		regeneration_rules_manifest_hash_u32(&hash, 0x564F4C43U);
		regeneration_rules_manifest_hash_u32(&hash,
			static_cast<uint32_t>(policy.volcano_mark_instability_delta));
		regeneration_rules_manifest_hash_u32(&hash,
			policy.maximum_volcano_plans);
		if (policy.volcano_footprint_chunks != 9U)
		{
			regeneration_rules_manifest_hash_u32(&hash, 0x46505457U);
			regeneration_rules_manifest_hash_u32(&hash,
				policy.volcano_footprint_chunks);
		}
	}
	if (policy.ruined_village_instability_delta > 0)
	{
		regeneration_rules_manifest_hash_u32(&hash, 0x5256494CU);
		regeneration_rules_manifest_hash_u32(&hash,
			static_cast<uint32_t>(policy.ruined_village_instability_delta));
		regeneration_rules_manifest_hash_u32(&hash,
			policy.maximum_ruined_village_plans);
	}
	if (policy.shipwreck_instability_delta > 0)
	{
		regeneration_rules_manifest_hash_u32(&hash, 0x53484B50U);
		regeneration_rules_manifest_hash_u32(&hash,
			static_cast<uint32_t>(policy.shipwreck_instability_delta));
		regeneration_rules_manifest_hash_u32(&hash,
			policy.maximum_shipwreck_plans);
	}
	if (policy.pyramid_instability_delta > 0)
	{
		regeneration_rules_manifest_hash_u32(&hash, 0x50595241U);
		regeneration_rules_manifest_hash_u32(&hash,
			static_cast<uint32_t>(policy.pyramid_instability_delta));
		regeneration_rules_manifest_hash_u32(&hash, policy.maximum_pyramid_plans);
	}
	if (policy.canopy_instability_delta > 0)
	{
		regeneration_rules_manifest_hash_u32(&hash, 0x43414E4FU);
		regeneration_rules_manifest_hash_u32(&hash,
			static_cast<uint32_t>(policy.canopy_instability_delta));
		regeneration_rules_manifest_hash_u32(&hash, policy.maximum_canopy_plans);
	}
	if (policy.spider_nest_instability_delta > 0)
	{
		regeneration_rules_manifest_hash_u32(&hash, 0x53504944U);
		regeneration_rules_manifest_hash_u32(&hash,
			static_cast<uint32_t>(policy.spider_nest_instability_delta));
		regeneration_rules_manifest_hash_u32(&hash,
			policy.maximum_spider_nest_plans);
	}
	if (policy.unstable_catalyst_instability_delta > 0)
	{
		regeneration_rules_manifest_hash_u32(&hash, 0x554E5354U);
		regeneration_rules_manifest_hash_u32(&hash,
			static_cast<uint32_t>(policy.unstable_catalyst_instability_delta));
		regeneration_rules_manifest_hash_u32(&hash,
			policy.maximum_unstable_catalyst_uses);
	}
	if (policy.empty_draw_instability_delta > 0)
	{
		regeneration_rules_manifest_hash_u32(&hash, 0x454D5054U);
		regeneration_rules_manifest_hash_u32(&hash,
			static_cast<uint32_t>(policy.empty_draw_instability_delta));
	}
	if (policy.hand_overflow_instability_delta > 0)
	{
		regeneration_rules_manifest_hash_u32(&hash, 0x4F564652U);
		regeneration_rules_manifest_hash_u32(&hash,
			static_cast<uint32_t>(policy.hand_overflow_instability_delta));
	}
	if (policy.volatile_stabilizer_risk_tier_count > 0U)
	{
		regeneration_rules_manifest_hash_u32(&hash, 0x53544142U);
		regeneration_rules_manifest_hash_u32(&hash,
			policy.volatile_stabilizer_risk_tier_count);
		index = 0U;
		while (index < policy.volatile_stabilizer_risk_tier_count
			&& index < RegenerationWorldRulesPolicy::MAXIMUM_STABILIZER_RISK_TIERS)
		{
			regeneration_rules_manifest_hash_u32(&hash,
				policy.volatile_stabilizer_overload_basis_points[index]);
			++index;
		}
		regeneration_rules_manifest_hash_u32(&hash,
			static_cast<uint32_t>(
				policy.volatile_stabilizer_success_reduction));
		regeneration_rules_manifest_hash_u32(&hash,
			static_cast<uint32_t>(
				policy.volatile_stabilizer_overload_surge));
	}
	regeneration_rules_manifest_hash_u32(&hash,
		static_cast<uint32_t>(policy.maximum_biome_weight_adjustment));
	index = 0U;
	while (index < base_config.biome_count)
	{
		regeneration_rules_manifest_hash_u32(&hash,
			static_cast<uint32_t>(policy.biome_compass_adjustments[index]));
		++index;
	}
	return (hash == 0U ? 1U : hash);
}

RegenerationCardEffectKind RegenerationWorldRules::effect_for_card(
	uint32_t card_id) noexcept
{
	if (card_id == 1U)
		return (REGENERATION_CARD_EFFECT_BIOME_EXCLUSION);
	if (card_id == 2U)
		return (REGENERATION_CARD_EFFECT_DEEP_SEAM);
	if (card_id == 3U)
		return (REGENERATION_CARD_EFFECT_VILLAGE_CHARTER);
	if (card_id == 4U)
		return (REGENERATION_CARD_EFFECT_SURFACE_MAGMA);
	if (card_id == 5U)
		return (REGENERATION_CARD_EFFECT_DOUBLE_DRAFT);
	if (card_id == 6U)
		return (REGENERATION_CARD_EFFECT_BIOME_COMPASS);
	if (card_id == 7U)
		return (REGENERATION_CARD_EFFECT_SURVEYORS_REACH);
	if (card_id == 11U)
		return (REGENERATION_CARD_EFFECT_ORE_OVERGROWTH);
	if (card_id == RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID)
		return (REGENERATION_CARD_EFFECT_ULTIMATE_GREED);
	if (card_id == RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID)
		return (REGENERATION_CARD_EFFECT_BIOME_RUPTURE);
	if (card_id == RegenerationCardSession::FORTUNES_DRAW_CARD_DEFINITION_ID)
		return (REGENERATION_CARD_EFFECT_FORTUNES_DRAW);
	if (card_id == RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID)
		return (REGENERATION_CARD_EFFECT_VOLCANO_MARK);
	if (card_id
		== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
		return (REGENERATION_CARD_EFFECT_VOLATILE_STABILIZER);
	if (card_id
		== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID)
		return (REGENERATION_CARD_EFFECT_RUINED_VILLAGE_RELIC);
	if (card_id == RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID)
		return (REGENERATION_CARD_EFFECT_SHIPWRECK_CHART);
	if (card_id == RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
		return (REGENERATION_CARD_EFFECT_UNSTABLE_CATALYST);
	if (card_id == RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID)
		return (REGENERATION_CARD_EFFECT_DESERT_PYRAMID_SEAL);
	if (card_id == RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID)
		return (REGENERATION_CARD_EFFECT_ANCIENT_CANOPY_SEAL);
	if (card_id == RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID)
		return (REGENERATION_CARD_EFFECT_SPIDER_NEST_SEAL);
	return (static_cast<RegenerationCardEffectKind>(0));
}

bool RegenerationWorldRules::intents_equal(
	const RegenerationPolicyIntent &left,
	const RegenerationPolicyIntent &right) noexcept
{
	return (left.request_id == right.request_id
		&& left.host_intent_sequence == right.host_intent_sequence
		&& left.content_version == right.content_version
		&& left.card_definition_id == right.card_definition_id
		&& left.card_instance_id == right.card_instance_id
		&& left.effect_kind == right.effect_kind
		&& left.choice.kind == right.choice.kind
		&& left.choice.value_primary == right.choice.value_primary
		&& left.choice.value_secondary == right.choice.value_secondary
		&& left.no_effect_choice_mask == right.no_effect_choice_mask
		&& left.private_result == right.private_result
		&& left.private_ordinal == right.private_ordinal);
}

int32_t RegenerationWorldRules::initialize(
	const voxel_generation_config &base_config,
	const RegenerationWorldRulesPolicy &policy) noexcept
{
	uint32_t index;

	if (this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (voxel_generation_config_is_valid(base_config) != FT_TRUE
		|| policy.generation_manifest_hash == 0U
		|| policy.generation_manifest_hash
			!= RegenerationWorldRules::calculate_manifest_hash(base_config,
				policy)
		|| policy.surface_magma_pools_per_card == 0U
		|| policy.surface_magma_pools_per_card
			> policy.maximum_surface_magma_pools
		|| policy.maximum_village_plans == 0U
		|| policy.maximum_surface_magma_pools == 0U
		|| policy.maximum_added_target_chunks == 0U
		|| policy.maximum_ore_scale_numerator == 0U
		|| policy.maximum_ore_scale_denominator == 0U
		|| policy.ore_overgrowth_instability_delta < 0
		|| policy.ultimate_greed_instability_delta < 0
		|| policy.biome_rupture_instability_delta < 0
		|| policy.volcano_mark_instability_delta < 0
		|| policy.ruined_village_instability_delta < 0
		|| policy.shipwreck_instability_delta < 0
		|| policy.pyramid_instability_delta < 0
		|| policy.canopy_instability_delta < 0
		|| policy.spider_nest_instability_delta < 0
		|| policy.unstable_catalyst_instability_delta < 0
		|| policy.empty_draw_instability_delta < 0
		|| policy.hand_overflow_instability_delta < 0
		|| policy.volatile_stabilizer_risk_tier_count
			> RegenerationWorldRulesPolicy::MAXIMUM_STABILIZER_RISK_TIERS
		|| ((policy.volatile_stabilizer_risk_tier_count == 0U)
			!= (policy.volatile_stabilizer_success_reduction == 0
				&& policy.volatile_stabilizer_overload_surge == 0))
		|| (policy.volatile_stabilizer_risk_tier_count > 0U
			&& (policy.volatile_stabilizer_success_reduction <= 0
				|| policy.volatile_stabilizer_overload_surge <= 0))
		|| (policy.volcano_mark_instability_delta > 0
			&& (policy.maximum_volcano_plans == 0U
				|| policy.volcano_footprint_chunks < 9U
				|| policy.volcano_footprint_chunks > 16U))
		|| (policy.ruined_village_instability_delta > 0
			&& policy.maximum_ruined_village_plans == 0U)
		|| (policy.shipwreck_instability_delta > 0
			&& policy.maximum_shipwreck_plans == 0U)
		|| (policy.pyramid_instability_delta > 0
			&& policy.maximum_pyramid_plans == 0U)
		|| (policy.canopy_instability_delta > 0
			&& policy.maximum_canopy_plans == 0U)
		|| (policy.spider_nest_instability_delta > 0
			&& policy.maximum_spider_nest_plans == 0U)
		|| (policy.unstable_catalyst_instability_delta > 0
			&& policy.maximum_unstable_catalyst_uses == 0U)
		|| policy.maximum_ore_scale_numerator <
			policy.maximum_ore_scale_denominator
		|| policy.maximum_ore_scale_numerator > 1000000U
		|| policy.maximum_ore_scale_denominator > 1000000U
		|| policy.maximum_biome_weight_adjustment <= 0
		|| base_config.biome_count > VOXEL_MAX_CUSTOM_BIOMES
		|| base_config.ore_rule_count > VOXEL_MAX_ORE_RULES)
		return (FT_ERR_CONFIGURATION);
	index = 0U;
	while (index < policy.volatile_stabilizer_risk_tier_count)
	{
		if (policy.volatile_stabilizer_overload_basis_points[index] > 10000U
			|| (index > 0U && policy.volatile_stabilizer_overload_basis_points[index]
				< policy.volatile_stabilizer_overload_basis_points[index - 1U]))
			return (FT_ERR_CONFIGURATION);
		++index;
	}
	index = 0U;
	while (index < base_config.biome_count)
	{
		if (policy.biome_compass_adjustments[index]
			> policy.maximum_biome_weight_adjustment
			|| policy.biome_compass_adjustments[index]
			< -policy.maximum_biome_weight_adjustment)
			return (FT_ERR_CONFIGURATION);
		index += 1U;
	}
	index = 0U;
	while (index < base_config.ore_rule_count)
	{
		if (base_config.ores[index].enabled == FT_TRUE)
		{
			if (base_config.ores[index].block_id == 0U)
				return (FT_ERR_CONFIGURATION);
		}
		index += 1U;
	}
	this->policy_ = policy;
	this->biome_count_ = base_config.biome_count;
	index = 0U;
	while (index < VOXEL_MAX_CUSTOM_BIOMES)
	{
		this->biome_enabled_[index] = index < this->biome_count_;
		this->biome_weight_adjustments_[index] = 0;
		index += 1U;
	}
	this->ore_count_ = 0U;
	index = 0U;
	while (index < base_config.ore_rule_count)
	{
		if (base_config.ores[index].enabled == FT_TRUE)
		{
			uint32_t prior_index = 0U;
			while (prior_index < this->ore_count_
				&& this->ores_[prior_index].block_id
					!= base_config.ores[index].block_id)
				++prior_index;
			if (prior_index == this->ore_count_)
			{
				this->ores_[this->ore_count_].block_id =
					base_config.ores[index].block_id;
				this->ores_[this->ore_count_].numerator = 1U;
				this->ores_[this->ore_count_].denominator = 1U;
				this->ore_count_ += 1U;
			}
		}
		index += 1U;
	}
	this->village_plan_count_ = 0U;
	this->surface_magma_pool_count_ = 0U;
	this->volcano_plan_count_ = 0U;
	this->ruined_village_plan_count_ = 0U;
	this->shipwreck_plan_count_ = 0U;
	this->pyramid_plan_count_ = 0U;
	this->canopy_plan_count_ = 0U;
	this->spider_nest_plan_count_ = 0U;
	this->added_target_chunk_count_ = 0U;
	this->last_applied_sequence_ = 0U;
	this->applied_intents_.clear();
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationWorldRules::destroy() noexcept
{
	this->initialized_ = false;
	this->policy_ = {};
	this->biome_count_ = 0U;
	this->ore_count_ = 0U;
	this->village_plan_count_ = 0U;
	this->surface_magma_pool_count_ = 0U;
	this->volcano_plan_count_ = 0U;
	this->ruined_village_plan_count_ = 0U;
	this->shipwreck_plan_count_ = 0U;
	this->pyramid_plan_count_ = 0U;
	this->canopy_plan_count_ = 0U;
	this->spider_nest_plan_count_ = 0U;
	this->added_target_chunk_count_ = 0U;
	this->last_applied_sequence_ = 0U;
	this->applied_intents_.clear();
	return (FT_ERR_SUCCESS);
}

bool RegenerationWorldRules::is_initialized() const noexcept
{
	return (this->initialized_);
}

int32_t RegenerationWorldRules::serialize(
	std::vector<uint8_t> &bytes_out) const noexcept
{
	std::vector<uint8_t> staged;
	std::map<uint64_t, RegenerationPolicyIntent>::const_iterator intent;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (this->applied_intents_.size() > MAXIMUM_APPLIED_INTENTS)
		return (FT_ERR_OUT_OF_RANGE);
	try
	{
		staged.reserve(28U + this->applied_intents_.size() * 52U);
		staged.push_back('R');
		staged.push_back('W');
		staged.push_back('R');
		staged.push_back('S');
		regeneration_rules_append_u32(staged, 3U);
		regeneration_rules_append_u64(staged,
			this->policy_.generation_manifest_hash);
		regeneration_rules_append_u32(staged,
			static_cast<uint32_t>(this->applied_intents_.size()));
		for (intent = this->applied_intents_.begin();
			intent != this->applied_intents_.end(); ++intent)
		{
			const RegenerationPolicyIntent &value = intent->second;
			regeneration_rules_append_u64(staged, value.request_id);
			regeneration_rules_append_u64(staged,
				value.host_intent_sequence);
			regeneration_rules_append_u32(staged, value.content_version);
			regeneration_rules_append_u32(staged,
				value.card_definition_id);
			regeneration_rules_append_u32(staged, value.card_instance_id);
			regeneration_rules_append_u32(staged,
				static_cast<uint32_t>(value.effect_kind));
			regeneration_rules_append_u32(staged,
				static_cast<uint32_t>(value.choice.kind));
			regeneration_rules_append_u32(staged,
				value.choice.value_primary);
			regeneration_rules_append_u32(staged,
				value.choice.value_secondary);
			regeneration_rules_append_u32(staged, value.private_result);
			regeneration_rules_append_u32(staged, value.private_ordinal);
		}
		regeneration_rules_append_u64(staged,
			regeneration_rules_checksum(staged.data(), staged.size()));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	bytes_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationWorldRules::save_file(const char *file_path) const noexcept
{
	std::vector<uint8_t> bytes;
	int32_t error_code;

	if (file_path == nullptr || file_path[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = this->serialize(bytes);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (file_replace_safe(file_path,
		reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}

int32_t RegenerationWorldRules::deserialize(const uint8_t *bytes,
	size_t byte_count, const voxel_generation_config &base_config,
	const RegenerationWorldRulesPolicy &policy) noexcept
{
	RegenerationWorldRules staged;
	uint32_t intent_count;
	uint32_t version;
	size_t expected_size;
	size_t offset;
	uint32_t index;
	int32_t error_code;

	if (bytes == nullptr || byte_count < 28U)
		return (FT_ERR_INVALID_ARGUMENT);
	version = regeneration_rules_read_u32(bytes, 4U);
	if (bytes[0] != 'R' || bytes[1] != 'W' || bytes[2] != 'R'
		|| bytes[3] != 'S' || (version != 2U && version != 3U))
		return (FT_ERR_INVALID_ARGUMENT);
	intent_count = regeneration_rules_read_u32(bytes, 16U);
	if (intent_count > MAXIMUM_APPLIED_INTENTS
		|| intent_count > (std::numeric_limits<size_t>::max() - 28U)
			/ (version == 2U ? 44U : 52U))
		return (FT_ERR_OUT_OF_RANGE);
	expected_size = 28U + static_cast<size_t>(intent_count)
		* (version == 2U ? 44U : 52U);
	if (byte_count != expected_size
		|| regeneration_rules_checksum(bytes, byte_count - 8U)
			!= regeneration_rules_read_u64(bytes, byte_count - 8U))
		return (FT_ERR_INVALID_ARGUMENT);
	if (policy.generation_manifest_hash == 0U
		|| regeneration_rules_read_u64(bytes, 8U)
			!= policy.generation_manifest_hash)
		return (FT_ERR_INVALID_STATE);
	error_code = staged.initialize(base_config, policy);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	offset = 20U;
	index = 0U;
	while (index < intent_count && error_code == FT_ERR_SUCCESS)
	{
		RegenerationPolicyIntent intent = {};
		bool replayed = false;
		intent.request_id = regeneration_rules_read_u64(bytes, offset);
		intent.host_intent_sequence = regeneration_rules_read_u64(bytes,
			offset + 8U);
		intent.content_version = regeneration_rules_read_u32(bytes,
			offset + 16U);
		intent.card_definition_id = regeneration_rules_read_u32(bytes,
			offset + 20U);
		intent.card_instance_id = regeneration_rules_read_u32(bytes,
			offset + 24U);
		intent.effect_kind = static_cast<RegenerationCardEffectKind>(
			regeneration_rules_read_u32(bytes, offset + 28U));
		intent.choice.kind = static_cast<RegenerationCardChoiceKind>(
			regeneration_rules_read_u32(bytes, offset + 32U));
		intent.choice.value_primary = regeneration_rules_read_u32(bytes,
			offset + 36U);
		intent.choice.value_secondary = regeneration_rules_read_u32(bytes,
			offset + 40U);
		if (version >= 3U)
		{
			intent.private_result = regeneration_rules_read_u32(bytes,
				offset + 44U);
			intent.private_ordinal = regeneration_rules_read_u32(bytes,
				offset + 48U);
		}
		intent.replayed = false;
		error_code = staged.apply_intent(intent, &replayed);
		offset += version == 2U ? 44U : 52U;
		++index;
	}
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	(void)this->destroy();
	this->initialized_ = staged.initialized_;
	this->policy_ = staged.policy_;
	this->biome_count_ = staged.biome_count_;
	std::copy(staged.biome_enabled_, staged.biome_enabled_ +
		VOXEL_MAX_CUSTOM_BIOMES, this->biome_enabled_);
	std::copy(staged.biome_weight_adjustments_,
		staged.biome_weight_adjustments_ + VOXEL_MAX_CUSTOM_BIOMES,
		this->biome_weight_adjustments_);
	this->ore_count_ = staged.ore_count_;
	std::copy(staged.ores_, staged.ores_ + VOXEL_MAX_ORE_RULES, this->ores_);
	this->village_plan_count_ = staged.village_plan_count_;
	this->surface_magma_pool_count_ = staged.surface_magma_pool_count_;
	this->volcano_plan_count_ = staged.volcano_plan_count_;
	this->ruined_village_plan_count_ = staged.ruined_village_plan_count_;
	this->shipwreck_plan_count_ = staged.shipwreck_plan_count_;
	this->pyramid_plan_count_ = staged.pyramid_plan_count_;
	this->canopy_plan_count_ = staged.canopy_plan_count_;
	this->spider_nest_plan_count_ = staged.spider_nest_plan_count_;
	this->added_target_chunk_count_ = staged.added_target_chunk_count_;
	this->last_applied_sequence_ = staged.last_applied_sequence_;
	this->applied_intents_.swap(staged.applied_intents_);
	staged.initialized_ = false;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationWorldRules::load_file(const char *file_path,
	const voxel_generation_config &base_config,
	const RegenerationWorldRulesPolicy &policy) noexcept
{
	static const size_t MAXIMUM_FILE_SIZE = 28U
		+ static_cast<size_t>(MAXIMUM_APPLIED_INTENTS) * 52U;
	std::vector<uint8_t> bytes;
	FILE *file;
	long file_size;
	size_t bytes_read;
	int32_t error_code;

	if (file_path == nullptr || file_path[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	file = std::fopen(file_path, "rb");
	if (file == nullptr)
		return (FT_ERR_IO);
	if (std::fseek(file, 0L, SEEK_END) != 0)
	{
		(void)std::fclose(file);
		return (FT_ERR_IO);
	}
	file_size = std::ftell(file);
	if (file_size < 28L
		|| static_cast<unsigned long long>(file_size) > MAXIMUM_FILE_SIZE
		|| std::fseek(file, 0L, SEEK_SET) != 0)
	{
		(void)std::fclose(file);
		return (file_size >= 0L
			&& static_cast<unsigned long long>(file_size) > MAXIMUM_FILE_SIZE
			? FT_ERR_OUT_OF_RANGE : FT_ERR_INVALID_ARGUMENT);
	}
	try
	{
		bytes.resize(static_cast<size_t>(file_size));
	}
	catch (const std::bad_alloc &)
	{
		(void)std::fclose(file);
		return (FT_ERR_NO_MEMORY);
	}
	bytes_read = std::fread(bytes.data(), 1U, bytes.size(), file);
	if (bytes_read != bytes.size() || std::ferror(file) != 0)
		error_code = FT_ERR_IO;
	else
		error_code = FT_ERR_SUCCESS;
	if (std::fclose(file) != 0 && error_code == FT_ERR_SUCCESS)
		error_code = FT_ERR_IO;
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (this->deserialize(bytes.data(), bytes.size(), base_config,
		policy));
}

int32_t RegenerationWorldRules::consume_pending_card_intent(
	RegenerationCardSession &session, bool *replayed_out) noexcept
{
	RegenerationPolicyIntent intent = {};
	int32_t error_code;
	bool was_applied;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (replayed_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = session.peek_pending_card_intent(&intent);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = this->apply_intent(intent, &was_applied);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = session.acknowledge_intent(intent.host_intent_sequence);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	*replayed_out = was_applied;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationWorldRules::apply_ore_scales(
	voxel_generation_config &generation_config) const noexcept
{
	uint32_t index = 0U;
	uint32_t scaled_minimums[VOXEL_MAX_ORE_RULES] = {};
	uint32_t scaled_maximums[VOXEL_MAX_ORE_RULES] = {};

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (voxel_generation_config_is_valid(generation_config) != FT_TRUE)
		return (FT_ERR_CONFIGURATION);
	while (index < generation_config.ore_rule_count)
	{
		voxel_ore_rule &ore = generation_config.ores[index];
		if (ore.enabled == FT_TRUE)
		{
			uint32_t numerator;
			uint32_t denominator;
			uint64_t scaled_minimum;
			uint64_t scaled_maximum;
			int32_t error_code;

			error_code = this->get_ore_scale(ore.block_id, &numerator,
				&denominator);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			scaled_minimum = (static_cast<uint64_t>(ore.veins_per_chunk_min)
				* numerator + denominator - 1U) / denominator;
			scaled_maximum = (static_cast<uint64_t>(ore.veins_per_chunk_max)
				* numerator + denominator - 1U) / denominator;
			if (scaled_minimum > std::numeric_limits<uint32_t>::max()
				|| scaled_maximum > std::numeric_limits<uint32_t>::max())
				return (FT_ERR_OUT_OF_RANGE);
			scaled_minimums[index] = static_cast<uint32_t>(scaled_minimum);
			scaled_maximums[index] = static_cast<uint32_t>(scaled_maximum);
		}
		++index;
	}
	index = 0U;
	while (index < generation_config.ore_rule_count)
	{
		if (generation_config.ores[index].enabled == FT_TRUE)
		{
			const int32_t error_code = generation_config.ores[index]
				.set_frequency_range(scaled_minimums[index],
					scaled_maximums[index]);

			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
		}
		++index;
	}
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationWorldRules::validate_intent(
	const RegenerationPolicyIntent &intent) const noexcept
{
	if (intent.request_id == 0U || intent.host_intent_sequence == 0U
		|| intent.content_version != RegenerationCardSession::CONTENT_VERSION
		|| intent.card_instance_id == 0U
		|| intent.card_definition_id == 0U
		|| intent.no_effect_choice_mask != 0U
		|| intent.effect_kind
		!= RegenerationWorldRules::effect_for_card(intent.card_definition_id))
		return (FT_ERR_INVALID_ARGUMENT);
	if (intent.card_definition_id
		== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
	{
		uint32_t prior_uses = 0U;
		for (std::map<uint64_t, RegenerationPolicyIntent>::const_iterator it =
			this->applied_intents_.begin(); it != this->applied_intents_.end(); ++it)
			if (it->second.card_definition_id
				== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
				++prior_uses;
		if (intent.choice.kind != REGENERATION_CARD_CHOICE_NONE
			|| intent.choice.value_primary != 0U
			|| intent.choice.value_secondary != 0U
			|| intent.private_result > 1U
			|| intent.private_ordinal != prior_uses + 1U
			|| this->policy_.volatile_stabilizer_risk_tier_count == 0U)
			return (FT_ERR_INVALID_ARGUMENT);
	}
	else if (intent.card_definition_id
		== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
	{
		uint32_t prior_uses = 0U;
		for (const auto &entry : this->applied_intents_)
			if (entry.second.card_definition_id
				== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
				++prior_uses;
		if (intent.choice.kind != REGENERATION_CARD_CHOICE_NONE
			|| intent.choice.value_primary != 0U
			|| intent.choice.value_secondary != 0U
			|| intent.private_result == 0U || intent.private_result > 7U
			|| intent.private_ordinal != prior_uses + 1U
			|| this->policy_.unstable_catalyst_instability_delta <= 0
			|| prior_uses >= this->policy_.maximum_unstable_catalyst_uses)
			return (FT_ERR_INVALID_ARGUMENT);
	}
	else if (intent.private_result != 0U || intent.private_ordinal != 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (intent.card_definition_id == 1U
		&& (intent.choice.kind != REGENERATION_CARD_CHOICE_BIOME_ID
			|| intent.choice.value_primary == 0U
			|| intent.choice.value_secondary != 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	if (intent.card_definition_id
		== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID
		&& (intent.choice.kind != REGENERATION_CARD_CHOICE_BIOME_ID
			|| intent.choice.value_primary == 0U
			|| intent.choice.value_secondary == 0U
			|| intent.choice.value_primary == intent.choice.value_secondary))
		return (FT_ERR_INVALID_ARGUMENT);
	if ((intent.card_definition_id == 2U || intent.card_definition_id == 11U)
		&& (intent.choice.kind != REGENERATION_CARD_CHOICE_ORE_ID
			|| intent.choice.value_primary == 0U
			|| intent.choice.value_secondary != 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	if (intent.card_definition_id == 7U
		&& intent.choice.kind != REGENERATION_CARD_CHOICE_TARGET_CHUNK)
		return (FT_ERR_INVALID_ARGUMENT);
	if (intent.card_definition_id == 7U)
	{
		for (std::map<uint64_t, RegenerationPolicyIntent>::const_iterator
			entry = this->applied_intents_.begin();
			entry != this->applied_intents_.end(); ++entry)
		{
			if (entry->second.card_definition_id == 7U
				&& entry->second.choice.value_primary
					== intent.choice.value_primary
				&& entry->second.choice.value_secondary
					== intent.choice.value_secondary)
				return (FT_ERR_ALREADY_EXISTS);
		}
	}
	if (intent.card_definition_id >= 3U && intent.card_definition_id <= 6U
		&& (intent.choice.kind != REGENERATION_CARD_CHOICE_NONE
			|| intent.choice.value_primary != 0U
			|| intent.choice.value_secondary != 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	if (intent.card_definition_id
		== RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID
		&& (intent.choice.kind != REGENERATION_CARD_CHOICE_NONE
			|| intent.choice.value_primary != 0U
			|| intent.choice.value_secondary != 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	if (intent.card_definition_id
		== RegenerationCardSession::FORTUNES_DRAW_CARD_DEFINITION_ID
		&& (intent.choice.kind != REGENERATION_CARD_CHOICE_NONE
			|| intent.choice.value_primary != 0U
			|| intent.choice.value_secondary != 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	if (intent.card_definition_id
		== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID
		&& (intent.choice.kind != REGENERATION_CARD_CHOICE_NONE
			|| intent.choice.value_primary != 0U
			|| intent.choice.value_secondary != 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	if (intent.card_definition_id
		== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID
		&& (intent.choice.kind != REGENERATION_CARD_CHOICE_NONE
			|| intent.choice.value_primary != 0U
			|| intent.choice.value_secondary != 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	if (intent.card_definition_id >= RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID
		&& intent.card_definition_id <= RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID
		&& (intent.choice.kind != REGENERATION_CARD_CHOICE_NONE
			|| intent.choice.value_primary != 0U
			|| intent.choice.value_secondary != 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationWorldRules::find_ore(uint32_t block_id,
	uint32_t *index_out) const noexcept
{
	uint32_t index;

	if (index_out == nullptr || block_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	index = 0U;
	while (index < this->ore_count_)
	{
		if (this->ores_[index].block_id == block_id)
		{
			*index_out = index;
			return (FT_ERR_SUCCESS);
		}
		index += 1U;
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t RegenerationWorldRules::preflight_card_effect(
	uint32_t card_definition_id,
	const RegenerationCardChoice &choice) const noexcept
{
	uint32_t index = 0U;
	uint32_t active_biome_count = 0U;
	uint32_t ore_index = 0U;
	uint32_t numerator;
	uint32_t denominator;
	uint32_t divisor;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (card_definition_id == 1U)
	{
		if (choice.kind != REGENERATION_CARD_CHOICE_BIOME_ID
			|| choice.value_primary == 0U || choice.value_secondary != 0U)
			return (FT_ERR_INVALID_ARGUMENT);
		if (choice.value_primary > this->biome_count_)
			return (FT_ERR_NOT_FOUND);
		while (index < this->biome_count_)
		{
			if (this->biome_enabled_[index])
				++active_biome_count;
			++index;
		}
		if (this->biome_enabled_[choice.value_primary - 1U]
			&& active_biome_count <= 1U)
			return (FT_ERR_OUT_OF_RANGE);
		return (FT_ERR_SUCCESS);
	}
	if (card_definition_id
		== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID)
	{
		uint32_t affected_biome_count = 0U;

		if (choice.kind != REGENERATION_CARD_CHOICE_BIOME_ID
			|| choice.value_primary == 0U || choice.value_secondary == 0U
			|| choice.value_primary == choice.value_secondary)
			return (FT_ERR_INVALID_ARGUMENT);
		if (choice.value_primary > this->biome_count_
			|| choice.value_secondary > this->biome_count_)
			return (FT_ERR_NOT_FOUND);
		if (this->policy_.biome_rupture_instability_delta <= 0)
			return (FT_ERR_CONFIGURATION);
		while (index < this->biome_count_)
		{
			if (this->biome_enabled_[index])
				++active_biome_count;
			++index;
		}
		if (this->biome_enabled_[choice.value_primary - 1U])
			++affected_biome_count;
		if (this->biome_enabled_[choice.value_secondary - 1U])
			++affected_biome_count;
		return (active_biome_count <= affected_biome_count
			? FT_ERR_OUT_OF_RANGE : FT_ERR_SUCCESS);
	}
	if (card_definition_id == 2U)
	{
		if (choice.kind != REGENERATION_CARD_CHOICE_ORE_ID
			|| choice.value_primary == 0U || choice.value_secondary != 0U)
			return (FT_ERR_INVALID_ARGUMENT);
		int32_t result = this->find_ore(choice.value_primary, &ore_index);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (this->ores_[ore_index].numerator > UINT32_MAX / 3U
			|| this->ores_[ore_index].denominator > UINT32_MAX / 2U)
			return (FT_ERR_FULL);
		numerator = this->ores_[ore_index].numerator * 3U;
		denominator = this->ores_[ore_index].denominator * 2U;
		divisor = regeneration_world_rules_gcd(numerator, denominator);
		numerator /= divisor;
		denominator /= divisor;
		if (static_cast<uint64_t>(numerator)
			* this->policy_.maximum_ore_scale_denominator
			> static_cast<uint64_t>(
				this->policy_.maximum_ore_scale_numerator) * denominator)
			return (FT_ERR_OUT_OF_RANGE);
		return (FT_ERR_SUCCESS);
	}
	if (card_definition_id == 11U)
	{
		if (this->policy_.ore_overgrowth_instability_delta <= 0)
			return (FT_ERR_CONFIGURATION);
		if (choice.kind != REGENERATION_CARD_CHOICE_ORE_ID
			|| choice.value_primary == 0U || choice.value_secondary != 0U)
			return (FT_ERR_INVALID_ARGUMENT);
		int32_t result = this->find_ore(choice.value_primary, &ore_index);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (this->ores_[ore_index].numerator > UINT32_MAX / 2U)
			return (FT_ERR_FULL);
		numerator = this->ores_[ore_index].numerator * 2U;
		denominator = this->ores_[ore_index].denominator;
		divisor = regeneration_world_rules_gcd(numerator, denominator);
		numerator /= divisor;
		denominator /= divisor;
		if (static_cast<uint64_t>(numerator)
			* this->policy_.maximum_ore_scale_denominator
			> static_cast<uint64_t>(
				this->policy_.maximum_ore_scale_numerator) * denominator)
			return (FT_ERR_OUT_OF_RANGE);
		return (FT_ERR_SUCCESS);
	}
	if (card_definition_id
		== RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID)
	{
		if (choice.kind != REGENERATION_CARD_CHOICE_NONE
			|| choice.value_primary != 0U || choice.value_secondary != 0U)
			return (FT_ERR_INVALID_ARGUMENT);
		return (this->policy_.ultimate_greed_instability_delta > 0
			? FT_ERR_SUCCESS : FT_ERR_CONFIGURATION);
	}
	if (card_definition_id
		== RegenerationCardSession::FORTUNES_DRAW_CARD_DEFINITION_ID)
		return (choice.kind == REGENERATION_CARD_CHOICE_NONE
			&& choice.value_primary == 0U && choice.value_secondary == 0U
			? FT_ERR_SUCCESS : FT_ERR_INVALID_ARGUMENT);
	if (card_definition_id
		== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
	{
		if (choice.kind != REGENERATION_CARD_CHOICE_NONE
			|| choice.value_primary != 0U || choice.value_secondary != 0U)
			return (FT_ERR_INVALID_ARGUMENT);
		if (this->policy_.unstable_catalyst_instability_delta <= 0
			|| this->policy_.maximum_unstable_catalyst_uses == 0U)
			return (FT_ERR_CONFIGURATION);
		uint32_t uses = 0U;
		for (const auto &entry : this->applied_intents_)
			if (entry.second.card_definition_id == card_definition_id)
				++uses;
		return (uses >= this->policy_.maximum_unstable_catalyst_uses
			? FT_ERR_FULL : FT_ERR_SUCCESS);
	}
	if (card_definition_id < 3U
		|| (card_definition_id > 7U
			&& card_definition_id
				!= RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID
			&& card_definition_id
				!= RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID
			&& card_definition_id
				!= RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID))
		return (FT_ERR_NOT_FOUND);
	if (card_definition_id == 7U)
	{
		if (choice.kind != REGENERATION_CARD_CHOICE_TARGET_CHUNK)
			return (FT_ERR_INVALID_ARGUMENT);
		for (std::map<uint64_t, RegenerationPolicyIntent>::const_iterator
			entry = this->applied_intents_.begin();
			entry != this->applied_intents_.end(); ++entry)
		{
			if (entry->second.card_definition_id == 7U
				&& entry->second.choice.value_primary == choice.value_primary
				&& entry->second.choice.value_secondary == choice.value_secondary)
				return (FT_ERR_ALREADY_EXISTS);
		}
		return (this->added_target_chunk_count_
			>= this->policy_.maximum_added_target_chunks
			? FT_ERR_FULL : FT_ERR_SUCCESS);
	}
	if (choice.kind != REGENERATION_CARD_CHOICE_NONE
		|| choice.value_primary != 0U || choice.value_secondary != 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (card_definition_id == 3U)
		return (this->village_plan_count_ >= this->policy_.maximum_village_plans
			? FT_ERR_FULL : FT_ERR_SUCCESS);
	if (card_definition_id == 4U)
		return (this->surface_magma_pool_count_
			> this->policy_.maximum_surface_magma_pools
				- this->policy_.surface_magma_pools_per_card
			? FT_ERR_FULL : FT_ERR_SUCCESS);
	if (card_definition_id
		== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID)
	{
		if (choice.kind != REGENERATION_CARD_CHOICE_NONE
			|| choice.value_primary != 0U || choice.value_secondary != 0U)
			return (FT_ERR_INVALID_ARGUMENT);
		if (this->policy_.volcano_mark_instability_delta <= 0
			|| this->policy_.maximum_volcano_plans == 0U)
			return (FT_ERR_CONFIGURATION);
		return (this->volcano_plan_count_
			>= this->policy_.maximum_volcano_plans
			? FT_ERR_FULL : FT_ERR_SUCCESS);
	}
	if (card_definition_id
		== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
	{
		if (this->policy_.volatile_stabilizer_risk_tier_count == 0U
			|| this->policy_.volatile_stabilizer_success_reduction <= 0
			|| this->policy_.volatile_stabilizer_overload_surge <= 0)
			return (FT_ERR_CONFIGURATION);
		return (FT_ERR_SUCCESS);
	}
	if (card_definition_id
		== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID)
	{
		if (this->policy_.ruined_village_instability_delta <= 0
			|| this->policy_.maximum_ruined_village_plans == 0U)
			return (FT_ERR_CONFIGURATION);
		return (this->ruined_village_plan_count_
			>= this->policy_.maximum_ruined_village_plans
			? FT_ERR_FULL : FT_ERR_SUCCESS);
	}
	if (card_definition_id
		== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID)
	{
		if (this->policy_.shipwreck_instability_delta <= 0
			|| this->policy_.maximum_shipwreck_plans == 0U)
			return (FT_ERR_CONFIGURATION);
		return (this->shipwreck_plan_count_ >= this->policy_.maximum_shipwreck_plans
			? FT_ERR_FULL : FT_ERR_SUCCESS);
	}
	if (card_definition_id == RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID
		|| card_definition_id == RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID
		|| card_definition_id == RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID)
	{
		if (choice.kind != REGENERATION_CARD_CHOICE_NONE
			|| choice.value_primary != 0U || choice.value_secondary != 0U)
			return (FT_ERR_INVALID_ARGUMENT);
		if (card_definition_id == RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID)
			return (this->policy_.pyramid_instability_delta <= 0
				|| this->policy_.maximum_pyramid_plans == 0U
				? FT_ERR_CONFIGURATION : (this->pyramid_plan_count_ >= this->policy_.maximum_pyramid_plans ? FT_ERR_FULL : FT_ERR_SUCCESS));
		if (card_definition_id == RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID)
			return (this->policy_.canopy_instability_delta <= 0
				|| this->policy_.maximum_canopy_plans == 0U
				? FT_ERR_CONFIGURATION : (this->canopy_plan_count_ >= this->policy_.maximum_canopy_plans ? FT_ERR_FULL : FT_ERR_SUCCESS));
		return (this->policy_.spider_nest_instability_delta <= 0
			|| this->policy_.maximum_spider_nest_plans == 0U
			? FT_ERR_CONFIGURATION : (this->spider_nest_plan_count_ >= this->policy_.maximum_spider_nest_plans ? FT_ERR_FULL : FT_ERR_SUCCESS));
	}
	if (card_definition_id == 5U)
		return (FT_ERR_SUCCESS);
	index = 0U;
	while (index < this->biome_count_)
	{
		if (this->biome_enabled_[index])
		{
			const int64_t adjusted =
				static_cast<int64_t>(this->biome_weight_adjustments_[index])
				+ this->policy_.biome_compass_adjustments[index];
			if (adjusted > this->policy_.maximum_biome_weight_adjustment
				|| adjusted < -this->policy_.maximum_biome_weight_adjustment)
				return (FT_ERR_OUT_OF_RANGE);
		}
		++index;
	}
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationWorldRules::query_legal_card_choices(
	uint32_t card_definition_id,
	const std::vector<RegenerationTargetChunk> &chunk_candidates,
	std::vector<RegenerationCardChoice> &choices_out) const noexcept
{
	std::vector<RegenerationCardChoice> staged;
	RegenerationCardChoice choice = {};
	uint32_t first = 0U;
	uint32_t second = 0U;
	int32_t result = FT_ERR_SUCCESS;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	try
	{
		if (card_definition_id == 1U)
		{
			first = 1U;
			while (first <= this->biome_count_)
			{
				choice = {REGENERATION_CARD_CHOICE_BIOME_ID, first, 0U};
				if (this->preflight_card_effect(card_definition_id, choice)
					== FT_ERR_SUCCESS)
					staged.push_back(choice);
				++first;
			}
		}
		else if (card_definition_id
			== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID)
		{
			first = 1U;
			while (first <= this->biome_count_)
			{
				second = 1U;
				while (second <= this->biome_count_)
				{
					if (first != second)
					{
						choice = {REGENERATION_CARD_CHOICE_BIOME_ID,
							first, second};
						if (this->preflight_card_effect(card_definition_id,
								choice) == FT_ERR_SUCCESS)
							staged.push_back(choice);
					}
					++second;
				}
				++first;
			}
		}
		else if (card_definition_id == 2U || card_definition_id == 11U)
		{
			first = 0U;
			while (first < this->ore_count_)
			{
				choice = {REGENERATION_CARD_CHOICE_ORE_ID,
					this->ores_[first].block_id, 0U};
				if (this->preflight_card_effect(card_definition_id, choice)
					== FT_ERR_SUCCESS)
					staged.push_back(choice);
				++first;
			}
		}
		else if (card_definition_id == 7U)
		{
			first = 0U;
			while (first < chunk_candidates.size())
			{
				choice = {REGENERATION_CARD_CHOICE_TARGET_CHUNK,
					static_cast<uint32_t>(chunk_candidates[first].chunk_x),
					static_cast<uint32_t>(chunk_candidates[first].chunk_z)};
				if (this->preflight_card_effect(card_definition_id, choice)
					== FT_ERR_SUCCESS)
					staged.push_back(choice);
				++first;
			}
		}
		else
		{
			choice = {REGENERATION_CARD_CHOICE_NONE, 0U, 0U};
			result = this->preflight_card_effect(card_definition_id, choice);
			if (result == FT_ERR_SUCCESS)
				staged.push_back(choice);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (result != FT_ERR_SUCCESS)
		return (result);
	choices_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationWorldRules::apply_intent(
	const RegenerationPolicyIntent &intent, bool *replayed_out) noexcept
{
	std::map<uint64_t, RegenerationPolicyIntent>::const_iterator cached;
	std::map<uint64_t, RegenerationPolicyIntent>::const_iterator prior;
	uint32_t index;
	uint32_t active_biome_count;
	uint32_t ore_index;
	uint32_t scale_numerator;
	uint32_t scale_denominator;
	uint32_t divisor;
	uint32_t staged_magma_count;
	uint32_t staged_volcano_count;
	uint32_t staged_ruined_village_count;
	uint32_t staged_shipwreck_count;
	uint32_t staged_pyramid_count;
	uint32_t staged_canopy_count;
	uint32_t staged_spider_nest_count;
	uint32_t staged_village_count;
	uint32_t staged_target_count;
	int32_t staged_weights[VOXEL_MAX_CUSTOM_BIOMES];
	int64_t adjusted_weight;
	int32_t error_code;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (replayed_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*replayed_out = false;
	cached = this->applied_intents_.find(intent.host_intent_sequence);
	if (cached != this->applied_intents_.end())
	{
		if (!RegenerationWorldRules::intents_equal(cached->second, intent))
			return (FT_ERR_INVALID_ARGUMENT);
		*replayed_out = true;
		return (FT_ERR_SUCCESS);
	}
	error_code = this->validate_intent(intent);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (intent.host_intent_sequence <= this->last_applied_sequence_)
		return (FT_ERR_INVALID_STATE);
	prior = this->applied_intents_.begin();
	while (prior != this->applied_intents_.end())
	{
		if (prior->second.request_id == intent.request_id)
			return (FT_ERR_INVALID_ARGUMENT);
		++prior;
	}
	if (this->applied_intents_.size()
		>= RegenerationWorldRules::MAXIMUM_APPLIED_INTENTS)
		return (FT_ERR_FULL);
	staged_village_count = this->village_plan_count_;
	staged_magma_count = this->surface_magma_pool_count_;
	staged_volcano_count = this->volcano_plan_count_;
	staged_ruined_village_count = this->ruined_village_plan_count_;
	staged_shipwreck_count = this->shipwreck_plan_count_;
	staged_pyramid_count = this->pyramid_plan_count_;
	staged_canopy_count = this->canopy_plan_count_;
	staged_spider_nest_count = this->spider_nest_plan_count_;
	staged_target_count = this->added_target_chunk_count_;
	active_biome_count = 0U;
	index = 0U;
	while (index < this->biome_count_)
	{
		staged_weights[index] = this->biome_weight_adjustments_[index];
		if (this->biome_enabled_[index])
			active_biome_count += 1U;
		++index;
	}
	scale_numerator = 0U;
	scale_denominator = 0U;
	ore_index = 0U;
	if (intent.card_definition_id == 1U)
	{
		if (intent.choice.value_primary > this->biome_count_)
			return (FT_ERR_NOT_FOUND);
		index = intent.choice.value_primary - 1U;
		if (this->biome_enabled_[index] && active_biome_count <= 1U)
			return (FT_ERR_OUT_OF_RANGE);
	}
	else if (intent.card_definition_id
		== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID)
	{
		uint32_t affected_biome_count = 0U;

		if (intent.choice.value_primary > this->biome_count_
			|| intent.choice.value_secondary > this->biome_count_)
			return (FT_ERR_NOT_FOUND);
		if (this->biome_enabled_[intent.choice.value_primary - 1U])
			++affected_biome_count;
		if (this->biome_enabled_[intent.choice.value_secondary - 1U])
			++affected_biome_count;
		if (active_biome_count <= affected_biome_count)
			return (FT_ERR_OUT_OF_RANGE);
	}
	else if (intent.card_definition_id == 2U
		|| intent.card_definition_id == 11U)
	{
		error_code = this->find_ore(intent.choice.value_primary, &ore_index);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		const uint32_t multiplier_numerator =
			intent.card_definition_id == 11U ? 2U : 3U;
		const uint32_t multiplier_denominator =
			intent.card_definition_id == 11U ? 1U : 2U;
		if (this->ores_[ore_index].numerator
			> UINT32_MAX / multiplier_numerator
			|| this->ores_[ore_index].denominator
			> UINT32_MAX / multiplier_denominator)
			return (FT_ERR_FULL);
		scale_numerator = this->ores_[ore_index].numerator
			* multiplier_numerator;
		scale_denominator = this->ores_[ore_index].denominator
			* multiplier_denominator;
		divisor = regeneration_world_rules_gcd(scale_numerator,
			scale_denominator);
		scale_numerator /= divisor;
		scale_denominator /= divisor;
		if (static_cast<uint64_t>(scale_numerator)
			* this->policy_.maximum_ore_scale_denominator
			> static_cast<uint64_t>(
				this->policy_.maximum_ore_scale_numerator)
				* scale_denominator)
			return (FT_ERR_OUT_OF_RANGE);
	}
	else if (intent.card_definition_id == 3U)
	{
		if (staged_village_count >= this->policy_.maximum_village_plans)
			return (FT_ERR_FULL);
		staged_village_count += 1U;
	}
	else if (intent.card_definition_id == 4U)
	{
		if (this->policy_.surface_magma_pools_per_card
			> this->policy_.maximum_surface_magma_pools - staged_magma_count)
			return (FT_ERR_FULL);
		staged_magma_count += this->policy_.surface_magma_pools_per_card;
	}
	else if (intent.card_definition_id
		== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID)
	{
		if (this->policy_.volcano_mark_instability_delta <= 0
			|| this->policy_.maximum_volcano_plans == 0U)
			return (FT_ERR_CONFIGURATION);
		if (staged_volcano_count >= this->policy_.maximum_volcano_plans)
			return (FT_ERR_FULL);
		staged_volcano_count += 1U;
	}
	else if (intent.card_definition_id
		== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID)
	{
		if (this->policy_.ruined_village_instability_delta <= 0
			|| this->policy_.maximum_ruined_village_plans == 0U)
			return (FT_ERR_CONFIGURATION);
		if (staged_ruined_village_count
			>= this->policy_.maximum_ruined_village_plans)
			return (FT_ERR_FULL);
		staged_ruined_village_count += 1U;
	}
	else if (intent.card_definition_id
		== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID)
	{
		if (this->policy_.shipwreck_instability_delta <= 0
			|| this->policy_.maximum_shipwreck_plans == 0U)
			return (FT_ERR_CONFIGURATION);
		if (staged_shipwreck_count >= this->policy_.maximum_shipwreck_plans)
			return (FT_ERR_FULL);
		staged_shipwreck_count += 1U;
	}
	else if (intent.card_definition_id == RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID)
	{
		if (this->policy_.pyramid_instability_delta <= 0 || staged_pyramid_count >= this->policy_.maximum_pyramid_plans)
			return (FT_ERR_FULL);
		++staged_pyramid_count;
	}
	else if (intent.card_definition_id == RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID)
	{
		if (this->policy_.canopy_instability_delta <= 0 || staged_canopy_count >= this->policy_.maximum_canopy_plans)
			return (FT_ERR_FULL);
		++staged_canopy_count;
	}
	else if (intent.card_definition_id == RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID)
	{
		if (this->policy_.spider_nest_instability_delta <= 0 || staged_spider_nest_count >= this->policy_.maximum_spider_nest_plans)
			return (FT_ERR_FULL);
		++staged_spider_nest_count;
	}
	else if (intent.card_definition_id == 6U)
	{
		index = 0U;
		while (index < this->biome_count_)
		{
			if (this->biome_enabled_[index])
			{
				adjusted_weight = static_cast<int64_t>(staged_weights[index])
					+ this->policy_.biome_compass_adjustments[index];
				if (adjusted_weight
					> this->policy_.maximum_biome_weight_adjustment
					|| adjusted_weight
					< -this->policy_.maximum_biome_weight_adjustment)
					return (FT_ERR_OUT_OF_RANGE);
				staged_weights[index] =
					static_cast<int32_t>(adjusted_weight);
			}
			++index;
		}
	}
	else if (intent.card_definition_id == 7U)
	{
		if (staged_target_count
			>= this->policy_.maximum_added_target_chunks)
			return (FT_ERR_FULL);
		staged_target_count += 1U;
	}
	try
	{
		if (!this->applied_intents_.emplace(intent.host_intent_sequence,
				intent).second)
			return (FT_ERR_ALREADY_EXISTS);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (intent.card_definition_id == 1U)
		this->biome_enabled_[intent.choice.value_primary - 1U] = false;
	else if (intent.card_definition_id
		== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID)
	{
		this->biome_enabled_[intent.choice.value_primary - 1U] = false;
		this->biome_enabled_[intent.choice.value_secondary - 1U] = false;
	}
	else if (intent.card_definition_id == 2U
		|| intent.card_definition_id == 11U)
	{
		this->ores_[ore_index].numerator = scale_numerator;
		this->ores_[ore_index].denominator = scale_denominator;
	}
	else if (intent.card_definition_id == 3U)
		this->village_plan_count_ = staged_village_count;
	else if (intent.card_definition_id == 4U)
		this->surface_magma_pool_count_ = staged_magma_count;
	else if (intent.card_definition_id
		== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID)
		this->volcano_plan_count_ = staged_volcano_count;
	else if (intent.card_definition_id
		== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID)
		this->ruined_village_plan_count_ = staged_ruined_village_count;
	else if (intent.card_definition_id
		== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID)
		this->shipwreck_plan_count_ = staged_shipwreck_count;
	else if (intent.card_definition_id == RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID)
		this->pyramid_plan_count_ = staged_pyramid_count;
	else if (intent.card_definition_id == RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID)
		this->canopy_plan_count_ = staged_canopy_count;
	else if (intent.card_definition_id == RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID)
		this->spider_nest_plan_count_ = staged_spider_nest_count;
	else if (intent.card_definition_id == 6U)
	{
		index = 0U;
		while (index < this->biome_count_)
		{
			this->biome_weight_adjustments_[index] = staged_weights[index];
			++index;
		}
	}
	else if (intent.card_definition_id == 7U)
		this->added_target_chunk_count_ = staged_target_count;
	this->last_applied_sequence_ = intent.host_intent_sequence;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationWorldRules::get_biome_state(uint32_t biome_id,
	bool *enabled_out, int32_t *weight_adjustment_out) const noexcept
{
	uint32_t index;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (enabled_out == nullptr || weight_adjustment_out == nullptr
		|| biome_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (biome_id > this->biome_count_)
		return (FT_ERR_NOT_FOUND);
	index = biome_id - 1U;
	*enabled_out = this->biome_enabled_[index];
	*weight_adjustment_out = this->biome_weight_adjustments_[index];
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationWorldRules::get_ore_scale(uint32_t ore_block_id,
	uint32_t *numerator_out, uint32_t *denominator_out) const noexcept
{
	uint32_t index;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (numerator_out == nullptr || denominator_out == nullptr
		|| ore_block_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	index = 0U;
	while (index < this->ore_count_)
	{
		if (this->ores_[index].block_id == ore_block_id)
		{
			*numerator_out = this->ores_[index].numerator;
			*denominator_out = this->ores_[index].denominator;
			return (FT_ERR_SUCCESS);
		}
		++index;
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t RegenerationWorldRules::get_plan_counts(uint32_t *village_count_out,
	uint32_t *surface_magma_pool_count_out,
	uint32_t *added_target_chunk_count_out) const noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (village_count_out == nullptr || surface_magma_pool_count_out == nullptr
		|| added_target_chunk_count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*village_count_out = this->village_plan_count_;
	*surface_magma_pool_count_out = this->surface_magma_pool_count_;
	*added_target_chunk_count_out = this->added_target_chunk_count_;
	return (FT_ERR_SUCCESS);
}

static int32_t regeneration_decode_chunk_coordinate(uint32_t encoded) noexcept
{
	if (encoded <= static_cast<uint32_t>(INT32_MAX))
		return (static_cast<int32_t>(encoded));
	return (static_cast<int32_t>(-1LL
		- static_cast<int64_t>(UINT32_MAX - encoded)));
}

int32_t RegenerationWorldRules::copy_added_target_chunks(
	std::vector<RegenerationTargetChunk> &chunks_out) const noexcept
{
	std::vector<RegenerationTargetChunk> staged;
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	try
	{
		staged.reserve(this->added_target_chunk_count_);
		for (std::map<uint64_t, RegenerationPolicyIntent>::const_iterator
			entry = this->applied_intents_.begin();
			entry != this->applied_intents_.end(); ++entry)
		{
			if (entry->second.card_definition_id != 7U)
				continue ;
			staged.push_back({regeneration_decode_chunk_coordinate(
				entry->second.choice.value_primary),
				regeneration_decode_chunk_coordinate(
				entry->second.choice.value_secondary)});
		}
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (staged.size() != this->added_target_chunk_count_)
		return (FT_ERR_INVALID_STATE);
	chunks_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationWorldRules::copy_feature_plans(
	std::vector<RegenerationWorldFeaturePlan> &plans_out) const noexcept
{
	std::vector<RegenerationWorldFeaturePlan> staged;
	const uint64_t plan_count = static_cast<uint64_t>(this->village_plan_count_)
		+ static_cast<uint64_t>(this->surface_magma_pool_count_)
		+ static_cast<uint64_t>(this->volcano_plan_count_)
		+ static_cast<uint64_t>(this->ruined_village_plan_count_)
		+ static_cast<uint64_t>(this->shipwreck_plan_count_)
		+ static_cast<uint64_t>(this->pyramid_plan_count_)
		+ static_cast<uint64_t>(this->canopy_plan_count_)
		+ static_cast<uint64_t>(this->spider_nest_plan_count_);
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (plan_count > staged.max_size())
		return (FT_ERR_OUT_OF_RANGE);
	try
	{
		staged.reserve(static_cast<std::size_t>(plan_count));
		for (std::map<uint64_t, RegenerationPolicyIntent>::const_iterator
			entry = this->applied_intents_.begin();
			entry != this->applied_intents_.end(); ++entry)
		{
			if (entry->second.card_definition_id == 3U)
				staged.push_back({REGENERATION_WORLD_FEATURE_VILLAGE,
					entry->first, 0U, 0U});
			else if (entry->second.card_definition_id == 4U)
			{
				uint32_t ordinal = 0U;
				while (ordinal < this->policy_.surface_magma_pools_per_card)
				{
					staged.push_back({
						REGENERATION_WORLD_FEATURE_SURFACE_MAGMA_POOL,
						entry->first, ordinal, 0U});
					++ordinal;
				}
			}
			else if (entry->second.card_definition_id
				== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID)
				staged.push_back({REGENERATION_WORLD_FEATURE_VOLCANO,
					entry->first, 0U,
					this->policy_.volcano_footprint_chunks});
			else if (entry->second.card_definition_id
				== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID)
				staged.push_back({REGENERATION_WORLD_FEATURE_RUINED_VILLAGE,
					entry->first, 0U, 0U});
			else if (entry->second.card_definition_id
				== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID)
				staged.push_back({REGENERATION_WORLD_FEATURE_SHIPWRECK,
					entry->first, 0U, 0U});
			else if (entry->second.card_definition_id == RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID)
				staged.push_back({REGENERATION_WORLD_FEATURE_DESERT_PYRAMID,
					entry->first, 0U, 0U});
			else if (entry->second.card_definition_id == RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID)
				staged.push_back({REGENERATION_WORLD_FEATURE_ANCIENT_CANOPY,
					entry->first, 0U, 0U});
			else if (entry->second.card_definition_id == RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID)
				staged.push_back({REGENERATION_WORLD_FEATURE_SPIDER_NEST,
					entry->first, 0U, 0U});
		}
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (staged.size() != static_cast<std::size_t>(plan_count))
		return (FT_ERR_INVALID_STATE);
	plans_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationWorldRules::get_active_biome_count(
	uint32_t *count_out) const noexcept
{
	uint32_t index;
	uint32_t count;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	count = 0U;
	index = 0U;
	while (index < this->biome_count_)
	{
		if (this->biome_enabled_[index])
			count += 1U;
		++index;
	}
	*count_out = count;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationWorldRules::get_volatile_stabilizer_use_count(
	uint32_t *count_out) const noexcept
{
	uint32_t count = 0U;
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	for (std::map<uint64_t, RegenerationPolicyIntent>::const_iterator it =
		this->applied_intents_.begin(); it != this->applied_intents_.end(); ++it)
		if (it->second.card_definition_id
			== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
			++count;
	*count_out = count;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationWorldRules::get_unstable_catalyst_use_count(
	uint32_t *count_out) const noexcept
{
	uint32_t count = 0U;
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	for (const auto &entry : this->applied_intents_)
		if (entry.second.card_definition_id
			== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
			++count;
	*count_out = count;
	return (FT_ERR_SUCCESS);
}
