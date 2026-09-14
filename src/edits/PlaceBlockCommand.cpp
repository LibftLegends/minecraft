#include "../../src/edits/PlaceBlockCommand.hpp"
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
# include <cstdio>
#endif

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
	int32_t remesh_error;
	ft_bool incremental_removal_light;
	uint8_t existing_light;
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
	existing_light = wc->light.get(lx, world_y, lz);
	err = wc->chunk.write_block(lx, world_y, lz, block_id);
	if (err != FT_ERR_SUCCESS)
		return (err);
	wc->mark_content_changed();
	err = wc->publish_read_state_after_block_edit(lx, world_y, lz, block_id);
	if (err != FT_ERR_SUCCESS)
		return (err);
	world.mark_geometry_changed();
	world.chunk_streamer.mark_remesh_dirty(*wc);
	wc->cancel_remesh_work();
	WorldEditHistory::Record record;
	record.edit.world_x = world_x;
	record.edit.world_y = world_y;
	record.edit.world_z = world_z;
	record.edit.block_type = block_id;
	record.edit.tick = world.current_tick;
	record.previous_block_id = current_block_id;
	(void)wc->chunk.record_dirty_edit(record.edit);
	world.edit_history.record(record);
	world.chunk_streamer.mark_edit_remeshes(cx, cz, lx, world_y, lz);
	/* Turning air into a solid block can remove sky/block light from the
	 * affected frontier.  Keep this as one explicit classification so the
	 * worker cannot accidentally receive a contradictory seed state. */
	incremental_removal_light = FT_TRUE;
	world.chunk_streamer.prioritize_edit_border_remeshes(cx, cz, lx,
		world_y, lz, FT_FALSE, incremental_removal_light,
		GAME_VOXEL_AIR_BLOCK, block_id, existing_light);
	/* Submit only the edited chunk immediately. Neighbor snapshots are large;
	 * their dirty marks are consumed one at a time by the persistent scheduler. */
	remesh_error = world.chunk_streamer.queue_chunk_remesh(*wc,
		FT_FALSE, incremental_removal_light, lx, world_y, lz,
		GAME_VOXEL_AIR_BLOCK, block_id);
	if (remesh_error != FT_ERR_SUCCESS && remesh_error != FT_ERR_FULL)
		return (remesh_error);
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (wc->voxel_revision % 32U == 0U)
	{
	std::fprintf(stderr,
		"[RendererTrace] place chunk=(%d,%d) voxel=%llu remesh=%d pending=%llu\n",
		cx, cz, static_cast<unsigned long long>(wc->voxel_revision),
		remesh_error,
		static_cast<unsigned long long>(wc->pending_mesh_request_id));
	}
	#endif
	return (FT_ERR_SUCCESS);
}
