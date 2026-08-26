#ifndef WORLD_EDIT_HISTORY_HPP
# define WORLD_EDIT_HISTORY_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../../Libft/Modules/Game/game_block_edit_op.hpp"
# include "../ft_vox.hpp"

class	World;

class WorldEditHistory
{
  public:
	/*
	 * M0 shared contract: undo/redo for src/edits/, built on the
	 * block-edit op format {x, y, z, block_type, tick}. Each record
	 * additionally keeps the block that was overwritten so an undo can
	 * restore it; that extra field is local to this history stack and is
	 * not part of the frozen wire/save format (see game_block_edit_op).
	 */
	struct				Record
	{
		game_block_edit_op	edit;
		uint32_t			previous_block_id;
	};

	static const uint32_t	CAPACITY = 256U;

	WorldEditHistory();
	WorldEditHistory(const WorldEditHistory &other);
	~WorldEditHistory();
	WorldEditHistory &operator=(const WorldEditHistory &other);

	void record(const Record &entry);
	bool can_undo() const;
	bool can_redo() const;
	int32_t undo(World &world);
	int32_t redo(World &world);
	void clear();

  private:
	Record					undo_entries_[CAPACITY];
	uint32_t				undo_count_;
	Record					redo_entries_[CAPACITY];
	uint32_t				redo_count_;

	static int32_t apply(World &world, const Record &entry,
		uint32_t block_id_to_write);
};

#endif
