#ifndef WORLD_REPLICATION_SERVER_SESSION_HPP
# define WORLD_REPLICATION_SERVER_SESSION_HPP

# include "ProtocolChunkAcknowledgementMessage.hpp"
# include "ProtocolEditIntentMessage.hpp"
# include "ProtocolChunkRequestMessage.hpp"
# include "ProtocolChunkSyncRequestMessage.hpp"
# include "ProtocolChunkInterestMessage.hpp"
# include "ProtocolChunkRepairRequestMessage.hpp"
# include "ProtocolChunkRepairResponseMessage.hpp"
# include "WorldReplicationService.hpp"
# include "../../Libft/Modules/Networking/networking_replication_protocol.hpp"

# define WORLD_REPLICATION_MAX_SUBSCRIPTIONS 256U
# define WORLD_REPLICATION_MAX_EDIT_RESULTS 256U

typedef int32_t (*world_replication_delta_broadcast_callback)(
	const game_block_delta &delta, uint64_t source_connection_id,
	void *user_data) noexcept;
typedef int32_t (*world_replication_repair_request_callback)(
	const ProtocolChunkRepairRequestMessage &request,
	ProtocolChunkRepairResponseMessage &response,
	void *user_data) noexcept;
typedef int32_t (*world_replication_sync_request_callback)(
	const ProtocolChunkSyncRequestMessage &request,
	uint64_t connection_id, void *user_data) noexcept;

class WorldReplicationServerSession
{
  public:
	WorldReplicationServerSession();
	WorldReplicationServerSession(
		const WorldReplicationServerSession &other);
	~WorldReplicationServerSession();
	WorldReplicationServerSession &operator=(
		const WorldReplicationServerSession &other);

	int32_t initialize(WorldReplicationService &service,
		networking_message_connection &connection,
		uint64_t server_instance_id, uint64_t session_id);
	int32_t process_message(const networking_received_message &message);
	ft_bool is_subscribed(int32_t chunk_x, int32_t chunk_z) const;
	uint32_t subscription_count() const;
	int32_t subscription_at(uint32_t index, int32_t &chunk_x,
		int32_t &chunk_z) const;
	int32_t get_chunk_acknowledgement(int32_t chunk_x, int32_t chunk_z,
		uint64_t &block_revision, uint64_t &light_revision,
		uint64_t &generation_epoch, uint8_t &snapshot_acknowledged) const;
	uint64_t connection_id() const;
	int32_t set_delta_broadcast_callback(
		world_replication_delta_broadcast_callback callback, void *user_data);
	int32_t set_repair_request_callback(
		world_replication_repair_request_callback callback, void *user_data);
	int32_t set_sync_request_callback(
		world_replication_sync_request_callback callback, void *user_data);
	int32_t publish_block_delta(const game_block_delta &delta);
	int32_t publish_light_delta(
		const ProtocolChunkLightDeltaMessage &delta);
	int32_t publish_hash_manifest(
		const ProtocolChunkHashManifestMessage &manifest);
	int32_t send_snapshot(int32_t chunk_x, int32_t chunk_z);
	int32_t destroy();

  private:
	WorldReplicationService *service_;
	networking_message_connection *connection_;
	uint64_t server_instance_id_;
	uint64_t session_id_;
	uint64_t next_message_sequence_;
	int32_t subscribed_chunk_x_[WORLD_REPLICATION_MAX_SUBSCRIPTIONS];
	int32_t subscribed_chunk_z_[WORLD_REPLICATION_MAX_SUBSCRIPTIONS];
	uint64_t acknowledged_block_revision_[WORLD_REPLICATION_MAX_SUBSCRIPTIONS];
	uint64_t acknowledged_light_revision_[WORLD_REPLICATION_MAX_SUBSCRIPTIONS];
	uint64_t acknowledged_generation_epoch_[WORLD_REPLICATION_MAX_SUBSCRIPTIONS];
	uint8_t snapshot_acknowledged_[WORLD_REPLICATION_MAX_SUBSCRIPTIONS];
	uint32_t subscription_count_;
	game_block_change_request recent_edit_requests_[
		WORLD_REPLICATION_MAX_EDIT_RESULTS];
	ProtocolEditResultMessage recent_edit_results_[
		WORLD_REPLICATION_MAX_EDIT_RESULTS];
	uint8_t recent_edit_result_used_[WORLD_REPLICATION_MAX_EDIT_RESULTS];
	uint32_t recent_edit_result_cursor_;
	world_replication_delta_broadcast_callback delta_broadcast_callback_;
	void *delta_broadcast_user_data_;
	world_replication_repair_request_callback repair_request_callback_;
	void *repair_request_user_data_;
	world_replication_sync_request_callback sync_request_callback_;
	void *sync_request_user_data_;
	bool initialized_;

	int32_t process_edit_intent(ft_byte_buffer &payload,
		uint64_t incoming_sequence);
	int32_t process_chunk_request(ft_byte_buffer &payload);
	int32_t process_chunk_sync_request(ft_byte_buffer &payload);
	int32_t process_chunk_interest(ft_byte_buffer &payload);
	int32_t process_acknowledgement(ft_byte_buffer &payload);
	int32_t process_repair_request(ft_byte_buffer &payload);
	int32_t process_cached_edit(const ProtocolEditIntentMessage &intent,
		uint32_t cache_index);
	int32_t cache_edit_result(const ProtocolEditIntentMessage &intent,
		const ProtocolEditResultMessage &result);
	static ft_bool edit_requests_match(
		const game_block_change_request &left,
		const game_block_change_request &right);
	int32_t next_sequence(uint64_t &sequence);
	int32_t subscribe_chunk(int32_t chunk_x, int32_t chunk_z);
	int32_t unsubscribe_chunk(int32_t chunk_x, int32_t chunk_z);
	int32_t acknowledge_chunk(
		const ProtocolChunkAcknowledgementMessage &acknowledgement);
};

#endif
