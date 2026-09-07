#ifndef WORLD_REPLICATION_CLIENT_HPP
# define WORLD_REPLICATION_CLIENT_HPP

# include "ProtocolChunkBlockDeltaMessage.hpp"
# include "ProtocolChunkLightDeltaMessage.hpp"
# include "ProtocolChunkSnapshotMessage.hpp"
# include "ProtocolChunkAcknowledgementMessage.hpp"
# include "ProtocolChunkRequestMessage.hpp"
# include "ProtocolChunkSyncRequestMessage.hpp"
# include "ProtocolChunkInterestMessage.hpp"
# include "ProtocolChunkRepairRequestMessage.hpp"
# include "ProtocolChunkRepairResponseMessage.hpp"
# include "ProtocolEditIntentMessage.hpp"
# include "ProtocolEditResultMessage.hpp"
# include "ProtocolMessageHeader.hpp"
# include "WorldReplicationReplicaStore.hpp"
# include "../../Libft/Modules/Networking/networking_replication_client.hpp"
# include "../../Libft/Modules/Template/deque.hpp"

typedef int32_t (*world_replication_snapshot_callback)(
	const ProtocolChunkSnapshotMessage &snapshot, void *user_data) noexcept;
typedef int32_t (*world_replication_block_delta_callback)(
	const ProtocolChunkBlockDeltaMessage &delta, void *user_data) noexcept;
typedef int32_t (*world_replication_light_delta_callback)(
	const ProtocolChunkLightDeltaMessage &delta, void *user_data) noexcept;
typedef int32_t (*world_replication_edit_result_callback)(
	const ProtocolEditResultMessage &result, void *user_data) noexcept;
typedef int32_t (*world_replication_repair_callback)(
	const ProtocolChunkRepairResponseMessage &response,
	void *user_data) noexcept;
typedef int32_t (*world_replication_hash_manifest_callback)(
		const ProtocolChunkHashManifestMessage &manifest,
		void *user_data) noexcept;

class WorldReplicationClient
{
  public:
	WorldReplicationClient();
	WorldReplicationClient(const WorldReplicationClient &other);
	~WorldReplicationClient();
	WorldReplicationClient &operator=(const WorldReplicationClient &other);

	int32_t initialize(uint64_t server_instance_id, uint64_t session_id,
		uint64_t world_id,
		uint32_t maximum_messages, uint32_t maximum_payload_bytes,
		uint32_t maximum_operations);
	int32_t set_callbacks(world_replication_snapshot_callback snapshot,
		world_replication_block_delta_callback block_delta,
		world_replication_light_delta_callback light_delta,
		world_replication_edit_result_callback edit_result,
		void *user_data) noexcept;
	/* The store is caller-owned and is updated on the application-drain path
	 * before the corresponding derived-work callback is invoked. */
	int32_t set_replica_store(WorldReplicationReplicaStore *store) noexcept;
	int32_t set_repair_callback(world_replication_repair_callback repair,
		void *user_data) noexcept;
	int32_t set_hash_manifest_callback(
		world_replication_hash_manifest_callback callback,
		void *user_data) noexcept;
	int32_t send_edit_intent(networking_message_connection &connection,
		const ProtocolEditIntentMessage &intent,
		uint64_t message_sequence) const;
	int32_t request_chunk(networking_message_connection &connection,
		int32_t chunk_x, int32_t chunk_z, uint64_t message_sequence) const;
	int32_t request_chunk_sync(networking_message_connection &connection,
		int32_t chunk_x, int32_t chunk_z, uint64_t block_revision,
		uint64_t light_revision, uint64_t message_sequence) const;
	int32_t set_chunk_interest(networking_message_connection &connection,
		int32_t chunk_x, int32_t chunk_z, uint8_t subscribed,
		uint64_t message_sequence) const;
	int32_t send_chunk_acknowledgement(
		networking_message_connection &connection, int32_t chunk_x,
		int32_t chunk_z, uint64_t block_revision, uint64_t light_revision,
		uint64_t generation_epoch, uint8_t snapshot_acknowledged,
		uint64_t message_sequence) const;
	int32_t send_chunk_repair_request(
		networking_message_connection &connection,
		const ProtocolChunkRepairRequestMessage &request,
		uint64_t message_sequence) const;
	int32_t reset_budget();
	uint64_t server_instance_id() const noexcept;
	uint64_t session_id() const noexcept;
	int32_t enqueue_received_message(
		const networking_received_message &message);
	int32_t drain_received_messages(uint32_t maximum_messages,
		uint32_t &processed_messages);
	int32_t apply_received_message(const networking_received_message &message);
	int32_t destroy();

  private:
	networking_replication_client replication_;
	world_replication_snapshot_callback snapshot_callback_;
	world_replication_block_delta_callback block_delta_callback_;
	world_replication_light_delta_callback light_delta_callback_;
	world_replication_edit_result_callback edit_result_callback_;
	world_replication_repair_callback repair_callback_;
	world_replication_hash_manifest_callback hash_manifest_callback_;
	void *callback_user_data_;
	void *repair_callback_user_data_;
	void *hash_manifest_callback_user_data_;
	WorldReplicationReplicaStore *replica_store_;
	uint64_t server_instance_id_;
	uint64_t session_id_;
	uint64_t world_id_;
	ft_deque<networking_received_message *> received_messages_;
	uint32_t maximum_queued_messages_;
	uint64_t maximum_queued_bytes_;
	uint64_t queued_bytes_;
	bool queue_initialized_;
	bool initialized_;

	static int32_t apply_snapshot_payload(const ft_byte_buffer &payload,
		void *user_data) noexcept;
	static int32_t apply_block_delta_payload(const ft_byte_buffer &payload,
		void *user_data) noexcept;
	static int32_t apply_light_delta_payload(const ft_byte_buffer &payload,
		void *user_data) noexcept;
	int32_t apply_edit_result_payload(const ft_byte_buffer &payload);
	int32_t apply_repair_response_payload(const ft_byte_buffer &payload);
	int32_t apply_hash_manifest_payload(const ft_byte_buffer &payload);
	void clear_received_messages();
};

#endif
