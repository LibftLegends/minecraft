#include "MinecraftLootCatalog.hpp"
#include "MinecraftItemCatalog.hpp"
#include "../crafting/MinecraftRecipeCatalog.hpp"
#include "../../Libft/Modules/Voxel/voxel_types.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <new>

namespace
{
	struct MinecraftLootEntryDefinition
	{
		uint32_t loot_table_id;
		int32_t item_id;
		uint32_t chance_basis_points;
		int32_t minimum_quantity;
		int32_t maximum_quantity;
	};

	static const MinecraftLootEntryDefinition MINECRAFT_LOOT_ENTRIES[] =
	{
		{MINECRAFT_LOOT_ZOMBIE,
			MinecraftItemCatalog::ROTTEN_FLESH_ITEM_ID, 10000U, 1, 2},
		{MINECRAFT_LOOT_SKELETON,
			MinecraftItemCatalog::BONE_ITEM_ID, 10000U, 1, 2},
		{MINECRAFT_LOOT_SKELETON,
			MinecraftItemCatalog::STRING_ITEM_ID, 5000U, 1, 2},
		{MINECRAFT_LOOT_CREEPER,
			MinecraftItemCatalog::GUNPOWDER_ITEM_ID, 10000U, 1, 2},
		{MINECRAFT_LOOT_COW,
			MinecraftItemCatalog::RAW_BEEF_ITEM_ID, 10000U, 1, 3},
		{MINECRAFT_LOOT_COW,
			MinecraftItemCatalog::LEATHER_ITEM_ID, 7500U, 1, 2},
		{MINECRAFT_LOOT_PIG,
			MinecraftItemCatalog::RAW_PORK_ITEM_ID, 10000U, 1, 3},
		{MINECRAFT_LOOT_CHICKEN,
			MinecraftItemCatalog::RAW_CHICKEN_ITEM_ID, 10000U, 1, 2},
		{MINECRAFT_LOOT_CHICKEN,
			MinecraftItemCatalog::FEATHER_ITEM_ID, 7500U, 1, 2},
		{MINECRAFT_LOOT_SHEEP,
			MinecraftItemCatalog::RAW_MUTTON_ITEM_ID, 10000U, 1, 2},
		{MINECRAFT_LOOT_SHEEP,
			MinecraftItemCatalog::WOOL_ITEM_ID, 10000U, 1, 1},
		{MINECRAFT_LOOT_RUINED_CACHE,
			MinecraftItemCatalog::EMERALD_MATERIAL_ITEM_ID, 1800U, 1, 2},
		{MINECRAFT_LOOT_RUINED_CACHE,
			MinecraftItemCatalog::DIAMOND_MATERIAL_ITEM_ID, 350U, 1, 1},
		{MINECRAFT_LOOT_SHIPWRECK_CACHE,
			MinecraftItemCatalog::EMERALD_MATERIAL_ITEM_ID, 2400U, 1, 2},
		{MINECRAFT_LOOT_SHIPWRECK_CACHE,
			MinecraftItemCatalog::DIAMOND_MATERIAL_ITEM_ID, 250U, 1, 1},
		{MINECRAFT_LOOT_SHIPWRECK_CACHE_REWARD,
			MinecraftItemCatalog::EMERALD_MATERIAL_ITEM_ID, 4500U, 1, 2},
		{MINECRAFT_LOOT_SHIPWRECK_CACHE_REWARD,
			MinecraftItemCatalog::DIAMOND_MATERIAL_ITEM_ID, 1200U, 1, 1},
		{MINECRAFT_LOOT_SHIPWRECK_CACHE_HIGH_REWARD,
			MinecraftItemCatalog::EMERALD_MATERIAL_ITEM_ID, 7000U, 1, 2},
		{MINECRAFT_LOOT_SHIPWRECK_CACHE_HIGH_REWARD,
			MinecraftItemCatalog::DIAMOND_MATERIAL_ITEM_ID, 3000U, 1, 1},
		{MINECRAFT_LOOT_SPIDER,
			MinecraftItemCatalog::STRING_ITEM_ID, 10000U, 1, 2},
		{MINECRAFT_LOOT_SPIDER,
			MinecraftItemCatalog::SPIDER_EYE_ITEM_ID, 2500U, 1, 1},
		{MINECRAFT_LOOT_DESERT_PYRAMID_CACHE,
			MinecraftItemCatalog::GOLD_BAR_ITEM_ID, 10000U, 1, 2},
		{MINECRAFT_LOOT_DESERT_PYRAMID_CACHE,
			MinecraftItemCatalog::DIAMOND_MATERIAL_ITEM_ID, 1200U, 1, 1},
		{MINECRAFT_LOOT_ANCIENT_CANOPY_CACHE,
			MinecraftItemCatalog::PLANT_FIBER_ITEM_ID, 10000U, 2, 4},
		{MINECRAFT_LOOT_ANCIENT_CANOPY_CACHE,
			MinecraftItemCatalog::APPLE_ITEM_ID, 3500U, 1, 2},
		{MINECRAFT_LOOT_SPIDER_NEST_CACHE,
			MinecraftItemCatalog::STRING_ITEM_ID, 10000U, 2, 4},
		{MINECRAFT_LOOT_SPIDER_NEST_CACHE,
			MinecraftItemCatalog::SPIDER_EYE_ITEM_ID, 4000U, 1, 2}
	};

	struct MinecraftLootRecipeDiscoveryDefinition
	{
		uint32_t loot_table_id;
		uint32_t source_type_id;
		uint32_t source_id;
		uint32_t recipe_id;
	};

	static const MinecraftLootRecipeDiscoveryDefinition
		MINECRAFT_LOOT_RECIPE_DISCOVERIES[] = {
			{MINECRAFT_LOOT_RUINED_CACHE, 1U, MINECRAFT_LOOT_RUINED_CACHE,
				MinecraftRecipeCatalog::RUINED_VILLAGE_RELIC_CARD_RECIPE_ID},
			{MINECRAFT_LOOT_SHIPWRECK_CACHE, 1U,
				MINECRAFT_LOOT_SHIPWRECK_CACHE,
				MinecraftRecipeCatalog::SHIPWRECK_CHART_CARD_RECIPE_ID},
			{MINECRAFT_LOOT_SHIPWRECK_CACHE_REWARD, 1U,
				MINECRAFT_LOOT_SHIPWRECK_CACHE_REWARD,
				MinecraftRecipeCatalog::SHIPWRECK_CHART_CARD_RECIPE_ID},
			{MINECRAFT_LOOT_SHIPWRECK_CACHE_HIGH_REWARD, 1U,
				MINECRAFT_LOOT_SHIPWRECK_CACHE_HIGH_REWARD,
				MinecraftRecipeCatalog::SHIPWRECK_CHART_CARD_RECIPE_ID},
			{MINECRAFT_LOOT_DESERT_PYRAMID_CACHE, 1U,
				MINECRAFT_LOOT_DESERT_PYRAMID_CACHE,
				MinecraftRecipeCatalog::DESERT_PYRAMID_SEAL_CARD_RECIPE_ID},
			{MINECRAFT_LOOT_ANCIENT_CANOPY_CACHE, 1U,
				MINECRAFT_LOOT_ANCIENT_CANOPY_CACHE,
				MinecraftRecipeCatalog::ANCIENT_CANOPY_SEAL_CARD_RECIPE_ID},
			{MINECRAFT_LOOT_SPIDER_NEST_CACHE, 1U,
				MINECRAFT_LOOT_SPIDER_NEST_CACHE,
				MinecraftRecipeCatalog::SPIDER_NEST_SEAL_CARD_RECIPE_ID}};

	static uint32_t minecraft_loot_roll(uint32_t entity_id,
		uint32_t loot_table_id, uint32_t entry_index) noexcept
	{
		uint32_t hash = entity_id
			^ (loot_table_id * 0x9e3779b1U)
			^ (entry_index * 0x85ebca6bU);
		hash ^= hash >> 16U;
		hash *= 0x7feb352dU;
		hash ^= hash >> 15U;
		hash *= 0x846ca68bU;
		hash ^= hash >> 16U;
		return (hash);
	}
}

int32_t MinecraftLootCatalog::resolve(uint32_t loot_table_id,
	uint32_t entity_id, std::vector<MinecraftLootDrop> &drops_out) noexcept
{
	std::vector<MinecraftLootDrop> staged;
	bool table_found = false;
	if (entity_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		for (uint32_t entry_index = 0U;
			entry_index < sizeof(MINECRAFT_LOOT_ENTRIES)
				/ sizeof(MINECRAFT_LOOT_ENTRIES[0]); ++entry_index)
		{
			const MinecraftLootEntryDefinition &entry =
				MINECRAFT_LOOT_ENTRIES[entry_index];
			if (entry.loot_table_id != loot_table_id)
				continue ;
			table_found = true;
			if (entry.chance_basis_points
				> MinecraftLootCatalog::MAXIMUM_CHANCE_BASIS_POINTS
				|| entry.minimum_quantity <= 0
				|| entry.maximum_quantity < entry.minimum_quantity)
				return (FT_ERR_CONFIGURATION);
			if (minecraft_loot_roll(entity_id, loot_table_id, entry_index)
				% MinecraftLootCatalog::MAXIMUM_CHANCE_BASIS_POINTS
				>= entry.chance_basis_points)
				continue ;
			if (staged.size() >= MinecraftLootCatalog::MAXIMUM_DROPS_PER_DEATH)
				return (FT_ERR_FULL);
			const uint32_t quantity_range = static_cast<uint32_t>(
				entry.maximum_quantity - entry.minimum_quantity) + 1U;
			const int32_t quantity = entry.minimum_quantity
				+ static_cast<int32_t>(minecraft_loot_roll(entity_id,
					loot_table_id, entry_index + 0x100U) % quantity_range);
			staged.push_back({entry.item_id, quantity});
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (!table_found)
		return (FT_ERR_CONFIGURATION);
	drops_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftLootCatalog::resolve_recipe_discovery(
	uint32_t loot_table_id,
	MinecraftLootRecipeDiscovery *discovery_out) noexcept
{
	if (discovery_out == nullptr || loot_table_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	for (const MinecraftLootRecipeDiscoveryDefinition &definition
		: MINECRAFT_LOOT_RECIPE_DISCOVERIES)
	{
		if (definition.loot_table_id != loot_table_id)
			continue ;
		discovery_out->source_type_id = definition.source_type_id;
		discovery_out->source_id = definition.source_id;
		discovery_out->recipe_id = definition.recipe_id;
		return (FT_ERR_SUCCESS);
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t MinecraftLootCatalog::resolve_tree_fruit(uint32_t leaf_block_id,
	int32_t world_x, int32_t world_y, int32_t world_z, uint64_t server_tick,
	MinecraftLootDrop *drop_out) noexcept
{
	uint64_t hash = 1469598103934665603ULL;
	const uint32_t values[] = {leaf_block_id, static_cast<uint32_t>(world_x),
		static_cast<uint32_t>(world_y), static_cast<uint32_t>(world_z),
		static_cast<uint32_t>(server_tick),
		static_cast<uint32_t>(server_tick >> 32)};
	if (drop_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (leaf_block_id != VOXEL_GENERATOR_OAK_LEAVES_BLOCK_ID
		&& leaf_block_id != VOXEL_GENERATOR_BIRCH_LEAVES_BLOCK_ID)
		return (FT_ERR_NOT_FOUND);
	for (std::size_t index = 0U;
		index < sizeof(values) / sizeof(values[0]); ++index)
	{
		hash ^= values[index];
		hash *= 1099511628211ULL;
	}
	if (hash % 8U != 0U)
		return (FT_ERR_NOT_FOUND);
	drop_out->item_id = leaf_block_id == VOXEL_GENERATOR_OAK_LEAVES_BLOCK_ID
		? MinecraftItemCatalog::APPLE_ITEM_ID
		: MinecraftItemCatalog::PEAR_ITEM_ID;
	drop_out->quantity = 1;
	return (FT_ERR_SUCCESS);
}
