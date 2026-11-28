#ifndef REGENERATION_WORLD_RULES_HPP
# define REGENERATION_WORLD_RULES_HPP

# include "RegenerationCardSession.hpp"
# include <vector>

struct RegenerationWorldRulesPolicy
{
	static const uint32_t MAXIMUM_STABILIZER_RISK_TIERS = 16U;
	/* Server-owned hash of the complete generation/content manifest. */
	uint64_t generation_manifest_hash;
	uint32_t surface_magma_pools_per_card;
	uint32_t maximum_village_plans;
	uint32_t maximum_surface_magma_pools;
	uint32_t maximum_volcano_plans;
	uint32_t maximum_ruined_village_plans;
	uint32_t maximum_shipwreck_plans;
	uint32_t maximum_pyramid_plans;
	uint32_t maximum_canopy_plans;
	uint32_t maximum_spider_nest_plans;
	uint32_t maximum_unstable_catalyst_uses;
	uint32_t volcano_footprint_chunks;
	uint32_t maximum_added_target_chunks;
	uint32_t maximum_ore_scale_numerator;
	uint32_t maximum_ore_scale_denominator;
	int32_t ore_overgrowth_instability_delta;
	int32_t ultimate_greed_instability_delta;
	int32_t biome_rupture_instability_delta;
	int32_t volcano_mark_instability_delta;
	int32_t ruined_village_instability_delta;
	int32_t shipwreck_instability_delta;
	int32_t pyramid_instability_delta;
	int32_t canopy_instability_delta;
	int32_t spider_nest_instability_delta;
	int32_t unstable_catalyst_instability_delta;
	int32_t empty_draw_instability_delta;
	int32_t hand_overflow_instability_delta;
	uint32_t volatile_stabilizer_risk_tier_count;
	uint32_t volatile_stabilizer_overload_basis_points[
		MAXIMUM_STABILIZER_RISK_TIERS];
	int32_t volatile_stabilizer_success_reduction;
	int32_t volatile_stabilizer_overload_surge;
	int32_t maximum_biome_weight_adjustment;
	int32_t biome_compass_adjustments[VOXEL_MAX_CUSTOM_BIOMES];
};

struct RegenerationTargetChunk
{
	int32_t chunk_x;
	int32_t chunk_z;
};

enum RegenerationWorldFeatureKind
{
	REGENERATION_WORLD_FEATURE_VILLAGE = 1,
	REGENERATION_WORLD_FEATURE_SURFACE_MAGMA_POOL = 2,
	REGENERATION_WORLD_FEATURE_VOLCANO = 3,
	REGENERATION_WORLD_FEATURE_RUINED_VILLAGE = 4,
	REGENERATION_WORLD_FEATURE_SHIPWRECK = 5,
	REGENERATION_WORLD_FEATURE_DESERT_PYRAMID = 6,
	REGENERATION_WORLD_FEATURE_ANCIENT_CANOPY = 7,
	REGENERATION_WORLD_FEATURE_SPIDER_NEST = 8,
	REGENERATION_WORLD_FEATURE_INSTABILITY_OVERLAY = 9
};

enum RegenerationPermanentModifierId
{
	REGENERATION_MODIFIER_HOSTILE_CAMP = 0x554E0001U,
	REGENERATION_MODIFIER_CURSED_MINE = 0x554E0002U,
	REGENERATION_MODIFIER_WATCHTOWER = 0x554E0003U,
	REGENERATION_MODIFIER_HOSTILE_VAULT = 0x554E0004U,
	REGENERATION_MODIFIER_STABILIZER_OVERLOAD = 0x554E0005U,
	REGENERATION_MODIFIER_HOSTILE_SHIPWRECK = 0x554E0006U,
	REGENERATION_MODIFIER_VOLCANIC_OUTPOST = 0x554E0007U,
	REGENERATION_MODIFIER_RUINED_VILLAGE = 0x554E0008U
};

struct RegenerationWorldFeaturePlan
{
	RegenerationWorldFeatureKind kind;
	uint64_t host_intent_sequence;
	uint32_t ordinal;
	uint32_t footprint_chunks;
};

class RegenerationWorldRules
{
  public:
	static const uint32_t MAXIMUM_APPLIED_INTENTS = 4096U;

	RegenerationWorldRules();
	RegenerationWorldRules(const RegenerationWorldRules &other) = delete;
	~RegenerationWorldRules();
	RegenerationWorldRules &operator=(
		const RegenerationWorldRules &other) = delete;
	static uint64_t calculate_manifest_hash(
		const voxel_generation_config &base_config,
		const RegenerationWorldRulesPolicy &policy) noexcept;

	int32_t initialize(const voxel_generation_config &base_config,
		const RegenerationWorldRulesPolicy &policy) noexcept;
	int32_t destroy() noexcept;
	bool is_initialized() const noexcept;
	int32_t serialize(std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t save_file(const char *file_path) const noexcept;
	int32_t deserialize(const uint8_t *bytes, size_t byte_count,
		const voxel_generation_config &base_config,
		const RegenerationWorldRulesPolicy &policy) noexcept;
	int32_t load_file(const char *file_path,
		const voxel_generation_config &base_config,
		const RegenerationWorldRulesPolicy &policy) noexcept;
	int32_t apply_intent(const RegenerationPolicyIntent &intent,
		bool *replayed_out) noexcept;
	/* Validate a proposed starter effect against current rules without changing
	 * policy state. Call before consuming the physical card. */
	int32_t preflight_card_effect(uint32_t card_definition_id,
		const RegenerationCardChoice &choice) const noexcept;
	/* Returns all finite, currently valid biome/ore/no-choice targets. Chunk
	 * targets are drawn only from the supplied host-authorized candidate set. */
	int32_t query_legal_card_choices(uint32_t card_definition_id,
		const std::vector<RegenerationTargetChunk> &chunk_candidates,
		std::vector<RegenerationCardChoice> &choices_out) const noexcept;
	int32_t consume_pending_card_intent(RegenerationCardSession &session,
		bool *replayed_out) noexcept;
	int32_t apply_ore_scales(voxel_generation_config &generation_config) const noexcept;
	int32_t get_biome_state(uint32_t biome_id, bool *enabled_out,
		int32_t *weight_adjustment_out) const noexcept;
	int32_t get_ore_scale(uint32_t ore_block_id, uint32_t *numerator_out,
		uint32_t *denominator_out) const noexcept;
	int32_t get_plan_counts(uint32_t *village_count_out,
		uint32_t *surface_magma_pool_count_out,
		uint32_t *added_target_chunk_count_out) const noexcept;
	int32_t copy_added_target_chunks(
		std::vector<RegenerationTargetChunk> &chunks_out) const noexcept;
	int32_t copy_feature_plans(
		std::vector<RegenerationWorldFeaturePlan> &plans_out) const noexcept;
	int32_t get_active_biome_count(uint32_t *count_out) const noexcept;
	int32_t get_volatile_stabilizer_use_count(
		uint32_t *count_out) const noexcept;
	int32_t get_unstable_catalyst_use_count(
		uint32_t *count_out) const noexcept;

  private:
	struct OreScale
	{
		uint32_t block_id;
		uint32_t numerator;
		uint32_t denominator;
	};
	bool initialized_;
	RegenerationWorldRulesPolicy policy_;
	uint32_t biome_count_;
	bool biome_enabled_[VOXEL_MAX_CUSTOM_BIOMES];
	int32_t biome_weight_adjustments_[VOXEL_MAX_CUSTOM_BIOMES];
	uint32_t ore_count_;
	OreScale ores_[VOXEL_MAX_ORE_RULES];
	uint32_t village_plan_count_;
	uint32_t surface_magma_pool_count_;
	uint32_t volcano_plan_count_;
	uint32_t ruined_village_plan_count_;
	uint32_t shipwreck_plan_count_;
	uint32_t pyramid_plan_count_;
	uint32_t canopy_plan_count_;
	uint32_t spider_nest_plan_count_;
	uint32_t added_target_chunk_count_;
	uint64_t last_applied_sequence_;
	std::map<uint64_t, RegenerationPolicyIntent> applied_intents_;

	static RegenerationCardEffectKind effect_for_card(uint32_t card_id) noexcept;
	static bool intents_equal(const RegenerationPolicyIntent &left,
		const RegenerationPolicyIntent &right) noexcept;
	int32_t validate_intent(const RegenerationPolicyIntent &intent) const noexcept;
	int32_t find_ore(uint32_t block_id, uint32_t *index_out) const noexcept;
};

#endif
