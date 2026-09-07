#ifndef WORLD_REPLICATION_CLIENT_RUNTIME_HPP
# define WORLD_REPLICATION_CLIENT_RUNTIME_HPP

# include "WorldReplicationTransportPump.hpp"
# include "WorldReplicationCursorStore.hpp"

class WorldReplicationClientRuntime
{
  public:
	WorldReplicationClientRuntime();
	WorldReplicationClientRuntime(
		const WorldReplicationClientRuntime &other);
	~WorldReplicationClientRuntime();
	WorldReplicationClientRuntime &operator=(
		const WorldReplicationClientRuntime &other);

	int32_t initialize(networking_message_transport &transport,
		uint64_t server_instance_id, uint64_t session_id, uint64_t world_id,
		uint32_t maximum_messages, uint32_t maximum_payload_bytes,
		uint32_t maximum_operations);
	int32_t set_callbacks(world_replication_snapshot_callback snapshot,
		world_replication_block_delta_callback block_delta,
		world_replication_light_delta_callback light_delta,
		world_replication_edit_result_callback edit_result,
		void *user_data) noexcept;
	int32_t set_replica_store(WorldReplicationReplicaStore *store) noexcept;
	int32_t set_cursor_store(WorldReplicationCursorStore *store) noexcept;
	int32_t restore_cursor(uint64_t subscription_id,
		networking_replication_peer_cursor &cursor) const;
	int32_t persist_cursor(
		const networking_replication_peer_cursor &cursor) const;
	int32_t set_repair_callback(world_replication_repair_callback repair,
		void *user_data) noexcept;
	int32_t set_hash_manifest_callback(
		world_replication_hash_manifest_callback callback,
		void *user_data) noexcept;
	int32_t start_worker();
	int32_t pump_transport(int32_t timeout_milliseconds,
		uint32_t maximum_messages, uint32_t &processed_messages);
	int32_t advance_frame(uint32_t maximum_messages,
		uint32_t &processed_messages);
	int32_t tick(int32_t timeout_milliseconds,
		uint32_t maximum_transport_messages,
		uint32_t maximum_application_messages,
		uint32_t &processed_transport_messages,
		uint32_t &processed_application_messages);
	int32_t request_chunk_sync(networking_message_connection &connection,
		int32_t chunk_x, int32_t chunk_z, uint64_t block_revision,
		uint64_t light_revision, uint64_t message_sequence) const;
	int32_t stop_worker();
	WorldReplicationClient &client() noexcept;
	int32_t destroy();

  private:
	WorldReplicationClient client_;
	WorldReplicationTransportPump pump_;
	networking_message_transport *transport_;
	WorldReplicationCursorStore *cursor_store_;
	bool worker_started_;
	bool initialized_;
};

#endif
