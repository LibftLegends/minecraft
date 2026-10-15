#ifndef WORLD_REPLICATION_BLOCK_REPLICA_HPP
# define WORLD_REPLICATION_BLOCK_REPLICA_HPP

# include "ProtocolChunkBlockDeltaMessage.hpp"
# include "ProtocolChunkLightDeltaMessage.hpp"
# include "ProtocolChunkRepairResponseMessage.hpp"
# include "ProtocolChunkSnapshotMessage.hpp"
# include "../../Libft/Modules/Game/game_voxel_chunk.hpp"
# include "../../Libft/Modules/Voxel/voxel_lighting.hpp"

class WorldReplicationBlockReplica
{
  public:
	WorldReplicationBlockReplica();
	WorldReplicationBlockReplica(
		const WorldReplicationBlockReplica &other);
	~WorldReplicationBlockReplica();
	WorldReplicationBlockReplica &operator=(
		const WorldReplicationBlockReplica &other);

	int32_t initialize(uint64_t session_id, uint64_t world_id,
		int32_t chunk_x, int32_t chunk_z);
	int32_t apply_snapshot(const ProtocolChunkSnapshotMessage &snapshot);
	int32_t apply_block_delta(const ProtocolChunkBlockDeltaMessage &delta);
	int32_t apply_light_delta(const ProtocolChunkLightDeltaMessage &delta);
	int32_t apply_canonical_repair(
		const ProtocolChunkRepairResponseMessage &response);
	int32_t read_block(int32_t local_x, int32_t local_y, int32_t local_z,
		uint32_t *block_id) const;
	uint64_t block_revision() const noexcept;
	uint64_t light_revision() const noexcept;
	uint64_t generation_epoch() const noexcept;
	int32_t chunk_x() const noexcept;
	int32_t chunk_z() const noexcept;
	const game_voxel_chunk &chunk() const noexcept;
	const voxel_light_chunk &light() const noexcept;
	int32_t destroy();

  private:
	game_voxel_chunk chunk_;
	voxel_light_chunk light_;
	uint64_t session_id_;
	uint64_t world_id_;
	int32_t chunk_x_;
	int32_t chunk_z_;
	uint64_t block_revision_;
	uint64_t light_revision_;
	uint64_t generation_epoch_;
	bool initialized_;

	int32_t validate_snapshot(
		const ProtocolChunkSnapshotMessage &snapshot) const;
	int32_t validate_delta(const ProtocolChunkBlockDeltaMessage &delta) const;
};

#endif
