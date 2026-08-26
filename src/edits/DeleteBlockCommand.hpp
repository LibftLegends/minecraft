#ifndef DELETE_BLOCK_COMMAND_HPP
# define DELETE_BLOCK_COMMAND_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../../src/chunks/WorldChunkLoader.hpp"
# include "../../src/coordinates/WorldCoordinates.hpp"
# include "../../src/edits/WorldEditCommand.hpp"
# include "../../src/world/World.hpp"
# include "../ft_vox.hpp"

class DeleteBlockCommand : public WorldEditCommand
{
  public:
	DeleteBlockCommand();
	DeleteBlockCommand(int32_t block_x, int32_t block_y, int32_t block_z);
	DeleteBlockCommand(const DeleteBlockCommand &other);
	~DeleteBlockCommand() override;
	DeleteBlockCommand &operator=(const DeleteBlockCommand &other);

	int32_t execute(World &world) const override;

  private:
	int32_t world_x;
	int32_t world_y;
	int32_t world_z;
};

#endif
