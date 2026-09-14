#include "../../src/edits/DeleteBlockCommand.hpp"
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
# include <cstdio>
#endif

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
	int32_t remesh_error;
	ft_bool incremental_additive_light;
	ft_bool incremental_removal_light;
	uint8_t existing_light;
	WorldChunk *wc = resolve_chunk(world, world_x, world_y, world_z, cx, cz, lx,
			lz);
	if (!wc)
		return (FT_ERR_INVALID_ARGUMENT);
	existing_light = wc->light.get(lx, world_y, lz);
	uint32_t existing_block_id;
	int32_t read_err = wc->chunk.read_block(lx, world_y, lz,
			&existing_block_id);
	if (read_err != FT_ERR_SUCCESS)
		return (read_err);
	if (voxel_block_is_breakable(existing_block_id) == FT_FALSE)
		return (FT_ERR_INVALID_OPERATION);
	int32_t err = wc->chunk.write_block(lx, world_y, lz, GAME_VOXEL_AIR_BLOCK);
	if (err != FT_ERR_SUCCESS)
		return (err);
	wc->mark_content_changed();
	err = wc->publish_read_state_after_block_edit(lx, world_y, lz,
		GAME_VOXEL_AIR_BLOCK);
	if (err != FT_ERR_SUCCESS)
		return (err);
	world.mark_geometry_changed();
	world.chunk_streamer.mark_remesh_dirty(*wc);
	wc->cancel_remesh_work();
	WorldEditHistory::Record record;
	record.edit.world_x = world_x;
	record.edit.world_y = world_y;
	record.edit.world_z = world_z;
	record.edit.block_type = GAME_VOXEL_AIR_BLOCK;
	record.edit.tick = world.current_tick;
	record.previous_block_id = existing_block_id;
	(void)wc->chunk.record_dirty_edit(record.edit);
	world.edit_history.record(record);
	world.chunk_streamer.mark_edit_remeshes(cx, cz, lx, world_y, lz);
	incremental_additive_light = FT_FALSE;
	incremental_removal_light = FT_FALSE;
	if (voxel_block_emitted_light_level(existing_block_id) == 0U)
		incremental_additive_light = FT_TRUE;
	else
		/* An emitter removal must enter the subtraction frontier even when
		 * the cell also carries sky light.  The old implementation only chose
		 * this path when sky was zero, which sent mixed sky/block cells through
		 * a full relight and exposed the chunk's intermediate dark state. */
		incremental_removal_light = FT_TRUE;
	world.chunk_streamer.prioritize_edit_border_remeshes(cx, cz, lx,
		world_y, lz, incremental_additive_light,
		incremental_removal_light, existing_block_id,
		GAME_VOXEL_AIR_BLOCK, existing_light);
	/* Submit only the edited chunk immediately. Neighbor snapshots are large;
	 * their dirty marks are consumed one at a time by the persistent scheduler. */
	remesh_error = world.chunk_streamer.queue_chunk_remesh(*wc,
		incremental_additive_light, incremental_removal_light, lx, world_y, lz,
		existing_block_id, GAME_VOXEL_AIR_BLOCK);
	if (remesh_error != FT_ERR_SUCCESS && remesh_error != FT_ERR_FULL)
		return (remesh_error);
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (wc->voxel_revision % 32U == 0U)
	{
	std::fprintf(stderr,
		"[RendererTrace] delete chunk=(%d,%d) voxel=%llu remesh=%d pending=%llu\n",
		cx, cz, static_cast<unsigned long long>(wc->voxel_revision),
		remesh_error,
		static_cast<unsigned long long>(wc->pending_mesh_request_id));
	}
	#endif
	return (FT_ERR_SUCCESS);
}
