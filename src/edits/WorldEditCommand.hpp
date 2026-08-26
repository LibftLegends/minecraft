#ifndef WORLD_EDIT_COMMAND_HPP
# define WORLD_EDIT_COMMAND_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../../src/chunks/WorldChunkLoader.hpp"
# include "../../src/coordinates/WorldCoordinates.hpp"
# include "../ft_vox.hpp"

class	World;
class	WorldChunk;

class WorldEditCommand
{
  public:
	WorldEditCommand();
	WorldEditCommand(const WorldEditCommand &other);
	virtual ~WorldEditCommand();
	WorldEditCommand &operator=(const WorldEditCommand &other);

	virtual int32_t execute(World &world) const = 0;

	static WorldChunk *resolve_chunk(World &world, int32_t world_x,
		int32_t world_y, int32_t world_z, int32_t &chunk_x, int32_t &chunk_z,
		int32_t &local_x, int32_t &local_z);
};

#endif
