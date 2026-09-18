#include "../../src/network/WorldReplicationService.hpp"

static int32_t replication_encode_light_snapshot(
	const voxel_light_chunk &light, ft_byte_buffer &output)
{
	std::vector<ProtocolChunkLightCell> cells;
	ProtocolChunkLightCell cell;
	uint32_t local_x;
	uint32_t local_y;
	uint32_t local_z;
	uint8_t light_value;

	for (local_z = 0U; local_z < GAME_VOXEL_CHUNK_DEPTH; ++local_z)
	{
		for (local_y = 0U; local_y < GAME_VOXEL_CHUNK_HEIGHT; ++local_y)
		{
			for (local_x = 0U; local_x < GAME_VOXEL_CHUNK_WIDTH; ++local_x)
			{
				light_value = light.get(static_cast<int32_t>(local_x),
					static_cast<int32_t>(local_y),
					static_cast<int32_t>(local_z));
				if (light_value == 0U)
					continue ;
				cell.cell_index = local_z * GAME_VOXEL_CHUNK_HEIGHT
					* GAME_VOXEL_CHUNK_WIDTH + local_y
					* GAME_VOXEL_CHUNK_WIDTH + local_x;
				cell.packed_light = light_value;
				cells.push_back(cell);
			}
		}
	}
	return (protocol_chunk_light_payload_append(cells, output));
}
#include "../../Libft/Modules/Crypto/crypto_primitives.hpp"
#include <memory>
#include <new>

WorldReplicationService::WorldReplicationService() : world_(nullptr),
	server_instance_id_(0U), world_id_(0U), initialized_(false)
{
}

WorldReplicationService::WorldReplicationService(
	const WorldReplicationService &other) : world_(nullptr),
	server_instance_id_(0U), world_id_(0U), initialized_(false)
{
	(void)other;
}

WorldReplicationService::~WorldReplicationService()
{
	this->destroy();
}

WorldReplicationService &WorldReplicationService::operator=(
	const WorldReplicationService &other)
{
	(void)other;
	return (*this);
}

int32_t WorldReplicationService::initialize(World &world,
	uint64_t server_instance_id, uint64_t world_id)
{
	if (this->initialized_)
		return (FT_ERR_ALREADY_INITIALISED);
	if (server_instance_id == 0U || world_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	this->world_ = &world;
	this->server_instance_id_ = server_instance_id;
	this->world_id_ = world_id;
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

uint64_t WorldReplicationService::server_instance_id() const
{
	return (this->server_instance_id_);
}

uint64_t WorldReplicationService::world_id() const
{
	return (this->world_id_);
}

void WorldReplicationService::destroy()
{
	this->world_ = nullptr;
	this->server_instance_id_ = 0U;
	this->world_id_ = 0U;
	this->initialized_ = false;
}

uint64_t WorldReplicationService::current_revision(
	const game_block_change_request &request) const
{
	const WorldChunk *world_chunk;

	if (this->world_ == nullptr)
		return (0U);
	world_chunk = this->world_->find_chunk(request.chunk_x, request.chunk_z);
	if (world_chunk == nullptr)
		return (0U);
	return (world_chunk->chunk.get_revision());
}

int32_t WorldReplicationService::handle_edit_intent(
	const ProtocolEditIntentMessage &intent, ProtocolEditResultMessage &result)
{
	game_block_delta delta;
	int32_t error_code;

	if (!this->initialized_ || this->world_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	result = ProtocolEditResultMessage();
	result.protocol_version = GAME_WORLD_DELTA_PROTOCOL_VERSION;
	result.session_id = intent.request.session_id;
	result.request_id = intent.request.request_id;
	result.authoritative_revision = this->current_revision(intent.request);
	if (intent.request.protocol_version != GAME_WORLD_DELTA_PROTOCOL_VERSION
		|| intent.request.session_id == 0U || intent.request.request_id == 0U
		|| intent.request.world_id != this->world_id_)
	{
		result.result_code = FT_ERR_INVALID_ARGUMENT;
		return (FT_ERR_SUCCESS);
	}
	error_code = this->world_->apply_authoritative_block_change(
		intent.request, &delta);
	result.result_code = error_code;
	if (error_code != FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	result.accepted = 1U;
	result.authoritative_revision = delta.revision;
	result.delta = delta;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationService::validate_chunk_acknowledgement(
	const ProtocolChunkAcknowledgementMessage &acknowledgement,
	uint64_t session_id) const
{
	const WorldChunk *world_chunk;
	uint64_t current_block_revision;
	uint64_t current_light_revision;

	if (!this->initialized_ || this->world_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	if (session_id == 0U || acknowledgement.session_id != session_id
		|| acknowledgement.world_id != this->world_id_
		|| acknowledgement.block_revision == 0U
		|| acknowledgement.light_revision == 0U
		|| acknowledgement.generation_epoch == 0U
		|| acknowledgement.snapshot_acknowledged > 1U)
		return (FT_ERR_INVALID_ARGUMENT);
	world_chunk = this->world_->find_chunk(acknowledgement.chunk_x,
		acknowledgement.chunk_z);
	if (world_chunk == nullptr || world_chunk->initialized == false)
		return (FT_ERR_NOT_FOUND);
	current_block_revision = world_chunk->chunk.get_revision();
	current_light_revision = world_chunk->light_revision;
	if (acknowledgement.block_revision > current_block_revision
		|| (current_light_revision != 0U
			&& acknowledgement.light_revision > current_light_revision))
		return (FT_ERR_OUT_OF_RANGE);
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationService::create_chunk_snapshot(int32_t chunk_x,
	int32_t chunk_z, uint64_t session_id,
	ProtocolChunkSnapshotMessage &snapshot)
{
	WorldChunk *world_chunk;
	ft_byte_buffer payload;
	ft_byte_buffer light_payload;
	const uint8_t *payload_data;
	const uint8_t *light_payload_data;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_ || this->world_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	if (session_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	world_chunk = this->world_->find_chunk_mutable(chunk_x, chunk_z);
	if (world_chunk == nullptr)
		return (FT_ERR_NOT_FOUND);
	if (world_chunk->initialized == false)
		return (FT_ERR_INVALID_STATE);
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = game_world_delta_snapshot_serialize(world_chunk->chunk,
			payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = light_payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = replication_encode_light_snapshot(world_chunk->light,
			light_payload);
	if (error_code == FT_ERR_SUCCESS
		&& payload.size() > PROTOCOL_CHUNK_SNAPSHOT_MAX_PAYLOAD)
		error_code = FT_ERR_OUT_OF_RANGE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = payload.view(0U, payload.size(), &payload_data);
	if (error_code == FT_ERR_SUCCESS)
		error_code = light_payload.view(0U, light_payload.size(),
		&light_payload_data);
	if (error_code == FT_ERR_SUCCESS)
	{
		try
		{
			snapshot.snapshot_payload.assign(payload_data,
				payload_data + payload.size());
		}
		catch (...)
		{
			error_code = FT_ERR_NO_MEMORY;
		}
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		try
		{
			snapshot.light_payload.assign(light_payload_data,
				light_payload_data + light_payload.size());
		}
		catch (...)
		{
			error_code = FT_ERR_NO_MEMORY;
		}
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		snapshot.protocol_version = GAME_WORLD_DELTA_PROTOCOL_VERSION;
		snapshot.session_id = session_id;
		snapshot.world_id = this->world_id_;
		snapshot.chunk_x = chunk_x;
		snapshot.chunk_z = chunk_z;
		snapshot.block_revision = world_chunk->chunk.get_revision();
		snapshot.light_revision = world_chunk->light_revision;
		if (snapshot.light_revision == 0U)
			snapshot.light_revision = 1U;
		snapshot.generation_epoch = 1U;
	}
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = light_payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationService::create_chunk_hash_manifest(
	int32_t chunk_x, int32_t chunk_z, uint64_t session_id,
	ProtocolChunkHashManifestMessage &manifest) const
{
	const WorldChunk *world_chunk;
	ft_byte_buffer canonical_blocks;
	const uint8_t *canonical_data;
	uint32_t block_id;
	uint8_t digest[PROTOCOL_CHUNK_HASH_SIZE];
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_ || this->world_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	if (session_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	world_chunk = this->world_->find_chunk(chunk_x, chunk_z);
	if (world_chunk == nullptr || world_chunk->initialized == false)
		return (FT_ERR_NOT_FOUND);
	error_code = canonical_blocks.initialize();
	if (error_code == FT_ERR_SUCCESS)
	{
		for (int32_t local_z = 0;
			local_z < GAME_VOXEL_CHUNK_DEPTH && error_code == FT_ERR_SUCCESS;
			++local_z)
		{
			for (int32_t local_y = 0;
				local_y < GAME_VOXEL_CHUNK_HEIGHT
					&& error_code == FT_ERR_SUCCESS; ++local_y)
			{
				for (int32_t local_x = 0;
					local_x < GAME_VOXEL_CHUNK_WIDTH
						&& error_code == FT_ERR_SUCCESS; ++local_x)
				{
					error_code = world_chunk->chunk.read_block(local_x,
						local_y, local_z, &block_id);
					if (error_code == FT_ERR_SUCCESS)
						error_code = canonical_blocks.append_u32_le(block_id);
				}
			}
		}
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = canonical_blocks.view(0U, canonical_blocks.size(),
			&canonical_data);
	if (error_code == FT_ERR_SUCCESS)
		error_code = crypto_sha256_hash(canonical_data,
			canonical_blocks.size(), digest);
	if (error_code == FT_ERR_SUCCESS)
	{
		manifest.protocol_version = GAME_WORLD_DELTA_PROTOCOL_VERSION;
		manifest.session_id = session_id;
		manifest.world_id = this->world_id_;
		manifest.chunk_x = chunk_x;
		manifest.chunk_z = chunk_z;
		manifest.block_revision = world_chunk->chunk.get_revision();
		manifest.light_revision = world_chunk->light_revision;
		if (manifest.light_revision == 0U)
			manifest.light_revision = 1U;
		manifest.generation_epoch = 1U;
		ft_memcpy(manifest.content_hash, digest,
			PROTOCOL_CHUNK_HASH_SIZE);
	}
	destroy_error = canonical_blocks.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationService::create_canonical_chunk_payload(
	int32_t chunk_x, int32_t chunk_z, ft_byte_buffer &payload) const
{
	return (this->create_canonical_chunk_payload(chunk_x, chunk_z, 0xffffU,
		payload));
}

int32_t WorldReplicationService::create_canonical_chunk_payload(
	int32_t chunk_x, int32_t chunk_z, uint16_t section_mask,
	ft_byte_buffer &payload) const
{
	const WorldChunk *world_chunk;
	std::unique_ptr<uint32_t[]> blocks;
	uint32_t block_id;
	uint32_t block_index;
	int32_t error_code;
	int32_t section_index;
	int32_t local_y;
	int32_t local_z;
	int32_t local_x;

	if (!this->initialized_ || this->world_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	if (section_mask == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	world_chunk = this->world_->find_chunk(chunk_x, chunk_z);
	if (world_chunk == nullptr || world_chunk->initialized == false)
		return (FT_ERR_NOT_FOUND);
	error_code = payload.initialize();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	blocks.reset(new (std::nothrow) uint32_t[
		GAME_VOXEL_CHUNK_WIDTH * GAME_VOXEL_CHUNK_DEPTH
		* GAME_VOXEL_CHUNK_HEIGHT]);
	if (!blocks)
		error_code = FT_ERR_NO_MEMORY;
	if (error_code == FT_ERR_SUCCESS)
		error_code = world_chunk->chunk.copy_blocks(blocks.get(),
		GAME_VOXEL_CHUNK_WIDTH * GAME_VOXEL_CHUNK_DEPTH
			* GAME_VOXEL_CHUNK_HEIGHT);
	for (section_index = 0; section_index < GAME_VOXEL_CHUNK_SECTION_COUNT
		&& error_code == FT_ERR_SUCCESS; ++section_index)
	{
		if ((section_mask & (static_cast<uint16_t>(1U << section_index))) == 0U)
			continue ;
		for (local_z = 0; local_z < GAME_VOXEL_CHUNK_DEPTH
			&& error_code == FT_ERR_SUCCESS; ++local_z)
		{
			for (local_y = section_index * GAME_VOXEL_SECTION_EDGE;
			local_y < (section_index + 1) * GAME_VOXEL_SECTION_EDGE
				&& error_code == FT_ERR_SUCCESS; ++local_y)
			{
				for (local_x = 0; local_x < GAME_VOXEL_CHUNK_WIDTH
					&& error_code == FT_ERR_SUCCESS; ++local_x)
				{
					block_index = static_cast<uint32_t>(local_z)
						* GAME_VOXEL_CHUNK_HEIGHT * GAME_VOXEL_CHUNK_WIDTH
						+ static_cast<uint32_t>(local_y)
						* GAME_VOXEL_CHUNK_WIDTH
						+ static_cast<uint32_t>(local_x);
					block_id = blocks[block_index];
					error_code = payload.append_u32_le(block_id);
				}
			}
		}
	}
	if (error_code != FT_ERR_SUCCESS)
	{
		int32_t destroy_error = payload.destroy();
		if (destroy_error != FT_ERR_SUCCESS)
			return (destroy_error);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationService::create_chunk_repair_response(
	const ProtocolChunkRepairRequestMessage &request,
	ProtocolChunkRepairResponseMessage &response) const
{
	const WorldChunk *world_chunk;
	ft_byte_buffer canonical_payload;
	const uint8_t *payload_data;
	uint8_t digest[PROTOCOL_CHUNK_HASH_SIZE];
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_ || this->world_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	if (request.protocol_version != GAME_WORLD_DELTA_PROTOCOL_VERSION
		|| request.session_id == 0U || request.world_id != this->world_id_
		|| request.block_revision == 0U || request.light_revision == 0U
		|| request.generation_epoch == 0U
		|| request.requested_section_mask == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	world_chunk = this->world_->find_chunk(request.chunk_x, request.chunk_z);
	if (world_chunk == nullptr || world_chunk->initialized == false)
		return (FT_ERR_NOT_FOUND);
	error_code = this->create_canonical_chunk_payload(request.chunk_x,
		request.chunk_z, request.requested_section_mask, canonical_payload);
	if (error_code == FT_ERR_SUCCESS
		&& canonical_payload.size() > PROTOCOL_CHUNK_REPAIR_MAX_PAYLOAD)
		error_code = FT_ERR_OUT_OF_RANGE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = canonical_payload.view(0U, canonical_payload.size(),
		&payload_data);
	if (error_code == FT_ERR_SUCCESS)
		error_code = networking_replication_hash_payload(canonical_payload,
		digest);
	if (error_code == FT_ERR_SUCCESS)
	{
		response.protocol_version = GAME_WORLD_DELTA_PROTOCOL_VERSION;
		response.session_id = request.session_id;
		response.world_id = this->world_id_;
		response.chunk_x = request.chunk_x;
		response.chunk_z = request.chunk_z;
		response.block_revision = world_chunk->chunk.get_revision();
		response.light_revision = world_chunk->light_revision;
		if (response.light_revision == 0U)
			response.light_revision = 1U;
		response.generation_epoch = 1U;
		response.repaired_section_mask = request.requested_section_mask;
		ft_memcpy(response.content_hash, digest,
			PROTOCOL_CHUNK_HASH_SIZE);
		try
		{
			response.repair_payload.assign(payload_data,
				payload_data + canonical_payload.size());
		}
		catch (...)
		{
			error_code = FT_ERR_NO_MEMORY;
		}
	}
	destroy_error = canonical_payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationService::send_edit_result(
	networking_message_connection &connection,
	const ProtocolEditResultMessage &result, uint64_t session_id,
	uint64_t message_sequence) const
{
	ft_byte_buffer payload;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (session_id == 0U || message_sequence == 0U
		|| result.session_id != session_id
		|| (result.accepted != 0U
			&& result.delta.world_id != this->world_id_))
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = result.serialize(payload);
	if (error_code == FT_ERR_SUCCESS)
	{
		networking_replication_sender sender;
		error_code = sender.send_reliable_control(connection,
			static_cast<uint16_t>(ProtocolMessageHeader::Type::EDIT_RESULT),
			payload, this->server_instance_id_, session_id, message_sequence);
	}
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationService::send_block_delta(
	networking_message_connection &connection,
	const ProtocolChunkBlockDeltaMessage &delta, uint64_t session_id,
	uint64_t message_sequence) const
{
	ft_byte_buffer payload;
	networking_replication_sender sender;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (session_id == 0U || message_sequence == 0U
		|| delta.delta.session_id != session_id
		|| delta.delta.world_id != this->world_id_)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = delta.serialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = sender.send_reliable_delta(connection,
		static_cast<uint16_t>(ProtocolMessageHeader::Type::CHUNK_BLOCK_DELTA),
		payload, this->server_instance_id_, session_id, message_sequence);
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationService::send_light_delta(
	networking_message_connection &connection,
	const ProtocolChunkLightDeltaMessage &delta, uint64_t session_id,
	uint64_t message_sequence) const
{
	ft_byte_buffer payload;
	networking_replication_sender sender;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (session_id == 0U || message_sequence == 0U
		|| delta.session_id != session_id || delta.world_id != this->world_id_)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = delta.serialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = sender.send_reliable_delta(connection,
		static_cast<uint16_t>(ProtocolMessageHeader::Type::CHUNK_LIGHT_DELTA),
		payload, this->server_instance_id_, session_id, message_sequence);
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationService::send_chunk_snapshot(
	networking_message_connection &connection,
	const ProtocolChunkSnapshotMessage &snapshot, uint64_t session_id,
	uint64_t message_sequence) const
{
	ft_byte_buffer payload;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (session_id == 0U || message_sequence == 0U
		|| snapshot.session_id != session_id
		|| snapshot.world_id != this->world_id_)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = snapshot.serialize(payload);
	if (error_code == FT_ERR_SUCCESS)
	{
		networking_replication_sender sender;
		error_code = sender.send_reliable_snapshot(connection,
			static_cast<uint16_t>(ProtocolMessageHeader::Type::CHUNK_SNAPSHOT),
			payload, this->server_instance_id_, session_id, message_sequence);
	}
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationService::send_chunk_repair_response(
	networking_message_connection &connection,
	const ProtocolChunkRepairResponseMessage &response,
	uint64_t session_id, uint64_t message_sequence) const
{
	ft_byte_buffer payload;
	networking_replication_sender sender;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (session_id == 0U || message_sequence == 0U
		|| response.session_id != session_id
		|| response.world_id != this->world_id_)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = response.serialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = sender.send_reliable_snapshot(connection,
		static_cast<uint16_t>(ProtocolMessageHeader::Type::CHUNK_REPAIR_RESPONSE),
		payload, this->server_instance_id_, session_id, message_sequence);
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationService::send_chunk_hash_manifest(
	networking_message_connection &connection,
	const ProtocolChunkHashManifestMessage &manifest,
	uint64_t session_id, uint64_t message_sequence) const
{
	ft_byte_buffer payload;
	networking_replication_sender sender;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (session_id == 0U || message_sequence == 0U
		|| manifest.session_id != session_id
		|| manifest.world_id != this->world_id_)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = manifest.serialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = sender.send_reliable_control(connection,
		static_cast<uint16_t>(ProtocolMessageHeader::Type::CHUNK_HASH_MANIFEST),
		payload, this->server_instance_id_, session_id, message_sequence);
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}
