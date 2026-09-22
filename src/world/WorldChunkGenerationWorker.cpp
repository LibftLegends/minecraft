#include "../../src/world/WorldChunkGenerationWorker.hpp"
#include <chrono>
#include <cstdio>
#include <utility>

namespace
{
	bool remesh_request_is_cancelled(
		const WorldGenerationPipeline::Request &request) noexcept
	{
		if (request.cancellation_token == nullptr)
			return (false);
		return (request.cancellation_token->load(std::memory_order_acquire)
			!= request.cancellation_token_value);
	}

	bool incremental_request_is_cancelled(
		const std::shared_ptr<std::atomic<uint64_t>> &cancellation_token,
		uint64_t cancellation_token_value) noexcept
	{
		if (cancellation_token == nullptr)
			return (false);
		return (cancellation_token->load(std::memory_order_acquire)
			!= cancellation_token_value);
	}

	using incremental_light_node =
		WorldGenerationPipeline::IncrementalLightNode;

	using incremental_removal_node =
		WorldGenerationPipeline::IncrementalRemovalNode;

	struct incremental_light_lookup_context
	{
		voxel_light_chunk *light;
		const WorldGenerationPipeline::WorldChunkSnapshot *snapshot;
		int32_t chunk_x;
		int32_t chunk_z;
	};

	int32_t collect_incremental_light_deltas(
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		const voxel_light_chunk &light,
		std::vector<WorldGenerationPipeline::IncrementalLightDelta> &deltas,
		ft_bool *complete) noexcept
	{
		if (complete == nullptr
			|| snapshot.existing_light.size() < static_cast<std::size_t>(
				GAME_VOXEL_CHUNK_WIDTH) * GAME_VOXEL_CHUNK_HEIGHT
				* GAME_VOXEL_CHUNK_DEPTH)
			return (FT_ERR_INVALID_ARGUMENT);
		*complete = FT_FALSE;
		try
		{
			deltas.clear();
			deltas.reserve(256U);
			for (int32_t local_z = 0;
				local_z < GAME_VOXEL_CHUNK_DEPTH; ++local_z)
			{
				for (int32_t local_y = 0;
					local_y < GAME_VOXEL_CHUNK_HEIGHT; ++local_y)
				{
					for (int32_t local_x = 0;
						local_x < GAME_VOXEL_CHUNK_WIDTH; ++local_x)
					{
						const std::size_t index = (static_cast<std::size_t>(local_z)
							* GAME_VOXEL_CHUNK_HEIGHT + local_y)
							* GAME_VOXEL_CHUNK_WIDTH + local_x;
						const uint8_t value = light.get(local_x, local_y, local_z);
						if (value == snapshot.existing_light[index])
							continue ;
						WorldGenerationPipeline::IncrementalLightDelta delta;

						delta.set(static_cast<uint8_t>(local_x),
							static_cast<uint16_t>(local_y),
							static_cast<uint8_t>(local_z), value);
						deltas.push_back(delta);
					}
				}
			}
		}
		catch (...)
		{
			deltas.clear();
			return (FT_ERR_NO_MEMORY);
		}
		*complete = FT_TRUE;
		return (FT_ERR_SUCCESS);
	}

	/* Incremental propagation stores only the cells reached by the frontier.
	 * It is therefore not a complete light field and must never be used as the
	 * source for a replacement mesh directly: every untouched cell would look
	 * like zero light for one publication.  Reconstruct the complete field from
	 * the immutable snapshot and then overlay the computed deltas. */
	int32_t build_incremental_mesh_light(
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		const std::vector<WorldGenerationPipeline::IncrementalLightDelta> &deltas,
		voxel_light_chunk &mesh_light) noexcept
	{
		int32_t error_code;
		std::size_t index;

		if (snapshot.existing_light.size() < static_cast<std::size_t>(
			GAME_VOXEL_CHUNK_WIDTH) * GAME_VOXEL_CHUNK_HEIGHT
			* GAME_VOXEL_CHUNK_DEPTH)
			return (FT_ERR_INVALID_ARGUMENT);
		error_code = mesh_light.initialize();
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		index = 0U;
		for (int32_t local_z = 0;
			local_z < GAME_VOXEL_CHUNK_DEPTH; ++local_z)
		{
			for (int32_t local_y = 0;
				local_y < GAME_VOXEL_CHUNK_HEIGHT; ++local_y)
			{
				for (int32_t local_x = 0;
					local_x < GAME_VOXEL_CHUNK_WIDTH; ++local_x)
				{
					uint8_t light_value = snapshot.existing_light[index];

					/* Generated meshes use direct-sky fallback for exposed columns even
					 * when the persisted/authoritative light buffer is still empty.
					 * Reproduce that fallback in the complete temporary field so an
					 * incremental edit cannot publish a mostly-dark replacement. */
					if (light_value == 0U)
					{
						error_code = WorldChunkSnapshotReader::lookup_snapshot_light(
							const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(
								&snapshot), snapshot.chunk_x * GAME_VOXEL_CHUNK_WIDTH
								+ local_x, local_y,
								snapshot.chunk_z * GAME_VOXEL_CHUNK_DEPTH + local_z,
								&light_value);
						if (error_code != FT_ERR_SUCCESS)
							return (error_code);
					}
					error_code = mesh_light.set(local_x, local_y, local_z,
						light_value);
					if (error_code != FT_ERR_SUCCESS)
						return (error_code);
					index += 1U;
				}
			}
		}
		for (const WorldGenerationPipeline::IncrementalLightDelta &delta : deltas)
		{
			error_code = mesh_light.set(delta.local_x(), delta.local_y(),
				delta.local_z(), delta.packed_light());
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
		}
		return (FT_ERR_SUCCESS);
	}

	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	static std::size_t count_light_nonzero(const voxel_light_chunk &light) noexcept
	{
		std::size_t count = 0U;
		for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
			for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
				for (int32_t x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
					if (light.get(x, y, z) != 0U)
						count += 1U;
		return (count);
	}

	static std::size_t count_snapshot_light_nonzero(
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot) noexcept
	{
		std::size_t count = 0U;
		for (std::size_t index = 0U; index < snapshot.existing_light.size();
			++index)
			if (snapshot.existing_light[index] != 0U)
				count += 1U;
		return (count);
	}
	#endif

	int32_t lookup_incremental_light(void *user_data, int32_t world_x,
		int32_t world_y, int32_t world_z, uint8_t *packed_light) noexcept
	{
		incremental_light_lookup_context *context =
			static_cast<incremental_light_lookup_context *>(user_data);
		int32_t local_x;
		int32_t local_z;

		if (context == nullptr || context->light == nullptr
			|| packed_light == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		local_x = world_x - context->chunk_x * GAME_VOXEL_CHUNK_WIDTH;
		local_z = world_z - context->chunk_z * GAME_VOXEL_CHUNK_DEPTH;
		if (local_x < 0 || local_x >= GAME_VOXEL_CHUNK_WIDTH
			|| local_z < 0 || local_z >= GAME_VOXEL_CHUNK_DEPTH
			|| world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT)
		{
			if (context->snapshot == nullptr)
				return (FT_ERR_OUT_OF_RANGE);
			return (WorldChunkSnapshotReader::lookup_snapshot_light(
				const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(
					context->snapshot), world_x, world_y, world_z, packed_light));
		}
		*packed_light = context->light->get(local_x, world_y, local_z);
		if (*packed_light == 0U && context->snapshot != nullptr)
		{
			int32_t fallback_error =
				WorldChunkSnapshotReader::lookup_snapshot_light(
					const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(
						context->snapshot), world_x, world_y, world_z,
					packed_light);
			if (fallback_error != FT_ERR_SUCCESS)
				return (fallback_error);
		}
		return (FT_ERR_SUCCESS);
	}

	int32_t lookup_local_light_block(void *user_data, int32_t world_x,
		int32_t world_y, int32_t world_z, uint32_t *block_id) noexcept
	{
		game_voxel_chunk *chunk = static_cast<game_voxel_chunk *>(user_data);
		if (chunk == nullptr || block_id == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		world_x %= GAME_VOXEL_CHUNK_WIDTH;
		world_z %= GAME_VOXEL_CHUNK_DEPTH;
		if (world_x < 0)
			world_x += GAME_VOXEL_CHUNK_WIDTH;
		if (world_z < 0)
			world_z += GAME_VOXEL_CHUNK_DEPTH;
		return (chunk->read_block(world_x, world_y, world_z, block_id));
	}

	bool snapshot_column_has_direct_sky(
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		int32_t chunk_x, int32_t chunk_z, int32_t local_x, int32_t local_y,
		int32_t local_z) noexcept
	{
		int32_t y = local_y;

		while (y < GAME_VOXEL_CHUNK_HEIGHT)
		{
			uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
			const voxel_block_metadata *metadata;

			if (WorldChunkSnapshotReader::lookup_snapshot_block(
				const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
				chunk_x * GAME_VOXEL_CHUNK_WIDTH + local_x, y,
				chunk_z * GAME_VOXEL_CHUNK_DEPTH + local_z, &block_id)
				!= FT_ERR_SUCCESS)
				return (false);
			metadata = &voxel_get_block_metadata(block_id);
			if (metadata->light_attenuation >= 15U
				|| metadata->transparent == FT_FALSE)
				return (false);
			y += 1;
		}
		return (true);
	}

	uint8_t snapshot_sky_after_block_change(
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		int32_t chunk_x, int32_t chunk_z, int32_t local_x, int32_t local_y,
		int32_t local_z, uint32_t new_block_id) noexcept
	{
		const voxel_block_metadata *metadata =
			&voxel_get_block_metadata(new_block_id);
		uint8_t source_sky;
		uint8_t attenuation;

		if (metadata->occludes_faces != FT_FALSE
			|| metadata->light_attenuation >= 15U)
			return (0U);
		attenuation = metadata->light_attenuation;
		if (local_y + 1 >= GAME_VOXEL_CHUNK_HEIGHT)
			source_sky = 15U;
		else if (WorldChunkSnapshotReader::lookup_snapshot_light(
			const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
			chunk_x * GAME_VOXEL_CHUNK_WIDTH + local_x, local_y + 1,
			chunk_z * GAME_VOXEL_CHUNK_DEPTH + local_z, &source_sky)
			!= FT_ERR_SUCCESS)
			return (0U);
		source_sky = voxel_light_sky(source_sky);
		return (source_sky > attenuation
			? static_cast<uint8_t>(source_sky - attenuation) : 0U);
	}

	uint8_t snapshot_external_source_light(
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		int32_t chunk_x, int32_t chunk_z, int32_t local_x, int32_t local_y,
		int32_t local_z, uint32_t new_block_id) noexcept
	{
		int32_t world_x = chunk_x * GAME_VOXEL_CHUNK_WIDTH + local_x;
		int32_t world_z = chunk_z * GAME_VOXEL_CHUNK_DEPTH + local_z;
		int32_t y = local_y + 1;
		bool direct_sky = true;
		const voxel_block_metadata *new_metadata =
			&voxel_get_block_metadata(new_block_id);

		if (local_x == 0)
			world_x -= 1;
		else if (local_x == GAME_VOXEL_CHUNK_WIDTH - 1)
			world_x += 1;
		else if (local_z == 0)
			world_z -= 1;
		else if (local_z == GAME_VOXEL_CHUNK_DEPTH - 1)
			world_z += 1;
		else
			return (voxel_light_pack(0U,
				voxel_block_emitted_light_level(new_block_id)));
		if (new_metadata->light_attenuation >= 15U
			|| new_metadata->transparent == FT_FALSE)
			return (voxel_light_pack(0U,
				voxel_block_emitted_light_level(new_block_id)));
		while (y < GAME_VOXEL_CHUNK_HEIGHT)
		{
			uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
			const voxel_block_metadata *metadata;

			if (WorldChunkSnapshotReader::lookup_snapshot_block(
				const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
				world_x, y, world_z, &block_id) != FT_ERR_SUCCESS)
			{
				direct_sky = false;
				break ;
			}
			metadata = &voxel_get_block_metadata(block_id);
			if (metadata->light_attenuation >= 15U
				|| metadata->transparent == FT_FALSE)
			{
				direct_sky = false;
				break ;
			}
			y += 1;
		}
		return (voxel_light_pack(direct_sky ? 15U : 0U,
			voxel_block_emitted_light_level(new_block_id)));
	}

	int32_t build_incremental_additive_light(voxel_light_chunk &light,
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		int32_t chunk_x, int32_t chunk_z, int32_t changed_x,
		int32_t changed_y, int32_t changed_z, uint64_t *processed,
		ft_bool preserve_existing,
		const std::shared_ptr<std::atomic<uint64_t>> &cancellation_token,
		uint64_t cancellation_token_value,
		std::vector<incremental_light_node> &queue,
		std::size_t *queue_index, ft_bool *state_initialized,
		uint32_t node_budget, ft_bool *complete, ft_bool external_source,
		uint32_t external_new_block_id) noexcept
	{
		std::size_t index;
		int32_t local_z;
		uint32_t processed_this_call;

		if (processed == nullptr || changed_x < 0
			|| changed_x >= GAME_VOXEL_CHUNK_WIDTH || changed_z < 0
			|| changed_z >= GAME_VOXEL_CHUNK_DEPTH || changed_y < 0
			|| changed_y >= GAME_VOXEL_CHUNK_HEIGHT
			|| snapshot.existing_light.size()
			< static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_DEPTH)
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT))
			return (FT_ERR_INVALID_ARGUMENT);
		if (queue_index == nullptr || state_initialized == nullptr
			|| complete == nullptr || node_budget == 0U)
			return (FT_ERR_INVALID_ARGUMENT);
		if (*state_initialized == FT_FALSE)
		{
			if (preserve_existing == FT_FALSE)
			{
				if (light.initialize(0U) != FT_ERR_SUCCESS)
					return (FT_ERR_NO_MEMORY);
				local_z = 0;
				while (local_z < GAME_VOXEL_CHUNK_DEPTH)
				{
					int32_t local_y = 0;
					while (local_y < GAME_VOXEL_CHUNK_HEIGHT)
					{
						int32_t local_x = 0;
						while (local_x < GAME_VOXEL_CHUNK_WIDTH)
						{
							index = (static_cast<std::size_t>(local_z)
								* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
								+ static_cast<std::size_t>(local_y))
								* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
								+ static_cast<std::size_t>(local_x);
							if (light.set(local_x, local_y, local_z,
								snapshot.existing_light[index]) != FT_ERR_SUCCESS)
								return (FT_ERR_NO_MEMORY);
							local_x += 1;
						}
						local_y += 1;
					}
					local_z += 1;
				}
			}
			if (snapshot_column_has_direct_sky(snapshot, chunk_x, chunk_z,
			changed_x, changed_y, changed_z))
			{
				const uint8_t current = light.get(changed_x, changed_y, changed_z);
				if (light.set(changed_x, changed_y, changed_z,
					voxel_light_pack(15U, voxel_light_block(current)))
					!= FT_ERR_SUCCESS)
					return (FT_ERR_NO_MEMORY);
			}
			{
			static const int32_t delta_x[6] = {1, -1, 0, 0, 0, 0};
			static const int32_t delta_y[6] = {0, 0, 1, -1, 0, 0};
			static const int32_t delta_z[6] = {0, 0, 0, 0, 1, -1};
			uint32_t changed_block_id = GAME_VOXEL_AIR_BLOCK;
			const voxel_block_metadata *metadata;
			uint8_t direction = 0U;

			if (WorldChunkSnapshotReader::lookup_snapshot_block(
				const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
				chunk_x * GAME_VOXEL_CHUNK_WIDTH + changed_x, changed_y,
				chunk_z * GAME_VOXEL_CHUNK_DEPTH + changed_z,
				&changed_block_id) != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_OPERATION);
			metadata = &voxel_get_block_metadata(changed_block_id);
			while (direction < 6U)
			{
				const int32_t neighbour_x = changed_x + delta_x[direction];
				const int32_t neighbour_y = changed_y + delta_y[direction];
				const int32_t neighbour_z = changed_z + delta_z[direction];
				if (neighbour_x >= 0 && neighbour_x < GAME_VOXEL_CHUNK_WIDTH
					&& neighbour_y >= 0
					&& neighbour_y < GAME_VOXEL_CHUNK_HEIGHT
					&& neighbour_z >= 0
					&& neighbour_z < GAME_VOXEL_CHUNK_DEPTH)
				{
					direction += 1U;
					continue ;
				}
				uint8_t neighbour_light = 0U;
				uint8_t cost = metadata->light_attenuation;
				if (WorldChunkSnapshotReader::lookup_snapshot_light(
					const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
					chunk_x * GAME_VOXEL_CHUNK_WIDTH + neighbour_x, neighbour_y,
					chunk_z * GAME_VOXEL_CHUNK_DEPTH + neighbour_z,
					&neighbour_light) == FT_ERR_SUCCESS)
				{
					if (external_source != FT_FALSE)
						neighbour_light = snapshot_external_source_light(snapshot,
							chunk_x, chunk_z, changed_x, changed_y, changed_z,
							external_new_block_id);
					if (cost < 1U)
						cost = 1U;
					const uint8_t candidate_sky =
						voxel_light_sky(neighbour_light) > cost
						? static_cast<uint8_t>(voxel_light_sky(neighbour_light) - cost)
						: 0U;
					const uint8_t candidate_block =
						voxel_light_block(neighbour_light) > cost
						? static_cast<uint8_t>(voxel_light_block(neighbour_light) - cost)
						: 0U;
					const uint8_t current = light.get(changed_x, changed_y,
						changed_z);
					const uint8_t result_sky = candidate_sky
						> voxel_light_sky(current) ? candidate_sky
						: voxel_light_sky(current);
					const uint8_t result_block = candidate_block
						> voxel_light_block(current) ? candidate_block
						: voxel_light_block(current);
					if (light.set(changed_x, changed_y, changed_z,
						voxel_light_pack(result_sky, result_block))
						!= FT_ERR_SUCCESS)
						return (FT_ERR_NO_MEMORY);
				}
				direction += 1U;
			}
		}
			try
		{
			queue.clear();
			queue.reserve(64U);
			queue.push_back({changed_x, changed_y, changed_z});
			queue.push_back({changed_x - 1, changed_y, changed_z});
			queue.push_back({changed_x + 1, changed_y, changed_z});
			queue.push_back({changed_x, changed_y - 1, changed_z});
			queue.push_back({changed_x, changed_y + 1, changed_z});
			queue.push_back({changed_x, changed_y, changed_z - 1});
			queue.push_back({changed_x, changed_y, changed_z + 1});
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
			*queue_index = 0U;
			*state_initialized = FT_TRUE;
		}
		index = *queue_index;
		processed_this_call = 0U;
		*processed = 0U;
		while (index < queue.size() && processed_this_call < node_budget)
		{
			if ((index & 255U) == 0U
				&& incremental_request_is_cancelled(cancellation_token,
					cancellation_token_value))
				return (FT_ERR_INVALID_STATE);
			const incremental_light_node node = queue[index];
			static const int32_t delta_x[6] = {1, -1, 0, 0, 0, 0};
			static const int32_t delta_y[6] = {0, 0, 1, -1, 0, 0};
			static const int32_t delta_z[6] = {0, 0, 0, 0, 1, -1};
			uint8_t direction = 0U;
			if (node.x < 0 || node.x >= GAME_VOXEL_CHUNK_WIDTH
				|| node.y < 0 || node.y >= GAME_VOXEL_CHUNK_HEIGHT
				|| node.z < 0 || node.z >= GAME_VOXEL_CHUNK_DEPTH)
			{
				index += 1U;
				continue ;
			}
			const uint8_t source = light.get(node.x, node.y, node.z);

			while (direction < 6U)
			{
				const int32_t x = node.x + delta_x[direction];
				const int32_t y = node.y + delta_y[direction];
				const int32_t z = node.z + delta_z[direction];
				uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
				uint8_t destination;
				uint8_t candidate_sky;
				uint8_t candidate_block;
				const voxel_block_metadata *metadata;

				if (x >= 0 && x < GAME_VOXEL_CHUNK_WIDTH && y >= 0
					&& y < GAME_VOXEL_CHUNK_HEIGHT && z >= 0
					&& z < GAME_VOXEL_CHUNK_DEPTH)
				{
					if (WorldChunkSnapshotReader::lookup_snapshot_block(
						const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(
							&snapshot), chunk_x * GAME_VOXEL_CHUNK_WIDTH + x,
							 y, chunk_z * GAME_VOXEL_CHUNK_DEPTH + z,
							&block_id) != FT_ERR_SUCCESS)
						return (FT_ERR_INVALID_OPERATION);
					metadata = &voxel_get_block_metadata(block_id);
					if (metadata->light_attenuation < 15U
						&& metadata->occludes_faces == FT_FALSE)
					{
						uint8_t cost = metadata->light_attenuation;
						if (cost < 1U)
							cost = 1U;
						if (snapshot_column_has_direct_sky(snapshot, chunk_x, chunk_z,
							x, y, z))
							candidate_sky = 15U;
						else
							candidate_sky = voxel_light_sky(source) > cost
								? static_cast<uint8_t>(voxel_light_sky(source) - cost) : 0U;
						candidate_block = voxel_light_block(source) > cost
							? static_cast<uint8_t>(voxel_light_block(source) - cost) : 0U;
						destination = light.get(x, y, z);
						if (candidate_sky > voxel_light_sky(destination)
							|| candidate_block > voxel_light_block(destination))
						{
							uint8_t result_sky = candidate_sky
								> voxel_light_sky(destination) ? candidate_sky
								: voxel_light_sky(destination);
							uint8_t result_block = candidate_block
								> voxel_light_block(destination) ? candidate_block
								: voxel_light_block(destination);
							if (light.set(x, y, z,
								voxel_light_pack(result_sky, result_block))
								!= FT_ERR_SUCCESS)
								return (FT_ERR_NO_MEMORY);
							try
							{
								queue.push_back({x, y, z});
							}
							catch (...)
							{
								return (FT_ERR_NO_MEMORY);
							}
						}
					}
				}
				direction += 1U;
			}
			index += 1U;
			*processed += 1U;
			processed_this_call += 1U;
		}
		*queue_index = index;
		*complete = index >= queue.size() ? FT_TRUE : FT_FALSE;
		if (*complete != FT_FALSE)
		{
			*state_initialized = FT_FALSE;
			queue.clear();
		}
		return (FT_ERR_SUCCESS);
	}

	uint8_t incremental_channel_value(uint8_t packed_light,
		uint8_t channel) noexcept
	{
		if (channel == 0U)
			return (voxel_light_sky(packed_light));
		return (voxel_light_block(packed_light));
	}

	uint8_t incremental_set_channel(uint8_t packed_light, uint8_t channel,
		uint8_t value) noexcept
	{
		uint8_t sky_light = voxel_light_sky(packed_light);
		uint8_t block_light = voxel_light_block(packed_light);

		if (channel == 0U)
			sky_light = value;
		else
			block_light = value;
		return (voxel_light_pack(sky_light, block_light));
	}

	uint8_t incremental_recomputed_channel(const voxel_light_chunk &light,
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		int32_t chunk_x, int32_t chunk_z, int32_t x, int32_t y, int32_t z,
		uint8_t channel) noexcept
	{
		static const int32_t delta_x[6] = {1, -1, 0, 0, 0, 0};
		static const int32_t delta_y[6] = {0, 0, 1, -1, 0, 0};
		static const int32_t delta_z[6] = {0, 0, 0, 0, 1, -1};
		uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
		const voxel_block_metadata *metadata;
		uint8_t result = 0U;
		uint8_t direction = 0U;

		if (WorldChunkSnapshotReader::lookup_snapshot_block(
			const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
			chunk_x * GAME_VOXEL_CHUNK_WIDTH + x, y,
			chunk_z * GAME_VOXEL_CHUNK_DEPTH + z, &block_id) != FT_ERR_SUCCESS)
			return (0U);
		metadata = &voxel_get_block_metadata(block_id);
		if (metadata->light_attenuation >= 15U
			|| metadata->occludes_faces != FT_FALSE)
			return (0U);
		if (channel == 0U
			&& snapshot_column_has_direct_sky(snapshot, chunk_x, chunk_z,
				x, y, z))
			result = 15U;
		if (channel == 1U)
			result = voxel_block_emitted_light_level(block_id);
		while (direction < 6U)
		{
			const int32_t source_x = x + delta_x[direction];
			const int32_t source_y = y + delta_y[direction];
			const int32_t source_z = z + delta_z[direction];
			uint8_t source_light = 0U;
			uint8_t source_value;
			uint8_t cost;

			if (source_x >= 0 && source_x < GAME_VOXEL_CHUNK_WIDTH
				&& source_y >= 0 && source_y < GAME_VOXEL_CHUNK_HEIGHT
				&& source_z >= 0 && source_z < GAME_VOXEL_CHUNK_DEPTH)
				source_light = light.get(source_x, source_y, source_z);
			else if (WorldChunkSnapshotReader::lookup_snapshot_light(
				const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
				chunk_x * GAME_VOXEL_CHUNK_WIDTH + source_x, source_y,
				chunk_z * GAME_VOXEL_CHUNK_DEPTH + source_z, &source_light)
				!= FT_ERR_SUCCESS)
			{
				direction += 1U;
				continue ;
			}
			cost = metadata->light_attenuation;
			if (cost < 1U)
				cost = 1U;
			source_value = channel == 0U
				? voxel_light_sky(source_light) : voxel_light_block(source_light);
			if (source_value > cost
				&& static_cast<uint8_t>(source_value - cost) > result)
				result = static_cast<uint8_t>(source_value - cost);
			direction += 1U;
		}
		return (result);
	}

	int32_t build_incremental_removal_light(voxel_light_chunk &light,
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		int32_t chunk_x, int32_t chunk_z, int32_t changed_x,
		int32_t changed_y, int32_t changed_z, uint32_t old_block_id,
		uint32_t new_block_id, uint64_t *processed,
		ft_bool preserve_existing, ft_bool external_source,
		uint8_t old_source_light,
		const std::shared_ptr<std::atomic<uint64_t>> &cancellation_token,
		uint64_t cancellation_token_value,
		std::vector<incremental_removal_node> &removal_queue,
		std::size_t *removal_queue_index,
		std::vector<incremental_removal_node> &addition_queue,
		std::size_t *addition_queue_index,
		ft_bool *state_initialized,
		ft_bool *addition_started, uint32_t node_budget,
		ft_bool *complete, uint8_t external_new_channel[2]) noexcept
	{
		std::size_t index;
		int32_t local_z;

		if (processed == nullptr || removal_queue_index == nullptr
			|| addition_queue_index == nullptr || state_initialized == nullptr
			|| addition_started == nullptr || complete == nullptr
			|| external_new_channel == nullptr || node_budget == 0U
			|| changed_x < 0
			|| changed_x >= GAME_VOXEL_CHUNK_WIDTH || changed_z < 0
			|| changed_z >= GAME_VOXEL_CHUNK_DEPTH || changed_y < 0
			|| changed_y >= GAME_VOXEL_CHUNK_HEIGHT
			|| snapshot.existing_light.size()
			< static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_DEPTH)
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT))
			return (FT_ERR_INVALID_ARGUMENT);
		*processed = 0U;
		*complete = FT_FALSE;
		if (*state_initialized == FT_FALSE && preserve_existing == FT_FALSE)
		{
			if (light.initialize(0U) != FT_ERR_SUCCESS)
				return (FT_ERR_NO_MEMORY);
			local_z = 0;
			while (local_z < GAME_VOXEL_CHUNK_DEPTH)
			{
				int32_t local_y = 0;
				while (local_y < GAME_VOXEL_CHUNK_HEIGHT)
				{
					int32_t local_x = 0;
					while (local_x < GAME_VOXEL_CHUNK_WIDTH)
					{
						const std::size_t source_index =
							(static_cast<std::size_t>(local_z)
							* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
							+ static_cast<std::size_t>(local_y))
							* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
							+ static_cast<std::size_t>(local_x);
						if (light.set(local_x, local_y, local_z,
							snapshot.existing_light[source_index]) != FT_ERR_SUCCESS)
							return (FT_ERR_NO_MEMORY);
						local_x += 1;
					}
					local_y += 1;
				}
				local_z += 1;
			}
		}
		if (*state_initialized == FT_FALSE)
		{
			try
			{
				removal_queue.reserve(128U);
				addition_queue.reserve(128U);
			}
			catch (...)
			{
				return (FT_ERR_NO_MEMORY);
			}
		}
		if (*state_initialized == FT_FALSE && external_source != FT_FALSE)
		{
			int32_t source_world_x = chunk_x * GAME_VOXEL_CHUNK_WIDTH
				+ changed_x;
			int32_t source_world_z = chunk_z * GAME_VOXEL_CHUNK_DEPTH
				+ changed_z;
			uint8_t new_source_light;
			uint32_t target_block_id = GAME_VOXEL_AIR_BLOCK;
			const voxel_block_metadata *target_metadata;
			uint8_t attenuation;

			if (changed_x == 0)
				source_world_x -= 1;
			else if (changed_x == GAME_VOXEL_CHUNK_WIDTH - 1)
				source_world_x += 1;
			else if (changed_z == 0)
				source_world_z -= 1;
			else if (changed_z == GAME_VOXEL_CHUNK_DEPTH - 1)
				source_world_z += 1;
			else
				return (FT_ERR_INVALID_ARGUMENT);
			if (WorldChunkSnapshotReader::lookup_snapshot_light(
				const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
				source_world_x, changed_y, source_world_z,
				&new_source_light) != FT_ERR_SUCCESS
				|| WorldChunkSnapshotReader::lookup_snapshot_block(
				const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
				chunk_x * GAME_VOXEL_CHUNK_WIDTH + changed_x, changed_y,
				chunk_z * GAME_VOXEL_CHUNK_DEPTH + changed_z, &target_block_id)
				!= FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_STATE);
			/* The edited chunk's read-state light still contains the old
			 * source while this neighbour is being solved.  For an external
			 * removal, the new source must come from the edited block's new
			 * material, not from that stale read-state sample. */
			new_source_light = voxel_light_pack(0U,
				voxel_block_emitted_light_level(new_block_id));
			target_metadata = &voxel_get_block_metadata(target_block_id);
			if (target_metadata->occludes_faces != FT_FALSE
				|| target_metadata->light_attenuation >= 15U)
				attenuation = 15U;
			else
			{
				attenuation = target_metadata->light_attenuation;
				if (attenuation < 1U)
					attenuation = 1U;
			}
			for (uint8_t channel = 0U; channel < 2U; ++channel)
			{
				const uint8_t old_source_level = channel == 0U
					? voxel_light_sky(old_source_light)
					: voxel_light_block(old_source_light);
				const uint8_t new_source_level = channel == 0U
					? voxel_light_sky(new_source_light)
					: voxel_light_block(new_source_light);
				const uint8_t old_contribution = old_source_level > attenuation
					? static_cast<uint8_t>(old_source_level - attenuation) : 0U;
				const uint8_t new_contribution = new_source_level > attenuation
					? static_cast<uint8_t>(new_source_level - attenuation) : 0U;
				if (old_contribution > 0U)
				{
					removal_queue.push_back({changed_x, changed_y, changed_z,
						channel, old_contribution});
					if (incremental_channel_value(light.get(changed_x,
						changed_y, changed_z), channel) <= old_contribution
						&& light.set(changed_x, changed_y, changed_z,
						incremental_set_channel(light.get(changed_x, changed_y,
						changed_z), channel, 0U)) != FT_ERR_SUCCESS)
						return (FT_ERR_NO_MEMORY);
				}
				external_new_channel[channel] = new_contribution;
			}
		}
		else
		{
			const uint8_t old_light = light.get(changed_x, changed_y, changed_z);
			if (voxel_light_sky(old_light) != 0U)
			{
				removal_queue.push_back({changed_x, changed_y, changed_z, 0U,
					voxel_light_sky(old_light)});
			}
			if (voxel_light_block(old_light) != 0U)
			{
				removal_queue.push_back({changed_x, changed_y, changed_z, 1U,
					voxel_light_block(old_light)});
			}
			if (light.set(changed_x, changed_y, changed_z, 0U)
				!= FT_ERR_SUCCESS)
				return (FT_ERR_NO_MEMORY);
		}
		if (*state_initialized == FT_FALSE && external_source == FT_FALSE)
		{
			const uint8_t new_emission =
				voxel_block_emitted_light_level(new_block_id);
			const voxel_block_metadata *old_metadata =
				&voxel_get_block_metadata(old_block_id);
			const voxel_block_metadata *new_metadata =
				&voxel_get_block_metadata(new_block_id);
			const bool old_blocks_sky = old_metadata->occludes_faces != FT_FALSE
				|| old_metadata->light_attenuation >= 15U;
			const bool new_blocks_sky = new_metadata->occludes_faces != FT_FALSE
				|| new_metadata->light_attenuation >= 15U;
			if (new_emission != 0U)
			{
				if (light.set(changed_x, changed_y, changed_z,
					voxel_light_pack(0U, new_emission)) != FT_ERR_SUCCESS)
					return (FT_ERR_NO_MEMORY);
				addition_queue.push_back({changed_x, changed_y, changed_z, 1U,
					new_emission});
			}
			uint8_t restored_sky = snapshot_sky_after_block_change(snapshot,
				chunk_x, chunk_z, changed_x, changed_y, changed_z, new_block_id);
			/* The edit snapshot already contains the authoritative sky value at
		 * this cell.  Preserve it when the edit only removed block light; the
		 * above-cell estimate is still needed when an opaque block previously
		 * masked a newly opened sky column. */
			{
				const std::size_t changed_index =
					(static_cast<std::size_t>(changed_z)
					* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
					+ static_cast<std::size_t>(changed_y))
					* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
					+ static_cast<std::size_t>(changed_x);
				const uint8_t previous_sky = voxel_light_sky(
					snapshot.existing_light[changed_index]);
				/* Removing an emissive block must not reinterpret the old sky
				 * channel as a fresh open-column solve.  The snapshot value is the
				 * authoritative pre-edit sky state; use it directly for that case.
				 * For an opaque-block removal, retain the larger derived value so a
				 * newly opened sky column can still be restored. */
				if (new_blocks_sky)
					restored_sky = 0U;
				else if (voxel_block_emitted_light_level(old_block_id) != 0U
					&& voxel_block_emitted_light_level(new_block_id) == 0U)
					restored_sky = previous_sky;
				else if (old_blocks_sky && previous_sky > restored_sky)
					restored_sky = previous_sky;
			}
			if (restored_sky != 0U)
			{
				if (light.set(changed_x, changed_y, changed_z,
					voxel_light_pack(restored_sky,
						voxel_light_block(light.get(changed_x, changed_y, changed_z))))
					!= FT_ERR_SUCCESS)
					return (FT_ERR_NO_MEMORY);
				addition_queue.push_back({changed_x, changed_y, changed_z, 0U,
					restored_sky});
			}
		}
		if (*state_initialized == FT_FALSE)
		{
			*removal_queue_index = 0U;
			*addition_queue_index = 0U;
			*addition_started = FT_FALSE;
			*state_initialized = FT_TRUE;
		}
		index = *removal_queue_index;
		while (index < removal_queue.size()
			&& *processed < static_cast<uint64_t>(node_budget))
		{
			if ((index & 255U) == 0U
				&& incremental_request_is_cancelled(cancellation_token,
					cancellation_token_value))
				return (FT_ERR_INVALID_STATE);
			const incremental_removal_node node = removal_queue[index];
			static const int32_t delta_x[6] = {1, -1, 0, 0, 0, 0};
			static const int32_t delta_y[6] = {0, 0, 1, -1, 0, 0};
			static const int32_t delta_z[6] = {0, 0, 0, 0, 1, -1};
			uint8_t direction = 0U;

			while (direction < 6U)
			{
				const int32_t x = node.x + delta_x[direction];
				const int32_t y = node.y + delta_y[direction];
				const int32_t z = node.z + delta_z[direction];
				if (x >= 0 && x < GAME_VOXEL_CHUNK_WIDTH && y >= 0
					&& y < GAME_VOXEL_CHUNK_HEIGHT && z >= 0
					&& z < GAME_VOXEL_CHUNK_DEPTH)
				{
					const uint8_t current = incremental_channel_value(
						light.get(x, y, z), node.channel);
					const bool direct_sky = node.channel == 0U
						&& snapshot_column_has_direct_sky(snapshot, chunk_x, chunk_z,
							x, y, z);
					if (direct_sky)
					{
						/* A column that is still open to the top of the world is
						 * an independent source.  It must not be darkened merely
						 * because the removed path had the same level. */
						if (current < 15U
							&& light.set(x, y, z, incremental_set_channel(
								light.get(x, y, z), node.channel, 15U))
							!= FT_ERR_SUCCESS)
							return (FT_ERR_NO_MEMORY);
					}
					else if (current != 0U && current <= node.level)
					{
						if (light.set(x, y, z,
							incremental_set_channel(light.get(x, y, z),
								node.channel, 0U)) != FT_ERR_SUCCESS)
							return (FT_ERR_NO_MEMORY);
						removal_queue.push_back({x, y, z, node.channel,
							current});
						{
							const uint8_t replacement =
								incremental_recomputed_channel(light, snapshot, chunk_x,
									chunk_z, x, y, z, node.channel);
							if (replacement != 0U)
							{
								if (light.set(x, y, z,
									incremental_set_channel(light.get(x, y, z),
										node.channel, replacement))
									!= FT_ERR_SUCCESS)
									return (FT_ERR_NO_MEMORY);
								addition_queue.push_back({x, y, z, node.channel,
									replacement});
							}
						}
					}
					else if (current > node.level)
						addition_queue.push_back({x, y, z, node.channel,
							current});
				}
				direction += 1U;
			}
			index += 1U;
			*processed += 1U;
		}
		*removal_queue_index = index;
		if (index < removal_queue.size())
			return (FT_ERR_SUCCESS);
		if (*addition_started == FT_FALSE && external_source != FT_FALSE)
		{
			uint8_t channel = 0U;
			while (channel < 2U)
			{
				const uint8_t current = incremental_channel_value(
					light.get(changed_x, changed_y, changed_z), channel);
				if (external_new_channel[channel] > current)
				{
					if (light.set(changed_x, changed_y, changed_z,
						incremental_set_channel(light.get(changed_x, changed_y,
						changed_z), channel, external_new_channel[channel]))
						!= FT_ERR_SUCCESS)
						return (FT_ERR_NO_MEMORY);
					addition_queue.push_back({changed_x, changed_y, changed_z,
						channel, external_new_channel[channel]});
				}
				channel += 1U;
			}
			*addition_started = FT_TRUE;
		}
		index = *addition_queue_index;
		while (index < addition_queue.size()
			&& *processed < static_cast<uint64_t>(node_budget))
		{
			if ((index & 255U) == 0U
				&& incremental_request_is_cancelled(cancellation_token,
					cancellation_token_value))
				return (FT_ERR_INVALID_STATE);
			const incremental_removal_node node = addition_queue[index];
			static const int32_t delta_x[6] = {1, -1, 0, 0, 0, 0};
			static const int32_t delta_y[6] = {0, 0, 1, -1, 0, 0};
			static const int32_t delta_z[6] = {0, 0, 0, 0, 1, -1};
			uint8_t direction = 0U;

			while (direction < 6U)
			{
				const int32_t x = node.x + delta_x[direction];
				const int32_t y = node.y + delta_y[direction];
				const int32_t z = node.z + delta_z[direction];
				uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
				if (x >= 0 && x < GAME_VOXEL_CHUNK_WIDTH && y >= 0
					&& y < GAME_VOXEL_CHUNK_HEIGHT && z >= 0
					&& z < GAME_VOXEL_CHUNK_DEPTH
					&& WorldChunkSnapshotReader::lookup_snapshot_block(
						const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(
							&snapshot), chunk_x * GAME_VOXEL_CHUNK_WIDTH + x,
						y, chunk_z * GAME_VOXEL_CHUNK_DEPTH + z,
						&block_id) == FT_ERR_SUCCESS)
				{
					const voxel_block_metadata &metadata =
						voxel_get_block_metadata(block_id);
						if (metadata.light_attenuation < 15U
							&& metadata.occludes_faces == FT_FALSE)
						{
							uint8_t cost = metadata.light_attenuation;
							if (cost < 1U)
								cost = 1U;
							if (node.level > cost)
							{
								const uint8_t candidate =
									static_cast<uint8_t>(node.level - cost);
								const uint8_t current = incremental_channel_value(
									light.get(x, y, z), node.channel);
								if (candidate > current)
								{
									if (light.set(x, y, z,
										incremental_set_channel(light.get(x, y, z),
											node.channel, candidate))
										!= FT_ERR_SUCCESS)
										return (FT_ERR_NO_MEMORY);
									addition_queue.push_back({x, y, z, node.channel,
										candidate});
								}
							}
						}
					}
				direction += 1U;
			}
			index += 1U;
			*processed += 1U;
		}
		*addition_queue_index = index;
		if (index < addition_queue.size())
			return (FT_ERR_SUCCESS);
		removal_queue.clear();
		addition_queue.clear();
		*removal_queue_index = 0U;
		*addition_queue_index = 0U;
		*addition_started = FT_FALSE;
		*state_initialized = FT_FALSE;
		*complete = FT_TRUE;
		return (FT_ERR_SUCCESS);
	}
}

WorldChunkGenerationWorker::WorldChunkGenerationWorker()
{
}

WorldChunkGenerationWorker::WorldChunkGenerationWorker(const WorldChunkGenerationWorker &other)
{
	(void)other;
}

WorldChunkGenerationWorker::~WorldChunkGenerationWorker()
{
}

WorldChunkGenerationWorker &WorldChunkGenerationWorker::operator=(const WorldChunkGenerationWorker &other)
{
	(void)other;
	return (*this);
}

int32_t WorldChunkGenerationWorker::initialize_chunk_for_generation(WorldChunk &chunk,
	int32_t chunk_x, int32_t chunk_z, const char *seed,
	voxel_generation_config &config, uint32_t stage_mask,
	std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> &deferred_edits,
	const WorldGenerationPipeline::WorldChunkSnapshot *source_snapshot,
	uint64_t *generation_duration_nanoseconds,
	uint64_t *mesh_duration_nanoseconds) noexcept
{
	int32_t error_code;
	std::chrono::steady_clock::time_point phase_start;

	chunk.chunk_x = chunk_x;
	chunk.chunk_z = chunk_z;
	chunk.world_x = chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	chunk.world_z = chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	if (source_snapshot == nullptr)
		error_code = chunk.chunk.initialize();
	else
	{
	#if defined(DEBUG)
		std::fprintf(stderr,
			"[WorldRevision] worker snapshot-init chunk=(%d,%d) blocks=%zu "
			"metadata_valid=%d stages=%u\n", chunk_x, chunk_z,
			source_snapshot->blocks.size(),
			source_snapshot->generation_metadata.valid != FT_FALSE ? 1 : 0,
			source_snapshot->generation_metadata.completed_stage_mask);
	#endif
		error_code = WorldChunkSnapshotReader::initialize_snapshot_chunk(chunk.chunk,
				*source_snapshot);
	}
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (chunk_mesh_initialize(chunk.mesh) != FT_ERR_SUCCESS)
	{
		(void)chunk.chunk.destroy();
		return (FT_ERR_NO_MEMORY);
	}
	phase_start = std::chrono::steady_clock::now();
	#if defined(DEBUG)
	if (source_snapshot != nullptr)
		std::fprintf(stderr,
			"[WorldRevision] worker generate chunk=(%d,%d) stages=%u\n",
			chunk_x, chunk_z, stage_mask);
	#endif
	error_code = voxel_generate_chunk_with_stage_mask(chunk.chunk,
			chunk.world_x, chunk.world_z, seed, config, stage_mask);
	if (generation_duration_nanoseconds != nullptr)
		*generation_duration_nanoseconds = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - phase_start).count());
	phase_start = std::chrono::steady_clock::now();
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = voxel_light_build_chunk_local(chunk.light, chunk.world_x,
			chunk.world_z, lookup_local_light_block, &chunk.chunk);
		if (error_code == FT_ERR_SUCCESS)
			error_code = chunk_mesh_generate_from_chunk_with_light(chunk.mesh,
				chunk.chunk, chunk.light);
	}
	if (mesh_duration_nanoseconds != nullptr)
		*mesh_duration_nanoseconds = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - phase_start).count());
	if (error_code != FT_ERR_SUCCESS)
	{
		(void)chunk_mesh_destroy(chunk.mesh);
		(void)chunk.chunk.destroy();
		return (error_code);
	}
	/* The worker has already built both the chunk-local light field and the
	 * mesh from that field.  This is a valid baseline for rendering.  A later
	 * neighbour-aware remesh may refine border values, but it must not make the
	 * renderer treat this complete local result as an all-zero/black light
	 * buffer while it waits for that refinement. */
	chunk.light_ready_for_render = true;
	#if defined(DEBUG)
	if (source_snapshot != nullptr)
		std::fprintf(stderr,
			"[WorldRevision] worker generate complete chunk=(%d,%d)\n",
			chunk_x, chunk_z);
	#endif
	chunk.initialized = true;
	(void)deferred_edits;
	return (FT_ERR_SUCCESS);
}

std::unique_ptr<WorldGenerationPipeline::Result> WorldChunkGenerationWorker::process_generation(WorldGenerationPipeline::Request &request) noexcept
{
	std::unique_ptr<WorldGenerationPipeline::Result> result(new (std::nothrow) WorldGenerationPipeline::Result());

	if (result == nullptr)
		return (nullptr);
	result->request_id = request.request_id;
	result->world_epoch = request.world_epoch;
	result->relevance_epoch = request.relevance_epoch;
	result->generation_revision = request.generation_revision;
	result->configuration_signature = request.configuration_signature;
	result->stage_mask = request.stage_mask;
	result->voxel_revision = 0U;
	result->light_revision = 0U;
	result->content_version = 1U;
	result->light_input_version = 1U;
	result->chunk_x = request.chunk_x;
	result->chunk_z = request.chunk_z;
	result->operation = request.operation;
	result->error_code = FT_ERR_SUCCESS;
	result->generation_duration_nanoseconds = 0U;
	result->mesh_duration_nanoseconds = 0U;
	result->light_scanned_cells = 0U;
	result->light_propagated_cells = 0U;
	result->light_queue_peak = 0U;
	result->chunk.reset(new (std::nothrow) WorldChunk());
	if (result->chunk == nullptr)
	{
		result->error_code = FT_ERR_NO_MEMORY;
		return (result);
	}
	result->error_code = WorldChunkGenerationWorker::initialize_chunk_for_generation(*result->chunk,
			request.chunk_x, request.chunk_z, request.seed.c_str(),
			request.config, request.stage_mask, request.deferred_edits,
			request.snapshot.get(), &result->generation_duration_nanoseconds,
			&result->mesh_duration_nanoseconds);
	if (result->error_code != FT_ERR_SUCCESS)
	{
		result->chunk.reset();
		return (result);
	}
	return (WorldChunkGenerationWorker::finish_generation_result(std::move(result),
			request));
}

std::unique_ptr<WorldGenerationPipeline::Result> WorldChunkGenerationWorker::finish_generation_result(std::unique_ptr<WorldGenerationPipeline::Result> result,
	WorldGenerationPipeline::Request &request) noexcept
{
	game_voxel_generation_metadata metadata;

	metadata = result->chunk->chunk.get_generation_metadata();
	#if defined(DEBUG)
	std::fprintf(stderr,
		"[WorldRevision] worker finalize request=%llu chunk=(%d,%d)\n",
		static_cast<unsigned long long>(request.request_id), request.chunk_x,
		request.chunk_z);
	#endif
	metadata.configuration_signature = request.configuration_signature;
	if (result->chunk->chunk.set_generation_metadata(metadata) != FT_ERR_SUCCESS)
	{
		result->error_code = FT_ERR_INVALID_OPERATION;
		result->chunk.reset();
		return (result);
	}
	result->deferred_edits = std::move(request.deferred_edits);
	for (WorldGenerationPipeline::WorldDeferredBlockEdit &edit : result->deferred_edits)
		edit.request_id = request.request_id;
	return (result);
}

std::unique_ptr<WorldGenerationPipeline::Result> WorldChunkGenerationWorker::process_remesh(WorldGenerationPipeline::Request &request) noexcept
{
	std::unique_ptr<WorldGenerationPipeline::Result> result(new (std::nothrow) WorldGenerationPipeline::Result());
	voxel_light_build_stats light_stats;
	voxel_light_update_config light_config;
	ft_bool light_complete;
	int32_t error_code;
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	std::chrono::steady_clock::time_point light_step_start;
	std::chrono::steady_clock::time_point mesh_start;
#endif

	if (result == nullptr)
		return (nullptr);
	result->request_id = request.request_id;
	result->world_epoch = request.world_epoch;
	result->relevance_epoch = request.relevance_epoch;
	result->generation_revision = request.generation_revision;
	result->configuration_signature = 0U;
	result->stage_mask = 0U;
	result->voxel_revision = request.voxel_revision;
	result->light_revision = request.light_revision;
	result->content_version = request.content_version;
	result->light_input_version = request.light_input_version;
	result->chunk_x = request.chunk_x;
	result->chunk_z = request.chunk_z;
	result->operation = request.operation;
	result->error_code = FT_ERR_SUCCESS;
	result->generation_duration_nanoseconds = 0U;
	result->mesh_duration_nanoseconds = 0U;
	result->light_scanned_cells = 0U;
	result->light_propagated_cells = 0U;
	result->light_queue_peak = 0U;
	result->incremental_light = FT_FALSE;
	result->incremental_light_deltas_complete = FT_FALSE;
	result->interactive_remesh = request.remesh_interactive;
	result->lighting_halo_valid = request.snapshot != nullptr
		&& request.snapshot->lighting_halo_valid != FT_FALSE
		? FT_TRUE : FT_FALSE;
	result->lighting_cardinal_halo_valid = request.snapshot != nullptr
		&& request.snapshot->lighting_cardinal_halo_valid != FT_FALSE
		? FT_TRUE : FT_FALSE;
	result->lighting_halo_blocked = request.snapshot != nullptr
		&& request.snapshot->lighting_halo_blocked != FT_FALSE
		? FT_TRUE : FT_FALSE;
	result->dependency_valid = request.snapshot != nullptr
		&& request.snapshot->dependency_valid != FT_FALSE
		? FT_TRUE : FT_FALSE;
	result->dependency_chunk_x = request.snapshot != nullptr
		? request.snapshot->dependency_chunk_x : 0;
	result->dependency_chunk_z = request.snapshot != nullptr
		? request.snapshot->dependency_chunk_z : 0;
	result->dependency_voxel_revision = request.snapshot != nullptr
		? request.snapshot->dependency_voxel_revision : 0U;
	result->dependency_light_revision = request.snapshot != nullptr
		? request.snapshot->dependency_light_revision : 0U;
	result->dependency_content_version = request.snapshot != nullptr
		? request.snapshot->dependency_content_version : 0U;
	result->dependency_light_input_version = request.snapshot != nullptr
		? request.snapshot->dependency_light_input_version : 0U;
	if (request.snapshot == nullptr)
	{
		result->error_code = FT_ERR_NO_MEMORY;
		return (result);
	}
	if (request.snapshot->existing_light_valid == FT_FALSE)
	{
	#if defined(DEBUG)
		if (request.incremental_additive_light != FT_FALSE
			|| request.incremental_removal_light != FT_FALSE)
			std::fprintf(stderr,
				"[WorldGen] incremental request downgraded to full light "
				"request=%llu chunk=(%d,%d)\n",
				static_cast<unsigned long long>(request.request_id),
				request.chunk_x, request.chunk_z);
	#endif
		request.incremental_additive_light = FT_FALSE;
		request.incremental_removal_light = FT_FALSE;
	}
	/* Record the path actually selected after validity fallback.  This keeps
	 * diagnostics and completion accounting honest when an edit has to use a
	 * full rebuild because no previous light buffer exists. */
	if (request.incremental_additive_light != FT_FALSE
		|| request.incremental_removal_light != FT_FALSE)
		result->incremental_light = FT_TRUE;
	if (request.remesh_target == nullptr)
	{
		request.remesh_target.reset(new (std::nothrow) game_voxel_chunk());
		if (request.remesh_target == nullptr)
		{
			result->error_code = FT_ERR_NO_MEMORY;
			return (result);
		}
		error_code = WorldChunkSnapshotReader::initialize_snapshot_chunk(
			*request.remesh_target, *request.snapshot);
		if (error_code == FT_ERR_SUCCESS
			&& request.incremental_additive_light == FT_FALSE
			&& request.incremental_removal_light == FT_FALSE)
		{
			request.remesh_light.reset(new (std::nothrow) voxel_light_chunk());
			request.remesh_light_operation.reset(
				new (std::nothrow) voxel_light_build_operation());
			if (request.remesh_light == nullptr
				|| request.remesh_light_operation == nullptr)
				error_code = FT_ERR_NO_MEMORY;
		}
		if (error_code == FT_ERR_SUCCESS
			&& request.incremental_additive_light == FT_FALSE
			&& request.incremental_removal_light == FT_FALSE)
			error_code = request.remesh_light_operation->initialize(
				*request.remesh_light,
				request.chunk_x * GAME_VOXEL_CHUNK_WIDTH,
				request.chunk_z * GAME_VOXEL_CHUNK_DEPTH,
				&WorldChunkSnapshotReader::lookup_snapshot_block,
				request.snapshot.get(), FT_TRUE);
		if (error_code != FT_ERR_SUCCESS)
		{
			result->error_code = error_code;
			return (result);
		}
	}
	/* A geometry-only result reuses the snapshot light field.  If that field
	 * was never published, the snapshot contains the zero-initialised fallback
	 * and committing the geometry-only mesh would replace a drawable chunk with
	 * a black mesh.  Promote this request to a complete light build before any
	 * geometry-only result is produced. */
	if (request.remesh_geometry_only != FT_FALSE
		&& request.snapshot->existing_light_valid == FT_FALSE)
		request.remesh_geometry_only = FT_FALSE;
	if (request.remesh_geometry_only != FT_FALSE)
	{
		result->mesh.reset(new (std::nothrow) chunk_mesh());
		if (result->mesh == nullptr)
		{
			result->error_code = FT_ERR_NO_MEMORY;
			return (result);
		}
		error_code = chunk_mesh_initialize(*result->mesh);
		if (error_code == FT_ERR_SUCCESS)
			error_code = chunk_mesh_generate_from_chunk_with_neighbors_and_light_lookup(
				*result->mesh, *request.remesh_target, request.chunk_x,
				request.chunk_z, &WorldChunkSnapshotReader::lookup_snapshot_mesh_block,
				request.snapshot.get(), nullptr,
				&WorldChunkSnapshotReader::lookup_snapshot_light,
				request.snapshot.get());
		if (error_code != FT_ERR_SUCCESS)
		{
			result->error_code = error_code;
			return (result);
		}
		result->stage_mask = WorldGenerationPipeline::Result::STAGE_GEOMETRY_ONLY
			| WorldGenerationPipeline::Result::STAGE_GEOMETRY_FINAL;
		request.remesh_in_progress = FT_FALSE;
		return (result);
	}
	/* Publish the geometry phase as soon as the edited chunk has a usable
	 * previous light buffer.  Requiring every neighbouring chunk's light
	 * revision to be current here made an ordinary block edit wait for an
	 * unrelated full relight.  The snapshot still contains the previous halo
	 * values, so this mesh keeps the old lighting while the incremental
	 * frontier updates the target chunk.  A later light-aware result remains
	 * responsible for converging the edge values. */
	/* Publish a geometry-only preview for every interactive edit when the
	 * snapshot contains a complete light field and valid cardinal halo.  The
	 * preview uses that complete, pre-edit light field; it must never use the
	 * partially solved frontier.  This lets a removed block disappear
	 * immediately while the old lighting remains stable until the final light
	 * result is ready. */
	if (request.remesh_geometry_published == FT_FALSE
		&& request.snapshot->existing_light_valid != FT_FALSE
		&& request.snapshot->lighting_cardinal_halo_valid != FT_FALSE
		&& request.remesh_interactive != FT_FALSE)
	{
		result->mesh.reset(new (std::nothrow) chunk_mesh());
		if (result->mesh == nullptr)
		{
			result->error_code = FT_ERR_NO_MEMORY;
			return (result);
		}
		error_code = chunk_mesh_initialize(*result->mesh);
		if (error_code == FT_ERR_SUCCESS)
			error_code = chunk_mesh_generate_from_chunk_with_neighbors_and_light_lookup(
				*result->mesh, *request.remesh_target, request.chunk_x,
				request.chunk_z, &WorldChunkSnapshotReader::lookup_snapshot_mesh_block,
				request.snapshot.get(), nullptr,
				&WorldChunkSnapshotReader::lookup_snapshot_light,
				request.snapshot.get());
		if (error_code != FT_ERR_SUCCESS)
		{
			result->error_code = error_code;
			return (result);
		}
		result->stage_mask = WorldGenerationPipeline::Result::STAGE_GEOMETRY_ONLY;
		request.remesh_geometry_published = FT_TRUE;
		request.remesh_in_progress = FT_TRUE;
		return (result);
	}
	if (remesh_request_is_cancelled(request))
	{
		result->error_code = FT_ERR_INVALID_STATE;
		request.remesh_in_progress = FT_FALSE;
		return (result);
	}
	if (!request.incremental_light_seeds.empty())
	{
		/*
		 * Mixed edit seeds are still an incremental operation.  Removal must run
		 * first so it can subtract obsolete contributions; additive propagation
		 * then restores surviving/new sources from the same mutable worker-owned
		 * light buffer.  The seed order is made stable and in-place so no second
		 * allocation is needed at the worker boundary.
		 */
		if (request.remesh_interactive != FT_FALSE)
		{
			std::size_t removal_insert = 0U;
			std::size_t seed_index = 0U;
			while (seed_index < request.incremental_light_seeds.size())
			{
				if (request.incremental_light_seeds[seed_index].removal
					!= FT_FALSE)
				{
					std::swap(request.incremental_light_seeds[removal_insert],
						request.incremental_light_seeds[seed_index]);
					removal_insert += 1U;
				}
				seed_index += 1U;
			}
			uint32_t node_budget = request.light_update_config
				.max_nodes_per_frame;
			ft_bool seed_complete = FT_FALSE;
			incremental_light_lookup_context light_context;

			if (node_budget == 0U)
				node_budget = 1024U;
			if (request.remesh_light == nullptr)
			{
				request.remesh_light.reset(new (std::nothrow)
					voxel_light_chunk());
				if (request.remesh_light == nullptr)
				{
					result->error_code = FT_ERR_NO_MEMORY;
					return (result);
				}
			}
			while (request.incremental_seed_index
				< request.incremental_light_seeds.size())
			{
				const WorldGenerationPipeline::IncrementalLightSeed &seed =
					request.incremental_light_seeds[
						request.incremental_seed_index];
				uint64_t seed_processed = 0U;
				ft_bool preserve_existing =
					(request.incremental_seed_index == 0U
						&& request.incremental_removal_state_initialized == FT_FALSE
						&& request.incremental_additive_state_initialized == FT_FALSE)
					? FT_FALSE : FT_TRUE;

				if (seed.removal == FT_FALSE)
					error_code = build_incremental_additive_light(
						*request.remesh_light, *request.snapshot, request.chunk_x,
						request.chunk_z, seed.local_x, seed.local_y, seed.local_z,
						&seed_processed, preserve_existing,
						request.cancellation_token,
						request.cancellation_token_value,
						request.incremental_additive_queue,
						&request.incremental_additive_queue_index,
						&request.incremental_additive_state_initialized,
						node_budget, &seed_complete, seed.external_source,
						seed.new_block_id);
				else
					error_code = build_incremental_removal_light(
						*request.remesh_light, *request.snapshot, request.chunk_x,
						request.chunk_z, seed.local_x, seed.local_y, seed.local_z,
						seed.old_block_id, seed.new_block_id, &seed_processed,
						preserve_existing, seed.external_source,
						seed.old_source_light, request.cancellation_token,
						request.cancellation_token_value,
						request.incremental_removal_queue,
						&request.incremental_removal_queue_index,
						request.incremental_removal_addition_queue,
						&request.incremental_removal_addition_queue_index,
						&request.incremental_removal_state_initialized,
						&request.incremental_removal_addition_started,
						node_budget, &seed_complete,
						request.incremental_removal_external_new_channel);
				request.incremental_processed_cells += seed_processed;
				if (error_code != FT_ERR_SUCCESS)
				{
					result->error_code = error_code;
					request.remesh_in_progress = FT_FALSE;
					return (result);
				}
				if (seed_complete == FT_FALSE)
				{
					request.remesh_in_progress = FT_TRUE;
					return (nullptr);
				}
				request.incremental_seed_index += 1U;
			}
			result->light_scanned_cells = request.incremental_processed_cells;
			result->light = std::move(request.remesh_light);
			error_code = collect_incremental_light_deltas(*request.snapshot,
				*result->light, result->incremental_light_deltas,
				&result->incremental_light_deltas_complete);
			if (error_code != FT_ERR_SUCCESS)
			{
				result->error_code = error_code;
				return (result);
			}
			voxel_light_chunk mesh_light;
			error_code = build_incremental_mesh_light(*request.snapshot,
				result->incremental_light_deltas, mesh_light);
			if (error_code != FT_ERR_SUCCESS)
			{
				result->error_code = error_code;
				return (result);
			}
		#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
			if (count_light_nonzero(mesh_light) == 0U)
				std::fprintf(stderr,
					"[WorldLight] incremental mesh field empty chunk=(%d,%d) "
					"snapshot_nonzero=%zu deltas=%zu\n", request.chunk_x,
					request.chunk_z, count_snapshot_light_nonzero(*request.snapshot),
					result->incremental_light_deltas.size());
		#endif
			result->mesh.reset(new (std::nothrow) chunk_mesh());
			if (result->mesh == nullptr
				|| chunk_mesh_initialize(*result->mesh) != FT_ERR_SUCCESS)
			{
				result->error_code = FT_ERR_NO_MEMORY;
				return (result);
			}
			light_context.light = &mesh_light;
			light_context.snapshot = request.snapshot.get();
			light_context.chunk_x = request.chunk_x;
			light_context.chunk_z = request.chunk_z;
			result->error_code =
				chunk_mesh_generate_from_chunk_with_neighbors_and_light_lookup(
					*result->mesh, *request.remesh_target, request.chunk_x,
					request.chunk_z, &WorldChunkSnapshotReader::lookup_snapshot_mesh_block,
					request.snapshot.get(), &mesh_light,
					&lookup_incremental_light, &light_context);
			request.remesh_target.reset();
			request.remesh_in_progress = FT_FALSE;
			if (result->error_code != FT_ERR_SUCCESS)
				result->mesh.reset();
			else if (result->incremental_light_deltas_complete != FT_FALSE)
			{
				/* The mesh was built from mesh_light.  The result queue only
				 * needs the changed-cell patch for authoritative publication. */
				result->light.reset();
			}
			return (result);
		}
	}
	if (!request.incremental_light_seeds.empty())
	{
		uint64_t processed = 0U;
		incremental_light_lookup_context light_context;
		voxel_light_chunk mesh_light;
		std::size_t seed_index = 0U;
		std::vector<incremental_light_node> additive_queue;
		std::size_t additive_queue_index = 0U;
		ft_bool additive_state_initialized = FT_FALSE;
		ft_bool additive_complete = FT_FALSE;
		std::vector<incremental_removal_node> removal_queue;
		std::vector<incremental_removal_node> removal_addition_queue;
		std::size_t removal_queue_index = 0U;
		std::size_t removal_addition_queue_index = 0U;
		ft_bool removal_state_initialized = FT_FALSE;
		ft_bool removal_addition_started = FT_FALSE;
		uint8_t removal_external_new_channel[2] = {0U, 0U};

		result->light.reset(new (std::nothrow) voxel_light_chunk());
		if (result->light == nullptr)
		{
			result->error_code = FT_ERR_NO_MEMORY;
			return (result);
		}
		error_code = FT_ERR_SUCCESS;
		while (seed_index < request.incremental_light_seeds.size())
		{
			const WorldGenerationPipeline::IncrementalLightSeed &seed =
				request.incremental_light_seeds[seed_index];
			uint64_t seed_processed = 0U;
			if (seed.removal != FT_FALSE)
				error_code = build_incremental_removal_light(*result->light,
					*request.snapshot, request.chunk_x, request.chunk_z,
					seed.local_x, seed.local_y, seed.local_z,
					seed.old_block_id, seed.new_block_id, &seed_processed,
					seed_index != 0U ? FT_TRUE : FT_FALSE,
					seed.external_source, seed.old_source_light,
					request.cancellation_token, request.cancellation_token_value,
					removal_queue, &removal_queue_index,
					removal_addition_queue, &removal_addition_queue_index,
					&removal_state_initialized, &removal_addition_started,
					0xffffffffU, &result->incremental_light_deltas_complete,
					removal_external_new_channel);
			else
					error_code = build_incremental_additive_light(*result->light,
					*request.snapshot, request.chunk_x, request.chunk_z,
					seed.local_x, seed.local_y, seed.local_z, &seed_processed,
					seed_index != 0U ? FT_TRUE : FT_FALSE,
					request.cancellation_token, request.cancellation_token_value,
					additive_queue, &additive_queue_index,
					&additive_state_initialized, 0xffffffffU,
					&additive_complete, seed.external_source,
					seed.new_block_id);
			processed += seed_processed;
			if (error_code != FT_ERR_SUCCESS)
				break ;
			seed_index += 1U;
		}
		result->light_scanned_cells = processed;
		if (error_code != FT_ERR_SUCCESS)
		{
			result->error_code = error_code;
			return (result);
		}
		error_code = collect_incremental_light_deltas(*request.snapshot,
			*result->light, result->incremental_light_deltas,
			&result->incremental_light_deltas_complete);
		if (error_code != FT_ERR_SUCCESS)
		{
			result->error_code = error_code;
			return (result);
		}
		error_code = build_incremental_mesh_light(*request.snapshot,
			result->incremental_light_deltas, mesh_light);
		if (error_code != FT_ERR_SUCCESS)
		{
			result->error_code = error_code;
			return (result);
		}
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		if (count_light_nonzero(mesh_light) == 0U)
			std::fprintf(stderr,
				"[WorldLight] incremental mesh field empty chunk=(%d,%d) "
				"snapshot_nonzero=%zu deltas=%zu\n", request.chunk_x,
				request.chunk_z, count_snapshot_light_nonzero(*request.snapshot),
				result->incremental_light_deltas.size());
	#endif
		result->mesh.reset(new (std::nothrow) chunk_mesh());
		if (result->mesh == nullptr
			|| chunk_mesh_initialize(*result->mesh) != FT_ERR_SUCCESS)
		{
			result->error_code = FT_ERR_NO_MEMORY;
			return (result);
		}
		light_context.light = &mesh_light;
		light_context.snapshot = request.snapshot.get();
		light_context.chunk_x = request.chunk_x;
		light_context.chunk_z = request.chunk_z;
		result->error_code =
			chunk_mesh_generate_from_chunk_with_neighbors_and_light_lookup(
				*result->mesh, *request.remesh_target, request.chunk_x,
				request.chunk_z, &WorldChunkSnapshotReader::lookup_snapshot_mesh_block,
				request.snapshot.get(), &mesh_light,
				&lookup_incremental_light, &light_context);
		request.remesh_target.reset();
		request.remesh_in_progress = FT_FALSE;
		if (result->error_code != FT_ERR_SUCCESS)
			result->mesh.reset();
		else if (result->incremental_light_deltas_complete != FT_FALSE)
		{
			/* Keep the cross-thread result compact after mesh construction. */
			result->light.reset();
		}
		return (result);
	}
	voxel_light_update_config_defaults(light_config);
	light_config = request.light_update_config;
	if (voxel_light_update_config_is_valid(light_config) == FT_FALSE)
	{
		result->error_code = FT_ERR_INVALID_ARGUMENT;
		return (result);
	}
	light_complete = FT_FALSE;
	#if defined(DEBUG)
	light_step_start = std::chrono::steady_clock::now();
	#endif
	error_code = request.remesh_light_operation->step(light_config,
			&light_stats, &light_complete);
	#if defined(DEBUG)
	{
		const uint64_t light_step_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - light_step_start).count());
		if (light_step_us >= 100000U)
			std::fprintf(stderr,
				"[WorldGen] slow remesh light step request=%llu chunk=(%d,%d) "
				"duration_us=%llu scanned=%llu propagated=%llu complete=%d\n",
				static_cast<unsigned long long>(request.request_id),
				request.chunk_x, request.chunk_z,
				static_cast<unsigned long long>(light_step_us),
				static_cast<unsigned long long>(light_stats.scanned_cells),
				static_cast<unsigned long long>(light_stats.propagated_cells),
				light_complete != FT_FALSE ? 1 : 0);
	}
	#endif
	result->light_scanned_cells = light_stats.scanned_cells;
	result->light_propagated_cells = light_stats.propagated_cells;
	result->light_queue_peak = light_stats.queue_peak;
	if (remesh_request_is_cancelled(request))
	{
		result->error_code = FT_ERR_INVALID_STATE;
		request.remesh_in_progress = FT_FALSE;
		return (result);
	}
	if (error_code != FT_ERR_SUCCESS)
	{
		result->error_code = error_code;
		return (result);
	}
	if (light_complete == FT_FALSE)
	{
		request.remesh_in_progress = FT_TRUE;
	#if defined(DEBUG)
		if (request.request_id % 32U == 0U && light_stats.scanned_cells % 65536U == 0U)
			std::fprintf(stderr,
				"[WorldGen] remesh lighting slice chunk=(%d,%d) scanned=%llu "
				"propagated=%llu queue_peak=%llu\n", request.chunk_x,
				request.chunk_z,
				static_cast<unsigned long long>(light_stats.scanned_cells),
				static_cast<unsigned long long>(light_stats.propagated_cells),
				static_cast<unsigned long long>(light_stats.queue_peak));
	#endif
		return (nullptr);
	}
	result->mesh.reset(new (std::nothrow) chunk_mesh());
	if (result->mesh == nullptr)
	{
		result->error_code = FT_ERR_NO_MEMORY;
		return (result);
	}
	if (chunk_mesh_initialize(*result->mesh) != FT_ERR_SUCCESS)
	{
		result->error_code = FT_ERR_NO_MEMORY;
		return (result);
	}
	result->light = std::move(request.remesh_light);
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	mesh_start = std::chrono::steady_clock::now();
	#endif
	result->error_code =
		chunk_mesh_generate_from_chunk_with_neighbors_and_light_lookup(
		*result->mesh, *request.remesh_target, request.chunk_x, request.chunk_z,
		&WorldChunkSnapshotReader::lookup_snapshot_mesh_block, request.snapshot.get(),
		result->light.get(), &voxel_light_build_operation_lookup,
		request.remesh_light_operation.get());
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	{
		const uint64_t mesh_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - mesh_start).count());
		if (mesh_us >= 100000U)
			std::fprintf(stderr,
				"[WorldGen] slow remesh mesh request=%llu chunk=(%d,%d) "
				"duration_us=%llu error=%d\n",
				static_cast<unsigned long long>(request.request_id),
				request.chunk_x, request.chunk_z,
				static_cast<unsigned long long>(mesh_us), result->error_code);
	}
	#endif
	request.remesh_light_operation.reset();
	request.remesh_target.reset();
	request.remesh_in_progress = FT_FALSE;
	if (result->error_code != FT_ERR_SUCCESS)
		result->mesh.reset();
	return (result);
}
