#ifndef WORLD_REPLICATION_DELTA_JOURNAL_HPP
# define WORLD_REPLICATION_DELTA_JOURNAL_HPP

# include "../ft_vox.hpp"
# include "../../Libft/Modules/Template/vector.hpp"

class WorldReplicationDeltaJournal
{
  public:
	WorldReplicationDeltaJournal();
	WorldReplicationDeltaJournal(const WorldReplicationDeltaJournal &other);
	~WorldReplicationDeltaJournal();
	WorldReplicationDeltaJournal &operator=(
		const WorldReplicationDeltaJournal &other);

	int32_t initialize(uint32_t maximum_entries);
	int32_t append(const game_block_delta &delta);
	int32_t next_delta(int32_t chunk_x, int32_t chunk_z,
		uint64_t base_revision, game_block_delta &delta) const;
	ft_bool needs_snapshot(int32_t chunk_x, int32_t chunk_z,
		uint64_t base_revision) const;
	int32_t latest_chunk_revision(int32_t chunk_x, int32_t chunk_z,
		uint64_t &revision) const;
	int32_t prune_through(int32_t chunk_x, int32_t chunk_z,
		uint64_t revision);
	uint32_t retained_count() const;
	int32_t destroy();

  private:
	ft_vector<game_block_delta> entries_;
	uint32_t maximum_entries_;
	bool initialized_;

	int32_t latest_revision(int32_t chunk_x, int32_t chunk_z,
		uint64_t &revision) const;
};

#endif
