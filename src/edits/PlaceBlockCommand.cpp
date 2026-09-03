#include "../../src/edits/PlaceBlockCommand.hpp"

PlaceBlockCommand::PlaceBlockCommand() : world_x(0), world_y(0), world_z(0),
	block_id(0)
{
}

PlaceBlockCommand::PlaceBlockCommand(int32_t block_x, int32_t block_y,
	int32_t block_z, uint32_t selected_block_id) : world_x(block_x),
	world_y(block_y), world_z(block_z), block_id(selected_block_id)
{
}

PlaceBlockCommand::PlaceBlockCommand(const PlaceBlockCommand &other)
	: WorldEditCommand(other), world_x(0), world_y(0), world_z(0),
	block_id(0)
{
	*this = other;
}

PlaceBlockCommand::~PlaceBlockCommand()
{
}

PlaceBlockCommand &PlaceBlockCommand::operator=(const PlaceBlockCommand &other)
{
	if (this != &other)
	{
		world_x = other.world_x;
		world_y = other.world_y;
		world_z = other.world_z;
		block_id = other.block_id;
	}
	return (*this);
}

int32_t PlaceBlockCommand::execute(World &world) const
{
	if (block_id == GAME_VOXEL_AIR_BLOCK
		|| voxel_block_is_known(block_id) != FT_TRUE
		|| voxel_block_is_breakable(block_id) != FT_TRUE)
		return (FT_ERR_INVALID_ARGUMENT);
	int32_t cx, cz, lx, lz;
	WorldChunk *wc = resolve_chunk(world, world_x, world_y, world_z, cx, cz, lx,
			lz);
	if (!wc)
		return (FT_ERR_NOT_FOUND);
	uint32_t current_block_id;
	int32_t err = wc->chunk.read_block(lx, world_y, lz, &current_block_id);
	if (err != FT_ERR_SUCCESS)
		return (err);
	if (current_block_id != GAME_VOXEL_AIR_BLOCK)
		return (FT_ERR_ALREADY_EXISTS);
	err = wc->chunk.write_block(lx, world_y, lz, block_id);
	if (err != FT_ERR_SUCCESS)
		return (err);
	wc->voxel_revision += 1U;
	world.mark_geometry_changed();
	wc->pending_mesh_request_id = 0U;
	WorldEditHistory::Record record;
	record.edit.world_x = world_x;
	record.edit.world_y = world_y;
	record.edit.world_z = world_z;
	record.edit.block_type = block_id;
	record.edit.tick = world.current_tick;
	record.previous_block_id = current_block_id;
	(void)wc->chunk.record_dirty_edit(record.edit);
	world.edit_history.record(record);
	return (WorldChunkLoader::remesh_edited_chunk_border(world.chunks,
			world.chunk_count, cx, cz, lx, lz));
}
