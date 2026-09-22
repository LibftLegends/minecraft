#ifndef WORLD_REPLICATION_SERVER_HPP
# define WORLD_REPLICATION_SERVER_HPP

# include "WorldReplicationServerSession.hpp"
# include "WorldReplicationDeltaJournal.hpp"
# include "WorldReplicationLightJournal.hpp"

# define WORLD_REPLICATION_MAX_PEERS 32U

class WorldReplicationServer
{
  public:
	WorldReplicationServer();
	WorldReplicationServer(const WorldReplicationServer &other);
	~WorldReplicationServer();
	WorldReplicationServer &operator=(const WorldReplicationServer &other);

	int32_t initialize(WorldReplicationService &service);
	int32_t add_peer(networking_message_connection &connection,
		uint64_t session_id);
	int32_t add_authenticated_peer(networking_message_connection &connection,
		uint64_t session_id);
	int32_t remove_peer(uint64_t connection_id);
	int32_t set_repair_request_callback(
		world_replication_repair_request_callback callback, void *user_data);
	int32_t process_message(const networking_received_message &message);
	int32_t synchronize_peer(uint64_t connection_id, int32_t chunk_x,
		int32_t chunk_z, uint64_t base_revision);
	int32_t broadcast_light_delta(
		const ProtocolChunkLightDeltaMessage &delta) noexcept;
	int32_t broadcast_hash_manifest(
		const ProtocolChunkHashManifestMessage &manifest) noexcept;
	int32_t synchronize_light_peer(uint64_t connection_id, int32_t chunk_x,
		int32_t chunk_z, uint64_t base_revision);
	int32_t replay_next_block_delta(int32_t chunk_x, int32_t chunk_z,
		uint64_t base_revision, game_block_delta &delta) const;
	ft_bool needs_snapshot(int32_t chunk_x, int32_t chunk_z,
		uint64_t base_revision) const;
	int32_t destroy();

  private:
	WorldReplicationService *service_;
	WorldReplicationServerSession peers_[WORLD_REPLICATION_MAX_PEERS];
	uint8_t peer_active_[WORLD_REPLICATION_MAX_PEERS];
	WorldReplicationDeltaJournal delta_journal_;
	WorldReplicationLightJournal light_journal_;
	bool initialized_;
	world_replication_repair_request_callback repair_request_callback_;
	void *repair_request_user_data_;

	static int32_t broadcast_block_delta(const game_block_delta &delta,
		uint64_t source_connection_id, void *user_data) noexcept;
	static int32_t synchronize_request(
		const ProtocolChunkSyncRequestMessage &request,
		uint64_t connection_id, void *user_data) noexcept;
	int32_t fanout_block_delta(const game_block_delta &delta,
		uint64_t source_connection_id) noexcept;
	int32_t fanout_light_delta(
		const ProtocolChunkLightDeltaMessage &delta) noexcept;
	int32_t fanout_hash_manifest(
		const ProtocolChunkHashManifestMessage &manifest) noexcept;
	int32_t prune_acknowledged_history() noexcept;
	int32_t prune_acknowledged_chunk(int32_t chunk_x, int32_t chunk_z)
		noexcept;
	int32_t add_peer_internal(networking_message_connection &connection,
		uint64_t session_id, ft_bool require_authenticated);
};

#endif
