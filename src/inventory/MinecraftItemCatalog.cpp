#include "../../src/inventory/MinecraftItemCatalog.hpp"

#include "../../Libft/Modules/Voxel/voxel_api.hpp"
#include "../../Libft/Modules/Voxel/voxel_block_registry.hpp"
#include "../../Libft/Modules/Game/game_inventory.hpp"
#include <limits>
#include <new>
#include <algorithm>
#include <cstdio>
#include <utility>

MinecraftItemCatalog::MinecraftItemCatalog() : definitions_(),
	builtin_rules_(), runtime_rules_(), content_rules_(), equipment_rules_(),
	item_tag_rules_(),
	initialized_(false)
{
}

MinecraftItemCatalog::~MinecraftItemCatalog()
{
	(void)this->destroy();
}

int32_t MinecraftItemCatalog::register_item(int32_t item_id,
	int32_t max_stack) noexcept
{
	game_item_definition definition;
	game_item_definition registered_definition;
	int32_t error_code;

	error_code = definition.initialize(item_id, 0, max_stack, 1, 1, 1, 1);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->definitions_.register_item_definition(definition);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->definitions_.fetch_item_definition(item_id,
			registered_definition);
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"item-catalog: registered item read-back failed id=%d error=%d\n",
			item_id, error_code);
	(void)registered_definition.destroy();
	(void)definition.destroy();
	return (error_code);
}

int32_t MinecraftItemCatalog::register_builtin_block_items() noexcept
{
	uint32_t block_id;
	int32_t error_code;

	block_id = 0U;
	while (block_id < VOXEL_BUILTIN_BLOCK_COUNT)
	{
		MinecraftBlockItemRule &rule = this->builtin_rules_[block_id];

		rule.block_id = block_id;
		rule.policy = MINECRAFT_BLOCK_ACQUISITION_SELF_ITEM;
		rule.item_id = BLOCK_ITEM_ID_BASE + static_cast<int32_t>(block_id);
		++block_id;
	}
	this->builtin_rules_[VOXEL_GENERATOR_AIR_BLOCK].policy =
		MINECRAFT_BLOCK_ACQUISITION_NONE;
	this->builtin_rules_[VOXEL_GENERATOR_AIR_BLOCK].item_id = 0;
	this->builtin_rules_[VOXEL_GENERATOR_GRASS_BLOCK_ID].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_GRASS_BLOCK_ID].item_id =
		BLOCK_ITEM_ID_BASE + static_cast<int32_t>(VOXEL_GENERATOR_DIRT_BLOCK_ID);
	this->builtin_rules_[VOXEL_GENERATOR_WATER_BLOCK_ID].policy =
		MINECRAFT_BLOCK_ACQUISITION_CONTAINER;
	this->builtin_rules_[VOXEL_GENERATOR_WATER_BLOCK_ID].item_id =
		WATER_BUCKET_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_LAVA_BLOCK_ID].policy =
		MINECRAFT_BLOCK_ACQUISITION_CONTAINER;
	this->builtin_rules_[VOXEL_GENERATOR_LAVA_BLOCK_ID].item_id =
		LAVA_BUCKET_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_FLOWING_WATER_BLOCK_ID].policy =
		MINECRAFT_BLOCK_ACQUISITION_NONE;
	this->builtin_rules_[VOXEL_GENERATOR_FLOWING_WATER_BLOCK_ID].item_id = 0;
	this->builtin_rules_[VOXEL_GENERATOR_FLOWING_LAVA_BLOCK_ID].policy =
		MINECRAFT_BLOCK_ACQUISITION_NONE;
	this->builtin_rules_[VOXEL_GENERATOR_FLOWING_LAVA_BLOCK_ID].item_id = 0;
	this->builtin_rules_[VOXEL_GENERATOR_FURNACE_BLOCK_ID].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_FURNACE_BLOCK_ID].item_id =
		FURNACE_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_WORKBENCH_BLOCK_ID].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_WORKBENCH_BLOCK_ID].item_id =
		WORKBENCH_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_PAPER_MILL_BLOCK_ID].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_PAPER_MILL_BLOCK_ID].item_id =
		PAPER_MILL_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_CARD_PRESS_BLOCK_ID].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_CARD_PRESS_BLOCK_ID].item_id =
		CARD_PRESS_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_WHEAT_CROP_IMMATURE_BLOCK_ID].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_WHEAT_CROP_IMMATURE_BLOCK_ID].item_id =
		SEEDS_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_WHEAT_CROP_MATURE_BLOCK_ID].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_WHEAT_CROP_MATURE_BLOCK_ID].item_id =
		WHEAT_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_FARMLAND_BLOCK_ID].policy =
		MINECRAFT_BLOCK_ACQUISITION_NONE;
	this->builtin_rules_[VOXEL_GENERATOR_FARMLAND_BLOCK_ID].item_id = 0;
	this->builtin_rules_[VOXEL_GENERATOR_COAL_ORE_BLOCK].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_COAL_ORE_BLOCK].item_id =
		COAL_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_SANDSTONE_BLOCK].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_SANDSTONE_BLOCK].item_id =
		SANDSTONE_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_LADDER_BLOCK].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_LADDER_BLOCK].item_id = LADDER_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_TRAPDOOR_CLOSED_BLOCK].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_TRAPDOOR_CLOSED_BLOCK].item_id =
		TRAPDOOR_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_TRAPDOOR_OPEN_BLOCK].policy =
		MINECRAFT_BLOCK_ACQUISITION_NONE;
	this->builtin_rules_[VOXEL_GENERATOR_TRAPDOOR_OPEN_BLOCK].item_id = 0;
	const uint32_t fiber_harvest_blocks[] = {
		VOXEL_GENERATOR_TALL_GRASS_BLOCK,
		VOXEL_GENERATOR_FERN_BLOCK,
		VOXEL_GENERATOR_DEAD_BUSH_BLOCK};
	for (uint32_t index = 0U;
		index < sizeof(fiber_harvest_blocks) / sizeof(fiber_harvest_blocks[0]);
		++index)
	{
		this->builtin_rules_[fiber_harvest_blocks[index]].policy =
			MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
		this->builtin_rules_[fiber_harvest_blocks[index]].item_id =
			PLANT_FIBER_ITEM_ID;
	}
	this->builtin_rules_[VOXEL_GENERATOR_SEAGRASS_BLOCK].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_SEAGRASS_BLOCK].item_id =
		PAPER_REED_FIBER_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_PAPER_REED_BLOCK_ID].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_PAPER_REED_BLOCK_ID].item_id =
		PAPER_REED_FIBER_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID].item_id =
		PAPER_REED_FIBER_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_RED_FLOWER_BLOCK].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_RED_FLOWER_BLOCK].item_id =
		RED_PIGMENT_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_YELLOW_FLOWER_BLOCK].policy =
		MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM;
	this->builtin_rules_[VOXEL_GENERATOR_YELLOW_FLOWER_BLOCK].item_id =
		YELLOW_PIGMENT_ITEM_ID;
	this->builtin_rules_[VOXEL_GENERATOR_BEDROCK_BLOCK_ID].policy =
		MINECRAFT_BLOCK_ACQUISITION_ADMIN_ONLY;
	error_code = FT_ERR_SUCCESS;
	block_id = 0U;
	while (block_id < VOXEL_BUILTIN_BLOCK_COUNT
		&& error_code == FT_ERR_SUCCESS)
	{
		const MinecraftBlockItemRule &rule = this->builtin_rules_[block_id];

		if (rule.policy == MINECRAFT_BLOCK_ACQUISITION_SELF_ITEM
			|| rule.policy == MINECRAFT_BLOCK_ACQUISITION_ADMIN_ONLY)
			error_code = this->register_item(rule.item_id, 64);
		++block_id;
	}
	return (error_code);
}

int32_t MinecraftItemCatalog::register_special_items() noexcept
{
	const MinecraftContentItemRule acquisition_rules[] =
	{
		{EMPTY_BUCKET_ITEM_ID, 16, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{WATER_BUCKET_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_WORLD},
		{LAVA_BUCKET_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_WORLD},
		{MAGICAL_DUST_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{ROTTEN_FLESH_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_WORLD},
		{BONE_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_WORLD},
		{GUNPOWDER_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_WORLD},
		{STRING_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_WORLD},
		{SPIDER_EYE_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_WORLD}
	};
	int32_t error_code;

	error_code = this->register_item(EMPTY_BUCKET_ITEM_ID, 16);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->register_item(WATER_BUCKET_ITEM_ID, 1);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->register_item(LAVA_BUCKET_ITEM_ID, 1);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->register_item(MAGICAL_DUST_ITEM_ID, 64);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->register_item(ROTTEN_FLESH_ITEM_ID, 64);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->register_item(BONE_ITEM_ID, 64);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->register_item(GUNPOWDER_ITEM_ID, 64);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->register_item(STRING_ITEM_ID, 64);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->register_item(SPIDER_EYE_ITEM_ID, 64);
	if (error_code == FT_ERR_SUCCESS)
	{
		try
		{
			this->content_rules_.insert(this->content_rules_.end(),
				acquisition_rules,
				acquisition_rules + sizeof(acquisition_rules)
					/ sizeof(acquisition_rules[0]));
		}
		catch (const std::bad_alloc &)
		{
			error_code = FT_ERR_NO_MEMORY;
		}
	}
	return (error_code);
}

int32_t MinecraftItemCatalog::register_content_items() noexcept
{
	static const MinecraftContentItemRule definitions[] =
	{
		{OAK_PLANKS_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{PINE_PLANKS_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{BIRCH_PLANKS_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{STICKS_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{WORKBENCH_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{FURNACE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{CHEST_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{LADDER_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{TRAPDOOR_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{SANDSTONE_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{BOAT_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{PAPER_MILL_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{BINDER_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{PAPER_REED_FIBER_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{PAPER_SHEET_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{CARD_STOCK_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{DARK_INK_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{COLORED_INK_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{RED_PIGMENT_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{YELLOW_PIGMENT_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{ADVANCED_INK_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{IRON_BAR_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{GOLD_BAR_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{COPPER_BAR_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{DIAMOND_MATERIAL_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{EMERALD_MATERIAL_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{COAL_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_WORLD},
		{WHEAT_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{SEEDS_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{BREAD_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{APPLE_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{PEAR_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{RAW_BEEF_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{COOKED_BEEF_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{RAW_PORK_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{COOKED_PORK_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{RAW_CHICKEN_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{COOKED_CHICKEN_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{RAW_MUTTON_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{COOKED_MUTTON_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{WOOL_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{LEATHER_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{FEATHER_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{EGG_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{PLANT_FIBER_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_HARVEST},
		{WOOD_HOE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{WOOD_PICKAXE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{STONE_PICKAXE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{IRON_PICKAXE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{GOLD_PICKAXE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{COPPER_PICKAXE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{DIAMOND_PICKAXE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{WOOD_AXE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{STONE_AXE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{IRON_AXE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{GOLD_AXE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{COPPER_AXE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{DIAMOND_AXE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{WOOD_SWORD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{STONE_SWORD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{IRON_SWORD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{GOLD_SWORD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{COPPER_SWORD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{DIAMOND_SWORD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{BOW_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{ARROW_ITEM_ID, 64, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{SHIELD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{STAFF_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{CLOTH_HELMET_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{CLOTH_CHESTPLATE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{CLOTH_LEGGINGS_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{CLOTH_BOOTS_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{LEATHER_HELMET_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{LEATHER_CHESTPLATE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{LEATHER_LEGGINGS_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{LEATHER_BOOTS_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{IRON_HELMET_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{IRON_CHESTPLATE_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{IRON_LEGGINGS_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{IRON_BOOTS_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{BIOME_EXCLUSION_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{DEEP_SEAM_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{VILLAGE_CHARTER_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{SURFACE_MAGMA_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{DOUBLE_DRAFT_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{BIOME_COMPASS_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{SURVEYORS_REACH_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{VOLCANO_MARK_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_DISCOVERY},
		{ULTIMATE_GREED_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_DISCOVERY},
		{BIOME_RUPTURE_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_DISCOVERY},
		{ORE_OVERGROWTH_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_DISCOVERY},
		{SHIPWRECK_CHART_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_DISCOVERY},
		{RUINED_VILLAGE_RELIC_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_DISCOVERY},
		{UNSTABLE_CATALYST_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_DISCOVERY},
		{DESERT_PYRAMID_SEAL_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_DISCOVERY},
		{ANCIENT_CANOPY_SEAL_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_DISCOVERY},
		{SPIDER_NEST_SEAL_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_DISCOVERY},
		{VOLATILE_STABILIZER_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_DISCOVERY},
		{CARD_PRESS_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{WORLD_LOOM_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_RECIPE},
		{FORTUNES_DRAW_CARD_ITEM_ID, 1, MINECRAFT_ITEM_ACQUISITION_DISCOVERY}
	};
	const std::size_t definition_count = sizeof(definitions)
		/ sizeof(definitions[0]);
	std::size_t index = 0U;
	int32_t error_code = FT_ERR_SUCCESS;

	try
	{
		this->content_rules_.reserve(definition_count
			+ this->content_rules_.size());
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	while (index < definition_count && error_code == FT_ERR_SUCCESS)
	{
		error_code = this->register_item(definitions[index].item_id,
			definitions[index].max_stack);
		if (error_code == FT_ERR_SUCCESS)
			this->content_rules_.push_back(definitions[index]);
		++index;
	}
	return (error_code);
}

int32_t MinecraftItemCatalog::register_item_tags() noexcept
{
	const MinecraftItemTagRule rules[] =
	{
		{MINECRAFT_ITEM_TAG_WOOD_LOGS,
			BLOCK_ITEM_ID_BASE + VOXEL_GENERATOR_OAK_LOG_BLOCK},
		{MINECRAFT_ITEM_TAG_WOOD_LOGS,
			BLOCK_ITEM_ID_BASE + VOXEL_GENERATOR_PINE_LOG_BLOCK},
		{MINECRAFT_ITEM_TAG_WOOD_LOGS,
			BLOCK_ITEM_ID_BASE + VOXEL_GENERATOR_BIRCH_LOG_BLOCK},
		{MINECRAFT_ITEM_TAG_WOOD_PLANKS, OAK_PLANKS_ITEM_ID},
		{MINECRAFT_ITEM_TAG_WOOD_PLANKS, PINE_PLANKS_ITEM_ID},
		{MINECRAFT_ITEM_TAG_WOOD_PLANKS, BIRCH_PLANKS_ITEM_ID},
		{MINECRAFT_ITEM_TAG_CRYSTAL_SAMPLES,
			BLOCK_ITEM_ID_BASE + VOXEL_GENERATOR_QUARTZ_BLOCK},
		{MINECRAFT_ITEM_TAG_CRYSTAL_SAMPLES,
			BLOCK_ITEM_ID_BASE + VOXEL_GENERATOR_AMETHYST_BLOCK},
		{MINECRAFT_ITEM_TAG_CRYSTAL_SAMPLES,
			BLOCK_ITEM_ID_BASE + VOXEL_GENERATOR_SHIMMER_STONE_BLOCK},
		{MINECRAFT_ITEM_TAG_ORE_SAMPLES, COAL_ITEM_ID},
		{MINECRAFT_ITEM_TAG_ORE_SAMPLES,
			BLOCK_ITEM_ID_BASE + VOXEL_GENERATOR_IRON_ORE_BLOCK},
		{MINECRAFT_ITEM_TAG_ORE_SAMPLES,
			BLOCK_ITEM_ID_BASE + VOXEL_GENERATOR_GOLD_ORE_BLOCK},
		{MINECRAFT_ITEM_TAG_ORE_SAMPLES,
			BLOCK_ITEM_ID_BASE + VOXEL_GENERATOR_DIAMOND_ORE_BLOCK},
		{MINECRAFT_ITEM_TAG_ORE_SAMPLES,
			BLOCK_ITEM_ID_BASE + VOXEL_GENERATOR_EMERALD_ORE_BLOCK},
		{MINECRAFT_ITEM_TAG_ORE_SAMPLES,
			BLOCK_ITEM_ID_BASE + VOXEL_GENERATOR_COPPER_ORE_BLOCK},
		{MINECRAFT_ITEM_TAG_FLOWER_PIGMENTS, RED_PIGMENT_ITEM_ID},
		{MINECRAFT_ITEM_TAG_FLOWER_PIGMENTS, YELLOW_PIGMENT_ITEM_ID}
	};
	const std::size_t rule_count = sizeof(rules) / sizeof(rules[0]);

	try
	{
		this->item_tag_rules_.assign(rules, rules + rule_count);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::register_equipment_rules() noexcept
{
	static const MinecraftEquipmentRule rules[] = {
		{WOOD_PICKAXE_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_WOOD, 60U, 2U, 0U, 1U},
		{STONE_PICKAXE_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_STONE, 132U, 3U, 0U, 2U},
		{IRON_PICKAXE_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_IRON, 251U, 4U, 0U, 4U},
		{GOLD_PICKAXE_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_GOLD, 33U, 2U, 0U, 5U},
		{COPPER_PICKAXE_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_COPPER, 191U, 4U, 0U, 3U},
		{DIAMOND_PICKAXE_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_DIAMOND, 1562U, 5U, 0U, 5U},
		{WOOD_AXE_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_WOOD, 60U, 4U, 0U, 1U},
		{STONE_AXE_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_STONE, 132U, 5U, 0U, 2U},
		{IRON_AXE_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_IRON, 251U, 6U, 0U, 4U},
		{GOLD_AXE_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_GOLD, 33U, 4U, 0U, 5U},
		{COPPER_AXE_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_COPPER, 191U, 6U, 0U, 3U},
		{DIAMOND_AXE_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_DIAMOND, 1562U, 7U, 0U, 5U},
		{WOOD_SWORD_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_WOOD, 60U, 4U, 0U, 0U},
		{STONE_SWORD_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_STONE, 132U, 5U, 0U, 0U},
		{IRON_SWORD_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_IRON, 251U, 6U, 0U, 0U},
		{GOLD_SWORD_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_GOLD, 33U, 4U, 0U, 0U},
		{COPPER_SWORD_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_COPPER, 191U, 5U, 0U, 0U},
		{DIAMOND_SWORD_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_DIAMOND, 1562U, 7U, 0U, 0U},
		{BOW_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_WOOD, 385U, 5U, 0U, 0U},
		{SHIELD_ITEM_ID, MINECRAFT_EQUIPMENT_OFF_HAND,
			MINECRAFT_MATERIAL_WOOD, 337U, 0U, 4U, 0U},
		{STAFF_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_DIAMOND, 256U, 6U, 0U, 0U},
		{WOOD_HOE_ITEM_ID, MINECRAFT_EQUIPMENT_MAIN_HAND,
			MINECRAFT_MATERIAL_WOOD, 60U, 1U, 0U, 1U},
		{CLOTH_HELMET_ITEM_ID, MINECRAFT_EQUIPMENT_HEAD,
			MINECRAFT_MATERIAL_CLOTH, 41U, 0U, 1U, 0U},
		{CLOTH_CHESTPLATE_ITEM_ID, MINECRAFT_EQUIPMENT_CHEST,
			MINECRAFT_MATERIAL_CLOTH, 57U, 0U, 2U, 0U},
		{CLOTH_LEGGINGS_ITEM_ID, MINECRAFT_EQUIPMENT_LEGS,
			MINECRAFT_MATERIAL_CLOTH, 54U, 0U, 1U, 0U},
		{CLOTH_BOOTS_ITEM_ID, MINECRAFT_EQUIPMENT_FEET,
			MINECRAFT_MATERIAL_CLOTH, 47U, 0U, 1U, 0U},
		{LEATHER_HELMET_ITEM_ID, MINECRAFT_EQUIPMENT_HEAD,
			MINECRAFT_MATERIAL_LEATHER, 56U, 0U, 2U, 0U},
		{LEATHER_CHESTPLATE_ITEM_ID, MINECRAFT_EQUIPMENT_CHEST,
			MINECRAFT_MATERIAL_LEATHER, 81U, 0U, 4U, 0U},
		{LEATHER_LEGGINGS_ITEM_ID, MINECRAFT_EQUIPMENT_LEGS,
			MINECRAFT_MATERIAL_LEATHER, 76U, 0U, 3U, 0U},
		{LEATHER_BOOTS_ITEM_ID, MINECRAFT_EQUIPMENT_FEET,
			MINECRAFT_MATERIAL_LEATHER, 66U, 0U, 1U, 0U},
		{IRON_HELMET_ITEM_ID, MINECRAFT_EQUIPMENT_HEAD,
			MINECRAFT_MATERIAL_IRON, 166U, 0U, 4U, 0U},
		{IRON_CHESTPLATE_ITEM_ID, MINECRAFT_EQUIPMENT_CHEST,
			MINECRAFT_MATERIAL_IRON, 241U, 0U, 8U, 0U},
		{IRON_LEGGINGS_ITEM_ID, MINECRAFT_EQUIPMENT_LEGS,
			MINECRAFT_MATERIAL_IRON, 226U, 0U, 6U, 0U},
		{IRON_BOOTS_ITEM_ID, MINECRAFT_EQUIPMENT_FEET,
			MINECRAFT_MATERIAL_IRON, 196U, 0U, 2U, 0U}};
	try
	{
		this->equipment_rules_.assign(rules, rules
			+ sizeof(rules) / sizeof(rules[0]));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::initialize() noexcept
{
	uint32_t rule_count;
	int32_t error_code;

	if (this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	error_code = this->definitions_.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->register_builtin_block_items();
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "item-catalog: builtin registration error=%d\n",
			error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->register_special_items();
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "item-catalog: special item registration error=%d\n",
			error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->register_content_items();
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "item-catalog: content item registration error=%d\n",
			error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->register_equipment_rules();
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "item-catalog: equipment registration error=%d\n",
			error_code);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->register_item_tags();
	if (error_code == FT_ERR_SUCCESS)
	{
		this->initialized_ = true;
		error_code = this->validate_builtin_coverage(&rule_count);
		if (error_code != FT_ERR_SUCCESS)
			std::fprintf(stderr, "item-catalog: built-in validation error=%d\n",
				error_code);
	}
	if (error_code != FT_ERR_SUCCESS)
	{
		this->initialized_ = false;
		this->content_rules_.clear();
		this->equipment_rules_.clear();
		this->item_tag_rules_.clear();
		(void)this->definitions_.destroy();
		return (error_code);
	}
	error_code = FT_ERR_SUCCESS;
	{
		uint32_t content_rule_count = 0U;
		uint32_t equipment_rule_count = 0U;
		error_code = this->validate_content_coverage(&content_rule_count);
		if (error_code == FT_ERR_SUCCESS)
			error_code = this->validate_equipment_coverage(
				&equipment_rule_count);
	}
	if (error_code != FT_ERR_SUCCESS)
	{
		this->initialized_ = false;
		this->content_rules_.clear();
		this->equipment_rules_.clear();
		this->item_tag_rules_.clear();
		(void)this->definitions_.destroy();
		return (error_code);
	}
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::destroy() noexcept
{
	if (!this->initialized_)
		return (FT_ERR_SUCCESS);
	this->initialized_ = false;
	this->runtime_rules_.clear();
	this->content_rules_.clear();
	this->equipment_rules_.clear();
	this->item_tag_rules_.clear();
	return (this->definitions_.destroy());
}

bool MinecraftItemCatalog::is_initialized() const noexcept
{
	return (this->initialized_);
}

int32_t MinecraftItemCatalog::resolve_builtin_block(uint32_t block_id,
	MinecraftBlockItemRule *rule_out) const noexcept
{
	if (rule_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (block_id >= VOXEL_BUILTIN_BLOCK_COUNT)
		return (FT_ERR_NOT_FOUND);
	*rule_out = this->builtin_rules_[block_id];
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::register_runtime_block_rule(uint32_t block_id,
	MinecraftBlockAcquisitionPolicy policy, int32_t item_id) noexcept
{
	MinecraftBlockItemRule rule = {};
	std::vector<MinecraftBlockItemRule>::iterator existing;
	int32_t expected_item_id;
	int32_t error_code;

	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (block_id < VOXEL_BUILTIN_BLOCK_COUNT
		|| voxel_runtime_block_is_known(block_id) == FT_FALSE
		|| policy < MINECRAFT_BLOCK_ACQUISITION_NONE
		|| policy > MINECRAFT_BLOCK_ACQUISITION_ADMIN_ONLY)
		return (FT_ERR_INVALID_ARGUMENT);
	existing = std::find_if(this->runtime_rules_.begin(),
		this->runtime_rules_.end(), [block_id](const MinecraftBlockItemRule &entry)
		{
			return (entry.block_id == block_id);
		});
	if (policy == MINECRAFT_BLOCK_ACQUISITION_NONE)
	{
		if (item_id != 0)
			return (FT_ERR_INVALID_ARGUMENT);
	}
	else if (policy == MINECRAFT_BLOCK_ACQUISITION_SELF_ITEM
		|| policy == MINECRAFT_BLOCK_ACQUISITION_ADMIN_ONLY)
	{
		if (block_id > static_cast<uint32_t>(
				std::numeric_limits<int32_t>::max() - BLOCK_ITEM_ID_BASE))
			return (FT_ERR_OUT_OF_RANGE);
		expected_item_id = BLOCK_ITEM_ID_BASE
			+ static_cast<int32_t>(block_id);
		if (item_id != expected_item_id)
			return (FT_ERR_INVALID_ARGUMENT);
	}
	else if (item_id <= 0)
		return (FT_ERR_INVALID_ARGUMENT);
	if (existing != this->runtime_rules_.end())
	{
		if (existing->policy == policy && existing->item_id == item_id)
			return (FT_ERR_SUCCESS);
		return (FT_ERR_ALREADY_EXISTS);
	}
	try
	{
		this->runtime_rules_.reserve(this->runtime_rules_.size() + 1U);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (policy == MINECRAFT_BLOCK_ACQUISITION_SELF_ITEM
		|| policy == MINECRAFT_BLOCK_ACQUISITION_ADMIN_ONLY)
		error_code = this->register_item(item_id, 64);
	else if (policy == MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM
		|| policy == MINECRAFT_BLOCK_ACQUISITION_CONTAINER)
	{
		game_item_definition definition;

		error_code = this->definitions_.fetch_item_definition(item_id,
			definition);
		if (error_code == FT_ERR_SUCCESS)
			(void)definition.destroy();
	}
	else
		error_code = FT_ERR_SUCCESS;
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	rule.block_id = block_id;
	rule.policy = policy;
	rule.item_id = item_id;
	this->runtime_rules_.push_back(rule);
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::resolve_block_item_rule(uint32_t block_id,
	MinecraftBlockItemRule *rule_out) const noexcept
{
	if (rule_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (block_id < VOXEL_BUILTIN_BLOCK_COUNT)
		return (this->resolve_builtin_block(block_id, rule_out));
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (voxel_runtime_block_is_known(block_id) == FT_FALSE)
		return (FT_ERR_NOT_FOUND);
	for (const MinecraftBlockItemRule &rule : this->runtime_rules_)
	{
		if (rule.block_id == block_id)
		{
			*rule_out = rule;
			return (FT_ERR_SUCCESS);
		}
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t MinecraftItemCatalog::resolve_placed_block_item(int32_t item_id,
	uint32_t *block_id_out) const noexcept
{
	uint32_t block_id;
	MinecraftBlockItemRule rule = {};

	if (block_id_out == nullptr || item_id <= 0)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (item_id == FURNACE_ITEM_ID)
	{
		block_id = VOXEL_GENERATOR_FURNACE_BLOCK_ID;
		if (this->resolve_block_item_rule(block_id, &rule) != FT_ERR_SUCCESS
			|| rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM
			|| rule.item_id != item_id)
			return (FT_ERR_INVALID_STATE);
		*block_id_out = block_id;
		return (FT_ERR_SUCCESS);
	}
	if (item_id == SEEDS_ITEM_ID)
	{
		block_id = VOXEL_GENERATOR_WHEAT_CROP_IMMATURE_BLOCK_ID;
		if (this->resolve_block_item_rule(block_id, &rule) != FT_ERR_SUCCESS
			|| rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM
			|| rule.item_id != item_id)
			return (FT_ERR_INVALID_STATE);
		*block_id_out = block_id;
		return (FT_ERR_SUCCESS);
	}
	if (item_id == PAPER_REED_FIBER_ITEM_ID)
	{
		block_id = VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID;
		if (this->resolve_block_item_rule(block_id, &rule) != FT_ERR_SUCCESS
			|| rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM
			|| rule.item_id != item_id)
			return (FT_ERR_INVALID_STATE);
		*block_id_out = block_id;
		return (FT_ERR_SUCCESS);
	}
	if (item_id == WORKBENCH_ITEM_ID
		|| item_id == PAPER_MILL_ITEM_ID || item_id == CARD_PRESS_ITEM_ID
		|| item_id == TRAPDOOR_ITEM_ID)
	{
		block_id = item_id == WORKBENCH_ITEM_ID
			? VOXEL_GENERATOR_WORKBENCH_BLOCK_ID
			: item_id == PAPER_MILL_ITEM_ID
				? VOXEL_GENERATOR_PAPER_MILL_BLOCK_ID
				: item_id == CARD_PRESS_ITEM_ID
					? VOXEL_GENERATOR_CARD_PRESS_BLOCK_ID
					: VOXEL_GENERATOR_TRAPDOOR_CLOSED_BLOCK_ID;
		if (this->resolve_block_item_rule(block_id, &rule) != FT_ERR_SUCCESS
			|| rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM
			|| rule.item_id != item_id)
			return (FT_ERR_INVALID_STATE);
		*block_id_out = block_id;
		return (FT_ERR_SUCCESS);
	}
	if (item_id < BLOCK_ITEM_ID_BASE)
		return (FT_ERR_NOT_FOUND);
	block_id = static_cast<uint32_t>(item_id - BLOCK_ITEM_ID_BASE);
	if (this->resolve_block_item_rule(block_id, &rule) != FT_ERR_SUCCESS
		|| rule.policy != MINECRAFT_BLOCK_ACQUISITION_SELF_ITEM
		|| rule.item_id != item_id)
		return (FT_ERR_NOT_FOUND);
	*block_id_out = block_id;
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::validate_builtin_coverage(
	uint32_t *rule_count_out) const noexcept
{
	uint32_t block_id;

	if (rule_count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	block_id = 0U;
	while (block_id < VOXEL_BUILTIN_BLOCK_COUNT)
	{
		const MinecraftBlockItemRule &rule = this->builtin_rules_[block_id];
		game_item_definition item_definition;
		int32_t error_code;

		if (rule.block_id != block_id
			|| rule.policy < MINECRAFT_BLOCK_ACQUISITION_NONE
			|| rule.policy > MINECRAFT_BLOCK_ACQUISITION_ADMIN_ONLY)
			return (FT_ERR_INVALID_STATE);
		if ((rule.policy == MINECRAFT_BLOCK_ACQUISITION_NONE
				&& rule.item_id != 0)
			|| (rule.policy != MINECRAFT_BLOCK_ACQUISITION_NONE
				&& rule.item_id <= 0))
			return (FT_ERR_INVALID_STATE);
		if (rule.policy == MINECRAFT_BLOCK_ACQUISITION_SELF_ITEM
			&& rule.item_id != BLOCK_ITEM_ID_BASE
				+ static_cast<int32_t>(block_id))
			return (FT_ERR_INVALID_STATE);
		if (rule.policy == MINECRAFT_BLOCK_ACQUISITION_ADMIN_ONLY
			&& rule.item_id != BLOCK_ITEM_ID_BASE
				+ static_cast<int32_t>(block_id))
			return (FT_ERR_INVALID_STATE);
		if (rule.policy == MINECRAFT_BLOCK_ACQUISITION_CONTAINER
			&& rule.item_id != WATER_BUCKET_ITEM_ID
			&& rule.item_id != LAVA_BUCKET_ITEM_ID)
			return (FT_ERR_INVALID_STATE);
		if (rule.policy != MINECRAFT_BLOCK_ACQUISITION_NONE)
		{
			error_code = this->definitions_.fetch_item_definition(rule.item_id,
				item_definition);
			if (error_code != FT_ERR_SUCCESS
				|| item_definition.get_max_stack() <= 0)
			{
				std::fprintf(stderr,
					"item-catalog: block rule missing definition block=%u item=%d policy=%d error=%d\n",
					block_id, rule.item_id, rule.policy, error_code);
				(void)item_definition.destroy();
				return (error_code == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE
					: error_code);
			}
			(void)item_definition.destroy();
		}
		++block_id;
	}
	*rule_count_out = VOXEL_BUILTIN_BLOCK_COUNT;
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::validate_runtime_coverage(
	uint32_t *rule_count_out) const noexcept
{
	uint32_t runtime_index;
	uint32_t known_count = 0U;

	if (rule_count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	runtime_index = 0U;
	while (runtime_index < VOXEL_RUNTIME_BLOCK_CAPACITY)
	{
		const uint32_t block_id = VOXEL_BUILTIN_BLOCK_COUNT + runtime_index;

		if (voxel_runtime_block_is_known(block_id) == FT_TRUE)
		{
			MinecraftBlockItemRule rule = {};
			int32_t error_code = this->resolve_block_item_rule(block_id, &rule);

			if (error_code != FT_ERR_SUCCESS)
				return (error_code == FT_ERR_NOT_FOUND ? FT_ERR_INVALID_STATE
					: error_code);
			if (rule.policy != MINECRAFT_BLOCK_ACQUISITION_NONE)
			{
				game_item_definition definition;

				error_code = this->definitions_.fetch_item_definition(
					rule.item_id, definition);
				if (error_code != FT_ERR_SUCCESS)
					return (FT_ERR_INVALID_STATE);
				if (definition.get_max_stack() <= 0)
				{
					(void)definition.destroy();
					return (FT_ERR_INVALID_STATE);
				}
				(void)definition.destroy();
			}
			++known_count;
		}
		++runtime_index;
	}
	*rule_count_out = known_count;
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::validate_content_coverage(
	uint32_t *rule_count_out) const noexcept
{
	std::size_t index = 0U;
	uint32_t tag_id;
	uint64_t policy_item_count;

	if (rule_count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	policy_item_count = 0U;
	while (index < this->content_rules_.size())
	{
		const MinecraftContentItemRule &rule = this->content_rules_[index];
		game_item_definition definition;
		MinecraftContentItemRule resolved_rule = {};
		std::size_t previous_index = 0U;
		int32_t error_code;

		if (rule.item_id <= 0 || rule.max_stack <= 0
			|| rule.acquisition < MINECRAFT_ITEM_ACQUISITION_RECIPE
			|| rule.acquisition > MINECRAFT_ITEM_ACQUISITION_RESTRICTED)
		{
			std::fprintf(stderr,
				"item-catalog: invalid content rule index=%zu item=%d stack=%d acquisition=%d\n",
				index, rule.item_id, rule.max_stack, rule.acquisition);
			return (FT_ERR_INVALID_STATE);
		}
		if (rule.item_id >= BLOCK_ITEM_ID_BASE
			&& static_cast<uint64_t>(rule.item_id - BLOCK_ITEM_ID_BASE)
				< static_cast<uint64_t>(VOXEL_BUILTIN_BLOCK_COUNT)
					+ VOXEL_RUNTIME_BLOCK_CAPACITY)
			return (FT_ERR_ALREADY_EXISTS);
		error_code = this->definitions_.fetch_item_definition(rule.item_id,
			definition);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (definition.get_max_stack() != rule.max_stack)
		{
			std::fprintf(stderr,
				"item-catalog: content stack mismatch index=%zu item=%d expected=%d actual=%d\n",
				index, rule.item_id, rule.max_stack,
				definition.get_max_stack());
			(void)definition.destroy();
			return (FT_ERR_INVALID_STATE);
		}
		(void)definition.destroy();
		if (this->resolve_content_item_rule(rule.item_id,
				&resolved_rule) != FT_ERR_SUCCESS
			|| resolved_rule.max_stack != rule.max_stack
			|| resolved_rule.acquisition != rule.acquisition)
		{
			std::fprintf(stderr,
				"item-catalog: content lookup mismatch index=%zu item=%d\n",
				index, rule.item_id);
			return (FT_ERR_INVALID_STATE);
		}
		while (previous_index < index)
		{
			if (this->content_rules_[previous_index].item_id == rule.item_id)
				return (FT_ERR_ALREADY_EXISTS);
			++previous_index;
		}
		++policy_item_count;
		++index;
	}
	index = 0U;
	while (index < VOXEL_BUILTIN_BLOCK_COUNT)
	{
		const MinecraftBlockItemRule &rule = this->builtin_rules_[index];
		if (rule.policy == MINECRAFT_BLOCK_ACQUISITION_SELF_ITEM
			|| rule.policy == MINECRAFT_BLOCK_ACQUISITION_ADMIN_ONLY)
			++policy_item_count;
		++index;
	}
	index = 0U;
	while (index < this->runtime_rules_.size())
	{
		const MinecraftBlockItemRule &rule = this->runtime_rules_[index];
		if (rule.policy == MINECRAFT_BLOCK_ACQUISITION_SELF_ITEM
			|| rule.policy == MINECRAFT_BLOCK_ACQUISITION_ADMIN_ONLY)
			++policy_item_count;
		++index;
	}
	index = 0U;
	while (index < this->item_tag_rules_.size())
	{
		const MinecraftItemTagRule &tag_rule = this->item_tag_rules_[index];
		game_item_definition definition;
		std::size_t prior_index = 0U;
		int32_t error_code;

		if (tag_rule.tag_id == 0U || tag_rule.item_id <= 0)
			{
				std::fprintf(stderr,
					"item-catalog: invalid item tag index=%zu tag=%u item=%d\n",
					index, tag_rule.tag_id, tag_rule.item_id);
			return (FT_ERR_INVALID_STATE);
			}
		error_code = this->definitions_.fetch_item_definition(
			tag_rule.item_id, definition);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		(void)definition.destroy();
		while (prior_index < index)
		{
			if (this->item_tag_rules_[prior_index].tag_id == tag_rule.tag_id
				&& this->item_tag_rules_[prior_index].item_id == tag_rule.item_id)
				return (FT_ERR_ALREADY_EXISTS);
			++prior_index;
		}
		++index;
	}
	tag_id = MINECRAFT_ITEM_TAG_WOOD_LOGS;
	while (tag_id <= MINECRAFT_ITEM_TAG_FLOWER_PIGMENTS)
	{
		bool found = false;

		index = 0U;
		while (index < this->item_tag_rules_.size())
		{
			if (this->item_tag_rules_[index].tag_id == tag_id)
			{
				found = true;
				break ;
			}
			++index;
		}
		if (!found)
			return (FT_ERR_NOT_FOUND);
		++tag_id;
	}
	{
		const MinecraftFluidContainerRule expected[] =
		{
			{EMPTY_BUCKET_ITEM_ID, 1U, MINECRAFT_FLUID_NONE},
			{WATER_BUCKET_ITEM_ID, 1U, MINECRAFT_FLUID_WATER},
			{LAVA_BUCKET_ITEM_ID, 1U, MINECRAFT_FLUID_LAVA}
		};
		std::size_t container_index = 0U;

		while (container_index < sizeof(expected) / sizeof(expected[0]))
		{
			MinecraftFluidContainerRule resolved = {};
			MinecraftContentItemRule item_rule = {};
			int32_t error_code = this->resolve_fluid_container_rule(
				expected[container_index].item_id, &resolved);

			if (error_code == FT_ERR_SUCCESS)
				error_code = this->resolve_content_item_rule(
					expected[container_index].item_id, &item_rule);
			if (error_code != FT_ERR_SUCCESS
				|| resolved.capacity != expected[container_index].capacity
				|| resolved.fluid_kind != expected[container_index].fluid_kind
				|| (container_index == 0U && item_rule.max_stack != 16)
				|| (container_index != 0U && item_rule.max_stack != 1))
				return (error_code == FT_ERR_SUCCESS
					? FT_ERR_INVALID_STATE : error_code);
			++container_index;
		}
	}
	if (this->content_rules_.size() > UINT32_MAX)
		return (FT_ERR_OUT_OF_RANGE);
	if (policy_item_count
		!= this->definitions_.get_item_definitions().size())
	{
		std::fprintf(stderr,
			"item-catalog: policy count mismatch rules=%zu policy=%llu definitions=%zu\n",
			this->content_rules_.size(),
			static_cast<unsigned long long>(policy_item_count),
			this->definitions_.get_item_definitions().size());
		return (FT_ERR_INVALID_STATE);
	}
	*rule_count_out = static_cast<uint32_t>(this->content_rules_.size());
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::validate_equipment_coverage(
	uint32_t *rule_count_out) const noexcept
{
	uint32_t expected_count = 0U;
	if (rule_count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	for (int32_t item_id = WOOD_PICKAXE_ITEM_ID;
		item_id <= IRON_BOOTS_ITEM_ID; ++item_id)
		if (item_id != ARROW_ITEM_ID)
			++expected_count;
	++expected_count;
	if (this->equipment_rules_.size() != expected_count)
		return (FT_ERR_INVALID_STATE);
	for (std::size_t index = 0U; index < this->equipment_rules_.size(); ++index)
	{
		const MinecraftEquipmentRule &rule = this->equipment_rules_[index];
		MinecraftContentItemRule content = {};
		MinecraftContentItemRule repair_material = {};
		int32_t repair_material_id = 0;
		int32_t repair_material_quantity = 0;
		bool physical_identity_required = false;
		if (!((rule.item_id >= WOOD_PICKAXE_ITEM_ID
				&& rule.item_id <= IRON_BOOTS_ITEM_ID
				&& rule.item_id != ARROW_ITEM_ID)
			|| rule.item_id == WOOD_HOE_ITEM_ID)
			|| rule.slot < MINECRAFT_EQUIPMENT_MAIN_HAND
			|| rule.slot > MINECRAFT_EQUIPMENT_FEET
			|| rule.material_tier < MINECRAFT_MATERIAL_CLOTH
			|| rule.material_tier > MINECRAFT_MATERIAL_DIAMOND
			|| rule.durability == 0U
			|| rule.durability > static_cast<uint32_t>(
				std::numeric_limits<int32_t>::max())
			|| (rule.attack_power == 0U && rule.armor_points == 0U
				&& rule.mining_power == 0U))
			return (FT_ERR_INVALID_STATE);
		if (this->resolve_content_item_rule(rule.item_id, &content)
			!= FT_ERR_SUCCESS || content.max_stack != 1
			|| content.acquisition != MINECRAFT_ITEM_ACQUISITION_RECIPE
			|| this->requires_physical_instance_id(rule.item_id,
				&physical_identity_required) != FT_ERR_SUCCESS
			|| !physical_identity_required)
			return (FT_ERR_INVALID_STATE);
		if (this->resolve_equipment_repair_cost(rule.item_id,
				&repair_material_id, &repair_material_quantity) != FT_ERR_SUCCESS
			|| repair_material_quantity <= 0
			|| this->resolve_content_item_rule(repair_material_id,
				&repair_material) != FT_ERR_SUCCESS
			|| repair_material.max_stack <= 1)
			return (FT_ERR_INVALID_STATE);
		for (std::size_t previous = 0U; previous < index; ++previous)
			if (this->equipment_rules_[previous].item_id == rule.item_id)
				return (FT_ERR_ALREADY_EXISTS);
	}
	*rule_count_out = static_cast<uint32_t>(this->equipment_rules_.size());
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::resolve_equipment_rule(int32_t item_id,
	MinecraftEquipmentRule *rule_out) const noexcept
{
	if (rule_out == nullptr || item_id <= 0)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	for (const MinecraftEquipmentRule &rule : this->equipment_rules_)
		if (rule.item_id == item_id)
		{
			*rule_out = rule;
			return (FT_ERR_SUCCESS);
		}
	return (FT_ERR_NOT_FOUND);
}

int32_t MinecraftItemCatalog::resolve_block_mining_requirement(
	uint32_t block_id, MinecraftMiningToolClass *tool_class_out,
	uint32_t *minimum_power_out) const noexcept
{
	if (tool_class_out == nullptr || minimum_power_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->initialized_ || voxel_block_is_known(block_id) == FT_FALSE)
		return (FT_ERR_INVALID_OPERATION);
	MinecraftMiningToolClass tool_class = MINECRAFT_MINING_TOOL_NONE;
	uint32_t minimum_power = 0U;
	if (block_id == VOXEL_GENERATOR_OAK_LOG_BLOCK
		|| block_id == VOXEL_GENERATOR_PINE_LOG_BLOCK
		|| block_id == VOXEL_GENERATOR_BIRCH_LOG_BLOCK)
	{
		tool_class = MINECRAFT_MINING_TOOL_AXE;
		minimum_power = 1U;
	}
	else if (block_id == VOXEL_GENERATOR_STONE_BLOCK
		|| block_id == VOXEL_GENERATOR_CANYON_ROCK_BLOCK
		|| block_id == VOXEL_GENERATOR_SLATE_BLOCK
		|| block_id == VOXEL_GENERATOR_MOSS_ROCK_BLOCK
		|| block_id == VOXEL_GENERATOR_GRANITE_BLOCK
		|| block_id == VOXEL_GENERATOR_ANDESITE_BLOCK
		|| block_id == VOXEL_GENERATOR_DIORITE_BLOCK
		|| block_id == VOXEL_GENERATOR_OBSIDIAN_BLOCK
		|| block_id == VOXEL_GENERATOR_MOSSY_STONE_BLOCK
		|| block_id == VOXEL_GENERATOR_CRACKED_STONE_BLOCK
		|| block_id == VOXEL_GENERATOR_LIMESTONE_BLOCK
		|| block_id == VOXEL_GENERATOR_BASALT_BLOCK
		|| block_id == VOXEL_GENERATOR_FROZEN_STONE_BLOCK
		|| block_id == VOXEL_GENERATOR_CHALK_BLOCK
		|| block_id == VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK
		|| block_id == VOXEL_GENERATOR_SANDSTONE_BLOCK)
	{
		tool_class = MINECRAFT_MINING_TOOL_PICKAXE;
		minimum_power = block_id == VOXEL_GENERATOR_OBSIDIAN_BLOCK ? 5U : 1U;
	}
	else if (block_id == VOXEL_GENERATOR_COAL_ORE_BLOCK
		|| block_id == VOXEL_GENERATOR_IRON_ORE_BLOCK
		|| block_id == VOXEL_GENERATOR_GOLD_ORE_BLOCK
		|| block_id == VOXEL_GENERATOR_COPPER_ORE_BLOCK)
	{
		tool_class = MINECRAFT_MINING_TOOL_PICKAXE;
		minimum_power = block_id == VOXEL_GENERATOR_COAL_ORE_BLOCK ? 1U : 2U;
	}
	else if (block_id == VOXEL_GENERATOR_DIAMOND_ORE_BLOCK
		|| block_id == VOXEL_GENERATOR_EMERALD_ORE_BLOCK)
	{
		tool_class = MINECRAFT_MINING_TOOL_PICKAXE;
		minimum_power = 4U;
	}
	else if (block_id == VOXEL_GENERATOR_QUARTZ_BLOCK
		|| block_id == VOXEL_GENERATOR_AMETHYST_BLOCK
		|| block_id == VOXEL_GENERATOR_FROST_CRYSTAL_BLOCK
		|| block_id == VOXEL_GENERATOR_SHIMMER_STONE_BLOCK
		|| block_id == VOXEL_GENERATOR_AMBER_BLOCK)
	{
		tool_class = MINECRAFT_MINING_TOOL_PICKAXE;
		minimum_power = 3U;
	}
	*tool_class_out = tool_class;
	*minimum_power_out = minimum_power;
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::query_equipment_durability(
	const game_inventory_item_snapshot &item, uint32_t *current_out,
	uint32_t *maximum_out) const noexcept
{
	MinecraftEquipmentRule rule = {};
	uint32_t current;
	int32_t result;
	if (current_out == nullptr || maximum_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	result = this->resolve_equipment_rule(item.item_id, &rule);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (item.modifier1_id == 0)
	{
		if (item.modifier1_value != 0)
			return (FT_ERR_INVALID_STATE);
		current = rule.durability;
	}
	else
	{
		if (item.modifier1_id != EQUIPMENT_DURABILITY_MODIFIER_ID
			|| item.modifier1_value < 0
			|| static_cast<uint32_t>(item.modifier1_value) > rule.durability)
			return (FT_ERR_INVALID_STATE);
		current = static_cast<uint32_t>(item.modifier1_value);
	}
	*current_out = current;
	*maximum_out = rule.durability;
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::stage_equipment_damage(
	const game_inventory_snapshot &before, int32_t inventory_slot,
	uint64_t expected_revision, uint32_t damage,
	game_inventory_snapshot &after, bool *item_broken_out) const noexcept
{
	game_inventory_snapshot staged;
	std::vector<game_inventory_item_snapshot>::iterator item;
	uint32_t current_durability = 0U;
	uint32_t maximum_durability = 0U;
	bool item_broken = false;
	int32_t result;
	if (!this->initialized_ || inventory_slot < 0 || damage == 0U
		|| item_broken_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (before.revision != expected_revision
		|| before.revision == UINT64_MAX)
		return (FT_ERR_INVALID_STATE);
	try
	{
		staged = before;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	item = std::find_if(staged.items.begin(), staged.items.end(),
		[inventory_slot](const game_inventory_item_snapshot &candidate)
		{
			return (candidate.slot == inventory_slot);
		});
	if (item == staged.items.end())
		return (FT_ERR_NOT_FOUND);
	result = this->query_equipment_durability(*item, &current_durability,
		&maximum_durability);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (item->instance_id == 0U || item->stack_size != 1
		|| item->max_stack != 1)
		return (FT_ERR_INVALID_STATE);
	if (current_durability == 0U)
		return (FT_ERR_INVALID_STATE);
	item_broken = damage >= current_durability;
	item->modifier1_id = EQUIPMENT_DURABILITY_MODIFIER_ID;
	item->modifier1_value = item_broken ? 0
		: static_cast<int32_t>(current_durability - damage);
	staged.revision += 1U;
	after = std::move(staged);
	*item_broken_out = item_broken;
	(void)maximum_durability;
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::stage_equipment_repair(
	const game_inventory_snapshot &before, int32_t inventory_slot,
	uint64_t expected_revision, int32_t material_item_id,
	int32_t material_quantity, game_inventory_snapshot &after) const noexcept
{
	game_inventory_snapshot staged;
	std::vector<game_inventory_item_snapshot>::iterator item;
	int32_t repaired_item_id = 0;
	uint64_t repaired_instance_id = 0U;
	uint32_t current = 0U, maximum = 0U;
	int64_t available = 0;
	int32_t required_material_id = 0;
	int32_t required_material_quantity = 0;
	int32_t result;
	if (!this->initialized_ || inventory_slot < 0 || material_item_id <= 0
		|| material_quantity <= 0
		|| before.revision != expected_revision || before.revision == UINT64_MAX)
		return (FT_ERR_INVALID_STATE);
	try
	{
		staged = before;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	item = std::find_if(staged.items.begin(), staged.items.end(),
		[inventory_slot](const game_inventory_item_snapshot &candidate)
		{ return (candidate.slot == inventory_slot); });
	if (item == staged.items.end())
		return (FT_ERR_NOT_FOUND);
	if (item->instance_id == 0U || item->stack_size != 1 || item->max_stack != 1)
		return (FT_ERR_INVALID_STATE);
	repaired_item_id = item->item_id;
	repaired_instance_id = item->instance_id;
	result = this->resolve_equipment_repair_cost(item->item_id,
		&required_material_id, &required_material_quantity);
	if (result != FT_ERR_SUCCESS || material_item_id != required_material_id
		|| material_quantity != required_material_quantity)
		return (FT_ERR_PERMISSION_DENIED);
	result = this->query_equipment_durability(*item, &current, &maximum);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (current == maximum)
		return (FT_ERR_INVALID_STATE);
	for (const game_inventory_item_snapshot &candidate : staged.items)
		if (candidate.item_id == material_item_id && candidate.instance_id == 0U
			&& candidate.stack_size > 0
			&& candidate.max_stack > 1)
			available += candidate.stack_size;
	if (available < material_quantity)
		return (FT_ERR_NOT_FOUND);
#if USE_INVENTORY_WEIGHT
	if (staged.current_weight < material_quantity)
		return (FT_ERR_INVALID_STATE);
#endif
	int32_t remaining = material_quantity;
	for (std::vector<game_inventory_item_snapshot>::iterator material =
			staged.items.begin(); material != staged.items.end() && remaining > 0;)
	{
		if (material->item_id != material_item_id || material->instance_id != 0U
			|| material->stack_size <= 0 || material->max_stack <= 1)
		{
			++material;
			continue ;
		}
		const int32_t consumed = std::min(remaining, material->stack_size);
		material->stack_size -= consumed;
		remaining -= consumed;
		if (material->stack_size == 0)
		{
			const int64_t area = static_cast<int64_t>(material->width)
				* material->height;
			if (material->width <= 0 || material->height <= 0 || area < 0)
				return (FT_ERR_INVALID_STATE);
#if USE_INVENTORY_SLOTS
			if (static_cast<uint64_t>(area) > staged.used_slots)
				return (FT_ERR_INVALID_STATE);
			staged.used_slots -= static_cast<ft_size_t>(area);
#endif
			material = staged.items.erase(material);
		}
		else
			++material;
	}
	if (remaining != 0)
		return (FT_ERR_INVALID_STATE);
#if USE_INVENTORY_WEIGHT
	staged.current_weight -= material_quantity;
#endif
	item = std::find_if(staged.items.begin(), staged.items.end(),
		[inventory_slot, repaired_item_id, repaired_instance_id](
			const game_inventory_item_snapshot &candidate)
		{
			return (candidate.slot == inventory_slot
				&& candidate.item_id == repaired_item_id
				&& candidate.instance_id == repaired_instance_id);
		});
	if (item == staged.items.end())
		return (FT_ERR_INVALID_STATE);
	item->modifier1_id = EQUIPMENT_DURABILITY_MODIFIER_ID;
	item->modifier1_value = static_cast<int32_t>(maximum);
	++staged.revision;
	after = std::move(staged);
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::resolve_equipment_repair_cost(
	int32_t equipment_item_id, int32_t *material_item_id_out,
	int32_t *material_quantity_out) const noexcept
{
	MinecraftEquipmentRule rule = {};
	int32_t material_item_id;
	int32_t result;
	if (material_item_id_out == nullptr || material_quantity_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	result = this->resolve_equipment_rule(equipment_item_id, &rule);
	if (result != FT_ERR_SUCCESS)
		return (result);
	switch (rule.material_tier)
	{
		case MINECRAFT_MATERIAL_CLOTH:
			material_item_id = WOOL_ITEM_ID;
			break ;
		case MINECRAFT_MATERIAL_WOOD:
			material_item_id = OAK_PLANKS_ITEM_ID;
			break ;
		case MINECRAFT_MATERIAL_LEATHER:
			material_item_id = LEATHER_ITEM_ID;
			break ;
		case MINECRAFT_MATERIAL_STONE:
			material_item_id = BLOCK_ITEM_ID_BASE
				+ static_cast<int32_t>(VOXEL_GENERATOR_STONE_BLOCK);
			break ;
		case MINECRAFT_MATERIAL_COPPER:
			material_item_id = COPPER_BAR_ITEM_ID;
			break ;
		case MINECRAFT_MATERIAL_IRON:
			material_item_id = IRON_BAR_ITEM_ID;
			break ;
		case MINECRAFT_MATERIAL_GOLD:
			material_item_id = GOLD_BAR_ITEM_ID;
			break ;
		case MINECRAFT_MATERIAL_DIAMOND:
			material_item_id = DIAMOND_MATERIAL_ITEM_ID;
			break ;
		default:
			return (FT_ERR_INVALID_STATE);
	}
	*material_item_id_out = material_item_id;
	*material_quantity_out = 1;
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::resolve_content_item_rule(int32_t item_id,
	MinecraftContentItemRule *rule_out) const noexcept
{
	std::size_t index = 0U;

	if (rule_out == nullptr || item_id <= 0)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (item_id >= BLOCK_ITEM_ID_BASE)
	{
		const uint32_t block_id = static_cast<uint32_t>(
			item_id - BLOCK_ITEM_ID_BASE);
		MinecraftBlockItemRule block_rule = {};
		int32_t error_code = this->resolve_block_item_rule(block_id,
			&block_rule);

		if (error_code == FT_ERR_SUCCESS && block_rule.item_id == item_id
			&& (block_rule.policy == MINECRAFT_BLOCK_ACQUISITION_SELF_ITEM
				|| block_rule.policy == MINECRAFT_BLOCK_ACQUISITION_ADMIN_ONLY))
		{
			game_item_definition definition;

			error_code = this->definitions_.fetch_item_definition(item_id,
				definition);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			rule_out->item_id = item_id;
			rule_out->max_stack = definition.get_max_stack();
			rule_out->acquisition = block_rule.policy
				== MINECRAFT_BLOCK_ACQUISITION_ADMIN_ONLY
				? MINECRAFT_ITEM_ACQUISITION_RESTRICTED
				: MINECRAFT_ITEM_ACQUISITION_WORLD;
			(void)definition.destroy();
			return (FT_ERR_SUCCESS);
		}
	}
	while (index < this->content_rules_.size())
	{
		if (this->content_rules_[index].item_id == item_id)
		{
			*rule_out = this->content_rules_[index];
			return (FT_ERR_SUCCESS);
		}
		++index;
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t MinecraftItemCatalog::requires_physical_instance_id(int32_t item_id,
	bool *required_out) const noexcept
{
	MinecraftContentItemRule rule = {};
	int32_t result;
	if (required_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*required_out = false;
	result = this->resolve_content_item_rule(item_id, &rule);
	if (result != FT_ERR_SUCCESS)
		return (result);
	*required_out = item_id == WATER_BUCKET_ITEM_ID
		|| item_id == LAVA_BUCKET_ITEM_ID || item_id == BOAT_ITEM_ID
		|| (item_id >= WOOD_PICKAXE_ITEM_ID
			&& item_id <= IRON_BOOTS_ITEM_ID
			&& item_id != ARROW_ITEM_ID)
		|| item_id == WOOD_HOE_ITEM_ID
		|| (item_id >= BIOME_EXCLUSION_CARD_ITEM_ID
			&& item_id <= VOLATILE_STABILIZER_CARD_ITEM_ID)
		|| item_id == FORTUNES_DRAW_CARD_ITEM_ID;
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::validate_inventory_instance_policy(
	const game_inventory_snapshot &snapshot) const noexcept
{
	if (!this->initialized_ || snapshot.items.size() > snapshot.capacity)
		return (FT_ERR_INVALID_STATE);
	for (const game_inventory_item_snapshot &item : snapshot.items)
	{
		bool required = false;
		const int32_t result = this->requires_physical_instance_id(item.item_id,
			&required);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (required && (item.instance_id == 0U || item.stack_size != 1
			|| item.max_stack != 1))
			return (FT_ERR_INVALID_STATE);
		MinecraftEquipmentRule equipment = {};
		if (this->resolve_equipment_rule(item.item_id, &equipment)
			== FT_ERR_SUCCESS)
		{
			uint32_t current_durability = 0U;
			uint32_t maximum_durability = 0U;
			if (this->query_equipment_durability(item, &current_durability,
					&maximum_durability) != FT_ERR_SUCCESS
				|| current_durability == 0U
				|| maximum_durability != equipment.durability)
				return (FT_ERR_INVALID_STATE);
		}
		else if ((item.item_id >= WOOD_PICKAXE_ITEM_ID
				&& item.item_id <= IRON_BOOTS_ITEM_ID
				&& item.item_id != ARROW_ITEM_ID)
			|| item.item_id == WOOD_HOE_ITEM_ID)
			return (FT_ERR_INVALID_STATE);
	}
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::resolve_fluid_container_rule(int32_t item_id,
	MinecraftFluidContainerRule *rule_out) const noexcept
{
	static const MinecraftFluidContainerRule rules[] =
	{
		{EMPTY_BUCKET_ITEM_ID, 1U, MINECRAFT_FLUID_NONE},
		{WATER_BUCKET_ITEM_ID, 1U, MINECRAFT_FLUID_WATER},
		{LAVA_BUCKET_ITEM_ID, 1U, MINECRAFT_FLUID_LAVA}
	};
	std::size_t index = 0U;

	if (rule_out == nullptr || item_id <= 0)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	while (index < sizeof(rules) / sizeof(rules[0]))
	{
		if (rules[index].item_id == item_id)
		{
			if (rules[index].capacity == 0U
				|| (rules[index].fluid_kind != MINECRAFT_FLUID_NONE
					&& rules[index].fluid_kind != MINECRAFT_FLUID_WATER
					&& rules[index].fluid_kind != MINECRAFT_FLUID_LAVA))
				return (FT_ERR_INVALID_STATE);
			*rule_out = rules[index];
			return (FT_ERR_SUCCESS);
		}
		++index;
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t MinecraftItemCatalog::prepare_fluid_transfer_snapshot(
	const game_inventory_snapshot &snapshot, int32_t inventory_slot,
	uint64_t expected_revision, bool pickup_source,
	MinecraftFluidKind fluid_kind,
	game_inventory_snapshot &snapshot_out,
	uint64_t output_instance_id) const noexcept
{
	game_inventory_snapshot staged = {};
	MinecraftFluidContainerRule input_rule = {};
	MinecraftFluidContainerRule output_rule = {};
	game_item_definition output_definition;
	int32_t output_item_id;
	int32_t output_max_stack;
	int32_t output_rarity;
	int32_t output_width;
	int32_t output_height;
	int32_t result;
	std::vector<game_inventory_item_snapshot>::iterator item;

	if (!this->initialized_ || inventory_slot < 0 || expected_revision == 0U
		|| (fluid_kind != MINECRAFT_FLUID_WATER
			&& fluid_kind != MINECRAFT_FLUID_LAVA)
		|| snapshot.revision != expected_revision)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		staged = snapshot;
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	item = std::find_if(staged.items.begin(), staged.items.end(),
		[inventory_slot](const game_inventory_item_snapshot &entry)
		{
			return (entry.slot == inventory_slot);
		});
	if (item == staged.items.end())
		return (FT_ERR_NOT_FOUND);
	result = this->resolve_fluid_container_rule(item->item_id, &input_rule);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if ((pickup_source
			&& (input_rule.fluid_kind != MINECRAFT_FLUID_NONE
				|| input_rule.item_id != EMPTY_BUCKET_ITEM_ID))
		|| (!pickup_source
			&& (input_rule.fluid_kind != fluid_kind
				|| input_rule.capacity != 1U)))
		return (FT_ERR_INVALID_STATE);
	output_item_id = pickup_source
		? (fluid_kind == MINECRAFT_FLUID_WATER
			? WATER_BUCKET_ITEM_ID : LAVA_BUCKET_ITEM_ID)
		: EMPTY_BUCKET_ITEM_ID;
	result = this->resolve_fluid_container_rule(output_item_id, &output_rule);
	if (result != FT_ERR_SUCCESS
		|| output_rule.capacity != input_rule.capacity)
		return (result == FT_ERR_SUCCESS ? FT_ERR_CONFIGURATION : result);
	result = this->definitions_.fetch_item_definition(output_item_id,
		output_definition);
	if (result != FT_ERR_SUCCESS)
		return (result);
	output_max_stack = output_definition.get_max_stack();
	output_rarity = output_definition.get_rarity();
	output_width = output_definition.get_width();
	output_height = output_definition.get_height();
	(void)output_definition.destroy();
	if (output_max_stack <= 0 || output_width <= 0 || output_height <= 0
		|| item->stack_size <= 0 || item->width <= 0 || item->height <= 0
		|| (item->instance_id != 0U
			&& (item->stack_size != 1 || item->max_stack != 1)))
		return (FT_ERR_INVALID_STATE);
	const uint64_t old_area = static_cast<uint64_t>(item->width)
		* static_cast<uint64_t>(item->height);
	const uint64_t new_area = static_cast<uint64_t>(output_width)
		* static_cast<uint64_t>(output_height);
	if (item->stack_size > 1)
	{
		if (!pickup_source)
			return (FT_ERR_INVALID_STATE);
		int32_t output_slot = 0;
		for (;; ++output_slot)
		{
			const bool occupied = std::any_of(staged.items.begin(),
				staged.items.end(),
				[output_slot](const game_inventory_item_snapshot &entry)
				{
					return (entry.slot == output_slot);
				});
			if (!occupied)
				break ;
			if (output_slot == std::numeric_limits<int32_t>::max())
				return (FT_ERR_FULL);
		}
		if ((staged.capacity != 0U
				&& (staged.used_slots > staged.capacity
					|| new_area > staged.capacity - staged.used_slots))
			|| output_slot == std::numeric_limits<int32_t>::max())
			return (FT_ERR_FULL);
		item->stack_size -= 1;
		game_inventory_item_snapshot added = {};
		added.slot = output_slot;
		added.item_id = output_item_id;
		added.instance_id = output_instance_id;
		added.stack_size = 1;
		added.max_stack = output_instance_id == 0U ? output_max_stack : 1;
		added.rarity = output_rarity;
		added.width = output_width;
		added.height = output_height;
		try
		{
			staged.items.push_back(added);
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		staged.used_slots += new_area;
		staged.next_slot = std::max(staged.next_slot, output_slot + 1);
	}
	else
	{
		if (new_area > old_area && staged.capacity != 0U
			&& (staged.used_slots > staged.capacity
				|| new_area - old_area > staged.capacity - staged.used_slots))
			return (FT_ERR_FULL);
		item->item_id = output_item_id;
		item->max_stack = item->instance_id == 0U ? output_max_stack : 1;
		item->rarity = output_rarity;
		item->width = output_width;
		item->height = output_height;
		item->modifier1_id = 0;
		item->modifier1_value = 0;
		item->modifier2_id = 0;
		item->modifier2_value = 0;
		item->modifier3_id = 0;
		item->modifier3_value = 0;
		item->modifier4_id = 0;
		item->modifier4_value = 0;
		if (new_area >= old_area)
			staged.used_slots += new_area - old_area;
		else
			staged.used_slots -= old_area - new_area;
	}
	if (staged.revision == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	staged.revision += 1U;
	snapshot_out = std::move(staged);
	return (FT_ERR_SUCCESS);
}

const std::vector<MinecraftContentItemRule> &
MinecraftItemCatalog::content_item_rules() const noexcept
{
	return (this->content_rules_);
}

const std::vector<MinecraftEquipmentRule> &
MinecraftItemCatalog::equipment_item_rules() const noexcept
{
	return (this->equipment_rules_);
}

int32_t MinecraftItemCatalog::resolve_item_tag(uint32_t tag_id,
	std::vector<int32_t> &item_ids_out) const noexcept
{
	std::vector<int32_t> staged;
	if (!this->initialized_ || tag_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		for (const MinecraftItemTagRule &rule : this->item_tag_rules_)
		{
			if (rule.tag_id == tag_id)
				staged.push_back(rule.item_id);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (staged.empty())
		return (FT_ERR_NOT_FOUND);
	item_ids_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftItemCatalog::create_item(int32_t item_id, int32_t count,
	ft_sharedptr<game_item> *item_out) const noexcept
{
	game_item_definition definition;
	MinecraftEquipmentRule equipment = {};
	ft_sharedptr<game_item> item;
	int32_t error_code;

	if (item_out == nullptr || count <= 0)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	error_code = this->definitions_.fetch_item_definition(item_id, definition);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (definition.get_max_stack() <= 0 || count > definition.get_max_stack())
	{
		(void)definition.destroy();
		return (FT_ERR_OUT_OF_RANGE);
	}
	item = ft_sharedptr<game_item>(new (std::nothrow) game_item());
	if (!item)
		error_code = FT_ERR_NO_MEMORY;
	else
		error_code = item->initialize();
	if (error_code == FT_ERR_SUCCESS)
	{
		item->set_item_id(item_id);
		item->set_max_stack(definition.get_max_stack());
		item->set_stack_size(count);
		item->set_rarity(definition.get_rarity());
		item->set_width(definition.get_width());
		item->set_height(definition.get_height());
		const int32_t equipment_result = this->resolve_equipment_rule(item_id,
			&equipment);
		if (equipment_result == FT_ERR_SUCCESS)
		{
			item->set_modifier1_id(EQUIPMENT_DURABILITY_MODIFIER_ID);
			item->set_modifier1_value(static_cast<int32_t>(equipment.durability));
		}
		else if (equipment_result != FT_ERR_NOT_FOUND)
			error_code = equipment_result;
		if (error_code == FT_ERR_SUCCESS)
			*item_out = item;
	}
	(void)definition.destroy();
	return (error_code);
}

const game_data_catalog &MinecraftItemCatalog::definitions() const noexcept
{
	return (this->definitions_);
}
