#ifndef MINECRAFT_LOOT_CATALOG_HPP
# define MINECRAFT_LOOT_CATALOG_HPP

# include <cstdint>
# include <cstddef>
# include <vector>

enum MinecraftLootTableId : uint32_t
{
	MINECRAFT_LOOT_ZOMBIE = 1U,
	MINECRAFT_LOOT_SKELETON = 2U,
	MINECRAFT_LOOT_CREEPER = 3U,
	MINECRAFT_LOOT_COW = 4U,
	MINECRAFT_LOOT_PIG = 5U,
	MINECRAFT_LOOT_CHICKEN = 6U,
	MINECRAFT_LOOT_SHEEP = 7U,
	MINECRAFT_LOOT_RUINED_CACHE = 8U,
	MINECRAFT_LOOT_SHIPWRECK_CACHE = 9U,
	MINECRAFT_LOOT_SHIPWRECK_CACHE_REWARD = 10U,
	MINECRAFT_LOOT_SHIPWRECK_CACHE_HIGH_REWARD = 11U,
	MINECRAFT_LOOT_SPIDER = 12U,
	MINECRAFT_LOOT_DESERT_PYRAMID_CACHE = 13U,
	MINECRAFT_LOOT_ANCIENT_CANOPY_CACHE = 14U,
	MINECRAFT_LOOT_SPIDER_NEST_CACHE = 15U
};

struct MinecraftLootRecipeDiscovery
{
	uint32_t source_type_id;
	uint32_t source_id;
	uint32_t recipe_id;
};

struct MinecraftLootDrop
{
	int32_t item_id;
	int32_t quantity;
};

/* Stable, bounded mob and structure-cache loot. Entity IDs are the
 * deterministic seed, so a pending death event resolves identically after
 * restore and retry. */
class MinecraftLootCatalog
{
  public:
	static const std::size_t MAXIMUM_DROPS_PER_DEATH = 2U;
	static const uint32_t MAXIMUM_CHANCE_BASIS_POINTS = 10000U;
	static int32_t resolve(uint32_t loot_table_id, uint32_t entity_id,
		std::vector<MinecraftLootDrop> &drops_out) noexcept;
	static int32_t resolve_recipe_discovery(uint32_t loot_table_id,
		MinecraftLootRecipeDiscovery *discovery_out) noexcept;
	static int32_t resolve_tree_fruit(uint32_t leaf_block_id, int32_t world_x,
		int32_t world_y, int32_t world_z, uint64_t server_tick,
		MinecraftLootDrop *drop_out) noexcept;
};

#endif
