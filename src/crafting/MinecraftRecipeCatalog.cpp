#include "../../src/crafting/MinecraftRecipeCatalog.hpp"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <new>
#include <utility>

namespace
{
	static constexpr uint32_t FIRST_PROCESSING_RECIPE_ID = 1029U;
	static constexpr uint32_t LAST_PROCESSING_RECIPE_ID = 1035U;

	static void minecraft_recipe_cache_append_u32(
		std::vector<uint8_t> &bytes, uint32_t value)
	{
		for (uint32_t shift = 0U; shift < 32U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	static void minecraft_recipe_cache_append_u64(
		std::vector<uint8_t> &bytes, uint64_t value)
	{
		for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	static uint64_t minecraft_recipe_cache_checksum(const uint8_t *bytes,
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

	static bool minecraft_recipe_cache_read_u32(const uint8_t *bytes,
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

	static bool minecraft_recipe_cache_read_u64(const uint8_t *bytes,
		std::size_t byte_count, std::size_t *offset, uint64_t *value) noexcept
	{
		uint32_t low;
		uint32_t high;

		if (!minecraft_recipe_cache_read_u32(bytes, byte_count, offset, &low)
			|| !minecraft_recipe_cache_read_u32(bytes, byte_count, offset, &high))
			return (false);
		*value = static_cast<uint64_t>(low)
			| (static_cast<uint64_t>(high) << 32U);
		return (true);
	}

	static void minecraft_processing_append_product(std::vector<uint8_t> &bytes,
		const game_crafting_product_snapshot &product)
	{
		minecraft_recipe_cache_append_u32(bytes,
			static_cast<uint32_t>(product.item_id));
		minecraft_recipe_cache_append_u32(bytes,
			static_cast<uint32_t>(product.quantity));
		minecraft_recipe_cache_append_u32(bytes,
			static_cast<uint32_t>(product.max_stack));
		minecraft_recipe_cache_append_u32(bytes,
			static_cast<uint32_t>(product.rarity));
		minecraft_recipe_cache_append_u32(bytes,
			static_cast<uint32_t>(product.width));
		minecraft_recipe_cache_append_u32(bytes,
			static_cast<uint32_t>(product.height));
		minecraft_recipe_cache_append_u32(bytes,
			static_cast<uint32_t>(product.modifier1_id));
		minecraft_recipe_cache_append_u32(bytes,
			static_cast<uint32_t>(product.modifier1_value));
		minecraft_recipe_cache_append_u32(bytes,
			static_cast<uint32_t>(product.modifier2_id));
		minecraft_recipe_cache_append_u32(bytes,
			static_cast<uint32_t>(product.modifier2_value));
		minecraft_recipe_cache_append_u32(bytes,
			static_cast<uint32_t>(product.modifier3_id));
		minecraft_recipe_cache_append_u32(bytes,
			static_cast<uint32_t>(product.modifier3_value));
		minecraft_recipe_cache_append_u32(bytes,
			static_cast<uint32_t>(product.modifier4_id));
		minecraft_recipe_cache_append_u32(bytes,
			static_cast<uint32_t>(product.modifier4_value));
		minecraft_recipe_cache_append_u64(bytes, product.instance_id);
	}

	static bool minecraft_processing_read_product(const uint8_t *bytes,
		std::size_t byte_count, std::size_t *offset,
		game_crafting_product_snapshot *product) noexcept
	{
		uint32_t values[14];
		uint32_t index = 0U;
		uint64_t instance_id;
		if (product == nullptr)
			return (false);
		while (index < 14U)
		{
			if (!minecraft_recipe_cache_read_u32(bytes, byte_count,
					offset, &values[index]))
				return (false);
			++index;
		}
		if (!minecraft_recipe_cache_read_u64(bytes, byte_count, offset,
				&instance_id))
			return (false);
		product->item_id = static_cast<int32_t>(values[0]);
		product->quantity = static_cast<int32_t>(values[1]);
		product->max_stack = static_cast<int32_t>(values[2]);
		product->rarity = static_cast<int32_t>(values[3]);
		product->width = static_cast<int32_t>(values[4]);
		product->height = static_cast<int32_t>(values[5]);
		product->modifier1_id = static_cast<int32_t>(values[6]);
		product->modifier1_value = static_cast<int32_t>(values[7]);
		product->modifier2_id = static_cast<int32_t>(values[8]);
		product->modifier2_value = static_cast<int32_t>(values[9]);
		product->modifier3_id = static_cast<int32_t>(values[10]);
		product->modifier3_value = static_cast<int32_t>(values[11]);
		product->modifier4_id = static_cast<int32_t>(values[12]);
		product->modifier4_value = static_cast<int32_t>(values[13]);
		product->instance_id = instance_id;
		return (true);
	}

	struct MinecraftRecipeIngredientSpec
	{
		int32_t item_ids[game_crafting_ingredient::MAX_ALTERNATIVE_ITEM_IDS];
		uint32_t item_id_count;
		int32_t quantity;
		int32_t tag_id;
	};

	MinecraftRecipeIngredientSpec minecraft_recipe_ingredient(
		int32_t item_id, int32_t quantity)
	{
		MinecraftRecipeIngredientSpec result = {};
		result.item_ids[0] = item_id;
		result.item_id_count = 1U;
		result.quantity = quantity;
		return (result);
	}

	MinecraftRecipeIngredientSpec minecraft_recipe_tag(
		int32_t tag_id, int32_t quantity)
	{
		MinecraftRecipeIngredientSpec result = {};
		result.tag_id = tag_id;
		result.quantity = quantity;
		return (result);
	}

	MinecraftRecipeIngredientSpec minecraft_recipe_alternatives(
		std::initializer_list<int32_t> item_ids, int32_t quantity)
	{
		MinecraftRecipeIngredientSpec result = {};
		std::initializer_list<int32_t>::const_iterator iterator = item_ids.begin();

		while (iterator != item_ids.end()
			&& result.item_id_count
				< game_crafting_ingredient::MAX_ALTERNATIVE_ITEM_IDS)
		{
			result.item_ids[result.item_id_count] = *iterator;
			result.item_id_count += 1U;
			++iterator;
		}
		result.quantity = quantity;
		if (iterator != item_ids.end())
			result.item_id_count = 0U;
		return (result);
	}

	int32_t minecraft_block_item_id(uint32_t block_id)
	{
		return (MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
			+ static_cast<int32_t>(block_id));
	}
}

MinecraftRecipeCatalog::MinecraftRecipeCatalog()
	: crafting_(), recipe_book_(), registered_recipe_ids_(),
	  recipe_output_item_ids_(), discovery_sources_(),
	  craft_action_cache_(), processing_jobs_(), processing_action_cache_(),
	  highest_processing_request_id_(0U),
	  items_(nullptr),
	  highest_craft_request_id_(0U),
	  next_output_instance_id_(1U), initialized_(false)
{
	for (uint32_t index = 0U; index < 7U; ++index)
	{
		this->processing_fuel_item_ids_[index] =
			DEFAULT_PROCESSING_FUEL_ITEM_ID;
		this->processing_fuel_units_per_item_[index] =
			DEFAULT_PROCESSING_FUEL_UNITS_PER_ITEM;
		this->processing_ticks_per_item_[index] =
			DEFAULT_PROCESSING_TICKS_PER_ITEM;
	}
}

int32_t MinecraftRecipeCatalog::configure_processing_recipe_policy(
	uint32_t recipe_id, int32_t fuel_item_id, uint32_t fuel_units_per_item,
	uint32_t ticks_per_item) noexcept
{
	if (recipe_id < FIRST_PROCESSING_RECIPE_ID
		|| recipe_id > LAST_PROCESSING_RECIPE_ID)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (fuel_item_id <= 0 || fuel_units_per_item == 0U
		|| fuel_units_per_item > 64U || ticks_per_item == 0U
		|| ticks_per_item > 60000U)
		return (FT_ERR_INVALID_ARGUMENT);
	const uint32_t policy_index = recipe_id - FIRST_PROCESSING_RECIPE_ID;
	this->processing_fuel_item_ids_[policy_index] = fuel_item_id;
	this->processing_fuel_units_per_item_[policy_index] =
		fuel_units_per_item;
	this->processing_ticks_per_item_[policy_index] = ticks_per_item;
	return (FT_ERR_SUCCESS);
}

MinecraftRecipeCatalog::~MinecraftRecipeCatalog()
{
	(void)this->destroy();
}

int32_t MinecraftRecipeCatalog::initialize(const MinecraftItemCatalog &items)
	noexcept
{
	uint32_t recipe_output_count = 0U;
	int32_t missing_recipe_item_id = 0;
	int32_t error_code;

	if (this->initialized_ || !items.is_initialized())
		return (FT_ERR_INVALID_STATE);
	error_code = this->crafting_.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->register_bootstrap_recipes(items);
	if (error_code != FT_ERR_SUCCESS)
	{
		(void)this->recipe_book_.destroy();
		(void)this->crafting_.destroy();
		return (error_code);
	}
	this->initialized_ = true;
	this->items_ = &items;
	error_code = this->validate_recipe_coverage(items,
		&recipe_output_count, &missing_recipe_item_id);
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"recipe-catalog: coverage validation failed error=%d missing_item=%d outputs=%u\n",
			error_code, missing_recipe_item_id, recipe_output_count);
		(void)this->destroy();
		return (error_code);
	}
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftRecipeCatalog::register_bootstrap_recipes(
	const MinecraftItemCatalog &items) noexcept
{
	std::vector<uint32_t> recipe_ids;
	std::vector<game_recipe_unlock_record> initial_unlocks;
	std::vector<int32_t> recipe_output_item_ids;
	ft_vector<uint32_t> recipe_id_vector;
	int32_t error_code = FT_ERR_SUCCESS;
	uint64_t initial_event_id = 0x4D43520000000000ULL;

	try
	{
		recipe_ids.reserve(32U);
		initial_unlocks.reserve(32U);
		recipe_output_item_ids.reserve(128U);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	auto add_recipe = [&](uint32_t recipe_id, int32_t output_item_id,
		int32_t output_quantity, StationId station_id, ft_bool known,
		const MinecraftRecipeIngredientSpec *specifications,
		uint32_t specification_count, int32_t returned_item_id,
		int32_t returned_quantity) -> int32_t
	{
		ft_vector<game_crafting_ingredient> ingredients;
		ft_sharedptr<game_item> output;
		std::vector<game_crafting_byproduct> byproducts;
		uint32_t ingredient_index = 0U;
		int32_t result = ingredients.initialize();

		if (recipe_id == 0U || output_quantity <= 0
			|| specifications == nullptr || specification_count == 0U
			|| (known != FT_TRUE && known != FT_FALSE))
			return (FT_ERR_INVALID_ARGUMENT);
		while (ingredient_index < specification_count
			&& result == FT_ERR_SUCCESS)
		{
			game_crafting_ingredient ingredient;
			const MinecraftRecipeIngredientSpec &specification =
				specifications[ingredient_index];

			if ((specification.item_id_count == 0U
				&& specification.tag_id <= 0)
				|| specification.quantity <= 0)
				return (FT_ERR_INVALID_ARGUMENT);
			if (specification.tag_id > 0)
				result = ingredient.initialize_tag(specification.tag_id,
					specification.quantity, -1);
			else
				result = ingredient.initialize_alternatives(
					specification.item_ids, specification.item_id_count,
					specification.quantity, -1);
			if (result == FT_ERR_SUCCESS)
				result = ingredients.push_back(ingredient);
			ingredient_index += 1U;
		}
		if (result == FT_ERR_SUCCESS)
			result = items.create_item(output_item_id, 1, &output);
		if (result == FT_ERR_SUCCESS && returned_item_id > 0)
		{
			game_crafting_byproduct byproduct = {};

			byproduct.item_id = returned_item_id;
			byproduct.quantity = returned_quantity;
			byproduct.max_stack = 16;
			byproduct.width = 1;
			byproduct.height = 1;
			try
			{
				byproducts.push_back(byproduct);
			}
			catch (const std::bad_alloc &)
			{
				return (FT_ERR_NO_MEMORY);
			}
		}
		if (result == FT_ERR_SUCCESS)
			result = this->crafting_.register_recipe(
				static_cast<int32_t>(recipe_id),
				static_cast<ft_vector<game_crafting_ingredient> &&>(ingredients),
				output, output_quantity, byproducts, FT_FALSE);
		if (result == FT_ERR_SUCCESS && station_id != HAND_STATION_ID)
			result = this->crafting_.set_recipe_station(
				static_cast<int32_t>(recipe_id), static_cast<int32_t>(station_id));
		if (result == FT_ERR_SUCCESS)
		{
			try
			{
				recipe_ids.push_back(recipe_id);
				recipe_output_item_ids.push_back(output_item_id);
				if (returned_item_id > 0)
					recipe_output_item_ids.push_back(returned_item_id);
				if (known == FT_TRUE)
				{
					game_recipe_unlock_record unlock = {};
					unlock.recipe_id = recipe_id;
					unlock.source_event_id = initial_event_id
						| static_cast<uint64_t>(recipe_id);
					initial_unlocks.push_back(unlock);
				}
			}
			catch (const std::bad_alloc &)
			{
				return (FT_ERR_NO_MEMORY);
			}
			if (known == FT_TRUE)
				result = this->crafting_.unlock_recipe(
					static_cast<int32_t>(recipe_id));
		}
		return (result);
	};

	std::vector<int32_t> plank_ids;
	std::vector<int32_t> log_ids;
	const int32_t mushroom_id = minecraft_block_item_id(
		VOXEL_GENERATOR_BROWN_MUSHROOM_BLOCK);
	const int32_t red_flower_id = minecraft_block_item_id(
		VOXEL_GENERATOR_RED_FLOWER_BLOCK);
	const int32_t yellow_flower_id = minecraft_block_item_id(
		VOXEL_GENERATOR_YELLOW_FLOWER_BLOCK);
	const int32_t crystal_ids[] = {
		minecraft_block_item_id(VOXEL_GENERATOR_QUARTZ_BLOCK),
		minecraft_block_item_id(VOXEL_GENERATOR_AMETHYST_BLOCK),
		minecraft_block_item_id(VOXEL_GENERATOR_SHIMMER_STONE_BLOCK)};
	std::vector<int32_t> crystal_sample_ids;
	std::vector<int32_t> ore_sample_ids;
	std::vector<int32_t> flower_pigment_ids;
	MinecraftRecipeIngredientSpec ingredient_specs[8] = {};
	error_code = items.resolve_item_tag(MINECRAFT_ITEM_TAG_WOOD_PLANKS,
		plank_ids);
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.resolve_item_tag(MINECRAFT_ITEM_TAG_WOOD_LOGS,
			log_ids);
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.resolve_item_tag(MINECRAFT_ITEM_TAG_CRYSTAL_SAMPLES,
			crystal_sample_ids);
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.resolve_item_tag(MINECRAFT_ITEM_TAG_ORE_SAMPLES,
			ore_sample_ids);
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.resolve_item_tag(MINECRAFT_ITEM_TAG_FLOWER_PIGMENTS,
			flower_pigment_ids);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = this->crafting_.register_item_tag(
		static_cast<int32_t>(MINECRAFT_ITEM_TAG_WOOD_LOGS), log_ids.data(),
		static_cast<uint32_t>(log_ids.size()));
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->crafting_.register_item_tag(
			static_cast<int32_t>(MINECRAFT_ITEM_TAG_WOOD_PLANKS),
			plank_ids.data(), static_cast<uint32_t>(plank_ids.size()));
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->crafting_.register_item_tag(
			static_cast<int32_t>(MINECRAFT_ITEM_TAG_CRYSTAL_SAMPLES),
			crystal_sample_ids.data(),
			static_cast<uint32_t>(crystal_sample_ids.size()));
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->crafting_.register_item_tag(
			static_cast<int32_t>(MINECRAFT_ITEM_TAG_ORE_SAMPLES),
			ore_sample_ids.data(),
			static_cast<uint32_t>(ore_sample_ids.size()));
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->crafting_.register_item_tag(
			static_cast<int32_t>(MINECRAFT_ITEM_TAG_FLOWER_PIGMENTS),
			flower_pigment_ids.data(),
			static_cast<uint32_t>(flower_pigment_ids.size()));
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);

	ingredient_specs[0] = minecraft_recipe_ingredient(log_ids[0], 1);
	const MinecraftRecipeIngredientSpec oak_plank_recipe[] = {ingredient_specs[0]};
	error_code = add_recipe(1001U, MinecraftItemCatalog::OAK_PLANKS_ITEM_ID,
		4, HAND_STATION_ID, FT_TRUE, oak_plank_recipe, 1U, 0, 0);
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(log_ids[1], 1);
		const MinecraftRecipeIngredientSpec pine_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1002U, MinecraftItemCatalog::PINE_PLANKS_ITEM_ID,
			4, HAND_STATION_ID, FT_TRUE, pine_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(log_ids[2], 1);
		const MinecraftRecipeIngredientSpec birch_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1003U, MinecraftItemCatalog::BIRCH_PLANKS_ITEM_ID,
			4, HAND_STATION_ID, FT_TRUE, birch_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_tag(
			static_cast<int32_t>(MINECRAFT_ITEM_TAG_WOOD_PLANKS), 2);
		const MinecraftRecipeIngredientSpec sticks_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1004U, MinecraftItemCatalog::STICKS_ITEM_ID,
			4, HAND_STATION_ID, FT_TRUE, sticks_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_tag(
			static_cast<int32_t>(MINECRAFT_ITEM_TAG_WOOD_PLANKS), 4);
		const MinecraftRecipeIngredientSpec workbench_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1005U, MinecraftItemCatalog::WORKBENCH_ITEM_ID,
			1, HAND_STATION_ID, FT_TRUE, workbench_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(
			minecraft_block_item_id(VOXEL_GENERATOR_STONE_BLOCK), 8);
		const MinecraftRecipeIngredientSpec furnace_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1006U, MinecraftItemCatalog::FURNACE_ITEM_ID,
			1, WORKBENCH_STATION_ID, FT_TRUE, furnace_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_tag(
			static_cast<int32_t>(MINECRAFT_ITEM_TAG_WOOD_PLANKS), 8);
		const MinecraftRecipeIngredientSpec chest_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1007U, MinecraftItemCatalog::CHEST_ITEM_ID,
			1, WORKBENCH_STATION_ID, FT_TRUE, chest_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::STICKS_ITEM_ID, 7);
		const MinecraftRecipeIngredientSpec ladder_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1008U, MinecraftItemCatalog::LADDER_ITEM_ID,
			3, WORKBENCH_STATION_ID, FT_TRUE, ladder_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_tag(
			static_cast<int32_t>(MINECRAFT_ITEM_TAG_WOOD_PLANKS), 6);
		const MinecraftRecipeIngredientSpec trapdoor_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1009U, MinecraftItemCatalog::TRAPDOOR_ITEM_ID,
			2, WORKBENCH_STATION_ID, FT_TRUE, trapdoor_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(
			minecraft_block_item_id(VOXEL_GENERATOR_SAND_BLOCK), 4);
		const MinecraftRecipeIngredientSpec sandstone_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1010U, MinecraftItemCatalog::SANDSTONE_ITEM_ID,
			1, HAND_STATION_ID, FT_TRUE, sandstone_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::OAK_PLANKS_ITEM_ID, 5);
		const MinecraftRecipeIngredientSpec boat_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1011U, MinecraftItemCatalog::BOAT_ITEM_ID,
			1, WORKBENCH_STATION_ID, FT_TRUE, boat_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::PINE_PLANKS_ITEM_ID, 5);
		const MinecraftRecipeIngredientSpec boat_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1024U, MinecraftItemCatalog::BOAT_ITEM_ID,
			1, WORKBENCH_STATION_ID, FT_TRUE, boat_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::BIRCH_PLANKS_ITEM_ID, 5);
		const MinecraftRecipeIngredientSpec boat_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1025U, MinecraftItemCatalog::BOAT_ITEM_ID,
			1, WORKBENCH_STATION_ID, FT_TRUE, boat_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::IRON_BAR_ITEM_ID, 3);
		const MinecraftRecipeIngredientSpec bucket_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1012U, MinecraftItemCatalog::EMPTY_BUCKET_ITEM_ID,
			1, WORKBENCH_STATION_ID, FT_TRUE, bucket_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_tag(
			static_cast<int32_t>(MINECRAFT_ITEM_TAG_WOOD_PLANKS), 6);
		ingredient_specs[1] = minecraft_recipe_ingredient(
			minecraft_block_item_id(VOXEL_GENERATOR_STONE_BLOCK), 2);
		const MinecraftRecipeIngredientSpec paper_mill_recipe[] = {
			ingredient_specs[0], ingredient_specs[1]};
		error_code = add_recipe(1013U, MinecraftItemCatalog::PAPER_MILL_ITEM_ID,
			1, WORKBENCH_STATION_ID, FT_TRUE, paper_mill_recipe, 2U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::PAPER_REED_FIBER_ITEM_ID, 2);
		const MinecraftRecipeIngredientSpec binder_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1014U, MinecraftItemCatalog::BINDER_ITEM_ID,
			2, PAPER_MILL_STATION_ID, FT_TRUE, binder_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::PAPER_REED_FIBER_ITEM_ID, 3);
		ingredient_specs[1] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::WATER_BUCKET_ITEM_ID, 1);
		const MinecraftRecipeIngredientSpec paper_recipe[] = {
			ingredient_specs[0], ingredient_specs[1]};
		error_code = add_recipe(1015U, MinecraftItemCatalog::PAPER_SHEET_ITEM_ID,
			3, PAPER_MILL_STATION_ID, FT_TRUE, paper_recipe, 2U,
			MinecraftItemCatalog::EMPTY_BUCKET_ITEM_ID, 1);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::PAPER_SHEET_ITEM_ID, 3);
		ingredient_specs[1] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::BINDER_ITEM_ID, 1);
		const MinecraftRecipeIngredientSpec card_stock_recipe[] = {
			ingredient_specs[0], ingredient_specs[1]};
		error_code = add_recipe(1016U, MinecraftItemCatalog::CARD_STOCK_ITEM_ID,
			3, PAPER_MILL_STATION_ID, FT_TRUE, card_stock_recipe, 2U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_alternatives(
			{crystal_ids[0], crystal_ids[1],
				minecraft_block_item_id(VOXEL_GENERATOR_AMBER_BLOCK)}, 1);
		ingredient_specs[1] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::BINDER_ITEM_ID, 1);
		ingredient_specs[2] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::WATER_BUCKET_ITEM_ID, 1);
		const MinecraftRecipeIngredientSpec advanced_ink_recipe[] = {
			ingredient_specs[0], ingredient_specs[1], ingredient_specs[2]};
		error_code = add_recipe(1026U, MinecraftItemCatalog::ADVANCED_INK_ITEM_ID,
			2, PAPER_MILL_STATION_ID, FT_FALSE, advanced_ink_recipe, 3U,
			MinecraftItemCatalog::EMPTY_BUCKET_ITEM_ID, 1);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(mushroom_id, 1);
		ingredient_specs[1] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::BINDER_ITEM_ID, 1);
		ingredient_specs[2] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::WATER_BUCKET_ITEM_ID, 1);
		const MinecraftRecipeIngredientSpec dark_ink_recipe[] = {
			ingredient_specs[0], ingredient_specs[1], ingredient_specs[2]};
		error_code = add_recipe(1017U, MinecraftItemCatalog::DARK_INK_ITEM_ID,
			2, PAPER_MILL_STATION_ID, FT_TRUE, dark_ink_recipe, 3U,
			MinecraftItemCatalog::EMPTY_BUCKET_ITEM_ID, 1);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_tag(
			static_cast<int32_t>(MINECRAFT_ITEM_TAG_FLOWER_PIGMENTS), 1);
		ingredient_specs[1] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::BINDER_ITEM_ID, 1);
		ingredient_specs[2] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::WATER_BUCKET_ITEM_ID, 1);
		const MinecraftRecipeIngredientSpec colored_ink_recipe[] = {
			ingredient_specs[0], ingredient_specs[1], ingredient_specs[2]};
		error_code = add_recipe(1018U, MinecraftItemCatalog::COLORED_INK_ITEM_ID,
			2, PAPER_MILL_STATION_ID, FT_TRUE, colored_ink_recipe, 3U,
			MinecraftItemCatalog::EMPTY_BUCKET_ITEM_ID, 1);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_tag(
			static_cast<int32_t>(MINECRAFT_ITEM_TAG_WOOD_PLANKS), 4);
		ingredient_specs[1] = minecraft_recipe_ingredient(
			minecraft_block_item_id(VOXEL_GENERATOR_STONE_BLOCK), 4);
		ingredient_specs[2] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::BINDER_ITEM_ID, 2);
		const MinecraftRecipeIngredientSpec card_press_recipe[] = {
			ingredient_specs[0], ingredient_specs[1], ingredient_specs[2]};
		error_code = add_recipe(1021U, MinecraftItemCatalog::CARD_PRESS_ITEM_ID,
			1, WORKBENCH_STATION_ID, FT_TRUE, card_press_recipe, 3U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_tag(
			static_cast<int32_t>(MINECRAFT_ITEM_TAG_WOOD_PLANKS), 6);
		ingredient_specs[1] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::CARD_STOCK_ITEM_ID, 2);
		ingredient_specs[2] = minecraft_recipe_ingredient(
			minecraft_block_item_id(VOXEL_GENERATOR_STONE_BLOCK), 2);
		const MinecraftRecipeIngredientSpec world_loom_recipe[] = {
			ingredient_specs[0], ingredient_specs[1], ingredient_specs[2]};
		error_code = add_recipe(1022U, MinecraftItemCatalog::WORLD_LOOM_ITEM_ID,
			1, WORKBENCH_STATION_ID, FT_TRUE, world_loom_recipe, 3U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_tag(
			static_cast<int32_t>(MINECRAFT_ITEM_TAG_CRYSTAL_SAMPLES), 1);
		const MinecraftRecipeIngredientSpec dust_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1023U, MinecraftItemCatalog::MAGICAL_DUST_ITEM_ID,
			2, WORKBENCH_STATION_ID, FT_FALSE, dust_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(ore_sample_ids[3], 1);
		const MinecraftRecipeIngredientSpec diamond_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1027U,
			MinecraftItemCatalog::DIAMOND_MATERIAL_ITEM_ID, 1,
			WORKBENCH_STATION_ID, FT_FALSE, diamond_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(ore_sample_ids[4], 1);
		const MinecraftRecipeIngredientSpec emerald_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(1028U,
			MinecraftItemCatalog::EMERALD_MATERIAL_ITEM_ID, 1,
			WORKBENCH_STATION_ID, FT_FALSE, emerald_recipe, 1U, 0, 0);
	}
	const int32_t smelt_input_ids[] = {
		ore_sample_ids[1], ore_sample_ids[2], ore_sample_ids[5],
		MinecraftItemCatalog::RAW_BEEF_ITEM_ID,
		MinecraftItemCatalog::RAW_PORK_ITEM_ID,
		MinecraftItemCatalog::RAW_CHICKEN_ITEM_ID,
		MinecraftItemCatalog::RAW_MUTTON_ITEM_ID};
	const int32_t smelt_output_ids[] = {
		MinecraftItemCatalog::IRON_BAR_ITEM_ID,
		MinecraftItemCatalog::GOLD_BAR_ITEM_ID,
		MinecraftItemCatalog::COPPER_BAR_ITEM_ID,
		MinecraftItemCatalog::COOKED_BEEF_ITEM_ID,
		MinecraftItemCatalog::COOKED_PORK_ITEM_ID,
		MinecraftItemCatalog::COOKED_CHICKEN_ITEM_ID,
		MinecraftItemCatalog::COOKED_MUTTON_ITEM_ID};
	for (uint32_t smelt_index = 0U;
		smelt_index < sizeof(smelt_input_ids) / sizeof(smelt_input_ids[0])
			&& error_code == FT_ERR_SUCCESS; ++smelt_index)
	{
		MinecraftContentItemRule processing_fuel_rule = {};
		error_code = items.resolve_content_item_rule(
			this->processing_fuel_item_ids_[smelt_index],
			&processing_fuel_rule);
		if (error_code == FT_ERR_SUCCESS
			&& processing_fuel_rule.max_stack < static_cast<int32_t>(
				this->processing_fuel_units_per_item_[smelt_index]))
			error_code = FT_ERR_CONFIGURATION;
		ingredient_specs[0] = minecraft_recipe_ingredient(
			smelt_input_ids[smelt_index], 1);
		ingredient_specs[1] = minecraft_recipe_ingredient(
			this->processing_fuel_item_ids_[smelt_index],
			static_cast<int32_t>(
				this->processing_fuel_units_per_item_[smelt_index]));
		const MinecraftRecipeIngredientSpec smelt_recipe[] = {
			ingredient_specs[0], ingredient_specs[1]};
		if (error_code == FT_ERR_SUCCESS)
			error_code = add_recipe(FIRST_PROCESSING_RECIPE_ID + smelt_index,
				smelt_output_ids[smelt_index], 1, FURNACE_STATION_ID,
				FT_TRUE, smelt_recipe, 2U, 0, 0);
	}
	struct CardRecipeSpec
	{
		int32_t card_item_id;
		int32_t catalyst_item_id;
		int32_t catalyst_quantity;
		int32_t second_item_id;
		int32_t second_quantity;
		int32_t third_item_id;
		int32_t third_quantity;
		bool rare;
	};
	const CardRecipeSpec card_recipes[] = {
		{MinecraftItemCatalog::BIOME_EXCLUSION_CARD_ITEM_ID,
			minecraft_block_item_id(VOXEL_GENERATOR_SNOW_BLOCK), 1, 0, 0, 0, 0, false},
		{MinecraftItemCatalog::DEEP_SEAM_CARD_ITEM_ID,
			ore_sample_ids[1], 1, 0, 0, 0, 0, false},
		{MinecraftItemCatalog::VILLAGE_CHARTER_CARD_ITEM_ID,
			MinecraftItemCatalog::WHEAT_ITEM_ID, 1,
			MinecraftItemCatalog::OAK_PLANKS_ITEM_ID, 2, 0, 0, false},
		{MinecraftItemCatalog::SURFACE_MAGMA_CARD_ITEM_ID,
			minecraft_block_item_id(VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK),
			1, 0, 0, 0, 0, false},
		{MinecraftItemCatalog::DOUBLE_DRAFT_CARD_ITEM_ID,
			MinecraftItemCatalog::PAPER_SHEET_ITEM_ID, 2, 0, 0, 0, 0, false},
		{MinecraftItemCatalog::BIOME_COMPASS_CARD_ITEM_ID,
			red_flower_id, 1, 0, 0, 0, 0, false},
		{MinecraftItemCatalog::SURVEYORS_REACH_CARD_ITEM_ID,
			minecraft_block_item_id(VOXEL_GENERATOR_STONE_BLOCK),
			2, 0, 0, 0, 0, false},
		{MinecraftItemCatalog::VOLCANO_MARK_CARD_ITEM_ID,
			minecraft_block_item_id(VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK),
			4, minecraft_block_item_id(VOXEL_GENERATOR_OBSIDIAN_BLOCK), 2,
			0, 0, true},
		{MinecraftItemCatalog::ULTIMATE_GREED_CARD_ITEM_ID,
			MinecraftItemCatalog::GOLD_BAR_ITEM_ID, 2,
			minecraft_block_item_id(VOXEL_GENERATOR_AMBER_BLOCK), 1, 0, 0, true},
		{MinecraftItemCatalog::BIOME_RUPTURE_CARD_ITEM_ID,
			minecraft_block_item_id(VOXEL_GENERATOR_SAND_BLOCK), 1,
			minecraft_block_item_id(VOXEL_GENERATOR_SNOW_BLOCK), 1, 0, 0, true},
		{MinecraftItemCatalog::ORE_OVERGROWTH_CARD_ITEM_ID,
			ore_sample_ids[1], 2,
			MinecraftItemCatalog::EMERALD_MATERIAL_ITEM_ID, 1, 0, 0, true},
		{MinecraftItemCatalog::SHIPWRECK_CHART_CARD_ITEM_ID,
			MinecraftItemCatalog::OAK_PLANKS_ITEM_ID, 2,
			MinecraftItemCatalog::COPPER_BAR_ITEM_ID, 1,
			MinecraftItemCatalog::PAPER_REED_FIBER_ITEM_ID, 1, true},
		{MinecraftItemCatalog::RUINED_VILLAGE_RELIC_CARD_ITEM_ID,
			minecraft_block_item_id(VOXEL_GENERATOR_CRACKED_STONE_BLOCK), 2,
			MinecraftItemCatalog::IRON_BAR_ITEM_ID, 1, 0, 0, true},
		{MinecraftItemCatalog::UNSTABLE_CATALYST_CARD_ITEM_ID,
			minecraft_block_item_id(VOXEL_GENERATOR_SHIMMER_STONE_BLOCK), 2,
			crystal_ids[1], 1, 0, 0, true},
		{MinecraftItemCatalog::DESERT_PYRAMID_SEAL_CARD_ITEM_ID,
			MinecraftItemCatalog::SANDSTONE_ITEM_ID, 4,
			MinecraftItemCatalog::GOLD_BAR_ITEM_ID, 1, 0, 0, true},
		{MinecraftItemCatalog::ANCIENT_CANOPY_SEAL_CARD_ITEM_ID,
			log_ids[0], 4, MinecraftItemCatalog::LADDER_ITEM_ID, 1, 0, 0, true},
		{MinecraftItemCatalog::SPIDER_NEST_SEAL_CARD_ITEM_ID,
			MinecraftItemCatalog::PLANT_FIBER_ITEM_ID, 4, mushroom_id, 1,
			0, 0, true},
		{MinecraftItemCatalog::VOLATILE_STABILIZER_CARD_ITEM_ID,
			minecraft_block_item_id(VOXEL_GENERATOR_AMBER_BLOCK), 1,
			0, 0, 0, 0, true},
		{MinecraftItemCatalog::FORTUNES_DRAW_CARD_ITEM_ID,
			MinecraftItemCatalog::PAPER_SHEET_ITEM_ID, 1,
			0, 0, 0, 0, true}};
	for (uint32_t card_index = 0U;
		card_index < sizeof(card_recipes) / sizeof(card_recipes[0])
			&& error_code == FT_ERR_SUCCESS; ++card_index)
	{
		const CardRecipeSpec &card = card_recipes[card_index];
		const uint32_t recipe_id = 2001U + card_index;

		ingredient_specs[0] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::CARD_STOCK_ITEM_ID, card.rare ? 2 : 1);
		ingredient_specs[1] = minecraft_recipe_alternatives(
			card.rare
				? std::initializer_list<int32_t>{
					MinecraftItemCatalog::ADVANCED_INK_ITEM_ID}
				: std::initializer_list<int32_t>{
					MinecraftItemCatalog::DARK_INK_ITEM_ID,
					MinecraftItemCatalog::COLORED_INK_ITEM_ID}, 1);
		if (!card.rare)
		{
			ingredient_specs[2] = minecraft_recipe_ingredient(
				MinecraftItemCatalog::BINDER_ITEM_ID, 1);
		}
		else
		{
			ingredient_specs[2] = minecraft_recipe_ingredient(
				minecraft_block_item_id(VOXEL_GENERATOR_QUARTZ_BLOCK), 2);
		}
		if (card_index == 0U)
			ingredient_specs[3] = minecraft_recipe_alternatives(
				{minecraft_block_item_id(VOXEL_GENERATOR_SAND_BLOCK),
					minecraft_block_item_id(VOXEL_GENERATOR_DIRT_BLOCK),
					minecraft_block_item_id(VOXEL_GENERATOR_SNOW_BLOCK)}, 1);
		else if (card_index == 1U || card_index == 10U)
			ingredient_specs[3] = minecraft_recipe_alternatives(
				{ore_sample_ids[0], ore_sample_ids[1], ore_sample_ids[2],
					ore_sample_ids[3], ore_sample_ids[4], ore_sample_ids[5]},
				card.catalyst_quantity);
		else if (card_index == 3U)
			ingredient_specs[3] = minecraft_recipe_alternatives(
				{minecraft_block_item_id(VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK),
					minecraft_block_item_id(VOXEL_GENERATOR_STONE_BLOCK)}, 1);
		else if (card_index == 5U)
			ingredient_specs[3] = minecraft_recipe_alternatives(
				{red_flower_id, yellow_flower_id}, 1);
		else
			ingredient_specs[3] = minecraft_recipe_ingredient(
				card.catalyst_item_id, card.catalyst_quantity);
		uint32_t card_ingredient_count = 4U;
		if (card.second_item_id > 0)
		{
			ingredient_specs[card_ingredient_count] =
				minecraft_recipe_ingredient(card.second_item_id,
					card.second_quantity);
			card_ingredient_count += 1U;
		}
		if (card.third_item_id > 0)
		{
			ingredient_specs[card_ingredient_count] =
				minecraft_recipe_ingredient(card.third_item_id,
					card.third_quantity);
			card_ingredient_count += 1U;
		}
		MinecraftRecipeIngredientSpec card_recipe[6] = {};
		for (uint32_t ingredient_index = 0U;
			ingredient_index < card_ingredient_count; ++ingredient_index)
			card_recipe[ingredient_index] = ingredient_specs[ingredient_index];
		error_code = add_recipe(recipe_id, card.card_item_id, 1,
			CARD_PRESS_STATION_ID, card.rare ? FT_FALSE : FT_TRUE,
			card_recipe, card_ingredient_count, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::WHEAT_ITEM_ID, 3);
		const MinecraftRecipeIngredientSpec bread_recipe[] = {ingredient_specs[0]};
		error_code = add_recipe(3001U, MinecraftItemCatalog::BREAD_ITEM_ID,
			1, HAND_STATION_ID, FT_TRUE, bread_recipe, 1U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::STICKS_ITEM_ID, 3);
		ingredient_specs[1] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::PLANT_FIBER_ITEM_ID, 3);
		const MinecraftRecipeIngredientSpec bow_recipe[] = {
			ingredient_specs[0], ingredient_specs[1]};
		error_code = add_recipe(3002U, MinecraftItemCatalog::BOW_ITEM_ID,
			1, WORKBENCH_STATION_ID, FT_TRUE, bow_recipe, 2U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(
			minecraft_block_item_id(VOXEL_GENERATOR_GRAVEL_BLOCK), 1);
		ingredient_specs[1] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::STICKS_ITEM_ID, 1);
		ingredient_specs[2] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::FEATHER_ITEM_ID, 1);
		const MinecraftRecipeIngredientSpec arrow_recipe[] = {
			ingredient_specs[0], ingredient_specs[1], ingredient_specs[2]};
		error_code = add_recipe(3003U, MinecraftItemCatalog::ARROW_ITEM_ID,
			4, WORKBENCH_STATION_ID, FT_TRUE, arrow_recipe, 3U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_tag(
			static_cast<int32_t>(MINECRAFT_ITEM_TAG_WOOD_PLANKS), 6);
		ingredient_specs[1] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::IRON_BAR_ITEM_ID, 1);
		const MinecraftRecipeIngredientSpec shield_recipe[] = {
			ingredient_specs[0], ingredient_specs[1]};
		error_code = add_recipe(3004U, MinecraftItemCatalog::SHIELD_ITEM_ID,
			1, WORKBENCH_STATION_ID, FT_FALSE, shield_recipe, 2U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::STICKS_ITEM_ID, 3);
		ingredient_specs[1] = minecraft_recipe_ingredient(crystal_ids[1], 1);
		const MinecraftRecipeIngredientSpec staff_recipe[] = {
			ingredient_specs[0], ingredient_specs[1]};
		error_code = add_recipe(3005U, MinecraftItemCatalog::STAFF_ITEM_ID,
			1, WORKBENCH_STATION_ID, FT_FALSE, staff_recipe, 2U, 0, 0);
	}
	const int32_t tool_material_ids[] = {
		MinecraftItemCatalog::OAK_PLANKS_ITEM_ID,
		minecraft_block_item_id(VOXEL_GENERATOR_STONE_BLOCK),
		MinecraftItemCatalog::IRON_BAR_ITEM_ID,
		MinecraftItemCatalog::GOLD_BAR_ITEM_ID,
		MinecraftItemCatalog::COPPER_BAR_ITEM_ID,
		MinecraftItemCatalog::DIAMOND_MATERIAL_ITEM_ID};
	const int32_t pickaxe_item_ids[] = {
		MinecraftItemCatalog::WOOD_PICKAXE_ITEM_ID,
		MinecraftItemCatalog::STONE_PICKAXE_ITEM_ID,
		MinecraftItemCatalog::IRON_PICKAXE_ITEM_ID,
		MinecraftItemCatalog::GOLD_PICKAXE_ITEM_ID,
		MinecraftItemCatalog::COPPER_PICKAXE_ITEM_ID,
		MinecraftItemCatalog::DIAMOND_PICKAXE_ITEM_ID};
	const int32_t axe_item_ids[] = {
		MinecraftItemCatalog::WOOD_AXE_ITEM_ID,
		MinecraftItemCatalog::STONE_AXE_ITEM_ID,
		MinecraftItemCatalog::IRON_AXE_ITEM_ID,
		MinecraftItemCatalog::GOLD_AXE_ITEM_ID,
		MinecraftItemCatalog::COPPER_AXE_ITEM_ID,
		MinecraftItemCatalog::DIAMOND_AXE_ITEM_ID};
	const int32_t sword_item_ids[] = {
		MinecraftItemCatalog::WOOD_SWORD_ITEM_ID,
		MinecraftItemCatalog::STONE_SWORD_ITEM_ID,
		MinecraftItemCatalog::IRON_SWORD_ITEM_ID,
		MinecraftItemCatalog::GOLD_SWORD_ITEM_ID,
		MinecraftItemCatalog::COPPER_SWORD_ITEM_ID,
		MinecraftItemCatalog::DIAMOND_SWORD_ITEM_ID};
	for (uint32_t tier = 0U; tier < 6U && error_code == FT_ERR_SUCCESS; ++tier)
	{
		const ft_bool unlocked = tier < 2U ? FT_TRUE : FT_FALSE;

		ingredient_specs[0] = minecraft_recipe_ingredient(
			tool_material_ids[tier], 3);
		ingredient_specs[1] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::STICKS_ITEM_ID, 2);
		const MinecraftRecipeIngredientSpec tool_recipe[] = {
			ingredient_specs[0], ingredient_specs[1]};
		error_code = add_recipe(3101U + tier, pickaxe_item_ids[tier], 1,
			WORKBENCH_STATION_ID, unlocked, tool_recipe, 2U, 0, 0);
		if (error_code == FT_ERR_SUCCESS)
			error_code = add_recipe(3111U + tier, axe_item_ids[tier], 1,
				WORKBENCH_STATION_ID, unlocked, tool_recipe, 2U, 0, 0);
		ingredient_specs[0] = minecraft_recipe_ingredient(
			tool_material_ids[tier], 2);
		ingredient_specs[1] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::STICKS_ITEM_ID, 1);
		const MinecraftRecipeIngredientSpec sword_recipe[] = {
			ingredient_specs[0], ingredient_specs[1]};
		if (error_code == FT_ERR_SUCCESS)
			error_code = add_recipe(3121U + tier, sword_item_ids[tier], 1,
				WORKBENCH_STATION_ID, unlocked, sword_recipe, 2U, 0, 0);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ingredient_specs[0] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::OAK_PLANKS_ITEM_ID, 2);
		ingredient_specs[1] = minecraft_recipe_ingredient(
			MinecraftItemCatalog::STICKS_ITEM_ID, 2);
		const MinecraftRecipeIngredientSpec hoe_recipe[] = {
			ingredient_specs[0], ingredient_specs[1]};
		error_code = add_recipe(3131U,
			MinecraftItemCatalog::WOOD_HOE_ITEM_ID, 1,
			WORKBENCH_STATION_ID, FT_TRUE, hoe_recipe, 2U, 0, 0);
	}
	const int32_t armor_materials[] = {
		MinecraftItemCatalog::PLANT_FIBER_ITEM_ID,
		MinecraftItemCatalog::LEATHER_ITEM_ID,
		MinecraftItemCatalog::IRON_BAR_ITEM_ID};
	const int32_t armor_item_ids[3][4] = {
		{MinecraftItemCatalog::CLOTH_HELMET_ITEM_ID,
			MinecraftItemCatalog::CLOTH_CHESTPLATE_ITEM_ID,
			MinecraftItemCatalog::CLOTH_LEGGINGS_ITEM_ID,
			MinecraftItemCatalog::CLOTH_BOOTS_ITEM_ID},
		{MinecraftItemCatalog::LEATHER_HELMET_ITEM_ID,
			MinecraftItemCatalog::LEATHER_CHESTPLATE_ITEM_ID,
			MinecraftItemCatalog::LEATHER_LEGGINGS_ITEM_ID,
			MinecraftItemCatalog::LEATHER_BOOTS_ITEM_ID},
		{MinecraftItemCatalog::IRON_HELMET_ITEM_ID,
			MinecraftItemCatalog::IRON_CHESTPLATE_ITEM_ID,
			MinecraftItemCatalog::IRON_LEGGINGS_ITEM_ID,
			MinecraftItemCatalog::IRON_BOOTS_ITEM_ID}};
	const int32_t armor_quantities[] = {5, 8, 7, 4};
	for (uint32_t material = 0U; material < 3U
		&& error_code == FT_ERR_SUCCESS; ++material)
	{
		const ft_bool unlocked = material < 2U ? FT_TRUE : FT_FALSE;

		for (uint32_t piece = 0U; piece < 4U
			&& error_code == FT_ERR_SUCCESS; ++piece)
		{
			ingredient_specs[0] = minecraft_recipe_ingredient(
				armor_materials[material], armor_quantities[piece]);
			const MinecraftRecipeIngredientSpec armor_recipe[] = {
				ingredient_specs[0]};
			error_code = add_recipe(3201U + material * 10U + piece,
				armor_item_ids[material][piece], 1, WORKBENCH_STATION_ID,
				unlocked, armor_recipe, 1U, 0, 0);
		}
	}
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (recipe_ids.empty() || recipe_ids.size() > UINT32_MAX
		|| initial_unlocks.size() > UINT32_MAX)
		return (FT_ERR_OUT_OF_RANGE);
	error_code = recipe_id_vector.initialize();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	for (uint32_t index = 0U; index < recipe_ids.size(); ++index)
	{
		error_code = recipe_id_vector.push_back(recipe_ids[index]);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
	}
	error_code = this->recipe_book_.initialize(recipe_id_vector,
		initial_unlocks.data(), static_cast<uint32_t>(initial_unlocks.size()));
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->registered_recipe_ids_.swap(recipe_ids);
	this->recipe_output_item_ids_.swap(recipe_output_item_ids);
	return (error_code);
}

int32_t MinecraftRecipeCatalog::destroy() noexcept
{
	int32_t book_error = this->recipe_book_.destroy();
	int32_t crafting_error = this->crafting_.destroy();

	this->registered_recipe_ids_.clear();
	this->recipe_output_item_ids_.clear();
	this->discovery_sources_.clear();
	this->craft_action_cache_.clear();
	this->processing_jobs_.clear();
	this->processing_action_cache_.clear();
	this->highest_processing_request_id_ = 0U;
	this->items_ = nullptr;
	this->highest_craft_request_id_ = 0U;
	this->next_output_instance_id_ = 1U;
	this->initialized_ = false;
	return (book_error != FT_ERR_SUCCESS ? book_error : crafting_error);
}

int32_t MinecraftRecipeCatalog::query_hand_recipe(
	const game_inventory &inventory, uint32_t recipe_id,
	game_crafting_query_result *result,
	ft_vector<game_crafting_missing_ingredient> *missing_groups) const noexcept
{
	if (!this->initialized_ || recipe_id == 0U || result == nullptr
		|| missing_groups == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (this->query_recipe_at_station(inventory, recipe_id,
		HAND_STATION_ID, result, missing_groups));
}

int32_t MinecraftRecipeCatalog::query_recipe_at_station(
	const game_inventory &inventory, uint32_t recipe_id, StationId station_id,
	game_crafting_query_result *result,
	ft_vector<game_crafting_missing_ingredient> *missing_groups) const noexcept
{
	if (!this->initialized_ || recipe_id == 0U || result == nullptr
		|| missing_groups == nullptr
		|| station_id < HAND_STATION_ID || station_id > FURNACE_STATION_ID)
		return (FT_ERR_INVALID_ARGUMENT);
	ft_bool known = FT_FALSE;
	int32_t error_code = this->recipe_book_.is_recipe_known(recipe_id, &known);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->crafting_.query_recipe_at_station(
			static_cast<int32_t>(recipe_id), inventory,
			static_cast<int32_t>(station_id), result, missing_groups);
	if (error_code == FT_ERR_SUCCESS)
	{
		result->known = known;
		if (known != FT_TRUE)
			result->craftable = FT_FALSE;
	}
	return (error_code);
}

int32_t MinecraftRecipeCatalog::query_recipes(const game_inventory &inventory,
	uint32_t recipe_id_filter, StationId station_id,
	std::vector<MinecraftRecipeQueryEntry> &entries_out,
	std::vector<MinecraftRecipeMissingGroup> &missing_out) const noexcept
{
	std::vector<MinecraftRecipeQueryEntry> staged_entries;
	std::vector<MinecraftRecipeMissingGroup> staged_missing;
	if (!this->initialized_ || station_id < HAND_STATION_ID
		|| station_id > FURNACE_STATION_ID)
		return (FT_ERR_INVALID_ARGUMENT);
	if (recipe_id_filter != 0U
		&& std::find(this->registered_recipe_ids_.begin(),
			this->registered_recipe_ids_.end(), recipe_id_filter)
			== this->registered_recipe_ids_.end())
		return (FT_ERR_NOT_FOUND);
	try
	{
		game_inventory_snapshot inventory_snapshot = {};
		int32_t snapshot_result = inventory.get_snapshot(inventory_snapshot);
		if (snapshot_result != FT_ERR_SUCCESS)
			return (snapshot_result);
		staged_entries.reserve(recipe_id_filter == 0U
			? this->registered_recipe_ids_.size() : 1U);
		for (uint32_t recipe_id : this->registered_recipe_ids_)
		{
			if (recipe_id_filter != 0U && recipe_id != recipe_id_filter)
				continue ;
			ft_vector<game_crafting_missing_ingredient> missing_groups;
			int32_t result = missing_groups.initialize();
			game_crafting_query_result status = {};
			ft_bool known = FT_FALSE;
			if (result == FT_ERR_SUCCESS)
				result = this->recipe_book_.is_recipe_known(recipe_id, &known);
			if (result == FT_ERR_SUCCESS)
				result = this->crafting_.query_recipe_at_station_snapshot(
					static_cast<int32_t>(recipe_id), inventory_snapshot,
					static_cast<int32_t>(station_id), &status, &missing_groups);
			if (result == FT_ERR_SUCCESS)
			{
				status.known = known;
				if (known != FT_TRUE)
					status.craftable = FT_FALSE;
				staged_entries.push_back({recipe_id, known, status});
				for (ft_size_t index = 0U; index < missing_groups.size(); ++index)
					staged_missing.push_back({recipe_id, missing_groups[index]});
			}
			const int32_t destroy_result = missing_groups.destroy();
			if (result == FT_ERR_SUCCESS && destroy_result != FT_ERR_SUCCESS)
				result = destroy_result;
			if (result != FT_ERR_SUCCESS)
				return (result);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	entries_out.swap(staged_entries);
	missing_out.swap(staged_missing);
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftRecipeCatalog::craft_hand_recipe(
	game_inventory &inventory, uint64_t request_id, uint32_t recipe_id,
	int32_t batch_count, uint64_t expected_inventory_revision,
	MinecraftCraftActionResult *result_out) noexcept
{
	return (this->craft_recipe_at_station_internal(inventory, request_id,
		recipe_id, batch_count, expected_inventory_revision, HAND_STATION_ID,
		0, 0, 0, 0U, result_out));
}

int32_t MinecraftRecipeCatalog::craft_recipe_at_station(
	game_inventory &inventory, uint64_t request_id, uint32_t recipe_id,
	int32_t batch_count, uint64_t expected_inventory_revision,
	StationId station_id, int32_t station_x, int32_t station_y,
	int32_t station_z, uint64_t expected_station_revision,
	MinecraftCraftActionResult *result_out) noexcept
{
	if (station_id < HAND_STATION_ID || station_id > FURNACE_STATION_ID
		|| (station_id == HAND_STATION_ID
			&& (station_x != 0 || station_y != 0 || station_z != 0
				|| expected_station_revision != 0U))
		|| (station_id != HAND_STATION_ID
			&& expected_station_revision == 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	return (this->craft_recipe_at_station_internal(inventory, request_id,
		recipe_id, batch_count, expected_inventory_revision, station_id,
		station_x, station_y, station_z, expected_station_revision, result_out));
}

int32_t MinecraftRecipeCatalog::craft_recipe_at_station_internal(
	game_inventory &inventory, uint64_t request_id, uint32_t recipe_id,
	int32_t batch_count, uint64_t expected_inventory_revision,
	StationId station_id, int32_t station_x, int32_t station_y,
	int32_t station_z, uint64_t expected_station_revision,
	MinecraftCraftActionResult *result_out) noexcept
{
	std::map<uint64_t, CachedCraftAction>::iterator cached;
	CachedCraftAction pending = {};
	game_crafting_prepared prepared;
	uint64_t current_revision = 0U;
	int32_t error_code;

	if (!this->initialized_ || result_out == nullptr || request_id == 0U
		|| recipe_id == 0U
		|| recipe_id > static_cast<uint32_t>(
			std::numeric_limits<int32_t>::max())
		|| batch_count <= 0)
		return (FT_ERR_INVALID_ARGUMENT);
	cached = this->craft_action_cache_.find(request_id);
	if (cached != this->craft_action_cache_.end())
	{
		if (cached->second.recipe_id != recipe_id
			|| cached->second.batch_count != batch_count
			|| cached->second.expected_inventory_revision
				!= expected_inventory_revision
			|| cached->second.station_id != station_id
			|| cached->second.station_x != station_x
			|| cached->second.station_y != station_y
			|| cached->second.station_z != station_z
			|| cached->second.expected_station_revision
				!= expected_station_revision)
			return (FT_ERR_INVALID_ARGUMENT);
		*result_out = cached->second.result;
		result_out->replayed = true;
		return (result_out->status);
	}
	if (request_id <= this->highest_craft_request_id_)
		return (FT_ERR_NOT_FOUND);
	pending.recipe_id = recipe_id;
	pending.batch_count = batch_count;
	pending.expected_inventory_revision = expected_inventory_revision;
	pending.station_id = station_id;
	pending.station_x = station_x;
	pending.station_y = station_y;
	pending.station_z = station_z;
	pending.expected_station_revision = expected_station_revision;
	pending.result.request_id = request_id;
	pending.result.recipe_id = recipe_id;
	pending.result.batch_count = batch_count;
	pending.result.status = FT_ERR_INVALID_STATE;
	try
	{
		cached = this->craft_action_cache_.emplace(request_id, pending).first;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (this->craft_action_cache_.size() > 4096U)
		this->craft_action_cache_.erase(this->craft_action_cache_.begin());
	this->highest_craft_request_id_ = request_id;
	/* Furnace outputs must pass through the persisted timed-processing queue.
	 * Retain the rejection in the action cache to preserve retry identity. */
	ft_bool recipe_known = FT_FALSE;
	error_code = this->recipe_book_.is_recipe_known(recipe_id,
		&recipe_known);
	if (error_code == FT_ERR_SUCCESS && recipe_known != FT_TRUE)
		error_code = FT_ERR_PERMISSION_DENIED;
	if (error_code == FT_ERR_SUCCESS
		&& recipe_id >= FIRST_PROCESSING_RECIPE_ID
		&& recipe_id <= LAST_PROCESSING_RECIPE_ID)
		error_code = FT_ERR_INVALID_OPERATION;
	else if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.get_revision(&current_revision);
	if (error_code == FT_ERR_SUCCESS
		&& current_revision != expected_inventory_revision)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = prepared.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->crafting_.prepare_craft_at_station(inventory,
			static_cast<int32_t>(recipe_id), static_cast<int32_t>(station_id),
			batch_count,
			&prepared);
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = inventory.get_revision(&current_revision);
		if (error_code == FT_ERR_SUCCESS
			&& current_revision != expected_inventory_revision)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		if (recipe_id >= 2001U && recipe_id <= 2019U)
		{
			uint64_t physical_card_instance_id = 0U;
			error_code = this->commit_unique_craft(inventory, &prepared,
				&physical_card_instance_id);
		}
		else
			error_code = this->crafting_.commit_craft(inventory, &prepared);
	}
	(void)prepared.destroy();
	if (inventory.get_revision(&current_revision) == FT_ERR_SUCCESS)
		cached->second.result.inventory_revision = current_revision;
	cached->second.result.status = error_code;
	*result_out = cached->second.result;
	return (error_code);
}

int32_t MinecraftRecipeCatalog::serialize_craft_action_cache(
	std::vector<uint8_t> &bytes_out) const noexcept
{
	std::vector<uint8_t> staged;

	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	try
	{
		staged.reserve(28U + this->craft_action_cache_.size() * 60U);
		staged.push_back('M');
		staged.push_back('C');
		staged.push_back('R');
		staged.push_back('A');
		minecraft_recipe_cache_append_u32(staged, 2U);
		minecraft_recipe_cache_append_u64(staged,
			this->highest_craft_request_id_);
		minecraft_recipe_cache_append_u32(staged, static_cast<uint32_t>(
			this->craft_action_cache_.size()));
		for (const std::pair<const uint64_t, CachedCraftAction> &entry
			: this->craft_action_cache_)
		{
			minecraft_recipe_cache_append_u64(staged, entry.first);
			minecraft_recipe_cache_append_u32(staged,
				entry.second.recipe_id);
			minecraft_recipe_cache_append_u32(staged,
				static_cast<uint32_t>(entry.second.batch_count));
			minecraft_recipe_cache_append_u64(staged,
				entry.second.expected_inventory_revision);
			minecraft_recipe_cache_append_u32(staged,
				static_cast<uint32_t>(entry.second.station_id));
			minecraft_recipe_cache_append_u32(staged,
				static_cast<uint32_t>(entry.second.station_x));
			minecraft_recipe_cache_append_u32(staged,
				static_cast<uint32_t>(entry.second.station_y));
			minecraft_recipe_cache_append_u32(staged,
				static_cast<uint32_t>(entry.second.station_z));
			minecraft_recipe_cache_append_u64(staged,
				entry.second.expected_station_revision);
			minecraft_recipe_cache_append_u64(staged,
				entry.second.result.inventory_revision);
			minecraft_recipe_cache_append_u32(staged,
				static_cast<uint32_t>(entry.second.result.status));
		}
		minecraft_recipe_cache_append_u64(staged,
			minecraft_recipe_cache_checksum(staged.data(), staged.size()));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	bytes_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftRecipeCatalog::validate_craft_action_cache(
	const uint8_t *bytes, std::size_t byte_count) const noexcept
{
	std::size_t offset = 4U;
	std::size_t expected_size;
	uint32_t version;
	uint32_t record_count;
	uint64_t high_watermark;
	uint64_t stored_checksum = 0U;
	uint64_t previous_request_id = 0U;

	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (bytes == nullptr || byte_count < 28U || bytes[0] != 'M'
		|| bytes[1] != 'C' || bytes[2] != 'R' || bytes[3] != 'A')
		return (FT_ERR_INVALID_ARGUMENT);
	for (uint32_t index = 0U; index < 8U; ++index)
		stored_checksum |= static_cast<uint64_t>(
			bytes[byte_count - 8U + index]) << (index * 8U);
	if (minecraft_recipe_cache_checksum(bytes, byte_count - 8U)
		!= stored_checksum
		|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
			&offset, &version)
		|| (version != 1U && version != 2U)
		|| !minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
			&offset, &high_watermark)
		|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
			&offset, &record_count)
		|| record_count > 4096U
		|| record_count > (std::numeric_limits<std::size_t>::max() - 28U)
			/ (version == 1U ? 36U : 60U))
		return (FT_ERR_INVALID_ARGUMENT);
	expected_size = 28U + static_cast<std::size_t>(record_count)
		* (version == 1U ? 36U : 60U);
	if (byte_count != expected_size)
		return (FT_ERR_INVALID_ARGUMENT);
	for (uint32_t index = 0U; index < record_count; ++index)
	{
		uint64_t request_id;
		uint32_t recipe_id;
		uint32_t batch_count;
		uint64_t expected_revision;
		uint32_t station_id = 0U;
		uint32_t station_x = 0U;
		uint32_t station_y = 0U;
		uint32_t station_z = 0U;
		uint64_t expected_station_revision = 0U;
		uint64_t result_revision;
		uint32_t status;

		if (!minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
				&offset, &request_id)
			|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
				&offset, &recipe_id)
			|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
				&offset, &batch_count)
			|| !minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
				&offset, &expected_revision))
			return (FT_ERR_INVALID_ARGUMENT);
		if (version == 2U
			&& (!minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &station_id)
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &station_x)
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &station_y)
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &station_z)
				|| !minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
					&offset, &expected_station_revision)))
			return (FT_ERR_INVALID_ARGUMENT);
		if (!minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
				&offset, &result_revision))
			return (FT_ERR_INVALID_ARGUMENT);
		if (!minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
				&offset, &status))
			return (FT_ERR_INVALID_ARGUMENT);
		(void)expected_revision;
		(void)result_revision;
		(void)status;
		if (request_id == 0U || request_id <= previous_request_id
			|| request_id > high_watermark || recipe_id == 0U
			|| recipe_id > static_cast<uint32_t>(
				std::numeric_limits<int32_t>::max())
			|| batch_count == 0U || batch_count > static_cast<uint32_t>(
				std::numeric_limits<int32_t>::max())
			|| station_id > static_cast<uint32_t>(FURNACE_STATION_ID)
			|| (station_id == static_cast<uint32_t>(HAND_STATION_ID)
				&& (station_x != 0U || station_y != 0U || station_z != 0U
					|| expected_station_revision != 0U))
			|| (station_id != static_cast<uint32_t>(HAND_STATION_ID)
				&& expected_station_revision == 0U))
			return (FT_ERR_INVALID_ARGUMENT);
		previous_request_id = request_id;
	}
	if (offset != byte_count - 8U
		|| (record_count > 0U && previous_request_id > high_watermark))
		return (FT_ERR_INVALID_ARGUMENT);
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftRecipeCatalog::restore_craft_action_cache(
	const uint8_t *bytes, std::size_t byte_count) noexcept
{
	std::map<uint64_t, CachedCraftAction> staged_cache;
	std::size_t offset = 4U;
	uint32_t version;
	uint32_t record_count;
	uint64_t high_watermark;
	uint64_t stored_checksum = 0U;
	uint64_t previous_request_id = 0U;
	std::size_t expected_size;

	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (this->validate_craft_action_cache(bytes, byte_count)
		!= FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_ARGUMENT);
	if (bytes == nullptr || byte_count < 28U || bytes[0] != 'M'
		|| bytes[1] != 'C' || bytes[2] != 'R' || bytes[3] != 'A')
		return (FT_ERR_INVALID_ARGUMENT);
	for (uint32_t index = 0U; index < 8U; ++index)
		stored_checksum |= static_cast<uint64_t>(
			bytes[byte_count - 8U + index]) << (index * 8U);
	if (minecraft_recipe_cache_checksum(bytes, byte_count - 8U)
		!= stored_checksum
		|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
			&offset, &version)
		|| (version != 1U && version != 2U)
		|| !minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
			&offset, &high_watermark)
		|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
			&offset, &record_count)
		|| record_count > 4096U
		|| record_count > (std::numeric_limits<std::size_t>::max() - 28U)
			/ (version == 1U ? 36U : 60U))
		return (FT_ERR_INVALID_ARGUMENT);
	expected_size = 28U + static_cast<std::size_t>(record_count)
		* (version == 1U ? 36U : 60U);
	if (byte_count != expected_size)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		for (uint32_t index = 0U; index < record_count; ++index)
		{
			CachedCraftAction action = {};
			uint64_t request_id;
			uint32_t batch_count;
			uint32_t status;

			if (!minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
					&offset, &request_id)
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &action.recipe_id)
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &batch_count)
				|| !minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
					&offset, &action.expected_inventory_revision))
				return (FT_ERR_INVALID_ARGUMENT);
			if (version >= 2U)
			{
				uint32_t station_id = 0U;
				uint32_t station_x = 0U;
				uint32_t station_y = 0U;
				uint32_t station_z = 0U;
				if (!minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
						&offset, &station_id)
					|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
						&offset, &station_x)
					|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
						&offset, &station_y)
					|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
						&offset, &station_z)
					|| !minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
						&offset, &action.expected_station_revision)
					|| station_id > static_cast<uint32_t>(FURNACE_STATION_ID))
					return (FT_ERR_INVALID_ARGUMENT);
				action.station_id = static_cast<StationId>(station_id);
				action.station_x = static_cast<int32_t>(station_x);
				action.station_y = static_cast<int32_t>(station_y);
				action.station_z = static_cast<int32_t>(station_z);
			}
			if (!minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
					&offset, &action.result.inventory_revision)
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &status))
				return (FT_ERR_INVALID_ARGUMENT);
			if (request_id == 0U || request_id <= previous_request_id
				|| request_id > high_watermark || action.recipe_id == 0U
				|| action.recipe_id > static_cast<uint32_t>(
					std::numeric_limits<int32_t>::max())
				|| batch_count == 0U || batch_count > static_cast<uint32_t>(
					std::numeric_limits<int32_t>::max()))
				return (FT_ERR_INVALID_ARGUMENT);
			action.batch_count = static_cast<int32_t>(batch_count);
			action.result.request_id = request_id;
			action.result.recipe_id = action.recipe_id;
			action.result.batch_count = action.batch_count;
			action.result.status = static_cast<int32_t>(status);
			action.result.replayed = false;
			staged_cache.emplace(request_id, action);
			previous_request_id = request_id;
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (offset != byte_count - 8U
		|| (record_count > 0U && previous_request_id > high_watermark))
		return (FT_ERR_INVALID_ARGUMENT);
	this->craft_action_cache_.swap(staged_cache);
	this->highest_craft_request_id_ = high_watermark;
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftRecipeCatalog::clear_craft_action_cache() noexcept
{
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	this->craft_action_cache_.clear();
	this->highest_craft_request_id_ = 0U;
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftRecipeCatalog::start_processing(game_inventory &inventory,
	uint64_t request_id, uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision,
	int32_t station_x, int32_t station_y, int32_t station_z,
	uint64_t expected_station_revision,
	MinecraftProcessingActionResult *result_out) noexcept
{
	static const uint32_t MAX_BATCH = 32U;
	const uint32_t processing_index = recipe_id
		- FIRST_PROCESSING_RECIPE_ID;
	std::map<uint64_t, CachedProcessingAction>::iterator cached;
	std::map<uint64_t, ProcessingJob>::iterator job;
	CachedProcessingAction action = {};
	ProcessingJob staged_job = {};
	game_crafting_prepared prepared;
	uint64_t revision = 0U;
	int32_t error_code;

	if (!this->initialized_ || this->items_ == nullptr || result_out == nullptr
		|| request_id == 0U || recipe_id < FIRST_PROCESSING_RECIPE_ID
		|| recipe_id > LAST_PROCESSING_RECIPE_ID
		|| batch_count <= 0 || static_cast<uint32_t>(batch_count) > MAX_BATCH
		|| expected_station_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	cached = this->processing_action_cache_.find(request_id);
	if (cached != this->processing_action_cache_.end())
	{
		if (cached->second.recipe_id != recipe_id
			|| cached->second.batch_count != batch_count
			|| cached->second.expected_inventory_revision
				!= expected_inventory_revision)
			return (FT_ERR_INVALID_ARGUMENT);
		*result_out = cached->second.result;
		result_out->replayed = true;
		return (result_out->status);
	}
	if (request_id <= this->highest_processing_request_id_)
		return (FT_ERR_NOT_FOUND);
	if (this->processing_jobs_.size() >= 64U
		|| this->processing_action_cache_.size() >= 4096U)
		return (FT_ERR_FULL);
	action.recipe_id = recipe_id;
	action.batch_count = batch_count;
	action.expected_inventory_revision = expected_inventory_revision;
	action.result.request_id = request_id;
	action.result.recipe_id = recipe_id;
	action.result.batch_count = batch_count;
	action.result.status = FT_ERR_INVALID_STATE;
	try
	{
		cached = this->processing_action_cache_.emplace(request_id, action).first;
		staged_job.recipe_id = recipe_id;
		staged_job.batch_count = batch_count;
		staged_job.ticks_remaining = static_cast<uint32_t>(batch_count)
			* this->processing_ticks_per_item_[processing_index];
		staged_job.station_x = station_x;
		staged_job.station_y = station_y;
		staged_job.station_z = station_z;
		staged_job.expected_station_revision = expected_station_revision;
		job = this->processing_jobs_.emplace(request_id, staged_job).first;
	}
	catch (const std::bad_alloc &)
	{
		this->processing_action_cache_.erase(request_id);
		return (FT_ERR_NO_MEMORY);
	}
	this->highest_processing_request_id_ = request_id;
	ft_bool recipe_known = FT_FALSE;
	error_code = this->recipe_book_.is_recipe_known(recipe_id,
		&recipe_known);
	if (error_code == FT_ERR_SUCCESS && recipe_known != FT_TRUE)
		error_code = FT_ERR_PERMISSION_DENIED;
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.get_revision(&revision);
	if (error_code == FT_ERR_SUCCESS
		&& revision != expected_inventory_revision)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = prepared.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->crafting_.prepare_processing_at_station(inventory,
			static_cast<int32_t>(recipe_id), FURNACE_STATION_ID, batch_count,
			&prepared, &job->second.output);
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.get_revision(&revision);
	if (error_code == FT_ERR_SUCCESS
		&& revision != expected_inventory_revision)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->crafting_.commit_processing_inputs(inventory, &prepared);
	(void)prepared.destroy();
	if (error_code != FT_ERR_SUCCESS)
		this->processing_jobs_.erase(request_id);
	if (inventory.get_revision(&revision) == FT_ERR_SUCCESS)
		cached->second.result.inventory_revision = revision;
	cached->second.result.ticks_remaining = error_code == FT_ERR_SUCCESS
		? static_cast<uint32_t>(batch_count)
			* this->processing_ticks_per_item_[processing_index] : 0U;
	cached->second.result.status = error_code;
	*result_out = cached->second.result;
	return (error_code);
}

int32_t MinecraftRecipeCatalog::advance_processing(game_inventory &inventory,
	uint32_t server_ticks, uint32_t job_budget, uint32_t *completed_out,
	uint32_t *pending_output_out) noexcept
{
	uint32_t completed = 0U;
	uint32_t pending = 0U;
	uint32_t visited = 0U;
	if (!this->initialized_ || completed_out == nullptr
		|| pending_output_out == nullptr || server_ticks > 1200U
		|| job_budget > 64U)
		return (FT_ERR_INVALID_ARGUMENT);
	std::map<uint64_t, ProcessingJob>::iterator iterator =
		this->processing_jobs_.begin();
	while (iterator != this->processing_jobs_.end() && visited < job_budget)
	{
		const uint64_t request_id = iterator->first;
		ProcessingJob &job = iterator->second;
		if (job.ticks_remaining > server_ticks)
			job.ticks_remaining -= server_ticks;
		else
			job.ticks_remaining = 0U;
		if (job.ticks_remaining == 0U)
		{
			game_crafting_product_snapshot &product = job.output.primary;
			ft_sharedptr<game_item> output;
			int32_t result = FT_ERR_SUCCESS;
			if (job.output.byproduct_count != 0U || product.instance_id != 0U
				|| product.quantity <= 0 || product.item_id <= 0)
				result = FT_ERR_UNSUPPORTED_TYPE;
			else
			{
				result = this->items_->create_item(product.item_id,
					product.quantity, &output);
				if (result == FT_ERR_SUCCESS)
				{
					output->set_max_stack(product.max_stack);
					output->set_rarity(product.rarity);
					output->set_width(product.width);
					output->set_height(product.height);
					output->set_modifier1_id(product.modifier1_id);
					output->set_modifier1_value(product.modifier1_value);
					output->set_modifier2_id(product.modifier2_id);
					output->set_modifier2_value(product.modifier2_value);
					output->set_modifier3_id(product.modifier3_id);
					output->set_modifier3_value(product.modifier3_value);
					output->set_modifier4_id(product.modifier4_id);
					output->set_modifier4_value(product.modifier4_value);
					result = inventory.add_item(output);
				}
			}
		if (result == FT_ERR_SUCCESS)
		{
			this->processing_action_cache_[request_id].result.ticks_remaining = 0U;
			this->processing_action_cache_[request_id].result.status = FT_ERR_SUCCESS;
			this->processing_jobs_.erase(iterator++);
			completed += 1U;
			++visited;
			continue;
		}
		if (result != FT_ERR_FULL && result != FT_ERR_NO_MEMORY)
			return (result);
		++pending;
		}
		this->processing_action_cache_[request_id].result.ticks_remaining =
			job.ticks_remaining;
		++iterator;
		++visited;
	}
	*completed_out = completed;
	*pending_output_out = pending;
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftRecipeCatalog::get_processing_job_count(
	uint32_t *count_out) const noexcept
{
	if (!this->initialized_ || count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->processing_jobs_.size() > UINT32_MAX)
		return (FT_ERR_OUT_OF_RANGE);
	*count_out = static_cast<uint32_t>(this->processing_jobs_.size());
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftRecipeCatalog::get_processing_station_contexts(
	std::vector<ProcessingStationContext> &contexts_out) const noexcept
{
	std::vector<ProcessingStationContext> staged;
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	try
	{
		staged.reserve(this->processing_jobs_.size());
		for (const auto &entry : this->processing_jobs_)
		{
			ProcessingStationContext context = {};
			context.request_id = entry.first;
			context.station_x = entry.second.station_x;
			context.station_y = entry.second.station_y;
			context.station_z = entry.second.station_z;
			context.expected_station_revision =
				entry.second.expected_station_revision;
			staged.push_back(context);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	contexts_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftRecipeCatalog::serialize_processing_state(
	std::vector<uint8_t> &bytes_out) const noexcept
{
	std::vector<uint8_t> staged;
	try
	{
		staged.reserve(32U + this->processing_action_cache_.size() * 40U
			+ this->processing_jobs_.size() * 128U);
		staged.push_back('M'); staged.push_back('C');
		staged.push_back('P'); staged.push_back('J');
		minecraft_recipe_cache_append_u32(staged, 4U);
		for (uint32_t index = 0U; index < 7U; ++index)
		{
			minecraft_recipe_cache_append_u32(staged,
				static_cast<uint32_t>(this->processing_fuel_item_ids_[index]));
			minecraft_recipe_cache_append_u32(staged,
				this->processing_fuel_units_per_item_[index]);
			minecraft_recipe_cache_append_u32(staged,
				this->processing_ticks_per_item_[index]);
		}
		minecraft_recipe_cache_append_u64(staged,
			this->highest_processing_request_id_);
		minecraft_recipe_cache_append_u32(staged,
			static_cast<uint32_t>(this->processing_action_cache_.size()));
		minecraft_recipe_cache_append_u32(staged,
			static_cast<uint32_t>(this->processing_jobs_.size()));
		for (const auto &entry : this->processing_action_cache_)
		{
			const CachedProcessingAction &action = entry.second;
			minecraft_recipe_cache_append_u64(staged, entry.first);
			minecraft_recipe_cache_append_u32(staged, action.recipe_id);
			minecraft_recipe_cache_append_u32(staged,
				static_cast<uint32_t>(action.batch_count));
			minecraft_recipe_cache_append_u64(staged,
				action.expected_inventory_revision);
			minecraft_recipe_cache_append_u64(staged,
				action.result.inventory_revision);
			minecraft_recipe_cache_append_u32(staged,
				action.result.ticks_remaining);
			minecraft_recipe_cache_append_u32(staged,
				static_cast<uint32_t>(action.result.status));
		}
		for (const auto &entry : this->processing_jobs_)
		{
			const ProcessingJob &job = entry.second;
			minecraft_recipe_cache_append_u64(staged, entry.first);
			minecraft_recipe_cache_append_u32(staged, job.recipe_id);
			minecraft_recipe_cache_append_u32(staged,
				static_cast<uint32_t>(job.batch_count));
			minecraft_recipe_cache_append_u32(staged, job.ticks_remaining);
			minecraft_recipe_cache_append_u32(staged,
				static_cast<uint32_t>(job.station_x));
			minecraft_recipe_cache_append_u32(staged,
				static_cast<uint32_t>(job.station_y));
			minecraft_recipe_cache_append_u32(staged,
				static_cast<uint32_t>(job.station_z));
			minecraft_recipe_cache_append_u64(staged,
				job.expected_station_revision);
			minecraft_processing_append_product(staged, job.output.primary);
			minecraft_recipe_cache_append_u32(staged,
				job.output.byproduct_count);
			for (uint32_t index = 0U;
				index < job.output.byproduct_count
					&& index < GAME_CRAFTING_MAX_BYPRODUCTS; ++index)
				minecraft_processing_append_product(staged,
					job.output.byproducts[index]);
		}
		minecraft_recipe_cache_append_u64(staged,
			minecraft_recipe_cache_checksum(staged.data(), staged.size()));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	bytes_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftRecipeCatalog::validate_processing_state(
	const uint8_t *bytes, std::size_t byte_count) const noexcept
{
	std::map<uint64_t, CachedProcessingAction> actions;
	std::map<uint64_t, ProcessingJob> jobs;
	std::size_t offset = 0U;
	uint32_t version = 0U;
	uint32_t fuel_item_ids[7];
	uint32_t fuel_units_per_item[7];
	uint32_t ticks_per_item[7];
	uint32_t action_count = 0U;
	uint32_t job_count = 0U;
	uint64_t high_watermark = 0U;
	uint64_t stored_checksum = 0U;
	uint64_t previous_id = 0U;
	for (uint32_t index = 0U; index < 7U; ++index)
	{
		fuel_item_ids[index] = static_cast<uint32_t>(
			DEFAULT_PROCESSING_FUEL_ITEM_ID);
		fuel_units_per_item[index] =
			DEFAULT_PROCESSING_FUEL_UNITS_PER_ITEM;
		ticks_per_item[index] = DEFAULT_PROCESSING_TICKS_PER_ITEM;
	}
	if (!this->initialized_ || bytes == nullptr || byte_count < 28U)
		return (FT_ERR_INVALID_ARGUMENT);
	for (uint32_t index = 0U; index < 8U; ++index)
		stored_checksum |= static_cast<uint64_t>(bytes[byte_count - 8U + index])
			<< (index * 8U);
	if (minecraft_recipe_cache_checksum(bytes, byte_count - 8U)
		!= stored_checksum || bytes[0] != 'M' || bytes[1] != 'C'
		|| bytes[2] != 'P' || bytes[3] != 'J')
		return (FT_ERR_INVALID_ARGUMENT);
	offset = 4U;
	if (!minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
			&offset, &version)
		|| (version < 1U || version > 4U))
		return (FT_ERR_INVALID_ARGUMENT);
	if (version == 3U)
	{
		uint32_t shared_fuel = 0U;
		uint32_t shared_units = 0U;
		uint32_t shared_ticks = 0U;
		if (!minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
				&offset, &shared_fuel)
			|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
				&offset, &shared_units)
			|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
				&offset, &shared_ticks))
			return (FT_ERR_INVALID_ARGUMENT);
		for (uint32_t index = 0U; index < 7U; ++index)
		{
			fuel_item_ids[index] = shared_fuel;
			fuel_units_per_item[index] = shared_units;
			ticks_per_item[index] = shared_ticks;
		}
	}
	else if (version == 4U)
	{
		for (uint32_t index = 0U; index < 7U; ++index)
		{
			if (!minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &fuel_item_ids[index])
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &fuel_units_per_item[index])
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &ticks_per_item[index]))
				return (FT_ERR_INVALID_ARGUMENT);
		}
	}
	for (uint32_t index = 0U; index < 7U; ++index)
	{
		if (fuel_item_ids[index] != static_cast<uint32_t>(
				this->processing_fuel_item_ids_[index])
			|| fuel_units_per_item[index]
				!= this->processing_fuel_units_per_item_[index]
			|| ticks_per_item[index]
				!= this->processing_ticks_per_item_[index])
			return (FT_ERR_INVALID_ARGUMENT);
	}
	if (!minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
			&offset, &high_watermark)
		|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
			&offset, &action_count)
		|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
			&offset, &job_count)
		|| action_count > 4096U || job_count > 64U)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		previous_id = 0U;
		for (uint32_t index = 0U; index < action_count; ++index)
		{
			CachedProcessingAction action = {};
			uint64_t request_id = 0U;
			uint32_t batch = 0U;
			uint32_t status = 0U;
			if (!minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
					&offset, &request_id)
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &action.recipe_id)
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &batch)
				|| !minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
					&offset, &action.expected_inventory_revision)
				|| !minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
					&offset, &action.result.inventory_revision)
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &action.result.ticks_remaining)
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &status)
				|| request_id == 0U || request_id <= previous_id
				|| action.recipe_id < 1029U || action.recipe_id > 1035U
				|| batch == 0U || batch > 32U
				|| action.result.ticks_remaining > batch
					* ticks_per_item[action.recipe_id
						- FIRST_PROCESSING_RECIPE_ID])
				return (FT_ERR_INVALID_ARGUMENT);
			action.batch_count = static_cast<int32_t>(batch);
			action.result.request_id = request_id;
			action.result.recipe_id = action.recipe_id;
			action.result.batch_count = action.batch_count;
			action.result.status = static_cast<int32_t>(status);
			action.result.replayed = false;
			if (static_cast<int32_t>(status) < -100
				|| static_cast<int32_t>(status) > 100)
				return (FT_ERR_INVALID_ARGUMENT);
			actions.emplace(request_id, action);
			previous_id = request_id;
		}
		previous_id = 0U;
		for (uint32_t index = 0U; index < job_count; ++index)
		{
			ProcessingJob job = {};
			uint64_t request_id = 0U;
			uint32_t batch = 0U;
			if (!minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
					&offset, &request_id)
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &job.recipe_id)
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &batch)
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &job.ticks_remaining))
				return (FT_ERR_INVALID_ARGUMENT);
			if (version >= 2U)
			{
				uint32_t station_x = 0U;
				uint32_t station_y = 0U;
				uint32_t station_z = 0U;
				if (!minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
						&offset, &station_x)
					|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
						&offset, &station_y)
					|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
						&offset, &station_z)
					|| !minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
						&offset, &job.expected_station_revision))
					return (FT_ERR_INVALID_ARGUMENT);
				job.station_x = static_cast<int32_t>(station_x);
				job.station_y = static_cast<int32_t>(station_y);
				job.station_z = static_cast<int32_t>(station_z);
			}
			if (!minecraft_processing_read_product(bytes, byte_count - 8U,
					&offset, &job.output.primary)
				|| !minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &job.output.byproduct_count)
				|| request_id == 0U || request_id <= previous_id
				|| job.recipe_id < 1029U || job.recipe_id > 1035U
				|| batch == 0U || batch > 32U
				|| job.ticks_remaining > batch * ticks_per_item[job.recipe_id
					- FIRST_PROCESSING_RECIPE_ID]
				|| job.output.primary.item_id <= 0
				|| job.output.primary.quantity <= 0
				|| job.output.primary.max_stack <= 0
				|| job.output.primary.instance_id != 0U
				|| job.output.byproduct_count > GAME_CRAFTING_MAX_BYPRODUCTS)
				return (FT_ERR_INVALID_ARGUMENT);
			job.batch_count = static_cast<int32_t>(batch);
			for (uint32_t product_index = 0U;
				product_index < job.output.byproduct_count; ++product_index)
			{
				if (!minecraft_processing_read_product(bytes, byte_count - 8U,
						&offset, &job.output.byproducts[product_index])
					|| job.output.byproducts[product_index].item_id <= 0
					|| job.output.byproducts[product_index].quantity <= 0
					|| job.output.byproducts[product_index].max_stack <= 0)
					return (FT_ERR_INVALID_ARGUMENT);
			}
			auto action = actions.find(request_id);
			if (action == actions.end()
				|| action->second.recipe_id != job.recipe_id
				|| action->second.batch_count != job.batch_count
				|| action->second.result.status != FT_ERR_SUCCESS
				|| action->second.result.ticks_remaining != job.ticks_remaining)
				return (FT_ERR_INVALID_ARGUMENT);
			jobs.emplace(request_id, job);
			previous_id = request_id;
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (offset != byte_count - 8U || previous_id > high_watermark
		|| (action_count > 0U && actions.rbegin()->first > high_watermark))
		return (FT_ERR_INVALID_ARGUMENT);
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftRecipeCatalog::restore_processing_state(
	const uint8_t *bytes, std::size_t byte_count) noexcept
{
	std::map<uint64_t, CachedProcessingAction> actions;
	std::map<uint64_t, ProcessingJob> jobs;
	std::size_t offset = 0U;
	uint32_t version = 0U;
	uint32_t action_count = 0U;
	uint32_t job_count = 0U;
	uint64_t high_watermark = 0U;
	uint64_t request_id = 0U;
	uint64_t ignored_u64 = 0U;
	uint32_t value = 0U;
	int32_t error_code = this->validate_processing_state(bytes, byte_count);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	offset = 4U;
	(void)minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
		&offset, &version);
	if (version == 3U)
		offset += 12U;
	else if (version == 4U)
		offset += 84U;
	(void)minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
		&offset, &high_watermark);
	(void)minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
		&offset, &action_count);
	(void)minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
		&offset, &job_count);
	try
	{
		for (uint32_t index = 0U; index < action_count; ++index)
		{
			CachedProcessingAction action = {};
			uint32_t batch = 0U;
			uint32_t status = 0U;
			(void)minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
				&offset, &request_id);
			(void)minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
				&offset, &action.recipe_id);
			(void)minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
				&offset, &batch);
			(void)minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
				&offset, &action.expected_inventory_revision);
			(void)minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
				&offset, &action.result.inventory_revision);
			(void)minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
				&offset, &action.result.ticks_remaining);
			(void)minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
				&offset, &status);
			action.batch_count = static_cast<int32_t>(batch);
			action.result.request_id = request_id;
			action.result.recipe_id = action.recipe_id;
			action.result.batch_count = action.batch_count;
			action.result.status = static_cast<int32_t>(status);
			actions.emplace(request_id, action);
		}
		for (uint32_t index = 0U; index < job_count; ++index)
		{
			ProcessingJob job = {};
			uint32_t batch = 0U;
			(void)minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
				&offset, &request_id);
			(void)minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
				&offset, &job.recipe_id);
			(void)minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
				&offset, &batch);
			(void)minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
				&offset, &job.ticks_remaining);
			if (version >= 2U)
			{
				uint32_t station_x = 0U;
				uint32_t station_y = 0U;
				uint32_t station_z = 0U;
				(void)minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &station_x);
				(void)minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &station_y);
				(void)minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
					&offset, &station_z);
				(void)minecraft_recipe_cache_read_u64(bytes, byte_count - 8U,
					&offset, &job.expected_station_revision);
				job.station_x = static_cast<int32_t>(station_x);
				job.station_y = static_cast<int32_t>(station_y);
				job.station_z = static_cast<int32_t>(station_z);
			}
			(void)minecraft_processing_read_product(bytes, byte_count - 8U,
				&offset, &job.output.primary);
			(void)minecraft_recipe_cache_read_u32(bytes, byte_count - 8U,
				&offset, &job.output.byproduct_count);
			job.batch_count = static_cast<int32_t>(batch);
			for (uint32_t product_index = 0U;
				product_index < job.output.byproduct_count; ++product_index)
				(void)minecraft_processing_read_product(bytes, byte_count - 8U,
					&offset, &job.output.byproducts[product_index]);
			jobs.emplace(request_id, job);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	(void)version;
	(void)ignored_u64;
	(void)value;
	this->processing_action_cache_.swap(actions);
	this->processing_jobs_.swap(jobs);
	this->highest_processing_request_id_ = high_watermark;
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftRecipeCatalog::clear_processing_state() noexcept
{
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	this->processing_action_cache_.clear();
	this->processing_jobs_.clear();
	this->highest_processing_request_id_ = 0U;
	return (FT_ERR_SUCCESS);
}

bool MinecraftRecipeCatalog::is_initialized() const noexcept
{
	return (this->initialized_);
}

int32_t MinecraftRecipeCatalog::unlock_recipe_with_result(
	uint32_t recipe_id, uint64_t source_event_id,
	game_recipe_unlock_result *result_out) noexcept
{
	std::vector<uint8_t> book_before;
	int32_t error_code;

	if (!this->initialized_ || result_out == nullptr)
		return (FT_ERR_INVALID_STATE);
	result_out->event_newly_recorded = FT_FALSE;
	result_out->recipe_newly_known = FT_FALSE;
	error_code = this->serialize_recipe_book(book_before);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = this->recipe_book_.unlock_recipe_with_result(recipe_id,
		source_event_id, result_out);
	if (error_code == FT_ERR_SUCCESS
		&& result_out->recipe_newly_known == FT_TRUE)
		error_code = this->crafting_.unlock_recipe(
			static_cast<int32_t>(recipe_id));
	if (error_code != FT_ERR_SUCCESS)
	{
		const int32_t restore_result = this->restore_recipe_book(
			book_before.data(), book_before.size());
		if (restore_result != FT_ERR_SUCCESS)
		{
			result_out->event_newly_recorded = FT_FALSE;
			result_out->recipe_newly_known = FT_FALSE;
			return (FT_ERR_INVALID_STATE);
		}
		result_out->event_newly_recorded = FT_FALSE;
		result_out->recipe_newly_known = FT_FALSE;
	}
	return (error_code);
}

int32_t MinecraftRecipeCatalog::unlock_recipe(uint32_t recipe_id,
	uint64_t source_event_id, ft_bool *newly_unlocked) noexcept
{
	game_recipe_unlock_result unlock_result = {};
	int32_t error_code;

	if (newly_unlocked == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*newly_unlocked = FT_FALSE;
	error_code = this->unlock_recipe_with_result(recipe_id, source_event_id,
		&unlock_result);
	if (error_code == FT_ERR_SUCCESS)
		*newly_unlocked = unlock_result.recipe_newly_known;
	return (error_code);
}

int32_t MinecraftRecipeCatalog::register_discovery_source(
	uint32_t source_type_id, uint32_t source_id, uint32_t recipe_id) noexcept
{
	uint64_t source_key;
	std::map<uint64_t, uint32_t>::iterator existing;

	if (!this->initialized_ || source_type_id == 0U || source_id == 0U
		|| recipe_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (std::find(this->registered_recipe_ids_.begin(),
			this->registered_recipe_ids_.end(), recipe_id)
		== this->registered_recipe_ids_.end())
		return (FT_ERR_NOT_FOUND);
	source_key = (static_cast<uint64_t>(source_type_id) << 32U)
		| static_cast<uint64_t>(source_id);
	existing = this->discovery_sources_.find(source_key);
	if (existing != this->discovery_sources_.end())
		return (existing->second == recipe_id
			? FT_ERR_SUCCESS : FT_ERR_ALREADY_EXISTS);
	if (this->discovery_sources_.size() >= 4096U)
		return (FT_ERR_FULL);
	try
	{
		this->discovery_sources_.emplace(source_key, recipe_id);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftRecipeCatalog::grant_recipe_from_discovery(
	uint32_t source_type_id, uint32_t source_id, uint64_t source_event_id,
	ft_bool *newly_unlocked) noexcept
{
	game_recipe_unlock_result unlock_result = {};
	int32_t error_code;

	if (newly_unlocked == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*newly_unlocked = FT_FALSE;
	error_code = this->grant_recipe_from_discovery_with_result(
		source_type_id, source_id, source_event_id, &unlock_result);
	if (error_code == FT_ERR_SUCCESS)
		*newly_unlocked = unlock_result.recipe_newly_known;
	return (error_code);
}

int32_t MinecraftRecipeCatalog::grant_recipe_from_discovery_with_result(
	uint32_t source_type_id, uint32_t source_id, uint64_t source_event_id,
	game_recipe_unlock_result *result_out) noexcept
{
	const uint64_t source_key = (static_cast<uint64_t>(source_type_id) << 32U)
		| static_cast<uint64_t>(source_id);
	std::map<uint64_t, uint32_t>::const_iterator source;

	if (!this->initialized_ || source_type_id == 0U || source_id == 0U
		|| source_event_id == 0U || result_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	result_out->event_newly_recorded = FT_FALSE;
	result_out->recipe_newly_known = FT_FALSE;
	source = this->discovery_sources_.find(source_key);
	if (source == this->discovery_sources_.end())
		return (FT_ERR_NOT_FOUND);
	return (this->unlock_recipe_with_result(source->second, source_event_id,
		result_out));
}

int32_t MinecraftRecipeCatalog::is_recipe_known(uint32_t recipe_id,
	ft_bool *known) const noexcept
{
	if (!this->initialized_)
		return (FT_ERR_INVALID_STATE);
	return (this->recipe_book_.is_recipe_known(recipe_id, known));
}

int32_t MinecraftRecipeCatalog::import_book(ft_byte_buffer &input) noexcept
{
	int32_t error_code;

	if (!this->initialized_)
		return (FT_ERR_INVALID_STATE);
	error_code = this->recipe_book_.import_book(input);
	for (uint32_t recipe_id : this->registered_recipe_ids_)
	{
		ft_bool known = FT_FALSE;

		if (error_code == FT_ERR_SUCCESS)
			error_code = this->recipe_book_.is_recipe_known(recipe_id, &known);
		if (error_code == FT_ERR_SUCCESS && known == FT_TRUE)
			error_code = this->crafting_.unlock_recipe(
				static_cast<int32_t>(recipe_id));
		else if (error_code == FT_ERR_SUCCESS)
			error_code = this->crafting_.lock_recipe(
				static_cast<int32_t>(recipe_id));
	}
	return (error_code);
}

int32_t MinecraftRecipeCatalog::serialize_recipe_book(
	std::vector<uint8_t> &bytes_out) const noexcept
{
	ft_byte_buffer buffer;
	int32_t result;

	if (!this->initialized_)
		return (FT_ERR_INVALID_STATE);
	result = buffer.initialize();
	if (result == FT_ERR_SUCCESS)
		result = this->recipe_book_.export_book(buffer);
	if (result == FT_ERR_SUCCESS)
	{
		try
		{
			bytes_out.assign(buffer.data(), buffer.data() + buffer.size());
		}
		catch (const std::bad_alloc &)
		{
			result = FT_ERR_NO_MEMORY;
		}
	}
	const int32_t destroy_result = buffer.destroy();
	if (result == FT_ERR_SUCCESS && destroy_result != FT_ERR_SUCCESS)
		result = destroy_result;
	return (result);
}

int32_t MinecraftRecipeCatalog::restore_recipe_book(const uint8_t *bytes,
	std::size_t byte_count) noexcept
{
	ft_byte_buffer buffer;
	int32_t result;

	if (!this->initialized_ || (byte_count != 0U && bytes == nullptr))
		return (FT_ERR_INVALID_ARGUMENT);
	result = buffer.initialize();
	if (result == FT_ERR_SUCCESS)
		result = buffer.append(bytes, byte_count);
	if (result == FT_ERR_SUCCESS)
		result = this->import_book(buffer);
	const int32_t destroy_result = buffer.destroy();
	if (result == FT_ERR_SUCCESS && destroy_result != FT_ERR_SUCCESS)
		result = destroy_result;
	return (result);
}

int32_t MinecraftRecipeCatalog::commit_unique_craft(
	game_inventory &inventory, game_crafting_prepared *prepared,
	uint64_t *instance_id_out) noexcept
{
	std::lock_guard<std::mutex> allocator_lock(this->instance_id_mutex_);
	uint64_t candidate_id;
	uint32_t attempts;
	int32_t error_code;

	if (!this->initialized_ || prepared == nullptr || instance_id_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	candidate_id = this->next_output_instance_id_;
	attempts = 0U;
	while (candidate_id != 0U && attempts < 4096U)
	{
		error_code = this->crafting_.commit_craft(inventory, prepared,
			candidate_id);
		if (error_code == FT_ERR_SUCCESS)
		{
			*instance_id_out = candidate_id;
			this->next_output_instance_id_ = candidate_id == UINT64_MAX
				? 0U : candidate_id + 1U;
			return (FT_ERR_SUCCESS);
		}
		if (error_code != FT_ERR_ALREADY_EXISTS)
			return (error_code);
		if (candidate_id == UINT64_MAX)
			candidate_id = 0U;
		else
			candidate_id += 1U;
		attempts += 1U;
	}
	return (candidate_id == 0U ? FT_ERR_OUT_OF_RANGE : FT_ERR_FULL);
}

int32_t MinecraftRecipeCatalog::validate_recipe_coverage(
	const MinecraftItemCatalog &items,
	uint32_t *recipe_output_count_out,
	int32_t *missing_recipe_item_id_out) const noexcept
{
	static const int32_t card_item_ids[] = {
		MinecraftItemCatalog::BIOME_EXCLUSION_CARD_ITEM_ID,
		MinecraftItemCatalog::DEEP_SEAM_CARD_ITEM_ID,
		MinecraftItemCatalog::VILLAGE_CHARTER_CARD_ITEM_ID,
		MinecraftItemCatalog::SURFACE_MAGMA_CARD_ITEM_ID,
		MinecraftItemCatalog::DOUBLE_DRAFT_CARD_ITEM_ID,
		MinecraftItemCatalog::BIOME_COMPASS_CARD_ITEM_ID,
		MinecraftItemCatalog::SURVEYORS_REACH_CARD_ITEM_ID,
		MinecraftItemCatalog::VOLCANO_MARK_CARD_ITEM_ID,
		MinecraftItemCatalog::ULTIMATE_GREED_CARD_ITEM_ID,
		MinecraftItemCatalog::BIOME_RUPTURE_CARD_ITEM_ID,
		MinecraftItemCatalog::ORE_OVERGROWTH_CARD_ITEM_ID,
		MinecraftItemCatalog::SHIPWRECK_CHART_CARD_ITEM_ID,
		MinecraftItemCatalog::RUINED_VILLAGE_RELIC_CARD_ITEM_ID,
		MinecraftItemCatalog::UNSTABLE_CATALYST_CARD_ITEM_ID,
		MinecraftItemCatalog::DESERT_PYRAMID_SEAL_CARD_ITEM_ID,
		MinecraftItemCatalog::ANCIENT_CANOPY_SEAL_CARD_ITEM_ID,
		MinecraftItemCatalog::SPIDER_NEST_SEAL_CARD_ITEM_ID,
		MinecraftItemCatalog::VOLATILE_STABILIZER_CARD_ITEM_ID,
		MinecraftItemCatalog::FORTUNES_DRAW_CARD_ITEM_ID};
	const std::vector<MinecraftContentItemRule> &content_rules =
		items.content_item_rules();
	std::size_t output_index = 0U;
	std::size_t content_index = 0U;
	uint32_t card_index = 0U;

	if (!this->initialized_ || !items.is_initialized()
		|| recipe_output_count_out == nullptr
		|| missing_recipe_item_id_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*missing_recipe_item_id_out = 0;
	if (this->recipe_output_item_ids_.size() > UINT32_MAX)
		return (FT_ERR_OUT_OF_RANGE);
	while (output_index < this->recipe_output_item_ids_.size())
	{
		MinecraftContentItemRule rule = {};
		if (items.resolve_content_item_rule(
				this->recipe_output_item_ids_[output_index], &rule)
			!= FT_ERR_SUCCESS)
		{
			*missing_recipe_item_id_out =
				this->recipe_output_item_ids_[output_index];
			return (FT_ERR_INVALID_STATE);
		}
		++output_index;
	}
	while (content_index < content_rules.size())
	{
		const MinecraftContentItemRule &rule = content_rules[content_index];
		bool has_recipe_output = false;

		if (rule.acquisition == MINECRAFT_ITEM_ACQUISITION_RECIPE)
		{
			output_index = 0U;
			while (output_index < this->recipe_output_item_ids_.size())
			{
				if (this->recipe_output_item_ids_[output_index] == rule.item_id)
				{
					has_recipe_output = true;
					break;
				}
				++output_index;
			}
			if (!has_recipe_output)
			{
				*missing_recipe_item_id_out = rule.item_id;
				return (FT_ERR_NOT_FOUND);
			}
		}
		++content_index;
	}
	while (card_index < sizeof(card_item_ids) / sizeof(card_item_ids[0]))
	{
		uint32_t output_matches = 0U;
		output_index = 0U;
		while (output_index < this->recipe_output_item_ids_.size())
		{
			if (this->recipe_output_item_ids_[output_index]
				== card_item_ids[card_index])
				++output_matches;
			++output_index;
		}
		MinecraftContentItemRule card_rule = {};
		ft_bool recipe_known = FT_FALSE;
		int32_t result = items.resolve_content_item_rule(
			card_item_ids[card_index], &card_rule);
		if (result == FT_ERR_SUCCESS)
			result = this->recipe_book_.is_recipe_known(2001U + card_index,
				&recipe_known);
		if (result != FT_ERR_SUCCESS || output_matches != 1U
			|| (card_index < 7U
				&& (card_rule.acquisition
					!= MINECRAFT_ITEM_ACQUISITION_RECIPE
					|| recipe_known != FT_TRUE))
			|| (card_index >= 7U
				&& (card_rule.acquisition
					!= MINECRAFT_ITEM_ACQUISITION_DISCOVERY
					|| recipe_known != FT_FALSE)))
		{
			*missing_recipe_item_id_out = card_item_ids[card_index];
			return (result == FT_ERR_SUCCESS
				? FT_ERR_INVALID_STATE : result);
		}
		++card_index;
	}
	*recipe_output_count_out = static_cast<uint32_t>(
		this->recipe_output_item_ids_.size());
	return (FT_ERR_SUCCESS);
}

uint64_t MinecraftRecipeCatalog::next_output_instance_id() const noexcept
{
	std::lock_guard<std::mutex> allocator_lock(this->instance_id_mutex_);
	return (this->next_output_instance_id_);
}

int32_t MinecraftRecipeCatalog::allocate_output_instance_id(
	uint64_t *instance_id_out) noexcept
{
	std::lock_guard<std::mutex> allocator_lock(this->instance_id_mutex_);
	if (!this->initialized_ || instance_id_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->next_output_instance_id_ == 0U)
		return (FT_ERR_OUT_OF_RANGE);
	*instance_id_out = this->next_output_instance_id_;
	this->next_output_instance_id_ = this->next_output_instance_id_ == UINT64_MAX
		? 0U : this->next_output_instance_id_ + 1U;
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftRecipeCatalog::ensure_next_output_instance_id_above(
	uint64_t maximum_existing_instance_id) noexcept
{
	std::lock_guard<std::mutex> allocator_lock(this->instance_id_mutex_);
	if (!this->initialized_)
		return (FT_ERR_INVALID_STATE);
	if (this->next_output_instance_id_ == 0U)
		return (FT_ERR_OUT_OF_RANGE);
	if (this->next_output_instance_id_ > maximum_existing_instance_id)
		return (FT_ERR_SUCCESS);
	if (maximum_existing_instance_id == UINT64_MAX)
	{
		this->next_output_instance_id_ = 0U;
		return (FT_ERR_OUT_OF_RANGE);
	}
	this->next_output_instance_id_ = maximum_existing_instance_id + 1U;
	return (FT_ERR_SUCCESS);
}

int32_t MinecraftRecipeCatalog::restore_next_output_instance_id(
	uint64_t next_instance_id) noexcept
{
	std::lock_guard<std::mutex> allocator_lock(this->instance_id_mutex_);
	if (!this->initialized_)
		return (FT_ERR_INVALID_STATE);
	if (this->next_output_instance_id_ == 0U
		&& next_instance_id != 0U)
		return (FT_ERR_INVALID_OPERATION);
	if (this->next_output_instance_id_ != 0U && next_instance_id != 0U
		&& next_instance_id < this->next_output_instance_id_)
		return (FT_ERR_OUT_OF_RANGE);
	this->next_output_instance_id_ = next_instance_id;
	return (FT_ERR_SUCCESS);
}

game_crafting &MinecraftRecipeCatalog::crafting() noexcept
{
	return (this->crafting_);
}

game_recipe_book &MinecraftRecipeCatalog::recipe_book() noexcept
{
	return (this->recipe_book_);
}

const game_recipe_book &MinecraftRecipeCatalog::recipe_book() const noexcept
{
	return (this->recipe_book_);
}
