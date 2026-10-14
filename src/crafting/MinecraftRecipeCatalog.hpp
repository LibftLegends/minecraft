#ifndef MINECRAFT_RECIPE_CATALOG_HPP
# define MINECRAFT_RECIPE_CATALOG_HPP

# include "../../Libft/Modules/Game/game_crafting.hpp"
# include "../../Libft/Modules/Game/game_recipe_book.hpp"
# include "../../src/inventory/MinecraftItemCatalog.hpp"
# include <map>
# include <mutex>
# include <vector>

struct MinecraftCraftActionResult
{
	uint64_t request_id;
	uint64_t inventory_revision;
	uint32_t recipe_id;
	int32_t batch_count;
	int32_t status;
	bool replayed;
};

struct MinecraftProcessingActionResult
{
	uint64_t request_id;
	uint64_t inventory_revision;
	uint32_t recipe_id;
	int32_t batch_count;
	uint32_t ticks_remaining;
	int32_t status;
	bool replayed;
};

struct MinecraftRecipeQueryEntry
{
	uint32_t recipe_id;
	ft_bool known;
	game_crafting_query_result status;
};

struct MinecraftRecipeMissingGroup
{
	uint32_t recipe_id;
	game_crafting_missing_ingredient ingredient;
};

class MinecraftRecipeCatalog
{
  public:
	enum StationId : int32_t
	{
		HAND_STATION_ID = 0,
		WORKBENCH_STATION_ID = 1,
		PAPER_MILL_STATION_ID = 2,
		CARD_PRESS_STATION_ID = 3,
		FURNACE_STATION_ID = 4
	};
	static constexpr uint32_t BIOME_EXCLUSION_CARD_RECIPE_ID = 2001U;
	static constexpr uint32_t DEEP_SEAM_CARD_RECIPE_ID = 2002U;
	static constexpr uint32_t VILLAGE_CHARTER_CARD_RECIPE_ID = 2003U;
	static constexpr uint32_t SURFACE_MAGMA_CARD_RECIPE_ID = 2004U;
	static constexpr uint32_t DOUBLE_DRAFT_CARD_RECIPE_ID = 2005U;
	static constexpr uint32_t BIOME_COMPASS_CARD_RECIPE_ID = 2006U;
	static constexpr uint32_t SURVEYORS_REACH_CARD_RECIPE_ID = 2007U;
	static constexpr uint32_t VOLCANO_MARK_CARD_RECIPE_ID = 2008U;
	static constexpr uint32_t ULTIMATE_GREED_CARD_RECIPE_ID = 2009U;
	static constexpr uint32_t BIOME_RUPTURE_CARD_RECIPE_ID = 2010U;
	static constexpr uint32_t ORE_OVERGROWTH_CARD_RECIPE_ID = 2011U;
	static constexpr uint32_t SHIPWRECK_CHART_CARD_RECIPE_ID = 2012U;
	static constexpr uint32_t RUINED_VILLAGE_RELIC_CARD_RECIPE_ID = 2013U;
	static constexpr uint32_t UNSTABLE_CATALYST_CARD_RECIPE_ID = 2014U;
	static constexpr uint32_t DESERT_PYRAMID_SEAL_CARD_RECIPE_ID = 2015U;
	static constexpr uint32_t ANCIENT_CANOPY_SEAL_CARD_RECIPE_ID = 2016U;
	static constexpr uint32_t SPIDER_NEST_SEAL_CARD_RECIPE_ID = 2017U;
	static constexpr uint32_t VOLATILE_STABILIZER_CARD_RECIPE_ID = 2018U;
	static constexpr uint32_t FORTUNES_DRAW_CARD_RECIPE_ID = 2019U;
	static constexpr uint32_t IRON_SMELTING_RECIPE_ID = 1029U;
	static constexpr uint32_t GOLD_SMELTING_RECIPE_ID = 1030U;
	static constexpr uint32_t COPPER_SMELTING_RECIPE_ID = 1031U;
	static constexpr uint32_t BEEF_COOKING_RECIPE_ID = 1032U;
	static constexpr uint32_t PORK_COOKING_RECIPE_ID = 1033U;
	static constexpr uint32_t CHICKEN_COOKING_RECIPE_ID = 1034U;
	static constexpr uint32_t MUTTON_COOKING_RECIPE_ID = 1035U;
	static constexpr int32_t DEFAULT_PROCESSING_FUEL_ITEM_ID =
		MinecraftItemCatalog::COAL_ITEM_ID;
	static constexpr uint32_t DEFAULT_PROCESSING_FUEL_UNITS_PER_ITEM = 1U;
	static constexpr uint32_t DEFAULT_PROCESSING_TICKS_PER_ITEM = 200U;

	MinecraftRecipeCatalog();
	MinecraftRecipeCatalog(const MinecraftRecipeCatalog &other) = delete;
	~MinecraftRecipeCatalog();
	MinecraftRecipeCatalog &operator=(
		const MinecraftRecipeCatalog &other) = delete;

	int32_t initialize(const MinecraftItemCatalog &items) noexcept;
	/* Configure a stable processing recipe before initialize(); the selected
	 * fuel must be a registered item by catalog initialization time. */
	int32_t configure_processing_recipe_policy(uint32_t recipe_id,
		int32_t fuel_item_id,
		uint32_t fuel_units_per_item, uint32_t ticks_per_item) noexcept;
	int32_t destroy() noexcept;
	bool is_initialized() const noexcept;
	int32_t unlock_recipe(uint32_t recipe_id, uint64_t source_event_id,
		ft_bool *newly_unlocked) noexcept;
	/* Trusted content maps stable discovery source IDs to recipe IDs. Source
	 * event IDs remain the idempotency key stored by the recipe book. */
	int32_t register_discovery_source(uint32_t source_type_id,
		uint32_t source_id, uint32_t recipe_id) noexcept;
	int32_t grant_recipe_from_discovery(uint32_t source_type_id,
		uint32_t source_id, uint64_t source_event_id,
		ft_bool *newly_unlocked) noexcept;
	int32_t serialize_recipe_book(std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t restore_recipe_book(const uint8_t *bytes,
		std::size_t byte_count) noexcept;
	int32_t is_recipe_known(uint32_t recipe_id, ft_bool *known) const noexcept;
	int32_t import_book(ft_byte_buffer &input) noexcept;
	int32_t commit_unique_craft(game_inventory &inventory,
		game_crafting_prepared *prepared,
		uint64_t *instance_id_out) noexcept;
	int32_t query_hand_recipe(const game_inventory &inventory,
		uint32_t recipe_id, game_crafting_query_result *result,
		ft_vector<game_crafting_missing_ingredient> *missing_groups) const noexcept;
	int32_t query_recipe_at_station(const game_inventory &inventory,
		uint32_t recipe_id, StationId station_id,
		game_crafting_query_result *result,
		ft_vector<game_crafting_missing_ingredient> *missing_groups) const noexcept;
	/* Returns all catalog entries, including undiscovered recipes, or one exact
	 * recipe when recipe_id_filter is nonzero. Both outputs are transactional. */
	int32_t query_recipes(const game_inventory &inventory,
		uint32_t recipe_id_filter, StationId station_id,
		std::vector<MinecraftRecipeQueryEntry> &entries_out,
		std::vector<MinecraftRecipeMissingGroup> &missing_out) const noexcept;
	/* Simulation-owner-thread API. Station-constrained recipes fail closed. */
	int32_t craft_hand_recipe(game_inventory &inventory,
		uint64_t request_id, uint32_t recipe_id, int32_t batch_count,
		uint64_t expected_inventory_revision,
		MinecraftCraftActionResult *result_out) noexcept;
	int32_t craft_recipe_at_station(game_inventory &inventory,
		uint64_t request_id, uint32_t recipe_id, int32_t batch_count,
		uint64_t expected_inventory_revision, StationId station_id,
		int32_t station_x, int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision,
		MinecraftCraftActionResult *result_out) noexcept;
	int32_t serialize_craft_action_cache(
		std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t validate_craft_action_cache(const uint8_t *bytes,
		std::size_t byte_count) const noexcept;
	int32_t restore_craft_action_cache(const uint8_t *bytes,
		std::size_t byte_count) noexcept;
	int32_t clear_craft_action_cache() noexcept;
	/* Authoritative simulation-owner-thread furnace queue. The configured fuel
	 * quantity is reserved per processed item; duration is configured in server
	 * ticks. Completed output waits in the job if inventory is full. */
	int32_t start_processing(game_inventory &inventory, uint64_t request_id,
		uint32_t recipe_id, int32_t batch_count,
		uint64_t expected_inventory_revision,
		int32_t station_x, int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision,
		MinecraftProcessingActionResult *result_out) noexcept;
	struct ProcessingStationContext
	{
		uint64_t request_id;
		int32_t station_x;
		int32_t station_y;
		int32_t station_z;
		uint64_t expected_station_revision;
	};
	int32_t get_processing_station_contexts(
		std::vector<ProcessingStationContext> &contexts_out) const noexcept;
	int32_t advance_processing(game_inventory &inventory,
		uint32_t server_ticks, uint32_t job_budget,
		uint32_t *completed_out, uint32_t *pending_output_out) noexcept;
	int32_t get_processing_job_count(uint32_t *count_out) const noexcept;
	int32_t serialize_processing_state(
		std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t validate_processing_state(const uint8_t *bytes,
		std::size_t byte_count) const noexcept;
	int32_t restore_processing_state(const uint8_t *bytes,
		std::size_t byte_count) noexcept;
	int32_t clear_processing_state() noexcept;
	int32_t validate_recipe_coverage(const MinecraftItemCatalog &items,
		uint32_t *recipe_output_count_out,
		int32_t *missing_recipe_item_id_out) const noexcept;
	/* Allocator reads and reservations are synchronized. Callers still need a
	 * session transaction boundary while scanning IDs from other containers. */
	uint64_t next_output_instance_id() const noexcept;
	int32_t allocate_output_instance_id(uint64_t *instance_id_out) noexcept;
	int32_t ensure_next_output_instance_id_above(
		uint64_t maximum_existing_instance_id) noexcept;
	int32_t restore_next_output_instance_id(uint64_t next_instance_id) noexcept;
	game_crafting &crafting() noexcept;
	game_recipe_book &recipe_book() noexcept;
	const game_recipe_book &recipe_book() const noexcept;

  private:
	game_crafting crafting_;
	game_recipe_book recipe_book_;
	std::vector<uint32_t> registered_recipe_ids_;
	std::vector<int32_t> recipe_output_item_ids_;
	std::map<uint64_t, uint32_t> discovery_sources_;
	struct CachedCraftAction
	{
		uint32_t recipe_id;
		int32_t batch_count;
		uint64_t expected_inventory_revision;
		StationId station_id;
		int32_t station_x;
		int32_t station_y;
		int32_t station_z;
		uint64_t expected_station_revision;
		MinecraftCraftActionResult result;
	};
	std::map<uint64_t, CachedCraftAction> craft_action_cache_;
	struct ProcessingJob
	{
		uint32_t recipe_id;
		int32_t batch_count;
		uint32_t ticks_remaining;
		int32_t station_x;
		int32_t station_y;
		int32_t station_z;
		uint64_t expected_station_revision;
		game_crafting_output_snapshot output;
	};
	std::map<uint64_t, ProcessingJob> processing_jobs_;
	struct CachedProcessingAction
	{
		uint32_t recipe_id;
		int32_t batch_count;
		uint64_t expected_inventory_revision;
		MinecraftProcessingActionResult result;
	};
	std::map<uint64_t, CachedProcessingAction> processing_action_cache_;
	uint64_t highest_processing_request_id_;
	int32_t processing_fuel_item_ids_[7];
	uint32_t processing_fuel_units_per_item_[7];
	uint32_t processing_ticks_per_item_[7];
	const MinecraftItemCatalog *items_;
	uint64_t highest_craft_request_id_;
	/* Protects allocator high-water updates and unique output commits. */
	mutable std::mutex instance_id_mutex_;
	uint64_t next_output_instance_id_;
	bool initialized_;

	int32_t register_bootstrap_recipes(
		const MinecraftItemCatalog &items) noexcept;
	int32_t craft_recipe_at_station_internal(game_inventory &inventory,
		uint64_t request_id, uint32_t recipe_id, int32_t batch_count,
		uint64_t expected_inventory_revision, StationId station_id,
		int32_t station_x, int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision,
		MinecraftCraftActionResult *result_out) noexcept;
};

#endif
