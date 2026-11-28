#include "../../src/network/ProtocolChunkAcknowledgementMessage.hpp"

ProtocolChunkAcknowledgementMessage::ProtocolChunkAcknowledgementMessage()
	: protocol_version(GAME_WORLD_DELTA_PROTOCOL_VERSION), session_id(0U),
	world_id(0U), chunk_x(0), chunk_z(0), block_revision(0U),
	light_revision(0U), generation_epoch(0U), snapshot_acknowledged(0U)
{
}

ProtocolChunkAcknowledgementMessage::ProtocolChunkAcknowledgementMessage(
	const ProtocolChunkAcknowledgementMessage &other)
	: protocol_version(other.protocol_version), session_id(other.session_id),
	world_id(other.world_id), chunk_x(other.chunk_x), chunk_z(other.chunk_z),
	block_revision(other.block_revision), light_revision(other.light_revision),
	generation_epoch(other.generation_epoch),
	snapshot_acknowledged(other.snapshot_acknowledged)
{
}

ProtocolChunkAcknowledgementMessage::~ProtocolChunkAcknowledgementMessage()
{
}

ProtocolChunkAcknowledgementMessage &ProtocolChunkAcknowledgementMessage::operator=(
	const ProtocolChunkAcknowledgementMessage &other)
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
		this->snapshot_acknowledged = other.snapshot_acknowledged;
	}
	return (*this);
}

int32_t ProtocolChunkAcknowledgementMessage::serialize(
	ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	if (this->protocol_version != GAME_WORLD_DELTA_PROTOCOL_VERSION
		|| this->session_id == 0U || this->world_id == 0U
		|| this->block_revision == 0U || this->light_revision == 0U
		|| this->snapshot_acknowledged > 1U)
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
		error_code = buffer.append_u8(this->snapshot_acknowledged);
	return (error_code);
}

int32_t ProtocolChunkAcknowledgementMessage::deserialize(
	ft_byte_buffer &buffer) noexcept
{
	ProtocolChunkAcknowledgementMessage decoded_acknowledgement;
	uint32_t raw_chunk_x;
	uint32_t raw_chunk_z;
	ft_size_t initial_read_position;
	int32_t error_code;
	int32_t restore_error;

	initial_read_position = buffer.read_position();
	error_code = buffer.read_u16_le(&decoded_acknowledgement.protocol_version);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_acknowledgement.session_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_acknowledgement.world_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&raw_chunk_x);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&raw_chunk_z);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_acknowledgement.block_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_acknowledgement.light_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(
		&decoded_acknowledgement.generation_epoch);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u8(
		&decoded_acknowledgement.snapshot_acknowledged);
	if (error_code == FT_ERR_SUCCESS)
	{
		decoded_acknowledgement.chunk_x = static_cast<int32_t>(raw_chunk_x);
		decoded_acknowledgement.chunk_z = static_cast<int32_t>(raw_chunk_z);
	}
	if (error_code == FT_ERR_SUCCESS
		&& (decoded_acknowledgement.protocol_version
			!= GAME_WORLD_DELTA_PROTOCOL_VERSION
			|| decoded_acknowledgement.session_id == 0U
			|| decoded_acknowledgement.world_id == 0U
			|| decoded_acknowledgement.block_revision == 0U
			|| decoded_acknowledgement.light_revision == 0U
			|| decoded_acknowledgement.snapshot_acknowledged > 1U
			|| buffer.remaining() != 0U))
		error_code = FT_ERR_INVALID_ARGUMENT;
	if (error_code != FT_ERR_SUCCESS)
	{
		restore_error = buffer.set_read_position(initial_read_position);
		if (restore_error != FT_ERR_SUCCESS)
			return (restore_error);
		return (error_code);
	}
	*this = decoded_acknowledgement;
	return (FT_ERR_SUCCESS);
}
