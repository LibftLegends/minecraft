#include "../../src/world/WorldRegenerationResultApplier.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

class WorldRegenerationLootStaging
{
  public:
	WorldRegenerationLootStaging(WorldItemStore *store) noexcept
		: store_(store), entity_ids_(), committed_(false)
	{
	}
	~WorldRegenerationLootStaging()
	{
		if (!committed_)
			(void)this->rollback();
	}
	int32_t stage(const WorldGenerationPipeline::Result &result,
		World &world) noexcept
	{
		std::size_t count = 0U;
		try
		{
			for (const auto &plan : result.applied_feature_plans)
				count += plan.loot_drops.size();
			if (count > 64U)
				return (FT_ERR_FULL);
			entity_ids_.reserve(count);
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		if (count == 0U)
			return (FT_ERR_SUCCESS);
		if (store_ == nullptr || world.item_catalog() == nullptr)
			return (FT_ERR_INVALID_OPERATION);
		for (const auto &plan : result.applied_feature_plans)
		{
			for (const auto &drop : plan.loot_drops)
			{
				if (drop.item_id <= 0 || drop.quantity <= 0
					|| !std::isfinite(drop.x) || !std::isfinite(drop.y)
					|| !std::isfinite(drop.z))
					return (FT_ERR_INVALID_ARGUMENT);
				const double owner_x = std::floor(drop.x
					/ GAME_VOXEL_CHUNK_WIDTH);
				const double owner_z = std::floor(drop.z
					/ GAME_VOXEL_CHUNK_DEPTH);
				if (owner_x < static_cast<double>(result.chunk_x)
					|| owner_x > static_cast<double>(result.chunk_x)
					|| owner_z < static_cast<double>(result.chunk_z)
					|| owner_z > static_cast<double>(result.chunk_z))
					continue ;
				ft_sharedptr<game_item> item;
				int32_t error_code = world.item_catalog()->create_item(
					drop.item_id, drop.quantity, &item);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				uint64_t entity_id = 0U;
				error_code = store_->spawn_pending(item, drop.x, drop.y,
					drop.z, world.current_tick, 0U, 0U, &entity_id);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				entity_ids_.push_back(entity_id);
			}
		}
		return (FT_ERR_SUCCESS);
	}
	int32_t publish() noexcept
	{
		for (uint64_t id : entity_ids_)
		{
			const int32_t error_code = store_->publish_spawn(id, 1U);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
		}
		return (FT_ERR_SUCCESS);
	}
	void commit() noexcept { committed_ = true; }
	int32_t rollback() noexcept
	{
		if (committed_ || entity_ids_.empty())
			return (FT_ERR_SUCCESS);
		if (store_ == nullptr)
			return (FT_ERR_INVALID_STATE);
		int32_t first_error = FT_ERR_SUCCESS;
		for (uint64_t id : entity_ids_)
		{
			const int32_t result = store_->despawn(id, 1U);
			if (first_error == FT_ERR_SUCCESS
				&& result != FT_ERR_SUCCESS && result != FT_ERR_NOT_FOUND)
				first_error = result;
		}
		if (first_error == FT_ERR_SUCCESS)
			entity_ids_.clear();
		return (first_error);
	}

  private:
	WorldItemStore *store_;
	std::vector<uint64_t> entity_ids_;
	bool committed_;
};

static uint32_t world_regeneration_feature_outcome_id(
	const WorldGenerationPipeline::GenerationFeaturePlan &plan,
	int32_t chunk_x, int32_t chunk_z) noexcept
{
	uint64_t hash = 14695981039346656037ULL;
	const uint64_t values[3U] = {plan.host_intent_sequence,
		(static_cast<uint64_t>(plan.ordinal) << 32U) | plan.kind,
		(static_cast<uint64_t>(static_cast<uint32_t>(chunk_x)) << 32U)
			| static_cast<uint32_t>(chunk_z)};
	for (uint64_t value : values)
	{
		for (uint32_t byte_index = 0U; byte_index < 8U; ++byte_index)
		{
			hash ^= static_cast<uint8_t>(value & 0xffU);
			hash *= 1099511628211ULL;
			value >>= 8U;
		}
	}
	const uint32_t outcome_id = static_cast<uint32_t>(hash ^ (hash >> 32U));
	return (outcome_id == 0U ? 1U : outcome_id);
}

static bool world_regeneration_outputs_already_committed(
	const WorldChunkInstabilityRecord &record,
	const std::vector<WorldChunkInstabilityRecord::VersionedId> &outcomes,
	uint64_t relevance_epoch, uint64_t rules_hash) noexcept
{
	if (outcomes.empty() || record.generation_revision < relevance_epoch
		|| record.rules_hash != rules_hash)
		return (false);
	for (const auto &expected : outcomes)
	{
		const bool found = std::any_of(record.feature_outcomes.begin(),
			record.feature_outcomes.end(), [&expected](const auto &stored)
			{
				return (stored.id == expected.id
					&& stored.version == expected.version);
			});
		if (!found)
			return (false);
	}
	return (true);
}

static int32_t world_regeneration_stage_spawners(
	const WorldGenerationPipeline::Result &result, World &world,
	std::vector<WorldSpawnerDefinition> &definitions_out,
	uint64_t *first_spawn_tick_out) noexcept
{
	std::vector<WorldSpawnerDefinition> staged;
	uint32_t maximum_interval = 0U;
	try
	{
		for (const auto &plan : result.applied_feature_plans)
		{
			for (const auto &spawner : plan.spawner_definitions)
			{
				if (!std::isfinite(spawner.x) || !std::isfinite(spawner.y)
					|| !std::isfinite(spawner.z))
					return (FT_ERR_INVALID_ARGUMENT);
				const double chunk_x = std::floor(spawner.x
					/ GAME_VOXEL_CHUNK_WIDTH);
				const double chunk_z = std::floor(spawner.z
					/ GAME_VOXEL_CHUNK_DEPTH);
				if (chunk_x < std::numeric_limits<int32_t>::min()
					|| chunk_x > std::numeric_limits<int32_t>::max()
					|| chunk_z < std::numeric_limits<int32_t>::min()
					|| chunk_z > std::numeric_limits<int32_t>::max())
					return (FT_ERR_OUT_OF_RANGE);
				if (static_cast<int32_t>(chunk_x) != result.chunk_x
					|| static_cast<int32_t>(chunk_z) != result.chunk_z)
					continue ;
				if (staged.size() >= WorldSpawnerStore::MAXIMUM_SPAWNERS)
					return (FT_ERR_FULL);
				WorldSpawnerDefinition definition = {};
				definition.x = spawner.x;
				definition.y = spawner.y;
				definition.z = spawner.z;
				definition.entity_type = spawner.entity_type;
				definition.maximum_alive = spawner.maximum_alive;
				definition.spawn_interval_ticks =
					spawner.spawn_interval_ticks;
				definition.loot_table_id = spawner.loot_table_id;
				staged.push_back(definition);
				maximum_interval = std::max(maximum_interval,
					spawner.spawn_interval_ticks);
			}
		}
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (!staged.empty())
	{
		if (world.spawner_store() == nullptr || world.entity_store() == nullptr)
			return (FT_ERR_INVALID_OPERATION);
		if (maximum_interval == 0U
			|| world.current_tick > UINT64_MAX - maximum_interval)
			return (FT_ERR_OUT_OF_RANGE);
		*first_spawn_tick_out = world.current_tick + maximum_interval;
	}
	definitions_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

WorldRegenerationResultApplier::WorldRegenerationResultApplier()
{
}

WorldRegenerationResultApplier::WorldRegenerationResultApplier(const WorldRegenerationResultApplier &other)
{
	(void)other;
}

WorldRegenerationResultApplier::~WorldRegenerationResultApplier()
{
}

WorldRegenerationResultApplier &WorldRegenerationResultApplier::operator=(const WorldRegenerationResultApplier &other)
{
	(void)other;
	return (*this);
}

void WorldRegenerationResultApplier::apply_chunk(WorldChunkStreamer &streamer,
	World &world, WorldGenerationPipeline::Result &result) noexcept
{
	WorldChunk *chunk;
	WorldChunk replacement;
	WorldChunk previous_chunk;
	WorldChunkInstabilityRecord instability_record = {};
	WorldChunkInstabilityRecord committed_instability_record = {};
	std::vector<WorldChunkInstabilityRecord::VersionedId> feature_outcomes;
	std::vector<WorldSpawnerDefinition> feature_spawners;
	std::vector<uint32_t> published_spawner_ids;
	WorldRegenerationLootStaging loot_staging(world.item_store());
	uint64_t first_spawn_tick = 0U;
	int32_t transfer_error = FT_ERR_SUCCESS;
	bool outputs_already_committed = false;

	chunk = world.find_chunk_mutable(result.chunk_x, result.chunk_z);
	if (chunk == nullptr || !chunk->initialized)
	{
		world.revision_manager.record_regeneration_skipped();
		return ;
	}
	try
	{
		for (const auto &plan : result.applied_feature_plans)
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
					!= WorldGenerationPipeline::GenerationFeaturePlan::SPIDER_NEST
				&& plan.kind
					!= WorldGenerationPipeline::GenerationFeaturePlan::INSTABILITY_OVERLAY)
			{
				world.revision_manager.record_regeneration_error(
					FT_ERR_INVALID_ARGUMENT);
				return ;
			}
			const uint32_t outcome_id =
				world_regeneration_feature_outcome_id(plan,
					result.chunk_x, result.chunk_z);
			const bool already_added = std::any_of(feature_outcomes.begin(),
				feature_outcomes.end(), [&plan, outcome_id](
					const WorldChunkInstabilityRecord::VersionedId &entry)
				{
					return (entry.id == outcome_id
						&& entry.version == plan.kind);
				});
			if (!already_added)
				feature_outcomes.push_back({outcome_id, plan.kind});
		}
	}
	catch (...)
	{
		world.revision_manager.record_regeneration_error(FT_ERR_NO_MEMORY);
		return ;
	}
	if (world.is_chunk_instability_configured())
	{
		transfer_error = world.get_chunk_instability(result.chunk_x,
			result.chunk_z, &instability_record);
		if (transfer_error != FT_ERR_SUCCESS
			|| result.configuration_signature == 0U
			|| result.relevance_epoch == 0U
			|| instability_record.record_revision == UINT64_MAX
			|| instability_record.generation_revision == UINT64_MAX)
		{
			world.revision_manager.record_regeneration_error(
				transfer_error == FT_ERR_SUCCESS ? FT_ERR_OUT_OF_RANGE
					: transfer_error);
			return ;
		}
		outputs_already_committed =
			world_regeneration_outputs_already_committed(instability_record,
				feature_outcomes, result.relevance_epoch,
				world.revision_manager.rules_hash());
	}
	if (!outputs_already_committed)
	{
		transfer_error = world_regeneration_stage_spawners(result, world,
			feature_spawners, &first_spawn_tick);
		if (transfer_error != FT_ERR_SUCCESS)
		{
			world.revision_manager.record_regeneration_error(transfer_error);
			return ;
		}
		transfer_error = loot_staging.stage(result, world);
		if (transfer_error != FT_ERR_SUCCESS)
		{
			world.revision_manager.record_regeneration_error(transfer_error);
			return ;
		}
	}
	/* Build the replacement off to the side.  Regeneration is allowed to fail
	 * (allocation, cancellation, or a malformed worker result); the currently
	 * published chunk, including its light field, must remain drawable until
	 * the complete replacement is ready. */
	transfer_error = replacement.chunk.move(result.chunk->chunk);
	if (transfer_error == FT_ERR_SUCCESS)
		transfer_error = replacement.light.move(result.chunk->light);
	if (transfer_error == FT_ERR_SUCCESS)
		transfer_error = chunk_mesh_initialize(replacement.mesh);
	if (transfer_error == FT_ERR_SUCCESS)
		transfer_error = WorldGenerationResultCommitter::move_mesh(
			replacement.mesh, result.chunk->mesh);
	if (transfer_error != FT_ERR_SUCCESS)
	{
		replacement.destroy();
		world.revision_manager.record_regeneration_error(FT_ERR_NO_MEMORY);
		return ;
	}
	replacement.chunk_x = result.chunk_x;
	replacement.chunk_z = result.chunk_z;
	replacement.world_x = result.chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	replacement.world_z = result.chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	replacement.initialized = true;
	/* The final ownership transfer is now the only operation that can replace
	 * the live chunk.  All worker-owned payloads have already been validated and
	 * transferred into a complete replacement. */
	/* Keep the currently published chunk until both replacement ownership and
	 * its instability/generation record can be committed. If the metadata write
	 * fails, the live chunk must not advertise a generation the durable record
	 * does not describe. */
	if (chunk->move(previous_chunk) != FT_ERR_SUCCESS)
	{
		replacement.destroy();
		world.revision_manager.record_regeneration_error(FT_ERR_NO_MEMORY);
		return ;
	}
	if (chunk->move(replacement) != FT_ERR_SUCCESS)
	{
		if (chunk->move(previous_chunk) != FT_ERR_SUCCESS)
			world.revision_manager.record_regeneration_error(
				FT_ERR_INVALID_STATE);
		else
			world.revision_manager.record_regeneration_error(FT_ERR_NO_MEMORY);
		replacement.destroy();
		return ;
	}
	chunk->chunk_x = result.chunk_x;
	chunk->chunk_z = result.chunk_z;
	chunk->world_x = result.chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	chunk->world_z = result.chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	chunk->initialized = true;
	chunk->mesh_revision += 1U;
	chunk->voxel_revision += 1U;
	chunk->light_revision += 1U;
	chunk->content_version = 1U;
	/* The regeneration worker publishes a mesh paired with a complete local
	 * light field.  Keep that field authoritative immediately; invalidating it
	 * here made the renderer treat a valid regenerated chunk as black until a
	 * second remesh happened to complete. */
	chunk->light_version = 1U;
	chunk->light_input_version = 1U;
	chunk->computed_light_input_version = 1U;
	chunk->light_ready_for_render = result.chunk->light_ready_for_render;
	chunk->last_light_remesh_incremental = FT_FALSE;
	chunk->last_incremental_light_content_version = 0U;
	chunk->last_incremental_light_voxel_revision = 0U;
	chunk->clear_pending_remesh_request();
	chunk->mesh_dirty = true;
	if (chunk->publish_read_state() != FT_ERR_SUCCESS)
	{
		const int32_t loot_rollback = loot_staging.rollback();
		const int32_t restore_result = chunk->move(previous_chunk);
		world.revision_manager.record_regeneration_error(
			restore_result == FT_ERR_SUCCESS
				&& loot_rollback == FT_ERR_SUCCESS ? FT_ERR_NO_MEMORY
				: FT_ERR_INVALID_STATE);
		return ;
	}
	if (!outputs_already_committed && !feature_spawners.empty())
	{
		transfer_error = world.spawner_store()->add_batch(feature_spawners,
			first_spawn_tick, published_spawner_ids);
		if (transfer_error != FT_ERR_SUCCESS)
		{
			const int32_t loot_rollback = loot_staging.rollback();
			const int32_t restore_result = chunk->move(previous_chunk);
			world.revision_manager.record_regeneration_error(
				restore_result == FT_ERR_SUCCESS
					&& loot_rollback == FT_ERR_SUCCESS ? transfer_error
					: FT_ERR_INVALID_STATE);
			return ;
		}
	}
	if (!outputs_already_committed)
		transfer_error = loot_staging.publish();
	if (!outputs_already_committed && transfer_error != FT_ERR_SUCCESS)
	{
		const int32_t loot_rollback = loot_staging.rollback();
		const int32_t spawner_rollback = published_spawner_ids.empty()
			? FT_ERR_SUCCESS
			: world.spawner_store()->remove_batch(published_spawner_ids);
		const int32_t restore_result = chunk->move(previous_chunk);
		world.revision_manager.record_regeneration_error(
			restore_result == FT_ERR_SUCCESS
				&& spawner_rollback == FT_ERR_SUCCESS
				&& loot_rollback == FT_ERR_SUCCESS ? transfer_error
				: FT_ERR_INVALID_STATE);
		return ;
	}
	if (world.is_chunk_instability_configured())
	{
		uint64_t generation_revision = result.relevance_epoch;
		if (generation_revision <= instability_record.generation_revision)
			generation_revision = instability_record.generation_revision + 1U;
		transfer_error = world.record_chunk_generation(
			instability_record.record_revision, result.chunk_x, result.chunk_z,
			generation_revision,
			world.revision_manager.rules_hash(), &committed_instability_record,
			&feature_outcomes);
		if (transfer_error != FT_ERR_SUCCESS)
		{
			const int32_t loot_rollback = loot_staging.rollback();
			const int32_t spawner_rollback = published_spawner_ids.empty()
				? FT_ERR_SUCCESS
				: world.spawner_store()->remove_batch(published_spawner_ids);
			const int32_t restore_result = chunk->move(previous_chunk);
			world.revision_manager.record_regeneration_error(
				restore_result == FT_ERR_SUCCESS
					&& spawner_rollback == FT_ERR_SUCCESS
					&& loot_rollback == FT_ERR_SUCCESS ? transfer_error
					: FT_ERR_INVALID_STATE);
			return ;
		}
	}
	transfer_error = world.checkpoint_generation_commit();
	if (transfer_error != FT_ERR_SUCCESS)
	{
		const int32_t loot_rollback = loot_staging.rollback();
		const int32_t spawner_rollback = published_spawner_ids.empty()
			? FT_ERR_SUCCESS
			: world.spawner_store()->remove_batch(published_spawner_ids);
		int32_t instability_rollback = FT_ERR_SUCCESS;
		if (world.is_chunk_instability_configured())
			instability_rollback = world.restore_chunk_instability_record(
				committed_instability_record.record_revision,
				instability_record);
		const int32_t chunk_restore = chunk->move(previous_chunk);
		world.revision_manager.record_regeneration_error(
			spawner_rollback == FT_ERR_SUCCESS
				&& loot_rollback == FT_ERR_SUCCESS
				&& instability_rollback == FT_ERR_SUCCESS
				&& chunk_restore == FT_ERR_SUCCESS
				? transfer_error : FT_ERR_INVALID_STATE);
		return ;
	}
	loot_staging.commit();
	world.mark_geometry_changed();
	world.revision_manager.record_regeneration_success();
	streamer.mark_neighbor_remeshes(result.chunk_x, result.chunk_z, false, true);
	streamer.deferred_edits_.insert(streamer.deferred_edits_.end(),
		result.deferred_edits.begin(), result.deferred_edits.end());
	if (!result.deferred_edits.empty())
	{
		if (streamer.deferred_edits_sorted_)
			streamer.deferred_sorted_end_ = streamer.deferred_edits_.size()
				- result.deferred_edits.size();
		streamer.deferred_edits_sorted_ = false;
	}
}

int32_t WorldRegenerationResultApplier::commit(WorldChunkStreamer &streamer,
	World &world, WorldGenerationPipeline::Result &result) noexcept
{
	if (!world.revision_manager.is_regenerating_for(result.relevance_epoch))
		return (FT_ERR_SUCCESS);
	world.revision_manager.record_regeneration_completed();
	if (result.error_code != FT_ERR_SUCCESS || result.chunk == nullptr)
	{
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		std::fprintf(stderr,
			"[WorldRevision] regeneration result failed request=%llu "
			"chunk=(%d,%d) error=%d has_chunk=%s relevance=%llu\n",
			static_cast<unsigned long long>(result.request_id), result.chunk_x,
			result.chunk_z, result.error_code,
			result.chunk == nullptr ? "false" : "true",
			static_cast<unsigned long long>(result.relevance_epoch));
	#endif
		world.revision_manager.record_regeneration_error(result.error_code);
	}
	else
		WorldRegenerationResultApplier::apply_chunk(streamer, world, result);
	if (world.revision_manager.all_regeneration_jobs_done())
		return (world.revision_manager.finish_regeneration());
	return (FT_ERR_SUCCESS);
}
