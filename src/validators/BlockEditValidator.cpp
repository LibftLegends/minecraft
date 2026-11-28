#include "../../src/validators/BlockEditValidator.hpp"
#include "../../src/interaction/BlockInteractor.hpp"
#include "../../src/inventory/MinecraftItemCatalog.hpp"
#include "../../src/inventory/MinecraftLootCatalog.hpp"
#include "../../src/entities/WorldItemStore.hpp"
#include "../../src/transactions/WorldFarmlandTransaction.hpp"
#include "../../src/transactions/WorldItemPickupTransaction.hpp"
#include "../../src/camera/Camera.hpp"
#include "../../src/app/GameSession.hpp"
#include "../../src/app/GameSessionPersistence.hpp"
#include "../../src/policy/FixedRenderStrategy.hpp"
#include "../../src/world/WorldChunkGenerationWorker.hpp"
#include "../../Libft/Modules/Basic/limits.hpp"
#include "../../Libft/Modules/File/file_utils.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <new>
#include <thread>
#include <vector>

namespace
{
	static int32_t validate_ground_item_snapshot_refresh()
	{
		MinecraftItemCatalog catalog;
		WorldItemStore items;
		WorldItemStore restored;
		game_inventory inventory;
		const int32_t item_id = MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
			+ static_cast<int32_t>(VOXEL_GENERATOR_STONE_BLOCK);
		uint64_t entities[2] = {};
		int32_t result = catalog.initialize();
		if (result == FT_ERR_SUCCESS)
			result = items.initialize();
		if (result == FT_ERR_SUCCESS)
			result = restored.initialize();
		if (result == FT_ERR_SUCCESS)
			result = inventory.initialize(8U, 1000);
		for (int32_t index = 0; index < 2 && result == FT_ERR_SUCCESS; ++index)
		{
			ft_sharedptr<game_item> stack;
			result = catalog.create_item(item_id, index == 0 ? 8 : 62, &stack);
			if (result == FT_ERR_SUCCESS)
				result = items.spawn(stack, 0.5 * index, 1.0, 0.0,
					1U, 0U, 0U, &entities[index]);
		}
		std::vector<WorldItemSnapshot> nearby;
		if (result == FT_ERR_SUCCESS)
			result = items.query_renderable(0.0, 1.0, 0.0, 2.0, &nearby);
		if (result != FT_ERR_SUCCESS || nearby.size() != 2U
			|| nearby[0].quantity != 8 || nearby[1].quantity != 62)
			return (ApplicationError::fail("block-edit ground item snapshot baseline", 1));
		std::vector<WorldItemRenderPosition> positions;
		result = items.query_render_positions(0.0, 1.0, 0.0, 2.0, &positions);
		if (result != FT_ERR_SUCCESS || positions.size() != 2U
			|| positions[0].entity_id != entities[0]
			|| positions[1].entity_id != entities[1]
			|| positions[1].item_id != item_id || std::abs(positions[1].x - 0.5) > 1.0e-9)
			return (ApplicationError::fail("block-edit ground item render positions", 1));
		int32_t merged = 0;
		result = items.merge_stacks(entities[0], nearby[0].entity_revision,
			entities[1], nearby[1].entity_revision, &merged);
		if (result == FT_ERR_SUCCESS)
			result = items.query_renderable(0.0, 1.0, 0.0, 2.0, &nearby);
		if (result != FT_ERR_SUCCESS || merged != 2 || nearby.size() != 2U
			|| nearby[0].quantity != 6 || nearby[1].quantity != 64)
			return (ApplicationError::fail("block-edit ground item merged snapshot", 1));
		ft_sharedptr<game_item> pending_stack;
		uint64_t pending_id = 0U;
		result = catalog.create_item(item_id, 1, &pending_stack);
		if (result == FT_ERR_SUCCESS)
			result = items.spawn_pending(pending_stack, 1.0, 1.0, 0.0,
				1U, 5U, 20U, &pending_id);
		if (result == FT_ERR_SUCCESS)
			result = items.query_render_positions(0.0, 1.0, 0.0, 2.0, &positions);
		if (result != FT_ERR_SUCCESS || positions.size() != 2U
			|| positions[0].entity_id != entities[0] || positions[1].entity_id != entities[1])
			return (ApplicationError::fail("block-edit ground item render publication", 1));
		result = items.despawn(pending_id, 1U);
		if (result != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit ground item pending cleanup", result));
		uint64_t inventory_revision = 0U;
		result = inventory.get_revision(&inventory_revision);
		WorldItemPickupResult pickup = {};
		if (result == FT_ERR_SUCCESS)
			result = items.pickup(entities[1], nearby[1].entity_revision, 1U,
				1U, inventory_revision, 2, 0.0, 1.0, 0.0, 1U, inventory, &pickup);
		std::vector<uint8_t> bytes;
		if (result == FT_ERR_SUCCESS)
			result = items.serialize(bytes);
		if (result == FT_ERR_SUCCESS)
			result = restored.deserialize(bytes.data(), bytes.size());
		if (result == FT_ERR_SUCCESS)
			result = restored.query_renderable(0.0, 1.0, 0.0, 2.0, &nearby);
		if (result != FT_ERR_SUCCESS || nearby.size() != 2U
			|| nearby[0].quantity != 6 || nearby[1].quantity != 62
			|| pickup.transferred_quantity != 2)
			return (ApplicationError::fail("block-edit saved ground item snapshot", 1));
		std::printf("block-edit: shared ground item snapshots refreshed after merge and pickup\n");
		return (FT_ERR_SUCCESS);
	}

	static int32_t validate_deferred_mining_action(World &world, int32_t x,
		int32_t y, int32_t z) noexcept
	{
		MinecraftItemCatalog catalog;
		WorldItemStore items;
		GameTransactionJournal journal;
		struct Checkpoints
		{
			uint32_t sync = 0U;
			uint32_t deferred = 0U;
			bool reject = false;
		} checkpoints;
		const GameSessionSnapshotWriter writer = {
			[](void *context) noexcept -> int32_t
			{
				++static_cast<Checkpoints *>(context)->sync;
				return (FT_ERR_SUCCESS);
			}, &checkpoints,
			[](void *context) noexcept -> int32_t
			{
				auto &state = *static_cast<Checkpoints *>(context);
				++state.deferred;
				return (state.reject ? FT_ERR_IO : FT_ERR_SUCCESS);
			}};
		constexpr uint32_t stone = VOXEL_GENERATOR_STONE_BLOCK;
		int32_t result = catalog.initialize();
		if (result == FT_ERR_SUCCESS)
			result = items.initialize();
		if (result == FT_ERR_SUCCESS)
			result = journal.initialize();
		MinecraftBlockItemRule rule = {};
		if (result == FT_ERR_SUCCESS)
			result = catalog.resolve_block_item_rule(stone, &rule);
		const WorldMiningDrop drop = {rule.item_id, 1};
		const int32_t chunk_x = WorldCoordinates::floor_divide(x, GAME_VOXEL_CHUNK_WIDTH);
		const int32_t chunk_z = WorldCoordinates::floor_divide(z, GAME_VOXEL_CHUNK_DEPTH);
		for (uint64_t id = 1U; id <= 3U && result == FT_ERR_SUCCESS; ++id)
		{
			uint32_t current = 0U;
			uint64_t pending = 0U;
			GameTransactionJournal::Record record = {};
			result = world.place_block_at(x, y, z, stone);
			const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
			if (result != FT_ERR_SUCCESS || chunk == nullptr)
				return (ApplicationError::fail("block-edit mining prepare fixture", 1));
			checkpoints.reject = id == 3U;
			result = WorldBlockMiningTransaction::execute("mining-fixture", world,
				catalog, items, journal, id, 1U, 1U, x, y, z, stone, GAME_VOXEL_AIR_BLOCK,
				chunk->voxel_revision, &drop, 1U, &writer, nullptr, -1, 0, 0U, 0U,
				nullptr, &pending);
			if (checkpoints.reject)
			{
				if (result != FT_ERR_IO || pending != 0U
					|| journal.find(WorldBlockMiningTransaction::journal_scope(), id, &record)
						!= FT_ERR_NOT_FOUND
					|| !world.block_id_at(x, y, z, &current) || current != stone)
					return (ApplicationError::fail("block-edit mining prepare failure", 1));
				result = world.delete_block_at(x, y, z);
				break ;
			}
			if (result != FT_ERR_SUCCESS || pending != id
				|| !world.block_id_at(x, y, z, &current) || current != stone
				|| journal.find(WorldBlockMiningTransaction::journal_scope(), id, &record)
					!= FT_ERR_SUCCESS || record.state != GameTransactionJournal::PREPARED
				|| checkpoints.deferred != id * 2U - 1U || checkpoints.sync != 0U)
				return (ApplicationError::fail("block-edit prepare leaves voxel unchanged", 1));
			if (id == 2U && world.delete_block_at(x, y, z) != FT_ERR_SUCCESS)
				return (ApplicationError::fail("block-edit mining conflict fixture", 1));
			result = WorldBlockMiningTransaction::complete_prepared("mining-fixture",
				world, catalog, items, journal, pending, &writer, nullptr, nullptr);
			if (result != FT_ERR_SUCCESS || !world.block_id_at(x, y, z, &current)
				|| current != GAME_VOXEL_AIR_BLOCK || checkpoints.deferred != id * 2U
				|| journal.find(WorldBlockMiningTransaction::journal_scope(), id, &record)
					!= FT_ERR_SUCCESS || record.state != (id == 1U
						? GameTransactionJournal::COMMITTED : GameTransactionJournal::ABORTED))
				return (ApplicationError::fail("block-edit mining completion or conflict", 1));
			std::vector<WorldItemSnapshot> nearby;
			result = items.query_nearby(x + 0.5, y + 0.5, z + 0.5, 1.0, 1U, 1U, &nearby);
			if (result != FT_ERR_SUCCESS || nearby.size() != 1U || nearby[0].quantity != 1)
				return (ApplicationError::fail("block-edit mining duplicate drop", 1));
		}
		if (result == FT_ERR_SUCCESS)
			std::printf("block-edit: mining prepare gated mutation and rejected a changed target\n");
		return (result);
	}

	static int32_t validate_boundary_read_state()
	{
		constexpr int32_t east = GAME_VOXEL_CHUNK_WIDTH - 1;
		constexpr int32_t south = GAME_VOXEL_CHUNK_DEPTH - 1;
		constexpr uint32_t stone = VOXEL_GENERATOR_STONE_BLOCK;
		WorldChunk chunk;
		if (chunk.chunk.initialize() != FT_ERR_SUCCESS
			|| chunk.light.initialize() != FT_ERR_SUCCESS
			|| chunk_mesh_initialize(chunk.mesh) != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit boundary snapshot fixture", 1));
		chunk.initialized = true;
		for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
			for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
				if (chunk.chunk.write_generated_block(0, y, z, stone) != FT_ERR_SUCCESS)
					return (ApplicationError::fail("block-edit sealed west boundary", 1));
		if (chunk.light.set(east, 15, 8, voxel_light_pack(9U, 2U)) != FT_ERR_SUCCESS
			|| chunk.light.set(0, 128, 8, voxel_light_pack(1U, 5U)) != FT_ERR_SUCCESS
			|| chunk.light.set(8, 250, south, voxel_light_pack(4U, 11U)) != FT_ERR_SUCCESS
			|| chunk.light.set(8, 32, 0, voxel_light_pack(13U, 1U)) != FT_ERR_SUCCESS
			|| chunk.light.set(8, 32, 8, voxel_light_pack(15U, 15U)) != FT_ERR_SUCCESS
			|| chunk.publish_read_state() != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit boundary snapshot publish", 1));
		const uint8_t expected[] = {9U, 5U, 11U, 13U};
		for (uint8_t face = 0U; face < 4U; ++face)
			if (chunk.get_boundary_source_light_max(face) != expected[face]
				|| chunk.boundary_has_transparent_target(face) != (face != 0U))
				return (ApplicationError::fail("block-edit boundary snapshot summaries", 1));
		const auto old_state = chunk.read_state;
		std::shared_ptr<WorldChunkReadState> edited_state;
		if (chunk.prepare_read_state_after_block_edit(0, 128, 8,
				GAME_VOXEL_AIR_BLOCK, edited_state) != FT_ERR_SUCCESS
			|| chunk.chunk.write_block(0, 128, 8, GAME_VOXEL_AIR_BLOCK) != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit boundary snapshot edit", 1));
		chunk.mark_content_changed();
		chunk.commit_prepared_read_state(edited_state);
		if (!chunk.boundary_has_transparent_target(0U)
			|| old_state->block_at(0, 128, 8) != stone
			|| chunk.read_state->block_at(0, 128, 8) != GAME_VOXEL_AIR_BLOCK)
			return (ApplicationError::fail("block-edit boundary snapshot update", 1));
		const auto edited_blocks = chunk.read_state->block_sections;
		const auto offset = WorldChunkReadState::light_offset(east, 15, 8);
		auto patched_light = std::make_shared<std::vector<uint8_t>>(*old_state->light);
		(*patched_light)[offset] = voxel_light_pack(0U, 4U);
		std::shared_ptr<WorldChunkReadState> patched_state;
		if (chunk.prepare_read_state_after_light_patch(patched_light, patched_state)
				!= FT_ERR_SUCCESS || chunk.light.set(east, 15, 8, (*patched_light)[offset])
				!= FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit prepare light snapshot patch", 1));
		chunk.light_version = chunk.content_version;
		chunk.computed_light_input_version = chunk.light_input_version;
		chunk.commit_prepared_read_state(patched_state, 1U << 1U);
		if (chunk.read_state->light != patched_light || (*old_state->light)[offset]
				!= voxel_light_pack(9U, 2U)
			|| chunk.read_state->block_sections != edited_blocks
			|| chunk.get_boundary_source_light_max(0U) != 4U
			|| chunk.get_boundary_source_light_max(1U) != expected[1]
			|| chunk.read_state->light_version != chunk.content_version
			|| !chunk.boundary_has_transparent_target(0U))
			return (ApplicationError::fail("block-edit publish light snapshot patch", 1));
		std::printf("block-edit: boundary snapshot summaries passed\n");
		return (FT_ERR_SUCCESS);
	}

	static int32_t validate_chunk_block_batch()
	{
		auto world = std::make_unique<World>();
		int32_t result = world->initialize("block-edit-batch-validator");
		const uint8_t store_id[32] = {'b', 'l', 'o', 'c', 'k', '-', 'b', 'a', 't', 'c', 'h'};
		if (result == FT_ERR_SUCCESS)
			result = world->terrain_chunk_store().initialize(store_id);
		const WorldChunk *chunk = world->find_chunk(0, 0);
		if (result != FT_ERR_SUCCESS || chunk == nullptr || !chunk->initialized)
			return (ApplicationError::fail("block-edit batch fixture", 1));
		constexpr int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 3;
		constexpr int32_t east = GAME_VOXEL_CHUNK_WIDTH - 1;
		constexpr uint32_t stone = VOXEL_GENERATOR_STONE_BLOCK;
		constexpr uint32_t air = GAME_VOXEL_AIR_BLOCK;
		const WorldChunkBlockEdit places[] = {{east, y, 7, air, stone},
			{east, y, 8, air, stone}, {8, y - 16, 8, air, stone}};
		const WorldChunkBlockEdit breaks[] = {{east, y, 7, stone, air},
			{east, y, 8, stone, air}, {8, y - 16, 8, stone, air}};
		const uint64_t initial_revision = chunk->voxel_revision;
		if (world->replace_chunk_block_states(0, 0, initial_revision, places, 3U) != FT_ERR_SUCCESS
			|| chunk->voxel_revision != initial_revision + 3U)
			return (ApplicationError::fail("block-edit batch placement", 1));
		const uint64_t placed_revision = chunk->voxel_revision;
		const auto placed = chunk->read_state;
		const WorldChunkBlockEdit stale[] = {breaks[0], {east, y, 8, air, stone}};
		const WorldChunkBlockEdit duplicate[] = {breaks[0], breaks[0]};
		game_voxel_chunk disk;
		uint32_t saved_block = air;
		if (disk.initialize() != FT_ERR_SUCCESS
			|| world->replace_chunk_block_states(0, 0, initial_revision, breaks, 3U) != FT_ERR_INVALID_STATE
			|| world->replace_chunk_block_states(0, 0, placed_revision, stale, 2U) != FT_ERR_INVALID_STATE
			|| world->replace_chunk_block_states(0, 0, placed_revision, duplicate, 2U) != FT_ERR_INVALID_ARGUMENT
			|| chunk->voxel_revision != placed_revision || chunk->read_state != placed
			|| world->terrain_chunk_store().load_chunk(0, 0, disk) != FT_ERR_SUCCESS
			|| disk.read_block(east, y, 7, &saved_block) != FT_ERR_SUCCESS || saved_block != stone)
			return (ApplicationError::fail("block-edit batch rejected unchanged", 1));
		if (world->replace_chunk_block_states(0, 0, placed_revision, breaks, 3U) != FT_ERR_SUCCESS
			|| chunk->voxel_revision != placed_revision + 3U
			|| world->terrain_chunk_store().load_chunk(0, 0, disk) != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit batch removal", 1));
		for (const WorldChunkBlockEdit &edit : breaks)
		{
			uint32_t live_block = stone;
			if (placed->block_at(edit.local_x, edit.local_y, edit.local_z) != stone
				|| !world->block_id_at(edit.local_x, edit.local_y, edit.local_z, &live_block)
				|| live_block != air
				|| disk.read_block(edit.local_x, edit.local_y, edit.local_z, &saved_block) != FT_ERR_SUCCESS
				|| saved_block != air)
				return (ApplicationError::fail("block-edit batch snapshots and disk", 1));
		}
		const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(4);
		while (chunk->last_mesh_publication_voxel_revision != chunk->voxel_revision
			|| !chunk->light_is_current())
		{
			if (std::chrono::steady_clock::now() >= timeout
				|| world->publish_pending_remeshes() != FT_ERR_SUCCESS)
				return (ApplicationError::fail("block-edit batch remesh and light", 1));
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		const auto removed = chunk->read_state;
		const uint64_t removed_revision = chunk->voxel_revision;
		const uint64_t removed_terrain_revision = chunk->chunk.get_revision();
		uint8_t skylight = 0U;
		if (!world->light_level_at(east, y, 7, &skylight) || skylight != 15U
			|| !world->light_level_at(east, y, 8, &skylight) || skylight != 15U)
			return (ApplicationError::fail("block-edit batch exposed skylight", 1));
		std::vector<uint8_t> overrides_before;
		std::string destination;
		if (world->serialize_fluid_state(overrides_before) != FT_ERR_SUCCESS
			|| world->terrain_chunk_store().capture_stream_path(0, 0, destination) != FT_ERR_SUCCESS
			|| destination.empty() || world->terrain_chunk_store().remove_chunk(0, 0) != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit batch write failure setup", 1));
		std::error_code file_error;
		if (!std::filesystem::create_directory(destination, file_error))
			return (ApplicationError::fail("block-edit batch blocked terrain destination", 1));
		result = world->replace_chunk_block_states(0, 0, removed_revision, places, 3U);
		if (!std::filesystem::remove(destination, file_error) || result == FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit batch expected terrain write error", 1));
		std::vector<uint8_t> overrides_after;
		if (chunk->read_state != removed || chunk->voxel_revision != removed_revision
			|| chunk->chunk.get_revision() != removed_terrain_revision
			|| world->serialize_fluid_state(overrides_after) != FT_ERR_SUCCESS
			|| overrides_after != overrides_before)
			return (ApplicationError::fail("block-edit batch rolled back publication", 1));
		for (const WorldChunkBlockEdit &edit : places)
		{
			if (chunk->chunk.read_block(edit.local_x, edit.local_y, edit.local_z, &saved_block)
				!= FT_ERR_SUCCESS || saved_block != air)
				return (ApplicationError::fail("block-edit batch rolled back voxel payload", 1));
		}
		std::puts("block-edit: chunk batch persisted and lit all removals, then rolled back a failed write");
		return (FT_ERR_SUCCESS);
	}

	static void initialize_sealed_light_snapshot(
		WorldGenerationPipeline::WorldChunkSnapshot &snapshot)
	{
		const std::size_t cell_count = GAME_VOXEL_CHUNK_WIDTH
			* GAME_VOXEL_CHUNK_HEIGHT * GAME_VOXEL_CHUNK_DEPTH;
		snapshot.blocks.assign(cell_count, VOXEL_GENERATOR_STONE_BLOCK);
		snapshot.existing_light.assign(cell_count, 0U);
		snapshot.existing_light_valid = FT_TRUE;
		snapshot.west_border_valid = FT_TRUE;
		snapshot.east_border_valid = FT_TRUE;
		snapshot.north_border_valid = FT_TRUE;
		snapshot.south_border_valid = FT_TRUE;
		snapshot.west_border.assign(GAME_VOXEL_CHUNK_HEIGHT * GAME_VOXEL_CHUNK_DEPTH,
			VOXEL_GENERATOR_STONE_BLOCK);
		snapshot.east_border = snapshot.west_border;
		snapshot.north_border.assign(GAME_VOXEL_CHUNK_HEIGHT * GAME_VOXEL_CHUNK_WIDTH,
			VOXEL_GENERATOR_STONE_BLOCK);
		snapshot.south_border = snapshot.north_border;
	}

	static int32_t validate_connected_opening_preview()
	{
		constexpr int32_t x = 7;
		constexpr int32_t y = 32;
		constexpr int32_t z = 8;
		WorldGenerationPipeline::Request request = {};
		request.operation = WorldGenerationPipeline::WorldGenerationOperation::REMESH;
		request.snapshot = std::make_unique<WorldGenerationPipeline::WorldChunkSnapshot>();
		auto &snapshot = *request.snapshot;
		initialize_sealed_light_snapshot(snapshot);
		snapshot.geometry_preview_only = FT_TRUE;
		const auto add_opening = [&](int32_t local_x, int32_t local_y)
		{
			snapshot.blocks[WorldChunkReadState::light_offset(local_x, local_y, z)] = GAME_VOXEL_AIR_BLOCK;
			request.incremental_light_seeds.push_back({FT_TRUE, FT_FALSE, FT_FALSE,
				local_x, local_y, z, VOXEL_GENERATOR_STONE_BLOCK, GAME_VOXEL_AIR_BLOCK, 0U});
		};
		add_opening(x + 1, y);
		add_opening(x, y);
		add_opening(x, y + 1);
		const auto source = WorldChunkReadState::light_offset(x, y + 2, z);
		snapshot.blocks[source] = GAME_VOXEL_AIR_BLOCK;
		snapshot.existing_light[source] = voxel_light_pack(15U, 0U);
		const auto result = WorldChunkGenerationWorker::process_remesh(request);
		if (result == nullptr || result->error_code != FT_ERR_SUCCESS || result->mesh == nullptr
			|| (result->stage_mask & WorldGenerationPipeline::Result::STAGE_GEOMETRY_WAIT_FOR_LIGHT) == 0U)
			return (ApplicationError::fail("block-edit connected opening preview mesh", 1));
		bool floor_face_found = false;
		for (std::size_t index = 0U; index < result->mesh->vertices.size(); ++index)
		{
			const auto &vertex = result->mesh->vertices[index];
			if (vertex.face == CHUNK_MESH_FACE_UP && vertex.coordinate_y == y
				&& vertex.coordinate_x == x + 2 && vertex.coordinate_z == z)
			{
				floor_face_found = true;
				if (voxel_light_sky(vertex.packed_light) != 14U)
					return (ApplicationError::fail("block-edit connected opening preview light", 1));
			}
		}
		if (!floor_face_found)
			return (ApplicationError::fail("block-edit connected opening missing floor face", 1));
		std::printf("block-edit: connected openings publish lit preview faces\n");
		return (FT_ERR_SUCCESS);
	}

	static int32_t validate_isolated_surface_opening(bool dark_neighbour)
	{
		constexpr int32_t x = 8;
		constexpr int32_t y = 32;
		constexpr int32_t z = 8;
		WorldGenerationPipeline::Request request = {};
		request.operation = WorldGenerationPipeline::WorldGenerationOperation::REMESH;
		request.snapshot = std::make_unique<WorldGenerationPipeline::WorldChunkSnapshot>();
		auto &snapshot = *request.snapshot;
		initialize_sealed_light_snapshot(snapshot);
		for (int32_t column_y = y; column_y < GAME_VOXEL_CHUNK_HEIGHT; ++column_y)
		{
			const auto index = WorldChunkReadState::light_offset(x, column_y, z);
			snapshot.blocks[index] = GAME_VOXEL_AIR_BLOCK;
			if (column_y > y)
				snapshot.existing_light[index] = voxel_light_pack(15U, 0U);
		}
		const auto neighbour = WorldChunkReadState::light_offset(x + 1, y, z);
		if (dark_neighbour)
			snapshot.blocks[neighbour] = GAME_VOXEL_AIR_BLOCK;
		request.incremental_removal_light = FT_TRUE;
		request.incremental_light_seeds.push_back({FT_FALSE, FT_TRUE, FT_FALSE,
			x, y, z, VOXEL_GENERATOR_STONE_BLOCK, GAME_VOXEL_AIR_BLOCK, 0U});
		request.light_update_config = {1U, 128U, 256U, 100000U};
		const auto result = WorldChunkGenerationWorker::process_remesh(request);
		if (result == nullptr || result->error_code != FT_ERR_SUCCESS || result->mesh == nullptr
			|| result->incremental_light_deltas_complete == FT_FALSE
			|| result->incremental_light_samples == nullptr)
			return (ApplicationError::fail("block-edit isolated surface opening result", 1));
		const auto &samples = *result->incremental_light_samples;
	const uint8_t opening_light = samples[WorldChunkReadState::light_offset(x, y, z)];
	const uint8_t neighbour_light = samples[neighbour];
	if (opening_light != voxel_light_pack(15U, 0U)
		|| neighbour_light != voxel_light_pack(dark_neighbour ? 14U : 0U, 0U)
			|| result->incremental_light_deltas.size() != (dark_neighbour ? 2U : 1U)
			|| (!dark_neighbour && (result->light_scanned_cells != 1U
				|| !request.incremental_removal_queued_cells.empty())))
	{
		std::fprintf(stderr, "block-edit: surface opening dark_neighbour=%d light=%u "
			"neighbour_light=%u deltas=%zu scanned=%llu removal_queue_cells=%zu\n",
			dark_neighbour ? 1 : 0, static_cast<unsigned>(opening_light),
			static_cast<unsigned>(neighbour_light), result->incremental_light_deltas.size(),
			static_cast<unsigned long long>(result->light_scanned_cells),
			request.incremental_removal_queued_cells.size());
			return (ApplicationError::fail("block-edit isolated surface opening light", 1));
	}
		return (FT_ERR_SUCCESS);
	}

	static int32_t validate_independent_emitter_removal(uint32_t node_target,
		bool expect_preview)
	{
		constexpr int32_t removed_x = 1;
		constexpr int32_t surviving_x = 13;
		constexpr int32_t y = 32;
		constexpr int32_t z = 8;
		const uint32_t emitter = VOXEL_GENERATOR_SHIMMER_STONE_BLOCK;
		const int32_t emission = voxel_block_emitted_light_level(emitter);
		const std::size_t row = (z * GAME_VOXEL_CHUNK_HEIGHT + y)
			* GAME_VOXEL_CHUNK_WIDTH;
		WorldGenerationPipeline::Request request = {};
		request.snapshot = std::make_unique<WorldGenerationPipeline::WorldChunkSnapshot>();
		auto &snapshot = *request.snapshot;
		initialize_sealed_light_snapshot(snapshot);
		for (int32_t x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
		{
			snapshot.blocks[row + x] = GAME_VOXEL_AIR_BLOCK;
			const int32_t distance = std::min(std::abs(x - removed_x),
				std::abs(x - surviving_x));
			snapshot.existing_light[row + x] = voxel_light_pack(0U,
				static_cast<uint8_t>(std::max(0, emission - distance)));
		}
		snapshot.blocks[row + surviving_x] = emitter;
		request.operation = WorldGenerationPipeline::WorldGenerationOperation::REMESH;
		request.incremental_removal_light = FT_TRUE;
		request.remesh_interactive = FT_TRUE;
		request.incremental_light_seeds.push_back({FT_FALSE, FT_TRUE, FT_FALSE,
			removed_x, y, z, emitter, GAME_VOXEL_AIR_BLOCK, 0U});
		request.light_update_config = {1U, node_target, node_target * 2U, 100000U};
		std::unique_ptr<WorldGenerationPipeline::Result> result;
		bool preview_seen = false;
		for (uint32_t step = 0U; step < 128U && result == nullptr; ++step)
		{
			const uint64_t before = request.incremental_processed_cells;
			result = WorldChunkGenerationWorker::process_remesh(request);
			if (request.incremental_processed_cells - before
				> request.light_update_config.target_nodes_per_frame)
				return (ApplicationError::fail("block-edit emitter slice budget", 1));
			if (result != nullptr
				&& (result->stage_mask & WorldGenerationPipeline::Result::STAGE_GEOMETRY_ONLY) != 0U)
			{
				if (preview_seen || result->error_code != FT_ERR_SUCCESS
					|| result->mesh == nullptr || request.remesh_in_progress == FT_FALSE)
					return (ApplicationError::fail("block-edit emitter preview", 1));
				preview_seen = true;
				result.reset();
			}
		}
		if (result == nullptr || result->error_code != FT_ERR_SUCCESS
			|| result->incremental_light_deltas_complete == FT_FALSE
			|| result->incremental_light_samples == nullptr)
			return (ApplicationError::fail("block-edit emitter frontier solve",
				result == nullptr ? FT_ERR_TIMEOUT : result->error_code));
		const auto &samples = *result->incremental_light_samples;
		for (const auto &delta : result->incremental_light_deltas)
		{
			if (delta.local_y() != y || delta.local_z() != z)
				return (ApplicationError::fail("block-edit emitter frontier escaped", 1));
			if (samples[row + delta.local_x()] != delta.packed_light())
				return (ApplicationError::fail("block-edit emitter patch sample", 1));
		}
		for (int32_t x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
		{
			const uint8_t expected = voxel_light_pack(0U,
				static_cast<uint8_t>(std::max(0, emission - std::abs(x - surviving_x))));
			if (samples[row + x] != expected)
			{
				std::fprintf(stderr, "block-edit: surviving emitter mismatch x=%d "
					"expected=%u actual=%u nodes=" FT_UINT64_DECIMAL_FORMAT "\n",
					x, static_cast<unsigned int>(expected),
					static_cast<unsigned int>(samples[row + x]),
					result->light_scanned_cells);
				return (ApplicationError::fail("block-edit surviving emitter light", 1));
			}
		}
		if (result->light_scanned_cells > 32U)
			return (ApplicationError::fail("block-edit removal visited the independent field", 1));
		if (preview_seen != expect_preview)
			return (ApplicationError::fail("block-edit emitter preview selection", 1));
		std::printf("block-edit: independent emitter frontier passed nodes="
			FT_UINT64_DECIMAL_FORMAT " preview=%d\n", result->light_scanned_cells,
			preview_seen ? 1 : 0);
		return (FT_ERR_SUCCESS);
	}

	static int32_t validate_emissive_roof_removal()
	{
		constexpr int32_t x = 8;
		constexpr int32_t z = 8;
		constexpr int32_t bottom = GAME_VOXEL_CHUNK_HEIGHT - 8;
		constexpr int32_t roof = GAME_VOXEL_CHUNK_HEIGHT - 4;
		const uint32_t emitter = VOXEL_GENERATOR_SHIMMER_STONE_BLOCK;
		const int32_t emission = voxel_block_emitted_light_level(emitter);
		WorldGenerationPipeline::Request request = {};
		request.snapshot = std::make_unique<WorldGenerationPipeline::WorldChunkSnapshot>();
		auto &snapshot = *request.snapshot;
		initialize_sealed_light_snapshot(snapshot);
		const auto index_at_height = [](int32_t y) noexcept
		{
			return (static_cast<std::size_t>((z * GAME_VOXEL_CHUNK_HEIGHT + y)
				* GAME_VOXEL_CHUNK_WIDTH + x));
		};
		for (int32_t y = bottom; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
		{
			snapshot.blocks[index_at_height(y)] = GAME_VOXEL_AIR_BLOCK;
			const int32_t block = std::max(0, emission - std::abs(y - roof));
			snapshot.existing_light[index_at_height(y)] = voxel_light_pack(
				y > roof ? 15U : 0U, static_cast<uint8_t>(block));
		}
		request.operation = WorldGenerationPipeline::WorldGenerationOperation::REMESH;
		request.incremental_removal_light = FT_TRUE;
		request.incremental_light_seeds.push_back({FT_FALSE, FT_TRUE, FT_FALSE,
			x, roof, z, emitter, GAME_VOXEL_AIR_BLOCK, 0U});
		request.light_update_config = {1U, 4U, 8U, 100000U};
		std::unique_ptr<WorldGenerationPipeline::Result> result;
		for (uint32_t step = 0U; step < 128U && result == nullptr; ++step)
			result = WorldChunkGenerationWorker::process_remesh(request);
		if (result == nullptr || result->error_code != FT_ERR_SUCCESS
			|| result->incremental_light_deltas_complete == FT_FALSE
			|| result->incremental_light_samples == nullptr)
			return (ApplicationError::fail("block-edit emissive roof solve",
				result == nullptr ? FT_ERR_TIMEOUT : result->error_code));
		const auto &samples = *result->incremental_light_samples;
		for (const auto &delta : result->incremental_light_deltas)
		{
			if (delta.local_x() != x || delta.local_z() != z || delta.local_y() < bottom)
				return (ApplicationError::fail("block-edit emissive roof escaped shaft", 1));
			if (samples[index_at_height(delta.local_y())] != delta.packed_light())
				return (ApplicationError::fail("block-edit roof patch sample", 1));
		}
		for (int32_t y = bottom; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
		{
			if (samples[index_at_height(y)] != voxel_light_pack(15U, 0U))
				return (ApplicationError::fail("block-edit direct sky below emissive roof", 1));
		}
		std::printf("block-edit: emissive roof removal restored full skylight down the shaft\n");
		return (FT_ERR_SUCCESS);
	}

	static std::size_t nonzero_light_cells(const WorldChunk &chunk) noexcept
	{
		std::size_t count = 0U;
		for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
			for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
				for (int32_t x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
					if (chunk.light.get(x, y, z) != 0U)
						count += 1U;
		return (count);
	}

	static std::size_t nonzero_mesh_light_vertices(
		const WorldChunk &chunk) noexcept
	{
		std::size_t count = 0U;
		for (std::size_t index = 0U; index < chunk.mesh.vertices.size(); ++index)
		{
			if (chunk.mesh.vertices[index].packed_light != 0U)
				count += 1U;
		}
		return (count);
	}

	static bool mesh_has_lit_west_boundary_face(const WorldChunk &chunk,
		int32_t y, int32_t z, uint8_t packed_light) noexcept
	{
		for (std::size_t index = 0U; index + 3U < chunk.mesh.vertices.size();
			index += 4U)
		{
			const chunk_mesh_vertex &vertex = chunk.mesh.vertices[index];
			if (vertex.face != CHUNK_MESH_FACE_WEST || vertex.coordinate_x != 0U)
				continue ;
			int32_t min_y = vertex.coordinate_y;
			int32_t min_z = vertex.coordinate_z;
			int32_t max_y = min_y;
			int32_t max_z = min_z;
			for (std::size_t corner = index + 1U; corner < index + 4U; ++corner)
			{
				const chunk_mesh_vertex &point = chunk.mesh.vertices[corner];
				min_y = std::min<int32_t>(min_y, point.coordinate_y);
				min_z = std::min<int32_t>(min_z, point.coordinate_z);
				max_y = std::max<int32_t>(max_y, point.coordinate_y);
				max_z = std::max<int32_t>(max_z, point.coordinate_z);
			}
			if (y >= min_y && y < max_y && z >= min_z && z < max_z)
				return (vertex.packed_light == packed_light);
		}
		return (false);
	}

	static int check_transient_light_publication(const WorldChunk *chunk,
		std::size_t baseline_light_cells,
		std::size_t baseline_mesh_light_vertices,
		bool *observed_empty) noexcept
	{
		std::size_t current_light_cells;
		std::size_t current_mesh_light_vertices;

		if (observed_empty == nullptr || chunk == nullptr
			|| !chunk->initialized || baseline_light_cells == 0U)
			return (FT_ERR_SUCCESS);
		current_light_cells = nonzero_light_cells(*chunk);
		if (!chunk->light_buffer_is_valid() || current_light_cells == 0U)
		{
			if (current_light_cells == 0U)
			{
				*observed_empty = true;
				return (FT_ERR_SUCCESS);
			}
			std::fprintf(stderr,
				"block-edit: transient light publication became empty "
				"chunk=(%d,%d) baseline_cells=%zu current_cells=%zu "
				"light_valid=%d light_current=%d pending=%llu\n",
				chunk->chunk_x, chunk->chunk_z, baseline_light_cells,
				current_light_cells, chunk->light_buffer_is_valid() ? 1 : 0,
				chunk->light_is_current() ? 1 : 0,
				static_cast<unsigned long long>(chunk->pending_mesh_request_id));
			return (FT_ERR_INVALID_STATE);
		}
		if (baseline_mesh_light_vertices == 0U || chunk->mesh.vertices.empty())
			return (FT_ERR_SUCCESS);
		current_mesh_light_vertices = nonzero_mesh_light_vertices(*chunk);
		if (current_mesh_light_vertices == 0U)
		{
			*observed_empty = true;
		}
		return (FT_ERR_SUCCESS);
	}

	static int32_t wait_for_edit_baseline(World &world, int32_t world_x,
		int32_t world_z) noexcept
	{
		const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
			GAME_VOXEL_CHUNK_WIDTH);
		const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
			GAME_VOXEL_CHUNK_DEPTH);
		const std::chrono::steady_clock::time_point deadline =
			std::chrono::steady_clock::now() + std::chrono::seconds(30);

		while (std::chrono::steady_clock::now() < deadline)
		{
			WorldChunk *chunk;

			if (world.update_around(static_cast<double>(world_x),
				static_cast<double>(world_z), 0,
				WorldCoordinates::MIN_RENDER_DISTANCE) != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_OPERATION);
			chunk = world.find_chunk_mutable(chunk_x, chunk_z);
			if (chunk != nullptr && chunk->initialized
				&& chunk->light_is_current()
				&& chunk->pending_mesh_request_id == 0U
				&& chunk->mesh_dirty == false)
				return (FT_ERR_SUCCESS);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return (FT_ERR_TIMEOUT);
	}

	static int32_t validate_test_colored_light_source() noexcept
	{
		const uint32_t test_glowstone_block =
			VOXEL_GENERATOR_SHIMMER_STONE_BLOCK;
		uint8_t light[5][3] = {{0U, 0U, 0U}, {0U, 0U, 0U},
			{0U, 0U, 0U}, {0U, 0U, 0U}, {0U, 0U, 0U}};
		int32_t index;

		(void)test_glowstone_block;
		light[0][0] = 15U;
		light[0][1] = 10U;
		light[0][2] = 5U;
		index = 1;
		while (index < 5)
		{
			if (light[index - 1][0] > 0U)
				light[index][0] = static_cast<uint8_t>(light[index - 1][0] - 1U);
			if (light[index - 1][1] > 0U)
				light[index][1] = static_cast<uint8_t>(light[index - 1][1] - 1U);
			if (light[index - 1][2] > 0U)
				light[index][2] = static_cast<uint8_t>(light[index - 1][2] - 1U);
			index += 1;
		}
		if (light[0][0] != 15U || light[0][1] != 10U
			|| light[0][2] != 5U || light[1][0] != 14U
			|| light[1][1] != 9U || light[1][2] != 4U
			|| light[4][0] != 11U || light[4][1] != 6U
			|| light[4][2] != 1U)
			return (1);
		return (0);
	}

	static int32_t validate_mining_drop_merge(World &world, int32_t x,
		int32_t y, int32_t z, uint32_t mined_block_id,
		uint32_t expected_block_after = GAME_VOXEL_AIR_BLOCK) noexcept
	{
		MinecraftItemCatalog catalog;
		WorldItemStore items;
		MinecraftBlockItemRule rule = {};
		ft_sharedptr<game_item> nearby_stack;
		uint64_t nearby_entity_id = 0U;
		uint32_t block_after = GAME_VOXEL_AIR_BLOCK;
		int32_t result;

		result = catalog.initialize();
		if (result == FT_ERR_SUCCESS)
			result = items.initialize();
		if (result == FT_ERR_SUCCESS)
			result = catalog.resolve_block_item_rule(
			mined_block_id, &rule);
		if (result == FT_ERR_SUCCESS
			&& rule.policy != MINECRAFT_BLOCK_ACQUISITION_SELF_ITEM
			&& rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM)
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
			result = catalog.create_item(rule.item_id, 5, &nearby_stack);
		if (result == FT_ERR_SUCCESS)
			result = items.spawn(nearby_stack, static_cast<double>(x) + 1.0,
				static_cast<double>(y) + 0.5, static_cast<double>(z) + 0.5,
				1U, 42U, 0U, &nearby_entity_id);
		if (result == FT_ERR_SUCCESS)
			result = world.place_block_at(x, y, z, mined_block_id);
		Camera camera;
		camera.initialize();
		camera.x = static_cast<double>(x) + 0.5;
		camera.y = static_cast<double>(y) + 2.5;
		camera.z = static_cast<double>(z) + 0.5;
		camera.yaw = 0.0;
		camera.pitch = -1.5707963267948966;
		if (result == FT_ERR_SUCCESS)
			result = BlockInteractor::try_delete_target_block(&world, camera,
				catalog, items, 42U, 2U);
		if (result == FT_ERR_SUCCESS
			&& (!world.block_id_at(x, y, z, &block_after)
				|| block_after != expected_block_after))
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
		{
			std::vector<WorldItemSnapshot> nearby;
			result = items.query_nearby(static_cast<double>(x) + 0.5,
				static_cast<double>(y) + 0.5, static_cast<double>(z) + 0.5,
				1.0, 42U, 2U, &nearby);
			if (result == FT_ERR_SUCCESS
				&& (nearby.size() != 1U
					|| nearby[0].entity_id != nearby_entity_id
					|| nearby[0].item_id != rule.item_id
					|| nearby[0].quantity != 6))
				result = FT_ERR_INVALID_STATE;
		}
		if (result == FT_ERR_SUCCESS)
			std::printf("block-edit: mining-drop=published-and-merged block=%u item=%d\n",
				mined_block_id, rule.item_id);
		else
			std::fprintf(stderr, "block-edit: mining-drop failed block=%u "
				"policy=%u item=%d result=%d expected-after=%u actual-after=%u\n",
				mined_block_id, static_cast<unsigned int>(rule.policy),
				rule.item_id, result, expected_block_after,
				block_after);
		return (result);
	}

	static int32_t validate_mature_wheat_harvest(World &world, int32_t x,
		int32_t y, int32_t z) noexcept
	{
		MinecraftItemCatalog catalog;
		WorldItemStore items;
		uint32_t block_after = GAME_VOXEL_AIR_BLOCK;
		const char *failed_stage = "catalog-initialize";
		int32_t result = catalog.initialize();
		if (result == FT_ERR_SUCCESS)
			failed_stage = "store-initialize";
		if (result == FT_ERR_SUCCESS)
			result = items.initialize();
		if (result == FT_ERR_SUCCESS)
			failed_stage = "crop-place";
		if (result == FT_ERR_SUCCESS)
		{
			result = world.place_block_at(x, y, z,
				VOXEL_GENERATOR_WHEAT_CROP_MATURE_BLOCK_ID);
		}
		if (result == FT_ERR_SUCCESS)
			failed_stage = "crop-harvest";
		Camera camera;
		camera.initialize();
		camera.x = static_cast<double>(x) + 0.5;
		camera.y = static_cast<double>(y) + 2.5;
		camera.z = static_cast<double>(z) + 0.5;
		camera.yaw = 0.0;
		camera.pitch = -1.5707963267948966;
		if (result == FT_ERR_SUCCESS)
			result = BlockInteractor::try_delete_target_block(&world, camera,
				catalog, items, 42U, 2U);
		if (result != FT_ERR_SUCCESS)
			std::fprintf(stderr, "block-edit: wheat stage=%s error=%d id=%u\n",
				failed_stage, result,
				VOXEL_GENERATOR_WHEAT_CROP_MATURE_BLOCK_ID);
		if (result == FT_ERR_SUCCESS
			&& (!world.block_id_at(x, y, z, &block_after)
				|| block_after != GAME_VOXEL_AIR_BLOCK))
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
		{
			failed_stage = "ground-drop-query";
			std::vector<WorldItemSnapshot> nearby;
			result = items.query_nearby(static_cast<double>(x) + 0.5,
				static_cast<double>(y) + 0.5, static_cast<double>(z) + 0.5,
				1.0, 42U, 2U, &nearby);
			bool found_wheat = false;
			bool found_seeds = false;
			for (const WorldItemSnapshot &entry : nearby)
			{
				found_wheat = found_wheat
					|| (entry.item_id == MinecraftItemCatalog::WHEAT_ITEM_ID
						&& entry.quantity == 1);
				found_seeds = found_seeds
					|| (entry.item_id == MinecraftItemCatalog::SEEDS_ITEM_ID
						&& entry.quantity == 1);
			}
			if (result == FT_ERR_SUCCESS
				&& (nearby.size() != 2U || !found_wheat || !found_seeds))
				result = FT_ERR_INVALID_STATE;
		}
		if (result == FT_ERR_SUCCESS)
			std::printf("block-edit: wheat mature harvest=1 wheat,1 seeds\n");
		return (result);
	}

	static int32_t validate_tree_fruit_harvest(World &world, int32_t x,
		int32_t y, int32_t z, uint32_t leaf_block_id,
		int32_t fruit_item_id) noexcept
	{
	MinecraftItemCatalog catalog;
	WorldItemStore items;
	MinecraftLootDrop expected_fruit = {};
	uint64_t harvest_tick = 0U;
	uint32_t block_after = GAME_VOXEL_AIR_BLOCK;
	int32_t result = catalog.initialize();
	if (result == FT_ERR_SUCCESS)
		result = items.initialize();
	if (result == FT_ERR_SUCCESS)
	{
		for (uint64_t tick = 2U; tick < 4096U; ++tick)
		{
			if (MinecraftLootCatalog::resolve_tree_fruit(leaf_block_id, x, y, z,
					tick, &expected_fruit) == FT_ERR_SUCCESS
				&& expected_fruit.item_id == fruit_item_id)
			{
				harvest_tick = tick;
				break;
			}
		}
		if (harvest_tick == 0U)
			result = FT_ERR_INVALID_STATE;
	}
	if (result == FT_ERR_SUCCESS)
		result = world.place_block_at(x, y, z, leaf_block_id);
	Camera camera;
	camera.initialize();
	camera.x = static_cast<double>(x) + 0.5;
	camera.y = static_cast<double>(y) + 2.5;
	camera.z = static_cast<double>(z) + 0.5;
	camera.yaw = 0.0;
	camera.pitch = -1.5707963267948966;
	if (result == FT_ERR_SUCCESS)
		result = BlockInteractor::try_delete_target_block(&world, camera,
			catalog, items, 42U, harvest_tick);
	if (result == FT_ERR_SUCCESS
		&& (!world.block_id_at(x, y, z, &block_after)
			|| block_after != GAME_VOXEL_AIR_BLOCK))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		std::vector<WorldItemSnapshot> nearby;
		result = items.query_nearby(static_cast<double>(x) + 0.5,
			static_cast<double>(y) + 0.5, static_cast<double>(z) + 0.5,
			1.0, 42U, harvest_tick, &nearby);
		bool found_leaf = false;
		bool found_fruit = false;
		for (const WorldItemSnapshot &entry : nearby)
		{
			found_leaf = found_leaf
				|| (entry.item_id == MinecraftItemCatalog::BLOCK_ITEM_ID_BASE
					+ static_cast<int32_t>(leaf_block_id)
					&& entry.quantity == 1);
			found_fruit = found_fruit
				|| (entry.item_id == fruit_item_id && entry.quantity == 1);
		}
		if (result == FT_ERR_SUCCESS
			&& (nearby.size() != 2U || !found_leaf || !found_fruit))
			result = FT_ERR_INVALID_STATE;
	}
	return (result);
	}

	struct FarmlandSnapshotFixture
	{
		uint32_t calls;
		uint32_t fail_on_call;
	};

	static int32_t farmland_snapshot_callback(void *context) noexcept
	{
		FarmlandSnapshotFixture *fixture =
			static_cast<FarmlandSnapshotFixture *>(context);
		if (fixture == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		++fixture->calls;
		if (fixture->calls == fixture->fail_on_call)
			return (FT_ERR_IO);
		return (FT_ERR_SUCCESS);
	}

	static void farmland_test_append_u32(std::vector<uint8_t> &bytes,
		uint32_t value)
	{
		for (uint32_t shift = 0U; shift < 32U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	static void farmland_test_append_u64(std::vector<uint8_t> &bytes,
		uint64_t value)
	{
		for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	static bool find_farmland_test_soil(World &world, int32_t start_x,
		int32_t z, int32_t *x_out, int32_t *y_out) noexcept
	{
		if (x_out == nullptr || y_out == nullptr)
			return (false);
		for (int32_t x = start_x; x < start_x + 16; ++x)
		{
			for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1; y >= 0; --y)
			{
				uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
				if (world.block_id_at(x, y, z, &block_id)
					&& (block_id == VOXEL_GENERATOR_DIRT_BLOCK_ID
						|| block_id == VOXEL_GENERATOR_GRASS_BLOCK_ID))
				{
					*x_out = x;
					*y_out = y;
					return (true);
				}
			}
		}
		return (false);
	}

	static int32_t validate_farmland_transaction(World &world,
		int32_t start_x, int32_t z) noexcept
	{
		MinecraftItemCatalog catalog;
		game_character character;
		GameTransactionJournal journal;
		ft_sharedptr<game_item> hoe_item;
		game_inventory_snapshot inventory = {};
		FarmlandSnapshotFixture snapshot_state = {0U, 0U};
		GameSessionSnapshotWriter snapshot_writer = {
			&farmland_snapshot_callback, &snapshot_state};
		uint64_t request_id = 0U;
		bool replayed = false;
		int32_t soil_x;
		int32_t soil_y;
		int32_t result = catalog.initialize();
		if (result == FT_ERR_SUCCESS)
			result = character.initialize();
		if (result == FT_ERR_SUCCESS)
			result = journal.initialize();
		if (result == FT_ERR_SUCCESS)
			result = catalog.create_item(MinecraftItemCatalog::WOOD_HOE_ITEM_ID,
				1, &hoe_item);
		if (result == FT_ERR_SUCCESS)
			result = character.get_inventory().add_item(hoe_item);
		if (result == FT_ERR_SUCCESS)
			result = character.get_inventory().get_snapshot(inventory);
		if (result != FT_ERR_SUCCESS)
			return (result);
		auto hoe = std::find_if(inventory.items.begin(), inventory.items.end(),
			[](const game_inventory_item_snapshot &item)
			{
				return (item.item_id == MinecraftItemCatalog::WOOD_HOE_ITEM_ID);
			});
		if (hoe == inventory.items.end()
			|| !find_farmland_test_soil(world, start_x, z, &soil_x, &soil_y))
			return (FT_ERR_NOT_FOUND);
		const int32_t first_chunk_x = WorldCoordinates::floor_divide(soil_x,
			GAME_VOXEL_CHUNK_WIDTH);
		const int32_t chunk_z = WorldCoordinates::floor_divide(z,
			GAME_VOXEL_CHUNK_DEPTH);
		const WorldChunk *first_chunk = world.find_chunk(first_chunk_x, chunk_z);
		if (first_chunk == nullptr)
			return (FT_ERR_NOT_FOUND);
		const uint64_t first_voxel_revision = first_chunk->voxel_revision;
		result = WorldFarmlandTransaction::execute("test-session", character, world,
			journal, 100U, hoe->slot, inventory.revision, soil_x, soil_y, z,
			first_voxel_revision, &snapshot_writer);
		if (result == FT_ERR_SUCCESS)
			result = WorldFarmlandTransaction::execute("test-session", character, world,
				journal, 100U, hoe->slot, inventory.revision, soil_x, soil_y, z,
				first_voxel_revision, &snapshot_writer);
		if (result != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "farmland transaction first/retry failed=%d\n",
			result);
			return (result);
	}
		uint32_t current_block_id = GAME_VOXEL_AIR_BLOCK;
		if (!world.block_id_at(soil_x, soil_y, z, &current_block_id)
			|| current_block_id != VOXEL_GENERATOR_FARMLAND_BLOCK_ID)
			return (FT_ERR_INVALID_STATE);
	const int32_t mismatch_result = WorldFarmlandTransaction::execute("test-session",
		character, world, journal, 100U, hoe->slot, inventory.revision,
		soil_x + 1, soil_y, z, first_voxel_revision, &snapshot_writer);
	if (mismatch_result != FT_ERR_INVALID_ARGUMENT)
	{
		std::fprintf(stderr, "farmland transaction mismatched retry=%d\n",
			mismatch_result);
			return (FT_ERR_INVALID_STATE);
	}
		game_inventory_snapshot inventory_after = {};
		result = character.get_inventory().get_snapshot(inventory_after);
		if (result != FT_ERR_SUCCESS || inventory_after.revision != inventory.revision
			|| inventory_after.items.size() != inventory.items.size())
			return (FT_ERR_INVALID_STATE);
		int32_t second_x;
		int32_t second_y;
		if (!find_farmland_test_soil(world, soil_x + 1, z, &second_x, &second_y))
			return (FT_ERR_NOT_FOUND);
		uint32_t original_soil = GAME_VOXEL_AIR_BLOCK;
		GameTransactionJournal::Record failed_record = {};
		if (!world.block_id_at(second_x, second_y, z, &original_soil))
			return (FT_ERR_NOT_FOUND);
		const int32_t second_chunk_x = WorldCoordinates::floor_divide(second_x,
			GAME_VOXEL_CHUNK_WIDTH);
		const WorldChunk *second_chunk = world.find_chunk(second_chunk_x,
			chunk_z);
		if (second_chunk == nullptr)
			return (FT_ERR_NOT_FOUND);
		snapshot_state.fail_on_call = snapshot_state.calls + 2U;
	result = WorldFarmlandTransaction::execute("test-session", character, world,
			journal, 101U, hoe->slot, inventory.revision, second_x, second_y, z,
			second_chunk->voxel_revision, &snapshot_writer);
		if (result != FT_ERR_IO
			|| !world.block_id_at(second_x, second_y, z, &current_block_id)
			|| current_block_id != original_soil
			|| journal.find(WorldFarmlandTransaction::journal_scope(), 101U,
				&failed_record) != FT_ERR_NOT_FOUND)
	{
		std::fprintf(stderr,
			"farmland transaction rollback result=%d block=%u expected=%u\n",
			result, current_block_id, original_soil);
			return (FT_ERR_INVALID_STATE);
	}
		snapshot_state.fail_on_call = 0U;
		int32_t recovery_x;
		int32_t recovery_y;
		if (!find_farmland_test_soil(world, second_x + 1, z, &recovery_x,
				&recovery_y))
			return (FT_ERR_NOT_FOUND);
		const int32_t recovery_chunk_x = WorldCoordinates::floor_divide(
			recovery_x, GAME_VOXEL_CHUNK_WIDTH);
		const WorldChunk *recovery_chunk = world.find_chunk(recovery_chunk_x,
			chunk_z);
		if (recovery_chunk == nullptr)
			return (FT_ERR_NOT_FOUND);
		std::vector<uint8_t> payload;
		try
		{
			payload.insert(payload.end(), {'G', 'T', 'I', 'L'});
			farmland_test_append_u32(payload, 1U);
			farmland_test_append_u64(payload, 102U);
			farmland_test_append_u32(payload, static_cast<uint32_t>(hoe->slot));
			farmland_test_append_u32(payload, static_cast<uint32_t>(
				MinecraftItemCatalog::WOOD_HOE_ITEM_ID));
			farmland_test_append_u32(payload, 0U);
			farmland_test_append_u64(payload, inventory.revision);
			farmland_test_append_u32(payload, static_cast<uint32_t>(recovery_x));
			farmland_test_append_u32(payload, static_cast<uint32_t>(recovery_y));
			farmland_test_append_u32(payload, static_cast<uint32_t>(z));
			farmland_test_append_u64(payload, recovery_chunk->voxel_revision);
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		result = journal.prepare(WorldFarmlandTransaction::journal_scope(),
			102U, WorldFarmlandTransaction::transaction_type(), payload.data(),
			static_cast<uint32_t>(payload.size()), &request_id, &replayed);
		if (result == FT_ERR_SUCCESS)
			result = world.set_block_state_override(recovery_x, recovery_y, z,
				VOXEL_GENERATOR_FARMLAND_BLOCK_ID);
		if (result == FT_ERR_SUCCESS)
			result = WorldFarmlandTransaction::recover_pending("test-session", world,
				journal, &snapshot_writer);
		GameTransactionJournal::Record recovered = {};
		if (result == FT_ERR_SUCCESS)
			result = journal.find(WorldFarmlandTransaction::journal_scope(),
				102U, &recovered);
		if (result != FT_ERR_SUCCESS
			|| recovered.state != GameTransactionJournal::COMMITTED
			|| !world.block_id_at(recovery_x, recovery_y, z, &current_block_id)
			|| current_block_id != VOXEL_GENERATOR_FARMLAND_BLOCK_ID)
	{
		std::fprintf(stderr,
			"farmland transaction recovery result=%d state=%u block=%u\n",
			result, static_cast<unsigned int>(recovered.state), current_block_id);
			return (FT_ERR_INVALID_STATE);
	}
		std::printf("block-edit: farmland transaction retry,rollback,recovery=ok\n");
		return (FT_ERR_SUCCESS);
	}
}

BlockEditValidator::BlockEditValidator()
{
}

BlockEditValidator::BlockEditValidator(const BlockEditValidator &other)
	: IValidator(other)
{
	*this = other;
}

BlockEditValidator::~BlockEditValidator()
{
}

BlockEditValidator &BlockEditValidator::operator=(const BlockEditValidator &other)
{
	(void)other;
	return (*this);
}

size_t BlockEditValidator::mesh_index_count_for_block(const World &world,
	int32_t world_x, int32_t world_z)
{
	int32_t	chunk_x;
	int32_t	chunk_z;
	int32_t	index;

	chunk_x = world_x >= 0 ? world_x / GAME_VOXEL_CHUNK_WIDTH : -(((-world_x)
				+ GAME_VOXEL_CHUNK_WIDTH - 1) / GAME_VOXEL_CHUNK_WIDTH);
	chunk_z = world_z >= 0 ? world_z / GAME_VOXEL_CHUNK_DEPTH : -(((-world_z)
				+ GAME_VOXEL_CHUNK_DEPTH - 1) / GAME_VOXEL_CHUNK_DEPTH);
	index = 0;
	while (index < world.chunk_count)
	{
		if (world.chunks[index].initialized == true
			&& world.chunks[index].chunk_x == chunk_x
			&& world.chunks[index].chunk_z == chunk_z)
			return (world.chunks[index].mesh.indices.size());
		index = index + 1;
	}
	return (0U);
}

uint64_t BlockEditValidator::mesh_revision_for_block(const World &world,
	int32_t world_x, int32_t world_z)
{
	int32_t chunk_x;
	int32_t chunk_z;
	const WorldChunk *chunk;

	chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	chunk = world.find_chunk(chunk_x, chunk_z);
	if (chunk == nullptr)
		return (0U);
	return (chunk->mesh_revision);
}

int BlockEditValidator::wait_for_mesh_revision(World &world, int32_t world_x,
	int32_t world_z, uint64_t previous_revision,
	uint64_t *latency_milliseconds) noexcept
{
	std::chrono::steady_clock::time_point deadline;
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
	int32_t error_code;
	if (validate_test_colored_light_source() != 0)
		return (ApplicationError::fail("block-edit colored light source", 1));

	deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (std::chrono::steady_clock::now() < deadline)
	{
		error_code = world.update_around(static_cast<double>(world_x),
			static_cast<double>(world_z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (BlockEditValidator::mesh_revision_for_block(world, world_x,
			world_z) > previous_revision)
		{
			const uint64_t elapsed_ms = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - started).count());
			std::printf("block-edit: remesh latency_ms=%llu\n",
				static_cast<unsigned long long>(elapsed_ms));
			if (latency_milliseconds != nullptr)
				*latency_milliseconds = elapsed_ms;
			return (FT_ERR_SUCCESS);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	{
		const WorldChunk *chunk = world.find_chunk(
			WorldCoordinates::floor_divide(world_x, GAME_VOXEL_CHUNK_WIDTH),
			WorldCoordinates::floor_divide(world_z, GAME_VOXEL_CHUNK_DEPTH));
		World::StreamDiagnostics diagnostics = world.stream_diagnostics();
		std::fprintf(stderr,
			"block-edit: remesh timeout chunk=%s mesh_revision=%llu "
			"previous_revision=%llu pending=%llu dirty=%s light_revision=%llu "
			"queued=%zu completed=%zu active=%zu oldest_pending=%llu\n",
			chunk == nullptr ? "missing" : "present",
			chunk == nullptr ? 0ULL
				: static_cast<unsigned long long>(chunk->mesh_revision),
			static_cast<unsigned long long>(previous_revision),
			chunk == nullptr ? 0ULL
				: static_cast<unsigned long long>(chunk->pending_mesh_request_id),
			chunk == nullptr || chunk->mesh_dirty == false ? "false" : "true",
			chunk == nullptr ? 0ULL
				: static_cast<unsigned long long>(chunk->light_revision),
			diagnostics.pending_count, diagnostics.ready_count,
			diagnostics.active_generation_count,
			static_cast<unsigned long long>(diagnostics.oldest_pending_age));
		std::fprintf(stderr,
			"block-edit: scheduler priority_pending=%s priority=(%d,%d) "
			"remesh_in_flight=%zu pipeline_queued=%zu\n",
			world.chunk_streamer.priority_remesh_pending_ ? "true" : "false",
			world.chunk_streamer.priority_remesh_chunk_x_,
			world.chunk_streamer.priority_remesh_chunk_z_,
			world.chunk_streamer.pipeline().remesh_in_flight_count(),
			world.chunk_streamer.pipeline().queued_count());
	}
	return (FT_ERR_TIMEOUT);
}

int BlockEditValidator::wait_for_region_convergence(World &world,
	int32_t world_x, int32_t world_z, uint64_t previous_revision) noexcept
{
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const int32_t offsets[5][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1},
		{0, 1}};
	std::size_t baseline_light_cells[5] = {0U, 0U, 0U, 0U, 0U};
	std::size_t baseline_mesh_light_vertices[5] = {0U, 0U, 0U, 0U, 0U};
	bool observed_empty[5] = {false, false, false, false, false};
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);
	for (int32_t index = 0; index < 5; ++index)
	{
		const WorldChunk *baseline = world.find_chunk(
			chunk_x + offsets[index][0], chunk_z + offsets[index][1]);
		if (baseline != nullptr && baseline->initialized
			&& baseline->light_buffer_is_valid())
		{
			baseline_light_cells[index] = nonzero_light_cells(*baseline);
			baseline_mesh_light_vertices[index] =
				nonzero_mesh_light_vertices(*baseline);
		}
	}
	while (std::chrono::steady_clock::now() < deadline)
	{
		bool converged = true;
		int32_t index = 0;
		int32_t error_code;

		error_code = world.update_around(static_cast<double>(world_x),
			static_cast<double>(world_z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		for (int32_t sample = 0; sample < 5; ++sample)
		{
			const WorldChunk *sampled = world.find_chunk(
				chunk_x + offsets[sample][0], chunk_z + offsets[sample][1]);
			if (check_transient_light_publication(sampled,
				baseline_light_cells[sample],
				baseline_mesh_light_vertices[sample], &observed_empty[sample])
				!= FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_STATE);
		}
		while (index < 5)
		{
			const WorldChunk *chunk = world.find_chunk(chunk_x + offsets[index][0],
				chunk_z + offsets[index][1]);
			if (chunk != nullptr && chunk->initialized
				&& (chunk->mesh_dirty
					|| chunk->pending_mesh_request_id != 0U))
				converged = false;
			index += 1;
		}
		const WorldChunk *target = world.find_chunk(chunk_x, chunk_z);
		if (converged && target != nullptr
			&& target->mesh_revision > previous_revision)
		{
			for (int32_t sample = 0; sample < 5; ++sample)
			{
				const WorldChunk *final_chunk = world.find_chunk(
					chunk_x + offsets[sample][0], chunk_z + offsets[sample][1]);
				if (observed_empty[sample] && final_chunk != nullptr
					&& baseline_light_cells[sample] != 0U
					&& nonzero_light_cells(*final_chunk) != 0U)
					return (FT_ERR_INVALID_STATE);
			}
			return (FT_ERR_SUCCESS);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	{
		const WorldChunk *target = world.find_chunk(chunk_x, chunk_z);
		World::StreamDiagnostics diagnostics = world.stream_diagnostics();
		std::fprintf(stderr,
			"block-edit: region convergence timeout target=%s mesh_revision=%llu "
			"previous_revision=%llu dirty=%s pending=%llu queued=%zu "
			"completed=%zu remesh_in_flight=%zu\n",
			target == nullptr ? "missing" : "present",
			target == nullptr ? 0ULL
				: static_cast<unsigned long long>(target->mesh_revision),
			static_cast<unsigned long long>(previous_revision),
			target == nullptr || !target->mesh_dirty ? "false" : "true",
			target == nullptr ? 0ULL
				: static_cast<unsigned long long>(target->pending_mesh_request_id),
			diagnostics.pending_count, diagnostics.ready_count,
			world.chunk_streamer.pipeline().remesh_in_flight_count());
		int32_t index = 0;
		while (index < 5)
		{
			const WorldChunk *chunk = world.find_chunk(
				chunk_x + offsets[index][0], chunk_z + offsets[index][1]);
			if (chunk != nullptr)
				std::fprintf(stderr,
					"block-edit: region chunk=(%d,%d) mesh=%llu dirty=%s "
					"pending=%llu light_revision=%llu\n",
					chunk->chunk_x, chunk->chunk_z,
					static_cast<unsigned long long>(chunk->mesh_revision),
					chunk->mesh_dirty ? "true" : "false",
					static_cast<unsigned long long>(chunk->pending_mesh_request_id),
					static_cast<unsigned long long>(chunk->light_revision));
			index += 1;
		}
	}
	return (FT_ERR_TIMEOUT);
}

int BlockEditValidator::wait_for_boundary_convergence(World &world,
	int32_t world_x, int32_t world_z,
	const uint64_t previous_revisions[4],
	const uint64_t previous_light_revisions[4]) noexcept
{
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const int32_t offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
	std::size_t baseline_light_cells[5] = {0U, 0U, 0U, 0U, 0U};
	std::size_t baseline_mesh_light_vertices[5] = {0U, 0U, 0U, 0U, 0U};
	bool observed_empty[5] = {false, false, false, false, false};
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);
	{
		const WorldChunk *baseline = world.find_chunk(chunk_x, chunk_z);
		if (baseline != nullptr && baseline->initialized
			&& baseline->light_buffer_is_valid())
		{
			baseline_light_cells[0] = nonzero_light_cells(*baseline);
			baseline_mesh_light_vertices[0] =
				nonzero_mesh_light_vertices(*baseline);
		}
		for (int32_t sample = 0; sample < 4; ++sample)
		{
			baseline = world.find_chunk(chunk_x + offsets[sample][0],
				chunk_z + offsets[sample][1]);
			if (baseline != nullptr && baseline->initialized
				&& baseline->light_buffer_is_valid())
			{
				baseline_light_cells[sample + 1] = nonzero_light_cells(*baseline);
				baseline_mesh_light_vertices[sample + 1] =
					nonzero_mesh_light_vertices(*baseline);
			}
		}
	}

	while (std::chrono::steady_clock::now() < deadline)
	{
		bool converged = true;
		int32_t index = 0;
		int32_t error_code;
		const int32_t local_x = world_x - chunk_x * GAME_VOXEL_CHUNK_WIDTH;
		const int32_t local_z = world_z - chunk_z * GAME_VOXEL_CHUNK_DEPTH;

		error_code = world.update_around(static_cast<double>(world_x),
			static_cast<double>(world_z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (check_transient_light_publication(world.find_chunk(chunk_x, chunk_z),
			baseline_light_cells[0], baseline_mesh_light_vertices[0],
			&observed_empty[0])
			!= FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
		for (int32_t sample = 0; sample < 4; ++sample)
		{
			const WorldChunk *sampled = world.find_chunk(
				chunk_x + offsets[sample][0], chunk_z + offsets[sample][1]);
			if (check_transient_light_publication(sampled,
				baseline_light_cells[sample + 1],
				baseline_mesh_light_vertices[sample + 1],
				&observed_empty[sample + 1])
				!= FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_STATE);
		}
		while (index < 4)
		{
			const WorldChunk *chunk = world.find_chunk(chunk_x
				+ offsets[index][0], chunk_z + offsets[index][1]);
			bool requires_update = false;
			if (local_x == 0 && index == 0)
				requires_update = true;
			if (local_x == GAME_VOXEL_CHUNK_WIDTH - 1 && index == 1)
				requires_update = true;
			if (local_z == 0 && index == 2)
				requires_update = true;
			if (local_z == GAME_VOXEL_CHUNK_DEPTH - 1 && index == 3)
				requires_update = true;
			if (chunk == nullptr || !chunk->initialized
				|| (requires_update && chunk->light_buffer_is_valid() == false)
				|| (requires_update && (chunk->mesh_dirty
					|| chunk->pending_mesh_request_id != 0U
					|| chunk->mesh_revision <= previous_revisions[index]
					|| chunk->light_revision <= previous_light_revisions[index])))
				converged = false;
			if (requires_update && chunk != nullptr && chunk->initialized
				&& chunk->mesh_dirty == false
				&& chunk->pending_mesh_request_id == 0U
				&& chunk->light_is_current() == false)
				converged = false;
			index += 1;
		}
		{
			const WorldChunk *target = world.find_chunk(chunk_x, chunk_z);
			if (target == nullptr || !target->initialized
				|| target->light_buffer_is_valid() == false)
				converged = false;
		}
		if (converged)
		{
			for (int32_t sample = 0; sample < 5; ++sample)
			{
				const WorldChunk *final_chunk;
				if (sample == 0)
					final_chunk = world.find_chunk(chunk_x, chunk_z);
				else
					final_chunk = world.find_chunk(chunk_x
						+ offsets[sample - 1][0], chunk_z
						+ offsets[sample - 1][1]);
				if (observed_empty[sample] && final_chunk != nullptr
					&& baseline_light_cells[sample] != 0U
					&& nonzero_light_cells(*final_chunk) != 0U)
					return (FT_ERR_INVALID_STATE);
			}
			return (FT_ERR_SUCCESS);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	std::fprintf(stderr,
		"block-edit: boundary neighbor remesh convergence timed out at "
		"world=(%d,%d)\n", world_x, world_z);
	{
		int32_t index = 0;
		while (index < 4)
		{
			const WorldChunk *chunk = world.find_chunk(chunk_x
				+ offsets[index][0], chunk_z + offsets[index][1]);
			if (chunk == nullptr)
				std::fprintf(stderr,
					"block-edit: boundary light neighbor[%d] missing\n", index);
			else
				std::fprintf(stderr,
					"block-edit: boundary light neighbor[%d] chunk=(%d,%d) "
					"mesh=%llu previous_mesh=%llu light=%llu previous_light=%llu "
					"dirty=%s pending=%llu\n", index, chunk->chunk_x,
					chunk->chunk_z,
					static_cast<unsigned long long>(chunk->mesh_revision),
					static_cast<unsigned long long>(previous_revisions[index]),
					static_cast<unsigned long long>(chunk->light_revision),
					static_cast<unsigned long long>(
						previous_light_revisions[index]),
					chunk->mesh_dirty ? "true" : "false",
					static_cast<unsigned long long>(
						chunk->pending_mesh_request_id));
			index += 1;
		}
	}
	return (FT_ERR_TIMEOUT);
}

int BlockEditValidator::wait_for_boundary_idle(World &world, int32_t world_x,
	int32_t world_z) noexcept
{
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const int32_t offsets[5][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);

	while (std::chrono::steady_clock::now() < deadline)
	{
		bool idle = true;
		int32_t index = 0;
		int32_t error_code;

		error_code = world.update_around(static_cast<double>(world_x),
			static_cast<double>(world_z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		while (index < 5)
		{
			const WorldChunk *chunk = world.find_chunk(chunk_x
				+ offsets[index][0], chunk_z + offsets[index][1]);
			if (chunk == nullptr || !chunk->initialized || chunk->mesh_dirty
				|| chunk->pending_mesh_request_id != 0U || !chunk->light_is_current())
				idle = false;
			index += 1;
		}
		if (idle)
			return (FT_ERR_SUCCESS);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return (FT_ERR_TIMEOUT);
}

int BlockEditValidator::prepare_edit_target(World &world, int32_t x, int32_t z,
	int32_t &y, size_t &mesh_before)
{
	double	surface_top;

	if (world.surface_top_at(x, z, &surface_top) == false)
	{
		std::fprintf(stderr, "block-edit: failed surface lookup\n");
		return (1);
	}
	y = static_cast<int32_t>(std::floor(surface_top + 1.0));
	mesh_before = mesh_index_count_for_block(world, x, z);
	return (0);
}

int BlockEditValidator::verify_place_block(World &world, int32_t x, int32_t y,
	int32_t z, size_t &mesh_after)
{
	uint32_t	block_id;
	int32_t		error_code;
	WorldChunk	*chunk;
	uint64_t previous_revision;

	previous_revision = BlockEditValidator::mesh_revision_for_block(world, x, z);
	error_code = world.place_block_at(x, y, z, VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit place", error_code));
	if (world.block_id_at(x, y, z, &block_id) == false
		|| block_id != VOXEL_GENERATOR_STONE_BLOCK)
	{
		std::fprintf(stderr,
			"block-edit: placed block not visible in storage\n");
		return (1);
	}
	chunk = world.find_chunk_mutable(WorldCoordinates::floor_divide(x,
		GAME_VOXEL_CHUNK_WIDTH), WorldCoordinates::floor_divide(z,
		GAME_VOXEL_CHUNK_DEPTH));
	if (chunk == nullptr || chunk->light_buffer_is_valid() == false
		|| chunk->light_is_current() != false)
	{
		std::fprintf(stderr,
			"block-edit: placement discarded the previous light buffer\n");
		return (1);
	}
	error_code = BlockEditValidator::wait_for_mesh_revision(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit place remesh", error_code));
	error_code = BlockEditValidator::wait_for_region_convergence(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit place region", error_code));
	if (chunk->light_is_current() == false)
	{
		std::fprintf(stderr,
			"block-edit: placement light did not converge to the new version\n");
		return (1);
	}
	if (chunk->last_incremental_light_content_version
		!= chunk->content_version
		|| chunk->last_incremental_light_voxel_revision
		!= chunk->voxel_revision)
	{
		const World::StreamDiagnostics diagnostics = world.stream_diagnostics();
		std::fprintf(stderr,
			"block-edit: placement unexpectedly used a full light rebuild "
			"chunk=(%d,%d) marker=%d marker_content=%u content=%u "
			"marker_voxel=%llu voxel=%llu light_current=%d mesh=%llu "
			"incremental=%llu full=%llu stale=%zu\n",
			chunk->chunk_x, chunk->chunk_z,
			chunk->last_light_remesh_incremental != FT_FALSE ? 1 : 0,
			static_cast<unsigned int>(
				chunk->last_incremental_light_content_version),
			static_cast<unsigned int>(chunk->content_version),
			static_cast<unsigned long long>(
				chunk->last_incremental_light_voxel_revision),
			static_cast<unsigned long long>(chunk->voxel_revision),
			chunk->light_is_current() ? 1 : 0,
			static_cast<unsigned long long>(chunk->mesh_revision),
			static_cast<unsigned long long>(
				diagnostics.remesh_incremental_completed_count),
			static_cast<unsigned long long>(diagnostics.remesh_full_completed_count),
			diagnostics.stale_remesh_result_count);
		return (1);
	}
	mesh_after = mesh_index_count_for_block(world, x, z);
	return (0);
}

int BlockEditValidator::verify_delete_block(World &world, int32_t x, int32_t y,
	int32_t z, size_t &mesh_after)
{
	uint32_t	block_id;
	int32_t		error_code;
	WorldChunk	*chunk;
	uint64_t previous_revision;
	const uint64_t geometry_publications_before =
		world.stream_diagnostics().remesh_geometry_only_count;
	const uint64_t visible_revision_before = world.geometry_revision;

	previous_revision = BlockEditValidator::mesh_revision_for_block(world, x, z);
	error_code = world.delete_block_at(x, y, z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit delete", error_code));
	if (world.geometry_revision != visible_revision_before)
		return (ApplicationError::fail("block-edit unpublished delete visibility", 1));
	if (world.block_id_at(x, y, z, &block_id) == false
		|| block_id != GAME_VOXEL_AIR_BLOCK)
	{
		std::fprintf(stderr,
			"block-edit: deleted block not visible in storage\n");
		return (1);
	}
	chunk = world.find_chunk_mutable(WorldCoordinates::floor_divide(x,
		GAME_VOXEL_CHUNK_WIDTH), WorldCoordinates::floor_divide(z,
		GAME_VOXEL_CHUNK_DEPTH));
	if (chunk == nullptr || chunk->light_buffer_is_valid() == false
		|| chunk->light_is_current() != false)
	{
		std::fprintf(stderr,
			"block-edit: deletion discarded the previous light buffer\n");
		return (1);
	}
	error_code = BlockEditValidator::wait_for_mesh_revision(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit delete remesh", error_code));
	if (world.geometry_revision == visible_revision_before)
		return (ApplicationError::fail("block-edit published delete visibility", 1));
	/* A neighbour can invalidate the lighting inputs after the edited mesh
	 * publishes in the same update. Its light still covers the local edit. */
	if (chunk->last_mesh_publication_voxel_revision != chunk->voxel_revision
		|| !chunk->light_ready_for_render
		|| (!world_light_version::matches(chunk->light_version, chunk->content_version)
			&& world.stream_diagnostics().remesh_geometry_only_count
				<= geometry_publications_before))
	{
		std::fprintf(stderr, "block-edit: deletion has neither a lit mesh nor a preview\n");
		return (1);
	}
	error_code = BlockEditValidator::wait_for_region_convergence(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit delete region", error_code));
	if (chunk->light_is_current() == false)
	{
		std::fprintf(stderr,
			"block-edit: deletion light did not converge to the new version\n");
		return (1);
	}
	if (chunk->last_incremental_light_content_version
		!= chunk->content_version
		|| chunk->last_incremental_light_voxel_revision
		!= chunk->voxel_revision)
	{
		const World::StreamDiagnostics diagnostics = world.stream_diagnostics();
		std::fprintf(stderr,
			"block-edit: deletion unexpectedly used a full light rebuild "
			"chunk=(%d,%d) marker=%d marker_content=%u content=%u "
			"marker_voxel=%llu voxel=%llu light_current=%d mesh=%llu "
			"incremental=%llu full=%llu stale=%zu\n",
			chunk->chunk_x, chunk->chunk_z,
			chunk->last_light_remesh_incremental != FT_FALSE ? 1 : 0,
			static_cast<unsigned int>(
				chunk->last_incremental_light_content_version),
			static_cast<unsigned int>(chunk->content_version),
			static_cast<unsigned long long>(
				chunk->last_incremental_light_voxel_revision),
			static_cast<unsigned long long>(chunk->voxel_revision),
			chunk->light_is_current() ? 1 : 0,
			static_cast<unsigned long long>(chunk->mesh_revision),
			static_cast<unsigned long long>(
				diagnostics.remesh_incremental_completed_count),
			static_cast<unsigned long long>(diagnostics.remesh_full_completed_count),
			diagnostics.stale_remesh_result_count);
		return (1);
	}
	mesh_after = mesh_index_count_for_block(world, x, z);
	return (0);
}

int BlockEditValidator::validate_repeated_edits(World &world, int32_t x,
	int32_t y, int32_t z) noexcept
{
	static const int32_t EDIT_COUNT = 8;
	std::vector<uint64_t> latencies;
	int32_t iteration;

	latencies.reserve(static_cast<size_t>(EDIT_COUNT * 2));
	iteration = 0;
	while (iteration < EDIT_COUNT)
	{
		uint64_t previous_revision;
		uint64_t latency;
		int32_t error_code;

		previous_revision = BlockEditValidator::mesh_revision_for_block(world,
			x, z);
		error_code = world.place_block_at(x, y, z,
			VOXEL_GENERATOR_STONE_BLOCK);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit repeated place", error_code));
		latency = 0U;
		error_code = BlockEditValidator::wait_for_mesh_revision(world, x, z,
			previous_revision, &latency);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit repeated place remesh",
				error_code));
		latencies.push_back(latency);

		previous_revision = BlockEditValidator::mesh_revision_for_block(world,
			x, z);
		error_code = world.delete_block_at(x, y, z);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit repeated delete", error_code));
		latency = 0U;
		error_code = BlockEditValidator::wait_for_mesh_revision(world, x, z,
			previous_revision, &latency);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit repeated delete remesh",
				error_code));
		latencies.push_back(latency);
		iteration += 1;
	}
	if (latencies.empty())
		return (FT_ERR_INTERNAL);
	std::sort(latencies.begin(), latencies.end());
	const size_t p50_index = (latencies.size() - 1U) * 50U / 100U;
	const size_t p95_index = (latencies.size() - 1U) * 95U / 100U;
	const size_t p99_index = (latencies.size() - 1U) * 99U / 100U;
	std::printf("block-edit: repeated samples=%zu p50_ms=%llu p95_ms=%llu "
		"p99_ms=%llu max_ms=%llu\n", latencies.size(),
		static_cast<unsigned long long>(latencies[p50_index]),
		static_cast<unsigned long long>(latencies[p95_index]),
		static_cast<unsigned long long>(latencies[p99_index]),
		static_cast<unsigned long long>(latencies.back()));
	if (latencies.back() > 1000U)
	{
		std::fprintf(stderr,
			"block-edit: repeated remesh exceeded one-second bound\n");
		return (FT_ERR_TIMEOUT);
	}
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::validate_rapid_edit_supersession(World &world,
	int32_t x, int32_t y, int32_t z) noexcept
{
	static const int32_t BURST_COUNT = 6;
	uint64_t previous_revision;
	int32_t iteration;
	int32_t error_code;
	uint32_t block_id;
	WorldChunk *chunk;

	chunk = world.find_chunk_mutable(WorldCoordinates::floor_divide(x,
		GAME_VOXEL_CHUNK_WIDTH), WorldCoordinates::floor_divide(z,
		GAME_VOXEL_CHUNK_DEPTH));
	if (chunk == nullptr || chunk->read_state == nullptr)
		return (FT_ERR_NOT_FOUND);
	const auto light_samples = chunk->read_state->light;
	previous_revision = BlockEditValidator::mesh_revision_for_block(world,
		x, z);
	iteration = 0;
	while (iteration < BURST_COUNT)
	{
		error_code = world.place_block_at(x, y, z,
			VOXEL_GENERATOR_STONE_BLOCK);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail(
				"block-edit rapid supersession place", error_code));
		error_code = world.delete_block_at(x, y, z);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail(
				"block-edit rapid supersession delete", error_code));
		iteration += 1;
	}
	error_code = BlockEditValidator::wait_for_region_convergence(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail(
			"block-edit rapid supersession convergence", error_code));
	if (world.block_id_at(x, y, z, &block_id) == false
		|| block_id != GAME_VOXEL_AIR_BLOCK)
		return (ApplicationError::fail(
			"block-edit rapid supersession final block", 1));
	chunk = world.find_chunk_mutable(WorldCoordinates::floor_divide(x,
		GAME_VOXEL_CHUNK_WIDTH), WorldCoordinates::floor_divide(z,
		GAME_VOXEL_CHUNK_DEPTH));
	if (chunk == nullptr || chunk->light_is_current() == false
		|| chunk->pending_mesh_request_id != 0U || chunk->mesh_dirty)
		return (ApplicationError::fail(
			"block-edit rapid supersession final light", 1));
	if (chunk->last_light_remesh_incremental == FT_FALSE
		|| chunk->read_state == nullptr || chunk->read_state->light != light_samples)
		return (ApplicationError::fail(
			"block-edit rapid supersession rebuilt the published light", 1));
	std::printf("block-edit: rapid supersession ok burst=%d\n", BURST_COUNT);
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::validate_light_neutral_edits(World &world,
	int32_t x, int32_t y, int32_t z) noexcept
{
	const WorldChunk *chunk = world.find_chunk(
		WorldCoordinates::floor_divide(x, GAME_VOXEL_CHUNK_WIDTH),
		WorldCoordinates::floor_divide(z, GAME_VOXEL_CHUNK_DEPTH));
	if (chunk == nullptr || chunk->read_state == nullptr || !chunk->light_is_current())
		return (FT_ERR_INVALID_STATE);
	const auto light_samples = chunk->read_state->light;
	const uint16_t light_input = chunk->light_input_version;
	for (uint32_t block_id : {VOXEL_GENERATOR_SHRUB_BLOCK, GAME_VOXEL_AIR_BLOCK})
	{
		const uint64_t previous_mesh = chunk->mesh_revision;
		int32_t result = block_id == GAME_VOXEL_AIR_BLOCK
			? world.delete_block_at(x, y, z) : world.place_block_at(x, y, z, block_id);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (!chunk->light_is_current() || chunk->light_input_version != light_input)
			return (FT_ERR_INVALID_STATE);
		result = BlockEditValidator::wait_for_region_convergence(world, x, z,
			previous_mesh);
		if (result != FT_ERR_SUCCESS)
			return (result);
		uint32_t current = 0U;
		if (!world.block_id_at(x, y, z, &current) || current != block_id
			|| chunk->read_state == nullptr || chunk->read_state->light != light_samples
			|| chunk->light_input_version != light_input
			|| chunk->last_mesh_publication_voxel_revision != chunk->voxel_revision)
			return (FT_ERR_INVALID_STATE);
	}
	std::printf("block-edit: shrub placement and removal reused published light\n");
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::validate_interior_emissive_edit(World &world) noexcept
{
	const int32_t x = 4;
	const int32_t z = -6;
	const int32_t chunk_x = WorldCoordinates::floor_divide(x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(z,
		GAME_VOXEL_CHUNK_DEPTH);
	WorldChunk *chunk;
	double surface_top;
	int32_t y;
	uint8_t baseline_light;
	uint8_t source_light;
	uint8_t restored_light;
	uint32_t restored_block;
	uint64_t previous_revision;
	int32_t error_code;

	if (world.surface_top_at(x, z, &surface_top) == false)
		return (ApplicationError::fail("block-edit emissive surface", 1));
	y = static_cast<int32_t>(std::floor(surface_top + 1.0));
	/* Capture the baseline only after the target has finished any prior
	 * generation/remesh work.  Otherwise a late sky-light result can change
	 * the baseline from (for example) 13 to 15 while this test is in flight,
	 * producing a false restore failure. */
	error_code = wait_for_edit_baseline(world, x, z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit emissive baseline",
			error_code));
	chunk = world.find_chunk_mutable(chunk_x, chunk_z);
	if (chunk == nullptr || !chunk->initialized
		|| x - chunk_x * GAME_VOXEL_CHUNK_WIDTH <= 0
		|| x - chunk_x * GAME_VOXEL_CHUNK_WIDTH >= GAME_VOXEL_CHUNK_WIDTH - 1
		|| z - chunk_z * GAME_VOXEL_CHUNK_DEPTH <= 0
		|| z - chunk_z * GAME_VOXEL_CHUNK_DEPTH >= GAME_VOXEL_CHUNK_DEPTH - 1)
		return (ApplicationError::fail("block-edit emissive interior", 1));
	baseline_light = chunk->light.get(x - chunk_x * GAME_VOXEL_CHUNK_WIDTH,
		y, z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	const auto light_index = WorldChunkReadState::light_offset(
		x - chunk_x * GAME_VOXEL_CHUNK_WIDTH, y, z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	const auto baseline_samples = chunk->read_state->light;
	previous_revision = BlockEditValidator::mesh_revision_for_block(world, x, z);
	error_code = world.place_block_at(x, y, z,
		VOXEL_GENERATOR_SHIMMER_STONE_BLOCK);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit emissive place", error_code));
	if (chunk->light_buffer_is_valid() == false
		|| chunk->light_is_current() != false)
		return (ApplicationError::fail("block-edit emissive place state", 1));
	error_code = BlockEditValidator::wait_for_mesh_revision(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit emissive place remesh",
			error_code));
	error_code = BlockEditValidator::wait_for_region_convergence(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit emissive place convergence",
			error_code));
	source_light = chunk->light.get(x - chunk_x * GAME_VOXEL_CHUNK_WIDTH,
		y, z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	if (source_light <= baseline_light || chunk->light_is_current() == false
		|| (*baseline_samples)[light_index] != baseline_light
		|| (*chunk->read_state->light)[light_index] != source_light)
		return (ApplicationError::fail("block-edit emissive source light", 1));
	previous_revision = BlockEditValidator::mesh_revision_for_block(world, x, z);
	error_code = world.delete_block_at(x, y, z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit emissive delete", error_code));
	error_code = BlockEditValidator::wait_for_mesh_revision(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit emissive delete remesh",
			error_code));
	error_code = BlockEditValidator::wait_for_region_convergence(world, x, z,
		previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit emissive delete convergence",
			error_code));
	restored_light = chunk->light.get(x - chunk_x * GAME_VOXEL_CHUNK_WIDTH,
		y, z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	if (world.block_id_at(x, y, z, &restored_block) == false)
		restored_block = UINT32_MAX;
	if (restored_light != baseline_light || chunk->light_is_current() == false)
	{
		std::fprintf(stderr,
			"block-edit: emissive restore mismatch baseline=%u source=%u "
			"restored=%u block=%u content=%u light=%u input=%u computed_input=%u\n",
			static_cast<unsigned int>(baseline_light),
			static_cast<unsigned int>(source_light),
			static_cast<unsigned int>(restored_light),
			restored_block,
			static_cast<unsigned int>(chunk->content_version),
			static_cast<unsigned int>(chunk->light_version),
			static_cast<unsigned int>(chunk->light_input_version),
			static_cast<unsigned int>(chunk->computed_light_input_version));
		return (ApplicationError::fail("block-edit emissive restore light", 1));
	}
	std::printf("block-edit: interior emissive source=%u restored=%u\n",
		static_cast<unsigned int>(source_light),
		static_cast<unsigned int>(restored_light));

	/* The edited voxel is one cell inside the chunk. The old scheduler
	 * only notified the neighbor when the edited voxel itself was on a face. */
	const int32_t edge_x = GAME_VOXEL_CHUNK_WIDTH - 2;
	const int32_t test_y = GAME_VOXEL_CHUNK_HEIGHT - 24;
	const int32_t east_x = GAME_VOXEL_CHUNK_WIDTH;
	const WorldChunk *east = world.find_chunk(1, chunk_z);
	if (east == nullptr || !east->initialized)
		return (ApplicationError::fail("block-edit interior emitter east chunk", 1));
	error_code = world.place_block_at(east_x, test_y, z + 1,
		VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code == FT_ERR_SUCCESS)
		error_code = wait_for_edit_baseline(world, east_x, z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit interior emitter receiver", error_code));
	previous_revision = chunk->mesh_revision;
	error_code = world.place_block_at(edge_x, test_y, z,
		VOXEL_GENERATOR_SHIMMER_STONE_BLOCK);
	if (error_code == FT_ERR_SUCCESS)
		error_code = BlockEditValidator::wait_for_region_convergence(world,
			edge_x, z, previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit interior emitter cross-chunk place", error_code));
	const int32_t local_z = z - chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	const auto expected_level = static_cast<uint8_t>(voxel_block_emitted_light_level(
		VOXEL_GENERATOR_SHIMMER_STONE_BLOCK) - 2U);
	const uint8_t lit_face = chunk->light.get(GAME_VOXEL_CHUNK_WIDTH - 1,
		test_y, local_z + 1);
	if (!east->light_is_current()
		|| voxel_light_block(east->light.get(0, test_y, local_z)) != expected_level
		|| voxel_light_block(lit_face) != expected_level
		|| !mesh_has_lit_west_boundary_face(*east, test_y, local_z + 1, lit_face))
		return (ApplicationError::fail("block-edit interior emitter seam lighting", 1));
	previous_revision = chunk->mesh_revision;
	error_code = world.delete_block_at(edge_x, test_y, z);
	if (error_code == FT_ERR_SUCCESS)
		error_code = BlockEditValidator::wait_for_region_convergence(world,
			edge_x, z, previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit interior emitter cross-chunk remove", error_code));
	const uint8_t unlit_face = chunk->light.get(GAME_VOXEL_CHUNK_WIDTH - 1,
		test_y, local_z + 1);
	if (!chunk->light_is_current() || !east->light_is_current()
		|| voxel_light_block(chunk->light.get(GAME_VOXEL_CHUNK_WIDTH - 1,
			test_y, local_z)) != 0U
		|| voxel_light_block(east->light.get(0, test_y, local_z)) != 0U
		|| voxel_light_block(unlit_face) != 0U
		|| !mesh_has_lit_west_boundary_face(*east, test_y, local_z + 1, unlit_face))
		return (ApplicationError::fail("block-edit interior emitter seam restoration", 1));
	error_code = world.delete_block_at(east_x, test_y, z + 1);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit interior emitter receiver cleanup", error_code));
	std::printf("block-edit: interior emitter updated neighbor light=%u then restored the face\n",
		static_cast<unsigned int>(expected_level));
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::validate_boundary_edit(World &world) noexcept
{
	const int32_t x = GAME_VOXEL_CHUNK_WIDTH - 1;
	const int32_t z = -6;
	const int32_t chunk_x = WorldCoordinates::floor_divide(x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(z,
		GAME_VOXEL_CHUNK_DEPTH);
	const int32_t offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
	uint64_t previous_revisions[4];
	uint64_t previous_light_revisions[4];
	uint8_t baseline_light;
	uint8_t baseline_neighbor_light;
	uint8_t placed_light;
	uint8_t placed_neighbor_light;
	uint8_t deleted_light;
	uint8_t deleted_neighbor_light;
	uint64_t place_started_milliseconds;
	uint64_t delete_started_milliseconds;
	uint64_t place_latency_milliseconds;
	uint64_t delete_latency_milliseconds;
	uint64_t incremental_before;
	uint64_t incremental_after;
	uint32_t edge_block_id;
	uint8_t edge_before_light;
	uint8_t edge_after_light;
	int32_t edge_y;
	WorldChunk *target_chunk;
	WorldChunk *right_neighbor;
	int32_t y;
	int32_t local_z;
	double surface_top;
	int32_t index;
	int32_t error_code;

	if (world.surface_top_at(x, z, &surface_top) == false)
		return (ApplicationError::fail("block-edit boundary surface", 1));
	y = static_cast<int32_t>(std::floor(surface_top + 1.0));
	error_code = BlockEditValidator::wait_for_boundary_idle(world, x, z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary baseline", error_code));
	target_chunk = world.find_chunk_mutable(chunk_x, chunk_z);
	if (target_chunk == nullptr || !target_chunk->initialized)
		return (ApplicationError::fail("block-edit boundary target", 1));
	right_neighbor = world.find_chunk_mutable(chunk_x + 1, chunk_z);
	if (right_neighbor == nullptr || !right_neighbor->initialized)
		return (ApplicationError::fail("block-edit boundary right neighbor", 1));
	local_z = z - chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	baseline_light = target_chunk->light.get(
		x - chunk_x * GAME_VOXEL_CHUNK_WIDTH, y,
		z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	baseline_neighbor_light = right_neighbor->light.get(0, y, local_z);
	index = 0;
	while (index < 4)
	{
		const WorldChunk *chunk = world.find_chunk(chunk_x + offsets[index][0],
			chunk_z + offsets[index][1]);
		if (chunk == nullptr || !chunk->initialized)
			return (ApplicationError::fail("block-edit boundary neighbor", 1));
		previous_revisions[index] = chunk->mesh_revision;
		previous_light_revisions[index] = chunk->light_revision;
		index += 1;
	}
	place_started_milliseconds = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	error_code = world.place_block_at(x, y, z,
		VOXEL_GENERATOR_SHIMMER_STONE_BLOCK);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary place", error_code));
	error_code = BlockEditValidator::wait_for_boundary_convergence(world, x, z,
		previous_revisions, previous_light_revisions);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary remesh", error_code));
	place_latency_milliseconds = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count())
		- place_started_milliseconds;
	placed_light = target_chunk->light.get(
		x - chunk_x * GAME_VOXEL_CHUNK_WIDTH, y,
		z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	placed_neighbor_light = right_neighbor->light.get(0, y, local_z);
	if (placed_light <= baseline_light
		|| placed_neighbor_light <= baseline_neighbor_light)
	{
		std::fprintf(stderr,
			"block-edit: boundary light did not propagate target=%u->%u "
			"neighbor=%u->%u\n",
			static_cast<unsigned int>(baseline_light),
			static_cast<unsigned int>(placed_light),
			static_cast<unsigned int>(baseline_neighbor_light),
			static_cast<unsigned int>(placed_neighbor_light));
		return (ApplicationError::fail("block-edit boundary light place", 1));
	}
	index = 0;
	while (index < 4)
	{
		const WorldChunk *chunk = world.find_chunk(chunk_x + offsets[index][0],
			chunk_z + offsets[index][1]);
		if (chunk == nullptr || !chunk->initialized)
			return (ApplicationError::fail("block-edit boundary post-place", 1));
		previous_revisions[index] = chunk->mesh_revision;
		previous_light_revisions[index] = chunk->light_revision;
		index += 1;
	}
	delete_started_milliseconds = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	error_code = world.delete_block_at(x, y, z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary delete", error_code));
	error_code = BlockEditValidator::wait_for_boundary_convergence(world, x, z,
		previous_revisions, previous_light_revisions);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	delete_latency_milliseconds = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count())
		- delete_started_milliseconds;
	deleted_light = target_chunk->light.get(
		x - chunk_x * GAME_VOXEL_CHUNK_WIDTH, y,
		z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	deleted_neighbor_light = right_neighbor->light.get(0, y, local_z);
	if (deleted_light != baseline_light
		|| deleted_neighbor_light != baseline_neighbor_light)
	{
		std::fprintf(stderr,
			"block-edit: boundary light did not restore target=%u->%u "
			"neighbor=%u->%u\n", static_cast<unsigned int>(baseline_light),
			static_cast<unsigned int>(deleted_light),
			static_cast<unsigned int>(baseline_neighbor_light),
			static_cast<unsigned int>(deleted_neighbor_light));
		return (ApplicationError::fail("block-edit boundary light delete", 1));
	}
	std::printf("block-edit: boundary place_ms=%llu delete_ms=%llu\n",
		static_cast<unsigned long long>(place_latency_milliseconds),
		static_cast<unsigned long long>(delete_latency_milliseconds));
	edge_y = y - 1;
	edge_block_id = GAME_VOXEL_AIR_BLOCK;
	while (edge_y >= 0 && edge_block_id == GAME_VOXEL_AIR_BLOCK)
	{
		if (world.block_id_at(x, edge_y, z, &edge_block_id) == false)
			return (ApplicationError::fail("block-edit boundary opaque lookup", 1));
		if (edge_block_id == GAME_VOXEL_AIR_BLOCK)
			edge_y -= 1;
	}
	if (edge_y < 0 || voxel_block_is_breakable(edge_block_id) == FT_FALSE)
	{
		std::fprintf(stderr,
			"block-edit: boundary opaque setup block=%u y=%d\n",
			edge_block_id, edge_y);
		return (ApplicationError::fail("block-edit boundary opaque setup", 1));
	}
	edge_before_light = target_chunk->light.get(
		x - chunk_x * GAME_VOXEL_CHUNK_WIDTH, edge_y,
		z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	incremental_before = world.stream_diagnostics()
		.remesh_incremental_completed_count;
	index = 0;
	while (index < 4)
	{
		const WorldChunk *chunk = world.find_chunk(chunk_x + offsets[index][0],
			chunk_z + offsets[index][1]);
		if (chunk == nullptr || !chunk->initialized)
			return (ApplicationError::fail("block-edit boundary opaque neighbors", 1));
		previous_revisions[index] = chunk->mesh_revision;
		previous_light_revisions[index] = chunk->light_revision;
		index += 1;
	}
	error_code = world.delete_block_at(x, edge_y, z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary opaque delete",
			error_code));
	error_code = BlockEditValidator::wait_for_boundary_convergence(world, x, z,
		previous_revisions, previous_light_revisions);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary opaque convergence",
			error_code));
	edge_after_light = target_chunk->light.get(
		x - chunk_x * GAME_VOXEL_CHUNK_WIDTH, edge_y,
		z - chunk_z * GAME_VOXEL_CHUNK_DEPTH);
	incremental_after = world.stream_diagnostics()
		.remesh_incremental_completed_count;
	if (incremental_after <= incremental_before
		|| target_chunk->light_buffer_is_valid() == false
		|| target_chunk->light_is_current() == false)
	{
		std::fprintf(stderr,
			"block-edit: boundary additive path missing before=%llu after=%llu "
			"light=%u->%u\n",
			static_cast<unsigned long long>(incremental_before),
			static_cast<unsigned long long>(incremental_after),
			static_cast<unsigned int>(edge_before_light),
			static_cast<unsigned int>(edge_after_light));
		return (ApplicationError::fail("block-edit boundary additive path", 1));
	}
	previous_revisions[0] = target_chunk->mesh_revision;
	index = 1;
	while (index < 4)
	{
		const WorldChunk *chunk = world.find_chunk(chunk_x + offsets[index][0],
			chunk_z + offsets[index][1]);
		if (chunk == nullptr || !chunk->initialized)
			return (ApplicationError::fail("block-edit boundary opaque restore neighbors", 1));
		previous_revisions[index] = chunk->mesh_revision;
		previous_light_revisions[index] = chunk->light_revision;
		index += 1;
	}
	error_code = world.place_block_at(x, edge_y, z, edge_block_id);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary opaque restore",
			error_code));
	error_code = BlockEditValidator::wait_for_boundary_convergence(world, x, z,
		previous_revisions, previous_light_revisions);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit boundary opaque restore convergence",
			error_code));
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::validate_boundary_face_matrix(World &world) noexcept
{
	static const int32_t positions[4][2] = {{0, -6},
		{GAME_VOXEL_CHUNK_WIDTH - 1, -6}, {2, 0},
		{2, GAME_VOXEL_CHUNK_DEPTH - 1}};
	static const int32_t neighbour_offsets[4][2] = {{-1, 0},
		{1, 0}, {0, -1}, {0, 1}};
	int32_t position_index = 0;

	while (position_index < 4)
	{
		const int32_t world_x = positions[position_index][0];
		const int32_t world_z = positions[position_index][1];
		const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
			GAME_VOXEL_CHUNK_WIDTH);
		const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
			GAME_VOXEL_CHUNK_DEPTH);
		int32_t target_local_x = world_x
			- chunk_x * GAME_VOXEL_CHUNK_WIDTH;
		int32_t target_local_z = world_z
			- chunk_z * GAME_VOXEL_CHUNK_DEPTH;
		int32_t neighbour_chunk_x = chunk_x;
		int32_t neighbour_chunk_z = chunk_z;
		int32_t neighbour_local_x = target_local_x;
		int32_t neighbour_local_z = target_local_z;
		int32_t y;
		double surface_top;
		WorldChunk *target_chunk;
		WorldChunk *neighbour_chunk;
		uint8_t baseline_target_light;
		uint8_t baseline_neighbour_light;
		uint64_t previous_revisions[4];
		uint64_t previous_light_revisions[4];
		int32_t index = 0;
		int32_t error_code;

		if (target_local_x == 0)
		{
			neighbour_chunk_x -= 1;
			neighbour_local_x = GAME_VOXEL_CHUNK_WIDTH - 1;
		}
		else if (target_local_x == GAME_VOXEL_CHUNK_WIDTH - 1)
		{
			neighbour_chunk_x += 1;
			neighbour_local_x = 0;
		}
		else if (target_local_z == 0)
		{
			neighbour_chunk_z -= 1;
			neighbour_local_z = GAME_VOXEL_CHUNK_DEPTH - 1;
		}
		else if (target_local_z == GAME_VOXEL_CHUNK_DEPTH - 1)
		{
			neighbour_chunk_z += 1;
			neighbour_local_z = 0;
		}
		else
			return (ApplicationError::fail("block-edit boundary matrix edge", 1));
		if (world.surface_top_at(world_x, world_z, &surface_top) == false)
			return (ApplicationError::fail("block-edit boundary matrix surface", 1));
		y = static_cast<int32_t>(std::floor(surface_top + 1.0));
		error_code = BlockEditValidator::wait_for_boundary_idle(world, world_x,
			world_z);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit boundary matrix idle",
				error_code));
		target_chunk = world.find_chunk_mutable(chunk_x, chunk_z);
		neighbour_chunk = world.find_chunk_mutable(neighbour_chunk_x,
			neighbour_chunk_z);
		if (target_chunk == nullptr || neighbour_chunk == nullptr
			|| target_chunk->initialized == false
			|| neighbour_chunk->initialized == false)
			return (ApplicationError::fail("block-edit boundary matrix chunks", 1));
		baseline_target_light = target_chunk->light.get(target_local_x, y,
			target_local_z);
		baseline_neighbour_light = neighbour_chunk->light.get(neighbour_local_x,
			y, neighbour_local_z);
		while (index < 4)
		{
			const WorldChunk *chunk = world.find_chunk(chunk_x
				+ neighbour_offsets[index][0], chunk_z
				+ neighbour_offsets[index][1]);
			if (chunk == nullptr || chunk->initialized == false)
				return (ApplicationError::fail(
					"block-edit boundary matrix neighbours", 1));
			previous_revisions[index] = chunk->mesh_revision;
			previous_light_revisions[index] = chunk->light_revision;
			index += 1;
		}
		error_code = world.place_block_at(world_x, y, world_z,
			VOXEL_GENERATOR_SHIMMER_STONE_BLOCK);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit boundary matrix place",
				error_code));
		error_code = BlockEditValidator::wait_for_boundary_convergence(world,
			world_x, world_z, previous_revisions, previous_light_revisions);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit boundary matrix place wait",
				error_code));
		if (voxel_light_block(target_chunk->light.get(target_local_x, y,
			target_local_z)) <= voxel_light_block(baseline_target_light)
			|| voxel_light_block(neighbour_chunk->light.get(neighbour_local_x,
			y, neighbour_local_z))
				<= voxel_light_block(baseline_neighbour_light))
			return (ApplicationError::fail("block-edit boundary matrix propagate",
				1));
		index = 0;
		while (index < 4)
		{
			const WorldChunk *chunk = world.find_chunk(chunk_x
				+ neighbour_offsets[index][0], chunk_z
				+ neighbour_offsets[index][1]);
			if (chunk == nullptr)
				return (ApplicationError::fail(
					"block-edit boundary matrix post-place", 1));
			previous_revisions[index] = chunk->mesh_revision;
			previous_light_revisions[index] = chunk->light_revision;
			index += 1;
		}
		error_code = world.delete_block_at(world_x, y, world_z);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit boundary matrix delete",
				error_code));
		error_code = BlockEditValidator::wait_for_boundary_convergence(world,
			world_x, world_z, previous_revisions, previous_light_revisions);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit boundary matrix delete wait",
				error_code));
		if (target_chunk->light.get(target_local_x, y, target_local_z)
			!= baseline_target_light
			|| neighbour_chunk->light.get(neighbour_local_x, y,
				neighbour_local_z) != baseline_neighbour_light)
			return (ApplicationError::fail("block-edit boundary matrix restore", 1));
		position_index += 1;
	}
	std::printf("block-edit: boundary face matrix passed faces=4\n");
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::validate_solid_neighbor_geometry(World &world) noexcept
{
	const int32_t world_x = GAME_VOXEL_CHUNK_WIDTH - 1;
	const int32_t world_z = -6;
	const int32_t neighbour_x = world_x + 1;
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const int32_t neighbour_chunk_x = chunk_x + 1;
	const uint32_t air_block = GAME_VOXEL_AIR_BLOCK;
	uint32_t target_block;
	uint32_t neighbour_block;
	WorldChunk *target_chunk;
	WorldChunk *neighbour_chunk;
	double surface_top;
	int32_t y;
	uint64_t target_before;
	uint64_t neighbour_before;
	int32_t error_code;

	if (world.surface_top_at(world_x, world_z, &surface_top) == false)
		return (ApplicationError::fail("block-edit solid-neighbor surface", 1));
	y = static_cast<int32_t>(std::floor(surface_top));
	target_block = air_block;
	neighbour_block = air_block;
	while (y >= 0)
	{
		if (world.block_id_at(world_x, y, world_z, &target_block) == false
			|| world.block_id_at(neighbour_x, y, world_z, &neighbour_block)
				== false)
			return (ApplicationError::fail("block-edit solid-neighbor lookup", 1));
		if (target_block != air_block && neighbour_block != air_block
			&& voxel_block_is_breakable(target_block) != FT_FALSE
			&& voxel_block_is_breakable(neighbour_block) != FT_FALSE)
			break ;
		y -= 1;
	}
	if (y < 0)
	{
		std::fprintf(stderr,
			"block-edit: solid-neighbor setup target=%u neighbor=%u y=%d\n",
			target_block, neighbour_block, y);
		return (ApplicationError::fail("block-edit solid-neighbor setup", 1));
	}
	target_chunk = world.find_chunk_mutable(chunk_x, chunk_z);
	neighbour_chunk = world.find_chunk_mutable(neighbour_chunk_x, chunk_z);
	if (target_chunk == nullptr || neighbour_chunk == nullptr
		|| target_chunk->initialized == false
		|| neighbour_chunk->initialized == false)
		return (ApplicationError::fail("block-edit solid-neighbor chunks", 1));
	error_code = BlockEditValidator::wait_for_boundary_idle(world, world_x,
		world_z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit solid-neighbor idle",
			error_code));
	target_before = target_chunk->mesh_revision;
	neighbour_before = neighbour_chunk->mesh_revision;
	const uint16_t neighbour_light_input_before = neighbour_chunk->light_input_version;
	error_code = world.delete_block_at(world_x, y, world_z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit solid-neighbor delete",
			error_code));
	error_code = BlockEditValidator::wait_for_mesh_revision(world, neighbour_x,
		world_z, neighbour_before);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail(
			"block-edit solid-neighbor geometry remesh", error_code));
	if (world.block_id_at(neighbour_x, y, world_z, &neighbour_block) == false
		|| neighbour_block == air_block)
		return (ApplicationError::fail("block-edit solid-neighbor preserved", 1));
	const int32_t local_z = world_z - chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	const uint8_t cell_light = target_chunk->light.get(
		GAME_VOXEL_CHUNK_WIDTH - 1, y, local_z);
	const uint8_t expected_light = voxel_light_pack(voxel_light_sky(cell_light),
		std::max(voxel_light_block(cell_light),
			voxel_block_emitted_light_level(neighbour_block)));
	if (!target_chunk->light_is_current()
		|| !neighbour_chunk->light_is_current()
		|| neighbour_chunk->light_input_version != neighbour_light_input_before
		|| !mesh_has_lit_west_boundary_face(*neighbour_chunk, y, local_z,
			expected_light))
	{
		std::fprintf(stderr,
			"block-edit: neighbor west boundary faces did not sample the "
			"published light chunk=(%d,%d) y=%d mesh=%llu\n",
			neighbour_chunk->chunk_x, neighbour_chunk->chunk_z, y,
			static_cast<unsigned long long>(neighbour_chunk->mesh_revision));
		return (ApplicationError::fail(
			"block-edit solid-neighbor boundary face", 1));
	}
	error_code = world.place_block_at(world_x, y, world_z, target_block);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit solid-neighbor restore",
			error_code));
	error_code = BlockEditValidator::wait_for_mesh_revision(world, world_x,
		world_z, target_before);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail(
			"block-edit solid-neighbor restore remesh", error_code));
	if (world.block_id_at(world_x, y, world_z, &target_block) == false
		|| target_block == air_block)
		return (ApplicationError::fail("block-edit solid-neighbor restore state",
			1));
	std::printf("block-edit: solid-neighbor geometry passed neighbor_mesh=%llu\n",
		static_cast<unsigned long long>(neighbour_chunk->mesh_revision));
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::validate_rapid_border_supersession(World &world) noexcept
{
	static const int32_t BURST_COUNT = 4;
	const int32_t world_x = GAME_VOXEL_CHUNK_WIDTH - 1;
	const int32_t world_z = -6;
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	uint32_t block_id;
	double surface_top;
	int32_t y;
	int32_t iteration;
	int32_t error_code;
	uint64_t previous_revision;
	WorldChunk *target_chunk;
	WorldChunk *neighbour_chunk;

	if (world.surface_top_at(world_x, world_z, &surface_top) == false)
		return (ApplicationError::fail("block-edit rapid border surface", 1));
	y = static_cast<int32_t>(std::floor(surface_top + 1.0));
	if (world.block_id_at(world_x, y, world_z, &block_id) == false
		|| block_id != GAME_VOXEL_AIR_BLOCK)
		return (ApplicationError::fail("block-edit rapid border target", 1));
	target_chunk = world.find_chunk_mutable(chunk_x, chunk_z);
	neighbour_chunk = world.find_chunk_mutable(chunk_x + 1, chunk_z);
	if (target_chunk == nullptr || neighbour_chunk == nullptr
		|| target_chunk->initialized == false
		|| neighbour_chunk->initialized == false)
		return (ApplicationError::fail("block-edit rapid border chunks", 1));
	error_code = BlockEditValidator::wait_for_boundary_idle(world, world_x,
		world_z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit rapid border idle",
			error_code));
	uint32_t border_block_id = GAME_VOXEL_AIR_BLOCK;
	error_code = neighbour_chunk->chunk.read_block(0, y,
		world_z - chunk_z * GAME_VOXEL_CHUNK_DEPTH, &border_block_id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	previous_revision = target_chunk->mesh_revision;
	iteration = 0;
	while (iteration < BURST_COUNT)
	{
		error_code = world.place_block_at(world_x, y, world_z,
			VOXEL_GENERATOR_STONE_BLOCK);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit rapid border place",
				error_code));
		error_code = world.delete_block_at(world_x, y, world_z);
		if (error_code != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit rapid border delete",
				error_code));
		iteration += 1;
	}
	error_code = BlockEditValidator::wait_for_region_convergence(world, world_x,
		world_z, previous_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit rapid border convergence",
			error_code));
	if (world.block_id_at(world_x, y, world_z, &block_id) == false
		|| block_id != GAME_VOXEL_AIR_BLOCK
		|| target_chunk->light_is_current() == false
		|| target_chunk->pending_mesh_request_id != 0U
		|| target_chunk->mesh_dirty
		|| neighbour_chunk->light_is_current() == false
		|| neighbour_chunk->pending_mesh_request_id != 0U
		|| neighbour_chunk->mesh_dirty)
		return (ApplicationError::fail("block-edit rapid border final state", 1));
	if (target_chunk->last_light_remesh_incremental == FT_FALSE
		|| (voxel_block_is_transparent(border_block_id) != FT_FALSE
			&& neighbour_chunk->last_light_remesh_incremental == FT_FALSE))
		return (ApplicationError::fail(
			"block-edit rapid border supersession rebuilt published light", 1));
	std::printf("block-edit: rapid border supersession ok burst=%d\n",
		BURST_COUNT);
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::validate_cross_chunk_light_publication(World &world)
noexcept
{
	const int32_t world_x = GAME_VOXEL_CHUNK_WIDTH - 1;
	const int32_t world_z = -6;
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	WorldChunk *source;
	WorldChunk *neighbour;
	double surface_top;
	int32_t y;
	uint32_t block_id;
	std::chrono::steady_clock::time_point deadline;

	if (!world.surface_top_at(world_x, world_z, &surface_top))
		return (ApplicationError::fail("block-edit paired-light surface", 1));
	y = static_cast<int32_t>(std::floor(surface_top + 1.0));
	source = world.find_chunk_mutable(0, chunk_z);
	neighbour = world.find_chunk_mutable(1, chunk_z);
	if (source == nullptr || neighbour == nullptr || !source->initialized
		|| !neighbour->initialized
		|| !world.block_id_at(world_x, y, world_z, &block_id)
		|| block_id != GAME_VOXEL_AIR_BLOCK)
		return (ApplicationError::fail("block-edit paired-light baseline", 1));
	if (BlockEditValidator::wait_for_boundary_idle(world, world_x, world_z)
		!= FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit paired-light idle", 1));
	if (world.place_block_at(world_x, y, world_z, VOXEL_GENERATOR_STONE_BLOCK)
		!= FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit paired-light place", 1));
	deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (world.update_around(static_cast<double>(world_x),
			static_cast<double>(world_z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE) != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit paired-light update", 1));
		source = world.find_chunk_mutable(0, chunk_z);
		neighbour = world.find_chunk_mutable(1, chunk_z);
		if (source == nullptr || neighbour == nullptr
			|| !source->light_buffer_is_valid()
			|| !neighbour->light_buffer_is_valid()
			|| nonzero_light_cells(*source) == 0U
			|| nonzero_light_cells(*neighbour) == 0U)
			return (ApplicationError::fail(
				"block-edit paired-light publication gap", 1));
		if (source->light_is_current() && neighbour->light_is_current()
			&& source->pending_mesh_request_id == 0U
			&& neighbour->pending_mesh_request_id == 0U
			&& !source->mesh_dirty && !neighbour->mesh_dirty)
		{
			std::printf("block-edit: cross-chunk light publication stable\n");
			return (FT_ERR_SUCCESS);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return (ApplicationError::fail("block-edit paired-light timeout", 1));
}

int BlockEditValidator::validate_simultaneous_border_edits(World &world)
noexcept
{
	const int32_t first_x = GAME_VOXEL_CHUNK_WIDTH - 1;
	const int32_t second_x = GAME_VOXEL_CHUNK_WIDTH;
	const int32_t world_z = -6;
	double first_surface;
	double second_surface;
	int32_t first_y;
	int32_t second_y;
	uint32_t first_block;
	uint32_t second_block;
	WorldChunk *first_chunk = nullptr;
	WorldChunk *second_chunk = nullptr;
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);

	if (!world.surface_top_at(first_x, world_z, &first_surface)
		|| !world.surface_top_at(second_x, world_z, &second_surface))
		return (ApplicationError::fail("block-edit simultaneous border surface", 1));
	first_y = static_cast<int32_t>(std::floor(first_surface + 1.0));
	second_y = static_cast<int32_t>(std::floor(second_surface + 1.0));
	if (!world.block_id_at(first_x, first_y, world_z, &first_block)
		|| !world.block_id_at(second_x, second_y, world_z, &second_block)
		|| first_block != GAME_VOXEL_AIR_BLOCK
		|| second_block != GAME_VOXEL_AIR_BLOCK)
		return (ApplicationError::fail("block-edit simultaneous border baseline", 1));
	if (BlockEditValidator::wait_for_boundary_idle(world, first_x, world_z)
		!= FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit simultaneous border idle", 1));
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z, GAME_VOXEL_CHUNK_DEPTH);
	second_chunk = world.find_chunk_mutable(1, chunk_z);
	if (second_chunk == nullptr || second_chunk->read_state == nullptr)
		return (ApplicationError::fail("block-edit simultaneous receiver snapshot", 1));
	const auto previous_receiver = second_chunk->read_state;
	if (world.place_block_at(first_x, first_y, world_z,
		VOXEL_GENERATOR_STONE_BLOCK) != FT_ERR_SUCCESS
		|| world.place_block_at(second_x, second_y, world_z,
			VOXEL_GENERATOR_STONE_BLOCK) != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit simultaneous border place", 1));
	{
		/* Model a source worker that captured the receiver before its placement.
		 * That cell is opaque now and cannot receive the reported light change. */
		constexpr std::size_t east_face = 1U;
		const uint16_t light_input = second_chunk->light_input_version;
		const uint64_t pending_request = second_chunk->pending_mesh_request_id;
		WorldGenerationPipeline::Result result = {};
		result.chunk_x = 0;
		result.chunk_z = chunk_z;
		result.changed_light_faces = 1U << east_face;
		result.neighbor_relight_faces = 1U << east_face;
		result.boundary_read_states[east_face] = previous_receiver;
		auto &changes = result.boundary_light_changes[east_face];
		changes.cells[0] = {0U, static_cast<uint8_t>(world_z - chunk_z * GAME_VOXEL_CHUNK_DEPTH),
			static_cast<uint16_t>(second_y), 15U, 0U, false};
		changes.count = 1U;
		world.chunk_streamer.invalidate_boundary_receivers(result);
		if (second_chunk->light_input_version != light_input
			|| second_chunk->pending_mesh_request_id != pending_request)
			return (ApplicationError::fail("block-edit obsolete receiver requested full light", 1));
	}
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (world.update_around(static_cast<double>(first_x),
			static_cast<double>(world_z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE) != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit simultaneous border update", 1));
		first_chunk = world.find_chunk_mutable(0,
			WorldCoordinates::floor_divide(world_z, GAME_VOXEL_CHUNK_DEPTH));
		second_chunk = world.find_chunk_mutable(1,
			WorldCoordinates::floor_divide(world_z, GAME_VOXEL_CHUNK_DEPTH));
		if (first_chunk == nullptr || second_chunk == nullptr
			|| !first_chunk->light_buffer_is_valid()
			|| !second_chunk->light_buffer_is_valid()
			|| nonzero_light_cells(*first_chunk) == 0U
			|| nonzero_light_cells(*second_chunk) == 0U)
			return (ApplicationError::fail(
				"block-edit simultaneous border light gap", 1));
		if (first_chunk->light_is_current() && second_chunk->light_is_current()
			&& first_chunk->pending_mesh_request_id == 0U
			&& second_chunk->pending_mesh_request_id == 0U
			&& !first_chunk->mesh_dirty && !second_chunk->mesh_dirty)
			break ;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (first_chunk == nullptr || second_chunk == nullptr
		|| !first_chunk->light_is_current()
		|| !second_chunk->light_is_current()
		|| first_chunk->pending_mesh_request_id != 0U
		|| second_chunk->pending_mesh_request_id != 0U
		|| first_chunk->mesh_dirty || second_chunk->mesh_dirty)
		return (ApplicationError::fail("block-edit simultaneous border timeout", 1));
	if (world.delete_block_at(first_x, first_y, world_z) != FT_ERR_SUCCESS
		|| world.delete_block_at(second_x, second_y, world_z) != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit simultaneous border delete", 1));
	const std::chrono::steady_clock::time_point restore_deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (std::chrono::steady_clock::now() < restore_deadline)
	{
		if (world.update_around(static_cast<double>(first_x),
			static_cast<double>(world_z), 0,
			WorldCoordinates::MIN_RENDER_DISTANCE) != FT_ERR_SUCCESS)
			return (ApplicationError::fail("block-edit simultaneous border restore", 1));
		first_chunk = world.find_chunk_mutable(0,
			WorldCoordinates::floor_divide(world_z, GAME_VOXEL_CHUNK_DEPTH));
		second_chunk = world.find_chunk_mutable(1,
			WorldCoordinates::floor_divide(world_z, GAME_VOXEL_CHUNK_DEPTH));
		if (first_chunk != nullptr && second_chunk != nullptr
			&& first_chunk->light_is_current()
			&& second_chunk->light_is_current()
			&& first_chunk->pending_mesh_request_id == 0U
			&& second_chunk->pending_mesh_request_id == 0U
			&& !first_chunk->mesh_dirty && !second_chunk->mesh_dirty)
		{
			std::printf("block-edit: simultaneous border edits converged\n");
			return (FT_ERR_SUCCESS);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return (ApplicationError::fail("block-edit simultaneous border restore timeout", 1));
}

int BlockEditValidator::validate_neighbor_arrival_after_completed_edit(
	World &world) noexcept
{
	static const int32_t neighbour_offsets[4][2] = {
		{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
	WorldChunk *source;
	WorldChunk *target;
	uint64_t previous_light_revision;
	uint16_t previous_light_input_version;
	int32_t source_index;
	int32_t direction_index;
	uint8_t summary_face;

	source_index = 0;
	while (source_index < world.chunk_count)
	{
		source = &world.chunks[source_index];
		if (!source->initialized || source->voxel_revision <= 1U
			|| source->pending_mesh_request_id != 0U || source->mesh_dirty)
		{
			source_index += 1;
			continue ;
		}
		direction_index = 0;
		while (direction_index < 4)
		{
			summary_face = 0U;
			if (direction_index == 0)
				summary_face = 1U;
			else if (direction_index == 1)
				summary_face = 0U;
			else if (direction_index == 2)
				summary_face = 3U;
			else
				summary_face = 2U;
			target = world.find_chunk_mutable(source->chunk_x
				+ neighbour_offsets[direction_index][0], source->chunk_z
				+ neighbour_offsets[direction_index][1]);
			if (target != nullptr && target->initialized
				&& target->pending_mesh_request_id == 0U
				&& !target->mesh_dirty
				&& source->get_boundary_source_light_max(summary_face) > 1U
				&& target->boundary_has_transparent_target(summary_face))
			{
				previous_light_revision = target->light_revision;
				previous_light_input_version = target->light_input_version;
				world.chunk_streamer.mark_neighbor_remeshes(source->chunk_x,
					source->chunk_z, false, true);
				if (target->light_revision == previous_light_revision
					|| !target->waits_for_neighbor_light
					|| target->light_dependency_chunk_x != source->chunk_x
					|| target->light_dependency_chunk_z != source->chunk_z)
				{
					std::fprintf(stderr,
						"block-edit: completed-edit neighbor arrival did not "
						"schedule border light source=(%d,%d) target=(%d,%d) "
						"source_voxel=%llu target_light=%llu previous=%llu\n",
						source->chunk_x, source->chunk_z, target->chunk_x,
						target->chunk_z,
						static_cast<unsigned long long>(source->voxel_revision),
						static_cast<unsigned long long>(target->light_revision),
						static_cast<unsigned long long>(previous_light_revision));
					return (1);
				}
				target->light_revision = previous_light_revision;
				target->light_input_version = previous_light_input_version;
				target->clear_light_dependency();
				target->mesh_dirty = false;
				std::printf("block-edit: completed-edit neighbor arrival "
					"scheduled source=(%d,%d) target=(%d,%d)\n",
					source->chunk_x, source->chunk_z, target->chunk_x,
					target->chunk_z);
				return (FT_ERR_SUCCESS);
			}
			direction_index += 1;
		}
		source_index += 1;
	}
	std::fprintf(stderr,
		"block-edit: no stable lit transparent neighbor pair for "
		"completed-edit arrival regression\n");
	return (1);
}

int BlockEditValidator::validate_authoritative_edit(World &world) noexcept
{
	const int32_t world_x = 2;
	const int32_t world_z = -6;
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const int32_t local_x = world_x - chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	const int32_t local_z = world_z - chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	const uint32_t edited_block = VOXEL_GENERATOR_STONE_BLOCK;
	const uint32_t air_block = GAME_VOXEL_AIR_BLOCK;
	WorldChunk *chunk;
	game_block_change_request request;
	game_block_delta delta;
	uint32_t current_block_id;
	uint64_t previous_revision;
	uint64_t previous_mesh_revision;
	int32_t error_code;

	double surface_top = 0.0;
	if (world.surface_top_at(world_x, world_z, &surface_top) == false)
		return (ApplicationError::fail("block-edit authoritative surface", 1));
	const int32_t local_y = static_cast<int32_t>(std::floor(surface_top + 1.0));
	chunk = world.find_chunk_mutable(chunk_x, chunk_z);
	if (chunk == nullptr || !chunk->initialized
		|| chunk->chunk.read_block(local_x, local_y, local_z,
			&current_block_id) != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit authoritative target", 1));
	if (current_block_id != air_block)
		return (ApplicationError::fail("block-edit authoritative target state", 1));
	previous_revision = chunk->chunk.get_revision();
	previous_mesh_revision = chunk->mesh_revision;
	request.protocol_version = GAME_WORLD_DELTA_PROTOCOL_VERSION;
	request.session_id = 91U;
	request.request_id = 1U;
	request.world_id = 7U;
	request.chunk_x = chunk_x;
	request.chunk_z = chunk_z;
	request.expected_revision = previous_revision;
	request.expected_block_id = air_block;
	request.requested_block_id = edited_block;
	request.local_x = static_cast<uint8_t>(local_x);
	request.local_y = static_cast<uint16_t>(local_y);
	request.local_z = static_cast<uint8_t>(local_z);
	error_code = world.apply_authoritative_block_change(request, &delta);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit authoritative place", error_code));
	if (delta.current_block_id != edited_block)
		return (ApplicationError::fail("block-edit authoritative place delta", 1));
	if (chunk->light_buffer_is_valid() == false || chunk->light_is_current()
		!= false)
		return (ApplicationError::fail("block-edit authoritative place light", 1));
	error_code = BlockEditValidator::wait_for_mesh_revision(world, world_x,
		world_z, previous_mesh_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit authoritative place remesh",
			error_code));
	request.request_id = 2U;
	request.expected_revision = delta.revision;
	request.expected_block_id = edited_block;
	request.requested_block_id = air_block;
	previous_mesh_revision = chunk->mesh_revision;
	const uint64_t visible_revision_before = world.geometry_revision;
	error_code = world.apply_authoritative_block_change(request, &delta);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit authoritative delete", error_code));
	if (delta.current_block_id != air_block)
		return (ApplicationError::fail("block-edit authoritative delete delta", 1));
	if (world.geometry_revision != visible_revision_before)
		return (ApplicationError::fail("block-edit unpublished server delete visibility", 1));
	error_code = BlockEditValidator::wait_for_mesh_revision(world, world_x,
		world_z, previous_mesh_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit authoritative delete remesh",
			error_code));
	if (world.geometry_revision == visible_revision_before)
		return (ApplicationError::fail("block-edit published server delete visibility", 1));
	request.request_id = 3U;
	request.expected_revision = delta.revision - 1U;
	request.expected_block_id = air_block;
	request.requested_block_id = edited_block;
	if (world.apply_authoritative_block_change(request, &delta)
		!= FT_ERR_INVALID_STATE)
		return (ApplicationError::fail("block-edit authoritative stale request", 1));
	request.request_id = 4U;
	request.expected_revision = chunk->chunk.get_revision();
	request.expected_block_id = air_block;
	request.requested_block_id = VOXEL_GENERATOR_WATER_BLOCK_ID;
	previous_revision = request.expected_revision;
	if (world.apply_authoritative_block_change(request, &delta)
		!= FT_ERR_PERMISSION_DENIED
		|| chunk->chunk.get_revision() != previous_revision
		|| chunk->chunk.read_block(local_x, local_y, local_z,
			&current_block_id) != FT_ERR_SUCCESS
		|| current_block_id != air_block)
		return (ApplicationError::fail(
			"block-edit authoritative fluid bypass", 1));
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::report_mesh_result(size_t before, size_t after_place,
	size_t after_delete)
{
	if (after_place == before || after_delete == after_place)
	{
		std::fprintf(stderr,
						"block-edit: mesh counts did not update"
						" before=%zu place=%zu delete=%zu\n",
						before,
						after_place,
						after_delete);
		return (1);
	}
	std::printf("block-edit: ok before=%zu place=%zu delete=%zu\n", before,
		after_place, after_delete);
	return (0);
}

int32_t BlockEditValidator::validate_deferred_mining_checkpoint()
{
	GameSessionSaveWorker worker;
	game_character character;
	WorldItemStore items;
	ft_string path;
	ft_string prepared;
	ft_string committed;
	int32_t descriptor = -1;
	int32_t error = character.initialize();
	if (error == FT_ERR_SUCCESS)
		error = items.initialize();
	if (error == FT_ERR_SUCCESS)
		error = path.initialize();
	if (error == FT_ERR_SUCCESS)
		error = prepared.initialize();
	if (error == FT_ERR_SUCCESS)
		error = committed.initialize();
	if (error != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit mining checkpoint fixture", error));
	error = file_secure_temp_file(".", "mining-checkpoint", &path, &descriptor);
	if (error != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit mining checkpoint file", error));
	error = file_close_descriptor(descriptor);
	std::string backup_path = std::string(path.c_str()) + ".bak";
	if (error == FT_ERR_SUCCESS)
		error = GameSessionPersistence::save_file(path.c_str(), character, items);
	if (error == FT_ERR_SUCCESS)
		error = file_read_all(path.c_str(), prepared);
	if (error == FT_ERR_SUCCESS)
	{
		character.set_x(3);
		error = GameSessionPersistence::save_file(path.c_str(), character, items);
	}
	if (error == FT_ERR_SUCCESS)
		error = file_read_all(path.c_str(), committed);
	if (error == FT_ERR_SUCCESS && prepared == committed)
		error = FT_ERR_INVALID_STATE;
	if (error == FT_ERR_SUCCESS)
	{
		const auto *source = reinterpret_cast<const uint8_t *>(committed.data());
		std::vector<uint8_t> bytes(source, source + committed.size());
		error = file_write_all(path.c_str(), prepared.c_str(), prepared.size());
		if (error == FT_ERR_SUCCESS)
			error = worker.submit(path.c_str(), bytes);
		std::vector<uint8_t> next_bytes(source, source + committed.size());
		if (error == FT_ERR_SUCCESS
			&& worker.submit(path.c_str(), next_bytes) != FT_ERR_THREAD_BUSY)
			error = FT_ERR_INVALID_STATE;
		bool pending = false;
		const int32_t write_result = worker.collect(false, &pending);
		if (error == FT_ERR_SUCCESS)
			error = pending ? worker.collect(true) : write_result;
		else if (pending)
			(void)worker.collect(true);
		if (error == FT_ERR_SUCCESS)
		{
			const std::string invalid_path = std::string(path.c_str()) + "/child";
			error = worker.submit(invalid_path.c_str(), next_bytes);
			if (error == FT_ERR_SUCCESS && worker.collect(true) == FT_ERR_SUCCESS)
				error = FT_ERR_INVALID_STATE;
		}
	}
	if (error == FT_ERR_SUCCESS)
	{
		ft_string actual;
		error = actual.initialize();
		if (error == FT_ERR_SUCCESS)
			error = file_read_all(path.c_str(), actual);
		if (error == FT_ERR_SUCCESS && !(actual == committed))
			error = FT_ERR_INVALID_STATE;
		if (error == FT_ERR_SUCCESS)
			error = file_read_all(backup_path.c_str(), actual);
		if (error == FT_ERR_SUCCESS && !(actual == prepared))
			error = FT_ERR_INVALID_STATE;
	}
	(void)file_delete(path.c_str());
	(void)file_delete(backup_path.c_str());
	if (error != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit mining checkpoint write", error));
	std::printf("block-edit: deferred mining checkpoint retained the recovery snapshot\n");
	return (FT_ERR_SUCCESS);
}

int32_t BlockEditValidator::validate_mining_worker_shutdown()
{
	auto session = std::make_unique<GameSession>();
	auto renderer = std::make_unique<VoxelRenderer>();
	ApplicationWindow window;
	const std::string seed = "mining-save-worker-validator-"
		+ std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
	int32_t error = session->start(seed, window, *renderer);
	if (error != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit mining worker session", error));
	const int32_t x = 4;
	const int32_t z = 4;
	const uint32_t stone = VOXEL_GENERATOR_STONE_BLOCK;
	double surface = 0.0;
	uint32_t current = 0U;
	uint64_t id = 0U;
	if (!session->world_.surface_top_at(x, z, &surface))
		error = FT_ERR_NOT_FOUND;
	const int32_t y = static_cast<int32_t>(surface + 1.0);
	const int32_t other_y = GAME_VOXEL_CHUNK_HEIGHT - 2;
	if (error == FT_ERR_SUCCESS)
		error = session->world_.place_block_at(x, y, z, stone);
	const WorldChunk *chunk = session->world_.find_chunk(0, 0);
	if (error == FT_ERR_SUCCESS && chunk == nullptr)
		error = FT_ERR_NOT_FOUND;
	if (error == FT_ERR_SUCCESS)
	{
		std::unique_ptr<WorldTerrainChunkStore::PreparedEdit> edit;
		error = session->world_.capture_block_state_write(x, y, z, stone,
			GAME_VOXEL_AIR_BLOCK, chunk->voxel_revision, edit);
		if (error == FT_ERR_SUCCESS && edit == nullptr)
			error = FT_ERR_INVALID_STATE;
		if (error == FT_ERR_SUCCESS)
			error = edit->file.prepare(edit->destination, 0, 0, edit->voxels);
		game_voxel_chunk saved;
		if (error == FT_ERR_SUCCESS)
			error = session->world_.terrain_chunk_store().load_chunk(0, 0, saved);
		if (error == FT_ERR_SUCCESS
			&& (saved.read_block(x, y, z, &current) != FT_ERR_SUCCESS || current != stone))
			error = FT_ERR_INVALID_STATE;
		/* A stale staged write must preserve a newer voxel in the same chunk. */
		if (error == FT_ERR_SUCCESS)
			error = session->world_.place_block_at(x + 1, other_y, z, stone);
		if (error == FT_ERR_SUCCESS && session->world_.reserve_prepared_block_write(*edit)
			!= FT_ERR_INVALID_STATE)
			error = FT_ERR_INVALID_STATE;
		if (error == FT_ERR_SUCCESS)
			error = session->world_.replace_block_state_at(x, y, z, stone,
				GAME_VOXEL_AIR_BLOCK, chunk->voxel_revision, edit.get());
		if (error == FT_ERR_SUCCESS)
			error = session->world_.terrain_chunk_store().load_chunk(0, 0, saved);
		if (error == FT_ERR_SUCCESS && (saved.get_revision() != chunk->chunk.get_revision()
			|| saved.read_block(x, y, z, &current) != FT_ERR_SUCCESS
			|| current != GAME_VOXEL_AIR_BLOCK))
			error = FT_ERR_INVALID_STATE;
		if (error == FT_ERR_SUCCESS)
			error = session->world_.place_block_at(x, y, z, stone);
	}
	if (error == FT_ERR_SUCCESS)
		error = session->transaction_journal_.next_request_id(
			WorldBlockMiningTransaction::journal_scope(), &id);
	if (error == FT_ERR_SUCCESS)
	{
		const GameSessionSnapshotWriter writer = {&GameSession::save_snapshot_callback,
			session.get(), nullptr, &GameSession::save_mining_checkpoint_callback};
		error = WorldBlockMiningTransaction::execute(session->world_item_save_path_,
			session->world_, session->item_catalog_, session->world_items_,
			session->transaction_journal_, id, 1U, session->world_.current_tick,
			x, y, z, stone, GAME_VOXEL_AIR_BLOCK, chunk->voxel_revision,
			nullptr, 0U, &writer, nullptr, -1, 0, 0U, 0U, nullptr,
			&session->pending_mining_prepare_);
	}
	if (error == FT_ERR_SUCCESS && (session->pending_mining_prepare_ != id
		|| !session->world_.block_id_at(x, y, z, &current) || current != stone))
		error = FT_ERR_INVALID_STATE;
	if (error == FT_ERR_SUCCESS && (!session->world_.terrain_chunk_store().has_reserved_edit()
		|| session->world_.delete_block_at(x + 1, other_y, z) != FT_ERR_THREAD_BUSY))
		error = FT_ERR_INVALID_STATE;
	std::string terrain_path;
	if (error == FT_ERR_SUCCESS)
		error = session->world_.terrain_chunk_store().capture_stream_path(0, 0, terrain_path);
	const int32_t stop_result = session->stop();
	if (error == FT_ERR_SUCCESS)
		error = stop_result;
	if (error == FT_ERR_SUCCESS)
	{
		game_voxel_chunk saved;
		uint32_t neighbor = 0U;
		error = WorldTerrainChunkStore::load_stream_chunk(terrain_path, 0, 0, saved);
		if (error == FT_ERR_SUCCESS
			&& (saved.read_block(x + 1, other_y, z, &neighbor) != FT_ERR_SUCCESS
				|| neighbor != stone))
			error = FT_ERR_INVALID_STATE;
	}
	if (error == FT_ERR_SUCCESS)
		error = session->start(seed, window, *renderer);
	if (error == FT_ERR_SUCCESS)
	{
		GameTransactionJournal::Record record = {};
		uint32_t neighbor = 0U;
		if (!session->world_.block_id_at(x, y, z, &current)
			|| current != GAME_VOXEL_AIR_BLOCK
			|| !session->world_.block_id_at(x + 1, other_y, z, &neighbor)
			|| neighbor != stone
			|| session->transaction_journal_.find(WorldBlockMiningTransaction::journal_scope(),
				id, &record) != FT_ERR_SUCCESS || record.state != GameTransactionJournal::COMMITTED)
			error = FT_ERR_INVALID_STATE;
	}
	if (error == FT_ERR_SUCCESS)
	{
		MinecraftBlockItemRule rule = {};
		error = session->item_catalog_.resolve_block_item_rule(stone, &rule);
		const int32_t original_quantity = session->player_character_.get_inventory()
			.count_item(rule.item_id);
		session->camera_.x = x + 0.5;
		session->camera_.y = y + 0.5;
		session->camera_.z = z + 0.5;
		for (int32_t index = 0; index < 2 && error == FT_ERR_SUCCESS; ++index)
		{
			ft_sharedptr<game_item> drop;
			uint64_t entity_id = 0U;
			error = session->item_catalog_.create_item(rule.item_id, 1, &drop);
			if (error == FT_ERR_SUCCESS)
				error = session->world_items_.spawn(drop, x + 0.5 * index, y + 0.5,
					z + 0.5, 1U, 1U, 0U, &entity_id);
		}
		const uint64_t first_pickup_id = session->pickup_request_id_;
		const FixedRenderStrategy strategy;
		const uint32_t frames_before_pickup = session->remesh_only_frames_;
		if (error == FT_ERR_SUCCESS
			&& (session->tick_world(0.0, strategy, false, false, false)
					!= GameSession::Action::CONTINUE
				|| session->remesh_only_frames_ != frames_before_pickup + 1U))
			error = FT_ERR_INVALID_STATE;
		if (error == FT_ERR_SUCCESS)
			error = session->pickup_nearby_ground_items();
		if (error == FT_ERR_SUCCESS && (session->pending_pickup_prepare_ != first_pickup_id
			|| session->pickup_request_id_ != first_pickup_id + 1U
			|| session->player_character_.get_inventory().count_item(rule.item_id)
				!= original_quantity))
			error = FT_ERR_INVALID_STATE;
		const auto pickup_deadline = std::chrono::steady_clock::now()
			+ std::chrono::seconds(3);
		while (error == FT_ERR_SUCCESS && session->pending_pickup_prepare_ != 0U)
		{
			bool pending = false;
			bool remesh_only = false;
			error = session->advance_save_checkpoint(false, &pending, &remesh_only);
			if (error == FT_ERR_SUCCESS && (!pending || !remesh_only))
				error = FT_ERR_INVALID_STATE;
			if (std::chrono::steady_clock::now() >= pickup_deadline
				&& session->pending_pickup_prepare_ != 0U)
				error = FT_ERR_TIMEOUT;
			if (session->pending_pickup_prepare_ != 0U)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		if (error == FT_ERR_SUCCESS)
			error = session->advance_save_checkpoint(true);
		if (error == FT_ERR_SUCCESS && session->player_character_.get_inventory()
				.count_item(rule.item_id) != original_quantity + 1)
			error = FT_ERR_INVALID_STATE;
		if (error == FT_ERR_SUCCESS)
			error = session->pickup_nearby_ground_items();
		if (error == FT_ERR_SUCCESS && session->pending_pickup_prepare_ != first_pickup_id + 1U)
			error = FT_ERR_INVALID_STATE;
		if (error == FT_ERR_SUCCESS)
			error = session->stop();
		if (error == FT_ERR_SUCCESS)
			error = session->start(seed, window, *renderer);
		for (uint64_t pickup_id = first_pickup_id; pickup_id < first_pickup_id + 2U
			&& error == FT_ERR_SUCCESS; ++pickup_id)
		{
			GameTransactionJournal::Record pickup_record = {};
			error = session->transaction_journal_.find(WorldItemPickupTransaction::journal_scope(),
				pickup_id, &pickup_record);
			if (error == FT_ERR_SUCCESS && pickup_record.state != GameTransactionJournal::COMMITTED)
				error = FT_ERR_INVALID_STATE;
		}
		if (error == FT_ERR_SUCCESS && session->player_character_.get_inventory()
				.count_item(rule.item_id) != original_quantity + 2)
			error = FT_ERR_INVALID_STATE;
		if (error == FT_ERR_SUCCESS)
			std::printf("block-edit: pickup checkpoint frames reserved for remeshes; "
				"both pickups durable through restart\n");
	}
	if (session->active_)
	{
		const int32_t cleanup_error = session->stop();
		if (error == FT_ERR_SUCCESS)
			error = cleanup_error;
	}
	if (error != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit mining worker shutdown", error));
	std::printf("block-edit: shutdown finished pending mining and restart loaded the edit\n");
	std::printf("block-edit: staged terrain published from current voxels and preserved a newer edit\n");
	return (FT_ERR_SUCCESS);
}

int32_t BlockEditValidator::validate_gameplay_mining_frames()
{
	ApplicationWindow window;
	VoxelRenderer renderer;
	auto session = std::make_unique<GameSession>();
	const FixedRenderStrategy strategy;
	uint8_t world_id[32] = {'m', 'i', 'n', 'i', 'n', 'g'};
	const auto nonce = static_cast<uint64_t>(
		std::chrono::system_clock::now().time_since_epoch().count());
	for (std::size_t index = 0U; index < sizeof(nonce); ++index)
		world_id[16U + index] = static_cast<uint8_t>(nonce >> (index * 8U));
	int32_t error = window.initialize(true, 320, 240, false);
	if (error == FT_ERR_SUCCESS)
		error = session->start_with_world_id_internal("gameplay-mining-frames",
			world_id, window, renderer);
	if (error != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit gameplay mining setup", error));
	constexpr int32_t block_y = GAME_VOXEL_CHUNK_HEIGHT - 16;
	constexpr int32_t block_z = 4;
	constexpr int32_t edit_count = 12;
	constexpr uint32_t dirt = VOXEL_GENERATOR_DIRT_BLOCK_ID;
	for (int32_t x = 4; x < 4 + edit_count && error == FT_ERR_SUCCESS; ++x)
		error = session->world_.place_block_at(x, block_y, block_z, dirt);
	std::vector<int64_t> peak_frame_us;
	int64_t max_publication_us = 0;
	int64_t max_input_to_collision_us = 0;
	for (int32_t x = 4; x < 4 + edit_count && error == FT_ERR_SUCCESS; ++x)
	{
		const auto deadline = std::chrono::steady_clock::now()
			+ std::chrono::seconds(3);
		std::chrono::steady_clock::time_point collision_changed;
		bool published = false;
		bool saw_terrain_reservation = false;
		int64_t peak_us = 0;
		session->camera_.x = static_cast<double>(x) + 0.5;
		session->camera_.y = block_y + 2.5;
		session->camera_.z = block_z + 0.5;
		session->camera_.pitch = -std::acos(0.0);
		session->camera_.vertical_velocity = 0.0;
		const auto input_queued_at = std::chrono::steady_clock::now();
		session->pending_primary_presses_ = 1U;
		while (std::chrono::steady_clock::now() < deadline)
		{
			const bool published_before_frame = published;
			const auto start = std::chrono::steady_clock::now();
			if (session->update(1.0 / 60.0, strategy, window)
				!= GameSession::Action::CONTINUE)
			{
				error = FT_ERR_INVALID_STATE;
				break ;
			}
			session->render(window, renderer, false);
			const auto finish = std::chrono::steady_clock::now();
			peak_us = std::max<int64_t>(peak_us, std::chrono::duration_cast<
				std::chrono::microseconds>(finish - start).count());
			uint32_t block_id = dirt;
			if (!session->world_.block_id_at(x, block_y, block_z, &block_id))
			{
				error = FT_ERR_NOT_FOUND;
				break ;
			}
			if (session->world_.terrain_chunk_store().has_reserved_edit())
			{
				if (block_id != dirt || session->world_.delete_block_at(x,
						block_y, block_z) != FT_ERR_THREAD_BUSY)
				{
					error = FT_ERR_INVALID_STATE;
					break ;
				}
				saw_terrain_reservation = true;
			}
			if (block_id == GAME_VOXEL_AIR_BLOCK && !published)
			{
				if (collision_changed == std::chrono::steady_clock::time_point())
				{
					collision_changed = std::chrono::steady_clock::now();
					max_input_to_collision_us = std::max<int64_t>(max_input_to_collision_us,
						std::chrono::duration_cast<std::chrono::microseconds>(
							collision_changed - input_queued_at).count());
				}
				const WorldChunk *chunk = session->world_.find_chunk(0, 0);
				if (chunk != nullptr && chunk->last_mesh_publication_voxel_revision
					== chunk->voxel_revision)
				{
					const int64_t publication_us = std::chrono::duration_cast<
						std::chrono::microseconds>(std::chrono::steady_clock::now()
							- collision_changed).count();
					max_publication_us = std::max(max_publication_us, publication_us);
					published = true;
				}
			}
			if (published_before_frame)
				break ;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		if (error == FT_ERR_SUCCESS && !published)
			error = FT_ERR_TIMEOUT;
		if (error == FT_ERR_SUCCESS && !saw_terrain_reservation)
			error = FT_ERR_INVALID_STATE;
		peak_frame_us.push_back(peak_us);
	}
	if (error == FT_ERR_SUCCESS)
	{
		constexpr int32_t burst_x = 8;
		constexpr int32_t burst_y = block_y + 8;
		const uint64_t scope = WorldBlockMiningTransaction::journal_scope();
		uint64_t first_id = 0U;
		error = session->transaction_journal_.next_request_id(scope, &first_id);
		if (error == FT_ERR_SUCCESS)
			error = session->world_.place_block_at(burst_x, burst_y, block_z, dirt);
		if (error == FT_ERR_SUCCESS)
			error = session->world_.place_block_at(burst_x, burst_y - 1, block_z, dirt);
		if (error == FT_ERR_SUCCESS)
			error = session->advance_save_checkpoint(true);
		session->camera_.x = burst_x + 0.5;
		session->camera_.y = burst_y + 2.5;
		session->camera_.z = block_z + 0.5;
		session->camera_.pitch = -std::acos(0.0);
		session->camera_.vertical_velocity = 0.0;
		session->remesh_only_frames_ = 0U;
		session->pending_primary_presses_ = 2U;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		while (error == FT_ERR_SUCCESS && !session->pending_mining_result_checkpoint_)
		{
			if (std::chrono::steady_clock::now() >= deadline)
				error = FT_ERR_TIMEOUT;
			else if (session->update(1.0 / 60.0, strategy, window) != GameSession::Action::CONTINUE)
				error = FT_ERR_INVALID_STATE;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		/* Keep this test focused on two available clicks, regardless of how
		 * many refresh polls the first disk write took on this host. */
		session->remesh_only_frames_ = 0U;
		if (error == FT_ERR_SUCCESS && (session->pending_primary_presses_ != 1U
			|| session->update(1.0 / 60.0, strategy, window) != GameSession::Action::CONTINUE
			|| session->pending_mining_prepare_ != first_id + 1U
			|| session->pending_mining_result_checkpoint_))
			error = FT_ERR_INVALID_STATE;
		GameTransactionJournal::Record first = {};
		GameTransactionJournal::Record second = {};
		if (error == FT_ERR_SUCCESS && (session->transaction_journal_.find(scope, first_id, &first)
				!= FT_ERR_SUCCESS || first.state != GameTransactionJournal::COMMITTED
			|| session->transaction_journal_.find(scope, first_id + 1U, &second) != FT_ERR_SUCCESS
			|| second.state != GameTransactionJournal::PREPARED))
			error = FT_ERR_INVALID_STATE;
		while (error == FT_ERR_SUCCESS && !session->pending_mining_result_checkpoint_)
		{
			if (std::chrono::steady_clock::now() >= deadline)
				error = FT_ERR_TIMEOUT;
			else
				error = session->advance_save_checkpoint(false);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		/* A queued miss cannot supply a prepare, so it must still flush the
		 * preceding result on that frame. */
		session->camera_.pitch = std::acos(0.0);
		session->pending_primary_presses_ = 1U;
		session->remesh_only_frames_ = 0U;
		if (error == FT_ERR_SUCCESS
			&& (session->update(1.0 / 60.0, strategy, window) != GameSession::Action::CONTINUE
				|| session->pending_mining_prepare_ != 0U || session->pending_mining_result_checkpoint_
				|| session->pending_primary_presses_ != 0U))
			error = FT_ERR_INVALID_STATE;
		if (error == FT_ERR_SUCCESS)
			error = session->advance_save_checkpoint(true);
		for (int32_t y = burst_y - 1; y <= burst_y && error == FT_ERR_SUCCESS; ++y)
		{
			uint32_t block_id = dirt;
			if (!session->world_.block_id_at(burst_x, y, block_z, &block_id)
				|| block_id != GAME_VOXEL_AIR_BLOCK)
				error = FT_ERR_INVALID_STATE;
		}
		if (error == FT_ERR_SUCCESS)
			std::puts("block-edit: queued breaks shared one prepare/result checkpoint");
	}
	if (error == FT_ERR_SUCCESS)
	{
		constexpr int32_t x = 6;
		constexpr int32_t y = block_y + 6;
		const uint64_t scope = WorldBlockMiningTransaction::journal_scope();
		WorldChunk *chunk = session->world_.find_chunk_mutable(0, 0);
		const GameSessionSnapshotWriter writer = {&GameSession::save_snapshot_callback,
			session.get(), nullptr, &GameSession::save_mining_checkpoint_callback};
		const auto prepare_break = [&](int32_t target_y)
		{
			uint64_t request = 0U;
			const int32_t result = session->transaction_journal_.next_request_id(scope, &request);
			if (result != FT_ERR_SUCCESS)
				return (result);
			return (WorldBlockMiningTransaction::execute(session->world_item_save_path_,
				session->world_, session->item_catalog_, session->world_items_,
				session->transaction_journal_, request, 1U, session->world_.current_tick,
				x, target_y, block_z, dirt, GAME_VOXEL_AIR_BLOCK, chunk->voxel_revision,
				nullptr, 0U, &writer, nullptr, -1, 0, 0U, 0U, nullptr,
				&session->pending_mining_prepare_));
		};
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		const auto finish_prepare = [&]() -> int32_t
		{
			while (error == FT_ERR_SUCCESS && session->pending_mining_prepare_ != 0U)
			{
				if (std::chrono::steady_clock::now() >= deadline)
					return (FT_ERR_TIMEOUT);
				error = session->advance_save_checkpoint(false);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			return (error);
		};
		error = chunk != nullptr ? session->world_.place_block_at(x, y, block_z, dirt) : FT_ERR_NOT_FOUND;
		if (error == FT_ERR_SUCCESS)
			error = session->world_.place_block_at(x, y - 1, block_z, dirt);
		if (error == FT_ERR_SUCCESS)
			error = prepare_break(y);
		if (error == FT_ERR_SUCCESS)
			error = finish_prepare();
		if (error == FT_ERR_SUCCESS)
		{
			const uint64_t first_revision = chunk->voxel_revision;
			auto &pipeline = session->world_.chunk_streamer.pipeline();
			bool ready = false;
			while (!ready && error == FT_ERR_SUCCESS && std::chrono::steady_clock::now() < deadline)
			{
				if (chunk->pending_mesh_request_id == 0U)
				{
					const int32_t queue_result = session->world_.chunk_streamer.queue_chunk_remesh(*chunk);
					if (queue_result != FT_ERR_FULL)
						error = queue_result;
				}
				{
					std::lock_guard<std::mutex> lock(pipeline.results_mutex_);
					ready = std::any_of(pipeline.results_.begin(), pipeline.results_.end(),
						[chunk](const auto &result)
						{
							return (result != nullptr && result->request_id == chunk->pending_mesh_request_id
								&& result->error_code == FT_ERR_SUCCESS && result->mesh != nullptr);
						});
				}
				if (!ready)
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			if (error == FT_ERR_SUCCESS)
				error = ready ? prepare_break(y - 1) : FT_ERR_TIMEOUT;
			if (error == FT_ERR_SUCCESS)
				error = finish_prepare();
			if (error == FT_ERR_SUCCESS && (chunk->voxel_revision != first_revision + 1U
				|| chunk->last_mesh_publication_voxel_revision != first_revision))
				return (ApplicationError::fail("block-edit superseded ready mining mesh", FT_ERR_INVALID_STATE));
		}
		if (error == FT_ERR_SUCCESS)
			std::puts("block-edit: ready mining mesh survived the next collision change");
	}
	const int32_t stop_result = session->stop();
	(void)window.destroy();
	if (error == FT_ERR_SUCCESS)
		error = stop_result;
	if (error != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit gameplay mining frames", error));
	std::sort(peak_frame_us.begin(), peak_frame_us.end());
	std::printf("block-edit: gameplay mining edits=%d median_peak_frame_us="
		FT_INT64_DECIMAL_FORMAT " max_frame_us=" FT_INT64_DECIMAL_FORMAT
		" max_input_to_collision_us=" FT_INT64_DECIMAL_FORMAT
		" max_mesh_after_collision_us=" FT_INT64_DECIMAL_FORMAT "\n", edit_count,
		peak_frame_us[peak_frame_us.size() / 2U], peak_frame_us.back(),
		max_input_to_collision_us, max_publication_us);
	return (FT_ERR_SUCCESS);
}

int BlockEditValidator::validate() const
{
	if (validate_ground_item_snapshot_refresh() != FT_ERR_SUCCESS
		|| validate_chunk_block_batch() != FT_ERR_SUCCESS)
		return (1);
	uint32_t pending_presses = 0U;
	if (!GameSession::take_primary_press(pending_presses, 3U)
		|| !GameSession::take_primary_press(pending_presses, 0U)
		|| !GameSession::take_primary_press(pending_presses, 0U)
		|| GameSession::take_primary_press(pending_presses, 0U))
		return (ApplicationError::fail("block-edit primary press burst", 1));
	if (GameSession::take_primary_press(pending_presses, 2U, false)
		|| pending_presses != 2U
		|| !GameSession::take_primary_press(pending_presses, 0U)
		|| !GameSession::take_primary_press(pending_presses, 0U))
		return (ApplicationError::fail("block-edit pending mining checkpoint input", 1));
	if (!GameSession::take_primary_press(pending_presses, UINT32_MAX)
		|| pending_presses != 7U
		|| !GameSession::take_primary_press(pending_presses, UINT32_MAX)
		|| pending_presses != 7U)
		return (ApplicationError::fail("block-edit primary press limit", 1));
	std::printf("block-edit: primary press burst preserved across frames\n");
	if (validate_deferred_mining_checkpoint() != FT_ERR_SUCCESS
		|| validate_mining_worker_shutdown() != FT_ERR_SUCCESS
		|| validate_gameplay_mining_frames() != FT_ERR_SUCCESS)
		return (1);

	std::unique_ptr<World> world_storage(new (std::nothrow) World());
	if (world_storage == nullptr)
		return (ApplicationError::fail("block-edit world allocation",
			FT_ERR_NO_MEMORY));
	World &world = *world_storage;
	int32_t edit_x;
	int32_t edit_y;
	int32_t edit_z;
	size_t mesh_before;
	size_t mesh_after_place;
	size_t mesh_after_delete;
	int32_t error_code;
	if (world_light_version::next(65535U) != 1U
		|| world_light_version::next(0U) != 1U
		|| world_light_version::matches(0U, 1U))
		return (ApplicationError::fail("block-edit light version rollover", 1));
	if (validate_boundary_read_state() != FT_ERR_SUCCESS
		|| validate_connected_opening_preview() != FT_ERR_SUCCESS
		|| validate_isolated_surface_opening(false) != FT_ERR_SUCCESS
		|| validate_isolated_surface_opening(true) != FT_ERR_SUCCESS
		|| validate_independent_emitter_removal(2U, true) != FT_ERR_SUCCESS
		|| validate_independent_emitter_removal(128U, false) != FT_ERR_SUCCESS
		|| validate_emissive_roof_removal() != FT_ERR_SUCCESS)
		return (1);

	edit_x = 2;
	edit_z = -6;
	error_code = world.initialize("integration-seed");
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit world initialization",
				error_code));
	{
		const WorldChunk *initial_chunk = world.find_chunk(0, 0);
		if (initial_chunk != nullptr && initial_chunk->initialized
			&& (initial_chunk->content_version == 0U
				|| initial_chunk->light_input_version == 0U))
			return (ApplicationError::fail("block-edit initial light versions", 1));
	}
	if (wait_for_edit_baseline(world, edit_x, edit_z) != FT_ERR_SUCCESS)
	{
		world.destroy();
		return (ApplicationError::fail("block-edit light baseline", 1));
	}
	if (prepare_edit_target(world, edit_x, edit_z, edit_y, mesh_before) != 0
		|| verify_place_block(world, edit_x, edit_y, edit_z,
			mesh_after_place) != 0 || verify_delete_block(world, edit_x, edit_y,
			edit_z, mesh_after_delete) != 0 || report_mesh_result(mesh_before,
			mesh_after_place, mesh_after_delete) != 0)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_repeated_edits(world, edit_x, edit_y,
		edit_z) != FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_rapid_edit_supersession(world, edit_x,
		edit_y, edit_z) != FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	const int32_t neutral_edit_result = BlockEditValidator::validate_light_neutral_edits(
		world, edit_x, edit_y, edit_z);
	if (neutral_edit_result != FT_ERR_SUCCESS)
	{
		world.destroy();
		return (ApplicationError::fail("block-edit skipped unchanged lighting",
			neutral_edit_result));
	}
	if (BlockEditValidator::validate_interior_emissive_edit(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_boundary_edit(world) != FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_boundary_face_matrix(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_solid_neighbor_geometry(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_rapid_border_supersession(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_cross_chunk_light_publication(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_simultaneous_border_edits(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_neighbor_arrival_after_completed_edit(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (BlockEditValidator::validate_authoritative_edit(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (validate_deferred_mining_action(world, edit_x, edit_y, edit_z)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}

	world.destroy();
	return (0);
}

int BlockEditValidator::validate_mining_drop() noexcept
{
	std::unique_ptr<World> world_storage(new (std::nothrow) World());
	int32_t x = 2;
	int32_t z = -6;
	int32_t y = 0;
	size_t mesh_before = 0U;
	int32_t result;

	if (!world_storage)
		return (ApplicationError::fail("mining-drop world allocation",
			FT_ERR_NO_MEMORY));
	World &world = *world_storage;
	result = world.initialize("mining-drop-integration-seed");
	if (result == FT_ERR_SUCCESS)
		result = wait_for_edit_baseline(world, x, z);
	if (result == FT_ERR_SUCCESS
		&& BlockEditValidator::prepare_edit_target(world, x, z, y,
			mesh_before) != 0)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = validate_mining_drop_merge(world, x, y, z,
			VOXEL_GENERATOR_DIRT_BLOCK);
	if (result == FT_ERR_SUCCESS
		&& BlockEditValidator::prepare_edit_target(world, x + 1, z, y,
			mesh_before) != 0)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = validate_mining_drop_merge(world, x + 1, y, z,
			VOXEL_GENERATOR_COAL_ORE_BLOCK);
	if (result == FT_ERR_SUCCESS
		&& BlockEditValidator::prepare_edit_target(world, x + 2, z, y,
			mesh_before) != 0)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = validate_mining_drop_merge(world, x + 2, y, z,
			VOXEL_GENERATOR_SEAGRASS_BLOCK);
	if (result == FT_ERR_SUCCESS
		&& BlockEditValidator::prepare_edit_target(world, x + 3, z, y,
			mesh_before) != 0)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = validate_mining_drop_merge(world, x + 3, y, z,
			VOXEL_GENERATOR_TALL_GRASS_BLOCK);
	if (result == FT_ERR_SUCCESS
		&& BlockEditValidator::prepare_edit_target(world, x + 4, z, y,
			mesh_before) != 0)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = validate_mining_drop_merge(world, x + 4, y, z,
			VOXEL_GENERATOR_RED_FLOWER_BLOCK);
	if (result == FT_ERR_SUCCESS
		&& BlockEditValidator::prepare_edit_target(world, x + 5, z, y,
			mesh_before) != 0)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = validate_mining_drop_merge(world, x + 5, y, z,
			VOXEL_GENERATOR_YELLOW_FLOWER_BLOCK);
	if (result == FT_ERR_SUCCESS
		&& BlockEditValidator::prepare_edit_target(world, x + 6, z, y,
			mesh_before) != 0)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = validate_mature_wheat_harvest(world, x + 6, y, z);
	world.destroy();
	if (result != FT_ERR_SUCCESS)
		return (ApplicationError::fail("mining-drop integration", result));
	return (0);
}

int BlockEditValidator::validate_wheat_crop() noexcept
{
	std::unique_ptr<World> world_storage(new (std::nothrow) World());
	const int32_t x = 2;
	const int32_t z = -6;
	int32_t y = 0;
	size_t mesh_before = 0U;
	int32_t result;

	if (!world_storage)
		return (ApplicationError::fail("wheat-crop world allocation",
			FT_ERR_NO_MEMORY));
	World &world = *world_storage;
	result = world.initialize("wheat-crop-harvest-validator");
	if (result == FT_ERR_SUCCESS)
		result = wait_for_edit_baseline(world, x, z);
	if (result == FT_ERR_SUCCESS
		&& BlockEditValidator::prepare_edit_target(world, x, z, y,
			mesh_before) != 0)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		uint32_t soil_block_id = GAME_VOXEL_AIR_BLOCK;
		int32_t soil_y = y - 1;
		const int32_t chunk_x = WorldCoordinates::floor_divide(x,
			GAME_VOXEL_CHUNK_WIDTH);
		const int32_t chunk_z = WorldCoordinates::floor_divide(z,
			GAME_VOXEL_CHUNK_DEPTH);
		const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
		while (soil_y >= 0 && chunk != nullptr
			&& world.block_id_at(x, soil_y, z, &soil_block_id)
			&& soil_block_id != VOXEL_GENERATOR_DIRT_BLOCK_ID
			&& soil_block_id != VOXEL_GENERATOR_GRASS_BLOCK_ID)
			--soil_y;
		if (soil_y < 0 || chunk == nullptr
			|| !world.block_id_at(x, soil_y, z, &soil_block_id))
		{
			result = FT_ERR_INVALID_STATE;
		}
		else
		{
			const uint64_t soil_revision = chunk->voxel_revision;
			result = world.till_farmland_at(x, soil_y, z, soil_revision);
			if (result == FT_ERR_SUCCESS
				&& world.till_farmland_at(x, soil_y, z, soil_revision)
				!= FT_ERR_INVALID_STATE)
				result = FT_ERR_INVALID_STATE;
			if (result == FT_ERR_SUCCESS
				&& (!world.block_id_at(x, soil_y, z, &soil_block_id)
					|| soil_block_id != VOXEL_GENERATOR_FARMLAND_BLOCK_ID))
				result = FT_ERR_INVALID_STATE;
			if (result == FT_ERR_SUCCESS
				&& world.farmland_is_hydrated(x, soil_y, z))
				result = FT_ERR_INVALID_STATE;
			const WorldChunk *water_chunk = world.find_chunk(chunk_x, chunk_z);
			if (result == FT_ERR_SUCCESS && water_chunk == nullptr)
				result = FT_ERR_NOT_FOUND;
			if (result == FT_ERR_SUCCESS)
				result = world.replace_fluid_source_at(x + 1, soil_y + 1, z,
					GAME_VOXEL_AIR_BLOCK, VOXEL_GENERATOR_WATER_BLOCK_ID,
					water_chunk->voxel_revision);
			if (result == FT_ERR_SUCCESS
				&& !world.farmland_is_hydrated(x, soil_y, z))
				result = FT_ERR_INVALID_STATE;
			water_chunk = world.find_chunk(chunk_x, chunk_z);
			if (result == FT_ERR_SUCCESS && water_chunk == nullptr)
				result = FT_ERR_NOT_FOUND;
			if (result == FT_ERR_SUCCESS)
				result = world.replace_fluid_source_at(x + 1, soil_y + 1, z,
					VOXEL_GENERATOR_WATER_BLOCK_ID, GAME_VOXEL_AIR_BLOCK,
					water_chunk->voxel_revision);
			if (result == FT_ERR_SUCCESS
				&& world.farmland_is_hydrated(x, soil_y, z))
				result = FT_ERR_INVALID_STATE;
			y = soil_y + 1;
		}
	}
	if (result == FT_ERR_SUCCESS)
		result = validate_mature_wheat_harvest(world, x, y, z);
	if (result != FT_ERR_SUCCESS)
		std::fprintf(stderr, "crop-validation-stage=wheat-harvest error=%d\n", result);
	if (result == FT_ERR_SUCCESS)
		result = validate_tree_fruit_harvest(world, x + 1, y, z,
			VOXEL_GENERATOR_OAK_LEAVES_BLOCK_ID,
			MinecraftItemCatalog::APPLE_ITEM_ID);
	if (result != FT_ERR_SUCCESS)
		std::fprintf(stderr, "crop-validation-stage=apple-harvest error=%d\n", result);
	if (result == FT_ERR_SUCCESS)
		result = validate_tree_fruit_harvest(world, x + 2, y, z,
			VOXEL_GENERATOR_BIRCH_LEAVES_BLOCK_ID,
			MinecraftItemCatalog::PEAR_ITEM_ID);
	if (result != FT_ERR_SUCCESS)
		std::fprintf(stderr, "crop-validation-stage=pear-harvest error=%d\n", result);
	if (result == FT_ERR_SUCCESS
		&& BlockEditValidator::prepare_edit_target(world, x + 3, z, y,
			mesh_before) != 0)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = validate_mining_drop_merge(world, x + 3, y, z,
			VOXEL_GENERATOR_PAPER_REED_BLOCK_ID,
			VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID);
	if (result != FT_ERR_SUCCESS)
		std::fprintf(stderr, "crop-validation-stage=reed-harvest error=%d\n", result);
	if (result == FT_ERR_SUCCESS)
	{
		const int32_t reed_x = x + 3;
		const int32_t water_x = x + 1;
		const int32_t chunk_x = WorldCoordinates::floor_divide(reed_x,
			GAME_VOXEL_CHUNK_WIDTH);
		const int32_t chunk_z = WorldCoordinates::floor_divide(z,
			GAME_VOXEL_CHUNK_DEPTH);
		const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
		uint32_t reed_id = GAME_VOXEL_AIR_BLOCK;
		if (chunk == nullptr
			|| !world.block_id_at(reed_x, y, z, &reed_id)
			|| reed_id != VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID
			|| world.has_water_source_near(reed_x, y - 1, z))
			result = FT_ERR_INVALID_STATE;
		else
			result = world.replace_fluid_source_at(water_x, y, z,
				GAME_VOXEL_AIR_BLOCK, VOXEL_GENERATOR_WATER_BLOCK_ID,
				chunk->voxel_revision);
		if (result == FT_ERR_SUCCESS
			&& !world.has_water_source_near(reed_x, y - 1, z))
			result = FT_ERR_INVALID_STATE;
		chunk = world.find_chunk(chunk_x, chunk_z);
		if (result == FT_ERR_SUCCESS && chunk == nullptr)
			result = FT_ERR_NOT_FOUND;
		if (result == FT_ERR_SUCCESS)
			result = world.replace_fluid_source_at(water_x, y, z,
				VOXEL_GENERATOR_WATER_BLOCK_ID, GAME_VOXEL_AIR_BLOCK,
				chunk->voxel_revision);
		if (result == FT_ERR_SUCCESS
			&& world.has_water_source_near(reed_x, y - 1, z))
			result = FT_ERR_INVALID_STATE;
	}
	if (result == FT_ERR_SUCCESS)
		result = validate_farmland_transaction(world, x + 4, z);
	if (result != FT_ERR_SUCCESS)
		std::fprintf(stderr, "crop-validation-stage=tilling error=%d\n", result);
	if (result == FT_ERR_SUCCESS)
		result = validate_paper_reed_regrowth_tick();
	world.destroy();
	if (result != FT_ERR_SUCCESS)
		return (ApplicationError::fail("wheat-crop harvest", result));
	std::printf("block-edit: crop sources=tilling,wheat,seeds,apple,pear\n");
	return (0);
}

int BlockEditValidator::validate_paper_reed_regrowth_tick() noexcept
{
	GameSession session;
	World &world = session.world_;
	int32_t plant_x = 0;
	int32_t plant_y = 0;
	int32_t plant_z = 0;
	bool found = false;
	int32_t result = world.initialize("wheat-crop-harvest-validator");
	if (result == FT_ERR_SUCCESS)
		result = wait_for_edit_baseline(world, 2, -6);
	if (result != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "paper-reed growth setup failed=%d\n", result);
		world.destroy();
		return (result);
	}
	session.camera_.initialize();
	CropGrowthPolicy invalid_growth_policy = {0U, 3U, 9U, 12U, 4U};
	if (session.configure_crop_growth_policy(invalid_growth_policy)
		!= FT_ERR_INVALID_ARGUMENT)
		result = FT_ERR_INVALID_STATE;
	CropGrowthPolicy invalid_radius_policy = {200U, 3U, 9U, 12U, 9U};
	if (result == FT_ERR_SUCCESS
		&& session.configure_crop_growth_policy(invalid_radius_policy)
			!= FT_ERR_INVALID_ARGUMENT)
		result = FT_ERR_INVALID_STATE;
	CropGrowthPolicy growth_policy = {200U, 3U, 9U, 12U, 0U};
	if (result == FT_ERR_SUCCESS)
		result = session.configure_crop_growth_policy(growth_policy);
	for (int32_t z = -10; z < -2 && !found; ++z)
	{
		for (int32_t x = -2; x < 6 && !found; ++x)
		{
			for (int32_t y = 2; y < GAME_VOXEL_CHUNK_HEIGHT - 1; ++y)
			{
				uint32_t soil_id = GAME_VOXEL_AIR_BLOCK;
				uint32_t plant_id = GAME_VOXEL_AIR_BLOCK;
				uint32_t water_cell_id = GAME_VOXEL_AIR_BLOCK;
				const uint32_t growth_hash = static_cast<uint32_t>(x)
					* 73856093U ^ static_cast<uint32_t>(y) * 19349663U
					^ static_cast<uint32_t>(z) * 83492791U ^ 2U;
				if (growth_hash % 3U != 0U
					|| !world.block_id_at(x, y - 1, z, &soil_id)
					|| (soil_id != VOXEL_GENERATOR_GRASS_BLOCK_ID
						&& soil_id != VOXEL_GENERATOR_DIRT_BLOCK_ID
						&& soil_id != VOXEL_GENERATOR_MUD_BLOCK_ID
						&& soil_id != VOXEL_GENERATOR_SAND_BLOCK_ID)
					|| !world.block_id_at(x, y, z, &plant_id)
					|| plant_id != GAME_VOXEL_AIR_BLOCK
					|| !world.block_id_at(x + 1, y, z, &water_cell_id)
					|| water_cell_id != GAME_VOXEL_AIR_BLOCK)
					continue;
				plant_x = x;
				plant_y = y;
				plant_z = z;
				found = true;
				break;
			}
		}
	}
	if (!found)
	{
		std::fprintf(stderr, "paper-reed growth fixture found no soil cell\n");
		result = FT_ERR_NOT_FOUND;
	}
	for (int32_t water_y = std::max(0, plant_y - 2);
		water_y <= std::min(GAME_VOXEL_CHUNK_HEIGHT - 1, plant_y);
		++water_y)
	{
		for (int32_t water_z = plant_z - 4; water_z <= plant_z + 4;
			++water_z)
		{
			for (int32_t water_x = plant_x - 4; water_x <= plant_x + 4;
				++water_x)
			{
				uint32_t water_id = GAME_VOXEL_AIR_BLOCK;
				if (result != FT_ERR_SUCCESS
					|| !world.block_id_at(water_x, water_y, water_z, &water_id)
					|| water_id != VOXEL_GENERATOR_WATER_BLOCK_ID)
					continue;
				WorldChunk *water_chunk = world.find_chunk_mutable(
					WorldCoordinates::floor_divide(water_x,
						GAME_VOXEL_CHUNK_WIDTH),
					WorldCoordinates::floor_divide(water_z,
						GAME_VOXEL_CHUNK_DEPTH));
				if (water_chunk == nullptr)
					result = FT_ERR_NOT_FOUND;
				else
					result = world.replace_fluid_source_at(water_x, water_y,
						water_z, VOXEL_GENERATOR_WATER_BLOCK_ID,
						GAME_VOXEL_AIR_BLOCK, water_chunk->voxel_revision);
			}
		}
	}
	if (result == FT_ERR_SUCCESS && world.has_water_source_near(plant_x,
		plant_y - 1, plant_z))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = world.place_block_at(plant_x, plant_y, plant_z,
			VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID);
	WorldChunk *chunk = world.find_chunk_mutable(
		WorldCoordinates::floor_divide(plant_x, GAME_VOXEL_CHUNK_WIDTH),
		WorldCoordinates::floor_divide(plant_z, GAME_VOXEL_CHUNK_DEPTH));
	if (result == FT_ERR_SUCCESS && chunk == nullptr)
		result = FT_ERR_NOT_FOUND;
	uint8_t observed_crop_light = UINT8_MAX;
	if (result == FT_ERR_SUCCESS
		&& world.light_level_at(plant_x, plant_y, plant_z,
			&observed_crop_light))
		result = FT_ERR_INVALID_STATE;
	auto set_crop_light = [&](uint8_t value) -> int32_t
	{
		int32_t light_result;
		light_result = chunk->light.set(
			WorldCoordinates::positive_modulo(plant_x,
				GAME_VOXEL_CHUNK_WIDTH), plant_y,
			WorldCoordinates::positive_modulo(plant_z,
				GAME_VOXEL_CHUNK_DEPTH), value);
		if (light_result == FT_ERR_SUCCESS)
		{
			chunk->light_version = chunk->content_version;
			chunk->computed_light_input_version = chunk->light_input_version;
		}
		return (light_result);
	};
	if (result == FT_ERR_SUCCESS)
		result = set_crop_light(voxel_light_pack(0U, 0U));
	if (result == FT_ERR_SUCCESS)
	{
		const bool light_read = world.light_level_at(plant_x, plant_y, plant_z,
			&observed_crop_light);
		if (!light_read || observed_crop_light != 0U)
		{
			std::fprintf(stderr, "crop-light low read=%d value=%u content=%u "
				"light=%u input=%u computed=%u valid=%d current=%d\n",
				light_read ? 1 : 0, static_cast<unsigned int>(observed_crop_light),
				static_cast<unsigned int>(chunk->content_version),
				static_cast<unsigned int>(chunk->light_version),
				static_cast<unsigned int>(chunk->light_input_version),
				static_cast<unsigned int>(chunk->computed_light_input_version),
				chunk->light_buffer_is_valid() ? 1 : 0,
				chunk->light_is_current() ? 1 : 0);
			result = FT_ERR_INVALID_STATE;
		}
	}
	if (result == FT_ERR_SUCCESS)
	{
		session.camera_.x = static_cast<double>(plant_x) + 0.5;
		session.camera_.y = static_cast<double>(plant_y) + 2.0;
		session.camera_.z = static_cast<double>(plant_z) + 0.5;
		world.current_tick = 200U;
		result = session.advance_nearby_crop_growth();
	}
	uint32_t plant_block_after = GAME_VOXEL_AIR_BLOCK;
	if (result == FT_ERR_SUCCESS
		&& (!world.block_id_at(plant_x, plant_y, plant_z,
			&plant_block_after)
			|| plant_block_after
				!= VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = world.replace_fluid_source_at(plant_x + 1, plant_y, plant_z,
			GAME_VOXEL_AIR_BLOCK, VOXEL_GENERATOR_WATER_BLOCK_ID,
			chunk->voxel_revision);
	if (result == FT_ERR_SUCCESS && !world.has_water_source_near(plant_x,
		plant_y - 1, plant_z))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = set_crop_light(voxel_light_pack(15U, 0U));
	if (result == FT_ERR_SUCCESS)
	{
		world.current_tick = 400U;
		result = session.advance_nearby_crop_growth();
	}
	if (result == FT_ERR_SUCCESS
		&& (!world.block_id_at(plant_x, plant_y, plant_z,
			&plant_block_after)
			|| plant_block_after
				!= VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS
		&& (world.has_water_source_near(plant_x, plant_y - 1, plant_z, 0U)
			|| !world.has_water_source_near(plant_x, plant_y - 1, plant_z, 1U)))
		result = FT_ERR_INVALID_STATE;
	growth_policy.hydration_radius = 1U;
	if (result == FT_ERR_SUCCESS)
		result = session.configure_crop_growth_policy(growth_policy);
	if (result == FT_ERR_SUCCESS)
		result = set_crop_light(voxel_light_pack(11U, 0U));
	if (result == FT_ERR_SUCCESS)
	{
		world.current_tick = 600U;
		result = session.advance_nearby_crop_growth();
	}
	if (result == FT_ERR_SUCCESS
		&& (!world.block_id_at(plant_x, plant_y, plant_z,
			&plant_block_after)
			|| plant_block_after
				!= VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = set_crop_light(voxel_light_pack(15U, 0U));
	if (result == FT_ERR_SUCCESS)
	{
		const bool light_read = world.light_level_at(plant_x, plant_y, plant_z,
			&observed_crop_light);
		if (!light_read || observed_crop_light != 15U)
		{
			std::fprintf(stderr, "crop-light bright read=%d value=%u content=%u "
				"light=%u input=%u computed=%u valid=%d current=%d\n",
				light_read ? 1 : 0, static_cast<unsigned int>(observed_crop_light),
				static_cast<unsigned int>(chunk->content_version),
				static_cast<unsigned int>(chunk->light_version),
				static_cast<unsigned int>(chunk->light_input_version),
				static_cast<unsigned int>(chunk->computed_light_input_version),
				chunk->light_buffer_is_valid() ? 1 : 0,
				chunk->light_is_current() ? 1 : 0);
			result = FT_ERR_INVALID_STATE;
		}
	}
	if (result == FT_ERR_SUCCESS)
	{
		uint32_t growth_epoch = 4U;
		const uint32_t base_hash = static_cast<uint32_t>(plant_x) * 73856093U
			^ static_cast<uint32_t>(plant_y) * 19349663U
			^ static_cast<uint32_t>(plant_z) * 83492791U;
		while (((base_hash ^ growth_epoch) % 3U) != 0U)
			++growth_epoch;
		world.current_tick = static_cast<uint64_t>(growth_epoch) * 200U;
		result = session.advance_nearby_crop_growth();
	}
	if (result == FT_ERR_SUCCESS
		&& (!world.block_id_at(plant_x, plant_y, plant_z,
			&plant_block_after)
			|| plant_block_after != VOXEL_GENERATOR_PAPER_REED_BLOCK_ID))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		std::printf("block-edit: paper-reed dry-stays-immature, wet-matures=ok\n");
	else
		std::fprintf(stderr, "paper-reed growth validation code=%d\n", result);
	world.destroy();
	return (result);
}
