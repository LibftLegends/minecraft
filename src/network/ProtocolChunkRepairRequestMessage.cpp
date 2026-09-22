#include "../../src/network/ProtocolChunkRepairRequestMessage.hpp"

ProtocolChunkRepairRequestMessage::ProtocolChunkRepairRequestMessage()
	: protocol_version(GAME_WORLD_DELTA_PROTOCOL_VERSION), session_id(0U),
	world_id(0U), chunk_x(0), chunk_z(0), block_revision(0U),
	light_revision(0U), generation_epoch(0U), requested_section_mask(0U),
	local_content_hash()
{
}

ProtocolChunkRepairRequestMessage::ProtocolChunkRepairRequestMessage(
	const ProtocolChunkRepairRequestMessage &other)
	: protocol_version(other.protocol_version), session_id(other.session_id),
	world_id(other.world_id), chunk_x(other.chunk_x), chunk_z(other.chunk_z),
	block_revision(other.block_revision), light_revision(other.light_revision),
	generation_epoch(other.generation_epoch),
	requested_section_mask(other.requested_section_mask), local_content_hash()
{
	ft_memcpy(this->local_content_hash, other.local_content_hash,
		PROTOCOL_CHUNK_HASH_SIZE);
}

ProtocolChunkRepairRequestMessage::~ProtocolChunkRepairRequestMessage()
{
}

ProtocolChunkRepairRequestMessage &ProtocolChunkRepairRequestMessage::operator=(
	const ProtocolChunkRepairRequestMessage &other)
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
		this->requested_section_mask = other.requested_section_mask;
		ft_memcpy(this->local_content_hash, other.local_content_hash,
			PROTOCOL_CHUNK_HASH_SIZE);
	}
	return (*this);
}

int32_t ProtocolChunkRepairRequestMessage::serialize(
	ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	if (this->protocol_version != GAME_WORLD_DELTA_PROTOCOL_VERSION
		|| this->session_id == 0U || this->world_id == 0U
		|| this->block_revision == 0U || this->light_revision == 0U
		|| this->generation_epoch == 0U
		|| this->requested_section_mask == 0U)
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
		error_code = buffer.append_u16_le(this->requested_section_mask);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append(this->local_content_hash,
		PROTOCOL_CHUNK_HASH_SIZE);
	return (error_code);
}

int32_t ProtocolChunkRepairRequestMessage::deserialize(
	ft_byte_buffer &buffer) noexcept
{
	ProtocolChunkRepairRequestMessage decoded_request;
	uint32_t raw_x;
	uint32_t raw_z;
	ft_size_t initial_read_position;
	int32_t error_code;
	int32_t restore_error;

	initial_read_position = buffer.read_position();
	error_code = buffer.read_u16_le(&decoded_request.protocol_version);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_request.session_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_request.world_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&raw_x);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&raw_z);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_request.block_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_request.light_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_request.generation_epoch);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u16_le(&decoded_request.requested_section_mask);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read(decoded_request.local_content_hash,
		PROTOCOL_CHUNK_HASH_SIZE);
	if (error_code == FT_ERR_SUCCESS)
	{
		decoded_request.chunk_x = static_cast<int32_t>(raw_x);
		decoded_request.chunk_z = static_cast<int32_t>(raw_z);
		if (decoded_request.protocol_version
			!= GAME_WORLD_DELTA_PROTOCOL_VERSION
			|| decoded_request.session_id == 0U
			|| decoded_request.world_id == 0U
			|| decoded_request.block_revision == 0U
			|| decoded_request.light_revision == 0U
			|| decoded_request.generation_epoch == 0U
			|| decoded_request.requested_section_mask == 0U
			|| buffer.remaining() != 0U)
			error_code = FT_ERR_INVALID_ARGUMENT;
	}
	if (error_code != FT_ERR_SUCCESS)
	{
		restore_error = buffer.set_read_position(initial_read_position);
		if (restore_error != FT_ERR_SUCCESS)
			return (restore_error);
		return (error_code);
	}
	*this = decoded_request;
	return (FT_ERR_SUCCESS);
}
