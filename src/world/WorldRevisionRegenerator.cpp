#include "../../src/world/WorldRevisionRegenerator.hpp"
#include "../../src/entities/EntityState.hpp"
#include "../../src/inventory/MinecraftLootCatalog.hpp"
#include "../../src/inventory/MinecraftItemCatalog.hpp"
#include "../../src/cards/RegenerationWorldRules.hpp"
#include <algorithm>
#include <cstdio>
#include <limits>
#include <utility>

static uint32_t world_revision_structure_cache_loot_table(
	uint32_t feature_kind) noexcept
{
	if (feature_kind
		== WorldGenerationPipeline::GenerationFeaturePlan::DESERT_PYRAMID)
		return (MINECRAFT_LOOT_DESERT_PYRAMID_CACHE);
	if (feature_kind
		== WorldGenerationPipeline::GenerationFeaturePlan::ANCIENT_CANOPY)
		return (MINECRAFT_LOOT_ANCIENT_CANOPY_CACHE);
	if (feature_kind
		== WorldGenerationPipeline::GenerationFeaturePlan::SPIDER_NEST)
		return (MINECRAFT_LOOT_SPIDER_NEST_CACHE);
	return (MINECRAFT_LOOT_RUINED_CACHE);
}

static uint64_t world_revision_opportunity_hash(int32_t chunk_x,
	int32_t chunk_z, uint32_t tier) noexcept
{
	uint64_t hash = 14695981039346656037ULL;
	const uint64_t values[2U] = {
		(static_cast<uint64_t>(static_cast<uint32_t>(chunk_x)) << 32U)
			| static_cast<uint32_t>(chunk_z), tier};
	for (uint64_t value : values)
		for (uint32_t byte_index = 0U; byte_index < 8U; ++byte_index)
		{
			hash ^= static_cast<uint8_t>(value & 0xffU);
			hash *= 1099511628211ULL;
			value >>= 8U;
		}
	return (hash == 0U ? 1U : hash);
}

static uint32_t world_revision_opportunity_outcome_id(
	const WorldGenerationPipeline::GenerationFeaturePlan &plan,
	int32_t chunk_x, int32_t chunk_z) noexcept
{
	uint64_t hash = 14695981039346656037ULL;
	const uint64_t values[3U] = {plan.host_intent_sequence,
		(static_cast<uint64_t>(plan.ordinal) << 32U) | plan.kind,
		(static_cast<uint64_t>(static_cast<uint32_t>(chunk_x)) << 32U)
			| static_cast<uint32_t>(chunk_z)};
	for (uint64_t value : values)
		for (uint32_t byte_index = 0U; byte_index < 8U; ++byte_index)
		{
			hash ^= static_cast<uint8_t>(value & 0xffU);
			hash *= 1099511628211ULL;
			value >>= 8U;
		}
	const uint32_t result = static_cast<uint32_t>(hash ^ (hash >> 32U));
	return (result == 0U ? 1U : result);
}

static bool world_revision_has_opportunity_outcome(
	const WorldChunkInstabilityRecord &record,
	const WorldGenerationPipeline::GenerationFeaturePlan &plan) noexcept
{
	const uint32_t outcome_id = world_revision_opportunity_outcome_id(plan,
		record.chunk_x, record.chunk_z);
	for (const auto &outcome : record.feature_outcomes)
		if (outcome.id == outcome_id && outcome.version == plan.kind)
			return (true);
	return (false);
}

static bool world_revision_find_opportunity_ground(
	const World &world, const WorldRevisionManager::RevisionChunk &coordinate,
	int32_t center_x, int32_t center_z, int32_t *ground_y_out) noexcept
{
	const WorldChunk *chunk = world.find_chunk(coordinate.chunk_x,
		coordinate.chunk_z);
	int32_t center_ground = -1;
	int32_t minimum_ground = GAME_VOXEL_CHUNK_HEIGHT;
	int32_t maximum_ground = -1;
	if (chunk == nullptr || !chunk->initialized || ground_y_out == nullptr
		|| center_x < 2 || center_x > GAME_VOXEL_CHUNK_WIDTH - 3
		|| center_z < 2 || center_z > GAME_VOXEL_CHUNK_DEPTH - 3)
		return (false);
	for (int32_t offset_z = -2; offset_z <= 2; ++offset_z)
	{
		for (int32_t offset_x = -2; offset_x <= 2; ++offset_x)
		{
			int32_t ground_y = -1;
			for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 8; y >= 2; --y)
			{
				uint32_t block = GAME_VOXEL_AIR_BLOCK;
				if (chunk->chunk.read_block(center_x + offset_x, y,
					center_z + offset_z, &block) != FT_ERR_SUCCESS)
					return (false);
				if (voxel_block_is_solid(block) != FT_FALSE)
				{
					ground_y = y;
					break ;
				}
				if (block != GAME_VOXEL_AIR_BLOCK)
					return (false);
			}
			if (ground_y < 2 || ground_y + 7 >= GAME_VOXEL_CHUNK_HEIGHT)
				return (false);
			for (int32_t y = ground_y + 1; y <= ground_y + 6; ++y)
			{
				uint32_t block = GAME_VOXEL_AIR_BLOCK;
				if (chunk->chunk.read_block(center_x + offset_x, y,
					center_z + offset_z, &block) != FT_ERR_SUCCESS
					|| block != GAME_VOXEL_AIR_BLOCK)
					return (false);
			}
			minimum_ground = std::min(minimum_ground, ground_y);
			maximum_ground = std::max(maximum_ground, ground_y);
			if (offset_x == 0 && offset_z == 0)
				center_ground = ground_y;
		}
	}
	if (center_ground < 0 || maximum_ground != minimum_ground)
		return (false);
	*ground_y_out = center_ground;
	return (true);
}

static bool world_regeneration_match_site_job(uint32_t job_index,
	const std::vector<uint8_t> &job_types,
	const std::vector<uint8_t> &site_masks,
	std::vector<int32_t> &chunk_jobs,
	std::vector<uint8_t> &visited_chunks) noexcept
{
	for (uint32_t chunk_index = 0U;
		chunk_index < static_cast<uint32_t>(site_masks.size()); ++chunk_index)
	{
		if ((site_masks[chunk_index] & job_types[job_index]) == 0U
			|| visited_chunks[chunk_index] != 0U)
			continue ;
		visited_chunks[chunk_index] = 1U;
		if (chunk_jobs[chunk_index] < 0
			|| world_regeneration_match_site_job(
				static_cast<uint32_t>(chunk_jobs[chunk_index]), job_types,
				site_masks, chunk_jobs, visited_chunks))
		{
			chunk_jobs[chunk_index] = static_cast<int32_t>(job_index);
			return (true);
		}
	}
	return (false);
}

static bool world_regeneration_chunk_is_ocean(const World &world,
	const WorldRevisionManager::RevisionChunk &coordinate,
	int32_t sea_level) noexcept
{
	const WorldChunk *chunk = world.find_chunk(coordinate.chunk_x,
		coordinate.chunk_z);
	const int32_t sample_y = sea_level - 3;
	if (chunk == nullptr || !chunk->initialized || sea_level < 16
		|| sample_y < 1
		|| sample_y >= GAME_VOXEL_CHUNK_HEIGHT)
		return (false);
	for (int32_t z = 2; z <= 14; z += 4)
		for (int32_t x = 2; x <= 14; x += 4)
		{
			uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
			if (chunk->chunk.read_block(x, sample_y, z, &block_id)
				!= FT_ERR_SUCCESS
				|| block_id != VOXEL_GENERATOR_WATER_BLOCK)
				return (false);
		}
	return (true);
}

static bool world_regeneration_chunk_has_desert_surface(const World &world,
	const WorldRevisionManager::RevisionChunk &coordinate) noexcept
{
	const WorldChunk *chunk = world.find_chunk(coordinate.chunk_x,
		coordinate.chunk_z);
	if (chunk == nullptr || !chunk->initialized)
		return (false);
	uint32_t sandy_columns = 0U;
	for (int32_t z = 2; z <= 14; z += 4)
		for (int32_t x = 2; x <= 14; x += 4)
		{
			for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1; y >= 1; --y)
			{
				uint32_t block = GAME_VOXEL_AIR_BLOCK;
				if (chunk->chunk.read_block(x, y, z, &block) != FT_ERR_SUCCESS)
					return (false);
				if (block == GAME_VOXEL_AIR_BLOCK
					|| block == VOXEL_GENERATOR_WATER_BLOCK)
					continue ;
				if (block == VOXEL_GENERATOR_SAND_BLOCK
					|| block == VOXEL_GENERATOR_RED_SAND_BLOCK
					|| block == VOXEL_GENERATOR_SANDSTONE_BLOCK)
					++sandy_columns;
				break ;
			}
		}
	return (sandy_columns >= 12U);
}

static bool world_regeneration_chunk_has_forest_surface(const World &world,
	const WorldRevisionManager::RevisionChunk &coordinate) noexcept
{
	const WorldChunk *chunk = world.find_chunk(coordinate.chunk_x,
		coordinate.chunk_z);
	if (chunk == nullptr || !chunk->initialized)
		return (false);
	uint32_t forest_columns = 0U;
	for (int32_t z = 2; z <= 14; z += 2)
		for (int32_t x = 2; x <= 14; x += 2)
		{
			for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1; y >= 1; --y)
			{
				uint32_t block = GAME_VOXEL_AIR_BLOCK;
				if (chunk->chunk.read_block(x, y, z, &block) != FT_ERR_SUCCESS)
					return (false);
				if (block == VOXEL_GENERATOR_OAK_LEAVES_BLOCK
					|| block == VOXEL_GENERATOR_PINE_LEAVES_BLOCK
					|| block == VOXEL_GENERATOR_BIRCH_LEAVES_BLOCK)
				{
					++forest_columns;
					break ;
				}
				if (block != GAME_VOXEL_AIR_BLOCK)
					break ;
			}
		}
	return (forest_columns >= 4U);
}

static bool world_regeneration_chunk_has_cave_site(const World &world,
	const WorldRevisionManager::RevisionChunk &coordinate) noexcept
{
	const WorldChunk *chunk = world.find_chunk(coordinate.chunk_x,
		coordinate.chunk_z);
	if (chunk == nullptr || !chunk->initialized)
		return (false);
	for (int32_t y = 48; y >= 8; --y)
	{
		uint32_t center = GAME_VOXEL_AIR_BLOCK;
		uint32_t floor = GAME_VOXEL_AIR_BLOCK;
		uint32_t side = GAME_VOXEL_AIR_BLOCK;
		if (chunk->chunk.read_block(8, y, 8, &center) != FT_ERR_SUCCESS
			|| chunk->chunk.read_block(8, y - 1, 8, &floor) != FT_ERR_SUCCESS
			|| chunk->chunk.read_block(9, y, 8, &side) != FT_ERR_SUCCESS)
			return (false);
		if (center == GAME_VOXEL_AIR_BLOCK
			&& side == GAME_VOXEL_AIR_BLOCK
			&& floor != GAME_VOXEL_AIR_BLOCK
			&& floor != VOXEL_GENERATOR_WATER_BLOCK)
			return (true);
	}
	return (false);
}

static double world_regeneration_feature_spawn_y(const World &world,
	const WorldRevisionManager::RevisionChunk &coordinate, bool cave,
	bool elevated) noexcept
{
	const WorldChunk *chunk = world.find_chunk(coordinate.chunk_x,
		coordinate.chunk_z);
	if (chunk == nullptr || !chunk->initialized)
		return (0.0);
	for (int32_t y = cave ? 48 : GAME_VOXEL_CHUNK_HEIGHT - 2;
		y >= (cave ? 8 : 1); --y)
	{
		uint32_t block = GAME_VOXEL_AIR_BLOCK;
		if (chunk->chunk.read_block(8, y, 8, &block) != FT_ERR_SUCCESS)
			return (0.0);
		if (cave)
		{
			uint32_t floor = GAME_VOXEL_AIR_BLOCK;
			uint32_t side = GAME_VOXEL_AIR_BLOCK;
			if (chunk->chunk.read_block(8, y - 1, 8, &floor) != FT_ERR_SUCCESS
				|| chunk->chunk.read_block(9, y, 8, &side) != FT_ERR_SUCCESS)
				return (0.0);
			if (block == GAME_VOXEL_AIR_BLOCK
				&& side == GAME_VOXEL_AIR_BLOCK
				&& floor != GAME_VOXEL_AIR_BLOCK)
				return (static_cast<double>(y));
		}
		else if (voxel_block_is_solid(block) != FT_FALSE)
			return (static_cast<double>(y + (elevated ? 9 : 2)));
	}
	return (0.0);
}

WorldRevisionRegenerator::WorldRevisionRegenerator()
{
}

WorldRevisionRegenerator::WorldRevisionRegenerator(const WorldRevisionRegenerator &other)
{
	(void)other;
}

WorldRevisionRegenerator::~WorldRevisionRegenerator()
{
}

WorldRevisionRegenerator &WorldRevisionRegenerator::operator=(const WorldRevisionRegenerator &other)
{
	(void)other;
	return (*this);
}

int32_t WorldRevisionRegenerator::capture_chunk_snapshot(WorldRevisionManager &manager,
	World &world, WorldChunk &chunk,
	WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
	const WorldGenerationPipeline::WorldChunkSnapshot **source_snapshot) noexcept
{
	int32_t error_code;

	*source_snapshot = nullptr;
	if (manager.mode_ == World::REGEN_FULL)
		return (FT_ERR_SUCCESS);
	error_code = world.chunk_streamer.pipeline().capture_snapshot(chunk,
			nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
			nullptr, snapshot);
	if (error_code != FT_ERR_SUCCESS)
	{
		manager.progress_.record_error(error_code);
		manager.progress_.finish();
		return (error_code);
	}
	*source_snapshot = &snapshot;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionRegenerator::submit_chunk_regeneration(WorldRevisionManager &manager,
	World &world, const WorldRevisionManager::RevisionChunk &entry) noexcept
{
	WorldChunk *chunk;
	WorldGenerationPipeline::WorldChunkSnapshot snapshot;
	const WorldGenerationPipeline::WorldChunkSnapshot *source_snapshot;
	uint64_t request_id;
	int32_t error_code;

	chunk = world.find_chunk_mutable(entry.chunk_x, entry.chunk_z);
	if (chunk == nullptr || manager.chunk_state(entry.chunk_x,
			entry.chunk_z) != World::REVISION_SELECTED)
	{
		manager.progress_.record_skipped();
		return (FT_ERR_SUCCESS);
	}
	error_code = WorldRevisionRegenerator::capture_chunk_snapshot(manager,
			world, *chunk, snapshot, &source_snapshot);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	request_id = world.chunk_streamer.allocate_request_id();
	error_code = world.chunk_streamer.pipeline().submit_generation(request_id,
			world.chunk_streamer.world_epoch(),
			manager.progress_.generation_epoch(), manager.revision_id_,
			entry.chunk_x, entry.chunk_z, world.seed, manager.config_,
			manager.stage_mask_,
			WorldGenerationPipeline::WorldGenerationOperation::REGENERATE,
			source_snapshot);
	if (error_code != FT_ERR_SUCCESS)
	{
		manager.progress_.record_error(error_code);
		manager.progress_.finish();
		world.chunk_streamer.cancel_pending_remeshes();
		return (error_code);
	}
	manager.progress_.set_job_count(manager.progress_.job_count() + 1U);
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionRegenerator::start(WorldRevisionManager &manager,
	World &world) noexcept
{
	std::vector<WorldGenerationPipeline::WorldChunkSnapshot> snapshots;
	std::vector<WorldGenerationPipeline::GenerationSubmission> submissions;
	std::vector<std::vector<WorldGenerationPipeline::GenerationFeaturePlan> >
		features_by_selected_chunk;
	std::vector<WorldRevisionManager::RevisionChunk> staged_selected;
	uint64_t generation_epoch;
	uint32_t maximum_selected_chunks;
	std::size_t village_plan_count = 0U;
	std::size_t magma_plan_count = 0U;
	std::size_t shipwreck_plan_count = 0U;
	bool selection_changed = false;
	int32_t error_code;

	if (!manager.pending_ || manager.progress_.active())
		return (FT_ERR_INVALID_OPERATION);
	for (const auto &plan : manager.feature_plans_)
	{
		if (plan.kind
			!= WorldGenerationPipeline::GenerationFeaturePlan::VILLAGE
			&& plan.kind
				!= WorldGenerationPipeline::GenerationFeaturePlan::SURFACE_MAGMA_POOL
			&& plan.kind
				!= WorldGenerationPipeline::GenerationFeaturePlan::VOLCANO
			&& plan.kind
				!= WorldGenerationPipeline::GenerationFeaturePlan::RUINED_VILLAGE
			&& plan.kind
				!= WorldGenerationPipeline::GenerationFeaturePlan::SHIPWRECK
			&& plan.kind
				!= WorldGenerationPipeline::GenerationFeaturePlan::DESERT_PYRAMID
			&& plan.kind
				!= WorldGenerationPipeline::GenerationFeaturePlan::ANCIENT_CANOPY
			&& plan.kind
				!= WorldGenerationPipeline::GenerationFeaturePlan::SPIDER_NEST)
			return (FT_ERR_INVALID_OPERATION);
	}
	maximum_selected_chunks = manager.cost_policy_configured_
		? manager.cost_policy_.maximum_selected_chunks
		: WorldRevisionManager::HARD_MAX_SELECTED_CHUNKS;
	try
	{
		staged_selected = manager.selected_;
		staged_selected.reserve(staged_selected.size()
			+ manager.rules_target_chunks_.size());
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (const WorldRevisionManager::RevisionChunk &target
		: manager.rules_target_chunks_)
	{
		WorldChunk *target_chunk = world.find_chunk_mutable(target.chunk_x,
			target.chunk_z);
		if (manager.is_chunk_protected(target.chunk_x, target.chunk_z))
			return (FT_ERR_INVALID_OPERATION);
		if (target_chunk == nullptr || !target_chunk->initialized)
			return (FT_ERR_NOT_FOUND);
		if (!WorldRevisionChunkSet::contains(staged_selected,
			target.chunk_x, target.chunk_z))
		{
			if (staged_selected.size() >= maximum_selected_chunks)
				return (FT_ERR_OUT_OF_RANGE);
			try
			{
				staged_selected.push_back(
					{target.chunk_x, target.chunk_z, true});
			}
			catch (...)
			{
				return (FT_ERR_NO_MEMORY);
			}
			selection_changed = true;
		}
	}
	for (const auto &plan : manager.feature_plans_)
	{
		if (plan.kind
			== WorldGenerationPipeline::GenerationFeaturePlan::VILLAGE)
			++village_plan_count;
		else if (plan.kind
			== WorldGenerationPipeline::GenerationFeaturePlan::RUINED_VILLAGE)
			++village_plan_count;
		else if (plan.kind
			== WorldGenerationPipeline::GenerationFeaturePlan::SURFACE_MAGMA_POOL)
			++magma_plan_count;
		else if (plan.kind
			== WorldGenerationPipeline::GenerationFeaturePlan::SHIPWRECK
			|| plan.kind
				== WorldGenerationPipeline::GenerationFeaturePlan::DESERT_PYRAMID
			|| plan.kind
				== WorldGenerationPipeline::GenerationFeaturePlan::ANCIENT_CANOPY
			|| plan.kind
				== WorldGenerationPipeline::GenerationFeaturePlan::SPIDER_NEST)
			++shipwreck_plan_count;
	}
	if (!manager.feature_plans_.empty() && staged_selected.empty())
		return (FT_ERR_NOT_FOUND);
	std::size_t reserved_feature_chunks = village_plan_count
		+ magma_plan_count + shipwreck_plan_count;
	for (const auto &plan : manager.feature_plans_)
	{
		if (plan.kind != WorldGenerationPipeline::GenerationFeaturePlan::VOLCANO)
			continue ;
		if (plan.volcano_footprint_chunks < 9U
			|| plan.volcano_footprint_chunks > 16U)
			return (FT_ERR_CONFIGURATION);
		reserved_feature_chunks += plan.volcano_footprint_chunks
			* plan.volcano_footprint_chunks;
	}
	if (reserved_feature_chunks > staged_selected.size())
		return (FT_ERR_FULL);
	if (selection_changed)
	{
		if (manager.selection_revision_ == UINT64_MAX)
			return (FT_ERR_OUT_OF_RANGE);
		manager.selected_.swap(staged_selected);
		manager.selection_revision_ += 1U;
	}
	generation_epoch = world.chunk_streamer.allocate_request_id();
	manager.progress_.begin(generation_epoch);
	try
	{
		snapshots.reserve(manager.selected_.size());
		submissions.reserve(manager.selected_.size());
		features_by_selected_chunk.resize(manager.selected_.size());
		std::vector<WorldRevisionManager::RevisionChunk> reserved_chunks;
		reserved_chunks.reserve(reserved_feature_chunks);
		for (std::size_t selected_index = 0U;
			selected_index < manager.selected_.size(); ++selected_index)
		{
			const auto &owner = manager.selected_[selected_index];
			WorldChunkInstabilityRecord instability = {};
			error_code = world.get_chunk_instability(owner.chunk_x,
				owner.chunk_z, &instability);
			if (error_code == FT_ERR_NOT_FOUND)
				continue ;
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			const uint32_t danger_band =
				std::min(instability.danger_band, 3U);
			const double base_x = static_cast<double>(owner.chunk_x)
				* GAME_VOXEL_CHUNK_WIDTH;
			const double base_z = static_cast<double>(owner.chunk_z)
				* GAME_VOXEL_CHUNK_DEPTH;
			for (const auto &modifier : instability.permanent_modifiers)
			{
				if (modifier.id
					< static_cast<uint32_t>(REGENERATION_MODIFIER_HOSTILE_CAMP)
					|| modifier.id
						> static_cast<uint32_t>(
							REGENERATION_MODIFIER_RUINED_VILLAGE))
					continue ;
				WorldGenerationPipeline::GenerationFeaturePlan overlay = {};
				overlay.kind = WorldGenerationPipeline::GenerationFeaturePlan::
					INSTABILITY_OVERLAY;
				overlay.host_intent_sequence = modifier.version;
				overlay.ordinal = modifier.id
					- static_cast<uint32_t>(
						REGENERATION_MODIFIER_HOSTILE_CAMP) + 1U;
				const bool tower = overlay.ordinal == 3U;
				const bool shipwreck = overlay.ordinal == 6U;
				const bool surface_structure = overlay.ordinal == 7U
					|| overlay.ordinal == 8U;
				const double surface_spawn_y = surface_structure
					? world_regeneration_feature_spawn_y(world, owner, false, false)
					: 0.0;
				const double spawn_y = shipwreck
					? static_cast<double>(manager.config_.sea_level - 7)
					: (surface_structure ? surface_spawn_y
						: (tower ? 9.0 : 4.0));
				if (spawn_y <= 0.0)
					return (FT_ERR_NOT_FOUND);
				const uint32_t maximum_alive = 1U + danger_band;
				const uint32_t spawn_interval = 300U - danger_band * 50U;
				const uint32_t reward_band =
					std::min(instability.reward_band, 3U);
				const uint32_t cache_table = !shipwreck
					? MINECRAFT_LOOT_RUINED_CACHE
					: (reward_band >= 2U
						? MINECRAFT_LOOT_SHIPWRECK_CACHE_HIGH_REWARD
						: (reward_band == 1U
							? MINECRAFT_LOOT_SHIPWRECK_CACHE_REWARD
							: MINECRAFT_LOOT_SHIPWRECK_CACHE));
				overlay.spawner_definitions.push_back({base_x + 6.5,
					spawn_y, base_z + 8.5,
					static_cast<uint32_t>(EntityState::Kind::ZOMBIE),
					maximum_alive, spawn_interval, MINECRAFT_LOOT_ZOMBIE});
				overlay.spawner_definitions.push_back({base_x + 10.5,
					spawn_y, base_z + 8.5,
					static_cast<uint32_t>(EntityState::Kind::SKELETON),
					maximum_alive, spawn_interval + 60U,
					cache_table});
				overlay.loot_drops.push_back({
					reward_band >= 2U
						? MinecraftItemCatalog::DIAMOND_MATERIAL_ITEM_ID
						: MinecraftItemCatalog::EMERALD_MATERIAL_ITEM_ID,
					static_cast<int32_t>(std::min(reward_band + 1U, 3U)),
					base_x + 8.5, spawn_y, base_z + 8.5});
				features_by_selected_chunk[selected_index].push_back(
					std::move(overlay));
			}
		}
		std::vector<std::size_t> structure_assignments(
			manager.feature_plans_.size(), std::numeric_limits<std::size_t>::max());
		std::vector<uint8_t> structure_job_types;
		std::vector<uint8_t> structure_site_masks;
		std::vector<uint8_t> structure_visited_chunks;
		std::vector<int32_t> structure_chunk_jobs;
		std::vector<std::size_t> structure_plan_indices;
		for (std::size_t plan_index = 0U;
			plan_index < manager.feature_plans_.size(); ++plan_index)
		{
			const auto kind = manager.feature_plans_[plan_index].kind;
			if (kind != WorldGenerationPipeline::GenerationFeaturePlan::DESERT_PYRAMID
				&& kind != WorldGenerationPipeline::GenerationFeaturePlan::ANCIENT_CANOPY
				&& kind != WorldGenerationPipeline::GenerationFeaturePlan::SPIDER_NEST)
				continue ;
			structure_plan_indices.push_back(plan_index);
			structure_job_types.push_back(kind
				== WorldGenerationPipeline::GenerationFeaturePlan::DESERT_PYRAMID
				? 2U : (kind == WorldGenerationPipeline::GenerationFeaturePlan::ANCIENT_CANOPY
					? 4U : 8U));
		}
		std::vector<uint8_t> site_job_types;
		std::vector<uint8_t> site_masks;
		std::vector<uint8_t> site_visited_chunks;
		std::vector<int32_t> site_chunk_jobs;
		std::vector<std::size_t> site_job_chunks;
		std::vector<uint32_t> shipwreck_assignments;
		for (const auto &plan : manager.feature_plans_)
		{
			uint8_t site_type = 0U;
			if (plan.kind
				== WorldGenerationPipeline::GenerationFeaturePlan::SHIPWRECK)
				site_type = 1U;
			else if (plan.kind
				== WorldGenerationPipeline::GenerationFeaturePlan::DESERT_PYRAMID)
				site_type = 2U;
			else if (plan.kind
				== WorldGenerationPipeline::GenerationFeaturePlan::ANCIENT_CANOPY)
				site_type = 4U;
			else if (plan.kind
				== WorldGenerationPipeline::GenerationFeaturePlan::SPIDER_NEST)
				site_type = 8U;
			if (site_type != 0U)
				site_job_types.push_back(site_type);
		}
		site_masks.reserve(manager.selected_.size());
		for (const auto &candidate : manager.selected_)
		{
			uint8_t mask = 0U;
			if (!WorldRevisionChunkSet::contains(reserved_chunks,
				candidate.chunk_x, candidate.chunk_z))
			{
				if (world_regeneration_chunk_is_ocean(world, candidate,
					manager.config_.sea_level))
					mask |= 1U;
				if (world_regeneration_chunk_has_desert_surface(world, candidate))
					mask |= 2U;
				if (world_regeneration_chunk_has_forest_surface(world, candidate))
					mask |= 4U;
				if (world_regeneration_chunk_has_cave_site(world, candidate))
					mask |= 8U;
			}
			site_masks.push_back(mask);
		}
		site_chunk_jobs.assign(manager.selected_.size(), -1);
		site_job_chunks.assign(site_job_types.size(),
			std::numeric_limits<std::size_t>::max());
		site_visited_chunks.assign(manager.selected_.size(), 0U);
		for (uint32_t job_index = 0U;
			job_index < site_job_types.size(); ++job_index)
		{
			std::fill(site_visited_chunks.begin(),
				site_visited_chunks.end(), 0U);
			if (!world_regeneration_match_site_job(job_index, site_job_types,
				site_masks, site_chunk_jobs, site_visited_chunks))
			{
				manager.progress_.record_error(FT_ERR_NOT_FOUND);
				manager.progress_.finish();
				return (FT_ERR_NOT_FOUND);
			}
		}
		for (std::size_t chunk_index = 0U;
			chunk_index < site_chunk_jobs.size(); ++chunk_index)
			if (site_chunk_jobs[chunk_index] >= 0)
				site_job_chunks[static_cast<std::size_t>(
					site_chunk_jobs[chunk_index])] = chunk_index;
		for (std::size_t job_index = 0U;
			job_index < site_job_types.size(); ++job_index)
			if (site_job_types[job_index] == 1U)
				shipwreck_assignments.push_back(static_cast<uint32_t>(
					site_job_chunks[job_index]));
		std::size_t structure_plan_index = 0U;
		std::size_t shipwreck_assignment_index = 0U;
		for (const auto &plan : manager.feature_plans_)
		{
			if (plan.kind
				!= WorldGenerationPipeline::GenerationFeaturePlan::SHIPWRECK)
				continue ;
			const std::size_t selected_index =
				shipwreck_assignment_index < shipwreck_assignments.size()
				? shipwreck_assignments[shipwreck_assignment_index]
				: manager.selected_.size();
			++shipwreck_assignment_index;
			if (selected_index >= manager.selected_.size())
			{
				manager.progress_.record_error(FT_ERR_NOT_FOUND);
				manager.progress_.finish();
				return (FT_ERR_NOT_FOUND);
			}
			auto assigned_plan = plan;
			const WorldRevisionManager::RevisionChunk &owner =
				manager.selected_[selected_index];
			WorldChunkInstabilityRecord instability = {};
			error_code = world.get_chunk_instability(owner.chunk_x,
				owner.chunk_z, &instability);
			if (error_code != FT_ERR_SUCCESS)
			{
				manager.progress_.record_error(error_code);
				manager.progress_.finish();
				return (error_code);
			}
			const double base_x = static_cast<double>(owner.chunk_x)
				* GAME_VOXEL_CHUNK_WIDTH;
			const double base_z = static_cast<double>(owner.chunk_z)
				* GAME_VOXEL_CHUNK_DEPTH;
			const double ship_y = static_cast<double>(
				manager.config_.sea_level - 7);
			const uint32_t danger_band = instability.danger_band > 3U
				? 3U : instability.danger_band;
			const uint32_t maximum_alive = 1U + danger_band;
			const uint32_t spawn_interval = 300U - danger_band * 50U;
			const uint32_t reward_band = instability.reward_band;
			const uint32_t cache_table = reward_band >= 2U
				? MINECRAFT_LOOT_SHIPWRECK_CACHE_HIGH_REWARD
				: (reward_band == 1U
					? MINECRAFT_LOOT_SHIPWRECK_CACHE_REWARD
					: MINECRAFT_LOOT_SHIPWRECK_CACHE);
			assigned_plan.spawner_definitions.push_back({base_x + 6.5,
				ship_y, base_z + 8.5,
				static_cast<uint32_t>(EntityState::Kind::ZOMBIE), maximum_alive,
				spawn_interval, MINECRAFT_LOOT_ZOMBIE});
			assigned_plan.spawner_definitions.push_back({base_x + 10.5,
				ship_y, base_z + 8.5,
				static_cast<uint32_t>(EntityState::Kind::SKELETON), maximum_alive,
				spawn_interval + 60U, cache_table});
			assigned_plan.loot_drops.push_back({
				reward_band >= 2U
					? MinecraftItemCatalog::DIAMOND_MATERIAL_ITEM_ID
					: MinecraftItemCatalog::EMERALD_MATERIAL_ITEM_ID,
				reward_band >= 1U ? 2 : 1,
				base_x + 8.5, static_cast<double>(manager.config_.sea_level - 4),
				base_z + 8.5});
			features_by_selected_chunk[selected_index].push_back(assigned_plan);
			reserved_chunks.push_back(owner);
		}
		std::size_t village_index = 0U;
		for (const auto &plan : manager.feature_plans_)
		{
			if (plan.kind
				!= WorldGenerationPipeline::GenerationFeaturePlan::VILLAGE
				&& plan.kind
					!= WorldGenerationPipeline::GenerationFeaturePlan::RUINED_VILLAGE)
				continue ;
			std::size_t selected_index = 0U;
			error_code = WorldRevisionChunkSet::reserve_next_unoccupied(
				manager.selected_, reserved_chunks, village_index,
				selected_index);
			if (error_code != FT_ERR_SUCCESS)
			{
				manager.progress_.record_error(error_code);
				manager.progress_.finish();
				return (error_code);
			}
			auto assigned_plan = plan;
			if (plan.kind
				== WorldGenerationPipeline::GenerationFeaturePlan::RUINED_VILLAGE)
			{
				const WorldRevisionManager::RevisionChunk &owner =
					manager.selected_[selected_index];
				const double base_x = static_cast<double>(owner.chunk_x)
					* GAME_VOXEL_CHUNK_WIDTH;
				const double base_z = static_cast<double>(owner.chunk_z)
					* GAME_VOXEL_CHUNK_DEPTH;
				assigned_plan.spawner_definitions.push_back({base_x + 6.5,
					5.0, base_z + 8.5, static_cast<uint32_t>(EntityState::Kind::ZOMBIE),
					2U, 200U, MINECRAFT_LOOT_ZOMBIE});
				assigned_plan.spawner_definitions.push_back({base_x + 10.5,
					5.0, base_z + 8.5, static_cast<uint32_t>(EntityState::Kind::SKELETON),
					2U, 260U, MINECRAFT_LOOT_RUINED_CACHE});
				assigned_plan.loot_drops.push_back({
					MinecraftItemCatalog::EMERALD_MATERIAL_ITEM_ID, 1,
					base_x + 8.5, 5.05, base_z + 8.5});
			}
			features_by_selected_chunk[selected_index].push_back(assigned_plan);
			reserved_chunks.push_back(manager.selected_[selected_index]);
		}
		std::fill(structure_assignments.begin(), structure_assignments.end(),
			std::numeric_limits<std::size_t>::max());
		structure_site_masks.assign(manager.selected_.size(), 0U);
		structure_chunk_jobs.assign(manager.selected_.size(), -1);
		structure_visited_chunks.assign(manager.selected_.size(), 0U);
		for (std::size_t chunk_index = 0U;
			chunk_index < manager.selected_.size(); ++chunk_index)
		{
			const auto &candidate = manager.selected_[chunk_index];
			if (WorldRevisionChunkSet::contains(reserved_chunks,
				candidate.chunk_x, candidate.chunk_z))
				continue ;
			if (world_regeneration_chunk_has_desert_surface(world, candidate))
				structure_site_masks[chunk_index] |= 2U;
			if (world_regeneration_chunk_has_forest_surface(world, candidate))
				structure_site_masks[chunk_index] |= 4U;
			if (world_regeneration_chunk_has_cave_site(world, candidate))
				structure_site_masks[chunk_index] |= 8U;
		}
		std::fill(structure_chunk_jobs.begin(), structure_chunk_jobs.end(), -1);
		for (uint32_t job_index = 0U;
			job_index < structure_job_types.size(); ++job_index)
		{
			std::fill(structure_visited_chunks.begin(),
				structure_visited_chunks.end(), 0U);
			if (!world_regeneration_match_site_job(job_index,
				structure_job_types, structure_site_masks,
				structure_chunk_jobs, structure_visited_chunks))
			{
				manager.progress_.record_error(FT_ERR_NOT_FOUND);
				manager.progress_.finish();
				return (FT_ERR_NOT_FOUND);
			}
		}
		for (std::size_t chunk_index = 0U;
			chunk_index < structure_chunk_jobs.size(); ++chunk_index)
			if (structure_chunk_jobs[chunk_index] >= 0)
				structure_assignments[structure_plan_indices[
					static_cast<std::size_t>(structure_chunk_jobs[chunk_index])]]
					= chunk_index;
		structure_plan_index = 0U;
		for (const auto &plan : manager.feature_plans_)
		{
			if (plan.kind
				!= WorldGenerationPipeline::GenerationFeaturePlan::DESERT_PYRAMID
				&& plan.kind
					!= WorldGenerationPipeline::GenerationFeaturePlan::ANCIENT_CANOPY
				&& plan.kind
					!= WorldGenerationPipeline::GenerationFeaturePlan::SPIDER_NEST)
				continue ;
			const std::size_t selected_index =
				structure_assignments[structure_plan_index];
			++structure_plan_index;
			if (selected_index >= manager.selected_.size())
			{
				manager.progress_.record_error(FT_ERR_NOT_FOUND);
				manager.progress_.finish();
				return (FT_ERR_NOT_FOUND);
			}
			auto assigned_plan = plan;
			const auto &owner = manager.selected_[selected_index];
			WorldChunkInstabilityRecord instability = {};
			error_code = world.get_chunk_instability(owner.chunk_x,
				owner.chunk_z, &instability);
			if (error_code != FT_ERR_SUCCESS)
			{
				manager.progress_.record_error(error_code);
				manager.progress_.finish();
				return (error_code);
			}
			const double base_x = static_cast<double>(owner.chunk_x)
				* GAME_VOXEL_CHUNK_WIDTH;
			const double base_z = static_cast<double>(owner.chunk_z)
				* GAME_VOXEL_CHUNK_DEPTH;
			const double spawn_y = world_regeneration_feature_spawn_y(world,
				owner, plan.kind
				== WorldGenerationPipeline::GenerationFeaturePlan::SPIDER_NEST,
				plan.kind
				== WorldGenerationPipeline::GenerationFeaturePlan::ANCIENT_CANOPY);
			if (spawn_y <= 0.0)
			{
				manager.progress_.record_error(FT_ERR_NOT_FOUND);
				manager.progress_.finish();
				return (FT_ERR_NOT_FOUND);
			}
			const uint32_t danger_band = std::min(instability.danger_band, 3U);
			const uint32_t maximum_alive = 1U + danger_band;
			const uint32_t spawn_interval = 300U - danger_band * 50U;
			assigned_plan.spawner_definitions.push_back({base_x + 6.5,
				spawn_y, base_z + 8.5,
			static_cast<uint32_t>(plan.kind
				== WorldGenerationPipeline::GenerationFeaturePlan::SPIDER_NEST
				? EntityState::Kind::SPIDER : EntityState::Kind::ZOMBIE),
			maximum_alive, spawn_interval,
			plan.kind == WorldGenerationPipeline::GenerationFeaturePlan::SPIDER_NEST
				? MINECRAFT_LOOT_SPIDER : MINECRAFT_LOOT_ZOMBIE});
		assigned_plan.spawner_definitions.push_back({base_x + 10.5,
				spawn_y, base_z + 8.5,
			static_cast<uint32_t>(plan.kind
				== WorldGenerationPipeline::GenerationFeaturePlan::SPIDER_NEST
				? EntityState::Kind::SPIDER : EntityState::Kind::SKELETON),
			maximum_alive, spawn_interval + 60U,
			world_revision_structure_cache_loot_table(plan.kind)});
		const uint32_t reward_band = std::min(instability.reward_band, 3U);
		if (plan.kind
			== WorldGenerationPipeline::GenerationFeaturePlan::SPIDER_NEST)
		{
			assigned_plan.loot_drops.push_back({
				MinecraftItemCatalog::SPIDER_EYE_ITEM_ID, 1,
				base_x + 8.5, spawn_y, base_z + 8.5});
			assigned_plan.loot_drops.push_back({
				MinecraftItemCatalog::STRING_ITEM_ID,
				static_cast<int32_t>(std::min(reward_band + 1U, 3U)),
				base_x + 7.5, spawn_y, base_z + 8.5});
		}
		else
			assigned_plan.loot_drops.push_back({
				reward_band >= 2U
					? MinecraftItemCatalog::DIAMOND_MATERIAL_ITEM_ID
					: MinecraftItemCatalog::EMERALD_MATERIAL_ITEM_ID,
				static_cast<int32_t>(std::min(reward_band + 1U, 3U)),
				base_x + 8.5, spawn_y, base_z + 8.5});
		features_by_selected_chunk[selected_index].push_back(assigned_plan);
		reserved_chunks.push_back(owner);
		}
		std::size_t magma_index = 0U;
		for (const auto &plan : manager.feature_plans_)
		{
			if (plan.kind
				!= WorldGenerationPipeline::GenerationFeaturePlan::SURFACE_MAGMA_POOL)
				continue ;
			std::size_t selected_index = 0U;
			error_code = WorldRevisionChunkSet::reserve_next_unoccupied(
				manager.selected_, reserved_chunks, magma_index,
				selected_index);
			if (error_code != FT_ERR_SUCCESS)
			{
				manager.progress_.record_error(error_code);
				manager.progress_.finish();
				return (error_code);
			}
			features_by_selected_chunk[selected_index].push_back(plan);
			reserved_chunks.push_back(manager.selected_[selected_index]);
		}
		for (const auto &source_plan : manager.feature_plans_)
		{
			if (source_plan.kind
				!= WorldGenerationPipeline::GenerationFeaturePlan::VOLCANO)
				continue ;
			auto plan = source_plan;
			int32_t anchor_x;
			int32_t anchor_z;
			error_code = WorldRevisionChunkSet::find_paid_square_anchor(
				manager.selected_,
				reserved_chunks, plan.volcano_footprint_chunks,
				anchor_x, anchor_z);
			if (error_code != FT_ERR_SUCCESS)
			{
				manager.progress_.record_error(error_code);
				manager.progress_.finish();
				return (error_code);
			}
			plan.volcano_anchor_chunk_x = anchor_x;
			plan.volcano_anchor_chunk_z = anchor_z;
			for (uint32_t dz = 0U; dz < plan.volcano_footprint_chunks; ++dz)
			{
				for (uint32_t dx = 0U; dx < plan.volcano_footprint_chunks; ++dx)
				{
					const int32_t x = static_cast<int32_t>(
						static_cast<int64_t>(anchor_x) + dx);
					const int32_t z = static_cast<int32_t>(
						static_cast<int64_t>(anchor_z) + dz);
					for (std::size_t selected_index = 0U;
						selected_index < manager.selected_.size();
						++selected_index)
					{
						if (manager.selected_[selected_index].chunk_x == x
							&& manager.selected_[selected_index].chunk_z == z)
						{
							features_by_selected_chunk[selected_index].push_back(plan);
							reserved_chunks.push_back(
								manager.selected_[selected_index]);
						break ;
						}
					}
				}
			}
		}
		static const int32_t OPPORTUNITY_CENTERS[3U][2U] = {
			{4, 4}, {12, 4}, {8, 12}};
		for (std::size_t selected_index = 0U;
			selected_index < manager.selected_.size(); ++selected_index)
		{
			const auto &owner = manager.selected_[selected_index];
			if (WorldRevisionChunkSet::contains(reserved_chunks,
				owner.chunk_x, owner.chunk_z))
				continue ;
			WorldChunkInstabilityRecord instability = {};
			error_code = world.get_chunk_instability(owner.chunk_x,
				owner.chunk_z, &instability);
			if (error_code == FT_ERR_NOT_FOUND)
				continue ;
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			bool has_overlay = false;
			bool stabilizer_overload = false;
			for (const auto &modifier : instability.permanent_modifiers)
			{
				if (modifier.id >= static_cast<uint32_t>(
						REGENERATION_MODIFIER_HOSTILE_CAMP)
					&& modifier.id <= static_cast<uint32_t>(
						REGENERATION_MODIFIER_RUINED_VILLAGE))
					has_overlay = true;
				if (modifier.id == static_cast<uint32_t>(
						REGENERATION_MODIFIER_STABILIZER_OVERLOAD))
					stabilizer_overload = true;
			}
			if (has_overlay && !stabilizer_overload)
				continue ;
			const uint32_t threshold_opportunity_count = std::min(
				std::min(instability.reward_band, instability.danger_band), 3U);
			const uint32_t opportunity_count = stabilizer_overload
				? std::max(threshold_opportunity_count, 1U)
				: threshold_opportunity_count;
			for (uint32_t tier = 1U; tier <= opportunity_count; ++tier)
			{
				WorldGenerationPipeline::GenerationFeaturePlan opportunity = {};
				const int32_t local_center_x =
					OPPORTUNITY_CENTERS[tier - 1U][0];
				const int32_t local_center_z =
					OPPORTUNITY_CENTERS[tier - 1U][1];
				int32_t ground_y = -1;
				if (!world_revision_find_opportunity_ground(world, owner,
					local_center_x, local_center_z, &ground_y))
					continue ;
				const uint64_t opportunity_hash =
					world_revision_opportunity_hash(owner.chunk_x,
						owner.chunk_z, tier);
				opportunity.kind = WorldGenerationPipeline::GenerationFeaturePlan::
					INSTABILITY_OVERLAY;
				opportunity.host_intent_sequence = opportunity_hash;
				const uint32_t variant = 1U + static_cast<uint32_t>(
					(opportunity_hash >> 17U) % 5U);
				opportunity.ordinal = 100U + tier * 10U + variant;
				const bool reward_available =
					!world_revision_has_opportunity_outcome(instability,
						opportunity);
				const double spawn_y = static_cast<double>(ground_y) + 1.5;
				const uint32_t reward_band = std::min(
					std::max(instability.reward_band,
						stabilizer_overload ? 1U : 0U), 3U);
				const uint32_t danger_band = std::min(
					std::max(instability.danger_band,
						stabilizer_overload ? 1U : 0U), 3U);
				const uint32_t maximum_alive = 1U + danger_band;
				const uint32_t spawn_interval = 300U - danger_band * 50U;
				const double base_x = static_cast<double>(owner.chunk_x)
					* GAME_VOXEL_CHUNK_WIDTH;
				const double base_z = static_cast<double>(owner.chunk_z)
					* GAME_VOXEL_CHUNK_DEPTH;
				const double site_x = static_cast<double>(local_center_x);
				const double site_z = static_cast<double>(local_center_z);
				if (reward_available)
				{
					opportunity.spawner_definitions.push_back({
						base_x + site_x - 0.5, spawn_y, base_z + site_z + 0.5,
						static_cast<uint32_t>(EntityState::Kind::ZOMBIE),
						maximum_alive, spawn_interval, MINECRAFT_LOOT_ZOMBIE});
					opportunity.spawner_definitions.push_back({
						base_x + site_x + 0.5, spawn_y, base_z + site_z + 0.5,
						static_cast<uint32_t>(EntityState::Kind::SKELETON),
						maximum_alive, spawn_interval + 60U,
						MINECRAFT_LOOT_RUINED_CACHE});
					opportunity.loot_drops.push_back({
						reward_band >= 2U
							? MinecraftItemCatalog::DIAMOND_MATERIAL_ITEM_ID
							: MinecraftItemCatalog::EMERALD_MATERIAL_ITEM_ID,
						static_cast<int32_t>(tier),
						base_x + site_x + 0.5,
						static_cast<double>(ground_y) + 1.05,
						base_z + site_z + 0.5});
				}
				features_by_selected_chunk[selected_index].push_back(
					std::move(opportunity));
			}
		}
		for (std::size_t feature_index = 0U;
			feature_index < manager.feature_plans_.size(); ++feature_index)
		{
			const auto &plan = manager.feature_plans_[feature_index];
			if (plan.kind
				== WorldGenerationPipeline::GenerationFeaturePlan::VILLAGE
				|| plan.kind
					== WorldGenerationPipeline::GenerationFeaturePlan::SURFACE_MAGMA_POOL
				|| plan.kind
					== WorldGenerationPipeline::GenerationFeaturePlan::RUINED_VILLAGE
				|| plan.kind
					== WorldGenerationPipeline::GenerationFeaturePlan::SHIPWRECK
				|| plan.kind
					== WorldGenerationPipeline::GenerationFeaturePlan::VOLCANO
				|| plan.kind
					== WorldGenerationPipeline::GenerationFeaturePlan::DESERT_PYRAMID
				|| plan.kind
					== WorldGenerationPipeline::GenerationFeaturePlan::ANCIENT_CANOPY
				|| plan.kind
					== WorldGenerationPipeline::GenerationFeaturePlan::SPIDER_NEST)
				continue ;
			features_by_selected_chunk[feature_index % manager.selected_.size()]
				.push_back(plan);
		}
	}
	catch (...)
	{
		manager.progress_.record_error(FT_ERR_NO_MEMORY);
		manager.progress_.finish();
		return (FT_ERR_NO_MEMORY);
	}
	std::size_t selected_index = 0U;
	for (const WorldRevisionManager::RevisionChunk &entry : manager.selected_)
	{
		const std::size_t current_selected_index = selected_index++;
		WorldChunk *chunk = world.find_chunk_mutable(entry.chunk_x,
			entry.chunk_z);
		const WorldGenerationPipeline::WorldChunkSnapshot *source_snapshot;
		WorldGenerationPipeline::WorldChunkSnapshot *snapshot;
		uint64_t request_id;

		if (chunk == nullptr || manager.chunk_state(entry.chunk_x,
				entry.chunk_z) != World::REVISION_SELECTED)
		{
			if (!features_by_selected_chunk[current_selected_index].empty())
			{
				manager.progress_.record_error(FT_ERR_NOT_FOUND);
				manager.progress_.finish();
				world.chunk_streamer.cancel_pending_remeshes();
				return (FT_ERR_NOT_FOUND);
			}
			manager.progress_.record_skipped();
			continue ;
		}
		if (world.is_chunk_instability_configured())
		{
			error_code = world.ensure_chunk_instability_record(
				entry.chunk_x, entry.chunk_z);
			if (error_code != FT_ERR_SUCCESS)
			{
				manager.progress_.record_error(error_code);
				manager.progress_.finish();
				world.chunk_streamer.cancel_pending_remeshes();
				return (error_code);
			}
		}
		source_snapshot = nullptr;
		if (manager.mode_ != World::REGEN_FULL)
		{
			try
			{
				snapshots.emplace_back();
			}
			catch (...)
			{
				manager.progress_.record_error(FT_ERR_NO_MEMORY);
				manager.progress_.finish();
				return (FT_ERR_NO_MEMORY);
			}
			snapshot = &snapshots.back();
			error_code = WorldRevisionRegenerator::capture_chunk_snapshot(
				manager, world, *chunk, *snapshot, &source_snapshot);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
		}
		request_id = world.chunk_streamer.allocate_request_id();
		try
		{
			submissions.push_back({request_id,
				world.chunk_streamer.world_epoch(), generation_epoch,
				manager.revision_id_, entry.chunk_x, entry.chunk_z, world.seed,
				&manager.config_, manager.stage_mask_,
				WorldGenerationPipeline::WorldGenerationOperation::REGENERATE,
				source_snapshot,
				&features_by_selected_chunk[current_selected_index]});
		}
		catch (...)
		{
			manager.progress_.record_error(FT_ERR_NO_MEMORY);
			manager.progress_.finish();
			return (FT_ERR_NO_MEMORY);
		}
	}
	manager.progress_.set_job_count(submissions.size());
	if (submissions.empty())
		return (WorldRevisionRegenerator::finish(manager, world));
	error_code = world.chunk_streamer.pipeline().submit_generation_batch(
		submissions);
	if (error_code != FT_ERR_SUCCESS)
	{
		manager.progress_.record_error(error_code);
		manager.progress_.finish();
		return (error_code);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionRegenerator::commit_voxel_config(WorldRevisionManager &manager,
	World &world) noexcept
{
	int32_t error_code;

	error_code = world.voxel_config.initialize(manager.config_);
	if (error_code == FT_ERR_SUCCESS)
	{
		(void)world.voxel_context.destroy();
		error_code = voxel_generation_context_initialize(world.voxel_context,
				world.voxel_config);
	}
	return (error_code);
}

int32_t WorldRevisionRegenerator::finish(WorldRevisionManager &manager,
	World &world) noexcept
{
	int32_t generation_error;
	int32_t error_code;

	if (!manager.progress_.active())
		return (manager.progress_.error());
	generation_error = manager.progress_.error();
	manager.progress_.finish();
	if (generation_error != FT_ERR_SUCCESS)
	{
		manager.pending_ = false;
		return (generation_error);
	}
	error_code = WorldRevisionRegenerator::commit_voxel_config(manager,
			world);
	if (error_code != FT_ERR_SUCCESS)
	{
		manager.pending_ = false;
		return (error_code);
	}
	manager.revision_id_ += 1U;
	manager.pending_ = false;
	manager.selected_.clear();
	manager.rules_target_chunks_.clear();
	manager.feature_plans_.clear();
	manager.barriers_active_ = false;
	world.chunk_streamer.cancel_pending_remeshes();
	world.chunk_streamer.bump_generation_revision();
	world.chunk_streamer.reset_candidates_after_regeneration();
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionRegenerator::regenerate_selected_chunks(WorldRevisionManager &manager,
	World &world, int32_t *regenerated_count, int32_t *skipped_count) noexcept
{
	int32_t error_code;

	if (regenerated_count == nullptr || skipped_count == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = WorldRevisionRegenerator::start(manager, world);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	while (manager.progress_.active())
	{
		error_code = WorldGenerationResultCommitter::drain(world.chunk_streamer,
				world);
		if (error_code != FT_ERR_SUCCESS)
		{
		#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
			std::fprintf(stderr,
				"[WorldRevision] drain failed error=%d jobs=%zu progress_error=%d\n",
				error_code, manager.progress_.job_count(),
				manager.progress_.error());
		#endif
			break ;
		}
		if (manager.progress_.active())
			std::this_thread::yield();
	}
	*regenerated_count = manager.progress_.regenerated_count();
	*skipped_count = manager.progress_.skipped_count();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (manager.progress_.error());
}
