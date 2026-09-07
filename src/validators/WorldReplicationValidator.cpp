#include "../../src/validators/WorldReplicationValidator.hpp"
#include "../../src/network/WorldReplicationBlockReplica.hpp"
#include "../../src/network/WorldReplicationCursorStore.hpp"
#include "../../src/network/ProtocolChunkSyncRequestMessage.hpp"
#include "../../Libft/Modules/Networking/networking_replication_protocol.hpp"
#include <cstdio>
#include <cstring>

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
