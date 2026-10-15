#ifndef WORLD_REPLICATION_REPLICA_STORE_HPP
# define WORLD_REPLICATION_REPLICA_STORE_HPP

# include "WorldReplicationBlockReplica.hpp"
# include "ProtocolChunkBlockDeltaMessage.hpp"
# include "ProtocolChunkLightDeltaMessage.hpp"
# include "ProtocolChunkSnapshotMessage.hpp"
# include "../../Libft/Modules/Template/vector.hpp"

class WorldReplicationReplicaStore
{
  public:
	WorldReplicationReplicaStore();
	WorldReplicationReplicaStore(const WorldReplicationReplicaStore &other);
	~WorldReplicationReplicaStore();
	WorldReplicationReplicaStore &operator=(
		const WorldReplicationReplicaStore &other);

	int32_t initialize(uint32_t maximum_replicas);
	int32_t apply_snapshot(const ProtocolChunkSnapshotMessage &snapshot);
	int32_t apply_block_delta(const ProtocolChunkBlockDeltaMessage &delta);
	int32_t apply_light_delta(const ProtocolChunkLightDeltaMessage &delta);
	int32_t apply_canonical_repair(
		const ProtocolChunkRepairResponseMessage &response);
	WorldReplicationBlockReplica *find(int32_t chunk_x,
		int32_t chunk_z) noexcept;
	const WorldReplicationBlockReplica *find(int32_t chunk_x,
		int32_t chunk_z) const noexcept;
	uint32_t size() const noexcept;
	int32_t destroy();

  private:
	ft_vector<WorldReplicationBlockReplica *> replicas_;
	uint32_t maximum_replicas_;
	bool initialized_;

	int32_t add_snapshot_replica(
		const ProtocolChunkSnapshotMessage &snapshot);
	void clear_replicas() noexcept;
};

#endif
