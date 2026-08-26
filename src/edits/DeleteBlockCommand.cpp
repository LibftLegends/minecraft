#include "../../src/edits/DeleteBlockCommand.hpp"

DeleteBlockCommand::DeleteBlockCommand() : world_x(0), world_y(0), world_z(0)
{
}

DeleteBlockCommand::DeleteBlockCommand(int32_t block_x, int32_t block_y,
	int32_t block_z) : world_x(block_x), world_y(block_y), world_z(block_z)
{
}

DeleteBlockCommand::DeleteBlockCommand(const DeleteBlockCommand &other)
	: WorldEditCommand(other), world_x(0), world_y(0), world_z(0)
{
	*this = other;
}

DeleteBlockCommand::~DeleteBlockCommand()
{
}

DeleteBlockCommand &DeleteBlockCommand::operator=(const DeleteBlockCommand &other)
{
	if (this != &other)
	{
		world_x = other.world_x;
		world_y = other.world_y;
		world_z = other.world_z;
	}
	return (*this);
}

int32_t DeleteBlockCommand::execute(World &world) const
{
	int32_t cx, cz, lx, lz;
	WorldChunk *wc = resolve_chunk(world, world_x, world_y, world_z, cx, cz, lx,
			lz);
	if (!wc)
		return (FT_ERR_INVALID_ARGUMENT);
	uint32_t existing_block_id;
	int32_t read_err = wc->chunk.read_block(lx, world_y, lz,
			&existing_block_id);
	if (read_err != FT_ERR_SUCCESS)
		return (read_err);
	if (terrain_block_is_breakable(existing_block_id) == FT_FALSE)
		return (FT_ERR_INVALID_OPERATION);
	int32_t err = wc->chunk.write_block(lx, world_y, lz, GAME_VOXEL_AIR_BLOCK);
	if (err != FT_ERR_SUCCESS)
		return (err);
	wc->voxel_revision += 1U;
	wc->pending_mesh_request_id = 0U;
	WorldEditHistory::Record record;
	record.edit.world_x = world_x;
	record.edit.world_y = world_y;
	record.edit.world_z = world_z;
	record.edit.block_type = GAME_VOXEL_AIR_BLOCK;
	record.edit.tick = world.current_tick;
	record.previous_block_id = existing_block_id;
	(void)wc->chunk.record_dirty_edit(record.edit);
	world.edit_history.record(record);
	return (WorldChunkLoader::remesh_edited_chunk_border(world.chunks,
			world.chunk_count, cx, cz, lx, lz));
}
