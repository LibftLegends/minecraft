#ifndef WORLD_REPLICATION_SERVICE_HPP
# define WORLD_REPLICATION_SERVICE_HPP

# include "ProtocolEditIntentMessage.hpp"
# include "ProtocolEditResultMessage.hpp"
# include "ProtocolChunkBlockDeltaMessage.hpp"
# include "ProtocolChunkLightDeltaMessage.hpp"
# include "ProtocolChunkAcknowledgementMessage.hpp"
# include "ProtocolChunkSnapshotMessage.hpp"
# include "ProtocolChunkRepairResponseMessage.hpp"
# include "ProtocolChunkRepairRequestMessage.hpp"
# include "ProtocolChunkHashManifestMessage.hpp"
# include "ProtocolMessageHeader.hpp"
# include "../world/World.hpp"
# include "../../Libft/Modules/Networking/networking_replication_protocol.hpp"

class WorldReplicationService
{
  public:
	WorldReplicationService();
	WorldReplicationService(const WorldReplicationService &other);
	~WorldReplicationService();
	WorldReplicationService &operator=(const WorldReplicationService &other);

	int32_t initialize(World &world, uint64_t server_instance_id,
		uint64_t world_id);
	uint64_t server_instance_id() const;
	uint64_t world_id() const;
	int32_t handle_edit_intent(const ProtocolEditIntentMessage &intent,
		ProtocolEditResultMessage &result);
	int32_t validate_chunk_acknowledgement(
		const ProtocolChunkAcknowledgementMessage &acknowledgement,
		uint64_t session_id) const;
	int32_t create_chunk_snapshot(int32_t chunk_x, int32_t chunk_z,
		uint64_t session_id, ProtocolChunkSnapshotMessage &snapshot);
	int32_t create_chunk_hash_manifest(int32_t chunk_x, int32_t chunk_z,
		uint64_t session_id, ProtocolChunkHashManifestMessage &manifest) const;
	int32_t create_chunk_repair_response(
		const ProtocolChunkRepairRequestMessage &request,
		ProtocolChunkRepairResponseMessage &response) const;
	int32_t send_edit_result(networking_message_connection &connection,
		const ProtocolEditResultMessage &result, uint64_t session_id,
		uint64_t message_sequence) const;
	int32_t send_block_delta(networking_message_connection &connection,
		const ProtocolChunkBlockDeltaMessage &delta, uint64_t session_id,
		uint64_t message_sequence) const;
	int32_t send_light_delta(networking_message_connection &connection,
		const ProtocolChunkLightDeltaMessage &delta, uint64_t session_id,
		uint64_t message_sequence) const;
	int32_t send_chunk_snapshot(networking_message_connection &connection,
		const ProtocolChunkSnapshotMessage &snapshot, uint64_t session_id,
		uint64_t message_sequence) const;
	int32_t send_chunk_repair_response(
		networking_message_connection &connection,
		const ProtocolChunkRepairResponseMessage &response,
		uint64_t session_id, uint64_t message_sequence) const;
	int32_t send_chunk_hash_manifest(
		networking_message_connection &connection,
		const ProtocolChunkHashManifestMessage &manifest,
		uint64_t session_id, uint64_t message_sequence) const;
	void destroy();

  private:
	World *world_;
	uint64_t server_instance_id_;
	uint64_t world_id_;
	bool initialized_;

	uint64_t current_revision(const game_block_change_request &request) const;
	int32_t create_canonical_chunk_payload(int32_t chunk_x, int32_t chunk_z,
		ft_byte_buffer &payload) const;
	int32_t create_canonical_chunk_payload(int32_t chunk_x, int32_t chunk_z,
		uint16_t section_mask, ft_byte_buffer &payload) const;
};

#endif
