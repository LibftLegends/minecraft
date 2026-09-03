#include "../../src/world/WorldDeferredEditApplier.hpp"
#include <chrono>

WorldDeferredEditApplier::WorldDeferredEditApplier()
{
}

WorldDeferredEditApplier::WorldDeferredEditApplier(const WorldDeferredEditApplier &other)
{
	(void)other;
}

WorldDeferredEditApplier::~WorldDeferredEditApplier()
{
}

WorldDeferredEditApplier &WorldDeferredEditApplier::operator=(const WorldDeferredEditApplier &other)
{
	(void)other;
	return (*this);
}

bool WorldDeferredEditApplier::deferred_edit_less(const WorldGenerationPipeline::WorldDeferredBlockEdit &left,
	const WorldGenerationPipeline::WorldDeferredBlockEdit &right) noexcept
{
	if (left.world_x != right.world_x)
		return (left.world_x < right.world_x);
	if (left.world_y != right.world_y)
		return (left.world_y < right.world_y);
	if (left.world_z != right.world_z)
		return (left.world_z < right.world_z);
	if (left.request_id != right.request_id)
		return (left.request_id < right.request_id);
	return (left.sequence < right.sequence);
}

int32_t WorldDeferredEditApplier::apply_single_edit(World &world,
	const WorldGenerationPipeline::WorldDeferredBlockEdit &edit,
	std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> &pending,
	std::vector<WorldChunk *> &touched_chunks) noexcept
{
	int32_t chunk_x;
	int32_t chunk_z;
	int32_t local_x;
	int32_t local_z;
	WorldChunk *chunk;

	chunk_x = WorldCoordinates::floor_divide(edit.world_x,
			GAME_VOXEL_CHUNK_WIDTH);
	chunk_z = WorldCoordinates::floor_divide(edit.world_z,
			GAME_VOXEL_CHUNK_DEPTH);
	chunk = world.find_chunk_mutable(chunk_x, chunk_z);
	if (chunk == nullptr || !chunk->initialized)
	{
		pending.push_back(edit);
		return (FT_ERR_SUCCESS);
	}
	local_x = WorldCoordinates::positive_modulo(edit.world_x,
			GAME_VOXEL_CHUNK_WIDTH);
	local_z = WorldCoordinates::positive_modulo(edit.world_z,
			GAME_VOXEL_CHUNK_DEPTH);
	if (chunk->chunk.write_generated_block(local_x, edit.world_y, local_z,
			edit.block_id) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_OPERATION);
	chunk->voxel_revision += 1U;
	chunk->mesh_dirty = true;
	world.mark_geometry_changed();
	for (std::size_t touched_index = 0U;
		touched_index < touched_chunks.size(); ++touched_index)
	{
		if (touched_chunks[touched_index] == chunk)
			return (FT_ERR_SUCCESS);
	}
	touched_chunks.push_back(chunk);
	return (FT_ERR_SUCCESS);
}

int32_t WorldDeferredEditApplier::apply(WorldChunkStreamer &streamer,
	World &world) noexcept
{
	return (WorldDeferredEditApplier::apply(streamer, world,
		streamer.deferred_edits_.size()));
}

int32_t WorldDeferredEditApplier::apply(WorldChunkStreamer &streamer,
	World &world, std::size_t maximum_edits) noexcept
{
	return (WorldDeferredEditApplier::apply(streamer, world, maximum_edits,
		0U));
}

int32_t WorldDeferredEditApplier::apply(WorldChunkStreamer &streamer,
	World &world, std::size_t maximum_edits,
	uint32_t maximum_milliseconds) noexcept
{
	std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> &pending =
		streamer.deferred_pending_edits_;
	int32_t error_code;
	std::size_t index;
	std::size_t processed;
	std::chrono::steady_clock::time_point deadline;
	std::size_t touched_index;
	std::size_t previous_size;
	std::size_t remaining_count;

	if (streamer.deferred_edits_.empty())
		return (FT_ERR_SUCCESS);
	deadline = std::chrono::steady_clock::time_point();
	if (maximum_milliseconds != 0U)
		deadline = std::chrono::steady_clock::now()
			+ std::chrono::milliseconds(maximum_milliseconds);
	pending.clear();
	streamer.deferred_touched_chunks_.clear();
	if (streamer.deferred_touched_chunks_.capacity() <
		streamer.deferred_edits_.size())
		streamer.deferred_touched_chunks_.reserve(
			streamer.deferred_edits_.size());
	if (streamer.deferred_apply_cursor_ > 256U
		&& streamer.deferred_apply_cursor_ * 2U
			>= streamer.deferred_edits_.size())
	{
		remaining_count = streamer.deferred_edits_.size()
			- streamer.deferred_apply_cursor_;
		std::move(streamer.deferred_edits_.begin()
			+ static_cast<std::ptrdiff_t>(streamer.deferred_apply_cursor_),
			streamer.deferred_edits_.end(), streamer.deferred_edits_.begin());
		streamer.deferred_edits_.resize(remaining_count);
		streamer.deferred_sorted_end_ -= streamer.deferred_apply_cursor_;
		streamer.deferred_apply_cursor_ = 0U;
	}
	if (!streamer.deferred_edits_sorted_)
	{
		if (streamer.deferred_sorted_end_ < streamer.deferred_apply_cursor_)
			streamer.deferred_sorted_end_ = streamer.deferred_apply_cursor_;
		if (streamer.deferred_sorted_end_ < streamer.deferred_edits_.size())
		{
			std::sort(streamer.deferred_edits_.begin()
				+ static_cast<std::ptrdiff_t>(streamer.deferred_sorted_end_),
				streamer.deferred_edits_.end(),
				&WorldDeferredEditApplier::deferred_edit_less);
			std::inplace_merge(streamer.deferred_edits_.begin()
				+ static_cast<std::ptrdiff_t>(streamer.deferred_apply_cursor_),
				streamer.deferred_edits_.begin()
				+ static_cast<std::ptrdiff_t>(streamer.deferred_sorted_end_),
				streamer.deferred_edits_.end(),
				&WorldDeferredEditApplier::deferred_edit_less);
		}
		streamer.deferred_sorted_end_ = streamer.deferred_edits_.size();
		streamer.deferred_edits_sorted_ = true;
	}
	index = streamer.deferred_apply_cursor_;
	processed = 0U;
	while (index < streamer.deferred_edits_.size()
		&& processed < maximum_edits
		&& (processed == 0U || maximum_milliseconds == 0U
			|| std::chrono::steady_clock::now() < deadline))
	{
		error_code = WorldDeferredEditApplier::apply_single_edit(world,
			streamer.deferred_edits_[index], pending,
			streamer.deferred_touched_chunks_);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		index += 1U;
		processed += 1U;
	}
	streamer.deferred_apply_cursor_ = index;
	previous_size = streamer.deferred_edits_.size();
	if (!pending.empty())
	{
		std::sort(pending.begin(), pending.end(),
			&WorldDeferredEditApplier::deferred_edit_less);
		streamer.deferred_edits_.insert(streamer.deferred_edits_.end(),
			pending.begin(), pending.end());
		pending.clear();
		streamer.deferred_edits_sorted_ = false;
		streamer.deferred_sorted_end_ = previous_size;
	}
	if (streamer.deferred_apply_cursor_ == streamer.deferred_edits_.size())
	{
		streamer.deferred_edits_.clear();
		streamer.deferred_apply_cursor_ = 0U;
		streamer.deferred_sorted_end_ = 0U;
		streamer.deferred_edits_sorted_ = false;
	}
	touched_index = 0U;
	while (touched_index < streamer.deferred_touched_chunks_.size())
	{
		error_code = streamer.queue_chunk_remesh(
			*streamer.deferred_touched_chunks_[touched_index]);
		if (error_code != FT_ERR_SUCCESS && error_code != FT_ERR_FULL)
			return (error_code);
		touched_index += 1U;
	}
	streamer.deferred_touched_chunks_.clear();
	return (FT_ERR_SUCCESS);
}
