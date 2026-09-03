#include "../../src/chunks/WorldChunkLoader.hpp"
#include "../../src/edits/WorldEditCommand.hpp"
#include "../../src/edits/WorldEditHistory.hpp"
#include "../../src/world/World.hpp"

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
	int32_t				chunk_x;
	int32_t				chunk_z;
	int32_t				local_x;
	int32_t				local_z;
	WorldChunk			*wc;
	game_block_edit_op	recorded_edit;
	int32_t				error_code;

	wc = WorldEditCommand::resolve_chunk(world, entry.edit.world_x,
			entry.edit.world_y, entry.edit.world_z, chunk_x, chunk_z, local_x,
			local_z);
	if (!wc)
		return (FT_ERR_NOT_FOUND);
	error_code = wc->chunk.write_block(local_x, entry.edit.world_y, local_z,
			block_id_to_write);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	world.mark_geometry_changed();
	recorded_edit.world_x = entry.edit.world_x;
	recorded_edit.world_y = entry.edit.world_y;
	recorded_edit.world_z = entry.edit.world_z;
	recorded_edit.block_type = block_id_to_write;
	recorded_edit.tick = world.current_tick;
	(void)wc->chunk.record_dirty_edit(recorded_edit);
	return (WorldChunkLoader::remesh_edited_chunk_border(world.chunks,
			world.chunk_count, chunk_x, chunk_z, local_x, local_z));
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
