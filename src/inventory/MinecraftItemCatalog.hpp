#ifndef MINECRAFT_ITEM_CATALOG_HPP
# define MINECRAFT_ITEM_CATALOG_HPP

# include "../../Libft/Modules/Game/game_data_catalog.hpp"
# include "../../Libft/Modules/Voxel/voxel_types.hpp"
# include <cstdint>
# include <vector>

struct game_inventory_snapshot;
struct game_inventory_item_snapshot;

enum MinecraftBlockAcquisitionPolicy
{
	MINECRAFT_BLOCK_ACQUISITION_NONE = 0,
	MINECRAFT_BLOCK_ACQUISITION_SELF_ITEM = 1,
	MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM = 2,
	MINECRAFT_BLOCK_ACQUISITION_CONTAINER = 3,
	MINECRAFT_BLOCK_ACQUISITION_ADMIN_ONLY = 4
};

enum MinecraftItemAcquisitionPolicy
{
	MINECRAFT_ITEM_ACQUISITION_RECIPE = 1,
	MINECRAFT_ITEM_ACQUISITION_WORLD = 2,
	MINECRAFT_ITEM_ACQUISITION_HARVEST = 3,
	MINECRAFT_ITEM_ACQUISITION_DISCOVERY = 4,
	MINECRAFT_ITEM_ACQUISITION_RESTRICTED = 5
};

enum MinecraftItemTagId : uint32_t
{
	MINECRAFT_ITEM_TAG_WOOD_LOGS = 1U,
	MINECRAFT_ITEM_TAG_WOOD_PLANKS = 2U,
	MINECRAFT_ITEM_TAG_CRYSTAL_SAMPLES = 3U,
	MINECRAFT_ITEM_TAG_ORE_SAMPLES = 4U,
	MINECRAFT_ITEM_TAG_FLOWER_PIGMENTS = 5U
};

struct MinecraftContentItemRule
{
	int32_t item_id;
	int32_t max_stack;
	MinecraftItemAcquisitionPolicy acquisition;
};

enum MinecraftEquipmentSlot : uint32_t
{
	MINECRAFT_EQUIPMENT_MAIN_HAND = 1U,
	MINECRAFT_EQUIPMENT_OFF_HAND = 2U,
	MINECRAFT_EQUIPMENT_HEAD = 3U,
	MINECRAFT_EQUIPMENT_CHEST = 4U,
	MINECRAFT_EQUIPMENT_LEGS = 5U,
	MINECRAFT_EQUIPMENT_FEET = 6U
};

enum MinecraftMaterialTier : uint32_t
{
	MINECRAFT_MATERIAL_CLOTH = 1U,
	MINECRAFT_MATERIAL_WOOD = 2U,
	MINECRAFT_MATERIAL_LEATHER = 3U,
	MINECRAFT_MATERIAL_STONE = 4U,
	MINECRAFT_MATERIAL_COPPER = 5U,
	MINECRAFT_MATERIAL_IRON = 6U,
	MINECRAFT_MATERIAL_GOLD = 7U,
	MINECRAFT_MATERIAL_DIAMOND = 8U
};

struct MinecraftEquipmentRule
{
	int32_t item_id;
	MinecraftEquipmentSlot slot;
	MinecraftMaterialTier material_tier;
	uint32_t durability;
	uint32_t attack_power;
	uint32_t armor_points;
	uint32_t mining_power;
};

enum MinecraftMiningToolClass : uint32_t
{
	MINECRAFT_MINING_TOOL_NONE = 0U,
	MINECRAFT_MINING_TOOL_PICKAXE = 1U,
	MINECRAFT_MINING_TOOL_AXE = 2U
};

enum MinecraftFluidKind : uint32_t
{
	MINECRAFT_FLUID_NONE = 0U,
	MINECRAFT_FLUID_WATER = 1U,
	MINECRAFT_FLUID_LAVA = 2U
};

struct MinecraftFluidContainerRule
{
	int32_t item_id;
	/* Fluid quantity is measured in source block cells. */
	uint32_t capacity;
	MinecraftFluidKind fluid_kind;
};

struct MinecraftItemTagRule
{
	uint32_t tag_id;
	int32_t item_id;
};

struct MinecraftBlockItemRule
{
	uint32_t block_id;
	MinecraftBlockAcquisitionPolicy policy;
	int32_t item_id;
};

class MinecraftItemCatalog
{
  public:
	static constexpr int32_t EQUIPMENT_DURABILITY_MODIFIER_ID = 0x4D445552;
	enum ItemId : int32_t
	{
		BLOCK_ITEM_ID_BASE = 100000,
		EMPTY_BUCKET_ITEM_ID = 200001,
		WATER_BUCKET_ITEM_ID = 200002,
		LAVA_BUCKET_ITEM_ID = 200003,
		MAGICAL_DUST_ITEM_ID = 200004,
		OAK_PLANKS_ITEM_ID = 210001,
		PINE_PLANKS_ITEM_ID = 210002,
		BIRCH_PLANKS_ITEM_ID = 210003,
		STICKS_ITEM_ID = 210004,
		WORKBENCH_ITEM_ID = 210005,
		FURNACE_ITEM_ID = 210006,
		CHEST_ITEM_ID = 210007,
		LADDER_ITEM_ID = 210008,
		TRAPDOOR_ITEM_ID = 210009,
		SANDSTONE_ITEM_ID = 210010,
		BOAT_ITEM_ID = 210011,
		PAPER_MILL_ITEM_ID = 210012,
		BINDER_ITEM_ID = 210013,
		PAPER_REED_FIBER_ITEM_ID = 210014,
		PAPER_SHEET_ITEM_ID = 210015,
		CARD_STOCK_ITEM_ID = 210016,
		DARK_INK_ITEM_ID = 210017,
		COLORED_INK_ITEM_ID = 210018,
		RED_PIGMENT_ITEM_ID = 210019,
		YELLOW_PIGMENT_ITEM_ID = 210020,
		ADVANCED_INK_ITEM_ID = 210021,
		IRON_BAR_ITEM_ID = 210022,
		GOLD_BAR_ITEM_ID = 210023,
		COPPER_BAR_ITEM_ID = 210024,
		DIAMOND_MATERIAL_ITEM_ID = 210025,
		EMERALD_MATERIAL_ITEM_ID = 210026,
		COAL_ITEM_ID = 210027,
		WHEAT_ITEM_ID = 210028,
		SEEDS_ITEM_ID = 210029,
		BREAD_ITEM_ID = 210030,
		APPLE_ITEM_ID = 210031,
		PEAR_ITEM_ID = 210032,
		RAW_BEEF_ITEM_ID = 210033,
		COOKED_BEEF_ITEM_ID = 210034,
		RAW_PORK_ITEM_ID = 210035,
		COOKED_PORK_ITEM_ID = 210036,
		RAW_CHICKEN_ITEM_ID = 210037,
		COOKED_CHICKEN_ITEM_ID = 210038,
		RAW_MUTTON_ITEM_ID = 210039,
		COOKED_MUTTON_ITEM_ID = 210040,
		WOOL_ITEM_ID = 210041,
		LEATHER_ITEM_ID = 210042,
		FEATHER_ITEM_ID = 210043,
		EGG_ITEM_ID = 210044,
		PLANT_FIBER_ITEM_ID = 210045,
		ROTTEN_FLESH_ITEM_ID = 210130,
		BONE_ITEM_ID = 210131,
		GUNPOWDER_ITEM_ID = 210132,
		STRING_ITEM_ID = 210133,
		SPIDER_EYE_ITEM_ID = 210134,
		WOOD_HOE_ITEM_ID = 210137,
		WOOD_PICKAXE_ITEM_ID = 210050,
		STONE_PICKAXE_ITEM_ID = 210051,
		IRON_PICKAXE_ITEM_ID = 210052,
		GOLD_PICKAXE_ITEM_ID = 210053,
		COPPER_PICKAXE_ITEM_ID = 210054,
		DIAMOND_PICKAXE_ITEM_ID = 210055,
		WOOD_AXE_ITEM_ID = 210056,
		STONE_AXE_ITEM_ID = 210057,
		IRON_AXE_ITEM_ID = 210058,
		GOLD_AXE_ITEM_ID = 210059,
		COPPER_AXE_ITEM_ID = 210060,
		DIAMOND_AXE_ITEM_ID = 210061,
		WOOD_SWORD_ITEM_ID = 210062,
		STONE_SWORD_ITEM_ID = 210063,
		IRON_SWORD_ITEM_ID = 210064,
		GOLD_SWORD_ITEM_ID = 210065,
		COPPER_SWORD_ITEM_ID = 210066,
		DIAMOND_SWORD_ITEM_ID = 210067,
		BOW_ITEM_ID = 210068,
		ARROW_ITEM_ID = 210069,
		SHIELD_ITEM_ID = 210070,
		STAFF_ITEM_ID = 210071,
		CLOTH_HELMET_ITEM_ID = 210072,
		CLOTH_CHESTPLATE_ITEM_ID = 210073,
		CLOTH_LEGGINGS_ITEM_ID = 210074,
		CLOTH_BOOTS_ITEM_ID = 210075,
		LEATHER_HELMET_ITEM_ID = 210076,
		LEATHER_CHESTPLATE_ITEM_ID = 210077,
		LEATHER_LEGGINGS_ITEM_ID = 210078,
		LEATHER_BOOTS_ITEM_ID = 210079,
		IRON_HELMET_ITEM_ID = 210080,
		IRON_CHESTPLATE_ITEM_ID = 210081,
		IRON_LEGGINGS_ITEM_ID = 210082,
		IRON_BOOTS_ITEM_ID = 210083,
		BIOME_EXCLUSION_CARD_ITEM_ID = 210101,
		DEEP_SEAM_CARD_ITEM_ID = 210102,
		VILLAGE_CHARTER_CARD_ITEM_ID = 210103,
		SURFACE_MAGMA_CARD_ITEM_ID = 210104,
		DOUBLE_DRAFT_CARD_ITEM_ID = 210105,
		BIOME_COMPASS_CARD_ITEM_ID = 210106,
		SURVEYORS_REACH_CARD_ITEM_ID = 210107,
		VOLCANO_MARK_CARD_ITEM_ID = 210108,
		ULTIMATE_GREED_CARD_ITEM_ID = 210109,
		BIOME_RUPTURE_CARD_ITEM_ID = 210110,
		ORE_OVERGROWTH_CARD_ITEM_ID = 210111,
		SHIPWRECK_CHART_CARD_ITEM_ID = 210112,
		RUINED_VILLAGE_RELIC_CARD_ITEM_ID = 210113,
		UNSTABLE_CATALYST_CARD_ITEM_ID = 210114,
		DESERT_PYRAMID_SEAL_CARD_ITEM_ID = 210115,
		ANCIENT_CANOPY_SEAL_CARD_ITEM_ID = 210116,
		SPIDER_NEST_SEAL_CARD_ITEM_ID = 210117,
		VOLATILE_STABILIZER_CARD_ITEM_ID = 210118,
		CARD_PRESS_ITEM_ID = 210119,
		WORLD_LOOM_ITEM_ID = 210120,
		FORTUNES_DRAW_CARD_ITEM_ID = 210121
	};

	MinecraftItemCatalog();
	MinecraftItemCatalog(const MinecraftItemCatalog &other) = delete;
	~MinecraftItemCatalog();
	MinecraftItemCatalog &operator=(const MinecraftItemCatalog &other) = delete;

	int32_t initialize() noexcept;
	int32_t destroy() noexcept;
	bool is_initialized() const noexcept;
	int32_t resolve_builtin_block(uint32_t block_id,
		MinecraftBlockItemRule *rule_out) const noexcept;
	int32_t register_runtime_block_rule(uint32_t block_id,
		MinecraftBlockAcquisitionPolicy policy, int32_t item_id) noexcept;
	int32_t resolve_block_item_rule(uint32_t block_id,
		MinecraftBlockItemRule *rule_out) const noexcept;
	int32_t resolve_placed_block_item(int32_t item_id,
		uint32_t *block_id_out) const noexcept;
	int32_t validate_builtin_coverage(uint32_t *rule_count_out) const noexcept;
	int32_t validate_runtime_coverage(uint32_t *rule_count_out) const noexcept;
	int32_t validate_content_coverage(uint32_t *rule_count_out) const noexcept;
	int32_t validate_equipment_coverage(uint32_t *rule_count_out) const noexcept;
	int32_t resolve_content_item_rule(int32_t item_id,
		MinecraftContentItemRule *rule_out) const noexcept;
	int32_t resolve_equipment_rule(int32_t item_id,
		MinecraftEquipmentRule *rule_out) const noexcept;
	int32_t resolve_block_mining_requirement(uint32_t block_id,
		MinecraftMiningToolClass *tool_class_out,
	uint32_t *minimum_power_out) const noexcept;
	int32_t query_equipment_durability(
		const game_inventory_item_snapshot &item,
		uint32_t *current_out, uint32_t *maximum_out) const noexcept;
	/* Creates a value-only inventory update. The caller owns the journal and
	 * checkpoint that commit the staged wear with the action using the item. */
	int32_t stage_equipment_damage(const game_inventory_snapshot &before,
		int32_t inventory_slot, uint64_t expected_revision,
		uint32_t damage, game_inventory_snapshot &after,
		bool *item_broken_out) const noexcept;
	int32_t stage_equipment_repair(const game_inventory_snapshot &before,
		int32_t inventory_slot, uint64_t expected_revision,
		int32_t material_item_id, int32_t material_quantity,
		game_inventory_snapshot &after) const noexcept;
	int32_t resolve_equipment_repair_cost(int32_t equipment_item_id,
		int32_t *material_item_id_out,
		int32_t *material_quantity_out) const noexcept;
	/* True for durable/stateful items that must remain distinguishable as
	 * physical instances even when their definition already has max_stack 1. */
	int32_t requires_physical_instance_id(int32_t item_id,
		bool *required_out) const noexcept;
	int32_t validate_inventory_instance_policy(
		const game_inventory_snapshot &snapshot) const noexcept;
	int32_t resolve_fluid_container_rule(int32_t item_id,
		MinecraftFluidContainerRule *rule_out) const noexcept;
	int32_t prepare_fluid_transfer_snapshot(
		const game_inventory_snapshot &snapshot, int32_t inventory_slot,
		uint64_t expected_revision, bool pickup_source,
		MinecraftFluidKind fluid_kind,
		game_inventory_snapshot &snapshot_out,
		uint64_t output_instance_id = 0U) const noexcept;
	const std::vector<MinecraftContentItemRule> &content_item_rules() const noexcept;
	const std::vector<MinecraftEquipmentRule> &equipment_item_rules() const noexcept;
	int32_t resolve_item_tag(uint32_t tag_id,
		std::vector<int32_t> &item_ids_out) const noexcept;
	int32_t create_item(int32_t item_id, int32_t count,
		ft_sharedptr<game_item> *item_out) const noexcept;
	const game_data_catalog &definitions() const noexcept;

  private:
	game_data_catalog definitions_;
	MinecraftBlockItemRule builtin_rules_[VOXEL_BUILTIN_BLOCK_COUNT];
	std::vector<MinecraftBlockItemRule> runtime_rules_;
	std::vector<MinecraftContentItemRule> content_rules_;
	std::vector<MinecraftEquipmentRule> equipment_rules_;
	std::vector<MinecraftItemTagRule> item_tag_rules_;
	bool initialized_;

	int32_t register_item(int32_t item_id, int32_t max_stack) noexcept;
	int32_t register_builtin_block_items() noexcept;
	int32_t register_special_items() noexcept;
	int32_t register_content_items() noexcept;
	int32_t register_equipment_rules() noexcept;
	int32_t register_item_tags() noexcept;
};

#endif
