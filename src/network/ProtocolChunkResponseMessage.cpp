#include "../../src/network/ProtocolChunkResponseMessage.hpp"

ProtocolChunkResponseMessage::ProtocolChunkResponseMessage() : chunk_x(0),
	chunk_z(0), chunk_payload()
{
}

ProtocolChunkResponseMessage::ProtocolChunkResponseMessage(
	const ProtocolChunkResponseMessage &other) : chunk_x(other.chunk_x),
	chunk_z(other.chunk_z), chunk_payload(other.chunk_payload)
{
}

ProtocolChunkResponseMessage::~ProtocolChunkResponseMessage()
{
}

ProtocolChunkResponseMessage &ProtocolChunkResponseMessage::operator=(
	const ProtocolChunkResponseMessage &other)
{
	if (this != &other)
	{
		this->chunk_x = other.chunk_x;
		this->chunk_z = other.chunk_z;
		this->chunk_payload = other.chunk_payload;
	}
	return (*this);
}

int32_t ProtocolChunkResponseMessage::serialize(ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	error_code = buffer.append_u32_le(static_cast<uint32_t>(this->chunk_x));
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = buffer.append_u32_le(static_cast<uint32_t>(this->chunk_z));
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = buffer.append_u32_le(static_cast<uint32_t>(this->chunk_payload.size()));
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (this->chunk_payload.size() == 0U)
		return (FT_ERR_SUCCESS);
	return (buffer.append(this->chunk_payload.data(),
			this->chunk_payload.size()));
}

int32_t ProtocolChunkResponseMessage::deserialize(ft_byte_buffer &buffer) noexcept
{
	uint32_t raw_x;
	uint32_t raw_z;
	uint32_t payload_size;
	int32_t error_code;

	error_code = buffer.read_u32_le(&raw_x);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&raw_z);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&payload_size);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->chunk_x = static_cast<int32_t>(raw_x);
	this->chunk_z = static_cast<int32_t>(raw_z);
	this->chunk_payload.resize(payload_size);
	if (payload_size == 0U)
		return (FT_ERR_SUCCESS);
	return (buffer.read(this->chunk_payload.data(), payload_size));
}
