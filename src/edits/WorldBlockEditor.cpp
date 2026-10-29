#include "../../src/edits/WorldBlockEditor.hpp"
#include "../../src/world/World.hpp"

WorldBlockEditor::WorldBlockEditor()
{
}

WorldBlockEditor::WorldBlockEditor(const WorldBlockEditor &other)
{
	*this = other;
}

WorldBlockEditor::~WorldBlockEditor()
{
}

WorldBlockEditor &WorldBlockEditor::operator=(const WorldBlockEditor &other)
{
	(void)other;
	return (*this);
}

int32_t WorldBlockEditor::delete_block_at(World &world, int32_t world_x,
	int32_t world_y, int32_t world_z)
{
	DeleteBlockCommand command(world_x, world_y, world_z);
	return (command.execute(world));
}

int32_t WorldBlockEditor::place_block_at(World &world, int32_t world_x,
	int32_t world_y, int32_t world_z, uint32_t block_id)
{
	PlaceBlockCommand command(world_x, world_y, world_z, block_id);
	return (command.execute(world));
}

int32_t WorldBlockEditor::replace_block_state(World &world, int32_t world_x,
	int32_t world_y, int32_t world_z, uint32_t expected_block_id,
	uint32_t replacement_block_id, uint64_t expected_voxel_revision)
{
	int32_t chunk_x;
	int32_t chunk_z;
	int32_t local_x;
	int32_t local_z;
	WorldChunk *chunk;
	uint32_t current_block_id;
	uint8_t existing_light;
	ft_bool incremental_additive_light = FT_FALSE;
	ft_bool incremental_removal_light = FT_FALSE;
	int32_t error_code;

	if (expected_voxel_revision == 0U
		|| voxel_block_is_known(expected_block_id) != FT_TRUE
		|| voxel_block_is_known(replacement_block_id) != FT_TRUE
		|| voxel_block_is_flowing_fluid(expected_block_id) == FT_TRUE
		|| voxel_block_is_flowing_fluid(replacement_block_id) == FT_TRUE
		|| expected_block_id == replacement_block_id)
		return (FT_ERR_INVALID_ARGUMENT);
	chunk = WorldEditCommand::resolve_chunk(world, world_x, world_y, world_z,
		chunk_x, chunk_z, local_x, local_z);
	if (chunk == nullptr)
		return (FT_ERR_NOT_FOUND);
	if (chunk->voxel_revision != expected_voxel_revision)
		return (FT_ERR_INVALID_STATE);
	error_code = chunk->chunk.read_block(local_x, world_y, local_z,
		&current_block_id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (current_block_id != expected_block_id)
		return (FT_ERR_INVALID_STATE);
	existing_light = chunk->light.get(local_x, world_y, local_z);
	error_code = chunk->chunk.write_block(local_x, world_y, local_z,
		replacement_block_id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	chunk->mark_content_changed();
	error_code = chunk->publish_read_state_after_block_edit(local_x, world_y,
		local_z, replacement_block_id);
	if (error_code != FT_ERR_SUCCESS)
	{
		const int32_t edit_error = error_code;
		if (chunk->chunk.write_block(local_x, world_y, local_z,
				current_block_id) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
		chunk->mark_content_changed();
		if (chunk->publish_read_state_after_block_edit(local_x, world_y,
				local_z, current_block_id) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
		return (edit_error);
	}
	if (world.terrain_chunk_store().is_initialized())
	{
		error_code = world.terrain_chunk_store().save_chunk(chunk_x, chunk_z,
			chunk->chunk);
		if (error_code != FT_ERR_SUCCESS)
		{
			const int32_t restore_block_result = chunk->chunk.write_block(
				local_x, world_y, local_z, current_block_id);
			if (restore_block_result != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_STATE);
			chunk->mark_content_changed();
			if (chunk->publish_read_state_after_block_edit(local_x, world_y,
					local_z, current_block_id) != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_STATE);
			return (error_code);
		}
	}
	world.mark_geometry_changed();
	world.chunk_streamer.mark_remesh_dirty(*chunk);
	chunk->cancel_remesh_work();
	{
		game_block_edit_op edit = {};

		edit.world_x = world_x;
		edit.world_y = world_y;
		edit.world_z = world_z;
		edit.block_type = replacement_block_id;
		edit.tick = world.current_tick;
		(void)chunk->chunk.record_dirty_edit(edit);
	}
	world.chunk_streamer.mark_edit_remeshes(chunk_x, chunk_z, local_x,
		world_y, local_z);
	if (voxel_block_emitted_light_level(current_block_id)
		> voxel_block_emitted_light_level(replacement_block_id))
		incremental_removal_light = FT_TRUE;
	else if (voxel_block_emitted_light_level(current_block_id)
		< voxel_block_emitted_light_level(replacement_block_id))
		incremental_additive_light = FT_TRUE;
	else if (replacement_block_id == GAME_VOXEL_AIR_BLOCK)
		incremental_additive_light = FT_TRUE;
	world.chunk_streamer.prioritize_edit_border_remeshes(chunk_x, chunk_z,
		local_x, world_y, local_z, incremental_additive_light,
		incremental_removal_light, current_block_id, replacement_block_id,
		existing_light);
	error_code = world.chunk_streamer.queue_chunk_remesh(*chunk,
		incremental_additive_light, incremental_removal_light, local_x, world_y,
		local_z, current_block_id, replacement_block_id);
	if (error_code != FT_ERR_SUCCESS && error_code != FT_ERR_FULL)
		return (error_code);
	return (FT_ERR_SUCCESS);
}

int32_t WorldBlockEditor::replace_fluid_source(World &world, int32_t world_x,
	int32_t world_y, int32_t world_z, uint32_t expected_block_id,
	uint32_t replacement_block_id, uint64_t expected_voxel_revision)
{
	if ((expected_block_id != GAME_VOXEL_AIR_BLOCK
			&& voxel_block_is_fluid_source(expected_block_id) != FT_TRUE)
		|| (replacement_block_id != GAME_VOXEL_AIR_BLOCK
			&& voxel_block_is_fluid_source(replacement_block_id) != FT_TRUE))
		return (FT_ERR_INVALID_ARGUMENT);
	return (WorldBlockEditor::replace_block_state(world, world_x, world_y,
		world_z, expected_block_id, replacement_block_id,
		expected_voxel_revision));
}

int32_t WorldBlockEditor::undo(World &world)
{
	return (world.edit_history.undo(world));
}

int32_t WorldBlockEditor::redo(World &world)
{
	return (world.edit_history.redo(world));
}
