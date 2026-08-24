#ifndef WORLD_BLOCK_EDITOR_HPP
#define WORLD_BLOCK_EDITOR_HPP

#include "../ft_vox.hpp"
#include "../../src/edits/DeleteBlockCommand.hpp"
#include "../../src/edits/PlaceBlockCommand.hpp"

class World;

class WorldBlockEditor
{
  public:
    WorldBlockEditor();
    WorldBlockEditor(const WorldBlockEditor &other);
    ~WorldBlockEditor();
    WorldBlockEditor &operator=(const WorldBlockEditor &other);

    static int32_t delete_block_at(World &world, int32_t world_x, int32_t world_y, int32_t world_z);
    static int32_t place_block_at(World &world, int32_t world_x, int32_t world_y, int32_t world_z,
                                  uint32_t block_id);
    static int32_t undo(World &world);
    static int32_t redo(World &world);
};

#endif
