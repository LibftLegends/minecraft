#ifndef WORLD_REPLICATION_LIGHT_JOURNAL_HPP
# define WORLD_REPLICATION_LIGHT_JOURNAL_HPP

# include "ProtocolChunkLightDeltaMessage.hpp"
# include "../../Libft/Modules/Template/vector.hpp"

class WorldReplicationLightJournal
{
  public:
	WorldReplicationLightJournal();
	WorldReplicationLightJournal(const WorldReplicationLightJournal &other);
	~WorldReplicationLightJournal();
	WorldReplicationLightJournal &operator=(
		const WorldReplicationLightJournal &other);

	int32_t initialize(uint32_t maximum_entries);
	int32_t append(const ProtocolChunkLightDeltaMessage &delta);
	int32_t next_delta(int32_t chunk_x, int32_t chunk_z,
		uint64_t base_revision, ProtocolChunkLightDeltaMessage &delta) const;
	ft_bool needs_snapshot(int32_t chunk_x, int32_t chunk_z,
		uint64_t base_revision) const;
	int32_t latest_chunk_revision(int32_t chunk_x, int32_t chunk_z,
		uint64_t &revision) const;
	int32_t prune_through(int32_t chunk_x, int32_t chunk_z,
		uint64_t revision);
	int32_t destroy();

  private:
	ft_vector<ProtocolChunkLightDeltaMessage> entries_;
	uint32_t maximum_entries_;
	bool initialized_;

	int32_t latest_revision(int32_t chunk_x, int32_t chunk_z,
		uint64_t &revision) const;
};

#endif
