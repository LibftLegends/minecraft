#include "../../src/validators/WorldReplicationValidator.hpp"
#include "../../src/network/WorldReplicationBlockReplica.hpp"
#include "../../src/network/WorldReplicationClient.hpp"
#include "../../src/network/WorldReplicationCursorStore.hpp"
#include "../../src/network/ProtocolChunkSyncRequestMessage.hpp"
#include "../../Libft/Modules/Networking/networking_replication_protocol.hpp"
#include <cstdio>
#include <cstring>

namespace
{
	template <typename Message>
	int32_t queue_chunk_message(WorldReplicationClient &client,
		ProtocolMessageHeader::Type type, const Message &data, uint64_t sequence)
	{
		ft_byte_buffer payload;
		ft_byte_buffer frame;
		networking_received_message message;
		networking_replication_envelope envelope;
		int32_t error = payload.initialize();
		if (error == FT_ERR_SUCCESS)
			error = data.serialize(payload);
		if (error == FT_ERR_SUCCESS)
			error = frame.initialize();
		if (error == FT_ERR_SUCCESS)
		{
			envelope.message_type = static_cast<uint16_t>(type);
			envelope.payload_size = static_cast<uint32_t>(payload.size());
			envelope.server_instance_id = client.server_instance_id();
			envelope.session_id = client.session_id();
			envelope.message_sequence = sequence;
			error = networking_replication_envelope_serialize(envelope, frame);
		}
		if (error == FT_ERR_SUCCESS)
			error = frame.append(payload.data(), payload.size());
		if (error == FT_ERR_SUCCESS)
			error = message.payload.initialize();
		if (error == FT_ERR_SUCCESS)
		{
			message.payload.reserve(frame.size());
			error = message.payload.get_error();
		}
		for (ft_size_t index = 0U; error == FT_ERR_SUCCESS
			&& index < frame.size(); ++index)
			error = message.payload.push_back(frame.data()[index]);
		if (error == FT_ERR_SUCCESS)
			error = client.enqueue_received_message(message);
		return (error);
	}

	int32_t validate_chunk_deletion_delivery(
		const ProtocolChunkSnapshotMessage &first_snapshot)
	{
		WorldReplicationReplicaStore store;
		WorldReplicationClient client;
		struct BlockDelivery
		{
			WorldReplicationReplicaStore &store;
			uint32_t applied;
			uint32_t light_applied;
		} delivery = {store, 0U, 0U};
		ProtocolChunkSnapshotMessage second_snapshot = first_snapshot;
		second_snapshot.chunk_x -= 1;
		int32_t error = store.initialize(2U);
		if (error == FT_ERR_SUCCESS)
			error = client.initialize(11U, first_snapshot.session_id,
				first_snapshot.world_id, 3U, 1024U * 1024U, 1U);
		if (error == FT_ERR_SUCCESS)
			error = client.set_replica_store(&store);
		if (error == FT_ERR_SUCCESS)
			error = client.set_callbacks(
				[](const ProtocolChunkSnapshotMessage &, void *) noexcept
					-> int32_t { return (FT_ERR_SUCCESS); },
				[](const ProtocolChunkBlockDeltaMessage &message,
					void *context) noexcept -> int32_t
				{
					const auto &delta = message.delta;
					auto &state = *static_cast<BlockDelivery *>(context);
					const auto *replica = state.store.find(
						delta.chunk_x, delta.chunk_z);
					uint32_t block_id = 0U;
					if (replica == nullptr
						|| replica->block_revision() != delta.revision
						|| replica->read_block(delta.local_x, delta.local_y,
							delta.local_z, &block_id) != FT_ERR_SUCCESS
						|| block_id != delta.current_block_id)
						return (FT_ERR_INVALID_STATE);
					state.applied += 1U;
					return (FT_ERR_SUCCESS);
				},
				[](const ProtocolChunkLightDeltaMessage &message, void *context) noexcept
					-> int32_t
				{
					auto &state = *static_cast<BlockDelivery *>(context);
					const auto *replica = state.store.find(message.chunk_x, message.chunk_z);
					if (replica == nullptr || replica->light_revision() != message.final_light_revision
						|| replica->light().get(0, 0, 0) != message.light_payload.back())
						return (FT_ERR_INVALID_STATE);
					state.light_applied += 1U;
					return (FT_ERR_SUCCESS);
				},
				[](const ProtocolEditResultMessage &, void *) noexcept
					-> int32_t { return (FT_ERR_SUCCESS); }, &delivery);
		if (error == FT_ERR_SUCCESS)
			error = queue_chunk_message(client,
				ProtocolMessageHeader::Type::CHUNK_SNAPSHOT, first_snapshot, 1U);
		if (error == FT_ERR_SUCCESS)
			error = queue_chunk_message(client,
				ProtocolMessageHeader::Type::CHUNK_SNAPSHOT, second_snapshot, 2U);
		uint32_t processed = 0U;
		if (error == FT_ERR_SUCCESS)
			error = client.drain_received_messages(2U, processed);
		if (error != FT_ERR_SUCCESS || processed != 1U || store.size() != 1U)
			return (FT_ERR_INVALID_STATE);
		if (client.reset_budget() != FT_ERR_SUCCESS
			|| client.drain_received_messages(2U, processed) != FT_ERR_SUCCESS
			|| processed != 1U || store.size() != 2U)
			return (FT_ERR_INVALID_STATE);
		ProtocolChunkLightDeltaMessage light;
		light.session_id = second_snapshot.session_id;
		light.world_id = second_snapshot.world_id;
		light.chunk_x = second_snapshot.chunk_x;
		light.chunk_z = second_snapshot.chunk_z;
		light.base_light_revision = second_snapshot.light_revision;
		light.final_light_revision = second_snapshot.light_revision + 1U;
		light.source_block_revision = second_snapshot.block_revision;
		light.generation_epoch = second_snapshot.generation_epoch;
		/* One packed-light sample at linear index zero. */
		light.light_payload = {1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0x4BU};
		if (queue_chunk_message(client, ProtocolMessageHeader::Type::CHUNK_LIGHT_DELTA,
				light, 3U) != FT_ERR_SUCCESS || client.reset_budget() != FT_ERR_SUCCESS
			|| client.drain_received_messages(1U, processed) != FT_ERR_SUCCESS
			|| processed != 1U || delivery.light_applied != 1U)
			return (FT_ERR_INVALID_STATE);
		ProtocolChunkBlockDeltaMessage deletion;
		deletion.delta.protocol_version = GAME_WORLD_DELTA_PROTOCOL_VERSION;
		deletion.delta.session_id = first_snapshot.session_id;
		deletion.delta.world_id = first_snapshot.world_id;
		deletion.delta.request_id = 1U;
		deletion.delta.chunk_x = first_snapshot.chunk_x;
		deletion.delta.chunk_z = first_snapshot.chunk_z;
		deletion.delta.previous_revision = first_snapshot.block_revision;
		deletion.delta.revision = first_snapshot.block_revision + 1U;
		deletion.delta.current_block_id = GAME_VOXEL_AIR_BLOCK;
		/* The fixture's stone is a player override of generated air. */
		deletion.delta.player_modified = FT_FALSE;
		error = queue_chunk_message(client,
			ProtocolMessageHeader::Type::CHUNK_BLOCK_DELTA, deletion, 4U);
		deletion.delta.request_id += 1U;
		deletion.delta.chunk_x = second_snapshot.chunk_x;
		if (error == FT_ERR_SUCCESS)
			error = queue_chunk_message(client,
				ProtocolMessageHeader::Type::CHUNK_BLOCK_DELTA, deletion, 5U);
		if (error != FT_ERR_SUCCESS)
			return (error);
		for (uint32_t frame = 0U; frame < 2U; ++frame)
		{
			error = client.reset_budget();
			if (error == FT_ERR_SUCCESS)
				error = client.drain_received_messages(2U, processed);
			if (error != FT_ERR_SUCCESS)
				return (error);
			if (processed != 1U)
				return (FT_ERR_INVALID_STATE);
		}
		/* Retransmitted light and deletion messages ahead of a new edit must
		 * leave the replica and the sole apply slot unchanged. */
		ProtocolChunkBlockDeltaMessage placement = deletion;
		placement.delta.request_id += 1U;
		placement.delta.previous_revision = deletion.delta.revision;
		placement.delta.revision += 1U;
		placement.delta.current_block_id = 7U;
		placement.delta.player_modified = FT_TRUE;
		error = queue_chunk_message(client,
			ProtocolMessageHeader::Type::CHUNK_LIGHT_DELTA, light, 6U);
		if (error == FT_ERR_SUCCESS)
			error = queue_chunk_message(client,
				ProtocolMessageHeader::Type::CHUNK_BLOCK_DELTA, deletion, 7U);
		if (error == FT_ERR_SUCCESS)
			error = queue_chunk_message(client,
				ProtocolMessageHeader::Type::CHUNK_BLOCK_DELTA, placement, 8U);
		if (error == FT_ERR_SUCCESS)
			error = client.reset_budget();
		if (error == FT_ERR_SUCCESS)
			error = client.drain_received_messages(3U, processed);
		if (error != FT_ERR_SUCCESS || processed != 3U || delivery.applied != 3U
			|| delivery.light_applied != 1U)
			return (FT_ERR_INVALID_STATE);

		ProtocolChunkBlockDeltaMessage gap = placement;
		gap.delta.request_id += 1U;
		gap.delta.previous_revision += 2U;
		gap.delta.revision += 2U;
		error = queue_chunk_message(client,
			ProtocolMessageHeader::Type::CHUNK_BLOCK_DELTA, deletion, 9U);
		if (error == FT_ERR_SUCCESS)
			error = queue_chunk_message(client,
				ProtocolMessageHeader::Type::CHUNK_BLOCK_DELTA, gap, 10U);
		if (error != FT_ERR_SUCCESS
			|| client.drain_received_messages(2U, processed) != FT_ERR_INVALID_STATE
			|| processed != 2U || delivery.applied != 3U)
			return (FT_ERR_INVALID_STATE);
		return (FT_ERR_SUCCESS);
	}
}

WorldReplicationValidator::WorldReplicationValidator()
{
}

WorldReplicationValidator::WorldReplicationValidator(
	const WorldReplicationValidator &other)
{
	(void)other;
}

WorldReplicationValidator::~WorldReplicationValidator()
{
}

WorldReplicationValidator &WorldReplicationValidator::operator=(
	const WorldReplicationValidator &other)
{
	(void)other;
	return (*this);
}

int WorldReplicationValidator::validate() const
{
	game_voxel_chunk source;
	WorldReplicationBlockReplica replica;
	WorldReplicationCursorStore cursor_store;
	networking_replication_peer_cursor cursor;
	networking_replication_peer_cursor loaded_cursor;
	ProtocolChunkSnapshotMessage snapshot;
	ProtocolChunkSnapshotMessage decoded_snapshot;
	ProtocolChunkRepairResponseMessage repair;
	ProtocolChunkSyncRequestMessage sync_request;
	ProtocolChunkSyncRequestMessage decoded_sync_request;
	ProtocolChunkLightDeltaMessage light_delta;
	std::vector<ProtocolChunkLightCell> light_cells;
	ft_byte_buffer snapshot_payload;
	ft_byte_buffer snapshot_wire;
	ft_byte_buffer section_payload;
	ft_byte_buffer sync_payload;
	ft_byte_buffer truncated_sync_payload;
	ft_byte_buffer light_payload;
	ft_byte_buffer invalid_light_payload;
	const uint8_t *section_data;
	const uint8_t *sync_data;
	const uint8_t *light_data;
	uint8_t digest[PROTOCOL_CHUNK_HASH_SIZE];
	uint32_t block_id;
	uint32_t local_x;
	uint32_t local_y;
	uint32_t local_z;
	int32_t error_code;
	int32_t destroy_error;

	error_code = source.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = source.write_block(0, 0, 0, 7U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = cursor_store.initialize(
		"world_replication_cursor_validator.bin");
	if (error_code == FT_ERR_SUCCESS)
	{
		cursor.server_instance_id = 11U;
		cursor.session_id = 22U;
		cursor.subscription_id = 33U;
		cursor.block_revision = 44U;
		cursor.light_revision = 55U;
		cursor.snapshot_generation = 66U;
		cursor.snapshot_acknowledged = FT_TRUE;
		error_code = cursor_store.save(cursor);
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = cursor_store.load(loaded_cursor);
	if (error_code == FT_ERR_SUCCESS
		&& (loaded_cursor.server_instance_id != cursor.server_instance_id
			|| loaded_cursor.session_id != cursor.session_id
			|| loaded_cursor.subscription_id != cursor.subscription_id
			|| loaded_cursor.block_revision != cursor.block_revision
			|| loaded_cursor.light_revision != cursor.light_revision
			|| loaded_cursor.snapshot_generation
				!= cursor.snapshot_generation
			|| loaded_cursor.snapshot_acknowledged
				!= cursor.snapshot_acknowledged))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& cursor_store.load_for_session(11U, 99U, 33U, loaded_cursor)
			!= FT_ERR_PERMISSION_DENIED)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS
		&& cursor_store.load(loaded_cursor) != FT_ERR_NOT_FOUND)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		sync_request.session_id = 1U;
		sync_request.world_id = 2U;
		sync_request.chunk_x = -3;
		sync_request.chunk_z = 5;
		sync_request.block_revision = 11U;
		sync_request.light_revision = 17U;
		error_code = sync_payload.initialize();
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = sync_request.serialize(sync_payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = sync_payload.view(0U, sync_payload.size(), &sync_data);
	if (error_code == FT_ERR_SUCCESS)
		error_code = truncated_sync_payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = truncated_sync_payload.append(sync_data,
		sync_payload.size() - 1U);
	if (error_code == FT_ERR_SUCCESS)
	{
		decoded_sync_request.session_id = 101U;
		decoded_sync_request.world_id = 202U;
		decoded_sync_request.chunk_x = 303;
		decoded_sync_request.chunk_z = 404;
		decoded_sync_request.block_revision = 505U;
		decoded_sync_request.light_revision = 606U;
		if (decoded_sync_request.deserialize(truncated_sync_payload)
			== FT_ERR_SUCCESS
			|| truncated_sync_payload.read_position() != 0U
			|| decoded_sync_request.session_id != 101U
			|| decoded_sync_request.world_id != 202U
			|| decoded_sync_request.chunk_x != 303
			|| decoded_sync_request.chunk_z != 404
			|| decoded_sync_request.block_revision != 505U
			|| decoded_sync_request.light_revision != 606U)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = decoded_sync_request.deserialize(sync_payload);
	if (error_code == FT_ERR_SUCCESS
		&& (decoded_sync_request.session_id != sync_request.session_id
			|| decoded_sync_request.world_id != sync_request.world_id
			|| decoded_sync_request.chunk_x != sync_request.chunk_x
			|| decoded_sync_request.chunk_z != sync_request.chunk_z
			|| decoded_sync_request.block_revision
			!= sync_request.block_revision
			|| decoded_sync_request.light_revision
			!= sync_request.light_revision))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = snapshot_payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = game_world_delta_snapshot_serialize(source,
		 snapshot_payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = snapshot_payload.view(0U, snapshot_payload.size(),
		&section_data);
	if (error_code == FT_ERR_SUCCESS)
	{
		snapshot.session_id = 1U;
		snapshot.world_id = 2U;
		snapshot.chunk_x = 0;
		snapshot.chunk_z = 0;
		snapshot.block_revision = source.get_revision();
		snapshot.light_revision = 1U;
		snapshot.generation_epoch = 1U;
		snapshot.snapshot_payload.assign(section_data,
			section_data + snapshot_payload.size());
		snapshot.light_payload.push_back(0U);
		snapshot.light_payload.push_back(0U);
		snapshot.light_payload.push_back(0U);
		snapshot.light_payload.push_back(0U);
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = snapshot_wire.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = snapshot.serialize(snapshot_wire);
	if (error_code == FT_ERR_SUCCESS)
		error_code = decoded_snapshot.deserialize(snapshot_wire);
	if (error_code == FT_ERR_SUCCESS)
		error_code = validate_chunk_deletion_delivery(decoded_snapshot);
	if (error_code == FT_ERR_SUCCESS
		&& (decoded_snapshot.block_revision != snapshot.block_revision
			|| decoded_snapshot.light_revision != snapshot.light_revision
			|| decoded_snapshot.snapshot_payload.size()
				!= snapshot.snapshot_payload.size()
			|| decoded_snapshot.light_payload.size()
				!= snapshot.light_payload.size()))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = replica.initialize(1U, 2U, 0, 0);
	if (error_code == FT_ERR_SUCCESS)
		error_code = replica.apply_snapshot(decoded_snapshot);
	if (error_code == FT_ERR_SUCCESS)
		error_code = section_payload.initialize();
	for (local_z = 0U; local_z < GAME_VOXEL_CHUNK_DEPTH
		&& error_code == FT_ERR_SUCCESS; ++local_z)
	{
		for (local_y = 0U; local_y < GAME_VOXEL_SECTION_EDGE
			&& error_code == FT_ERR_SUCCESS; ++local_y)
		{
			for (local_x = 0U; local_x < GAME_VOXEL_CHUNK_WIDTH
				&& error_code == FT_ERR_SUCCESS; ++local_x)
			{
				error_code = source.read_block(
					static_cast<int32_t>(local_x),
					static_cast<int32_t>(local_y),
					static_cast<int32_t>(local_z), &block_id);
				if (error_code == FT_ERR_SUCCESS)
					error_code = section_payload.append_u32_le(block_id);
			}
		}
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = section_payload.view(0U, section_payload.size(),
		&section_data);
	if (error_code == FT_ERR_SUCCESS)
		error_code = networking_replication_hash_payload(section_payload,
		digest);
	if (error_code == FT_ERR_SUCCESS)
	{
		repair.session_id = 1U;
		repair.world_id = 2U;
		repair.chunk_x = 0;
		repair.chunk_z = 0;
		repair.block_revision = source.get_revision();
		repair.light_revision = 1U;
		repair.generation_epoch = 1U;
		repair.repaired_section_mask = 1U;
		repair.payload_format = PROTOCOL_CHUNK_REPAIR_PAYLOAD_CANONICAL_BLOCKS;
		ft_memcpy(repair.content_hash, digest, PROTOCOL_CHUNK_HASH_SIZE);
		repair.repair_payload.assign(section_data,
			section_data + section_payload.size());
		error_code = replica.apply_canonical_repair(repair);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = replica.read_block(0, 0, 0, &block_id);
		if (error_code == FT_ERR_SUCCESS && block_id != 7U)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		ProtocolChunkLightCell first_cell;
		ProtocolChunkLightCell second_cell;
		first_cell.cell_index = 0U;
		first_cell.packed_light = 0x21U;
		second_cell.cell_index =
			PROTOCOL_CHUNK_LIGHT_CELL_COUNT - 1U;
		second_cell.packed_light = 0x43U;
		light_cells.push_back(first_cell);
		light_cells.push_back(second_cell);
		error_code = light_payload.initialize();
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = protocol_chunk_light_payload_append(light_cells,
		light_payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = light_payload.view(0U, light_payload.size(), &light_data);
	if (error_code == FT_ERR_SUCCESS)
	{
		light_delta.session_id = 1U;
		light_delta.world_id = 2U;
		light_delta.chunk_x = 0;
		light_delta.chunk_z = 0;
		light_delta.base_light_revision = 1U;
		light_delta.final_light_revision = 2U;
		light_delta.source_block_revision = source.get_revision();
		light_delta.generation_epoch = 1U;
		light_delta.light_payload.assign(light_data,
			light_data + light_payload.size());
		error_code = replica.apply_light_delta(light_delta);
	}
	if (error_code == FT_ERR_SUCCESS
		&& (replica.light().get(0, 0, 0) != 0x21U
			|| replica.light().get(GAME_VOXEL_CHUNK_WIDTH - 1,
			GAME_VOXEL_CHUNK_HEIGHT - 1,
			GAME_VOXEL_CHUNK_DEPTH - 1) != 0x43U
			|| replica.light_revision() != 2U))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		ProtocolChunkLightCell invalid_cell;
		invalid_cell.cell_index = 0U;
		invalid_cell.packed_light = 0x55U;
		light_cells.push_back(invalid_cell);
		error_code = invalid_light_payload.initialize();
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = invalid_light_payload.append_u8(0xA5U);
	if (error_code == FT_ERR_SUCCESS
		&& (protocol_chunk_light_payload_append(light_cells,
			invalid_light_payload) == FT_ERR_SUCCESS
			|| invalid_light_payload.size() != 1U
			|| invalid_light_payload.data()[0] != 0xA5U))
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		repair.content_hash[0] ^= 1U;
		if (replica.apply_canonical_repair(repair) == FT_ERR_SUCCESS)
			error_code = FT_ERR_INVALID_STATE;
		else if (replica.read_block(0, 0, 0, &block_id) != FT_ERR_SUCCESS
			|| block_id != 7U)
			error_code = FT_ERR_INVALID_STATE;
	}
	destroy_error = section_payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = sync_payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = truncated_sync_payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = light_payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = invalid_light_payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = snapshot_payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = snapshot_wire.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = replica.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = source.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = cursor_store.clear();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "[Validator] network-repair failed: %d\n",
			error_code);
		return (1);
	}
	std::fprintf(stderr, "[Validator] network-repair passed\n");
	return (0);
}
