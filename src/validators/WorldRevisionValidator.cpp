#include "../../src/validators/WorldRevisionValidator.hpp"
#include "../../src/app/GameSession.hpp"
#include "../../src/cards/RegenerationWorldRules.hpp"
#include "../../src/world/WorldGenerationResultCommitter.hpp"
#include "../../src/world/WorldRevisionServer.hpp"
#include "../../src/world/WorldRevisionMetadataStore.hpp"
#include "../../src/world/WorldRevisionChunkSet.hpp"
#include "../../src/world/WorldChunkGenerationWorker.hpp"
#include "../../src/world/WorldGenerationRequestBuilder.hpp"
#include "../../src/debug/RenderDebug.hpp"
#include "../../src/physics/PlayerCollision.hpp"
#include "../../src/spawn/PlayerSpawner.hpp"
#include "../../src/inventory/MinecraftLootCatalog.hpp"
#include "../../src/inventory/MinecraftItemCatalog.hpp"
#include "../../src/coordinates/WorldCoordinates.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <limits>
#include <new>
#include <string>
#include <thread>

static int32_t world_revision_validator_clock(uint64_t *epoch_ms_out,
	void *user_data) noexcept
{
	if (epoch_ms_out == nullptr || user_data == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*epoch_ms_out = *static_cast<const uint64_t *>(user_data);
	return (FT_ERR_SUCCESS);
}

static void world_revision_validator_append_u32(std::vector<uint8_t> &bytes,
	uint32_t value)
{
	for (uint32_t shift = 0U; shift < 32U; shift += 8U)
		bytes.push_back(static_cast<uint8_t>(value >> shift));
}

static void world_revision_validator_append_u64(std::vector<uint8_t> &bytes,
	uint64_t value)
{
	for (uint32_t shift = 0U; shift < 64U; shift += 8U)
		bytes.push_back(static_cast<uint8_t>(value >> shift));
}

static int32_t world_revision_validator_test_feature_preview_mask() noexcept
{
	RenderDebug source;
	RenderDebug copied;
	const std::size_t center_index =
		static_cast<std::size_t>(RenderDebug::REVISION_PREVIEW_RADIUS)
		* RenderDebug::REVISION_PREVIEW_WIDTH
		+ RenderDebug::REVISION_PREVIEW_RADIUS;
	source.revision_preview_visible = true;
	source.revision_barriers_active = true;
	source.revision_map_radius = static_cast<uint8_t>(
		RenderDebug::REVISION_PREVIEW_RADIUS);
	source.revision_feature_highlights[center_index] = 1U;
	copied = source;
	if (!copied.revision_preview_visible || !copied.revision_barriers_active
		|| copied.revision_map_radius != RenderDebug::REVISION_PREVIEW_RADIUS
		|| copied.revision_feature_highlights[center_index] != 1U)
		return (FT_ERR_INVALID_STATE);
	copied.clear();
	if (copied.revision_preview_visible || copied.revision_barriers_active
		|| copied.revision_map_radius != 0U)
		return (FT_ERR_INVALID_STATE);
	for (std::size_t index = 0U;
		index < RenderDebug::REVISION_PREVIEW_CELL_COUNT; ++index)
	{
		if (copied.revision_feature_highlights[index] != 0U)
			return (FT_ERR_INVALID_STATE);
	}
	return (FT_ERR_SUCCESS);
}

static int32_t world_revision_validator_test_movement_barrier(World &world)
{
	Camera camera;
	World::WorldRevision revision = world.world_revision();
	uint64_t selection_revision = revision.selection_revision;
	const int32_t chunk_width = GAME_VOXEL_CHUNK_WIDTH;
	const int32_t chunk_depth = GAME_VOXEL_CHUNK_DEPTH;
	const int32_t selected_chunk_x = 1;
	const double selected_x = static_cast<double>(
		selected_chunk_x * chunk_width);
	const double selected_z = 0.0;
	bool spawn_stayed_inside;
	int32_t result;

	result = world.begin_world_revision(world.voxel_generation_settings(),
		World::REGEN_FULL);
	if (result == FT_ERR_SUCCESS)
	{
		revision = world.world_revision();
		selection_revision = revision.selection_revision;
	}
	if (result == FT_ERR_SUCCESS)
		result = world.update_revision_chunk_selection(selection_revision,
			32U, selected_chunk_x, 0, true, &selection_revision);
	if (result == FT_ERR_SUCCESS)
		result = world.set_revision_barriers_active(true);
	if (result != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "movement barrier setup failed result=%d selection_revision=%llu\n",
			result, static_cast<unsigned long long>(selection_revision));
		return (result);
	}
	camera.x = selected_x + 1.0;
	camera.y = 1000.0;
	camera.z = selected_z + static_cast<double>(chunk_depth) * 0.5;
	PlayerSpawner::spawn_player_on_ground(&camera, world);
	spawn_stayed_inside = std::fabs(camera.x - (selected_x + 1.0)) < 0.001
		&& std::fabs(camera.z - (selected_z
			+ static_cast<double>(chunk_depth) * 0.5)) < 0.001;
	if ((spawn_stayed_inside
			&& (camera.y < 999.999 || camera.y > 1000.001))
		|| (!spawn_stayed_inside
			&& PlayerCollision::player_box_intersects_world(world, camera.x,
				camera.y, camera.z)))
	{
		std::fprintf(stderr, "movement barrier spawn check stayed=%d camera=(%.2f,%.2f,%.2f)\n",
			spawn_stayed_inside, camera.x, camera.y, camera.z);
		return (FT_ERR_INVALID_STATE);
	}
	camera.x = selected_x - 1.0;
	camera.y = 96.0;
	camera.z = selected_z + static_cast<double>(chunk_depth) * 0.5;
	if (!world.revision_barrier_blocks_chunk(selected_chunk_x, 0)
		|| world.revision_barrier_blocks_chunk(selected_chunk_x + 1, 0)
		|| !PlayerCollision::player_box_intersects_world(world,
			selected_x + 1.0, 1000.0,
			selected_z + static_cast<double>(chunk_depth) * 0.5)
		|| PlayerCollision::player_box_intersects_world(world,
			selected_x + static_cast<double>(chunk_width) + 2.0,
			1000.0, selected_z + static_cast<double>(chunk_depth) * 0.5)
		|| !PlayerCollision::move_player_axis(&camera, world, 0, 3.0)
		|| camera.x + PlayerGeometry::PLAYER_HALF_WIDTH > selected_x
		|| world.cancel_world_revision() != FT_ERR_INVALID_OPERATION)
	{
		std::fprintf(stderr, "movement barrier entry check barrier=%d outside=%d inside_collision=%d outside_collision=%d move=%d x=%.2f cancel=%d\n",
			world.revision_barrier_blocks_chunk(selected_chunk_x, 0),
			world.revision_barrier_blocks_chunk(selected_chunk_x + 1, 0),
			PlayerCollision::player_box_intersects_world(world,
				selected_x + 1.0, 1000.0,
				selected_z + static_cast<double>(chunk_depth) * 0.5),
			PlayerCollision::player_box_intersects_world(world,
				selected_x + static_cast<double>(chunk_width) + 2.0,
				1000.0, selected_z + static_cast<double>(chunk_depth) * 0.5),
			camera.x < selected_x, camera.x, -1);
		return (FT_ERR_INVALID_STATE);
	}
	/* A player already inside the reserved area can always leave it. */
	camera.x = selected_x + 1.0;
	const bool inside_move_blocked = PlayerCollision::move_player_axis(
		&camera, world, 0, 1.0);
	if (inside_move_blocked || camera.x <= selected_x + 1.0)
	{
		std::fprintf(stderr, "movement barrier exit check blocked=%d x=%.2f\n",
			inside_move_blocked, camera.x);
		return (FT_ERR_INVALID_STATE);
	}
	/* Large reverse movement must stop at the far edge of the selected chunk. */
	camera.x = selected_x + static_cast<double>(chunk_width) + 1.0;
	const bool reverse_move_blocked = PlayerCollision::move_player_axis(
		&camera, world, 0, -3.0);
	if (!reverse_move_blocked
		|| camera.x - PlayerGeometry::PLAYER_HALF_WIDTH
			< selected_x + static_cast<double>(chunk_width))
	{
		std::fprintf(stderr,
			"movement barrier reverse check blocked=%d x=%.2f\n",
			reverse_move_blocked, camera.x);
		return (FT_ERR_INVALID_STATE);
	}
	/* The same swept boundary rule applies while entering along the Z axis. */
	camera.x = selected_x + static_cast<double>(chunk_width) * 0.5;
	camera.z = -1.0;
	const bool z_move_blocked = PlayerCollision::move_player_axis(&camera,
		world, 2, 3.0);
	if (!z_move_blocked || camera.z + PlayerGeometry::PLAYER_HALF_WIDTH > 0.0)
	{
		std::fprintf(stderr,
			"movement barrier z check blocked=%d z=%.2f\n",
			z_move_blocked, camera.z);
		return (FT_ERR_INVALID_STATE);
	}
	if (world.set_revision_barriers_active(false) != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "movement barrier deactivate failed\n");
		return (FT_ERR_INVALID_STATE);
	}
	camera.x = selected_x - 1.0;
	const bool outside_move_blocked = PlayerCollision::move_player_axis(
		&camera, world, 0, 3.0);
	const int32_t cancel_result = world.cancel_world_revision();
	if (outside_move_blocked || camera.x < selected_x + 2.0
		|| cancel_result != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "movement barrier outside check blocked=%d x=%.2f cancel=%d\n",
			outside_move_blocked, camera.x, cancel_result);
		return (FT_ERR_INVALID_STATE);
	}
	return (FT_ERR_SUCCESS);
}

static int32_t world_revision_validator_test_barrier_restore() noexcept
{
	std::unique_ptr<World> source_storage(
		new (std::nothrow) World());
	std::unique_ptr<World> restored_storage(
		new (std::nothrow) World());
	if (!source_storage || !restored_storage)
		return (FT_ERR_NO_MEMORY);
	World &source = *source_storage;
	World &restored = *restored_storage;
	RegenerationWorldRules source_rules;
	RegenerationWorldRules restored_rules;
	RegenerationWorldRulesPolicy policy = {};
	const char *metadata_path = "world_revision_active_barrier.tmp";
	const char *failure_stage = "source.initialize";
	uint64_t selection_revision = 0U;
	int32_t error_code = source.initialize("revision-validator");
	const auto wait_for_chunk = [](World &world, int32_t x,
		int32_t z) -> int32_t
	{
		for (int32_t iteration = 0; iteration < 200; ++iteration)
		{
			if (world.find_chunk_mutable(x, z) != nullptr)
				return (FT_ERR_SUCCESS);
			const int32_t update_error = world.update_around(
				static_cast<double>(x * GAME_VOXEL_CHUNK_WIDTH),
				static_cast<double>(z * GAME_VOXEL_CHUNK_DEPTH), 4);
			if (update_error != FT_ERR_SUCCESS)
				return (update_error);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return (FT_ERR_NOT_FOUND);
	};
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	const int32_t center_x = GAME_VOXEL_CHUNK_WIDTH * 10;
	const int32_t center_z = GAME_VOXEL_CHUNK_DEPTH * 10;
	failure_stage = "source.update";
	error_code = source.update_around(static_cast<double>(center_x),
		static_cast<double>(center_z), 4);
	if (error_code == FT_ERR_SUCCESS)
	{
		failure_stage = "source.wait";
		error_code = wait_for_chunk(source, 10, 10);
	}
	policy.surface_magma_pools_per_card = 1U;
	policy.maximum_village_plans = 1U;
	policy.maximum_surface_magma_pools = 1U;
	policy.maximum_added_target_chunks = 1U;
	policy.maximum_ore_scale_numerator = 4U;
	policy.maximum_ore_scale_denominator = 1U;
	policy.maximum_biome_weight_adjustment = 5;
	policy.generation_manifest_hash =
		RegenerationWorldRules::calculate_manifest_hash(
			 source.voxel_generation_settings(), policy);
	if (error_code == FT_ERR_SUCCESS)
	{
		failure_stage = "source rules.initialize";
		error_code = source_rules.initialize(
			source.voxel_generation_settings(), policy);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		failure_stage = "source revision.begin";
		error_code = source.begin_world_revision(
			source.voxel_generation_settings(), source_rules,
			World::REGEN_FULL);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		failure_stage = "source selection";
		error_code = source.update_revision_chunk_selection(
			source.world_revision().selection_revision, 16U, 10, 10, true,
			&selection_revision);
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = source.set_revision_barriers_active(true);
	if (error_code == FT_ERR_SUCCESS)
		error_code = source.set_revision_decision_deadline_epoch(
			1234567890ULL);
	if (error_code == FT_ERR_SUCCESS
		&& source.set_revision_decision_deadline_epoch(1234567891ULL)
		!= FT_ERR_INVALID_STATE)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		failure_stage = "source metadata.save";
		error_code = source.save_revision_metadata(metadata_path);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		failure_stage = "restored.initialize";
		error_code = restored.initialize("revision-validator");
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		failure_stage = "restored.update";
		error_code = restored.update_around(static_cast<double>(center_x),
			static_cast<double>(center_z), 4);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		failure_stage = "restored.wait";
		error_code = wait_for_chunk(restored, 10, 10);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		failure_stage = "restored metadata.load";
		error_code = restored.load_revision_metadata(metadata_path);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		failure_stage = "restored rules.initialize";
		policy.generation_manifest_hash =
			RegenerationWorldRules::calculate_manifest_hash(
				restored.voxel_generation_settings(), policy);
		error_code = restored_rules.initialize(
			restored.voxel_generation_settings(), policy);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		failure_stage = "restored revision.restore";
		error_code = restored.restore_committed_revision(restored_rules);
	}
	if (error_code == FT_ERR_SUCCESS
		&& (!restored.revision_barriers_active()
			|| !restored.world_revision().pending
			|| restored.world_revision().selected_count != 1U
			|| restored.world_revision().selection_revision
				!= selection_revision
			|| restored.revision_decision_deadline_epoch()
				!= 1234567890ULL
			|| !restored.revision_barrier_blocks_chunk(10, 10)))
		error_code = FT_ERR_INVALID_STATE;
	if (restored.revision_barriers_active())
	{
		(void)restored.set_revision_barriers_active(false);
		(void)restored.cancel_world_revision();
	}
	if (source.revision_barriers_active())
	{
		(void)source.set_revision_barriers_active(false);
		(void)source.cancel_world_revision();
	}
	(void)restored_rules.destroy();
	(void)source_rules.destroy();
	restored.destroy();
	source.destroy();
	std::remove(metadata_path);
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr, "barrier-restore failure at %s: %d\n",
			failure_stage, error_code);
	return (error_code);
}

static int32_t world_revision_validator_test_volcano_anchor_selection()
	noexcept
{
	std::vector<WorldRevisionManager::RevisionChunk> selected;
	std::vector<WorldRevisionManager::RevisionChunk> reserved;
	int32_t anchor_x = 0;
	int32_t anchor_z = 0;
	try
	{
		selected.reserve(512U);
		for (int32_t z = 20; z < 36; ++z)
			for (int32_t x = 20; x < 36; ++x)
				selected.push_back({x, z, false});
		for (int32_t z = -20; z < -4; ++z)
			for (int32_t x = -20; x < -4; ++x)
				selected.push_back({x, z, false});
		reserved.push_back({-12, -12, false});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (WorldRevisionChunkSet::find_paid_square_anchor(selected, reserved,
			16U, anchor_x, anchor_z) != FT_ERR_SUCCESS
		|| anchor_x != 20 || anchor_z != 20)
		return (FT_ERR_INVALID_STATE);
	reserved.clear();
	if (WorldRevisionChunkSet::find_paid_square_anchor(selected, reserved,
			16U, anchor_x, anchor_z) != FT_ERR_SUCCESS
		|| anchor_x != -20 || anchor_z != -20)
		return (FT_ERR_INVALID_STATE);
	try
	{
		reserved.push_back({-12, -12, false});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	selected[0].bonus = true;
	return (WorldRevisionChunkSet::find_paid_square_anchor(selected, reserved,
		16U, anchor_x, anchor_z) == FT_ERR_FULL
		? FT_ERR_SUCCESS : FT_ERR_INVALID_STATE);
}

static int32_t world_revision_validator_test_feature_chunk_reservation()
	noexcept
{
	std::vector<WorldRevisionManager::RevisionChunk> selected;
	std::vector<WorldRevisionManager::RevisionChunk> reserved;
	std::size_t village_cursor = 0U;
	std::size_t magma_cursor = 0U;
	std::size_t selected_index = 0U;
	try
	{
		selected.push_back({4, -2, false});
		selected.push_back({5, -2, true});
		selected.push_back({6, -2, false});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (WorldRevisionChunkSet::reserve_next_unoccupied(selected, reserved,
			village_cursor, selected_index) != FT_ERR_SUCCESS
		|| selected_index != 0U)
		return (FT_ERR_INVALID_STATE);
	try
	{
		reserved.push_back(selected[selected_index]);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (WorldRevisionChunkSet::reserve_next_unoccupied(selected, reserved,
			magma_cursor, selected_index) != FT_ERR_SUCCESS
		|| selected_index != 1U)
		return (FT_ERR_INVALID_STATE);
	try
	{
		reserved.push_back(selected[selected_index]);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (WorldRevisionChunkSet::reserve_next_unoccupied(selected, reserved,
			magma_cursor, selected_index) != FT_ERR_SUCCESS
		|| selected_index != 2U)
		return (FT_ERR_INVALID_STATE);
	try
	{
		reserved.push_back(selected[selected_index]);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (WorldRevisionChunkSet::reserve_next_unoccupied(selected, reserved,
		magma_cursor, selected_index) == FT_ERR_FULL
		? FT_ERR_SUCCESS : FT_ERR_INVALID_STATE);
}

static int32_t world_revision_validator_test_revision_id_exhaustion(
	World &world) noexcept
{
	const voxel_generation_config &config = world.voxel_generation_settings();
	RegenerationWorldRules rules;
	RegenerationWorldRulesPolicy policy = {};
	const uint32_t saved_revision_id = world.revision_manager.revision_id_;
	const uint64_t saved_selection_revision =
		world.revision_manager.selection_revision();
	int32_t error_code;

	policy.surface_magma_pools_per_card = 1U;
	policy.maximum_village_plans = 1U;
	policy.maximum_surface_magma_pools = 1U;
	policy.maximum_added_target_chunks = 1U;
	policy.maximum_ore_scale_numerator = 4U;
	policy.maximum_ore_scale_denominator = 1U;
	policy.maximum_biome_weight_adjustment = 5;
	policy.generation_manifest_hash =
		RegenerationWorldRules::calculate_manifest_hash(config, policy);
	error_code = rules.initialize(config, policy);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	world.revision_manager.revision_id_ = UINT32_MAX;
	error_code = world.revision_manager.begin(config, World::REGEN_FULL,
		rules);
	if (error_code == FT_ERR_OUT_OF_RANGE)
		error_code = world.revision_manager.begin(config, World::REGEN_FULL);
	world.revision_manager.revision_id_ = saved_revision_id;
	if (error_code != FT_ERR_OUT_OF_RANGE
		|| world.revision_manager.pending()
		|| world.revision_manager.selection_revision()
			!= saved_selection_revision)
		return (FT_ERR_INVALID_STATE);
	return (FT_ERR_SUCCESS);
}

static int32_t world_revision_validator_test_card_preflight() noexcept
{
	voxel_generation_config config;
	RegenerationWorldRules rules;
	RegenerationWorldRules restored_rules;
	RegenerationWorldRulesPolicy policy = {};
	RegenerationWorldRulesPolicy changed_policy = {};
	RegenerationPolicyIntent intent = {};
	RegenerationCardChoice choice = {};
	std::vector<uint8_t> rules_bytes;
	std::vector<RegenerationWorldFeaturePlan> feature_plans;
	uint32_t villages_before = 0U;
	uint32_t villages_after = 0U;
	uint32_t magma_count = 0U;
	uint32_t target_count = 0U;
	uint32_t ore_config_index = 0U;
	uint32_t ore_numerator = 0U;
	uint32_t ore_denominator = 0U;
	bool replayed = false;
	int32_t result = voxel_default_generation_config(config);

	if (result != FT_ERR_SUCCESS)
		return (result);
	policy.surface_magma_pools_per_card = 1U;
	policy.maximum_village_plans = 1U;
	policy.maximum_surface_magma_pools = 1U;
	policy.maximum_added_target_chunks = 1U;
	policy.maximum_ore_scale_numerator = 4U;
	policy.maximum_ore_scale_denominator = 1U;
	policy.ore_overgrowth_instability_delta = 5;
	policy.ultimate_greed_instability_delta = 7;
	policy.biome_rupture_instability_delta = 9;
	policy.ruined_village_instability_delta = 11;
	policy.maximum_ruined_village_plans = 1U;
	policy.maximum_biome_weight_adjustment = 5;
	while (villages_before < config.biome_count)
	{
		policy.biome_compass_adjustments[villages_before] = 1;
		++villages_before;
	}
	policy.generation_manifest_hash =
		RegenerationWorldRules::calculate_manifest_hash(config, policy);
	changed_policy = policy;
	changed_policy.ore_overgrowth_instability_delta += 1;
	if (policy.generation_manifest_hash
		== RegenerationWorldRules::calculate_manifest_hash(config,
			changed_policy))
		return (FT_ERR_INVALID_STATE);
	changed_policy = policy;
	changed_policy.biome_rupture_instability_delta += 1;
	if (policy.generation_manifest_hash
		== RegenerationWorldRules::calculate_manifest_hash(config,
			changed_policy))
		return (FT_ERR_INVALID_STATE);
	changed_policy = policy;
	changed_policy.ultimate_greed_instability_delta += 1;
	if (policy.generation_manifest_hash
		== RegenerationWorldRules::calculate_manifest_hash(config,
			changed_policy))
		return (FT_ERR_INVALID_STATE);
	changed_policy = policy;
	changed_policy.empty_draw_instability_delta = 2;
	if (policy.generation_manifest_hash
		== RegenerationWorldRules::calculate_manifest_hash(config,
			changed_policy))
		return (FT_ERR_INVALID_STATE);
	changed_policy = policy;
	changed_policy.hand_overflow_instability_delta = 3;
	if (policy.generation_manifest_hash
		== RegenerationWorldRules::calculate_manifest_hash(config,
			changed_policy))
		return (FT_ERR_INVALID_STATE);
	changed_policy = policy;
	changed_policy.ruined_village_instability_delta += 1;
	if (policy.generation_manifest_hash
		== RegenerationWorldRules::calculate_manifest_hash(config,
			changed_policy))
		return (FT_ERR_INVALID_STATE);
	result = rules.initialize(config, policy);
	if (result != FT_ERR_SUCCESS)
		return (result);
	choice.kind = REGENERATION_CARD_CHOICE_NONE;
	result = rules.preflight_card_effect(3U, choice);
	if (result != FT_ERR_SUCCESS)
		return (result);
	intent.request_id = 1U;
	intent.host_intent_sequence = 1U;
	intent.content_version = RegenerationCardSession::CONTENT_VERSION;
	intent.card_definition_id = 3U;
	intent.card_instance_id = 1U;
	intent.effect_kind = REGENERATION_CARD_EFFECT_VILLAGE_CHARTER;
	result = rules.apply_intent(intent, &replayed);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = rules.get_plan_counts(&villages_before, &magma_count,
		&target_count);
	if (result != FT_ERR_SUCCESS || villages_before != 1U)
		return (FT_ERR_INVALID_STATE);
	result = rules.preflight_card_effect(3U, choice);
	if (result != FT_ERR_FULL)
		return (FT_ERR_INVALID_STATE);
	result = rules.get_plan_counts(&villages_after, &magma_count,
		&target_count);
	if (result != FT_ERR_SUCCESS || villages_after != villages_before)
		return (FT_ERR_INVALID_STATE);
	while (ore_config_index < config.ore_rule_count
		&& config.ores[ore_config_index].enabled != FT_TRUE)
		++ore_config_index;
	if (ore_config_index >= config.ore_rule_count)
		return (FT_ERR_INVALID_STATE);
	choice.kind = REGENERATION_CARD_CHOICE_ORE_ID;
	choice.value_primary = config.ores[ore_config_index].block_id;
	result = rules.get_ore_scale(choice.value_primary, &ore_numerator,
		&ore_denominator);
	if (result != FT_ERR_SUCCESS || ore_numerator != 1U
		|| ore_denominator != 1U)
		return (FT_ERR_INVALID_STATE);
	result = rules.preflight_card_effect(11U, choice);
	if (result != FT_ERR_SUCCESS)
		return (result);
	intent = {};
	intent.request_id = 2U;
	intent.host_intent_sequence = 2U;
	intent.content_version = RegenerationCardSession::CONTENT_VERSION;
	intent.card_definition_id = 11U;
	intent.card_instance_id = 2U;
	intent.effect_kind = REGENERATION_CARD_EFFECT_ORE_OVERGROWTH;
	intent.choice = choice;
	result = rules.apply_intent(intent, &replayed);
	if (result != FT_ERR_SUCCESS)
		return (result);
	intent.request_id = 3U;
	intent.host_intent_sequence = 3U;
	intent.card_definition_id = 2U;
	intent.card_instance_id = 3U;
	intent.effect_kind = REGENERATION_CARD_EFFECT_DEEP_SEAM;
	result = rules.apply_intent(intent, &replayed);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = rules.get_ore_scale(choice.value_primary, &ore_numerator,
		&ore_denominator);
	if (result != FT_ERR_SUCCESS || ore_numerator != 3U
		|| ore_denominator != 1U)
		return (FT_ERR_INVALID_STATE);
	result = rules.preflight_card_effect(11U, choice);
	if (result != FT_ERR_OUT_OF_RANGE)
		return (FT_ERR_INVALID_STATE);
	choice = {};
	choice.kind = REGENERATION_CARD_CHOICE_NONE;
	result = rules.preflight_card_effect(
		RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID, choice);
	if (result != FT_ERR_SUCCESS)
		return (result);
	intent = {};
	intent.request_id = 4U;
	intent.host_intent_sequence = 4U;
	intent.content_version = RegenerationCardSession::CONTENT_VERSION;
	intent.card_definition_id =
		RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID;
	intent.card_instance_id = 4U;
	intent.effect_kind = REGENERATION_CARD_EFFECT_ULTIMATE_GREED;
	intent.choice = choice;
	result = rules.apply_intent(intent, &replayed);
	if (result != FT_ERR_SUCCESS || replayed)
		return (FT_ERR_INVALID_STATE);
	result = rules.apply_intent(intent, &replayed);
	if (result != FT_ERR_SUCCESS || !replayed)
		return (FT_ERR_INVALID_STATE);
	choice = {};
	choice.kind = REGENERATION_CARD_CHOICE_BIOME_ID;
	choice.value_primary = 1U;
	choice.value_secondary = 2U;
	result = rules.preflight_card_effect(
		RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID, choice);
	if (result != FT_ERR_SUCCESS)
		return (result);
	intent = {};
	intent.request_id = 5U;
	intent.host_intent_sequence = 5U;
	intent.content_version = RegenerationCardSession::CONTENT_VERSION;
	intent.card_definition_id =
		RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID;
	intent.card_instance_id = 5U;
	intent.effect_kind = REGENERATION_CARD_EFFECT_BIOME_RUPTURE;
	intent.choice = choice;
	intent.no_effect_choice_mask = 1U;
	if (rules.apply_intent(intent, &replayed) != FT_ERR_INVALID_ARGUMENT)
		return (FT_ERR_INVALID_STATE);
	intent.no_effect_choice_mask = 0U;
	result = rules.apply_intent(intent, &replayed);
	if (result != FT_ERR_SUCCESS || replayed)
		return (FT_ERR_INVALID_STATE);
	bool biome_enabled = true;
	int32_t biome_weight = 0;
	if (rules.get_biome_state(1U, &biome_enabled, &biome_weight)
		!= FT_ERR_SUCCESS || biome_enabled
		|| rules.get_biome_state(2U, &biome_enabled, &biome_weight)
			!= FT_ERR_SUCCESS || biome_enabled)
		return (FT_ERR_INVALID_STATE);
	choice.value_primary = 1U;
	choice.value_secondary = 2U;
	result = rules.preflight_card_effect(
		RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID, choice);
	if (result != FT_ERR_SUCCESS)
		return (result);
	intent.request_id = 6U;
	intent.host_intent_sequence = 6U;
	intent.card_instance_id = 6U;
	intent.choice = choice;
	result = rules.apply_intent(intent, &replayed);
	if (result != FT_ERR_SUCCESS || replayed
		|| rules.get_biome_state(1U, &biome_enabled, &biome_weight)
			!= FT_ERR_SUCCESS || biome_enabled
		|| rules.get_biome_state(2U, &biome_enabled, &biome_weight)
			!= FT_ERR_SUCCESS || biome_enabled)
		return (FT_ERR_INVALID_STATE);
	choice.value_primary = 3U;
	choice.value_secondary = 4U;
	if (rules.get_biome_state(3U, &biome_enabled, &biome_weight)
		!= FT_ERR_SUCCESS || !biome_enabled
		|| rules.get_biome_state(4U, &biome_enabled, &biome_weight)
			!= FT_ERR_SUCCESS || !biome_enabled)
		return (FT_ERR_INVALID_STATE);
	result = rules.preflight_card_effect(
		RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID, choice);
	if (result != FT_ERR_SUCCESS
		|| rules.get_biome_state(3U, &biome_enabled, &biome_weight)
			!= FT_ERR_SUCCESS || !biome_enabled
		|| rules.get_biome_state(4U, &biome_enabled, &biome_weight)
			!= FT_ERR_SUCCESS || !biome_enabled)
		return (FT_ERR_INVALID_STATE);
	choice = {};
	choice.kind = REGENERATION_CARD_CHOICE_NONE;
	result = rules.preflight_card_effect(
		RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID,
		choice);
	if (result != FT_ERR_SUCCESS)
		return (result);
	intent = {};
	intent.request_id = 7U;
	intent.host_intent_sequence = 7U;
	intent.content_version = RegenerationCardSession::CONTENT_VERSION;
	intent.card_definition_id =
		RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID;
	intent.card_instance_id = 7U;
	intent.effect_kind = REGENERATION_CARD_EFFECT_RUINED_VILLAGE_RELIC;
	intent.choice = choice;
	result = rules.apply_intent(intent, &replayed);
	if (result != FT_ERR_SUCCESS || replayed)
		return (FT_ERR_INVALID_STATE);
	if (rules.preflight_card_effect(
			RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID,
			choice) != FT_ERR_FULL)
		return (FT_ERR_INVALID_STATE);
	result = rules.copy_feature_plans(feature_plans);
	if (result != FT_ERR_SUCCESS || feature_plans.size() != 2U
		|| feature_plans[1U].kind
			!= REGENERATION_WORLD_FEATURE_RUINED_VILLAGE
		|| feature_plans[1U].host_intent_sequence != 7U)
		return (FT_ERR_INVALID_STATE);
	result = rules.serialize(rules_bytes);
	if (result == FT_ERR_SUCCESS)
		result = restored_rules.initialize(config, policy);
	if (result == FT_ERR_SUCCESS)
		result = restored_rules.deserialize(rules_bytes.data(),
			rules_bytes.size(), config, policy);
	if (result != FT_ERR_SUCCESS
		|| restored_rules.copy_feature_plans(feature_plans) != FT_ERR_SUCCESS
		|| feature_plans.size() != 2U
		|| feature_plans[1U].kind
			!= REGENERATION_WORLD_FEATURE_RUINED_VILLAGE
		|| feature_plans[1U].host_intent_sequence != 7U)
		return (FT_ERR_INVALID_STATE);
	return (FT_ERR_SUCCESS);
}

static int32_t world_revision_validator_test_chunk_instability() noexcept
{
	WorldChunkInstabilityStore store;
	WorldChunkInstabilityStore restored;
	std::unique_ptr<World> world(new (std::nothrow) World());
	WorldChunkInstabilityPolicy policy = {};
	WorldChunkInstabilityRecord record = {};
	WorldChunkInstabilityRecord verified = {};
	WorldChunkInstabilityPublicView public_view = {};
	std::vector<uint8_t> bytes;
	std::vector<uint8_t> bytes_before_public_default;
	std::vector<uint8_t> bytes_after_public_default;
	std::vector<WorldChunkInstabilityRecord::VersionedId> generation_outcomes;
	int32_t result;

	policy.maximum_value = 100U;
	policy.reward_threshold_count = 2U;
	policy.reward_thresholds[0] = 20U;
	policy.reward_thresholds[1] = 60U;
	policy.danger_threshold_count = 2U;
	policy.danger_thresholds[0] = 30U;
	policy.danger_thresholds[1] = 80U;
	if (world == nullptr)
		return (FT_ERR_NO_MEMORY);
	result = world->configure_chunk_instability(policy);
	if (result == FT_ERR_SUCCESS)
		result = world->get_chunk_instability(-8, 11, &verified);
	if (result != FT_ERR_SUCCESS || verified.current_value != 0U
		|| verified.record_revision != 0U)
		return (FT_ERR_INVALID_STATE);
	result = world->adjust_chunk_instability(0U, -8, 11, 12, &verified);
	if (result != FT_ERR_SUCCESS || verified.current_value != 12U
		|| verified.record_revision != 1U)
		return (FT_ERR_INVALID_STATE);
	world->destroy();
	result = world->get_chunk_instability(-8, 11, &verified);
	if (result != FT_ERR_SUCCESS || verified.current_value != 0U
		|| verified.record_revision != 0U
		|| !world->is_chunk_instability_configured())
		return (FT_ERR_INVALID_STATE);
	result = store.configure(policy);
	if (result == FT_ERR_SUCCESS)
		result = store.get(3, -4, &record);
	if (result != FT_ERR_SUCCESS || record.initial_value != 0U
		|| record.current_value != 0U || record.record_revision != 0U)
		return (FT_ERR_INVALID_STATE);
	result = store.serialize(bytes_before_public_default);
	if (result == FT_ERR_SUCCESS)
		result = store.get_public_view(91, -92, &public_view);
	if (result == FT_ERR_SUCCESS)
		result = store.serialize(bytes_after_public_default);
	if (result != FT_ERR_SUCCESS || public_view.chunk_x != 91
		|| public_view.chunk_z != -92 || public_view.reward_band != 0U
		|| public_view.danger_band != 0U
		|| bytes_before_public_default != bytes_after_public_default)
		return (FT_ERR_INVALID_STATE);
	result = store.adjust(0U, 3, -4, 25, &record);
	if (result != FT_ERR_SUCCESS || record.current_value != 25U
		|| record.reward_band != 1U || record.danger_band != 0U
		|| record.record_revision != 1U)
		return (FT_ERR_INVALID_STATE);
	if (store.adjust(0U, 3, -4, 1, nullptr) != FT_ERR_INVALID_STATE
		|| store.adjust(1U, 3, -4, 80, nullptr) != FT_ERR_OUT_OF_RANGE)
		return (FT_ERR_INVALID_STATE);
	result = store.add_permanent_modifier(1U, 3, -4, 0xCA7A1U, 2U, &record);
	if (result == FT_ERR_SUCCESS)
		result = store.add_feature_outcome(2U, 3, -4, 0xF347U, 1U, &record);
	if (result == FT_ERR_SUCCESS)
		result = store.record_generation(3U, 3, -4, 1U, 0x91B2U, &record);
	if (result != FT_ERR_SUCCESS || record.record_revision != 4U
		|| record.generation_revision != 1U || record.rules_hash != 0x91B2U
		|| record.permanent_modifiers.size() != 1U
		|| record.feature_outcomes.size() != 1U)
		return (FT_ERR_INVALID_STATE);
	result = store.get_public_view(3, -4, &public_view);
	if (result != FT_ERR_SUCCESS || public_view.chunk_x != 3
		|| public_view.chunk_z != -4 || public_view.reward_band != 1U
		|| public_view.danger_band != 0U)
		return (FT_ERR_INVALID_STATE);
	result = store.ensure_record(5, 6);
	if (result == FT_ERR_SUCCESS)
		result = store.record_generation(0U, 5, 6, 9U, 0x8A17U, nullptr);
	if (result == FT_ERR_SUCCESS)
		result = store.get(5, 6, &verified);
	if (result != FT_ERR_SUCCESS || verified.record_revision != 1U
		|| verified.generation_revision != 9U
		|| verified.rules_hash != 0x8A17U)
		return (FT_ERR_INVALID_STATE);
	result = store.ensure_record(7, 8);
	try
	{
		generation_outcomes.push_back({0xF347U, 1U});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (result == FT_ERR_SUCCESS)
		result = store.record_generation(0U, 7, 8, 1U, 0x7788U,
			nullptr, &generation_outcomes);
	if (result == FT_ERR_SUCCESS)
		result = store.get(7, 8, &verified);
	if (result != FT_ERR_SUCCESS || verified.record_revision != 1U
		|| verified.generation_revision != 1U
		|| verified.rules_hash != 0x7788U
		|| verified.feature_outcomes.size() != 1U
		|| verified.feature_outcomes[0].id != 0xF347U
		|| verified.feature_outcomes[0].version != 1U)
		return (FT_ERR_INVALID_STATE);
	generation_outcomes[0] = {0U, 1U};
	if (store.record_generation(1U, 7, 8, 2U, 0x8899U, nullptr,
			&generation_outcomes) != FT_ERR_INVALID_ARGUMENT
		|| store.get(7, 8, &verified) != FT_ERR_SUCCESS
		|| verified.record_revision != 1U
		|| verified.generation_revision != 1U
		|| verified.rules_hash != 0x7788U
		|| verified.feature_outcomes.size() != 1U)
		return (FT_ERR_INVALID_STATE);
	result = store.serialize(bytes);
	if (result == FT_ERR_SUCCESS)
		result = restored.configure(policy);
	if (result == FT_ERR_SUCCESS)
		result = restored.deserialize(bytes.data(), bytes.size());
	if (result == FT_ERR_SUCCESS)
		result = restored.get(3, -4, &verified);
	if (result != FT_ERR_SUCCESS || verified.current_value != 25U
		|| verified.reward_band != 1U || verified.danger_band != 0U
		|| verified.record_revision != 4U
		|| verified.permanent_modifiers.size() != 1U
		|| verified.feature_outcomes.size() != 1U)
		return (FT_ERR_INVALID_STATE);
	bytes.back() ^= 0x01U;
	if (restored.deserialize(bytes.data(), bytes.size())
		!= FT_ERR_INVALID_ARGUMENT
		|| restored.get(3, -4, &verified) != FT_ERR_SUCCESS
		|| verified.current_value != 25U || verified.record_revision != 4U)
		return (FT_ERR_INVALID_STATE);
	result = store.adjust(4U, 3, -4, 60, &verified);
	if (result != FT_ERR_SUCCESS || verified.current_value != 85U
		|| verified.reward_band != 2U || verified.danger_band != 2U
		|| verified.record_revision != 5U)
		return (FT_ERR_INVALID_STATE);
	result = store.adjust(5U, 3, -4, -85, &verified);
	if (result != FT_ERR_SUCCESS || verified.current_value != 0U
		|| verified.reward_band != 2U || verified.danger_band != 2U
		|| verified.record_revision != 6U)
		return (FT_ERR_INVALID_STATE);
	result = store.get_public_view(3, -4, &public_view);
	if (result != FT_ERR_SUCCESS || public_view.reward_band != 2U
		|| public_view.danger_band != 2U)
		return (FT_ERR_INVALID_STATE);
	result = store.serialize(bytes);
	if (result == FT_ERR_SUCCESS)
		result = restored.deserialize(bytes.data(), bytes.size());
	if (result == FT_ERR_SUCCESS)
		result = restored.get(3, -4, &verified);
	if (result != FT_ERR_SUCCESS || verified.current_value != 0U
		|| verified.reward_band != 2U || verified.danger_band != 2U
		|| verified.record_revision != 6U)
		return (FT_ERR_INVALID_STATE);
	return (FT_ERR_SUCCESS);
}

struct WorldRevisionServerAuthorizationFixture
{
	int32_t authorized_handle;
	std::string quote_response;
	uint32_t quote_response_count;
	std::string instability_response;
	uint32_t instability_response_count;
	std::string action_response;
	uint32_t action_response_count;
	std::string first_action_response;
	std::string second_action_response;
	std::string cancel_response;
	uint32_t start_commit_count;
	uint64_t committed_actor_id;
	uint64_t committed_loom_id;
	uint64_t committed_request_id;
	uint64_t committed_selection_revision;
	uint64_t committed_policy_revision;
	uint32_t committed_opening_card_count;
	bool allow_start_commit;
};

static int32_t world_revision_validator_provide_occupancy(
	std::vector<World::RevisionChunkCoordinate> &occupied_chunks_out,
	void *user_data)
{
	if (user_data == nullptr)
		return (FT_ERR_INVALID_POINTER);
	occupied_chunks_out.clear();
	return (FT_ERR_SUCCESS);
}

static int32_t world_revision_validator_start_commit(uint64_t actor_id,
	uint64_t loom_id, uint64_t request_id,
	uint64_t expected_selection_revision,
	uint64_t expected_policy_revision, uint32_t opening_card_count,
	void *user_data)
{
	WorldRevisionServerAuthorizationFixture *fixture =
		static_cast<WorldRevisionServerAuthorizationFixture *>(user_data);

	if (fixture == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	fixture->start_commit_count += 1U;
	fixture->committed_actor_id = actor_id;
	fixture->committed_loom_id = loom_id;
	fixture->committed_request_id = request_id;
	fixture->committed_selection_revision = expected_selection_revision;
	fixture->committed_policy_revision = expected_policy_revision;
	fixture->committed_opening_card_count = opening_card_count;
	return (fixture->allow_start_commit ? FT_ERR_SUCCESS
		: FT_ERR_INVALID_OPERATION);
}

static int32_t world_revision_validator_capture_response(int32_t client_handle,
	const char *message, void *user_data)
{
	WorldRevisionServerAuthorizationFixture *fixture =
		static_cast<WorldRevisionServerAuthorizationFixture *>(user_data);

	if ((client_handle != 10 && client_handle != 11) || message == nullptr
		|| fixture == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		if (std::strstr(message, "world_chunk_instability_view") != nullptr)
		{
			fixture->instability_response = message;
			++fixture->instability_response_count;
			return (FT_ERR_SUCCESS);
		}
		if (std::strstr(message, "world_revision_quote") != nullptr)
			fixture->quote_response = message;
		else
		{
			fixture->action_response = message;
		if (std::strstr(message, "\"request_id\":\"8\"") != nullptr)
		{
			if (fixture->cancel_response.empty())
				fixture->cancel_response = message;
			else if (fixture->cancel_response != message)
				return (FT_ERR_INVALID_STATE);
		}
			if (fixture->action_response_count == 0U)
				fixture->first_action_response = message;
			else if (fixture->action_response_count == 1U)
				fixture->second_action_response = message;
		}
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (std::strstr(message, "world_revision_quote") != nullptr)
		fixture->quote_response_count += 1U;
	else
		fixture->action_response_count += 1U;
	return (FT_ERR_SUCCESS);
}

static int32_t world_revision_validator_test_occupied_exclusion(World &world)
{
	std::vector<World::RevisionChunkCoordinate> occupied_chunks;
	uint64_t selection_revision;
	uint64_t repeated_revision;
	uint64_t pre_occupancy_selection_revision;
	const uint64_t policy_revision =
		world.world_revision().cost_policy_revision;
	WorldRevisionCostQuote quote = {};
	World::RevisionProtectionInfo protection_info = {};
	int32_t result;
	int32_t protection_result;
	int32_t occupied_quote_result;
	int32_t selection_result;

	result = world.begin_world_revision(world.voxel_generation_settings(),
		World::REGEN_FULL);
	if (result != FT_ERR_SUCCESS)
		return (result);
	selection_revision = world.world_revision().selection_revision;
	result = world.update_revision_chunk_selection(selection_revision, 64U,
		1, 0, true, &selection_revision);
	if (result != FT_ERR_SUCCESS)
	{
		(void)world.cancel_world_revision();
		return (result);
	}
	if (world.query_revision_start_cost(selection_revision, policy_revision,
			4U, &quote) != FT_ERR_SUCCESS)
	{
		(void)world.cancel_world_revision();
		return (FT_ERR_INVALID_STATE);
	}
	try
	{
		occupied_chunks.push_back({0, 0});
	}
	catch (...)
	{
		(void)world.cancel_world_revision();
		return (FT_ERR_NO_MEMORY);
	}
	pre_occupancy_selection_revision = selection_revision;
	result = world.set_revision_occupied_chunks(occupied_chunks,
		&selection_revision);
	protection_result = world.get_revision_protection_info(0, 0,
		&protection_info);
	occupied_quote_result = world.query_revision_start_cost(
		pre_occupancy_selection_revision,
		policy_revision, 4U, &quote);
	selection_result = world.update_revision_chunk_selection(selection_revision,
		64U, 1, 0, true, &repeated_revision);
	if (result != FT_ERR_SUCCESS
		|| world.world_revision().selected_count != 0U
		|| protection_result != FT_ERR_SUCCESS
		|| !protection_info.occupied_player_exclusion
		|| occupied_quote_result != FT_ERR_INVALID_STATE
		|| selection_result != FT_ERR_INVALID_OPERATION)
	{
		(void)world.cancel_world_revision();
		return (FT_ERR_INVALID_STATE);
	}
	result = world.set_revision_occupied_chunks(occupied_chunks,
		&repeated_revision);
	if (result != FT_ERR_SUCCESS || repeated_revision != selection_revision)
	{
		(void)world.cancel_world_revision();
		return (FT_ERR_INVALID_STATE);
	}
	if (world.cancel_world_revision() != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	occupied_chunks.clear();
	return (world.set_revision_occupied_chunks(occupied_chunks,
		&repeated_revision));
}

static int32_t world_revision_validator_authorize_client(int32_t client_handle,
	const char *message, uint64_t *actor_id_out, uint64_t *loom_id_out,
	void *user_data)
{
	WorldRevisionServerAuthorizationFixture *fixture =
		static_cast<WorldRevisionServerAuthorizationFixture *>(user_data);

	if (fixture == nullptr || message == nullptr || actor_id_out == nullptr
		|| loom_id_out == nullptr)
		return (FT_ERR_PERMISSION_DENIED);
	if (client_handle != fixture->authorized_handle
		&& client_handle != fixture->authorized_handle + 1)
		return (FT_ERR_PERMISSION_DENIED);
	*actor_id_out = client_handle == fixture->authorized_handle ? 42U : 43U;
	*loom_id_out = 99U;
	return (FT_ERR_SUCCESS);
}

static int32_t world_revision_validator_authorize_instability_view(
	uint64_t actor_id, uint64_t loom_id, int32_t chunk_x, int32_t chunk_z,
	void *user_data)
{
	if (user_data == nullptr || actor_id != 42U || loom_id != 99U
		|| chunk_z != 11)
		return (FT_ERR_PERMISSION_DENIED);
	return (chunk_x == 999 ? FT_ERR_PERMISSION_DENIED : FT_ERR_SUCCESS);
}

static int32_t world_revision_validator_authorize_station_access(
	uint64_t actor_id, uint64_t loom_id, int32_t station_chunk_x,
	int32_t station_chunk_z, bool has_station_block, int32_t station_block_x,
	int32_t station_block_y, int32_t station_block_z,
	uint32_t station_block_id, const char *request_message, void *user_data)
{
	if (user_data == nullptr || request_message == nullptr || actor_id != 42U
		|| loom_id != 99U || station_chunk_x != 100
		|| station_chunk_z != 100 || has_station_block
		|| station_block_x != 0 || station_block_y != 0
		|| station_block_z != 0 || station_block_id != 0U)
		return (FT_ERR_PERMISSION_DENIED);
	return (FT_ERR_SUCCESS);
}

static bool world_revision_validator_corrupt_metadata(const char *path,
	World &world, uint64_t expected_cost_policy_revision) noexcept
{
	FILE *file = std::fopen(path, "r+b");
	long file_size;
	int byte_value;
	bool write_ok;
	int32_t close_result;
	World::WorldRevision before_load;

	if (file == nullptr)
		return (false);
	before_load = world.world_revision();
	write_ok = std::fseek(file, 0, SEEK_END) == 0
		&& (file_size = std::ftell(file)) > 0L
		&& std::fseek(file, file_size - 1L, SEEK_SET) == 0
		&& (byte_value = std::fgetc(file)) != EOF
		&& std::fseek(file, file_size - 1L, SEEK_SET) == 0
		&& std::fputc(byte_value ^ 0x01, file) != EOF;
	close_result = std::fclose(file);
	if (!write_ok || close_result != 0)
		return (false);
	return (world.load_revision_metadata(path) == FT_ERR_INVALID_ARGUMENT
		&& world.world_revision().identifier == before_load.identifier
		&& world.world_revision().cost_policy_revision
			== expected_cost_policy_revision
		&& world.is_chunk_protected(4, 0));
}

static bool world_revision_validator_write_legacy_metadata(
	const char *path) noexcept
{
	FILE *file = std::fopen(path, "wb");
	const uint32_t magic = 0x57525631U;
	const uint32_t version = 1U;
	const uint32_t revision = 9U;
	const uint32_t protected_count = 1U;
	const int32_t protected_x = 4;
	const int32_t protected_z = 0;
	bool write_ok;
	int32_t close_result;

	if (file == nullptr)
		return (false);
	write_ok = std::fwrite(&magic, sizeof(magic), 1U, file) == 1U
		&& std::fwrite(&version, sizeof(version), 1U, file) == 1U
		&& std::fwrite(&revision, sizeof(revision), 1U, file) == 1U
		&& std::fwrite(&protected_count, sizeof(protected_count), 1U, file) == 1U
		&& std::fwrite(&protected_x, sizeof(protected_x), 1U, file) == 1U
		&& std::fwrite(&protected_z, sizeof(protected_z), 1U, file) == 1U;
	close_result = std::fclose(file);
	return (write_ok && close_result == 0);
}

static uint64_t world_revision_validator_metadata_hash(
	const uint8_t *bytes, std::size_t size) noexcept
{
	uint64_t hash = 14695981039346656037ULL;
	for (std::size_t index = 0U; index < size; ++index)
	{
		hash ^= bytes[index];
		hash *= 1099511628211ULL;
	}
	return (hash);
}

static int32_t world_revision_validator_test_metadata_v2_v3() noexcept
{
	std::vector<uint8_t> version_four;
	std::vector<uint8_t> legacy;
	std::vector<WorldRevisionManager::RevisionChunk> protected_chunks;
	WorldRevisionCostPolicy policy = {};
	uint32_t revision_id = 0U;
	uint64_t policy_revision = 0U;
	bool policy_configured = false;
	bool policy_stored = false;
	bool station_configured = false;
	int32_t station_x = 0;
	int32_t station_z = 0;
	bool station_stored = false;
	bool station_block_configured = false;
	int32_t station_block_x = 0;
	int32_t station_block_y = 0;
	int32_t station_block_z = 0;
	uint32_t station_block_id = 0U;
	int32_t result = WorldRevisionMetadataStore::serialize(17U,
		std::vector<WorldRevisionManager::RevisionChunk>(),
		WorldRevisionCostPolicy{}, 0U, false, true, -1, 2, false,
		0, 0, 0, 0U, version_four);

	if (result != FT_ERR_SUCCESS || version_four.size() < 96U)
		return (result == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE : result);
	try
	{
		for (uint32_t legacy_version = 2U; legacy_version <= 3U
			&& result == FT_ERR_SUCCESS; ++legacy_version)
		{
			legacy = version_four;
			if (legacy_version == 2U)
				legacy.erase(legacy.begin() + 56, legacy.begin() + 88);
			else
				legacy.erase(legacy.begin() + 68, legacy.begin() + 88);
			legacy[4] = static_cast<uint8_t>(legacy_version);
			legacy[5] = 0U;
			legacy[6] = 0U;
			legacy[7] = 0U;
			const uint64_t checksum = world_revision_validator_metadata_hash(
				legacy.data(), legacy.size() - 8U);
			for (uint32_t shift = 0U; shift < 64U; shift += 8U)
				legacy[legacy.size() - 8U + shift / 8U]
					= static_cast<uint8_t>(checksum >> shift);
			result = WorldRevisionMetadataStore::deserialize(legacy, revision_id,
				protected_chunks, policy, policy_revision, policy_configured,
				policy_stored, station_configured, station_x, station_z,
				station_stored, station_block_configured, station_block_x,
				station_block_y, station_block_z, station_block_id);
			if (result == FT_ERR_SUCCESS
				&& (revision_id != 17U || policy_configured || !policy_stored
					|| policy_revision != 0U
				|| station_stored != (legacy_version == 3U)
				|| station_configured != (legacy_version == 3U)
				|| station_x != (legacy_version == 3U ? -1 : 0)
				|| station_z != (legacy_version == 3U ? 2 : 0)
				|| station_block_configured || station_block_id != 0U
				|| !protected_chunks.empty()))
				result = FT_ERR_INVALID_STATE;
	}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (result);
}

WorldRevisionValidator::WorldRevisionValidator()
{
}

WorldRevisionValidator::WorldRevisionValidator(const WorldRevisionValidator &other)
	: IValidator(other)
{
	(void)other;
}

WorldRevisionValidator::~WorldRevisionValidator()
{
}

WorldRevisionValidator &WorldRevisionValidator::operator=(const WorldRevisionValidator &other)
{
	(void)other;
	return (*this);
}

int32_t WorldRevisionValidator::initialize_world_with_edit(World &world) noexcept
{
	int32_t error_code;

	error_code = world.initialize("revision-validator");
	if (error_code != FT_ERR_SUCCESS)
		return (1);
	if (world.find_chunk_mutable(0, 0) == nullptr)
	{
		world.destroy();
		return (1);
	}
	if (WorldRevisionValidator::wait_for_loaded_chunk(world, 1, 0)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (WorldRevisionValidator::quiesce_stream_pipeline(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (world.find_chunk_mutable(0, 0)->chunk.write_block(0, 0, 0,
			VOXEL_GENERATOR_STONE_BLOCK) != FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionValidator::quiesce_stream_pipeline(World &world) noexcept
{
	std::unique_ptr<WorldGenerationPipeline::Result> discarded_result;
	int32_t iteration;

	world.chunk_streamer.cancel_pending_remeshes();
	iteration = 0;
	while (iteration < 500)
	{
		while (world.chunk_streamer.pipeline().poll(discarded_result)
			== FT_ERR_SUCCESS)
			discarded_result.reset();
		if (world.chunk_streamer.pipeline().queued_count() == 0U
			&& world.chunk_streamer.pipeline().completed_count() == 0U
			&& world.chunk_streamer.pipeline().active_count() == 0U)
			return (FT_ERR_SUCCESS);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		iteration += 1;
	}
	return (world.chunk_streamer.pipeline().queued_count() == 0U
		&& world.chunk_streamer.pipeline().completed_count() == 0U
		&& world.chunk_streamer.pipeline().active_count() == 0U
		? FT_ERR_SUCCESS : FT_ERR_TIMEOUT);
}

int32_t WorldRevisionValidator::wait_for_loaded_chunk(World &world,
	int32_t chunk_x, int32_t chunk_z) noexcept
{
	int32_t iteration;
	int32_t error_code;

	iteration = 0;
	while (iteration < 5000
		&& world.find_chunk_mutable(chunk_x, chunk_z) == nullptr)
	{
		error_code = world.update_around(0.0, 0.0, 4);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		iteration += 1;
	}
	return (world.find_chunk_mutable(chunk_x, chunk_z) != nullptr
		? FT_ERR_SUCCESS : FT_ERR_NOT_FOUND);
}

WorldRevisionValidator::SelectionResults WorldRevisionValidator::apply_selection_actions(World &world,
	std::vector<World::RevisionPreviewEntry> &preview) noexcept
{
	voxel_generation_config config;
	RegenerationWorldRules rules;
	RegenerationWorldRulesPolicy policy = {};
	WorldRevisionCostPolicy cost_policy = {};
	RegenerationPolicyIntent intent = {};
	SelectionResults results = {};
	World::WorldRevision revision;
	uint64_t current_selection_revision = 0U;
	uint64_t current_cost_policy_revision = 0U;
	uint64_t initial_cost_policy_revision = 0U;
	uint32_t ore_index = 0U;
	int32_t error_code;

	voxel_default_generation_config(config);
	while (ore_index < config.ore_rule_count
		&& config.ores[ore_index].enabled != FT_TRUE)
		++ore_index;
	if (ore_index >= config.ore_rule_count)
	{
		results.begin_result = FT_ERR_CONFIGURATION;
		return (results);
	}
	policy.surface_magma_pools_per_card = 1U;
	policy.maximum_village_plans = 1U;
	policy.maximum_surface_magma_pools = 1U;
	policy.maximum_added_target_chunks = 1U;
	policy.maximum_ore_scale_numerator = 4U;
	policy.maximum_ore_scale_denominator = 1U;
	policy.maximum_biome_weight_adjustment = 5;
	policy.biome_compass_adjustments[0] = 5;
	policy.biome_compass_adjustments[1] = 3;
	policy.generation_manifest_hash =
		RegenerationWorldRules::calculate_manifest_hash(config, policy);
	error_code = rules.initialize(config, policy);
	if (error_code != FT_ERR_SUCCESS)
	{
		results.begin_result = error_code;
		return (results);
	}
	intent.request_id = 1U;
	intent.host_intent_sequence = 1U;
	intent.content_version = RegenerationCardSession::CONTENT_VERSION;
	intent.card_definition_id = 1U;
	intent.card_instance_id = 1U;
	intent.effect_kind = REGENERATION_CARD_EFFECT_BIOME_EXCLUSION;
	intent.choice.kind = REGENERATION_CARD_CHOICE_BIOME_ID;
	intent.choice.value_primary = 1U;
	error_code = rules.apply_intent(intent, &intent.replayed);
	if (error_code != FT_ERR_SUCCESS)
	{
		results.begin_result = error_code;
		return (results);
	}
	intent.request_id = 2U;
	intent.host_intent_sequence = 2U;
	intent.card_definition_id = 2U;
	intent.effect_kind = REGENERATION_CARD_EFFECT_DEEP_SEAM;
	intent.choice.kind = REGENERATION_CARD_CHOICE_ORE_ID;
	intent.choice.value_primary = config.ores[ore_index].block_id;
	error_code = rules.apply_intent(intent, &intent.replayed);
	if (error_code != FT_ERR_SUCCESS)
	{
		results.begin_result = error_code;
		return (results);
	}
	intent.request_id = 3U;
	intent.host_intent_sequence = 3U;
	intent.card_definition_id = 6U;
	intent.card_instance_id = 3U;
	intent.effect_kind = REGENERATION_CARD_EFFECT_BIOME_COMPASS;
	intent.choice = {};
	error_code = rules.apply_intent(intent, &intent.replayed);
	if (error_code != FT_ERR_SUCCESS)
	{
		results.begin_result = error_code;
		return (results);
	}
	results.begin_result = world.begin_world_revision(config, rules,
			World::REGEN_FULL);
	if (results.begin_result == FT_ERR_SUCCESS)
	{
		intent.request_id = 4U;
		intent.host_intent_sequence = 4U;
		intent.card_definition_id = 2U;
		intent.card_instance_id = 4U;
		intent.effect_kind = REGENERATION_CARD_EFFECT_DEEP_SEAM;
		intent.choice.kind = REGENERATION_CARD_CHOICE_ORE_ID;
		intent.choice.value_primary = config.ores[ore_index].block_id;
		results.begin_result = rules.apply_intent(intent, &intent.replayed);
		if (results.begin_result == FT_ERR_SUCCESS)
			results.begin_result = world.update_pending_revision_rules(rules);
		if (results.begin_result == FT_ERR_SUCCESS
			&& world.revision_manager.config_.ores[ore_index]
				.veins_per_chunk_max <= config.ores[ore_index].veins_per_chunk_max)
			results.begin_result = FT_ERR_INVALID_STATE;
	}
	results.edit_select_result = world.select_revision_chunk(0, 0, true);
	results.protect_result = world.set_chunk_protected(4, 0, true);
	results.select_loaded_result = world.select_revision_chunk(1, 0, true);
	current_selection_revision = world.world_revision().selection_revision;
	results.bonus_unselect_result = world.update_revision_chunk_selection(
		current_selection_revision, 1U, 1, 0, false,
		&current_selection_revision);
	if (results.bonus_unselect_result == FT_ERR_SUCCESS)
	{
		intent.request_id = 5U;
		intent.host_intent_sequence = 5U;
		intent.card_definition_id = 7U;
		intent.card_instance_id = 5U;
		intent.effect_kind = REGENERATION_CARD_EFFECT_SURVEYORS_REACH;
		intent.choice.kind = REGENERATION_CARD_CHOICE_TARGET_CHUNK;
		intent.choice.value_primary = 1U;
		intent.choice.value_secondary = 0U;
		results.bonus_select_result = world.validate_revision_bonus_chunk(
			current_selection_revision, 64U, 1, 0);
		if (results.bonus_select_result == FT_ERR_SUCCESS)
			results.bonus_select_result = rules.apply_intent(intent,
				&intent.replayed);
		if (results.bonus_select_result == FT_ERR_SUCCESS)
			results.bonus_select_result = world.update_pending_revision_rules(
				rules);
		if (results.bonus_select_result == FT_ERR_SUCCESS)
		{
			current_selection_revision = world.world_revision().selection_revision;
			if (world.validate_revision_bonus_chunk(current_selection_revision,
					64U, 1, 0) == FT_ERR_ALREADY_EXISTS)
				results.bonus_select_result = FT_ERR_SUCCESS;
			else
				results.bonus_select_result = FT_ERR_INVALID_STATE;
		}
	}
	results.select_unloaded_result = world.select_revision_chunk(1000, 1000,
			true);
	results.select_far_result = world.select_revision_chunk(1000, 1000, true);
	results.transition_state = world.revision_state(2, 0);
	results.protected_state = world.revision_state(4, 0);
	results.preview_result = world.build_revision_preview(2, 0, 4, preview);
	if (results.preview_result == FT_ERR_SUCCESS)
	{
		std::vector<World::RevisionPreviewEntry> boundary_preview;
		const int32_t maximum = std::numeric_limits<int32_t>::max();
		const int32_t minimum = std::numeric_limits<int32_t>::min();
		bool maximum_center_found = false;
		bool minimum_center_found = false;
		results.preview_result = world.build_revision_preview(maximum, maximum,
			8, boundary_preview);
		if (results.preview_result == FT_ERR_SUCCESS)
		{
			for (const World::RevisionPreviewEntry &entry : boundary_preview)
			{
				if (entry.chunk_x < maximum - 8 || entry.chunk_x > maximum
					|| entry.chunk_z < maximum - 8 || entry.chunk_z > maximum)
					results.preview_result = FT_ERR_INVALID_STATE;
				if (entry.chunk_x == maximum && entry.chunk_z == maximum)
					maximum_center_found = true;
			}
		}
		if (results.preview_result == FT_ERR_SUCCESS
			&& !maximum_center_found)
			results.preview_result = FT_ERR_INVALID_STATE;
		if (results.preview_result == FT_ERR_SUCCESS)
			results.preview_result = world.build_revision_preview(minimum,
				minimum, 8, boundary_preview);
		if (results.preview_result == FT_ERR_SUCCESS)
		{
			for (const World::RevisionPreviewEntry &entry : boundary_preview)
			{
				if (entry.chunk_x < minimum || entry.chunk_x > minimum + 8
					|| entry.chunk_z < minimum || entry.chunk_z > minimum + 8)
					results.preview_result = FT_ERR_INVALID_STATE;
				if (entry.chunk_x == minimum && entry.chunk_z == minimum)
					minimum_center_found = true;
			}
		}
		if (results.preview_result == FT_ERR_SUCCESS
			&& !minimum_center_found)
			results.preview_result = FT_ERR_INVALID_STATE;
	}
	cost_policy.maximum_selected_chunks = 1U;
	cost_policy.currency_item_id = 77U;
	cost_policy.currency_units_per_chunk = 10U;
	cost_policy.four_card_opening_cost = 5U;
	cost_policy.maximum_total_currency_cost = 1000U;
	initial_cost_policy_revision = world.world_revision().cost_policy_revision;
	results.cost_policy_result = world.configure_revision_cost_policy(
		initial_cost_policy_revision,
		cost_policy, &current_cost_policy_revision);
	results.stale_cost_policy_result = world.configure_revision_cost_policy(
		initial_cost_policy_revision,
		cost_policy, &current_cost_policy_revision);
	results.cost_policy_update_result = world.configure_revision_cost_policy(
		initial_cost_policy_revision + 1U,
		cost_policy, &current_cost_policy_revision);
	revision = world.world_revision();
	results.cost_policy_revision = current_cost_policy_revision;
	results.cost_quote_result = world.query_revision_start_cost(
		revision.selection_revision, revision.cost_policy_revision, 6U,
		&results.cost_quote);
	results.stale_cost_quote_result = world.query_revision_start_cost(
		revision.selection_revision, initial_cost_policy_revision + 1U, 6U,
		&results.cost_quote);
	return (results);
}

int32_t WorldRevisionValidator::check_selection_results(const SelectionResults &results,
	const std::vector<World::RevisionPreviewEntry> &preview) noexcept
{
	bool manual_protection_visible = false;
	for (const World::RevisionPreviewEntry &entry : preview)
	{
		if (entry.chunk_x == 4 && entry.chunk_z == 0
			&& entry.protection_info.manually_protected)
			manual_protection_visible = true;
	}
	if (results.begin_result != FT_ERR_SUCCESS
		|| results.edit_select_result == FT_ERR_SUCCESS
		|| results.protect_result != FT_ERR_SUCCESS
		|| results.select_loaded_result != FT_ERR_SUCCESS
		|| results.bonus_unselect_result != FT_ERR_SUCCESS
		|| results.bonus_select_result != FT_ERR_SUCCESS
		|| results.select_unloaded_result != FT_ERR_NOT_FOUND
		|| results.select_far_result != FT_ERR_NOT_FOUND
		|| results.preview_result != FT_ERR_SUCCESS || preview.empty()
		|| results.cost_policy_result != FT_ERR_SUCCESS
		|| results.stale_cost_policy_result != FT_ERR_INVALID_STATE
		|| results.cost_policy_update_result != FT_ERR_SUCCESS
		|| results.cost_quote_result != FT_ERR_SUCCESS
		|| results.stale_cost_quote_result != FT_ERR_INVALID_STATE
		|| results.cost_quote.selected_paid_chunks != 0U
		|| results.cost_quote.selected_bonus_chunks != 1U
		|| results.cost_quote.currency_item_id != 77U
		|| results.cost_quote.chunk_currency_cost != 0U
		|| results.cost_quote.opening_card_currency_cost != 20U
		|| results.cost_quote.total_currency_cost != 20U
		|| results.cost_quote.cost_policy_revision
			!= results.cost_policy_revision
		|| !manual_protection_visible
		|| results.transition_state != World::REVISION_TRANSITION
		|| results.protected_state != World::REVISION_PROTECTED)
	{
		std::fprintf(stderr,
						"world-revision: setup failed begin=%d edit=%d protect=%d loaded=%d"
						" unloaded=%d preview=%d costs=%d/%d/%d/%d/%d"
						" transition=%d protected=%d policy=%llu quote_policy=%llu"
						" bonus=%d far=%d preview_count=%zu protected_visible=%u"
						" quote=%u/%u/%llu/%llu/%llu/%llu\n",
						results.begin_result,
						results.edit_select_result,
						results.protect_result,
						results.select_loaded_result,
						results.select_unloaded_result,
						results.preview_result,
						results.cost_policy_result,
						results.stale_cost_policy_result,
						results.cost_policy_update_result,
						results.cost_quote_result,
						results.stale_cost_quote_result,
						results.transition_state,
						results.protected_state,
						static_cast<unsigned long long>(
							results.cost_policy_revision),
						static_cast<unsigned long long>(
							results.cost_quote.cost_policy_revision),
						results.bonus_select_result,
						results.select_far_result, preview.size(),
						manual_protection_visible ? 1U : 0U,
						results.cost_quote.selected_paid_chunks,
						results.cost_quote.selected_bonus_chunks,
						static_cast<unsigned long long>(
							results.cost_quote.chunk_currency_cost),
						static_cast<unsigned long long>(
							results.cost_quote.currency_item_id),
						static_cast<unsigned long long>(
							results.cost_quote.opening_card_currency_cost),
						static_cast<unsigned long long>(
							results.cost_quote.total_currency_cost));
		return (1);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionValidator::setup_revision_selection(World &world,
	std::vector<World::RevisionPreviewEntry> &preview) noexcept
{
	SelectionResults results;

	results = WorldRevisionValidator::apply_selection_actions(world, preview);
	return (WorldRevisionValidator::check_selection_results(results, preview));
}

int32_t WorldRevisionValidator::regenerate_and_check(World &world,
	int32_t *regenerated, int32_t *skipped) noexcept
{
	RegenerationWorldRules rules;
	int32_t regenerate_result;
	int32_t drain_result;
	std::chrono::steady_clock::time_point drain_deadline;
	World::WorldRevision revision;

	revision = world.world_revision();
	std::fprintf(stderr,
				"world-revision: before-regeneration pending=%d selected=%zu"
				" state_1_0=%d state_4_0=%d chunk_1_0=%s\n",
				revision.pending ? 1 : 0,
				revision.selected_count,
					static_cast<int>(world.revision_state(1, 0)),
					static_cast<int>(world.revision_state(4, 0)),
					world.find_chunk_mutable(1, 0) == nullptr ? "missing" : "present");
	regenerate_result = world.start_revision_regeneration();
	if (regenerate_result != FT_ERR_SUCCESS
		|| !world.world_revision().regenerating
		|| world.cancel_world_revision() != FT_ERR_INVALID_OPERATION)
	{
		std::fprintf(stderr,
			"world-revision: active regeneration was not protected from cancellation\n");
		return (1);
	}
	drain_result = world.update_pending_revision_rules(rules);
	if (drain_result != FT_ERR_INVALID_OPERATION)
	{
		std::fprintf(stderr,
			"world-revision: pending-rule update during regeneration returned=%d\n",
			drain_result);
		return (FT_ERR_INVALID_STATE);
	}
	drain_result = FT_ERR_SUCCESS;
	drain_deadline = std::chrono::steady_clock::now()
		+ std::chrono::seconds(120);
	while (world.revision_manager.progress_.active()
		&& std::chrono::steady_clock::now() < drain_deadline)
	{
		drain_result = WorldGenerationResultCommitter::drain(
			world.chunk_streamer, world);
		if (drain_result != FT_ERR_SUCCESS)
		{
			std::fprintf(stderr,
				"world-revision: generation drain failed result=%d active=%d queued=%zu completed=%zu\n",
				drain_result,
				world.revision_manager.progress_.active() ? 1 : 0,
				world.chunk_streamer.pipeline().queued_count(),
				world.chunk_streamer.pipeline().completed_count());
			return (drain_result);
		}
		if (world.revision_manager.progress_.active())
			std::this_thread::yield();
	}
	*regenerated = world.revision_manager.progress_.regenerated_count();
	*skipped = world.revision_manager.progress_.skipped_count();
	regenerate_result = world.revision_manager.progress_.error();
	if (regenerate_result != FT_ERR_SUCCESS || *regenerated < 1
		|| world.world_revision().pending
		|| world.revision_manager.progress_.active())
	{
		std::fprintf(stderr,
						"world-revision: regeneration failed result=%d regenerated=%d"
						" skipped=%d pending=%d\n",
						regenerate_result,
						*regenerated,
						*skipped,
						world.world_revision().pending ? 1 : 0);
		return (1);
	}
	{
		WorldChunk *regenerated_chunk = world.find_chunk_mutable(1, 0);
		if (regenerated_chunk == nullptr
			|| regenerated_chunk->light_buffer_is_valid() == false
			|| regenerated_chunk->light_is_current() == false
			|| regenerated_chunk->light_ready_for_render == false)
		{
			std::fprintf(stderr,
				"world-revision: regenerated chunk lost valid light "
				"chunk=%s valid=%d current=%d ready=%d\n",
				regenerated_chunk == nullptr ? "missing" : "present",
				regenerated_chunk != nullptr
					&& regenerated_chunk->light_buffer_is_valid() ? 1 : 0,
				regenerated_chunk != nullptr
					&& regenerated_chunk->light_is_current() ? 1 : 0,
				regenerated_chunk != nullptr
					&& regenerated_chunk->light_ready_for_render ? 1 : 0);
			return (1);
		}
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionValidator::roundtrip_metadata(World &world) noexcept
{
	const char *metadata_path;
	const char *legacy_metadata_path;
	voxel_generation_config config;
	World::WorldRevision restored_revision;
	World::RevisionProtectionInfo protection_info = {};
	WorldRevisionCostQuote quote = {};
	uint64_t station_selection_revision;
	std::unique_ptr<World> restored_world(new (std::nothrow) World());

	metadata_path = "world_revision_validator.bin";
	station_selection_revision = world.world_revision().selection_revision;
	if (station_selection_revision == 0U
		|| world.configure_loom_station(station_selection_revision - 1U,
			10, 10, &station_selection_revision) != FT_ERR_INVALID_STATE
		|| world.is_chunk_protected(10, 10)
		|| world.configure_loom_station(station_selection_revision, 10, 10,
			&station_selection_revision) != FT_ERR_SUCCESS
		|| !world.is_chunk_protected(10, 10)
		|| !world.is_chunk_protected(9, 9)
		|| !world.is_chunk_protected(11, 11)
		|| world.is_chunk_protected(12, 12)
		|| world.get_revision_protection_info(10, 10, &protection_info)
			!= FT_ERR_SUCCESS
		|| !protection_info.loom_safe_zone
		|| world.get_revision_protection_info(4, 0, &protection_info)
			!= FT_ERR_SUCCESS
		|| !protection_info.manually_protected)
		return (1);
	if (restored_world == nullptr
		|| world.save_revision_metadata(metadata_path) != FT_ERR_SUCCESS)
		return (1);
	if (restored_world->initialize("revision-validator") != FT_ERR_SUCCESS
		|| restored_world->load_revision_metadata(metadata_path) != FT_ERR_SUCCESS
		|| !restored_world->is_chunk_protected(4, 0)
		|| !restored_world->is_chunk_protected(10, 10)
		|| !restored_world->is_chunk_protected(9, 9)
		|| restored_world->is_chunk_protected(12, 12)
		|| restored_world->get_revision_protection_info(10, 10,
			&protection_info) != FT_ERR_SUCCESS
		|| !protection_info.loom_safe_zone)
	{
		std::remove(metadata_path);
		restored_world->destroy();
		return (1);
	}
	restored_revision = restored_world->world_revision();
	if (restored_revision.cost_policy_revision
		!= world.world_revision().cost_policy_revision
		|| restored_revision.cost_policy_revision == 0U)
	{
		std::remove(metadata_path);
		restored_world->destroy();
		return (1);
	}
	if (!world_revision_validator_corrupt_metadata(metadata_path,
			*restored_world, restored_revision.cost_policy_revision)
		|| !restored_world->is_chunk_protected(10, 10)
		|| !restored_world->is_chunk_protected(9, 9))
	{
		std::remove(metadata_path);
		restored_world->destroy();
		return (1);
	}
	legacy_metadata_path = "world_revision_validator_v1.bin";
	if (!world_revision_validator_write_legacy_metadata(legacy_metadata_path)
		|| restored_world->load_revision_metadata(legacy_metadata_path)
			!= FT_ERR_SUCCESS
		|| restored_world->world_revision().cost_policy_revision
			!= restored_revision.cost_policy_revision
		|| restored_world->world_revision().identifier != 9U
		|| !restored_world->is_chunk_protected(4, 0)
		|| !restored_world->is_chunk_protected(10, 10))
	{
		std::remove(metadata_path);
		std::remove(legacy_metadata_path);
		restored_world->destroy();
		return (1);
	}
	if (voxel_default_generation_config(config) != FT_ERR_SUCCESS
		|| restored_world->begin_world_revision(config,
			World::REGEN_FULL) != FT_ERR_SUCCESS
		|| restored_world->configure_loom_station(
			restored_world->world_revision().selection_revision, 20, 20,
			&station_selection_revision) != FT_ERR_INVALID_OPERATION
		|| restored_world->select_revision_chunk(0, 0, true)
			!= FT_ERR_SUCCESS)
	{
		std::remove(metadata_path);
		std::remove(legacy_metadata_path);
		restored_world->destroy();
		return (1);
	}
	restored_revision = restored_world->world_revision();
	if (restored_world->query_revision_start_cost(
			restored_revision.selection_revision,
			restored_revision.cost_policy_revision, 6U, &quote)
			!= FT_ERR_SUCCESS
		|| quote.currency_item_id != 77U
		|| quote.chunk_currency_cost != 10U
		|| quote.opening_card_currency_cost != 20U
		|| quote.total_currency_cost != 30U)
	{
		std::remove(metadata_path);
		std::remove(legacy_metadata_path);
		restored_world->destroy();
		return (1);
	}
	std::remove(metadata_path);
	std::remove(legacy_metadata_path);
	restored_world->destroy();
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionValidator::apply_request_test(World &world) noexcept
{
	World::RevisionRequest request;
	World::RevisionRequestResult request_result = {};

	if (request.config.initialize(world.voxel_generation_settings())
		!= FT_ERR_SUCCESS)
		return (1);
	request.mode = World::REGEN_DECORATION_REFRESH;
	request.stage_mask = VOXEL_STAGE_DECORATION;
	request.selected_chunks.push_back({1, 0});
	if (world.apply_revision_request(request, &request_result) != FT_ERR_SUCCESS
		|| request_result.regenerated_count < 1
		|| request_result.stage_mask != VOXEL_STAGE_DECORATION)
		return (1);
	return (FT_ERR_SUCCESS);
}

bool WorldRevisionValidator::fail_if_error(World &world,
	int32_t error_code) noexcept
{
	if (error_code == FT_ERR_SUCCESS)
		return (false);
	std::fprintf(stderr, "world-revision: validation helper failed code=%d\n",
		error_code);
	world.destroy();
	return (true);
}

int32_t WorldRevisionValidator::test_server_draft_authorization(
	World &world) noexcept
{
	WorldRevisionServer server(world);
	WorldRevisionServer unconfigured_server(world);
	WorldRevisionServerAuthorizationFixture fixture;
	const char *begin_message =
		"{\"world_revision\":{\"action\":\"begin\",\"mode\":\"3\",\"request_id\":\"1\"}}";
	const char *altered_begin_message =
		"{\"world_revision\":{\"action\":\"begin\",\"mode\":\"2\",\"request_id\":\"1\"}}";
	const char *select_message =
		"{\"world_revision\":{\"action\":\"select\",\"request_id\":\"2\"}}";
	const char *cancel_message =
		"{\"world_revision\":{\"action\":\"cancel\",\"request_id\":\"8\"}}";
	char quote_message[256];
	char altered_quote_message[257];
	char confirm_message[320];
	char altered_confirm_message[320];
	char unconfigured_confirm_message[320];
	const char *instability_message =
		"{\"world_revision\":{\"action\":\"instability_view\",\"request_id\":\"77\",\"chunk_x\":\"-8\",\"chunk_z\":\"11\"}}";
	const char *altered_instability_message =
		"{\"world_revision\":{\"action\":\"instability_view\",\"request_id\":\"77\",\"chunk_x\":\"-7\",\"chunk_z\":\"11\"}}";
	const char *hidden_instability_message =
		"{\"world_revision\":{\"action\":\"instability_view\",\"request_id\":\"78\",\"chunk_x\":\"999\",\"chunk_z\":\"11\"}}";
	const char *overflow_instability_message =
		"{\"world_revision\":{\"action\":\"instability_view\",\"request_id\":\"79\",\"chunk_x\":\"2147483648\",\"chunk_z\":\"11\"}}";
	int32_t error_code;
	World::WorldRevision revision;
	WorldRevisionCostQuote quote = {};
	WorldRevisionCostQuote replayed_quote = {};
	WorldRevisionCostPolicy cost_policy = {};
	WorldRevisionCostPolicy bonus_cost_policy = {};
	std::vector<World::RevisionChunkCoordinate> paid_chunks;
	uint64_t selection_revision;
	uint64_t policy_revision;
	int32_t quote_result;
	int32_t bonus_config_result;
	int32_t bonus_select_result;
	int32_t paid_copy_result;
	int32_t bonus_quote_result;
	const char *failure_stage = "server setup";
	json_group *begin_groups;
	json_group *begin_revision;

	if (server.initialize() != FT_ERR_PERMISSION_DENIED)
		return (FT_ERR_INVALID_STATE);
	begin_groups = json_read_from_string(begin_message);
	begin_revision = begin_groups == nullptr ? nullptr
		: json_find_group(begin_groups, "world_revision");
	if (begin_revision == nullptr
		|| server.handle_begin_action(begin_revision) != FT_ERR_CONFIGURATION)
	{
		if (begin_groups != nullptr)
			json_free_groups(begin_groups);
		return (FT_ERR_INVALID_STATE);
	}
	json_free_groups(begin_groups);
	revision = world.world_revision();
	if (world.has_loom_station()
		|| world.configure_loom_station(revision.selection_revision, 100, 100,
			&selection_revision) != FT_ERR_SUCCESS
		|| !world.has_loom_station())
		return (FT_ERR_INVALID_STATE);
	fixture.authorized_handle = 10;
	fixture.quote_response_count = 0U;
	fixture.instability_response_count = 0U;
	fixture.action_response_count = 0U;
	fixture.start_commit_count = 0U;
	fixture.allow_start_commit = false;
	error_code = server.set_authorizer(
		&world_revision_validator_authorize_client, &fixture);
	if (error_code == FT_ERR_SUCCESS)
		error_code = server.set_instability_view_authorizer(
			&world_revision_validator_authorize_instability_view, &fixture);
	if (error_code == FT_ERR_SUCCESS)
		error_code = server.set_station_access_authorizer(
			&world_revision_validator_authorize_station_access, &fixture);
	if (error_code == FT_ERR_SUCCESS)
		error_code = server.set_occupancy_provider(
			&world_revision_validator_provide_occupancy, &fixture);
	if (error_code == FT_ERR_SUCCESS)
		error_code = server.set_start_committer(
			&world_revision_validator_start_commit, &fixture);
	if (error_code == FT_ERR_SUCCESS)
		error_code = server.initialize();
	if (error_code != FT_ERR_SUCCESS)
	{
		server.destroy();
		return (error_code);
	}
	server.response_sender_ =
		&world_revision_validator_capture_response;
	server.response_sender_user_data_ = &fixture;
	{
		WorldChunkInstabilityPolicy instability_policy = {};
		WorldChunkInstabilityRecord instability_record = {};
		failure_stage = "instability view";

		instability_policy.maximum_value = 100U;
		instability_policy.reward_threshold_count = 1U;
		instability_policy.reward_thresholds[0] = 5U;
		instability_policy.danger_threshold_count = 1U;
		instability_policy.danger_thresholds[0] = 8U;
		if (world.configure_chunk_instability(instability_policy)
				!= FT_ERR_SUCCESS
			|| world.adjust_chunk_instability(0U, -8, 11, 6,
				&instability_record) != FT_ERR_SUCCESS
			|| WorldRevisionServer::handle_message(10, instability_message,
				&server) != FT_ERR_SUCCESS
			|| fixture.instability_response_count != 1U
			|| fixture.instability_response.find("\"chunk_x\":-8")
				== std::string::npos
			|| fixture.instability_response.find("\"reward_band\":1")
				== std::string::npos
			|| fixture.instability_response.find("\"danger_band\":0")
				== std::string::npos
			|| fixture.instability_response.find("current_value")
				!= std::string::npos
			|| fixture.instability_response.find("modifier")
				!= std::string::npos
			|| fixture.instability_response.find("feature_outcome")
				!= std::string::npos
			|| fixture.instability_response.find("generation_revision")
				!= std::string::npos
			|| fixture.instability_response.find("record_revision")
				!= std::string::npos
			|| fixture.instability_response.find("initial_value")
				!= std::string::npos
			|| fixture.instability_response.find("rules_hash")
				!= std::string::npos
			|| WorldRevisionServer::handle_message(10, instability_message,
				&server) != FT_ERR_SUCCESS
			|| fixture.instability_response_count != 2U
			|| WorldRevisionServer::handle_message(10,
				altered_instability_message, &server) != FT_ERR_INVALID_ARGUMENT
			|| fixture.instability_response_count != 2U
			|| WorldRevisionServer::handle_message(10,
				hidden_instability_message, &server)
				!= FT_ERR_PERMISSION_DENIED
			|| fixture.instability_response_count != 2U
			|| WorldRevisionServer::handle_message(10,
				overflow_instability_message, &server)
				!= FT_ERR_INVALID_ARGUMENT
			|| fixture.instability_response_count != 2U)
		{
			server.destroy();
			return (FT_ERR_INVALID_STATE);
		}
	}
	failure_stage = "begin action";
	if (WorldRevisionServer::handle_message(10, begin_message, &server)
		!= FT_ERR_SUCCESS
		|| WorldRevisionServer::handle_message(10, begin_message, &server)
			!= FT_ERR_SUCCESS
		|| WorldRevisionServer::handle_message(10, altered_begin_message, &server)
			!= FT_ERR_INVALID_ARGUMENT
		|| WorldRevisionServer::handle_message(11, select_message, &server)
			!= FT_ERR_PERMISSION_DENIED
		|| fixture.action_response_count != 4U
		|| fixture.action_response.find("world_revision_result")
			== std::string::npos
		|| fixture.action_response.find("\"request_id\":\"2\"")
			== std::string::npos
		|| fixture.action_response.find("\"selection_revision\":\"")
			== std::string::npos
		|| fixture.first_action_response != fixture.second_action_response)
	{
		error_code = FT_ERR_INVALID_STATE;
	}
	else
	{
		WorldRevisionServer restored_server(world);
		WorldRevisionServer corrupt_state_server(world);
		std::vector<uint8_t> draft_state;
		std::vector<uint8_t> request_cache_state;
		if (server.serialize_draft_state(draft_state) != FT_ERR_SUCCESS
			|| restored_server.restore_draft_state(draft_state.data(),
				draft_state.size()) != FT_ERR_SUCCESS
			|| server.serialize_request_cache(request_cache_state)
				!= FT_ERR_SUCCESS
			|| restored_server.restore_request_cache(
				request_cache_state.data(), request_cache_state.size())
				!= FT_ERR_SUCCESS
			|| !restored_server.draft_owned_
			|| restored_server.draft_actor_id_ != 42U
			|| restored_server.draft_loom_id_ != 99U
			|| restored_server.draft_start_committed_
			|| restored_server.request_cache_.size()
				!= server.request_cache_.size()
			|| restored_server.wire_request_cache_.size()
				!= server.wire_request_cache_.size()
			|| restored_server.request_cache_.empty()
			|| restored_server.request_cache_.begin()->first
				!= server.request_cache_.begin()->first
			|| restored_server.request_cache_.begin()->second.payload
				!= server.request_cache_.begin()->second.payload
			|| restored_server.request_cache_.begin()->second.result
				!= server.request_cache_.begin()->second.result
			|| restored_server.request_cache_.begin()->second.completed
				!= server.request_cache_.begin()->second.completed
			|| restored_server.request_cache_.begin()->second.has_instability_view
				!= server.request_cache_.begin()->second.has_instability_view
			|| restored_server.request_cache_.begin()->second.instability_view.chunk_x
				!= server.request_cache_.begin()->second.instability_view.chunk_x
			|| restored_server.request_cache_.begin()->second.instability_view.reward_band
				!= server.request_cache_.begin()->second.instability_view.reward_band
			|| draft_state.size() != 48U)
			error_code = FT_ERR_INVALID_STATE;
		else
		{
			draft_state.back() ^= 1U;
			if (corrupt_state_server.restore_draft_state(draft_state.data(),
					draft_state.size()) != FT_ERR_INVALID_ARGUMENT
				|| corrupt_state_server.draft_owned_)
				error_code = FT_ERR_INVALID_STATE;
			request_cache_state.back() ^= 1U;
			if (corrupt_state_server.restore_request_cache(
					request_cache_state.data(), request_cache_state.size())
					!= FT_ERR_INVALID_ARGUMENT
				|| !corrupt_state_server.request_cache_.empty())
				error_code = FT_ERR_INVALID_STATE;
		}
		/* Both restored servers borrow the source World. Their destruction
		 * must not interpret fixture ownership as a real disconnect. */
		restored_server.draft_owned_ = false;
		if (error_code == FT_ERR_SUCCESS)
		{
			WorldRevisionServer restored_checkpoint_server(world);
			WorldRevisionServer rejected_checkpoint_server(world);
			WorldRevisionServer file_checkpoint_server(world);
			std::vector<uint8_t> checkpoint;
			const char *checkpoint_path =
				"world_revision_server_checkpoint.validator.tmp";
			int32_t file_result = server.save_checkpoint_file(checkpoint_path);
			if (file_result == FT_ERR_SUCCESS)
				file_result = file_checkpoint_server.load_checkpoint_file(
					checkpoint_path);
			(void)std::remove(checkpoint_path);
			if (server.serialize_checkpoint(checkpoint) != FT_ERR_SUCCESS
				|| file_result != FT_ERR_SUCCESS
				|| !file_checkpoint_server.draft_owned_
				|| file_checkpoint_server.draft_actor_id_ != 42U
				|| file_checkpoint_server.draft_loom_id_ != 99U
				|| file_checkpoint_server.request_cache_.size()
					!= server.request_cache_.size()
				|| file_checkpoint_server.wire_request_cache_.size()
					!= server.wire_request_cache_.size()
				|| restored_checkpoint_server.restore_checkpoint(
					checkpoint.data(), checkpoint.size()) != FT_ERR_SUCCESS
				|| !restored_checkpoint_server.draft_owned_
				|| restored_checkpoint_server.draft_actor_id_ != 42U
				|| restored_checkpoint_server.draft_loom_id_ != 99U
				|| restored_checkpoint_server.request_cache_.size()
					!= server.request_cache_.size()
				|| restored_checkpoint_server.wire_request_cache_.size()
					!= server.wire_request_cache_.size())
				error_code = FT_ERR_INVALID_STATE;
			else
			{
				checkpoint.back() ^= 1U;
				if (rejected_checkpoint_server.restore_checkpoint(
						checkpoint.data(), checkpoint.size())
						!= FT_ERR_INVALID_ARGUMENT
					|| rejected_checkpoint_server.draft_owned_
					|| !rejected_checkpoint_server.request_cache_.empty()
					|| !rejected_checkpoint_server.wire_request_cache_.empty())
					error_code = FT_ERR_INVALID_STATE;
			}
			/* This restore fixture shares the source World. */
			restored_checkpoint_server.draft_owned_ = false;
			file_checkpoint_server.draft_owned_ = false;
		}
		revision = world.world_revision();
		selection_revision = revision.selection_revision;
		policy_revision = revision.cost_policy_revision;
		cost_policy.maximum_selected_chunks = 1U;
		cost_policy.currency_item_id = 77U;
		cost_policy.currency_units_per_chunk = 10U;
		cost_policy.four_card_opening_cost = 5U;
		cost_policy.maximum_total_currency_cost = 1000U;
		if (world.configure_revision_cost_policy(policy_revision,
				cost_policy, &policy_revision) != FT_ERR_SUCCESS
			|| world.update_revision_chunk_selection(selection_revision, 1U,
				1, 0, true, &selection_revision) != FT_ERR_SUCCESS)
			error_code = FT_ERR_INVALID_STATE;
		else if (server.query_start_cost(43U, 99U, 3U,
				revision.selection_revision, revision.cost_policy_revision,
				6U, &quote) != FT_ERR_PERMISSION_DENIED)
			error_code = FT_ERR_INVALID_STATE;
		else
		{
			failure_stage = "quote action";
			std::snprintf(quote_message, sizeof(quote_message),
				"{\"world_revision\":{\"action\":\"quote\",\"request_id\":\"5\",\"selection_revision\":\"%llu\",\"policy_revision\":\"%llu\",\"opening_card_count\":\"6\"}}",
				static_cast<unsigned long long>(selection_revision),
				static_cast<unsigned long long>(policy_revision));
			std::snprintf(altered_quote_message,
				sizeof(altered_quote_message), "%s ", quote_message);
			quote_result = server.query_start_cost(42U, 99U, 3U,
				selection_revision, policy_revision,
				6U, &quote);
			if (quote_result != FT_ERR_SUCCESS
				|| quote.selected_paid_chunks != 1U
				|| quote.currency_item_id != 77U
				|| quote.chunk_currency_cost != 10U
				|| quote.opening_card_currency_cost != 20U
				|| quote.total_currency_cost != 30U
				|| server.query_start_cost(42U, 99U, 3U,
					selection_revision, policy_revision, 6U,
					&replayed_quote) != FT_ERR_SUCCESS
				|| replayed_quote.total_currency_cost != quote.total_currency_cost
				|| server.query_start_cost(42U, 99U, 3U,
					selection_revision, policy_revision, 5U,
					&replayed_quote) != FT_ERR_INVALID_ARGUMENT
				|| server.query_start_cost(42U, 99U, 4U,
					selection_revision - 1U, policy_revision, 6U,
					&replayed_quote) != FT_ERR_INVALID_STATE
				|| server.query_start_cost(42U, 99U, 4U,
					selection_revision - 1U, policy_revision, 6U,
					&replayed_quote) != FT_ERR_INVALID_STATE
				|| server.query_start_cost(42U, 99U, 4U,
					selection_revision, policy_revision, 6U,
					&replayed_quote) != FT_ERR_INVALID_ARGUMENT
				|| WorldRevisionServer::handle_message(10, quote_message,
					&server) != FT_ERR_SUCCESS
				|| fixture.quote_response_count != 1U
				|| fixture.quote_response.find("\"total_currency_cost\":\"30\"")
					== std::string::npos
				|| fixture.quote_response.find("\"selected_bonus_chunks\":0")
					== std::string::npos
				|| WorldRevisionServer::handle_message(10, quote_message,
					&server) != FT_ERR_SUCCESS
				|| fixture.quote_response_count != 2U
				|| fixture.quote_response.find("\"selection_revision\":\"")
					== std::string::npos
				|| WorldRevisionServer::handle_message(10,
					altered_quote_message, &server) != FT_ERR_INVALID_ARGUMENT
				|| fixture.quote_response_count != 2U)
				error_code = FT_ERR_INVALID_STATE;
			else
			{
					failure_stage = "bonus quote";
				bonus_cost_policy = cost_policy;
				bonus_cost_policy.maximum_selected_chunks = 2U;
					bonus_config_result = world.configure_revision_cost_policy(
						policy_revision, bonus_cost_policy, &policy_revision);
					bonus_select_result = world.update_revision_chunk_selection(
						selection_revision, 2U, 1, 0, false,
						&selection_revision);
					if (bonus_select_result == FT_ERR_SUCCESS)
						bonus_select_result = world.select_revision_bonus_chunk(
							selection_revision, 2U, 1, 0, &selection_revision);
					paid_copy_result = world.copy_selected_paid_revision_chunks(
						paid_chunks);
					bonus_quote_result = server.query_start_cost(42U, 99U, 6U,
						selection_revision, policy_revision, 6U, &replayed_quote);
					if (bonus_config_result != FT_ERR_SUCCESS
						|| bonus_select_result != FT_ERR_SUCCESS
						|| paid_copy_result != FT_ERR_SUCCESS
						|| paid_chunks.size() != 0U
						|| bonus_quote_result != FT_ERR_SUCCESS
						|| replayed_quote.selected_paid_chunks != 0U
						|| replayed_quote.selected_bonus_chunks != 1U
						|| replayed_quote.chunk_currency_cost != 0U
						|| replayed_quote.total_currency_cost != 20U)
						error_code = FT_ERR_INVALID_STATE;
				else
				{
					std::snprintf(quote_message, sizeof(quote_message),
						"{\"world_revision\":{\"action\":\"quote\",\"request_id\":\"7\",\"selection_revision\":\"%llu\",\"policy_revision\":\"%llu\",\"opening_card_count\":\"6\"}}",
						static_cast<unsigned long long>(selection_revision),
						static_cast<unsigned long long>(policy_revision));
					std::snprintf(confirm_message, sizeof(confirm_message),
						"{\"world_revision\":{\"action\":\"confirm\",\"request_id\":\"9\",\"selection_revision\":\"%llu\",\"policy_revision\":\"%llu\",\"opening_card_count\":\"6\"}}",
						static_cast<unsigned long long>(selection_revision),
						static_cast<unsigned long long>(policy_revision));
				std::snprintf(altered_confirm_message,
					sizeof(altered_confirm_message),
					"{\"world_revision\":{\"action\":\"confirm\",\"request_id\":\"9\",\"selection_revision\":\"%llu\",\"policy_revision\":\"%llu\",\"opening_card_count\":\"7\"}}",
					static_cast<unsigned long long>(selection_revision),
					static_cast<unsigned long long>(policy_revision));
					failure_stage = "confirm actions";
					if (WorldRevisionServer::handle_message(10, quote_message,
							&server) != FT_ERR_SUCCESS
						|| fixture.quote_response_count != 3U
						|| fixture.quote_response.find(
							"\"selected_paid_chunks\":0") == std::string::npos
						|| fixture.quote_response.find(
							"\"selected_bonus_chunks\":1")
							== std::string::npos
						|| WorldRevisionServer::handle_message(10,
							confirm_message, &server) != FT_ERR_INVALID_OPERATION
						|| WorldRevisionServer::handle_message(10,
							confirm_message, &server) != FT_ERR_INVALID_OPERATION
						|| WorldRevisionServer::handle_message(10,
							altered_confirm_message, &server)
							!= FT_ERR_INVALID_ARGUMENT
						|| fixture.start_commit_count != 1U
						|| fixture.committed_actor_id != 42U
						|| fixture.committed_loom_id != 99U
						|| fixture.committed_request_id != 9U
						|| fixture.committed_selection_revision
							!= selection_revision
						|| fixture.committed_policy_revision != policy_revision
						|| fixture.committed_opening_card_count != 6U
						|| WorldRevisionServer::handle_message(10,
							cancel_message, &server) != FT_ERR_SUCCESS
						|| world.world_revision().pending
						|| WorldRevisionServer::handle_message(10,
							cancel_message, &server) != FT_ERR_SUCCESS
						|| fixture.action_response_count != 9U
						|| fixture.action_response.find("\"request_id\":\"8\"")
							== std::string::npos)
						error_code = FT_ERR_INVALID_STATE;
					else
						error_code = FT_ERR_SUCCESS;
				}
			}
		}
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		failure_stage = "accepted commit";
		const char *accepted_begin_message =
			"{\"world_revision\":{\"action\":\"begin\",\"mode\":\"3\",\"request_id\":\"12\"}}";
		const char *accepted_cancel_message =
			"{\"world_revision\":{\"action\":\"cancel\",\"request_id\":\"14\"}}";
		char accepted_confirm_message[320];
		char altered_accepted_confirm_message[320];
		World::WorldRevision accepted_revision = {};
		fixture.allow_start_commit = true;
		if (WorldRevisionServer::handle_message(10, accepted_begin_message,
				&server) != FT_ERR_SUCCESS)
			error_code = FT_ERR_INVALID_STATE;
		accepted_revision = world.world_revision();
		std::snprintf(accepted_confirm_message,
			sizeof(accepted_confirm_message),
			"{\"world_revision\":{\"action\":\"confirm\",\"request_id\":\"13\",\"selection_revision\":\"%llu\",\"policy_revision\":\"%llu\",\"opening_card_count\":\"4\"}}",
			static_cast<unsigned long long>(
				accepted_revision.selection_revision),
			static_cast<unsigned long long>(
				accepted_revision.cost_policy_revision));
		std::snprintf(altered_accepted_confirm_message,
			sizeof(altered_accepted_confirm_message),
			"{\"world_revision\":{\"action\":\"confirm\",\"request_id\":\"13\",\"selection_revision\":\"%llu\",\"policy_revision\":\"%llu\",\"opening_card_count\":\"5\"}}",
			static_cast<unsigned long long>(
				accepted_revision.selection_revision),
			static_cast<unsigned long long>(
				accepted_revision.cost_policy_revision));
		if (error_code == FT_ERR_SUCCESS
			&& (WorldRevisionServer::handle_message(10,
					accepted_confirm_message, &server) != FT_ERR_SUCCESS
				|| WorldRevisionServer::handle_message(10,
					accepted_confirm_message, &server) != FT_ERR_SUCCESS
				|| WorldRevisionServer::handle_message(10,
					altered_accepted_confirm_message, &server)
					!= FT_ERR_INVALID_ARGUMENT
				|| WorldRevisionServer::handle_message(10,
					accepted_cancel_message, &server)
					!= FT_ERR_INVALID_OPERATION
				|| !world.world_revision().pending
				|| fixture.start_commit_count != 2U))
			error_code = FT_ERR_INVALID_STATE;
	}
	server.destroy();
	if (world.world_revision().pending
		&& world.cancel_world_revision() != FT_ERR_SUCCESS
		&& error_code == FT_ERR_SUCCESS)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		failure_stage = "unconfigured confirm";
		World::WorldRevision unconfigured_revision = {};

		if (unconfigured_server.set_authorizer(
				&world_revision_validator_authorize_client, &fixture)
			!= FT_ERR_SUCCESS
			|| unconfigured_server.set_station_access_authorizer(
				&world_revision_validator_authorize_station_access, &fixture)
			!= FT_ERR_SUCCESS
			|| unconfigured_server.set_occupancy_provider(
				&world_revision_validator_provide_occupancy, &fixture)
			!= FT_ERR_SUCCESS
			|| unconfigured_server.initialize() != FT_ERR_SUCCESS
			|| WorldRevisionServer::handle_message(10, instability_message,
				&unconfigured_server) != FT_ERR_PERMISSION_DENIED
			|| fixture.instability_response_count != 2U
			|| unconfigured_server.handle_revision_message(begin_message,
				42U, 99U) != FT_ERR_SUCCESS)
			error_code = FT_ERR_INVALID_STATE;
		unconfigured_revision = world.world_revision();
		std::snprintf(unconfigured_confirm_message,
			sizeof(unconfigured_confirm_message),
			"{\"world_revision\":{\"action\":\"confirm\",\"request_id\":\"10\",\"selection_revision\":\"%llu\",\"policy_revision\":\"%llu\",\"opening_card_count\":\"4\"}}",
			static_cast<unsigned long long>(
				unconfigured_revision.selection_revision),
			static_cast<unsigned long long>(
				unconfigured_revision.cost_policy_revision));
		if (error_code == FT_ERR_SUCCESS
			&& (unconfigured_server.handle_revision_message(
					unconfigured_confirm_message, 42U, 99U)
					!= FT_ERR_INVALID_OPERATION
				|| !world.world_revision().pending
				|| world.world_revision().regenerating))
			error_code = FT_ERR_INVALID_STATE;
		if (world.world_revision().pending)
			(void)world.cancel_world_revision();
		unconfigured_server.destroy();
	}
	if (error_code != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"world-revision: server authorization stage=%s code=%d\n",
			failure_stage, error_code);
	return (error_code);
}

int32_t WorldRevisionValidator::test_atomic_generation_batch_capacity() noexcept
{
	WorldGenerationPipeline pipeline;
	voxel_generation_config config;
	std::vector<WorldGenerationPipeline::GenerationSubmission> submissions;
	int32_t error_code;

	error_code = voxel_default_generation_config(config);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = pipeline.initialize(1U, 1U);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	try
	{
		submissions.push_back({1U, 1U, 1U, 1U, 0, 0,
			"batch-capacity-test", &config, VOXEL_STAGE_BASE_TERRAIN,
				WorldGenerationPipeline::WorldGenerationOperation::STREAM, nullptr,
				nullptr});
		submissions.push_back({2U, 1U, 1U, 1U, 1, 0,
			"batch-capacity-test", &config, VOXEL_STAGE_BASE_TERRAIN,
				WorldGenerationPipeline::WorldGenerationOperation::STREAM, nullptr,
				nullptr});
	}
	catch (...)
	{
		(void)pipeline.destroy();
		return (FT_ERR_NO_MEMORY);
	}
	error_code = pipeline.submit_generation_batch(submissions);
	if (error_code == FT_ERR_SUCCESS || pipeline.queued_count() != 0U)
		error_code = FT_ERR_INVALID_STATE;
	else if (error_code == FT_ERR_FULL)
		error_code = FT_ERR_SUCCESS;
	if (pipeline.destroy() != FT_ERR_SUCCESS && error_code == FT_ERR_SUCCESS)
		error_code = FT_ERR_INVALID_STATE;
	return (error_code);
}

int32_t WorldRevisionValidator::test_magma_feature_generation() noexcept
{
	WorldGenerationPipeline pipeline;
	WorldChunk fallback_chunk;
	voxel_generation_config config;
	std::vector<WorldGenerationPipeline::GenerationFeaturePlan> features;
	std::vector<WorldGenerationPipeline::GenerationFeaturePlan>
		fallback_features;
	std::vector<WorldGenerationPipeline::GenerationSubmission> submissions;
	std::unique_ptr<WorldGenerationPipeline::Result> result;
	std::vector<uint32_t> blocks;
	int32_t error_code = voxel_default_generation_config(config);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	try
	{
		fallback_features.push_back({
			WorldGenerationPipeline::GenerationFeaturePlan::SURFACE_MAGMA_POOL,
			0x6d61676d6166616cULL, 0U, 0, 0, 0U, {}, {}});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	error_code = fallback_chunk.chunk.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldChunkGenerationWorker::apply_feature_plans(
			fallback_chunk, fallback_features);
	uint32_t fallback_floor = GAME_VOXEL_AIR_BLOCK;
	uint32_t fallback_lava = GAME_VOXEL_AIR_BLOCK;
	uint32_t fallback_rim = GAME_VOXEL_AIR_BLOCK;
	if (error_code == FT_ERR_SUCCESS
		&& (fallback_chunk.chunk.read_block(8, 1, 8, &fallback_floor)
			!= FT_ERR_SUCCESS
			|| fallback_chunk.chunk.read_block(8, 2, 8, &fallback_lava)
			!= FT_ERR_SUCCESS
			|| fallback_chunk.chunk.read_block(6, 2, 6, &fallback_rim)
			!= FT_ERR_SUCCESS
			|| fallback_floor != VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK
			|| fallback_lava != VOXEL_GENERATOR_LAVA_BLOCK
			|| fallback_rim != VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK))
		error_code = FT_ERR_INVALID_STATE;
	if (fallback_chunk.chunk.destroy() != FT_ERR_SUCCESS
		&& error_code == FT_ERR_SUCCESS)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	for (uint32_t biome_index = 0U; biome_index < config.biome_count;
		++biome_index)
	{
		error_code = config.set_biome_height_profile(biome_index, 40, 0, 2U);
		if (error_code == FT_ERR_SUCCESS)
			error_code = config.set_biome_decoration_policy(biome_index,
				FT_FALSE, FT_FALSE, 0U, 0U);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
	}
	error_code = config.set_biome_transitions_enabled(FT_FALSE);
	if (error_code == FT_ERR_SUCCESS)
		error_code = config.set_mountain_ridges_enabled(FT_FALSE);
	if (error_code == FT_ERR_SUCCESS)
		error_code = config.set_erosion_enabled(FT_FALSE);
	if (error_code == FT_ERR_SUCCESS)
		error_code = config.set_water_chance_percent(0U);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	try
	{
		features.push_back({2U, 0x112233445566ULL, 0U, 0, 0, 0U, {}, {}});
		blocks.resize(GAME_VOXEL_CHUNK_WIDTH * GAME_VOXEL_CHUNK_HEIGHT
			* GAME_VOXEL_CHUNK_DEPTH);
		submissions.push_back({1U, 1U, 1U, 1U, 0, 0,
			"magma-feature-validation", &config, VOXEL_STAGE_BASE_TERRAIN,
			WorldGenerationPipeline::WorldGenerationOperation::REGENERATE,
		nullptr, &features});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	error_code = pipeline.initialize(1U, 2U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = pipeline.submit_generation_batch(submissions);
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (error_code == FT_ERR_SUCCESS && result == nullptr
		&& std::chrono::steady_clock::now() < deadline)
	{
		error_code = pipeline.poll(result);
		if (error_code == FT_ERR_NOT_FOUND)
		{
			error_code = FT_ERR_SUCCESS;
			std::this_thread::yield();
		}
	}
	if (error_code == FT_ERR_SUCCESS && result == nullptr)
		error_code = FT_ERR_TIMEOUT;
	if (error_code == FT_ERR_SUCCESS && (result->error_code != FT_ERR_SUCCESS
		|| result->chunk == nullptr
		|| result->applied_feature_plans.size() != 1U))
		error_code = result == nullptr ? FT_ERR_INVALID_STATE
			: (result->error_code != FT_ERR_SUCCESS ? result->error_code
				: FT_ERR_INVALID_STATE);
	if (error_code == FT_ERR_SUCCESS)
		error_code = result->chunk->chunk.copy_blocks(blocks.data(),
			static_cast<uint32_t>(blocks.size()));
	bool found_pool = false;
	if (error_code == FT_ERR_SUCCESS)
	{
		for (int32_t y = 1; y < GAME_VOXEL_CHUNK_HEIGHT && !found_pool; ++y)
		{
			for (int32_t center_z = 2; center_z < 14 && !found_pool;
				++center_z)
			{
				for (int32_t center_x = 2; center_x < 14 && !found_pool;
					++center_x)
				{
					bool matches = true;
					for (int32_t dz = -2; dz <= 2 && matches; ++dz)
					{
						for (int32_t dx = -2; dx <= 2; ++dx)
						{
							const std::size_t index = static_cast<std::size_t>(
								((center_z + dz) * GAME_VOXEL_CHUNK_HEIGHT + y)
									* GAME_VOXEL_CHUNK_WIDTH
								+ center_x + dx);
							const bool rim = dx == -2 || dx == 2
								|| dz == -2 || dz == 2;
							const uint32_t expected = rim
								? VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK
								: VOXEL_GENERATOR_LAVA_BLOCK;
							if (blocks[index] != expected)
							{
								matches = false;
								break ;
							}
						}
					}
					found_pool = matches;
				}
			}
		}
		if (!found_pool)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (pipeline.is_initialized()
		&& pipeline.destroy() != FT_ERR_SUCCESS
		&& error_code == FT_ERR_SUCCESS)
		error_code = FT_ERR_INVALID_STATE;
	return (error_code);
}

int32_t WorldRevisionValidator::test_village_feature_generation() noexcept
{
	WorldGenerationPipeline pipeline;
	WorldChunk fallback_chunk;
	voxel_generation_config config;
	std::vector<WorldGenerationPipeline::GenerationFeaturePlan> features;
	std::vector<WorldGenerationPipeline::GenerationSubmission> submissions;
	std::unique_ptr<WorldGenerationPipeline::Result> result;
	std::vector<uint32_t> blocks;
	int32_t error_code = voxel_default_generation_config(config);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	/* A flat all-air fixture has no natural village site. The fallback planner
	 * must still produce a supported, bounded pad inside its assigned chunk. */
	try
	{
		features.push_back({WorldGenerationPipeline::GenerationFeaturePlan::VILLAGE,
			0x66616c6c6261636bULL, 0U, 0, 0, 0U, {}, {}});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	error_code = fallback_chunk.chunk.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldChunkGenerationWorker::apply_feature_plans(
			fallback_chunk, features);
	uint32_t fallback_floor = GAME_VOXEL_AIR_BLOCK;
	uint32_t fallback_house = GAME_VOXEL_AIR_BLOCK;
	if (error_code == FT_ERR_SUCCESS
		&& (fallback_chunk.chunk.read_block(8, 1, 8, &fallback_floor)
			!= FT_ERR_SUCCESS
			|| fallback_chunk.chunk.read_block(5, 2, 7, &fallback_house)
			!= FT_ERR_SUCCESS
			|| fallback_floor != VOXEL_GENERATOR_DIRT_BLOCK
			|| fallback_house != VOXEL_GENERATOR_STONE_BLOCK))
		error_code = FT_ERR_INVALID_STATE;
	if (fallback_chunk.chunk.destroy() != FT_ERR_SUCCESS
		&& error_code == FT_ERR_SUCCESS)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	features.clear();
	try
	{
		features.push_back({WorldGenerationPipeline::GenerationFeaturePlan::VILLAGE,
			0x77696c6c616765ULL, 0U, 0, 0, 0U, {}, {}});
		WorldGenerationPipeline::GenerationFeaturePlan::SpawnerDefinition
			spawner = {};
		spawner.x = 4.5;
		spawner.y = 20.0;
		spawner.z = 6.5;
		spawner.entity_type = static_cast<uint32_t>(
			EntityState::Kind::ZOMBIE);
		spawner.maximum_alive = 2U;
		spawner.spawn_interval_ticks = 200U;
		spawner.loot_table_id = MINECRAFT_LOOT_ZOMBIE;
		features[0U].spawner_definitions.push_back(spawner);
		features[0U].loot_drops.push_back({
			MinecraftItemCatalog::EMERALD_MATERIAL_ITEM_ID, 1,
			4.5, 21.0, 6.5});
		features[0U].spawner_definitions.resize(
			WorldGenerationPipeline::GenerationFeaturePlan::
				MAXIMUM_SPAWNER_DEFINITIONS + 1U, spawner);
		std::unique_ptr<WorldGenerationPipeline::Request> rejected_request;
		if (WorldGenerationRequestBuilder::build(rejected_request, 1U, 1U,
				1U, 1U, 1U, 0, 0, "spawner-limit-validation", config,
				VOXEL_STAGE_BASE_TERRAIN,
				WorldGenerationPipeline::WorldGenerationOperation::REGENERATE,
				nullptr, &features) != FT_ERR_FULL)
			return (FT_ERR_INVALID_STATE);
		features[0U].spawner_definitions.resize(1U);
		features[0U].loot_drops.resize(
			WorldGenerationPipeline::GenerationFeaturePlan::
				MAXIMUM_LOOT_DROPS + 1U,
			features[0U].loot_drops[0U]);
		if (WorldGenerationRequestBuilder::build(rejected_request, 2U, 1U,
				1U, 1U, 1U, 0, 0, "loot-drop-limit-validation", config,
				VOXEL_STAGE_BASE_TERRAIN,
				WorldGenerationPipeline::WorldGenerationOperation::REGENERATE,
				nullptr, &features) != FT_ERR_FULL)
			return (FT_ERR_INVALID_STATE);
		features[0U].loot_drops.resize(1U);
		blocks.resize(GAME_VOXEL_CHUNK_WIDTH * GAME_VOXEL_CHUNK_HEIGHT
			* GAME_VOXEL_CHUNK_DEPTH);
		submissions.push_back({1U, 1U, 1U, 1U, 0, 0,
			"village-feature-validation", &config, VOXEL_STAGE_BASE_TERRAIN,
			WorldGenerationPipeline::WorldGenerationOperation::REGENERATE,
			nullptr, &features});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	error_code = pipeline.initialize(1U, 2U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = pipeline.submit_generation_batch(submissions);
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (error_code == FT_ERR_SUCCESS && result == nullptr
		&& std::chrono::steady_clock::now() < deadline)
	{
		error_code = pipeline.poll(result);
		if (error_code == FT_ERR_NOT_FOUND)
		{
			error_code = FT_ERR_SUCCESS;
			std::this_thread::yield();
		}
	}
	if (error_code == FT_ERR_SUCCESS && result == nullptr)
		error_code = FT_ERR_TIMEOUT;
	if (error_code == FT_ERR_SUCCESS && (result->error_code != FT_ERR_SUCCESS
		|| result->chunk == nullptr || result->applied_feature_plans.size() != 1U
		|| result->applied_feature_plans[0].kind
			!= WorldGenerationPipeline::GenerationFeaturePlan::VILLAGE
		|| result->applied_feature_plans[0].spawner_definitions.size() != 1U
		|| result->applied_feature_plans[0].spawner_definitions[0U]
			.entity_type != static_cast<uint32_t>(EntityState::Kind::ZOMBIE)
		|| result->applied_feature_plans[0].loot_drops.size() != 1U
		|| result->applied_feature_plans[0].loot_drops[0U].item_id
			!= MinecraftItemCatalog::EMERALD_MATERIAL_ITEM_ID))
		error_code = result == nullptr ? FT_ERR_INVALID_STATE
			: (result->error_code != FT_ERR_SUCCESS ? result->error_code
				: FT_ERR_INVALID_STATE);
	if (error_code == FT_ERR_SUCCESS)
		error_code = result->chunk->chunk.copy_blocks(blocks.data(),
			static_cast<uint32_t>(blocks.size()));
	const auto block_at = [&blocks](int32_t x, int32_t y,
		int32_t z) noexcept -> uint32_t
	{
		return (blocks[static_cast<std::size_t>((z * GAME_VOXEL_CHUNK_HEIGHT
			+ y) * GAME_VOXEL_CHUNK_WIDTH + x)]);
	};
	const auto is_house = [&block_at](int32_t center_x, int32_t center_z,
		int32_t floor_y, int32_t doorway_direction) noexcept -> bool
	{
		const int32_t roof_y = floor_y + 3;
		for (int32_t dz = -1; dz <= 1; ++dz)
		{
			for (int32_t dx = -1; dx <= 1; ++dx)
			{
				if (block_at(center_x + dx, floor_y, center_z + dz)
					!= VOXEL_GENERATOR_STONE_BLOCK
					|| block_at(center_x + dx, roof_y, center_z + dz)
						!= VOXEL_GENERATOR_OAK_LEAVES_BLOCK)
					return (false);
				if (dx != -1 && dx != 1 && dz != -1 && dz != 1)
					continue ;
				for (int32_t wall_y = floor_y + 1; wall_y < roof_y; ++wall_y)
				{
					const bool doorway = dz == 0
						&& dx == doorway_direction;
					const uint32_t expected = doorway ? GAME_VOXEL_AIR_BLOCK
						: VOXEL_GENERATOR_OAK_LOG_BLOCK;
					if (block_at(center_x + dx, wall_y, center_z + dz)
						!= expected)
						return (false);
				}
			}
		}
		return (true);
	};
	bool found_village = false;
	if (error_code == FT_ERR_SUCCESS)
	{
		for (int32_t floor_y = 4; floor_y + 3 < GAME_VOXEL_CHUNK_HEIGHT
			&& !found_village; ++floor_y)
		{
			for (int32_t z = 1; z + 1 < GAME_VOXEL_CHUNK_DEPTH
				&& !found_village; ++z)
			{
				for (int32_t left_x = 1; left_x + 5 < GAME_VOXEL_CHUNK_WIDTH
					&& !found_village; ++left_x)
				{
					const int32_t village_center_x = left_x + 2;
					found_village = village_center_x >= 5
						&& village_center_x <= 10 && z >= 5 && z <= 10
						&& is_house(left_x, z, floor_y, 1)
						&& is_house(left_x + 4, z, floor_y, -1);
				}
			}
		}
		if (!found_village)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (pipeline.is_initialized()
		&& pipeline.destroy() != FT_ERR_SUCCESS
		&& error_code == FT_ERR_SUCCESS)
		error_code = FT_ERR_INVALID_STATE;
	return (error_code);
}

int32_t WorldRevisionValidator::test_ruined_village_feature_generation() noexcept
{
	WorldChunk chunk;
	std::vector<WorldGenerationPipeline::GenerationFeaturePlan> plans;
	WorldGenerationPipeline::GenerationFeaturePlan plan = {};
	uint32_t cache_block = GAME_VOXEL_AIR_BLOCK;
	uint32_t missing_wall_block = GAME_VOXEL_AIR_BLOCK;
	int32_t error_code;
	plan.kind = WorldGenerationPipeline::GenerationFeaturePlan::RUINED_VILLAGE;
	plan.host_intent_sequence = 0x7275696e65647669ULL;
	plan.spawner_definitions.push_back({8.5, 5.0, 8.5,
		static_cast<uint32_t>(EntityState::Kind::ZOMBIE), 2U, 200U,
		MINECRAFT_LOOT_ZOMBIE});
	plan.loot_drops.push_back({MinecraftItemCatalog::EMERALD_MATERIAL_ITEM_ID,
		1, 8.5, 4.25, 8.5});
	try
	{
		plans.push_back(plan);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	error_code = chunk.chunk.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldChunkGenerationWorker::apply_feature_plans(chunk,
			plans);
	if (error_code == FT_ERR_SUCCESS
		&& (chunk.chunk.read_block(8, 4, 8, &cache_block) != FT_ERR_SUCCESS
			|| chunk.chunk.read_block(5, 5, 7, &missing_wall_block)
				!= FT_ERR_SUCCESS
			|| cache_block != VOXEL_GENERATOR_GOLD_ORE_BLOCK
			|| missing_wall_block != GAME_VOXEL_AIR_BLOCK
			|| plans[0].spawner_definitions.size() != 1U
			|| plans[0].loot_drops.size() != 1U))
		error_code = FT_ERR_INVALID_STATE;
	if (chunk.chunk.destroy() != FT_ERR_SUCCESS
		&& error_code == FT_ERR_SUCCESS)
		error_code = FT_ERR_INVALID_STATE;
	return (error_code);
}

int32_t WorldRevisionValidator::test_shipwreck_feature_generation() noexcept
{
	WorldChunk chunk;
	std::vector<WorldGenerationPipeline::GenerationFeaturePlan> plans;
	WorldGenerationPipeline::GenerationFeaturePlan plan = {};
	uint32_t keel_block = GAME_VOXEL_AIR_BLOCK;
	uint32_t interior_block = GAME_VOXEL_AIR_BLOCK;
	uint32_t mast_block = GAME_VOXEL_AIR_BLOCK;
	std::vector<MinecraftLootDrop> low_tier_loot;
	std::vector<MinecraftLootDrop> high_tier_loot;
	int32_t error_code = chunk.chunk.initialize();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	for (int32_t y = 48; y <= 63 && error_code == FT_ERR_SUCCESS; ++y)
		for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH
			&& error_code == FT_ERR_SUCCESS; ++z)
			for (int32_t x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
			{
				error_code = chunk.chunk.write_generated_block(x, y, z,
					VOXEL_GENERATOR_WATER_BLOCK);
				if (error_code != FT_ERR_SUCCESS)
					break ;
			}
	if (error_code == FT_ERR_SUCCESS)
	{
		plan.kind = WorldGenerationPipeline::GenerationFeaturePlan::SHIPWRECK;
		plan.host_intent_sequence = 0x7368697077726563ULL;
		plan.spawner_definitions.push_back({6.5, 56.0, 8.5,
			static_cast<uint32_t>(EntityState::Kind::ZOMBIE), 2U, 220U,
			MINECRAFT_LOOT_ZOMBIE});
		plan.loot_drops.push_back({MinecraftItemCatalog::DIAMOND_MATERIAL_ITEM_ID,
			1, 8.5, 59.0, 8.5});
		plans.push_back(plan);
		error_code = WorldChunkGenerationWorker::apply_feature_plans(chunk, plans);
	}
	if (error_code == FT_ERR_SUCCESS
		&& (chunk.chunk.read_block(8, 54, 8, &keel_block) != FT_ERR_SUCCESS
			|| chunk.chunk.read_block(8, 56, 8, &interior_block)
				!= FT_ERR_SUCCESS
			|| chunk.chunk.read_block(8, 62, 7, &mast_block)
				!= FT_ERR_SUCCESS
			|| keel_block != VOXEL_GENERATOR_OAK_LOG_BLOCK
			|| interior_block != GAME_VOXEL_AIR_BLOCK
			|| mast_block != VOXEL_GENERATOR_OAK_LOG_BLOCK
			|| plans[0].spawner_definitions.size() != 1U
			|| plans[0].loot_drops.size() != 1U))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = MinecraftLootCatalog::resolve(
			MINECRAFT_LOOT_SHIPWRECK_CACHE, 101U, low_tier_loot);
	if (error_code == FT_ERR_SUCCESS)
		error_code = MinecraftLootCatalog::resolve(
			MINECRAFT_LOOT_SHIPWRECK_CACHE_HIGH_REWARD, 101U,
			high_tier_loot);
	if (error_code == FT_ERR_SUCCESS
		&& (low_tier_loot.size() > MinecraftLootCatalog::MAXIMUM_DROPS_PER_DEATH
			|| high_tier_loot.size()
				> MinecraftLootCatalog::MAXIMUM_DROPS_PER_DEATH))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = chunk.chunk.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = chunk.chunk.initialize();
	if (error_code == FT_ERR_SUCCESS)
	{
		plans.clear();
		try
		{
			plans.push_back(plan);
		}
		catch (...)
		{
			error_code = FT_ERR_NO_MEMORY;
		}
		if (error_code == FT_ERR_SUCCESS
			&& WorldChunkGenerationWorker::apply_feature_plans(chunk, plans)
				!= FT_ERR_NOT_FOUND)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (chunk.chunk.destroy() != FT_ERR_SUCCESS
		&& error_code == FT_ERR_SUCCESS)
		error_code = FT_ERR_INVALID_STATE;
	return (error_code);
}

int32_t WorldRevisionValidator::test_volcano_feature_generation() noexcept
{
	WorldGenerationPipeline pipeline;
	voxel_generation_config config;
	std::vector<WorldGenerationPipeline::GenerationFeaturePlan> features;
	std::vector<WorldGenerationPipeline::GenerationSubmission> submissions;
	std::unique_ptr<WorldGenerationPipeline::Result> result;
	std::unique_ptr<WorldGenerationPipeline::Result> crater_chunk;
	std::unique_ptr<WorldGenerationPipeline::Result> seam_chunk;
	std::unique_ptr<WorldGenerationPipeline::Result> arena_chunk;
	std::vector<uint32_t> crater_blocks;
	std::vector<uint32_t> seam_blocks;
	std::vector<uint32_t> arena_blocks;
	int32_t error_code = voxel_default_generation_config(config);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	try
	{
		features.push_back({
			WorldGenerationPipeline::GenerationFeaturePlan::VOLCANO,
			0x766f6c63616e6fULL, 0U, 0, 0, 9U, {}, {}});
		crater_blocks.resize(GAME_VOXEL_CHUNK_WIDTH * GAME_VOXEL_CHUNK_HEIGHT
			* GAME_VOXEL_CHUNK_DEPTH);
		seam_blocks.resize(crater_blocks.size());
		arena_blocks.resize(crater_blocks.size());
		for (uint64_t request_id = 1U; request_id <= 3U; ++request_id)
		{
			const int32_t chunk_x = request_id == 1U ? 4
				: (request_id == 2U ? 5 : 6);
			submissions.push_back({request_id, 1U, 1U, 1U, chunk_x, 4,
				"volcano-feature-validation", &config, VOXEL_STAGE_BASE_TERRAIN,
				WorldGenerationPipeline::WorldGenerationOperation::REGENERATE,
				nullptr, &features});
		}
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	error_code = pipeline.initialize(1U, 3U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = pipeline.submit_generation_batch(submissions);
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);
	uint32_t completed_chunks = 0U;
	while (error_code == FT_ERR_SUCCESS && completed_chunks < 3U
		&& std::chrono::steady_clock::now() < deadline)
	{
		error_code = pipeline.poll(result);
		if (error_code == FT_ERR_NOT_FOUND)
		{
			error_code = FT_ERR_SUCCESS;
			std::this_thread::yield();
		}
		else if (error_code == FT_ERR_SUCCESS && result != nullptr)
		{
			if (result->error_code != FT_ERR_SUCCESS
				|| result->chunk == nullptr
				|| result->chunk->light_buffer_is_valid() == false
				|| result->chunk->light_is_current() == false
				|| result->chunk->light_ready_for_render == false
				|| result->applied_feature_plans.size() != 1U
				|| result->applied_feature_plans[0].kind
					!= WorldGenerationPipeline::GenerationFeaturePlan::VOLCANO)
				error_code = result->error_code != FT_ERR_SUCCESS
					? result->error_code : FT_ERR_INVALID_STATE;
			else if (result->chunk_x == 4)
				crater_chunk = std::move(result);
			else if (result->chunk_x == 5)
				seam_chunk = std::move(result);
			else if (result->chunk_x == 6)
				arena_chunk = std::move(result);
			else
				error_code = FT_ERR_INVALID_STATE;
			if (error_code == FT_ERR_SUCCESS)
				++completed_chunks;
		}
	}
	if (error_code == FT_ERR_SUCCESS && completed_chunks != 3U)
		error_code = FT_ERR_TIMEOUT;
	if (error_code == FT_ERR_SUCCESS)
		error_code = crater_chunk->chunk->chunk.copy_blocks(crater_blocks.data(),
			static_cast<uint32_t>(crater_blocks.size()));
	if (error_code == FT_ERR_SUCCESS)
		error_code = seam_chunk->chunk->chunk.copy_blocks(seam_blocks.data(),
			static_cast<uint32_t>(seam_blocks.size()));
	if (error_code == FT_ERR_SUCCESS)
		error_code = arena_chunk->chunk->chunk.copy_blocks(arena_blocks.data(),
			static_cast<uint32_t>(arena_blocks.size()));
	const auto block_at = [](const std::vector<uint32_t> &blocks,
		int32_t x, int32_t y,
		int32_t z) noexcept -> uint32_t
	{
		return (blocks[static_cast<std::size_t>((z * GAME_VOXEL_CHUNK_HEIGHT
			+ y) * GAME_VOXEL_CHUNK_WIDTH + x)]);
	};
	if (error_code == FT_ERR_SUCCESS)
	{
		const bool contained_lava =
			block_at(crater_blocks, 8, 18, 8) == VOXEL_GENERATOR_LAVA_BLOCK
			&& block_at(crater_blocks, 8, 17, 8)
				== VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK
			&& block_at(crater_blocks, 15, 18, 8)
				== VOXEL_GENERATOR_LAVA_BLOCK
			&& block_at(seam_blocks, 0, 18, 8)
				== VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK
			&& block_at(seam_blocks, 0, 17, 8)
				== VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK;
		const bool connected_passage =
			block_at(seam_blocks, 10, 21, 8) == GAME_VOXEL_AIR_BLOCK;
		const bool supported_arena =
			block_at(arena_blocks, 4, 20, 8)
				== VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK
			&& block_at(arena_blocks, 4, 19, 8)
				== VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK
			&& block_at(arena_blocks, 4, 21, 8) == GAME_VOXEL_AIR_BLOCK;
		const bool continuous_arena_shelf =
			block_at(seam_blocks, 15, 20, 8)
				== VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK
			&& block_at(arena_blocks, 0, 20, 8)
				== VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK
			&& block_at(arena_blocks, 0, 21, 8) == GAME_VOXEL_AIR_BLOCK;
		if (!contained_lava || !connected_passage || !supported_arena
			|| !continuous_arena_shelf)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (pipeline.is_initialized()
		&& pipeline.destroy() != FT_ERR_SUCCESS
		&& error_code == FT_ERR_SUCCESS)
		error_code = FT_ERR_INVALID_STATE;
	return (error_code);
}

int32_t WorldRevisionValidator::test_volcano_configured_footprint_generation()
	noexcept
{
	WorldGenerationPipeline pipeline;
	voxel_generation_config config;
	std::vector<WorldGenerationPipeline::GenerationFeaturePlan> features;
	std::vector<WorldGenerationPipeline::GenerationSubmission> submissions;
	std::vector<uint32_t> crater_blocks;
	std::vector<uint32_t> passage_blocks;
	std::vector<uint32_t> arena_blocks;
	const std::size_t block_count = GAME_VOXEL_CHUNK_WIDTH
		* GAME_VOXEL_CHUNK_HEIGHT * GAME_VOXEL_CHUNK_DEPTH;
	int32_t error_code = voxel_default_generation_config(config);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	try
	{
		features.push_back({
			WorldGenerationPipeline::GenerationFeaturePlan::VOLCANO,
			0x766f6c63616e6f31ULL, 0U, 4, 4, 12U, {}, {}});
		crater_blocks.resize(block_count);
		passage_blocks.resize(block_count);
		arena_blocks.resize(block_count);
		submissions.push_back({1U, 1U, 1U, 1U, 10, 10,
			"volcano-configured-footprint-validation", &config,
			VOXEL_STAGE_BASE_TERRAIN,
			WorldGenerationPipeline::WorldGenerationOperation::REGENERATE,
			nullptr, &features});
		submissions.push_back({2U, 1U, 1U, 1U, 11, 10,
			"volcano-configured-footprint-validation", &config,
			VOXEL_STAGE_BASE_TERRAIN,
			WorldGenerationPipeline::WorldGenerationOperation::REGENERATE,
			nullptr, &features});
		submissions.push_back({3U, 1U, 1U, 1U, 12, 10,
			"volcano-configured-footprint-validation", &config,
			VOXEL_STAGE_BASE_TERRAIN,
			WorldGenerationPipeline::WorldGenerationOperation::REGENERATE,
			nullptr, &features});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	error_code = pipeline.initialize(1U, 3U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = pipeline.submit_generation_batch(submissions);
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);
	uint32_t completed_chunks = 0U;
	while (error_code == FT_ERR_SUCCESS && completed_chunks < 3U
		&& std::chrono::steady_clock::now() < deadline)
	{
		std::unique_ptr<WorldGenerationPipeline::Result> result;
		error_code = pipeline.poll(result);
		if (error_code == FT_ERR_NOT_FOUND)
		{
			error_code = FT_ERR_SUCCESS;
			std::this_thread::yield();
			continue ;
		}
		if (error_code != FT_ERR_SUCCESS || result == nullptr)
			break ;
		if (result->error_code != FT_ERR_SUCCESS || result->chunk == nullptr
			|| result->chunk->light_buffer_is_valid() == false
			|| result->chunk->light_is_current() == false
			|| result->chunk->light_ready_for_render == false
			|| result->applied_feature_plans.size() != 1U
			|| result->applied_feature_plans[0].kind
				!= WorldGenerationPipeline::GenerationFeaturePlan::VOLCANO)
		{
			error_code = result->error_code != FT_ERR_SUCCESS
				? result->error_code : FT_ERR_INVALID_STATE;
			break ;
		}
		uint32_t *target = nullptr;
		if (result->chunk_x == 10)
			target = crater_blocks.data();
		else if (result->chunk_x == 11)
			target = passage_blocks.data();
		else if (result->chunk_x == 12)
			target = arena_blocks.data();
		else
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_SUCCESS)
			error_code = result->chunk->chunk.copy_blocks(target,
				static_cast<uint32_t>(block_count));
		if (error_code == FT_ERR_SUCCESS)
			++completed_chunks;
	}
	if (error_code == FT_ERR_SUCCESS && completed_chunks != 3U)
		error_code = FT_ERR_TIMEOUT;
	const auto block_at = [](const std::vector<uint32_t> &blocks,
		int32_t x, int32_t y, int32_t z) noexcept -> uint32_t
	{
		return (blocks[static_cast<std::size_t>((z * GAME_VOXEL_CHUNK_HEIGHT
			+ y) * GAME_VOXEL_CHUNK_WIDTH + x)]);
	};
	if (error_code == FT_ERR_SUCCESS
		&& (block_at(crater_blocks, 0, 18, 0) != VOXEL_GENERATOR_LAVA_BLOCK
			|| block_at(crater_blocks, 0, 17, 0)
				!= VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK
			|| block_at(passage_blocks, 0, 21, 0) != GAME_VOXEL_AIR_BLOCK
			|| block_at(arena_blocks, 0, 20, 0)
				!= VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK
			|| block_at(arena_blocks, 0, 19, 0)
				!= VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK
			|| block_at(arena_blocks, 0, 21, 0) != GAME_VOXEL_AIR_BLOCK))
		error_code = FT_ERR_INVALID_STATE;
	if (pipeline.is_initialized()
		&& pipeline.destroy() != FT_ERR_SUCCESS
		&& error_code == FT_ERR_SUCCESS)
		error_code = FT_ERR_INVALID_STATE;
	return (error_code);
}

int32_t WorldRevisionValidator::test_game_session_committed_start_retry()
	noexcept
{
	static const uint64_t start_scope = 0x574F524C444C4F4FULL;
	static const uint32_t start_transaction_type = 6U;
	const uint64_t actor_id = 42U;
	const uint64_t loom_id = 99U;
	const uint64_t request_id = 701U;
	const uint64_t selection_revision = 1U;
	const uint64_t policy_revision = 1U;
	const uint32_t opening_card_count = 4U;
	uint64_t clock_epoch_ms = 1000000U;
	std::unique_ptr<GameSession> session(new (std::nothrow) GameSession());
	std::unique_ptr<VoxelRenderer> renderer(new (std::nothrow) VoxelRenderer());
	ApplicationWindow window;
	std::vector<uint8_t> payload;
	uint64_t journal_sequence = 0U;
	bool replayed = false;
	std::string seed;
	int32_t result = FT_ERR_SUCCESS;

	if (session == nullptr || renderer == nullptr)
		return (FT_ERR_NO_MEMORY);
	try
	{
		seed = "world-revision-retry-validator-"
			+ std::to_string(std::chrono::steady_clock::now()
				.time_since_epoch().count());
		payload.reserve(56U);
		payload.push_back('W');
		payload.push_back('L');
		payload.push_back('S');
		payload.push_back('T');
		world_revision_validator_append_u32(payload, 1U);
		world_revision_validator_append_u64(payload, actor_id);
		world_revision_validator_append_u64(payload, loom_id);
		world_revision_validator_append_u64(payload, selection_revision);
		world_revision_validator_append_u64(payload, policy_revision);
		world_revision_validator_append_u32(payload, opening_card_count);
		world_revision_validator_append_u32(payload, 200004U);
		world_revision_validator_append_u64(payload, 0U);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	result = session->configure_revision_clock_provider(
		&world_revision_validator_clock,
		&clock_epoch_ms);
	if (result == FT_ERR_SUCCESS)
		result = session->start(seed, window, *renderer);
	if (result == FT_ERR_SUCCESS)
		result = session->transaction_journal_.prepare(start_scope, request_id,
			start_transaction_type, payload.data(),
			static_cast<uint32_t>(payload.size()), &journal_sequence,
			&replayed);
	if (result == FT_ERR_SUCCESS)
		result = session->transaction_journal_.mark_committed(start_scope,
			request_id);
	if (result == FT_ERR_SUCCESS)
		result = session->commit_regeneration_start(actor_id, loom_id,
			request_id, selection_revision, policy_revision,
			opening_card_count);
	if (result == FT_ERR_SUCCESS
		&& (session->world_loom_start_committed_
			|| session->world_.world_revision().pending
			|| session->world_.revision_barriers_active()))
		result = FT_ERR_INVALID_STATE;
	if (session->active_)
		session->stop();
	if (session->world_item_save_path_[0] != '\0')
		(void)std::remove(session->world_item_save_path_);
	if (session->world_revision_save_path_[0] != '\0')
		(void)std::remove(session->world_revision_save_path_);
	if (result != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"world-revision: GameSession stale committed retry failed code=%d\n",
			result);
	return (result);
}

static bool world_revision_validator_chunk_has_stabilizer_site(
	const World &world, int32_t chunk_x, int32_t chunk_z) noexcept
{
	static const int32_t CENTERS[3U][2U] = {
		{4, 4}, {12, 4}, {8, 12}};
	const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
	if (chunk == nullptr || !chunk->initialized)
		return (false);
	for (const auto &center : CENTERS)
	{
		int32_t minimum_ground = GAME_VOXEL_CHUNK_HEIGHT;
		int32_t maximum_ground = -1;
		bool valid_site = true;
		for (int32_t offset_z = -2; offset_z <= 2 && valid_site; ++offset_z)
		{
			for (int32_t offset_x = -2; offset_x <= 2 && valid_site; ++offset_x)
			{
				int32_t ground_y = -1;
				for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 8; y >= 2; --y)
				{
					uint32_t block = GAME_VOXEL_AIR_BLOCK;
					if (chunk->chunk.read_block(center[0] + offset_x, y,
							center[1] + offset_z, &block) != FT_ERR_SUCCESS)
						return (false);
					if (voxel_block_is_solid(block) != FT_FALSE)
					{
						ground_y = y;
						break ;
					}
					if (block != GAME_VOXEL_AIR_BLOCK)
						break ;
				}
				if (ground_y < 2 || ground_y + 7 >= GAME_VOXEL_CHUNK_HEIGHT)
				{
					valid_site = false;
					break ;
				}
				for (int32_t y = ground_y + 1; y <= ground_y + 6; ++y)
				{
					uint32_t block = GAME_VOXEL_AIR_BLOCK;
					if (chunk->chunk.read_block(center[0] + offset_x, y,
							center[1] + offset_z, &block) != FT_ERR_SUCCESS
						|| block != GAME_VOXEL_AIR_BLOCK)
					{
						valid_site = false;
					break ;
					}
				}
				minimum_ground = std::min(minimum_ground, ground_y);
				maximum_ground = std::max(maximum_ground, ground_y);
			}
		}
		if (valid_site && minimum_ground == maximum_ground)
			return (true);
	}
	return (false);
}

int32_t WorldRevisionValidator::test_game_session_volatile_stabilizer()
	noexcept
{
	static const uint64_t FIRST_REQUEST_ID = 0x5354414200000101ULL;
	static const uint64_t SECOND_REQUEST_ID = 0x5354414200000102ULL;
	static const uint32_t STABILIZER_DEFINITION_ID = 16U;
	const std::string seed("volatile-stabilizer-session-validator");
	WorldChunkInstabilityPolicy instability_policy = {};
	const uint32_t risk_curve[] = {0U, 10000U};
	uint64_t clock_epoch_ms = 2000000U;
	std::unique_ptr<GameSession> session(new (std::nothrow) GameSession());
	std::unique_ptr<GameSession> restored(new (std::nothrow) GameSession());
	std::unique_ptr<VoxelRenderer> renderer(new (std::nothrow) VoxelRenderer());
	ApplicationWindow window;
	RegenerationPolicyIntent first_intent = {};
	RegenerationPolicyIntent replayed_first_intent = {};
	RegenerationPolicyIntent second_intent = {};
	RegenerationPolicyIntent restored_retry_intent = {};
	WorldChunkInstabilityRecord record = {};
	WorldSpawnerDefinition spawner_definition = {};
	std::vector<WorldSpawnerDeath> spawner_deaths;
	std::vector<WorldEntitySnapshot> session_entities;
	World::WorldRevision revision;
	uint64_t selection_revision = 0U;
	uint32_t stabilizer_uses = 0U;
	int32_t stabilizer_chunk_x = 0;
	int32_t stabilizer_chunk_z = 0;
	int32_t player_chunk_x = 0;
	int32_t player_chunk_z = 0;
	uint32_t spawner_id = 0U;
	uint32_t spawner_entity_id = 0U;
	uint32_t first_instance_id = 0U;
	uint32_t second_instance_id = 0U;
	bool wheat_planting_failed = false;
	const char *wheat_planting_stage = "wheat-seed-planting-fixture";
	int32_t result = FT_ERR_SUCCESS;
	RegenerationCardChoice choice = {};
	const char *failure_stage = "allocation";

	if (session == nullptr || restored == nullptr || renderer == nullptr)
		return (FT_ERR_NO_MEMORY);
	instability_policy.maximum_value = 100U;
	instability_policy.default_initial_value = 20U;
	instability_policy.reward_threshold_count = 1U;
	instability_policy.reward_thresholds[0] = 30U;
	instability_policy.danger_threshold_count = 1U;
	instability_policy.danger_thresholds[0] = 50U;
	failure_stage = "configure-first-session";
	result = session->configure_chunk_instability_policy(instability_policy);
	if (result == FT_ERR_SUCCESS)
		result = session->configure_volatile_stabilizer_policy(risk_curve, 2U,
			5, 7);
	if (result == FT_ERR_SUCCESS)
		result = session->configure_revision_clock_provider(
			&world_revision_validator_clock, &clock_epoch_ms);
	failure_stage = "start-first-session";
	if (result == FT_ERR_SUCCESS)
		result = session->start(seed, window, *renderer);
	if (result == FT_ERR_SUCCESS)
	{
		ft_bool recipe_newly_known = FT_FALSE;
		failure_stage = "unlock-stabilizer-card-recipe-fixture";
		result = session->recipe_catalog_.unlock_recipe(
			MinecraftRecipeCatalog::VOLATILE_STABILIZER_CARD_RECIPE_ID,
			0x53544142494C495AULL, &recipe_newly_known);
		if (result == FT_ERR_SUCCESS && recipe_newly_known != FT_TRUE)
			result = FT_ERR_INVALID_STATE;
	}
	if (result == FT_ERR_SUCCESS)
	{
		EntityState chicken_state;
		std::vector<WorldAnimalProductionEvent> production_events;
		std::vector<WorldItemSnapshot> egg_items;
		WorldEntitySnapshot chicken_snapshot = {};
		char original_save_path[sizeof(session->world_item_save_path_)] = {};
		uint32_t chicken_id = 0U;
		bool animal_changed = false;
		chicken_state.type = static_cast<uint32_t>(
			EntityState::Kind::CHICKEN);
		chicken_state.position_x = 40.5;
		chicken_state.position_y = 20.0;
		chicken_state.position_z = -5.5;
		chicken_state.health = 10;
		chicken_state.tick =
			WorldAnimalSimulation::CHICKEN_EGG_PERIOD_MINIMUM_TICKS;
		failure_stage = "chicken-egg-spawn-fixture";
		result = session->world_entities_.spawn(chicken_state, &chicken_id);
		const uint64_t egg_period =
			WorldAnimalSimulation::CHICKEN_EGG_PERIOD_MINIMUM_TICKS
			+ chicken_id
			% WorldAnimalSimulation::CHICKEN_EGG_PERIOD_VARIANCE_TICKS;
		const uint64_t egg_tick = ((egg_period / 10U) + 1U) * 10U;
		if (result == FT_ERR_SUCCESS)
			result = WorldAnimalSimulation::tick(session->world_,
				session->world_entities_, chicken_state.position_x,
				chicken_state.position_y, chicken_state.position_z, egg_tick,
				&animal_changed, &production_events);
		if (result == FT_ERR_SUCCESS
			&& (production_events.size() != 1U
				|| production_events[0].animal_entity_id != chicken_id))
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
		{
			std::memcpy(original_save_path, session->world_item_save_path_,
				sizeof(original_save_path));
			std::strncpy(session->world_item_save_path_,
				"saves/world_items/missing-parent/test.wisp",
				sizeof(session->world_item_save_path_) - 1U);
			session->world_item_save_path_[
				sizeof(session->world_item_save_path_) - 1U] = '\0';
			failure_stage = "chicken-egg-checkpoint-rollback";
			if (session->process_animal_production(production_events)
				== FT_ERR_SUCCESS)
				result = FT_ERR_INVALID_STATE;
			std::memcpy(session->world_item_save_path_, original_save_path,
				sizeof(original_save_path));
		}
		if (result == FT_ERR_SUCCESS)
			result = session->world_entities_.get(chicken_id,
				&chicken_snapshot);
		if (result == FT_ERR_SUCCESS
			&& chicken_snapshot.state.tick != chicken_state.tick)
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
			result = session->world_items_.query_nearby(40.5, 20.0, -5.5,
				2.0, 1U, egg_tick, &egg_items);
		if (result == FT_ERR_SUCCESS && !egg_items.empty())
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
			result = WorldAnimalSimulation::tick(session->world_,
				session->world_entities_, chicken_state.position_x,
				chicken_state.position_y, chicken_state.position_z, egg_tick,
				&animal_changed, &production_events);
		if (result == FT_ERR_SUCCESS && production_events.size() != 1U)
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
			result = session->process_animal_production(production_events);
		if (result == FT_ERR_SUCCESS)
			result = session->world_items_.query_nearby(40.5, 20.0, -5.5,
				2.0, 1U, egg_tick, &egg_items);
		if (result == FT_ERR_SUCCESS
			&& (egg_items.size() != 1U
				|| egg_items[0].item_id != MinecraftItemCatalog::EGG_ITEM_ID
				|| egg_items[0].quantity != 1))
			result = FT_ERR_INVALID_STATE;
	}
	if (result == FT_ERR_SUCCESS)
	{
		spawner_definition.x = 10.5;
		spawner_definition.y = 20.0;
		spawner_definition.z = -5.5;
		spawner_definition.entity_type = static_cast<uint32_t>(
			EntityState::Kind::SKELETON);
		spawner_definition.maximum_alive = 2U;
		spawner_definition.spawn_interval_ticks = 40U;
		spawner_definition.loot_table_id = MINECRAFT_LOOT_SKELETON;
		result = session->world_spawners_.add(spawner_definition, 1000000U,
			&spawner_id);
	}
	failure_stage = "load-chunk-and-begin-revision";
	if (result == FT_ERR_SUCCESS)
	{
		failure_stage = "wait-for-fixture-chunk";
		result = WorldRevisionValidator::wait_for_loaded_chunk(session->world_,
			2, 0);
	}
	if (result == FT_ERR_SUCCESS)
	{
		failure_stage = "quiesce-fixture-streamer";
		result = WorldRevisionValidator::quiesce_stream_pipeline(session->world_);
	}
	if (result == FT_ERR_SUCCESS)
	{
		wheat_planting_failed = true;
		int32_t plant_x = -1;
		int32_t plant_y = -1;
		int32_t plant_z = -1;
		int32_t plant_chunk_x = 0;
		int32_t plant_chunk_z = 0;
		uint32_t support_id = GAME_VOXEL_AIR_BLOCK;
		uint32_t target_id = GAME_VOXEL_AIR_BLOCK;
		uint64_t inventory_revision = 0U;
		uint64_t chunk_revision = 0U;
		ft_sharedptr<game_item> seeds;
		ft_sharedptr<game_item> paper_reed_fiber;
		game_inventory_snapshot inventory_snapshot = {};
		failure_stage = wheat_planting_stage;
		for (int32_t z = -2 * GAME_VOXEL_CHUNK_DEPTH;
			z < 3 * GAME_VOXEL_CHUNK_DEPTH && plant_x < 0; ++z)
		{
			for (int32_t x = 0; x < 5 * GAME_VOXEL_CHUNK_WIDTH
				&& plant_x < 0; ++x)
			{
				for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 6; y > 0; --y)
				{
					if (!session->world_.block_id_at(x, y, z, &support_id)
						|| (support_id != VOXEL_GENERATOR_DIRT_BLOCK_ID
							&& support_id != VOXEL_GENERATOR_GRASS_BLOCK_ID))
						continue;
					bool clear_column = true;
					for (int32_t offset = 1; offset <= 5; ++offset)
					{
						if (!session->world_.block_id_at(x, y + offset, z,
								&target_id)
							|| target_id != GAME_VOXEL_AIR_BLOCK)
						{
							clear_column = false;
							break;
						}
					}
					if (!clear_column)
						continue;
					const int32_t candidate_chunk_x =
						WorldCoordinates::floor_divide(x,
							GAME_VOXEL_CHUNK_WIDTH);
					const int32_t candidate_chunk_z =
						WorldCoordinates::floor_divide(z,
							GAME_VOXEL_CHUNK_DEPTH);
					if (session->world_.is_chunk_protected(candidate_chunk_x,
						candidate_chunk_z))
						continue;
					plant_x = x;
					plant_y = y;
					plant_z = z;
					plant_chunk_x = candidate_chunk_x;
					plant_chunk_z = candidate_chunk_z;
					break;
				}
			}
		}
		if (plant_x < 0)
			result = FT_ERR_NOT_FOUND;
		wheat_planting_stage = "wheat-seed-find-chunk";
		WorldChunk *plant_chunk = result == FT_ERR_SUCCESS
			? session->world_.find_chunk_mutable(plant_chunk_x,
				plant_chunk_z) : nullptr;
		if (result == FT_ERR_SUCCESS && plant_chunk == nullptr)
			result = FT_ERR_NOT_FOUND;
		if (result == FT_ERR_SUCCESS)
		{
			wheat_planting_stage = "wheat-seed-till-call";
			chunk_revision = plant_chunk->voxel_revision;
			result = session->world_.till_farmland_at(plant_x, plant_y,
				plant_z, chunk_revision);
		}
		if (result == FT_ERR_SUCCESS)
			wheat_planting_stage = "wheat-seed-create-item";
		if (result == FT_ERR_SUCCESS)
			result = session->item_catalog_.create_item(
				MinecraftItemCatalog::SEEDS_ITEM_ID, 1, &seeds);
		if (result == FT_ERR_SUCCESS)
		{
			wheat_planting_stage = "wheat-seed-add-inventory";
			result = session->player_character_.get_inventory().add_item(seeds);
		}
		if (result == FT_ERR_SUCCESS)
			result = session->player_character_.get_inventory().get_snapshot(
				inventory_snapshot);
		auto seed_item = std::find_if(inventory_snapshot.items.begin(),
			inventory_snapshot.items.end(), [](const game_inventory_item_snapshot
				&item)
			{
				return (item.item_id == MinecraftItemCatalog::SEEDS_ITEM_ID
					&& item.stack_size == 1);
			});
		if (result == FT_ERR_SUCCESS
			&& seed_item == inventory_snapshot.items.end())
			result = FT_ERR_NOT_FOUND;
		if (result == FT_ERR_SUCCESS)
		{
			wheat_planting_stage = "wheat-seed-live-place";
			inventory_revision = inventory_snapshot.revision;
			session->selected_block_id_ =
				VOXEL_GENERATOR_WHEAT_CROP_IMMATURE_BLOCK_ID;
			session->camera_.x = static_cast<double>(plant_x) + 0.5;
			session->camera_.y = static_cast<double>(plant_y) + 4.0;
			session->camera_.z = static_cast<double>(plant_z) + 0.5;
			session->camera_.yaw = 0.0;
			session->camera_.pitch = -1.5707963267948966;
			plant_chunk = session->world_.find_chunk_mutable(plant_chunk_x,
				plant_chunk_z);
			if (plant_chunk == nullptr)
				result = FT_ERR_NOT_FOUND;
			else
				chunk_revision = plant_chunk->voxel_revision;
		}
		if (result == FT_ERR_SUCCESS)
		{
			double direction_x = 0.0;
			double direction_y = 0.0;
			double direction_z = 0.0;
			int32_t hit_x = 0;
			int32_t hit_y = 0;
			int32_t hit_z = 0;
			int32_t place_x = 0;
			int32_t place_y = 0;
			int32_t place_z = 0;
			uint32_t hit_id = GAME_VOXEL_AIR_BLOCK;
			World::RevisionProtectionInfo protection = {};
			PlayerController::camera_forward(session->camera_, &direction_x,
				&direction_y, &direction_z);
			result = session->world_.raycast_edit_target(session->camera_.x,
				session->camera_.y, session->camera_.z, direction_x,
				direction_y, direction_z, 10.0, &hit_x, &hit_y, &hit_z,
				&place_x, &place_y, &place_z, &hit_id);
			if (result == FT_ERR_SUCCESS
				&& (place_x != plant_x || place_y != plant_y + 1
					|| place_z != plant_z))
			{
				wheat_planting_stage = "wheat-seed-raycast-target";
				result = FT_ERR_PERMISSION_DENIED;
			}
			if (result == FT_ERR_SUCCESS
				&& hit_id != VOXEL_GENERATOR_FARMLAND_BLOCK_ID)
			{
				wheat_planting_stage = "wheat-seed-raycast-support";
				result = FT_ERR_PERMISSION_DENIED;
			}
			if (result == FT_ERR_SUCCESS)
				result = session->world_.get_revision_protection_info(
					plant_chunk_x, plant_chunk_z, &protection);
			if (result == FT_ERR_SUCCESS
				&& (protection.loom_safe_zone
					|| protection.manually_protected
					|| protection.occupied_player_exclusion))
			{
				wheat_planting_stage = "wheat-seed-raycast-protected";
				result = FT_ERR_PERMISSION_DENIED;
			}
			if (result == FT_ERR_SUCCESS && !protection.generation_protected)
			{
				wheat_planting_stage = "wheat-seed-generation-protection";
				result = FT_ERR_INVALID_STATE;
			}
		}
		if (result == FT_ERR_SUCCESS)
		{
			MinecraftBlockItemRule crop_rule = {};
			uint32_t resolved_crop = GAME_VOXEL_AIR_BLOCK;
			wheat_planting_stage = "wheat-seed-catalog-preflight";
			result = session->item_catalog_.resolve_block_item_rule(
				VOXEL_GENERATOR_WHEAT_CROP_IMMATURE_BLOCK_ID, &crop_rule);
			if (result == FT_ERR_SUCCESS
				&& (crop_rule.policy
					!= MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM
					|| crop_rule.item_id != MinecraftItemCatalog::SEEDS_ITEM_ID))
				result = FT_ERR_PERMISSION_DENIED;
			if (result == FT_ERR_SUCCESS)
				result = session->item_catalog_.resolve_placed_block_item(
					crop_rule.item_id, &resolved_crop);
			if (result == FT_ERR_SUCCESS
				&& resolved_crop != VOXEL_GENERATOR_WHEAT_CROP_IMMATURE_BLOCK_ID)
				result = FT_ERR_PERMISSION_DENIED;
		}
		if (result == FT_ERR_SUCCESS)
		{
			wheat_planting_stage = "wheat-seed-place-call";
			result = session->try_place_selected_inventory_block();
		}
		if (result == FT_ERR_SUCCESS)
			wheat_planting_stage = "wheat-seed-verify-state";
		if (result == FT_ERR_SUCCESS
			&& (!session->world_.block_id_at(plant_x, plant_y + 1, plant_z,
				&target_id)
				|| target_id
					!= VOXEL_GENERATOR_WHEAT_CROP_IMMATURE_BLOCK_ID))
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
			result = session->player_character_.get_inventory().get_snapshot(
				inventory_snapshot);
		if (result == FT_ERR_SUCCESS
			&& inventory_snapshot.revision <= inventory_revision)
			result = FT_ERR_INVALID_STATE;
		for (const game_inventory_item_snapshot &item : inventory_snapshot.items)
		{
			if (result == FT_ERR_SUCCESS
				&& item.item_id == MinecraftItemCatalog::SEEDS_ITEM_ID)
				result = FT_ERR_INVALID_STATE;
		}
		if (result == FT_ERR_SUCCESS)
		{
			int32_t reed_x = -1;
			int32_t reed_y = -1;
			int32_t reed_z = -1;
			uint32_t reed_support = GAME_VOXEL_AIR_BLOCK;
			uint32_t reed_target = GAME_VOXEL_AIR_BLOCK;
			World::RevisionProtectionInfo reed_protection = {};
			wheat_planting_stage = "paper-reed-find-dry-site";
			for (int32_t z = plant_chunk_z * GAME_VOXEL_CHUNK_DEPTH;
				z < (plant_chunk_z + 1) * GAME_VOXEL_CHUNK_DEPTH
					&& reed_x < 0; ++z)
			{
				for (int32_t x = plant_chunk_x * GAME_VOXEL_CHUNK_WIDTH;
				x < (plant_chunk_x + 1) * GAME_VOXEL_CHUNK_WIDTH - 1
						&& reed_x < 0; ++x)
				{
					for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 2; y > 0; --y)
					{
						if (!session->world_.block_id_at(x, y, z,
								&reed_support)
							|| (reed_support != VOXEL_GENERATOR_GRASS_BLOCK_ID
								&& reed_support != VOXEL_GENERATOR_DIRT_BLOCK_ID
								&& reed_support != VOXEL_GENERATOR_MUD_BLOCK_ID
								&& reed_support != VOXEL_GENERATOR_SAND_BLOCK_ID)
							|| !session->world_.block_id_at(x, y + 1, z,
								&reed_target)
							|| reed_target != GAME_VOXEL_AIR_BLOCK
							|| !session->world_.block_id_at(x + 1, y + 1, z,
								&reed_target)
							|| reed_target != GAME_VOXEL_AIR_BLOCK
							|| session->world_.has_water_source_near(x, y, z))
							continue;
						reed_x = x;
						reed_y = y;
						reed_z = z;
						break;
					}
				}
			}
			if (reed_x < 0)
				result = FT_ERR_NOT_FOUND;
			if (result == FT_ERR_SUCCESS)
				result = session->world_.get_revision_protection_info(
					plant_chunk_x, plant_chunk_z, &reed_protection);
			if (result == FT_ERR_SUCCESS
				&& (!reed_protection.generation_protected
					|| reed_protection.loom_safe_zone
					|| reed_protection.manually_protected
					|| reed_protection.occupied_player_exclusion))
				result = FT_ERR_PERMISSION_DENIED;
			if (result == FT_ERR_SUCCESS)
				result = session->item_catalog_.create_item(
					MinecraftItemCatalog::PAPER_REED_FIBER_ITEM_ID, 1,
					&paper_reed_fiber);
			if (result == FT_ERR_SUCCESS)
				result = session->player_character_.get_inventory().add_item(
					paper_reed_fiber);
			if (result == FT_ERR_SUCCESS)
				result = session->player_character_.get_inventory().get_snapshot(
					inventory_snapshot);
			const uint64_t reed_inventory_revision = inventory_snapshot.revision;
			if (result == FT_ERR_SUCCESS)
			{
				session->selected_block_id_ =
					VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID;
				session->camera_.x = static_cast<double>(reed_x) + 0.5;
				session->camera_.y = static_cast<double>(reed_y) + 5.0;
				session->camera_.z = static_cast<double>(reed_z) + 0.5;
				session->camera_.yaw = 0.0;
				session->camera_.pitch = -1.5707963267948966;
				wheat_planting_stage = "paper-reed-reject-dry-site";
				if (session->try_place_selected_inventory_block()
					!= FT_ERR_PERMISSION_DENIED)
					result = FT_ERR_INVALID_STATE;
			}
			if (result == FT_ERR_SUCCESS)
			{
				wheat_planting_stage = "paper-reed-add-test-water";
				WorldChunk *water_chunk = session->world_.find_chunk_mutable(
					plant_chunk_x, plant_chunk_z);
				if (water_chunk == nullptr)
					result = FT_ERR_NOT_FOUND;
				else
					result = session->world_.replace_fluid_source_at(
						reed_x + 1, reed_y + 1, reed_z,
						GAME_VOXEL_AIR_BLOCK,
						VOXEL_GENERATOR_WATER_BLOCK_ID,
						water_chunk->voxel_revision);
			}
			if (result == FT_ERR_SUCCESS)
			{
				wheat_planting_stage = "paper-reed-place-live";
				result = session->try_place_selected_inventory_block();
			}
			if (result == FT_ERR_SUCCESS
				&& (!session->world_.block_id_at(reed_x, reed_y + 1, reed_z,
					&reed_target)
					|| reed_target
						!= VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID))
				result = FT_ERR_INVALID_STATE;
			if (result == FT_ERR_SUCCESS)
				result = session->player_character_.get_inventory().get_snapshot(
					inventory_snapshot);
			if (result == FT_ERR_SUCCESS
				&& inventory_snapshot.revision <= reed_inventory_revision)
				result = FT_ERR_INVALID_STATE;
			for (const game_inventory_item_snapshot &item : inventory_snapshot.items)
			{
				if (result == FT_ERR_SUCCESS
					&& item.item_id
						== MinecraftItemCatalog::PAPER_REED_FIBER_ITEM_ID)
					result = FT_ERR_INVALID_STATE;
			}
		}
		if (result == FT_ERR_SUCCESS)
			wheat_planting_failed = false;
	}
	if (result == FT_ERR_SUCCESS)
	{
		bool opportunity_found = false;
		World::RevisionProtectionInfo protection = {};
		failure_stage = "prepare-stabilizer-opportunity-site";
		player_chunk_x = WorldCoordinates::floor_divide(
			session->player_character_.get_x(), GAME_VOXEL_CHUNK_WIDTH);
		player_chunk_z = WorldCoordinates::floor_divide(
			session->player_character_.get_z(), GAME_VOXEL_CHUNK_DEPTH);
		/* Fixture terrain is written through the generator path, which leaves
		 * the chunk eligible for regeneration; ordinary player edits do not. */
		for (int32_t radius = 0; radius <= 32 && !opportunity_found; ++radius)
		{
			for (int32_t z = -radius; z <= radius && !opportunity_found; ++z)
			{
				for (int32_t x = -radius; x <= radius
					&& !opportunity_found; ++x)
				{
					if (std::abs(x) != radius && std::abs(z) != radius)
						continue;
					/* Selection correctly rejects the player's chunk and its
					 * immediate ring; avoid choosing a fixture that only fails when
					 * the restored session republishes occupancy. */
					if (std::abs(x - player_chunk_x) <= 1
						&& std::abs(z - player_chunk_z) <= 1)
						continue;
					const WorldChunk *candidate =
						session->world_.find_chunk(x, z);
					if (candidate == nullptr || !candidate->initialized)
						continue;
					if (session->world_.get_revision_protection_info(x, z,
							&protection) != FT_ERR_SUCCESS)
						continue;
					if (protection.generation_protected || protection.loom_safe_zone
						|| protection.manually_protected
						|| protection.occupied_player_exclusion)
						continue;
					opportunity_found = true;
					stabilizer_chunk_x = x;
					stabilizer_chunk_z = z;
				}
			}
		}
		if (!opportunity_found)
			result = FT_ERR_NOT_FOUND;
		if (result == FT_ERR_SUCCESS)
		{
			WorldChunk *opportunity_chunk = session->world_.find_chunk_mutable(
				stabilizer_chunk_x, stabilizer_chunk_z);
			if (opportunity_chunk == nullptr || !opportunity_chunk->initialized)
				result = FT_ERR_NOT_FOUND;
			/* Set generated terrain rather than player overrides, preserving the
			 * chunk's unprotected state for selection and persistence validation. */
			for (int32_t z = 2; z <= 6 && result == FT_ERR_SUCCESS; ++z)
			{
				for (int32_t x = 2; x <= 6 && result == FT_ERR_SUCCESS; ++x)
				{
					result = opportunity_chunk->chunk.write_generated_block(x, 248,
						z, VOXEL_GENERATOR_STONE_BLOCK_ID);
					for (int32_t y = 249; y <= 254
						&& result == FT_ERR_SUCCESS; ++y)
						result = opportunity_chunk->chunk.write_generated_block(x, y,
							z, GAME_VOXEL_AIR_BLOCK);
				}
			}
			if (result == FT_ERR_SUCCESS
				&& (!world_revision_validator_chunk_has_stabilizer_site(
					session->world_, stabilizer_chunk_x, stabilizer_chunk_z)
					|| opportunity_chunk->chunk.is_generation_protected() == FT_TRUE))
				result = FT_ERR_INVALID_STATE;
		}
	}
	if (result == FT_ERR_SUCCESS)
	{
		failure_stage = "begin-fixture-revision";
		result = session->world_.begin_world_revision(
			session->world_.voxel_generation_settings(),
			session->regeneration_world_rules_, World::REGEN_FULL);
	}
	if (result == FT_ERR_SUCCESS)
	{
		failure_stage = "select-fixture-chunk";
		revision = session->world_.world_revision();
		selection_revision = revision.selection_revision;
		result = session->world_.update_revision_chunk_selection(
			selection_revision, WorldRevisionManager::HARD_MAX_SELECTED_CHUNKS,
			stabilizer_chunk_x, stabilizer_chunk_z, true, &selection_revision);
	}
	if (result == FT_ERR_SUCCESS)
	{
		failure_stage = "activate-fixture-barriers";
		result = session->world_.set_revision_barriers_active(true);
	}
	if (result == FT_ERR_SUCCESS)
	{
		failure_stage = "set-fixture-deadline";
		result = session->world_.set_revision_decision_deadline_epoch(
			clock_epoch_ms + 300000U);
	}
	if (result == FT_ERR_SUCCESS)
	{
		session->world_loom_start_committed_ = true;
		result = session->world_.get_chunk_instability(stabilizer_chunk_x,
			stabilizer_chunk_z, &record);
	}
	if (result == FT_ERR_SUCCESS)
		failure_stage = "put-stabilizers-in-hand";
	for (uint32_t ordinal = 0U; ordinal < 2U
		&& result == FT_ERR_SUCCESS; ++ordinal)
	{
		card_game_deck_card card = {};
		result = session->regeneration_cards_.engine_.deck_push_top(
			RegenerationCardSession::PLAYER_ID, STABILIZER_DEFINITION_ID);
		if (result == FT_ERR_SUCCESS)
			result = session->regeneration_cards_.engine_.draw_to_hand(
				RegenerationCardSession::PLAYER_ID, &card);
		if (result == FT_ERR_SUCCESS
			&& card.card_id != STABILIZER_DEFINITION_ID)
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS && ordinal == 0U)
			first_instance_id = card.instance_id;
		else if (result == FT_ERR_SUCCESS)
			second_instance_id = card.instance_id;
	}
	if (result == FT_ERR_SUCCESS)
		failure_stage = "first-success-outcome";
	choice.kind = REGENERATION_CARD_CHOICE_NONE;
	if (result == FT_ERR_SUCCESS)
		failure_stage = "first-success-retry";
	if (result == FT_ERR_SUCCESS)
		result = session->play_regeneration_card(FIRST_REQUEST_ID,
			first_instance_id, choice, &first_intent);
	if (result == FT_ERR_SUCCESS)
		result = session->world_.get_chunk_instability(stabilizer_chunk_x,
			stabilizer_chunk_z, &record);
	if (result == FT_ERR_SUCCESS && (record.current_value != 15U
		|| first_intent.private_result != 0U
		|| first_intent.private_ordinal != 0U))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = session->play_regeneration_card(FIRST_REQUEST_ID,
			first_instance_id, choice, &replayed_first_intent);
	if (result == FT_ERR_SUCCESS)
		result = session->world_.get_chunk_instability(stabilizer_chunk_x,
			stabilizer_chunk_z, &record);
	if (result == FT_ERR_SUCCESS && (record.current_value != 15U
		|| !replayed_first_intent.replayed
		|| replayed_first_intent.private_result != 0U))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		failure_stage = "second-overload-outcome";
	if (result == FT_ERR_SUCCESS)
		result = session->play_regeneration_card(SECOND_REQUEST_ID,
			second_instance_id, choice, &second_intent);
	if (result == FT_ERR_SUCCESS)
		result = session->world_.get_chunk_instability(stabilizer_chunk_x,
			stabilizer_chunk_z, &record);
	if (result == FT_ERR_SUCCESS && (record.current_value != 22U
		|| second_intent.private_result != 0U
		|| second_intent.private_ordinal != 0U))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS
		&& std::none_of(record.permanent_modifiers.begin(),
			record.permanent_modifiers.end(),
			[](const WorldChunkInstabilityRecord::VersionedId &modifier)
			{
				return (modifier.id
					== REGENERATION_MODIFIER_STABILIZER_OVERLOAD);
			}))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		failure_stage = "second-overload-use-count";
	if (result == FT_ERR_SUCCESS)
		result = session->regeneration_world_rules_.
			get_volatile_stabilizer_use_count(&stabilizer_uses);
	if (result == FT_ERR_SUCCESS && stabilizer_uses != 2U)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		failure_stage = "stop-first-session";
	if (session->active_)
		session->stop();
	if (result == FT_ERR_SUCCESS)
	{
		failure_stage = "configure-restored-instability-policy";
		result = restored->configure_chunk_instability_policy(instability_policy);
	}
	if (result == FT_ERR_SUCCESS)
	{
		failure_stage = "configure-restored-stabilizer-policy";
		result = restored->configure_volatile_stabilizer_policy(risk_curve, 2U,
			5, 7);
	}
	if (result == FT_ERR_SUCCESS)
	{
		failure_stage = "configure-restored-clock-provider";
		result = restored->configure_revision_clock_provider(
			&world_revision_validator_clock, &clock_epoch_ms);
	}
	if (result == FT_ERR_SUCCESS)
	{
		failure_stage = "start-restored-session";
		result = restored->start(seed, window, *renderer);
	}
	if (result == FT_ERR_SUCCESS)
	{
		std::vector<WorldItemSnapshot> restored_eggs;
		failure_stage = "restored-session-egg-query";
		result = restored->world_items_.query_nearby(40.5, 20.0, -5.5,
			2.0, 1U, 1000000U, &restored_eggs);
		if (result == FT_ERR_SUCCESS
			&& (restored_eggs.size() != 1U
				|| restored_eggs[0].item_id
					!= MinecraftItemCatalog::EGG_ITEM_ID
				|| restored_eggs[0].quantity != 1))
			result = FT_ERR_INVALID_STATE;
	}
	if (result == FT_ERR_SUCCESS)
		failure_stage = "restored-session-loom-state";
	if (result == FT_ERR_SUCCESS
		&& (!restored->world_loom_start_committed_
			|| !restored->world_.world_revision().pending))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		failure_stage = "restored-session-spawner-before-deadline";
	if (result == FT_ERR_SUCCESS)
	{
		bool state_changed = true;
		result = restored->world_spawners_.tick(999999U,
			restored->world_entities_, spawner_deaths, &state_changed);
		if (result == FT_ERR_SUCCESS && (state_changed || !spawner_deaths.empty()))
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
			failure_stage = "restored-session-spawner-at-deadline";
		if (result == FT_ERR_SUCCESS)
			result = restored->world_spawners_.tick(1000000U,
				restored->world_entities_, spawner_deaths, &state_changed);
		if (result == FT_ERR_SUCCESS && (!state_changed || spawner_id != 1U))
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
			failure_stage = "restored-session-entity-snapshot";
		if (result == FT_ERR_SUCCESS)
			result = restored->world_entities_.snapshot(session_entities);
		if (result == FT_ERR_SUCCESS && session_entities.size() != 2U)
			result = FT_ERR_INVALID_STATE;
		for (const WorldEntitySnapshot &entity : session_entities)
		{
			if (entity.state.type == spawner_definition.entity_type)
				spawner_entity_id = entity.state.id;
		}
		if (result == FT_ERR_SUCCESS && spawner_entity_id == 0U)
			result = FT_ERR_NOT_FOUND;
	}
	if (result == FT_ERR_SUCCESS)
		failure_stage = "restored-session-instability";
	if (result == FT_ERR_SUCCESS)
		result = restored->world_.get_chunk_instability(stabilizer_chunk_x,
			stabilizer_chunk_z, &record);
	if (result == FT_ERR_SUCCESS && record.current_value != 22U)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS
		&& std::none_of(record.permanent_modifiers.begin(),
			record.permanent_modifiers.end(),
			[](const WorldChunkInstabilityRecord::VersionedId &modifier)
			{
				return (modifier.id
					== REGENERATION_MODIFIER_STABILIZER_OVERLOAD);
			}))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		failure_stage = "restored-retry-action";
	}
	if (result == FT_ERR_SUCCESS)
		result = restored->play_regeneration_card(SECOND_REQUEST_ID,
			second_instance_id, choice, &restored_retry_intent);
	if (result == FT_ERR_SUCCESS)
		failure_stage = "restored-retry-check";
	if (result == FT_ERR_SUCCESS)
		result = restored->world_.get_chunk_instability(stabilizer_chunk_x,
			stabilizer_chunk_z, &record);
	if (result == FT_ERR_SUCCESS && (record.current_value != 22U
		|| !restored_retry_intent.replayed))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		failure_stage = "spawner-death-loot-checkpoint";
	if (result == FT_ERR_SUCCESS)
	{
		WorldEntitySnapshot corpse = {};
		std::vector<WorldSpawnerDeath> deaths;
		std::vector<WorldItemSnapshot> loot_items;
		std::vector<MinecraftLootDrop> expected_drops;
		char original_save_path[sizeof(restored->world_item_save_path_)] = {};
		bool state_changed = false;
		failure_stage = "spawner-death-get-corpse";
		result = restored->world_entities_.get(spawner_entity_id,
			&corpse);
		if (result == FT_ERR_SUCCESS)
		{
			failure_stage = "spawner-death-mark-dead";
			corpse.state.health = 0;
			result = restored->world_entities_.update(corpse.state.id,
				corpse.revision, corpse.state, &corpse.revision);
		}
		failure_stage = "spawner-death-emit-event";
		if (result == FT_ERR_SUCCESS)
			result = restored->world_spawners_.tick(1000001U,
				restored->world_entities_, deaths, &state_changed);
		if (result == FT_ERR_SUCCESS
			&& (deaths.size() != 1U || !state_changed
				|| deaths[0].loot_table_id != MINECRAFT_LOOT_SKELETON))
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
			result = MinecraftLootCatalog::resolve(deaths[0].loot_table_id,
				deaths[0].entity_id, expected_drops);
		if (result == FT_ERR_SUCCESS)
		{
			std::memcpy(original_save_path,
				restored->world_item_save_path_, sizeof(original_save_path));
			std::strncpy(restored->world_item_save_path_,
				"saves/world_items/missing-parent/test.wisp",
				sizeof(restored->world_item_save_path_) - 1U);
			restored->world_item_save_path_[
				sizeof(restored->world_item_save_path_) - 1U] = '\0';
			failure_stage = "spawner-death-injected-save-failure";
			if (restored->process_spawner_deaths(deaths)
				== FT_ERR_SUCCESS)
				result = FT_ERR_INVALID_STATE;
			std::memcpy(restored->world_item_save_path_, original_save_path,
				sizeof(original_save_path));
		}
		if (result == FT_ERR_SUCCESS)
		{
			failure_stage = "spawner-death-rollback-items";
			result = restored->world_items_.query_nearby(10.5, 20.0, -5.5,
				2.0, 1U, 1000001U, &loot_items);
			if (result == FT_ERR_SUCCESS && !loot_items.empty())
				result = FT_ERR_INVALID_STATE;
		}
		if (result == FT_ERR_SUCCESS
			&& restored->world_entities_.get(corpse.state.id, &corpse)
			!= FT_ERR_SUCCESS)
		result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
		{
			failure_stage = "spawner-death-event-retry";
			deaths.clear();
			result = restored->world_spawners_.tick(1000001U,
				restored->world_entities_, deaths, &state_changed);
			if (result == FT_ERR_SUCCESS
				&& (deaths.size() != 1U
					|| deaths[0].entity_id != corpse.state.id))
				result = FT_ERR_INVALID_STATE;
		}
		if (result == FT_ERR_SUCCESS)
			failure_stage = "spawner-death-commit";
		if (result == FT_ERR_SUCCESS)
			result = restored->process_spawner_deaths(deaths);
		failure_stage = "spawner-death-query-items";
		if (result == FT_ERR_SUCCESS)
			result = restored->world_items_.query_nearby(10.5, 20.0, -5.5,
				2.0, 1U, 1000001U, &loot_items);
		failure_stage = "spawner-death-check-items";
		failure_stage = "spawner-death-check-corpse";
		if (result == FT_ERR_SUCCESS
			&& loot_items.size() != expected_drops.size())
			result = FT_ERR_INVALID_STATE;
		for (std::size_t index = 0U; result == FT_ERR_SUCCESS
			&& index < expected_drops.size(); ++index)
		{
			if (loot_items[index].item_id != expected_drops[index].item_id
				|| loot_items[index].quantity != expected_drops[index].quantity)
				result = FT_ERR_INVALID_STATE;
		}
		if (result == FT_ERR_SUCCESS
			&& restored->world_entities_.get(corpse.state.id, &corpse)
				!= FT_ERR_NOT_FOUND)
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
		{
			WorldSpawnerDefinition cow_spawner = {};
			WorldEntitySnapshot cow = {};
			std::vector<WorldSpawnerDeath> cow_deaths;
			std::vector<MinecraftLootDrop> cow_expected_drops;
			std::vector<WorldItemSnapshot> cow_loot_items;
			uint32_t cow_spawner_id = 0U;
			bool cow_spawn_changed = false;
			cow_spawner.x = 20.5;
			cow_spawner.y = 20.0;
			cow_spawner.z = -5.5;
			cow_spawner.entity_type = static_cast<uint32_t>(
				EntityState::Kind::COW);
			cow_spawner.maximum_alive = 1U;
			cow_spawner.spawn_interval_ticks = 100U;
			cow_spawner.loot_table_id = MINECRAFT_LOOT_COW;
			failure_stage = "livestock-spawner-register";
			result = restored->world_spawners_.add(cow_spawner, 1000003U,
				&cow_spawner_id);
			if (result == FT_ERR_SUCCESS)
				result = restored->world_spawners_.tick(1000003U,
					restored->world_entities_, cow_deaths, &cow_spawn_changed);
			if (result == FT_ERR_SUCCESS
				&& (!cow_spawn_changed || !cow_deaths.empty()))
				result = FT_ERR_INVALID_STATE;
			std::vector<WorldEntitySnapshot> livestock;
			if (result == FT_ERR_SUCCESS)
				result = restored->world_entities_.snapshot(livestock);
			for (const WorldEntitySnapshot &candidate : livestock)
			{
				if (candidate.state.type
					== static_cast<uint32_t>(EntityState::Kind::COW))
				{
					cow = candidate;
					break ;
				}
			}
			if (result == FT_ERR_SUCCESS && cow.state.id == 0U)
				result = FT_ERR_NOT_FOUND;
			if (result == FT_ERR_SUCCESS)
			{
				cow.state.health = 0;
				result = restored->world_entities_.update(cow.state.id,
					cow.revision, cow.state, &cow.revision);
			}
			if (result == FT_ERR_SUCCESS)
				result = restored->world_spawners_.tick(1000004U,
					restored->world_entities_, cow_deaths, &cow_spawn_changed);
			if (result == FT_ERR_SUCCESS
				&& (cow_deaths.size() != 1U
					|| cow_deaths[0].spawner_id != cow_spawner_id
					|| cow_deaths[0].entity_id != cow.state.id
				|| cow_deaths[0].loot_table_id != MINECRAFT_LOOT_COW))
				result = FT_ERR_INVALID_STATE;
			if (result == FT_ERR_SUCCESS)
				result = MinecraftLootCatalog::resolve(MINECRAFT_LOOT_COW,
					cow.state.id, cow_expected_drops);
			if (result == FT_ERR_SUCCESS)
				result = restored->process_spawner_deaths(cow_deaths);
			if (result == FT_ERR_SUCCESS)
				result = restored->world_items_.query_nearby(20.5, 20.0,
					-5.5, 2.0, 2U, 1000004U, &cow_loot_items);
			if (result == FT_ERR_SUCCESS
				&& cow_loot_items.size() != cow_expected_drops.size())
				result = FT_ERR_INVALID_STATE;
			for (std::size_t index = 0U; result == FT_ERR_SUCCESS
				&& index < cow_expected_drops.size(); ++index)
			{
				if (cow_loot_items[index].item_id
						!= cow_expected_drops[index].item_id
					|| cow_loot_items[index].quantity
						!= cow_expected_drops[index].quantity)
					result = FT_ERR_INVALID_STATE;
			}
		}
		failure_stage = "spawner-death-check-replay";
		if (result == FT_ERR_SUCCESS)
		{
			deaths.clear();
			result = restored->world_spawners_.tick(1000002U,
				restored->world_entities_, deaths, &state_changed);
			if (result == FT_ERR_SUCCESS && !deaths.empty())
				result = FT_ERR_INVALID_STATE;
		}
		if (result == FT_ERR_SUCCESS)
		{
			failure_stage = "spawner-death-restart";
			restored->stop();
			result = restored->configure_chunk_instability_policy(
				instability_policy);
			failure_stage = "spawner-death-reconfigure-stabilizer";
			if (result == FT_ERR_SUCCESS)
				result = restored->configure_volatile_stabilizer_policy(
					risk_curve, 2U, 5, 7);
			if (result == FT_ERR_SUCCESS)
				result = restored->configure_revision_clock_provider(
					&world_revision_validator_clock, &clock_epoch_ms);
			failure_stage = "spawner-death-reload-session";
			if (result == FT_ERR_SUCCESS)
				result = restored->start(seed, window, *renderer);
			failure_stage = "spawner-death-query-restored-items";
			if (result == FT_ERR_SUCCESS)
				result = restored->world_items_.query_nearby(10.5, 20.0,
					-5.5, 2.0, 1U, 1000002U, &loot_items);
			if (result == FT_ERR_SUCCESS
				&& loot_items.size() != expected_drops.size())
				result = FT_ERR_INVALID_STATE;
			for (std::size_t index = 0U; result == FT_ERR_SUCCESS
				&& index < expected_drops.size(); ++index)
			{
				if (loot_items[index].item_id != expected_drops[index].item_id
					|| loot_items[index].quantity != expected_drops[index].quantity)
					result = FT_ERR_INVALID_STATE;
			}
		}
	}
	if (result != FT_ERR_SUCCESS && wheat_planting_failed)
		failure_stage = wheat_planting_stage;
	if (result != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"world-revision: stabilizer stage=%s code=%d value=%u use-count=%u\n",
			failure_stage, result, record.current_value, stabilizer_uses);
	if (restored->active_)
		restored->stop();
	if (restored->world_item_save_path_[0] != '\0')
		(void)std::remove(restored->world_item_save_path_);
	if (restored->world_revision_save_path_[0] != '\0')
		(void)std::remove(restored->world_revision_save_path_);
	if (session->world_item_save_path_[0] != '\0')
		(void)std::remove(session->world_item_save_path_);
	if (session->world_revision_save_path_[0] != '\0')
		(void)std::remove(session->world_revision_save_path_);
	if (session->active_)
		session->stop();
	if (session->world_item_save_path_[0] != '\0')
		(void)std::remove(session->world_item_save_path_);
	if (session->world_revision_save_path_[0] != '\0')
		(void)std::remove(session->world_revision_save_path_);
	return (result);
}

int WorldRevisionValidator::validate() const
{
	std::unique_ptr<World> world(new (std::nothrow) World());
	std::vector<World::RevisionPreviewEntry> preview;
	int32_t regenerated;
	int32_t skipped;

	if (world_revision_validator_test_metadata_v2_v3() != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: v2/v3 metadata migration failed\n");
		return (1);
	}
	if (world_revision_validator_test_card_preflight() != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: card preflight contract failed\n");
		return (1);
	}
	if (world_revision_validator_test_volcano_anchor_selection()
		!= FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: volcano anchor ordering/reservation failed\n");
		return (1);
	}
	if (world_revision_validator_test_feature_chunk_reservation()
		!= FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: village/magma chunk reservation was not distinct\n");
		return (1);
	}
	if (WorldRevisionValidator::test_game_session_volatile_stabilizer()
		!= FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: GameSession Volatile Stabilizer retry/restore failed\n");
		return (1);
	}
	if (WorldRevisionValidator::test_game_session_committed_start_retry()
		!= FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: committed-start retry reopened a stale decision window\n");
		return (1);
	}
	if (world_revision_validator_test_feature_preview_mask()
		!= FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: feature preview mask copy/clear failed\n");
		return (1);
	}
	if (world_revision_validator_test_chunk_instability()
		!= FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: chunk instability persistence failed\n");
		return (1);
	}
	if (WorldRevisionValidator::test_atomic_generation_batch_capacity()
		!= FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "world-revision: batch capacity was not atomic\n");
		return (1);
	}
	if (WorldRevisionValidator::test_magma_feature_generation()
		!= FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: magma feature generation was not contained\n");
		return (1);
	}
	if (WorldRevisionValidator::test_village_feature_generation()
		!= FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: village feature generation was incomplete\n");
		return (1);
	}
	if (WorldRevisionValidator::test_ruined_village_feature_generation()
		!= FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: ruined village feature generation was incomplete\n");
		return (1);
	}
	if (WorldRevisionValidator::test_shipwreck_feature_generation()
		!= FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: shipwreck shape/navigation failed\n");
		return (1);
	}
	if (WorldRevisionValidator::test_volcano_feature_generation()
		!= FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: volcano feature generation was incomplete\n");
		return (1);
	}
	if (WorldRevisionValidator::test_volcano_configured_footprint_generation()
		!= FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: configured volcano footprint was incomplete\n");
		return (1);
	}
	if (world == nullptr
		|| WorldRevisionValidator::initialize_world_with_edit(*world)
			!= FT_ERR_SUCCESS)
		return (1);
	if (world_revision_validator_test_movement_barrier(*world)
		!= FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: movement barrier did not protect reserved chunks\n");
		return (1);
	}
	if (world_revision_validator_test_barrier_restore() != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: committed barrier did not restore from metadata\n");
		return (1);
	}
	if (world_revision_validator_test_revision_id_exhaustion(*world)
		!= FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: exhausted revision ID was not rejected atomically\n");
		return (1);
	}
	const int32_t server_authorization_result =
		WorldRevisionValidator::test_server_draft_authorization(*world);
	if (WorldRevisionValidator::fail_if_error(*world,
			server_authorization_result))
	{
		std::fprintf(stderr,
			"world-revision: server draft authorization failed code=%d\n",
			server_authorization_result);
		return (1);
	}
	if (WorldRevisionValidator::fail_if_error(*world,
			world_revision_validator_test_occupied_exclusion(*world)))
	{
		std::fprintf(stderr,
			"world-revision: occupied-player exclusion was not enforced\n");
		return (1);
	}
	if (WorldRevisionValidator::fail_if_error(*world,
			WorldRevisionValidator::setup_revision_selection(*world, preview)))
		return (1);
	if (world->set_revision_barriers_active(true) != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"world-revision: selected chunks could not activate barriers\n");
		return (1);
	}
	if (WorldRevisionValidator::fail_if_error(*world,
			WorldRevisionValidator::regenerate_and_check(*world, &regenerated,
				&skipped)))
		return (1);
	if (world->revision_barriers_active())
	{
		std::fprintf(stderr,
			"world-revision: successful regeneration retained barriers\n");
		return (1);
	}
	voxel_generation_config original_config;
	if (voxel_default_generation_config(original_config) != FT_ERR_SUCCESS)
		return (1);
	const voxel_generation_config &committed_config =
		world->voxel_generation_settings();
	uint32_t ore_index = 0U;
	uint32_t compass_biome_samples = 0U;
	uint32_t biome_sample_count = 0U;
	int32_t biome_zone_width = voxel_get_biome_zone_width(
		committed_config, 0xC0FFEEU);
	while (ore_index < original_config.ore_rule_count
		&& original_config.ores[ore_index].enabled != FT_TRUE)
		++ore_index;
	if (ore_index >= original_config.ore_rule_count
		|| ore_index >= committed_config.ore_rule_count
		|| committed_config.ores[ore_index].veins_per_chunk_min
			!= (original_config.ores[ore_index].veins_per_chunk_min * 9U + 3U) / 4U
		|| committed_config.ores[ore_index].veins_per_chunk_max
			!= (original_config.ores[ore_index].veins_per_chunk_max * 9U + 3U) / 4U)
	{
		std::fprintf(stderr,
			"world-revision: committed ore mismatch index=%u original=%u..%u committed=%u..%u\n",
			ore_index, original_config.ores[ore_index].veins_per_chunk_min,
			original_config.ores[ore_index].veins_per_chunk_max,
			committed_config.ores[ore_index].veins_per_chunk_min,
			committed_config.ores[ore_index].veins_per_chunk_max);
		world->destroy();
		return (1);
	}
	if (biome_zone_width <= 0)
		biome_zone_width = 1;
	for (int32_t zone_z = 0; zone_z < 20; ++zone_z)
	{
		for (int32_t zone_x = 0; zone_x < 20; ++zone_x)
		{
			const int32_t world_x = zone_x * biome_zone_width
				+ biome_zone_width / 2;
			const int32_t world_z = zone_z * biome_zone_width
				+ biome_zone_width / 2;
			const uint32_t selected_biome = voxel_select_biome(
				committed_config, 0xC0FFEEU, world_x, world_z);

			if (selected_biome == 0U
				|| voxel_select_biome(committed_config, 0xC0FFEEU, world_x,
					world_z) != selected_biome)
			{
				std::fprintf(stderr,
					"world-revision: committed biome selection invalid zone=%d,%d selected=%u\n",
					zone_x, zone_z, selected_biome);
				world->destroy();
				return (1);
			}
			if (selected_biome == 1U)
				++compass_biome_samples;
			++biome_sample_count;
		}
	}
	if (biome_sample_count == 0U
		|| compass_biome_samples * 100U < biome_sample_count * 30U)
	{
		std::fprintf(stderr,
			"world-revision: weighted biome share too low compass=%u total=%u\n",
			compass_biome_samples, biome_sample_count);
		world->destroy();
		return (1);
	}
	if (WorldRevisionValidator::fail_if_error(*world,
			WorldRevisionValidator::roundtrip_metadata(*world)))
		return (1);
	if (WorldRevisionValidator::fail_if_error(*world,
			WorldRevisionValidator::apply_request_test(*world)))
		return (1);
	std::printf("world-revision: ok regenerated=%d skipped=%d\n", regenerated,
		skipped);
	world->destroy();
	return (0);
}
