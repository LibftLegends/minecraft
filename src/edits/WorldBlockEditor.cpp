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

int32_t WorldBlockEditor::undo(World &world)
{
	return (world.edit_history.undo(world));
}

int32_t WorldBlockEditor::redo(World &world)
{
	return (world.edit_history.redo(world));
}
