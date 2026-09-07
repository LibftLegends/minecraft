#ifndef WORLD_REPLICATION_SERVER_RUNTIME_HPP
# define WORLD_REPLICATION_SERVER_RUNTIME_HPP

# include "WorldReplicationTransportPump.hpp"

class WorldReplicationServerRuntime
{
  public:
	WorldReplicationServerRuntime();
	WorldReplicationServerRuntime(
		const WorldReplicationServerRuntime &other);
	~WorldReplicationServerRuntime();
	WorldReplicationServerRuntime &operator=(
		const WorldReplicationServerRuntime &other);

	int32_t initialize(World &world, networking_message_transport &transport,
		uint64_t server_instance_id, uint64_t world_id);
	int32_t listen(const networking_message_endpoint &endpoint);
	int32_t add_peer(networking_message_connection &connection,
		uint64_t session_id);
	int32_t add_authenticated_peer(
		networking_message_connection &connection, uint64_t session_id);
	int32_t set_repair_request_callback(
		world_replication_repair_request_callback callback, void *user_data);
	int32_t create_chunk_hash_manifest(int32_t chunk_x, int32_t chunk_z,
		uint64_t session_id, ProtocolChunkHashManifestMessage &manifest) const;
	int32_t broadcast_hash_manifest(
		const ProtocolChunkHashManifestMessage &manifest) noexcept;
	int32_t start_worker();
	int32_t pump(int32_t timeout_milliseconds, uint32_t maximum_messages,
		uint32_t &processed_messages);
	int32_t tick(int32_t timeout_milliseconds, uint32_t maximum_messages,
		uint32_t &processed_messages);
	int32_t stop_worker();
	int32_t destroy();

  private:
	WorldReplicationService service_;
	WorldReplicationServer server_;
	WorldReplicationTransportPump pump_;
	networking_message_transport *transport_;
	bool worker_started_;
	bool initialized_;
};

#endif
