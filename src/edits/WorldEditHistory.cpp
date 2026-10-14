#include "../../src/chunks/WorldChunkLoader.hpp"
#include "../../src/edits/WorldEditCommand.hpp"
#include "../../src/edits/WorldEditHistory.hpp"
#include "../../src/coordinates/WorldCoordinates.hpp"
#include "../../src/world/World.hpp"
#include <algorithm>

WorldEditHistory::WorldEditHistory() : undo_count_(0U), redo_count_(0U)
{
}

WorldEditHistory::WorldEditHistory(const WorldEditHistory &other)
	: undo_count_(0U), redo_count_(0U)
{
	(void)other;
}

WorldEditHistory::~WorldEditHistory()
{
}

WorldEditHistory &WorldEditHistory::operator=(const WorldEditHistory &other)
{
	(void)other;
	return (*this);
}

void WorldEditHistory::record(const Record &entry)
{
	uint32_t	index;

	if (this->undo_count_ >= WorldEditHistory::CAPACITY)
	{
		index = 0U;
		while (index < WorldEditHistory::CAPACITY - 1U)
		{
			this->undo_entries_[index] = this->undo_entries_[index + 1U];
			index += 1U;
		}
		this->undo_entries_[WorldEditHistory::CAPACITY - 1U] = entry;
	}
	else
	{
		this->undo_entries_[this->undo_count_] = entry;
		this->undo_count_ += 1U;
	}
	this->redo_count_ = 0U;
	return ;
}

bool WorldEditHistory::can_undo() const
{
	return (this->undo_count_ > 0U);
}

bool WorldEditHistory::can_redo() const
{
	return (this->redo_count_ > 0U);
}

int32_t WorldEditHistory::apply(World &world, const Record &entry,
	uint32_t block_id_to_write)
{
	std::vector<World::FluidCellOverride> staged_overrides;
	int32_t				chunk_x;
	int32_t				chunk_z;
	int32_t				local_x;
	int32_t				local_z;
	WorldChunk			*wc;
	game_block_edit_op	recorded_edit;
	uint32_t			previous_block_id;
	int32_t				error_code;

	wc = WorldEditCommand::resolve_chunk(world, entry.edit.world_x,
			entry.edit.world_y, entry.edit.world_z, chunk_x, chunk_z, local_x,
			local_z);
	if (!wc)
		return (FT_ERR_NOT_FOUND);
	error_code = wc->chunk.read_block(local_x, entry.edit.world_y, local_z,
		&previous_block_id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	try
	{
		staged_overrides = world.fluid_cell_overrides_;
		std::size_t index = 0U;
		for (; index < staged_overrides.size(); ++index)
			if (staged_overrides[index].world_x == entry.edit.world_x
				&& staged_overrides[index].world_y == entry.edit.world_y
				&& staged_overrides[index].world_z == entry.edit.world_z)
				break ;
		if (index == staged_overrides.size())
		{
			if (staged_overrides.size() >= 16384U)
				return (FT_ERR_FULL);
			staged_overrides.push_back({entry.edit.world_x, entry.edit.world_y,
				entry.edit.world_z, block_id_to_write});
		}
		else
			staged_overrides[index].block_id = block_id_to_write;
		std::sort(staged_overrides.begin(), staged_overrides.end(),
			[](const World::FluidCellOverride &left,
				const World::FluidCellOverride &right)
			{
				const int32_t left_chunk_x = WorldCoordinates::floor_divide(
					left.world_x, GAME_VOXEL_CHUNK_WIDTH);
				const int32_t right_chunk_x = WorldCoordinates::floor_divide(
					right.world_x, GAME_VOXEL_CHUNK_WIDTH);
				const int32_t left_chunk_z = WorldCoordinates::floor_divide(
					left.world_z, GAME_VOXEL_CHUNK_DEPTH);
				const int32_t right_chunk_z = WorldCoordinates::floor_divide(
					right.world_z, GAME_VOXEL_CHUNK_DEPTH);
				if (left_chunk_x != right_chunk_x)
					return (left_chunk_x < right_chunk_x);
				if (left_chunk_z != right_chunk_z)
					return (left_chunk_z < right_chunk_z);
				if (left.world_y != right.world_y)
					return (left.world_y < right.world_y);
				if (left.world_x != right.world_x)
					return (left.world_x < right.world_x);
				return (left.world_z < right.world_z);
			});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	error_code = wc->chunk.write_block(local_x, entry.edit.world_y, local_z,
			block_id_to_write);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	wc->mark_content_changed();
	error_code = wc->publish_read_state_after_block_edit(local_x,
		entry.edit.world_y, local_z, block_id_to_write);
	if (error_code != FT_ERR_SUCCESS)
	{
		(void)wc->chunk.write_block(local_x, entry.edit.world_y, local_z,
			previous_block_id);
		return (error_code);
	}
	world.fluid_cell_overrides_.swap(staged_overrides);
	world.mark_geometry_changed();
	world.chunk_streamer.mark_remesh_dirty(*wc);
	recorded_edit.world_x = entry.edit.world_x;
	recorded_edit.world_y = entry.edit.world_y;
	recorded_edit.world_z = entry.edit.world_z;
	recorded_edit.block_type = block_id_to_write;
	recorded_edit.tick = world.current_tick;
	(void)wc->chunk.record_dirty_edit(recorded_edit);
	(void)world.invalidate_loom_station_if_block_changed(
		entry.edit.world_x, entry.edit.world_y, entry.edit.world_z,
		block_id_to_write);
	world.chunk_streamer.mark_neighbor_remeshes(chunk_x, chunk_z, true);
	return (FT_ERR_SUCCESS);
}

int32_t WorldEditHistory::undo(World &world)
{
	Record	entry;
	int32_t	error_code;

	if (this->undo_count_ == 0U)
		return (FT_ERR_INVALID_OPERATION);
	this->undo_count_ -= 1U;
	entry = this->undo_entries_[this->undo_count_];
	error_code = WorldEditHistory::apply(world, entry, entry.previous_block_id);
	if (error_code != FT_ERR_SUCCESS)
	{
		this->undo_count_ += 1U;
		return (error_code);
	}
	if (this->redo_count_ < WorldEditHistory::CAPACITY)
	{
		this->redo_entries_[this->redo_count_] = entry;
		this->redo_count_ += 1U;
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldEditHistory::redo(World &world)
{
	Record	entry;
	int32_t	error_code;

	if (this->redo_count_ == 0U)
		return (FT_ERR_INVALID_OPERATION);
	this->redo_count_ -= 1U;
	entry = this->redo_entries_[this->redo_count_];
	error_code = WorldEditHistory::apply(world, entry, entry.edit.block_type);
	if (error_code != FT_ERR_SUCCESS)
	{
		this->redo_count_ += 1U;
		return (error_code);
	}
	if (this->undo_count_ < WorldEditHistory::CAPACITY)
	{
		this->undo_entries_[this->undo_count_] = entry;
		this->undo_count_ += 1U;
	}
	return (FT_ERR_SUCCESS);
}

void WorldEditHistory::clear()
{
	this->undo_count_ = 0U;
	this->redo_count_ = 0U;
	return ;
}
