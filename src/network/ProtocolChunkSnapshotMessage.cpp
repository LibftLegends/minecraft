#include "../../src/network/ProtocolChunkSnapshotMessage.hpp"

ProtocolChunkSnapshotMessage::ProtocolChunkSnapshotMessage()
	: protocol_version(GAME_WORLD_DELTA_PROTOCOL_VERSION), session_id(0U),
	world_id(0U), chunk_x(0), chunk_z(0), block_revision(0U),
	light_revision(0U), generation_epoch(0U), snapshot_payload(), light_payload()
{
}

ProtocolChunkSnapshotMessage::ProtocolChunkSnapshotMessage(
	const ProtocolChunkSnapshotMessage &other)
	: protocol_version(other.protocol_version), session_id(other.session_id),
	world_id(other.world_id), chunk_x(other.chunk_x), chunk_z(other.chunk_z),
	block_revision(other.block_revision), light_revision(other.light_revision),
	generation_epoch(other.generation_epoch), snapshot_payload(
		other.snapshot_payload), light_payload(other.light_payload)
{
}

ProtocolChunkSnapshotMessage::~ProtocolChunkSnapshotMessage()
{
}

ProtocolChunkSnapshotMessage &ProtocolChunkSnapshotMessage::operator=(
	const ProtocolChunkSnapshotMessage &other)
{
	if (this != &other)
	{
		this->protocol_version = other.protocol_version;
		this->session_id = other.session_id;
		this->world_id = other.world_id;
		this->chunk_x = other.chunk_x;
		this->chunk_z = other.chunk_z;
		this->block_revision = other.block_revision;
		this->light_revision = other.light_revision;
		this->generation_epoch = other.generation_epoch;
		this->snapshot_payload = other.snapshot_payload;
		this->light_payload = other.light_payload;
	}
	return (*this);
}

int32_t ProtocolChunkSnapshotMessage::serialize(
	ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	if (this->protocol_version != GAME_WORLD_DELTA_PROTOCOL_VERSION
		|| this->session_id == 0U || this->world_id == 0U
		|| this->block_revision == 0U || this->light_revision == 0U
		|| this->snapshot_payload.size() > PROTOCOL_CHUNK_SNAPSHOT_MAX_PAYLOAD
		|| protocol_chunk_light_payload_validate(
			this->light_payload.empty() ? ft_nullptr : &this->light_payload[0],
			this->light_payload.size()) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = buffer.append_u16_le(this->protocol_version);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->session_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->world_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u32_le(static_cast<uint32_t>(this->chunk_x));
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u32_le(static_cast<uint32_t>(this->chunk_z));
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->block_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->light_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->generation_epoch);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u32_le(static_cast<uint32_t>(
		this->snapshot_payload.size()));
	if (error_code == FT_ERR_SUCCESS && this->snapshot_payload.empty() == false)
		error_code = buffer.append(&this->snapshot_payload[0],
		this->snapshot_payload.size());
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u32_le(static_cast<uint32_t>(
		this->light_payload.size()));
	if (error_code == FT_ERR_SUCCESS && this->light_payload.empty() == false)
		error_code = buffer.append(&this->light_payload[0],
		this->light_payload.size());
	return (error_code);
}

int32_t ProtocolChunkSnapshotMessage::deserialize(
	ft_byte_buffer &buffer) noexcept
{
	ProtocolChunkSnapshotMessage decoded_snapshot;
	uint32_t raw_chunk_x;
	uint32_t raw_chunk_z;
	uint32_t payload_size;
	uint32_t light_payload_size;
	const uint8_t *payload_data;
	const uint8_t *light_payload_data;
	ft_size_t initial_read_position;
	int32_t error_code;
	int32_t restore_error;

	initial_read_position = buffer.read_position();
	error_code = buffer.read_u16_le(&decoded_snapshot.protocol_version);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_snapshot.session_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_snapshot.world_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&raw_chunk_x);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&raw_chunk_z);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_snapshot.block_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_snapshot.light_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_snapshot.generation_epoch);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&payload_size);
	if (error_code == FT_ERR_SUCCESS
		&& payload_size > PROTOCOL_CHUNK_SNAPSHOT_MAX_PAYLOAD)
		error_code = FT_ERR_OUT_OF_RANGE;
	if (error_code == FT_ERR_SUCCESS
		&& static_cast<ft_size_t>(payload_size) > buffer.remaining())
		error_code = FT_ERR_OUT_OF_RANGE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.view(buffer.read_position(), payload_size,
		&payload_data);
	if (error_code == FT_ERR_SUCCESS && payload_size > 0U)
	{
		decoded_snapshot.snapshot_payload.assign(payload_data,
			payload_data + payload_size);
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.skip(payload_size);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&light_payload_size);
	if (error_code == FT_ERR_SUCCESS
		&& light_payload_size > PROTOCOL_CHUNK_LIGHT_DELTA_MAX_PAYLOAD)
		error_code = FT_ERR_OUT_OF_RANGE;
	if (error_code == FT_ERR_SUCCESS
		&& static_cast<ft_size_t>(light_payload_size) > buffer.remaining())
		error_code = FT_ERR_OUT_OF_RANGE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.view(buffer.read_position(), light_payload_size,
		&light_payload_data);
	if (error_code == FT_ERR_SUCCESS)
		error_code = protocol_chunk_light_payload_validate(light_payload_data,
		light_payload_size);
	if (error_code == FT_ERR_SUCCESS && light_payload_size > 0U)
		decoded_snapshot.light_payload.assign(light_payload_data,
			light_payload_data + light_payload_size);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.skip(light_payload_size);
	if (error_code == FT_ERR_SUCCESS)
		decoded_snapshot.chunk_x = static_cast<int32_t>(raw_chunk_x);
	if (error_code == FT_ERR_SUCCESS)
		decoded_snapshot.chunk_z = static_cast<int32_t>(raw_chunk_z);
	if (error_code == FT_ERR_SUCCESS
		&& (decoded_snapshot.protocol_version
			!= GAME_WORLD_DELTA_PROTOCOL_VERSION
			|| decoded_snapshot.session_id == 0U
			|| decoded_snapshot.world_id == 0U
			|| decoded_snapshot.block_revision == 0U
			|| decoded_snapshot.light_revision == 0U
			|| buffer.remaining() != 0U))
		error_code = FT_ERR_INVALID_ARGUMENT;
	if (error_code != FT_ERR_SUCCESS)
	{
		restore_error = buffer.set_read_position(initial_read_position);
		if (restore_error != FT_ERR_SUCCESS)
			return (restore_error);
		return (error_code);
	}
	*this = decoded_snapshot;
	return (FT_ERR_SUCCESS);
}
