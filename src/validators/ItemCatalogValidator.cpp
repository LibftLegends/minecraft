#include "../../src/validators/ItemCatalogValidator.hpp"
#include "../../src/inventory/MinecraftItemCatalog.hpp"
#include "../../src/crafting/MinecraftRecipeCatalog.hpp"
#include "../../src/transactions/RecipeDiscoveryTransaction.hpp"
#include "../../src/app/GameSession.hpp"
#include "../../Libft/Modules/Voxel/voxel_api.hpp"

#include <cstdio>
#include <algorithm>
#include <thread>

struct ItemCatalogTestSaveContext
{
	int32_t result;
	uint32_t calls;
};

static int32_t item_catalog_test_save(void *context) noexcept
{
	ItemCatalogTestSaveContext *save_context =
		static_cast<ItemCatalogTestSaveContext *>(context);
	if (save_context == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	++save_context->calls;
	return (save_context->result);
}

struct ItemCatalogRecipeAuthorizationFixture
{
	uint32_t source_type_id;
	uint32_t source_id;
	uint64_t source_event_id;
	uint32_t calls;
};

static bool item_catalog_test_recipe_authorizer(uint32_t source_type_id,
	uint32_t source_id, uint64_t source_event_id, void *user_data) noexcept
{
	ItemCatalogRecipeAuthorizationFixture *fixture =
		static_cast<ItemCatalogRecipeAuthorizationFixture *>(user_data);
	if (fixture == nullptr)
		return (false);
	++fixture->calls;
	return (source_type_id == fixture->source_type_id
		&& source_id == fixture->source_id
		&& source_event_id == fixture->source_event_id);
}

static int32_t item_catalog_validate_recipe_discovery_authority() noexcept
{
	ItemCatalogRecipeAuthorizationFixture fixture = {2U, 0x2012U,
		0x4341524400002012ULL, 0U};
	if (GameSession::authorize_recipe_discovery_event(nullptr, 2U,
			0x2012U, fixture.source_event_id, &fixture)
		|| GameSession::authorize_recipe_discovery_event(
			&item_catalog_test_recipe_authorizer, 2U, 0x2012U, 0U,
			&fixture)
		|| fixture.calls != 0U
		|| GameSession::authorize_recipe_discovery_event(
			&item_catalog_test_recipe_authorizer, 2U, 0x2012U,
			fixture.source_event_id + 1U, &fixture)
		|| fixture.calls != 1U
		|| !GameSession::authorize_recipe_discovery_event(
			&item_catalog_test_recipe_authorizer, 2U, 0x2012U,
			fixture.source_event_id, &fixture)
		|| fixture.calls != 2U)
		return (FT_ERR_INVALID_STATE);
	return (FT_ERR_SUCCESS);
}

struct ItemCatalogVolcanoUseFixture
{
	uint32_t card_instance_id;
	uint32_t revision_identifier;
	uint32_t calls;
	bool allowed;
};

static bool item_catalog_test_volcano_use_authorizer(
	uint32_t card_instance_id, uint32_t revision_identifier,
	void *user_data) noexcept
{
	ItemCatalogVolcanoUseFixture *fixture =
		static_cast<ItemCatalogVolcanoUseFixture *>(user_data);
	if (fixture == nullptr)
		return (false);
	++fixture->calls;
	return (fixture->allowed
		&& card_instance_id == fixture->card_instance_id
		&& revision_identifier == fixture->revision_identifier);
}

static int32_t item_catalog_validate_volcano_use_authority() noexcept
{
	ItemCatalogVolcanoUseFixture fixture = {42U, 7U, 0U, false};
	GameSession session;
	if (GameSession::authorize_volcano_mark_use(nullptr, 42U, 7U, &fixture)
		|| GameSession::authorize_volcano_mark_use(
			&item_catalog_test_volcano_use_authorizer, 0U, 7U, &fixture)
		|| GameSession::authorize_volcano_mark_use(
			&item_catalog_test_volcano_use_authorizer, 42U, 0U, &fixture)
		|| fixture.calls != 0U)
		return (FT_ERR_INVALID_STATE);
	if (GameSession::authorize_volcano_mark_use(
			&item_catalog_test_volcano_use_authorizer, 41U, 7U, &fixture)
		|| fixture.calls != 1U)
		return (FT_ERR_INVALID_STATE);
	fixture.allowed = true;
	if (!GameSession::authorize_volcano_mark_use(
			&item_catalog_test_volcano_use_authorizer, 42U, 7U, &fixture)
		|| GameSession::authorize_volcano_mark_use(
			&item_catalog_test_volcano_use_authorizer, 42U, 8U, &fixture)
		|| fixture.calls != 3U
		|| session.configure_volcano_mark_use_authorizer(nullptr, &fixture)
			!= FT_ERR_INVALID_ARGUMENT
		|| session.configure_volcano_mark_use_authorizer(
			&item_catalog_test_volcano_use_authorizer, &fixture)
			!= FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	return (FT_ERR_SUCCESS);
}

static int32_t item_catalog_validate_draw_instability_configuration() noexcept
{
	GameSession session;
	WorldChunkInstabilityPolicy policy = {};
	const uint32_t risk_curve[] = {500U, 1500U, 10000U};
	const uint32_t decreasing_curve[] = {500U, 400U};
	const uint32_t invalid_curve[] = {10001U};
	if (session.configure_draw_instability_deltas(1, 1)
		!= FT_ERR_CONFIGURATION)
		return (FT_ERR_INVALID_STATE);
	policy.maximum_value = 10U;
	policy.reward_threshold_count = 1U;
	policy.reward_thresholds[0] = 4U;
	policy.danger_threshold_count = 1U;
	policy.danger_thresholds[0] = 7U;
	if (session.configure_chunk_instability_policy(policy) != FT_ERR_SUCCESS
		|| session.configure_draw_instability_deltas(3, 5) != FT_ERR_SUCCESS
		|| session.configure_draw_instability_deltas(-1, 5)
			!= FT_ERR_CONFIGURATION
		|| session.configure_draw_instability_deltas(3, 11)
			!= FT_ERR_CONFIGURATION
		|| session.configure_volatile_stabilizer_policy(risk_curve, 3U, 2, 5)
			!= FT_ERR_SUCCESS
		|| session.configure_volatile_stabilizer_policy(
			decreasing_curve, 2U, 2, 5) != FT_ERR_INVALID_ARGUMENT
		|| session.configure_volatile_stabilizer_policy(
			invalid_curve, 1U, 2, 5) != FT_ERR_INVALID_ARGUMENT
		|| session.configure_volatile_stabilizer_policy(
			risk_curve, 3U, 11, 5) != FT_ERR_INVALID_ARGUMENT)
		return (FT_ERR_INVALID_STATE);
	policy.maximum_value = 4U;
	if (session.configure_chunk_instability_policy(policy)
		!= FT_ERR_OUT_OF_RANGE)
		return (FT_ERR_INVALID_STATE);
	return (FT_ERR_SUCCESS);
}

static int32_t item_catalog_validate_volcano_preview_footprint() noexcept
{
	static const uint32_t widths[] = {9U, 12U, 16U};
	GameSession session;
	RegenerationCardPreview preview = {};
	std::size_t index = 0U;

	while (index < sizeof(widths) / sizeof(widths[0]))
	{
		if (session.configure_volcano_mark_footprint(widths[index])
			!= FT_ERR_SUCCESS
			|| session.get_regeneration_card_preview(
				RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID,
				&preview) != FT_ERR_SUCCESS
			|| preview.required_chunk_width != widths[index])
			return (FT_ERR_INVALID_STATE);
		++index;
	}
	if (session.configure_volcano_mark_footprint(8U) != FT_ERR_INVALID_ARGUMENT
		|| session.configure_volcano_mark_footprint(17U)
			!= FT_ERR_INVALID_ARGUMENT
		|| session.get_regeneration_card_preview(
			RegenerationCardSession::FORTUNES_DRAW_CARD_DEFINITION_ID,
			&preview) != FT_ERR_SUCCESS
		|| preview.required_chunk_width != 0U)
		return (FT_ERR_INVALID_STATE);
	return (FT_ERR_SUCCESS);
}

static int32_t item_catalog_validate_recipe_source_configuration() noexcept
{
	GameSession session;
	int32_t result = session.configure_recipe_discovery_source(0U, 1U, 2008U);
	if (result != FT_ERR_INVALID_ARGUMENT
		|| session.configure_recipe_discovery_source(1U, 0U, 2008U)
			!= FT_ERR_INVALID_ARGUMENT
		|| session.configure_recipe_discovery_source(1U, 1U, 0U)
			!= FT_ERR_INVALID_ARGUMENT)
		return (FT_ERR_INVALID_STATE);
	result = session.configure_recipe_discovery_source(1U, 0x2012U, 2008U);
	if (result == FT_ERR_SUCCESS)
		result = session.configure_recipe_discovery_source(1U, 0x2012U, 2008U);
	if (result != FT_ERR_SUCCESS
		|| session.configure_recipe_discovery_source(1U, 0x2012U, 2009U)
			!= FT_ERR_ALREADY_EXISTS)
		return (FT_ERR_INVALID_STATE);
	for (uint32_t source_id = 1U; source_id <= 4095U; ++source_id)
	{
		result = session.configure_recipe_discovery_source(2U, source_id,
			2008U);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	if (session.configure_recipe_discovery_source(2U, 4096U, 2008U)
		!= FT_ERR_FULL)
		return (FT_ERR_INVALID_STATE);
	return (FT_ERR_SUCCESS);
}

static int32_t item_catalog_validate_enabled_card_previews(
	const MinecraftItemCatalog &items,
	const MinecraftRecipeCatalog &recipes) noexcept
{
	struct CardCatalogEntry
	{
		int32_t item_id;
		uint32_t definition_id;
		MinecraftItemAcquisitionPolicy acquisition;
		ft_bool initially_known;
	};
	static const CardCatalogEntry CARD_ENTRIES[] = {
		{MinecraftItemCatalog::BIOME_EXCLUSION_CARD_ITEM_ID, 1U,
			MINECRAFT_ITEM_ACQUISITION_RECIPE, FT_TRUE},
		{MinecraftItemCatalog::DEEP_SEAM_CARD_ITEM_ID, 2U,
			MINECRAFT_ITEM_ACQUISITION_RECIPE, FT_TRUE},
		{MinecraftItemCatalog::VILLAGE_CHARTER_CARD_ITEM_ID, 3U,
			MINECRAFT_ITEM_ACQUISITION_RECIPE, FT_TRUE},
		{MinecraftItemCatalog::SURFACE_MAGMA_CARD_ITEM_ID, 4U,
			MINECRAFT_ITEM_ACQUISITION_RECIPE, FT_TRUE},
		{MinecraftItemCatalog::DOUBLE_DRAFT_CARD_ITEM_ID, 5U,
			MINECRAFT_ITEM_ACQUISITION_RECIPE, FT_TRUE},
		{MinecraftItemCatalog::BIOME_COMPASS_CARD_ITEM_ID, 6U,
			MINECRAFT_ITEM_ACQUISITION_RECIPE, FT_TRUE},
		{MinecraftItemCatalog::SURVEYORS_REACH_CARD_ITEM_ID, 7U,
			MINECRAFT_ITEM_ACQUISITION_RECIPE, FT_TRUE},
		{MinecraftItemCatalog::VOLCANO_MARK_CARD_ITEM_ID, 15U,
			MINECRAFT_ITEM_ACQUISITION_DISCOVERY, FT_FALSE},
		{MinecraftItemCatalog::ULTIMATE_GREED_CARD_ITEM_ID, 12U,
			MINECRAFT_ITEM_ACQUISITION_DISCOVERY, FT_FALSE},
		{MinecraftItemCatalog::BIOME_RUPTURE_CARD_ITEM_ID, 13U,
			MINECRAFT_ITEM_ACQUISITION_DISCOVERY, FT_FALSE},
		{MinecraftItemCatalog::ORE_OVERGROWTH_CARD_ITEM_ID, 11U,
			MINECRAFT_ITEM_ACQUISITION_DISCOVERY, FT_FALSE},
		{MinecraftItemCatalog::SHIPWRECK_CHART_CARD_ITEM_ID,
			RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID,
			MINECRAFT_ITEM_ACQUISITION_DISCOVERY, FT_FALSE},
		{MinecraftItemCatalog::RUINED_VILLAGE_RELIC_CARD_ITEM_ID,
			RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID,
			MINECRAFT_ITEM_ACQUISITION_DISCOVERY, FT_FALSE},
		{MinecraftItemCatalog::UNSTABLE_CATALYST_CARD_ITEM_ID,
			RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID,
			MINECRAFT_ITEM_ACQUISITION_DISCOVERY, FT_FALSE},
		{MinecraftItemCatalog::DESERT_PYRAMID_SEAL_CARD_ITEM_ID,
			RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID,
			MINECRAFT_ITEM_ACQUISITION_DISCOVERY, FT_FALSE},
		{MinecraftItemCatalog::ANCIENT_CANOPY_SEAL_CARD_ITEM_ID,
			RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID,
			MINECRAFT_ITEM_ACQUISITION_DISCOVERY, FT_FALSE},
		{MinecraftItemCatalog::SPIDER_NEST_SEAL_CARD_ITEM_ID,
			RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID,
			MINECRAFT_ITEM_ACQUISITION_DISCOVERY, FT_FALSE},
		{MinecraftItemCatalog::VOLATILE_STABILIZER_CARD_ITEM_ID, 16U,
			MINECRAFT_ITEM_ACQUISITION_DISCOVERY, FT_FALSE},
		{MinecraftItemCatalog::FORTUNES_DRAW_CARD_ITEM_ID, 14U,
			MINECRAFT_ITEM_ACQUISITION_DISCOVERY, FT_FALSE}};
	uint32_t index = 0U;
	while (index < sizeof(CARD_ENTRIES) / sizeof(CARD_ENTRIES[0]))
	{
		const uint32_t recipe_id = 2001U + index;
		const CardCatalogEntry &entry = CARD_ENTRIES[index];
		ft_bool known = FT_FALSE;
		MinecraftContentItemRule item_rule = {};
		int32_t result = recipes.is_recipe_known(recipe_id, &known);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (known != entry.initially_known
			|| RegenerationCardSession::card_definition_for_item(entry.item_id)
			!= entry.definition_id)
			return (FT_ERR_INVALID_STATE);
		result = items.resolve_content_item_rule(entry.item_id, &item_rule);
		if (result != FT_ERR_SUCCESS
			|| item_rule.acquisition != entry.acquisition
			|| item_rule.max_stack != 1)
			return (result == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE : result);
		if (entry.definition_id != 0U)
		{
			RegenerationCardPreview preview = {};
			if (RegenerationCardSession::effect_kind_for_card(
					entry.definition_id) == 0
				|| RegenerationCardSession::get_card_preview(
					entry.definition_id, &preview) != FT_ERR_SUCCESS
				|| preview.card_definition_id != entry.definition_id
				|| preview.title == nullptr || preview.description == nullptr)
				return (FT_ERR_INVALID_STATE);
		}
		++index;
	}
	return (FT_ERR_SUCCESS);
}

struct ItemCatalogScriptAuthorizationFixture
{
	const uint8_t *bytes;
	uint32_t byte_count;
	uint64_t manifest_hash;
	uint32_t calls;
};

static bool item_catalog_test_script_authorizer(const uint8_t *bytes,
	uint32_t byte_count, uint64_t expected_manifest_hash,
	void *user_data) noexcept
{
	ItemCatalogScriptAuthorizationFixture *fixture =
		static_cast<ItemCatalogScriptAuthorizationFixture *>(user_data);
	if (fixture == nullptr)
		return (false);
	++fixture->calls;
	if (bytes == nullptr || byte_count != fixture->byte_count
		|| expected_manifest_hash != fixture->manifest_hash)
		return (false);
	for (uint32_t index = 0U; index < byte_count; ++index)
	{
		if (bytes[index] != fixture->bytes[index])
			return (false);
	}
	return (true);
}

static int32_t item_catalog_validate_script_catalog_authority() noexcept
{
	static const uint8_t CATALOG_BYTES[] = {0x43U, 0x47U, 0x53U, 0x43U,
		0x01U, 0x00U};
	static const uint8_t ALTERED_CATALOG_BYTES[] = {0x43U, 0x47U, 0x53U,
		0x43U, 0x01U, 0x01U};
	static const uint32_t CATALOG_BYTE_COUNT =
		static_cast<uint32_t>(sizeof(CATALOG_BYTES));
	ItemCatalogScriptAuthorizationFixture fixture = {CATALOG_BYTES,
		CATALOG_BYTE_COUNT, 0x1020304050607080ULL, 0U};
	if (GameSession::authorize_script_catalog(nullptr, CATALOG_BYTES,
			CATALOG_BYTE_COUNT, fixture.manifest_hash, &fixture)
		|| GameSession::authorize_script_catalog(
			&item_catalog_test_script_authorizer, nullptr,
			CATALOG_BYTE_COUNT, fixture.manifest_hash, &fixture)
		|| GameSession::authorize_script_catalog(
			&item_catalog_test_script_authorizer, CATALOG_BYTES,
			CATALOG_BYTE_COUNT, 0U, &fixture)
		|| fixture.calls != 0U
		|| GameSession::authorize_script_catalog(
			&item_catalog_test_script_authorizer, CATALOG_BYTES,
			CATALOG_BYTE_COUNT, fixture.manifest_hash + 1U, &fixture)
		|| fixture.calls != 1U
		|| GameSession::authorize_script_catalog(
			&item_catalog_test_script_authorizer, ALTERED_CATALOG_BYTES,
			CATALOG_BYTE_COUNT, fixture.manifest_hash, &fixture)
		|| fixture.calls != 2U
		|| !GameSession::authorize_script_catalog(
			&item_catalog_test_script_authorizer, CATALOG_BYTES,
			CATALOG_BYTE_COUNT, fixture.manifest_hash, &fixture)
		|| fixture.calls != 3U)
		return (FT_ERR_INVALID_STATE);
	return (FT_ERR_SUCCESS);
}

static int32_t item_catalog_validate_parallel_instance_ids(
	MinecraftRecipeCatalog &recipes)
{
	static const std::size_t THREAD_COUNT = 8U;
	static const std::size_t IDS_PER_THREAD = 64U;
	std::vector<uint64_t> instance_ids(THREAD_COUNT * IDS_PER_THREAD, 0U);
	std::vector<int32_t> thread_results(THREAD_COUNT, FT_ERR_SUCCESS);
	std::vector<std::thread> workers;

	try
	{
		workers.reserve(THREAD_COUNT);
		for (std::size_t thread_index = 0U;
			thread_index < THREAD_COUNT; ++thread_index)
		{
			workers.emplace_back([&recipes, &instance_ids, &thread_results,
				thread_index]()
			{
				const std::size_t begin = thread_index * IDS_PER_THREAD;
				for (std::size_t offset = 0U; offset < IDS_PER_THREAD; ++offset)
				{
					const int32_t result = recipes.allocate_output_instance_id(
						&instance_ids[begin + offset]);
					if (result != FT_ERR_SUCCESS)
					{
						thread_results[thread_index] = result;
						return ;
					}
				}
			});
		}
	}
	catch (...)
	{
		for (std::thread &worker : workers)
			if (worker.joinable())
				worker.join();
		return (FT_ERR_NO_MEMORY);
	}
	for (std::thread &worker : workers)
		worker.join();
	for (int32_t result : thread_results)
		if (result != FT_ERR_SUCCESS)
			return (result);
	std::sort(instance_ids.begin(), instance_ids.end());
	for (std::size_t index = 0U; index < instance_ids.size(); ++index)
		if (instance_ids[index] == 0U
			|| (index > 0U && instance_ids[index] == instance_ids[index - 1U]))
			return (FT_ERR_INVALID_STATE);
	return (FT_ERR_SUCCESS);
}

static int32_t item_catalog_validate_runtime_registration(
	MinecraftItemCatalog &catalog)
{
	voxel_block_registration registration = {};
	uint32_t block_id = 0U;
	uint32_t runtime_rule_count = 0U;
	const char *asset_path = "Libft/Test/Scripting/export_values.asset";
	int32_t error_code;

	registration.name = "test:minecraft_item_catalog_runtime_block";
	registration.metadata.solid = FT_TRUE;
	registration.metadata.occludes_faces = FT_TRUE;
	registration.metadata.breakable = FT_TRUE;
	registration.metadata.hardness = 1U;
	registration.metadata.light_attenuation = 15U;
	for (uint32_t face = 0U; face < VOXEL_BLOCK_ASSET_FACE_COUNT; ++face)
		registration.asset_paths[face] = asset_path;
	error_code = voxel_register_block(registration, &block_id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = catalog.validate_runtime_coverage(&runtime_rule_count);
	if (error_code == FT_ERR_SUCCESS)
		error_code = FT_ERR_INVALID_STATE;
	else if (error_code == FT_ERR_INVALID_STATE)
		error_code = FT_ERR_SUCCESS;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.register_runtime_block_rule(block_id,
			MINECRAFT_BLOCK_ACQUISITION_SELF_ITEM,
			MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
				+ static_cast<int32_t>(block_id));
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.validate_runtime_coverage(&runtime_rule_count);
	if (error_code == FT_ERR_SUCCESS && runtime_rule_count != 1U)
		error_code = FT_ERR_INVALID_STATE;
	{
		int32_t cleanup_error = voxel_unregister_block(block_id);
		if (error_code == FT_ERR_SUCCESS)
			error_code = cleanup_error;
	}
	return (error_code);
}

static int32_t item_catalog_validate_recipes(
	const MinecraftItemCatalog &items)
{
	MinecraftRecipeCatalog recipes;
	game_inventory inventory;
	game_inventory container_inventory;
	game_inventory card_inventory;
	game_crafting_prepared prepared;
	game_crafting_prepared container_prepared;
	game_crafting_prepared card_prepared;
	ft_sharedptr<game_item> log_item;
	ft_sharedptr<game_item> paper_fiber;
	ft_sharedptr<game_item> water_bucket;
	ft_sharedptr<game_item> card_stock;
	ft_sharedptr<game_item> advanced_ink;
	ft_sharedptr<game_item> quartz_sample;
	ft_sharedptr<game_item> card_planks;
	ft_sharedptr<game_item> copper_bar;
	ft_sharedptr<game_item> seagrass_sample;
	ft_vector<game_crafting_missing_ingredient> missing_groups;
	game_crafting_query_result query_result = {};
	ft_bool known = FT_FALSE;
	ft_bool newly_unlocked = FT_FALSE;
	uint64_t card_instance_id = 0U;
	uint64_t inventory_revision = 0U;
	uint64_t crafted_revision = 0U;
	MinecraftCraftActionResult craft_action_result = {};
	uint32_t recipe_output_count = 0U;
	int32_t missing_recipe_item_id = 0;
	game_inventory_snapshot card_inventory_snapshot = {};
	std::vector<int32_t> item_tag_ids;
	std::vector<uint8_t> recipe_book_before_discovery;
	ItemCatalogTestSaveContext save_context = {FT_ERR_DISK_FULL, 0U};
	GameSessionSnapshotWriter snapshot_writer = {
		&item_catalog_test_save, &save_context};
	const int32_t log_item_id = MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
		+ static_cast<int32_t>(VOXEL_GENERATOR_OAK_LOG_BLOCK);
	int32_t error_code = items.resolve_item_tag(
		MINECRAFT_ITEM_TAG_WOOD_LOGS, item_tag_ids);
	if (error_code == FT_ERR_SUCCESS
		&& (item_tag_ids.size() != 3U || item_tag_ids[0] != log_item_id
			|| item_tag_ids[1] != MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
				+ static_cast<int32_t>(VOXEL_GENERATOR_PINE_LOG_BLOCK)
			|| item_tag_ids[2] != MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
				+ static_cast<int32_t>(VOXEL_GENERATOR_BIRCH_LOG_BLOCK)))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.resolve_item_tag(MINECRAFT_ITEM_TAG_WOOD_PLANKS,
			item_tag_ids);
	if (error_code == FT_ERR_SUCCESS
		&& (item_tag_ids.size() != 3U
			|| item_tag_ids[0] != MinecraftItemCatalog::OAK_PLANKS_ITEM_ID
			|| item_tag_ids[1] != MinecraftItemCatalog::PINE_PLANKS_ITEM_ID
			|| item_tag_ids[2] != MinecraftItemCatalog::BIRCH_PLANKS_ITEM_ID))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.resolve_item_tag(MINECRAFT_ITEM_TAG_ORE_SAMPLES,
			item_tag_ids);
	if (error_code == FT_ERR_SUCCESS
		&& (item_tag_ids.size() != 6U
			|| item_tag_ids[0] != MinecraftItemCatalog::COAL_ITEM_ID
			|| item_tag_ids[1] != MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
				+ static_cast<int32_t>(VOXEL_GENERATOR_IRON_ORE_BLOCK)))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.resolve_item_tag(MINECRAFT_ITEM_TAG_FLOWER_PIGMENTS,
			item_tag_ids);
	if (error_code == FT_ERR_SUCCESS
		&& (item_tag_ids.size() != 2U
			|| item_tag_ids[0] != MinecraftItemCatalog::RED_PIGMENT_ITEM_ID
			|| item_tag_ids[1] != MinecraftItemCatalog::YELLOW_PIGMENT_ITEM_ID))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		item_tag_ids.assign(1U, 1234567);
		if (items.resolve_item_tag(UINT32_MAX, item_tag_ids) != FT_ERR_NOT_FOUND
			|| item_tag_ids.size() != 1U || item_tag_ids[0] != 1234567)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.initialize(items);
	if (error_code == FT_ERR_SUCCESS)
		error_code = item_catalog_validate_parallel_instance_ids(recipes);
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.validate_recipe_coverage(items,
			&recipe_output_count, &missing_recipe_item_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = item_catalog_validate_enabled_card_previews(items,
			recipes);
	if (error_code != FT_ERR_SUCCESS && missing_recipe_item_id != 0)
		std::fprintf(stderr, "item-catalog: recipe coverage item=%d error=%d\n",
			missing_recipe_item_id, error_code);

	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.is_recipe_known(1001U, &known);
	if (error_code == FT_ERR_SUCCESS
		&& (known != FT_TRUE || !recipes.is_initialized()))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.is_recipe_known(1023U, &known);
	if (error_code == FT_ERR_SUCCESS && known != FT_FALSE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.is_recipe_known(1027U, &known);
	if (error_code == FT_ERR_SUCCESS && known != FT_FALSE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.is_recipe_known(1028U, &known);
	if (error_code == FT_ERR_SUCCESS && known != FT_FALSE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.is_recipe_known(2001U, &known);
	if (error_code == FT_ERR_SUCCESS && known != FT_TRUE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.is_recipe_known(
			MinecraftRecipeCatalog::VOLCANO_MARK_CARD_RECIPE_ID, &known);
	if (error_code == FT_ERR_SUCCESS && known != FT_FALSE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.is_recipe_known(1026U, &known);
	if (error_code == FT_ERR_SUCCESS && known != FT_FALSE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.initialize(4, 0);
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.create_item(log_item_id, 1, &log_item);
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.add_item(log_item);
	if (error_code == FT_ERR_SUCCESS)
		error_code = missing_groups.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.crafting().query_recipe(1001, inventory,
			&query_result, &missing_groups);
	if (error_code == FT_ERR_SUCCESS
		&& (query_result.found != FT_TRUE || query_result.known != FT_TRUE
			|| query_result.craftable != FT_TRUE
			|| missing_groups.size() != 0U))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.crafting().prepare_craft(inventory, 1001, 1,
			&prepared);
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.crafting().commit_craft(inventory, &prepared);
	if (error_code == FT_ERR_SUCCESS
		&& (inventory.count_item(log_item_id) != 0
			|| inventory.count_item(MinecraftItemCatalog::OAK_PLANKS_ITEM_ID) != 4))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.add_item(log_item);
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.get_revision(&inventory_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.craft_hand_recipe(inventory, 1U, 1001U, 1,
			inventory_revision, &craft_action_result);
	if (error_code == FT_ERR_SUCCESS
		&& (craft_action_result.status != FT_ERR_SUCCESS
			|| craft_action_result.replayed
			|| inventory.count_item(log_item_id) != 0
			|| inventory.count_item(MinecraftItemCatalog::OAK_PLANKS_ITEM_ID)
				!= 8))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.get_revision(&crafted_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.craft_hand_recipe(inventory, 1U, 1001U, 1,
			inventory_revision, &craft_action_result);
	if (error_code == FT_ERR_SUCCESS
		&& (!craft_action_result.replayed
			|| craft_action_result.inventory_revision != crafted_revision
			|| inventory.count_item(MinecraftItemCatalog::OAK_PLANKS_ITEM_ID)
				!= 8))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& recipes.craft_hand_recipe(inventory, 1U, 1002U, 1,
			inventory_revision, &craft_action_result)
		!= FT_ERR_INVALID_ARGUMENT)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& recipes.craft_hand_recipe(inventory, 2U, 1001U, 1,
			inventory_revision, &craft_action_result)
		!= FT_ERR_INVALID_STATE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& recipes.craft_hand_recipe(inventory, 2U, 1001U, 1,
			inventory_revision, &craft_action_result)
		!= FT_ERR_INVALID_STATE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS && !craft_action_result.replayed)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		query_result = {};
		error_code = recipes.query_hand_recipe(inventory, 1006U,
			&query_result, &missing_groups);
	}
	if (error_code == FT_ERR_SUCCESS
		&& query_result.station_available != FT_FALSE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = container_inventory.initialize(4, 0);
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.create_item(
			MinecraftItemCatalog::PAPER_REED_FIBER_ITEM_ID, 3, &paper_fiber);
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.create_item(
			MinecraftItemCatalog::WATER_BUCKET_ITEM_ID, 1, &water_bucket);
	if (error_code == FT_ERR_SUCCESS)
		error_code = container_inventory.add_item(paper_fiber);
	if (error_code == FT_ERR_SUCCESS)
		error_code = container_inventory.add_item(water_bucket);
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.crafting().prepare_craft_at_station(
			container_inventory, 1015,
			MinecraftRecipeCatalog::PAPER_MILL_STATION_ID, 1,
			&container_prepared);
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.crafting().commit_craft(container_inventory,
			&container_prepared);
	if (error_code == FT_ERR_SUCCESS
		&& (container_inventory.count_item(
			MinecraftItemCatalog::WATER_BUCKET_ITEM_ID) != 0
			|| container_inventory.count_item(
				MinecraftItemCatalog::EMPTY_BUCKET_ITEM_ID) != 1
			|| container_inventory.count_item(
				MinecraftItemCatalog::PAPER_SHEET_ITEM_ID) != 3))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.serialize_recipe_book(
			recipe_book_before_discovery);
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.register_discovery_source(1U, 0x2012U,
			MinecraftRecipeCatalog::VOLCANO_MARK_CARD_RECIPE_ID);
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.register_discovery_source(1U, 0x2012U, 2013U)
			== FT_ERR_ALREADY_EXISTS ? FT_ERR_SUCCESS : FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		const int32_t failed_save_result = RecipeDiscoveryTransaction::execute(
			recipes, 1U, 0x2012U, 0x4341524400002012ULL,
			snapshot_writer, &newly_unlocked);
		if (failed_save_result != FT_ERR_DISK_FULL
			|| newly_unlocked != FT_FALSE || save_context.calls != 1U)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.is_recipe_known(
			MinecraftRecipeCatalog::VOLCANO_MARK_CARD_RECIPE_ID, &known);
	if (error_code == FT_ERR_SUCCESS && known != FT_FALSE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.crafting().is_recipe_unlocked(
			static_cast<int32_t>(
				MinecraftRecipeCatalog::VOLCANO_MARK_CARD_RECIPE_ID), &known);
	if (error_code == FT_ERR_SUCCESS && known != FT_FALSE)
		error_code = FT_ERR_INVALID_STATE;
	save_context.result = FT_ERR_SUCCESS;
	if (error_code == FT_ERR_SUCCESS)
		error_code = RecipeDiscoveryTransaction::execute(recipes, 1U,
			0x2012U, 0x4341524400002012ULL, snapshot_writer,
			&newly_unlocked);
	if (error_code == FT_ERR_SUCCESS
		&& (newly_unlocked != FT_TRUE || save_context.calls != 2U))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.grant_recipe_from_discovery(1U, 0x2012U,
			0x4341524400002012ULL, &newly_unlocked);
	if (error_code == FT_ERR_SUCCESS && newly_unlocked != FT_FALSE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.register_discovery_source(1U, 0x2012U,
			MinecraftRecipeCatalog::VOLCANO_MARK_CARD_RECIPE_ID);
	if (error_code == FT_ERR_SUCCESS
		&& recipes.grant_recipe_from_discovery(1U, 0x2013U,
			0x4341524400002013ULL, &newly_unlocked) != FT_ERR_NOT_FOUND)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.restore_recipe_book(
			recipe_book_before_discovery.data(),
			recipe_book_before_discovery.size());
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.is_recipe_known(
			MinecraftRecipeCatalog::VOLCANO_MARK_CARD_RECIPE_ID, &known);
	if (error_code == FT_ERR_SUCCESS && known != FT_FALSE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.crafting().is_recipe_unlocked(
			static_cast<int32_t>(
				MinecraftRecipeCatalog::VOLCANO_MARK_CARD_RECIPE_ID), &known);
	if (error_code == FT_ERR_SUCCESS && known != FT_FALSE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.grant_recipe_from_discovery(1U, 0x2012U,
			0x4341524400002014ULL, &newly_unlocked);
	if (error_code == FT_ERR_SUCCESS && newly_unlocked != FT_TRUE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.register_discovery_source(2U, 0x2008U,
			MinecraftRecipeCatalog::VOLCANO_MARK_CARD_RECIPE_ID);
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.grant_recipe_from_discovery(2U, 0x2008U,
			0x4341524400002008ULL, &newly_unlocked);
	if (error_code == FT_ERR_SUCCESS && newly_unlocked != FT_FALSE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.register_discovery_source(3U, 0x2012U,
			2012U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.grant_recipe_from_discovery(3U, 0x2012U,
			0x4341524400002012ULL, &newly_unlocked);
	if (error_code == FT_ERR_SUCCESS && newly_unlocked != FT_TRUE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = card_inventory.initialize(8, 0);
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.create_item(
			MinecraftItemCatalog::CARD_STOCK_ITEM_ID, 2, &card_stock);
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.create_item(
			MinecraftItemCatalog::ADVANCED_INK_ITEM_ID, 1, &advanced_ink);
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.create_item(log_item_id -
			static_cast<int32_t>(VOXEL_GENERATOR_OAK_LOG_BLOCK)
			+ static_cast<int32_t>(VOXEL_GENERATOR_QUARTZ_BLOCK),
			2, &quartz_sample);
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.create_item(
			MinecraftItemCatalog::OAK_PLANKS_ITEM_ID, 2, &card_planks);
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.create_item(
			MinecraftItemCatalog::COPPER_BAR_ITEM_ID, 1, &copper_bar);
	if (error_code == FT_ERR_SUCCESS)
		error_code = items.create_item(
			MinecraftItemCatalog::PAPER_REED_FIBER_ITEM_ID, 1,
			&seagrass_sample);
	if (error_code == FT_ERR_SUCCESS)
		error_code = card_inventory.add_item(card_stock);
	if (error_code == FT_ERR_SUCCESS)
		error_code = card_inventory.add_item(advanced_ink);
	if (error_code == FT_ERR_SUCCESS)
		error_code = card_inventory.add_item(quartz_sample);
	if (error_code == FT_ERR_SUCCESS)
		error_code = card_inventory.add_item(card_planks);
	if (error_code == FT_ERR_SUCCESS)
		error_code = card_inventory.add_item(copper_bar);
	if (error_code == FT_ERR_SUCCESS)
		error_code = card_inventory.add_item(seagrass_sample);
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.crafting().prepare_craft_at_station(
			card_inventory, 2012,
			MinecraftRecipeCatalog::CARD_PRESS_STATION_ID, 1, &card_prepared);
	if (error_code == FT_ERR_SUCCESS)
		error_code = recipes.commit_unique_craft(card_inventory,
			&card_prepared, &card_instance_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = card_inventory.get_snapshot(card_inventory_snapshot);
	if (error_code == FT_ERR_SUCCESS
		&& (card_inventory.count_item(
			MinecraftItemCatalog::SHIPWRECK_CHART_CARD_ITEM_ID) != 1
			|| card_inventory.count_item(
				MinecraftItemCatalog::CARD_STOCK_ITEM_ID) != 0
			|| card_inventory.count_item(
				MinecraftItemCatalog::ADVANCED_INK_ITEM_ID) != 0
			|| card_inventory.count_item(
				MinecraftItemCatalog::OAK_PLANKS_ITEM_ID) != 0
			|| card_inventory.count_item(
				MinecraftItemCatalog::COPPER_BAR_ITEM_ID) != 0
			|| card_inventory.count_item(
				MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
					+ static_cast<int32_t>(VOXEL_GENERATOR_QUARTZ_BLOCK)) != 0
			|| card_inventory.count_item(
				MinecraftItemCatalog::PAPER_REED_FIBER_ITEM_ID) != 0
			|| card_inventory.count_item(
				MinecraftItemCatalog::PAPER_SHEET_ITEM_ID) != 0))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		ft_bool card_has_minted_id = FT_FALSE;
		std::size_t item_index = 0U;

		while (item_index < card_inventory_snapshot.items.size())
		{
			const game_inventory_item_snapshot &item =
				card_inventory_snapshot.items[item_index];
			if (item.item_id == MinecraftItemCatalog::SHIPWRECK_CHART_CARD_ITEM_ID
				&& item.stack_size == 1 && item.instance_id == card_instance_id
				&& card_instance_id != 0U)
				card_has_minted_id = FT_TRUE;
			item_index += 1U;
		}
		if (card_has_minted_id != FT_TRUE)
			error_code = FT_ERR_INVALID_STATE;
	}
	(void)inventory.destroy();
	(void)container_inventory.destroy();
	(void)card_inventory.destroy();
	(void)recipes.destroy();
	return (error_code);
}

ItemCatalogValidator::ItemCatalogValidator()
{
}

ItemCatalogValidator::ItemCatalogValidator(const ItemCatalogValidator &other)
	: IValidator(other)
{
	(void)other;
}

ItemCatalogValidator::~ItemCatalogValidator()
{
}

ItemCatalogValidator &ItemCatalogValidator::operator=(
	const ItemCatalogValidator &other)
{
	(void)other;
	return (*this);
}

static int32_t item_catalog_test_revision_occupancy_provider(
	std::vector<World::RevisionChunkCoordinate> &chunks_out,
	void *user_data) noexcept
{
	if (user_data == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		chunks_out.push_back({-3, 7});
		chunks_out.push_back({4, -9});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

static int32_t item_catalog_validate_revision_occupancy_configuration()
{
	GameSession session;
	int32_t fixture = 1;
	if (session.configure_revision_occupancy_provider(nullptr, &fixture)
			!= FT_ERR_INVALID_ARGUMENT
		|| session.configure_revision_occupancy_provider(
			&item_catalog_test_revision_occupancy_provider, nullptr)
			!= FT_ERR_SUCCESS
		|| session.configure_revision_occupancy_provider(nullptr, nullptr)
			!= FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	return (FT_ERR_SUCCESS);
}

int ItemCatalogValidator::validate() const
{
	MinecraftItemCatalog catalog;
	MinecraftBlockItemRule rule = {};
	MinecraftContentItemRule content_rule = {};
	MinecraftFluidContainerRule fluid_rule = {};
	uint32_t placed_block_id = 0U;
	uint32_t builtin_rule_count = 0U;
	uint32_t runtime_rule_count = 0U;
	uint32_t content_rule_count = 0U;
	int32_t error_code;

	error_code = item_catalog_validate_recipe_discovery_authority();
	if (error_code == FT_ERR_SUCCESS)
		error_code = item_catalog_validate_volcano_use_authority();
	if (error_code == FT_ERR_SUCCESS)
		error_code = item_catalog_validate_draw_instability_configuration();
	if (error_code == FT_ERR_SUCCESS)
		error_code = item_catalog_validate_volcano_preview_footprint();
	if (error_code == FT_ERR_SUCCESS)
		error_code = item_catalog_validate_recipe_source_configuration();
	if (error_code == FT_ERR_SUCCESS)
		error_code = item_catalog_validate_revision_occupancy_configuration();
	if (error_code == FT_ERR_SUCCESS)
		error_code = item_catalog_validate_script_catalog_authority();
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.initialize();
	if (error_code == FT_ERR_SUCCESS)
	{
		const int32_t unique_items[] = {
			MinecraftItemCatalog::BOAT_ITEM_ID,
			MinecraftItemCatalog::WOOD_PICKAXE_ITEM_ID,
			MinecraftItemCatalog::BIOME_EXCLUSION_CARD_ITEM_ID,
			MinecraftItemCatalog::WATER_BUCKET_ITEM_ID};
		for (std::size_t index = 0U;
			index < sizeof(unique_items) / sizeof(unique_items[0]); ++index)
		{
			bool required = false;
			error_code = catalog.requires_physical_instance_id(
				unique_items[index], &required);
			if (error_code != FT_ERR_SUCCESS || !required)
			{
				if (error_code == FT_ERR_SUCCESS)
					error_code = FT_ERR_INVALID_STATE;
				break ;
			}
		}
		for (int32_t item_id = MinecraftItemCatalog::WOOD_PICKAXE_ITEM_ID;
			item_id <= MinecraftItemCatalog::IRON_BOOTS_ITEM_ID
				&& error_code == FT_ERR_SUCCESS; ++item_id)
		{
			bool required = false;
			error_code = catalog.requires_physical_instance_id(item_id,
				&required);
			if (error_code != FT_ERR_SUCCESS || !required)
			{
				if (error_code == FT_ERR_SUCCESS)
					error_code = FT_ERR_INVALID_STATE;
				break ;
			}
		}
		for (int32_t item_id =
			MinecraftItemCatalog::BIOME_EXCLUSION_CARD_ITEM_ID;
			item_id <= MinecraftItemCatalog::VOLATILE_STABILIZER_CARD_ITEM_ID
				&& error_code == FT_ERR_SUCCESS; ++item_id)
		{
			bool required = false;
			error_code = catalog.requires_physical_instance_id(item_id,
				&required);
			if (error_code != FT_ERR_SUCCESS || !required)
			{
				if (error_code == FT_ERR_SUCCESS)
					error_code = FT_ERR_INVALID_STATE;
				break ;
			}
		}
		if (error_code == FT_ERR_SUCCESS)
		{
			bool required = false;
			error_code = catalog.requires_physical_instance_id(
				MinecraftItemCatalog::FORTUNES_DRAW_CARD_ITEM_ID, &required);
			if (error_code == FT_ERR_SUCCESS && !required)
				error_code = FT_ERR_INVALID_STATE;
		}
		if (error_code == FT_ERR_SUCCESS)
		{
			bool required = true;
			error_code = catalog.requires_physical_instance_id(
				MinecraftItemCatalog::EMPTY_BUCKET_ITEM_ID, &required);
			if (error_code == FT_ERR_SUCCESS && required)
				error_code = FT_ERR_INVALID_STATE;
		}
		game_inventory_snapshot identity_snapshot = {};
		game_inventory_item_snapshot identity_item = {};
		identity_snapshot.capacity = 1U;
		identity_item.slot = 0;
		identity_item.item_id = MinecraftItemCatalog::BOAT_ITEM_ID;
		identity_item.stack_size = 1;
		identity_item.max_stack = 1;
		identity_item.width = 1;
		identity_item.height = 1;
		identity_snapshot.items.push_back(identity_item);
		if (error_code == FT_ERR_SUCCESS
			&& catalog.validate_inventory_instance_policy(identity_snapshot)
			!= FT_ERR_INVALID_STATE)
			error_code = FT_ERR_INVALID_STATE;
		identity_snapshot.items[0].instance_id = UINT64_C(0x123456789ABCDEF0);
		if (error_code == FT_ERR_SUCCESS)
			error_code = catalog.validate_inventory_instance_policy(
				identity_snapshot);
		identity_snapshot.items[0].stack_size = 2;
		if (error_code == FT_ERR_SUCCESS
			&& catalog.validate_inventory_instance_policy(identity_snapshot)
			!= FT_ERR_INVALID_STATE)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.validate_builtin_coverage(&builtin_rule_count);
	if (error_code == FT_ERR_SUCCESS
		&& builtin_rule_count != VOXEL_BUILTIN_BLOCK_COUNT)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_builtin_block(
			VOXEL_GENERATOR_AIR_BLOCK, &rule);
	if (error_code == FT_ERR_SUCCESS
		&& (rule.policy != MINECRAFT_BLOCK_ACQUISITION_NONE
			|| rule.item_id != 0))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_builtin_block(
			VOXEL_GENERATOR_GRASS_BLOCK_ID, &rule);
	if (error_code == FT_ERR_SUCCESS
		&& (rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM
			|| rule.item_id != MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
				+ static_cast<int32_t>(VOXEL_GENERATOR_DIRT_BLOCK_ID)))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_builtin_block(
			VOXEL_GENERATOR_WATER_BLOCK_ID, &rule);
	if (error_code == FT_ERR_SUCCESS
		&& (rule.policy != MINECRAFT_BLOCK_ACQUISITION_CONTAINER
			|| rule.item_id != MinecraftItemCatalog::WATER_BUCKET_ITEM_ID))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_builtin_block(
			VOXEL_GENERATOR_LAVA_BLOCK_ID, &rule);
	if (error_code == FT_ERR_SUCCESS
		&& (rule.policy != MINECRAFT_BLOCK_ACQUISITION_CONTAINER
			|| rule.item_id != MinecraftItemCatalog::LAVA_BUCKET_ITEM_ID))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_builtin_block(
			VOXEL_GENERATOR_FLOWING_WATER_BLOCK_ID, &rule);
	if (error_code == FT_ERR_SUCCESS
		&& (rule.policy != MINECRAFT_BLOCK_ACQUISITION_NONE
			|| rule.item_id != 0
			|| voxel_block_is_liquid(
				VOXEL_GENERATOR_FLOWING_WATER_BLOCK_ID) != FT_TRUE))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_builtin_block(
			VOXEL_GENERATOR_FLOWING_LAVA_BLOCK_ID, &rule);
	if (error_code == FT_ERR_SUCCESS
		&& (rule.policy != MINECRAFT_BLOCK_ACQUISITION_NONE
			|| rule.item_id != 0
			|| voxel_block_is_liquid(
				VOXEL_GENERATOR_FLOWING_LAVA_BLOCK_ID) != FT_TRUE))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& (voxel_block_is_fluid_source(VOXEL_GENERATOR_WATER_BLOCK_ID)
			!= FT_TRUE
			|| voxel_block_is_fluid_source(VOXEL_GENERATOR_LAVA_BLOCK_ID)
			!= FT_TRUE
			|| voxel_block_is_fluid_source(
				VOXEL_GENERATOR_FLOWING_WATER_BLOCK_ID) != FT_FALSE
			|| voxel_block_is_fluid_source(
				VOXEL_GENERATOR_FLOWING_LAVA_BLOCK_ID) != FT_FALSE
			|| voxel_block_is_flowing_fluid(
				VOXEL_GENERATOR_FLOWING_WATER_BLOCK_ID) != FT_TRUE
			|| voxel_block_is_flowing_fluid(
				VOXEL_GENERATOR_FLOWING_LAVA_BLOCK_ID) != FT_TRUE))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_builtin_block(
			VOXEL_GENERATOR_COAL_ORE_BLOCK, &rule);
	if (error_code == FT_ERR_SUCCESS
		&& (rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM
			|| rule.item_id != MinecraftItemCatalog::COAL_ITEM_ID))
		error_code = FT_ERR_INVALID_STATE;
	const uint32_t fiber_blocks[] = {
		VOXEL_GENERATOR_TALL_GRASS_BLOCK,
		VOXEL_GENERATOR_FERN_BLOCK,
		VOXEL_GENERATOR_DEAD_BUSH_BLOCK};
	for (std::size_t index = 0U;
		index < sizeof(fiber_blocks) / sizeof(fiber_blocks[0])
			&& error_code == FT_ERR_SUCCESS; ++index)
	{
		error_code = catalog.resolve_builtin_block(fiber_blocks[index], &rule);
		if (error_code == FT_ERR_SUCCESS
			&& (rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM
				|| rule.item_id != MinecraftItemCatalog::PLANT_FIBER_ITEM_ID))
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_builtin_block(
			VOXEL_GENERATOR_SEAGRASS_BLOCK, &rule);
	if (error_code == FT_ERR_SUCCESS
		&& (rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM
			|| rule.item_id
				!= MinecraftItemCatalog::PAPER_REED_FIBER_ITEM_ID))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_builtin_block(
			VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID, &rule);
	if (error_code == FT_ERR_SUCCESS
		&& (rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM
			|| rule.item_id
				!= MinecraftItemCatalog::PAPER_REED_FIBER_ITEM_ID))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_placed_block_item(
			MinecraftItemCatalog::PAPER_REED_FIBER_ITEM_ID,
			&placed_block_id);
	if (error_code == FT_ERR_SUCCESS
		&& placed_block_id
			!= VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_builtin_block(
			VOXEL_GENERATOR_RED_FLOWER_BLOCK, &rule);
	if (error_code == FT_ERR_SUCCESS
		&& (rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM
			|| rule.item_id != MinecraftItemCatalog::RED_PIGMENT_ITEM_ID))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_builtin_block(
			VOXEL_GENERATOR_YELLOW_FLOWER_BLOCK, &rule);
	if (error_code == FT_ERR_SUCCESS
		&& (rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM
			|| rule.item_id != MinecraftItemCatalog::YELLOW_PIGMENT_ITEM_ID))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_builtin_block(
			VOXEL_GENERATOR_FURNACE_BLOCK_ID, &rule);
	if (error_code == FT_ERR_SUCCESS
		&& (rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM
			|| rule.item_id != MinecraftItemCatalog::FURNACE_ITEM_ID
			|| voxel_block_is_solid(VOXEL_GENERATOR_FURNACE_BLOCK_ID)
				!= FT_TRUE
			|| voxel_block_is_liquid(VOXEL_GENERATOR_FURNACE_BLOCK_ID)
				!= FT_FALSE))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_placed_block_item(
			MinecraftItemCatalog::FURNACE_ITEM_ID, &placed_block_id);
	if (error_code == FT_ERR_SUCCESS
		&& placed_block_id != VOXEL_GENERATOR_FURNACE_BLOCK_ID)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		const uint32_t station_block_ids[] = {
			VOXEL_GENERATOR_WORKBENCH_BLOCK_ID,
			VOXEL_GENERATOR_PAPER_MILL_BLOCK_ID,
			VOXEL_GENERATOR_CARD_PRESS_BLOCK_ID};
		const int32_t station_item_ids[] = {
			MinecraftItemCatalog::WORKBENCH_ITEM_ID,
			MinecraftItemCatalog::PAPER_MILL_ITEM_ID,
			MinecraftItemCatalog::CARD_PRESS_ITEM_ID};
		for (std::size_t index = 0U;
			index < sizeof(station_block_ids) / sizeof(station_block_ids[0]);
			++index)
		{
			error_code = catalog.resolve_builtin_block(
				station_block_ids[index], &rule);
			if (error_code != FT_ERR_SUCCESS)
				break ;
			if (rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM
				|| rule.item_id != station_item_ids[index]
				|| voxel_block_is_solid(station_block_ids[index]) != FT_TRUE
				|| voxel_block_is_liquid(station_block_ids[index]) != FT_FALSE)
			{
				error_code = FT_ERR_INVALID_STATE;
				break ;
			}
			error_code = catalog.resolve_placed_block_item(
				station_item_ids[index], &placed_block_id);
			if (error_code != FT_ERR_SUCCESS
				|| placed_block_id != station_block_ids[index])
			{
				if (error_code == FT_ERR_SUCCESS)
					error_code = FT_ERR_INVALID_STATE;
				break ;
			}
		}
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_placed_block_item(
			MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
				+ static_cast<int32_t>(VOXEL_GENERATOR_STONE_BLOCK_ID),
			&placed_block_id);
	if (error_code == FT_ERR_SUCCESS
		&& placed_block_id != VOXEL_GENERATOR_STONE_BLOCK_ID)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_builtin_block(
			VOXEL_GENERATOR_BEDROCK_BLOCK_ID, &rule);
	if (error_code == FT_ERR_SUCCESS
		&& rule.policy != MINECRAFT_BLOCK_ACQUISITION_ADMIN_ONLY)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_content_item_rule(
			MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
				+ static_cast<int32_t>(VOXEL_GENERATOR_BEDROCK_BLOCK_ID),
			&content_rule);
	if (error_code == FT_ERR_SUCCESS
		&& content_rule.acquisition != MINECRAFT_ITEM_ACQUISITION_RESTRICTED)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_content_item_rule(
			MinecraftItemCatalog::WATER_BUCKET_ITEM_ID, &content_rule);
	if (error_code == FT_ERR_SUCCESS
		&& content_rule.acquisition != MINECRAFT_ITEM_ACQUISITION_WORLD)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_content_item_rule(
			MinecraftItemCatalog::LAVA_BUCKET_ITEM_ID, &content_rule);
	if (error_code == FT_ERR_SUCCESS
		&& (content_rule.max_stack != 1
			|| content_rule.acquisition != MINECRAFT_ITEM_ACQUISITION_WORLD))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_content_item_rule(
			MinecraftItemCatalog::EMPTY_BUCKET_ITEM_ID, &content_rule);
	if (error_code == FT_ERR_SUCCESS
		&& (content_rule.max_stack != 16
			|| content_rule.acquisition != MINECRAFT_ITEM_ACQUISITION_RECIPE))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_fluid_container_rule(
			MinecraftItemCatalog::EMPTY_BUCKET_ITEM_ID, &fluid_rule);
	if (error_code == FT_ERR_SUCCESS
		&& (fluid_rule.capacity != 1U
			|| fluid_rule.fluid_kind != MINECRAFT_FLUID_NONE))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_fluid_container_rule(
			MinecraftItemCatalog::WATER_BUCKET_ITEM_ID, &fluid_rule);
	if (error_code == FT_ERR_SUCCESS
		&& (fluid_rule.capacity != 1U
			|| fluid_rule.fluid_kind != MINECRAFT_FLUID_WATER))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_fluid_container_rule(
			MinecraftItemCatalog::LAVA_BUCKET_ITEM_ID, &fluid_rule);
	if (error_code == FT_ERR_SUCCESS
		&& (fluid_rule.capacity != 1U
			|| fluid_rule.fluid_kind != MINECRAFT_FLUID_LAVA))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		const int32_t non_container_lookup = catalog.resolve_fluid_container_rule(
			MinecraftItemCatalog::MAGICAL_DUST_ITEM_ID, &fluid_rule);

		if (non_container_lookup != FT_ERR_NOT_FOUND)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		game_inventory_snapshot stacked_empty = {};
		game_inventory_snapshot filled_snapshot = {};
		game_inventory_snapshot failed_output = {};
		game_inventory_snapshot failed_sentinel = {};
		game_inventory_snapshot identified_empty = {};
		game_inventory_snapshot identified_filled = {};
		game_inventory_snapshot identified_poured = {};
		game_inventory_item_snapshot empty_entry = {};
		game_inventory_item_snapshot identified_entry = {};

		stacked_empty.capacity = 2U;
		stacked_empty.used_slots = 1U;
		stacked_empty.weight_limit = 100;
		stacked_empty.current_weight = 2;
		stacked_empty.next_slot = 1;
		stacked_empty.revision = 3U;
		empty_entry.slot = 0;
		empty_entry.item_id = MinecraftItemCatalog::EMPTY_BUCKET_ITEM_ID;
		empty_entry.stack_size = 2;
		empty_entry.max_stack = 16;
		empty_entry.width = 1;
		empty_entry.height = 1;
		stacked_empty.items.push_back(empty_entry);
		error_code = catalog.prepare_fluid_transfer_snapshot(stacked_empty,
			0, 3U, true, MINECRAFT_FLUID_WATER, filled_snapshot,
			UINT64_C(0x123456789ABCDEF0));
		if (error_code == FT_ERR_SUCCESS
			&& (filled_snapshot.revision != 4U
				|| filled_snapshot.items.size() != 2U
				|| filled_snapshot.items[0].stack_size != 1
				|| filled_snapshot.items[1].item_id
					!= MinecraftItemCatalog::WATER_BUCKET_ITEM_ID
				|| filled_snapshot.items[1].max_stack != 1
				|| filled_snapshot.items[1].instance_id
					!= UINT64_C(0x123456789ABCDEF0)
				|| filled_snapshot.current_weight != 2
				|| filled_snapshot.used_slots != 2U))
			error_code = FT_ERR_INVALID_STATE;
		stacked_empty.capacity = 1U;
		failed_output.capacity = 77U;
		failed_sentinel = failed_output;
		if (error_code == FT_ERR_SUCCESS
			&& (catalog.prepare_fluid_transfer_snapshot(stacked_empty, 0,
				3U, true, MINECRAFT_FLUID_LAVA, failed_output)
				!= FT_ERR_FULL
				|| failed_output.capacity != failed_sentinel.capacity
				|| !failed_output.items.empty()))
			error_code = FT_ERR_INVALID_STATE;
		identified_empty.capacity = 1U;
		identified_empty.used_slots = 1U;
		identified_empty.weight_limit = 10;
		identified_empty.current_weight = 1;
		identified_empty.next_slot = 1;
		identified_empty.revision = 8U;
		identified_entry.slot = 0;
		identified_entry.item_id = MinecraftItemCatalog::EMPTY_BUCKET_ITEM_ID;
		identified_entry.instance_id = UINT64_C(0xFEDCBA9876543210);
		identified_entry.stack_size = 1;
		identified_entry.max_stack = 1;
		identified_entry.width = 1;
		identified_entry.height = 1;
		identified_empty.items.push_back(identified_entry);
		if (error_code == FT_ERR_SUCCESS)
			error_code = catalog.prepare_fluid_transfer_snapshot(
				identified_empty, 0, 8U, true, MINECRAFT_FLUID_LAVA,
				identified_filled);
		if (error_code == FT_ERR_SUCCESS
			&& (identified_filled.items.size() != 1U
				|| identified_filled.items[0].item_id
					!= MinecraftItemCatalog::LAVA_BUCKET_ITEM_ID
				|| identified_filled.items[0].instance_id
					!= identified_entry.instance_id
				|| identified_filled.items[0].max_stack != 1))
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
			error_code = catalog.prepare_fluid_transfer_snapshot(
				identified_filled, 0, identified_filled.revision, false,
				MINECRAFT_FLUID_LAVA, identified_poured);
		if (error_code == FT_ERR_SUCCESS
			&& (identified_poured.items.size() != 1U
				|| identified_poured.items[0].item_id
					!= MinecraftItemCatalog::EMPTY_BUCKET_ITEM_ID
				|| identified_poured.items[0].instance_id
					!= identified_entry.instance_id
				|| identified_poured.items[0].max_stack != 1))
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_content_item_rule(
			MinecraftItemCatalog::MAGICAL_DUST_ITEM_ID, &content_rule);
	if (error_code == FT_ERR_SUCCESS
		&& content_rule.acquisition != MINECRAFT_ITEM_ACQUISITION_RECIPE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_content_item_rule(
			MinecraftItemCatalog::COAL_ITEM_ID, &content_rule);
	if (error_code == FT_ERR_SUCCESS
		&& content_rule.acquisition != MINECRAFT_ITEM_ACQUISITION_WORLD)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.validate_runtime_coverage(&runtime_rule_count);
	if (error_code == FT_ERR_SUCCESS && runtime_rule_count != 0U)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.validate_content_coverage(&content_rule_count);
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_content_item_rule(
			MinecraftItemCatalog::OAK_PLANKS_ITEM_ID, &content_rule);
	if (error_code == FT_ERR_SUCCESS
		&& (content_rule.max_stack != 64
			|| content_rule.acquisition != MINECRAFT_ITEM_ACQUISITION_RECIPE))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_content_item_rule(
			MinecraftItemCatalog::PLANT_FIBER_ITEM_ID, &content_rule);
	if (error_code == FT_ERR_SUCCESS
		&& content_rule.acquisition != MINECRAFT_ITEM_ACQUISITION_HARVEST)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_content_item_rule(
			MinecraftItemCatalog::RED_PIGMENT_ITEM_ID, &content_rule);
	if (error_code == FT_ERR_SUCCESS
		&& content_rule.acquisition != MINECRAFT_ITEM_ACQUISITION_HARVEST)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_content_item_rule(
			MinecraftItemCatalog::YELLOW_PIGMENT_ITEM_ID, &content_rule);
	if (error_code == FT_ERR_SUCCESS
		&& content_rule.acquisition != MINECRAFT_ITEM_ACQUISITION_HARVEST)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = catalog.resolve_content_item_rule(
			MinecraftItemCatalog::VOLATILE_STABILIZER_CARD_ITEM_ID,
			&content_rule);
	if (error_code == FT_ERR_SUCCESS
		&& content_rule.acquisition != MINECRAFT_ITEM_ACQUISITION_DISCOVERY)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = item_catalog_validate_recipes(catalog);
	/* The synthetic runtime registration intentionally adds a temporary
	 * runtime item rule and its definition. Run it after checks that require
	 * the production catalogue to remain unchanged. */
	if (error_code == FT_ERR_SUCCESS)
		error_code = item_catalog_validate_runtime_registration(catalog);
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"item-catalog: coverage validation failed error=%d builtin=%u runtime=%u content=%u\n",
			error_code, builtin_rule_count, runtime_rule_count,
			content_rule_count);
		(void)catalog.destroy();
		return (1);
	}
	(void)catalog.destroy();
	std::printf("item-catalog: ok builtin=%u runtime=%u content=%u\n",
		builtin_rule_count, runtime_rule_count, content_rule_count);
	return (FT_ERR_SUCCESS);
}
