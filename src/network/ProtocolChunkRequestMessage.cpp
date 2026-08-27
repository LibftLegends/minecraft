#include "../../src/network/ProtocolChunkRequestMessage.hpp"

ProtocolChunkRequestMessage::ProtocolChunkRequestMessage() : chunk_x(0),
	chunk_z(0)
{
}

ProtocolChunkRequestMessage::ProtocolChunkRequestMessage(
	const ProtocolChunkRequestMessage &other) : chunk_x(other.chunk_x),
	chunk_z(other.chunk_z)
{
}

ProtocolChunkRequestMessage::~ProtocolChunkRequestMessage()
{
}

ProtocolChunkRequestMessage &ProtocolChunkRequestMessage::operator=(
	const ProtocolChunkRequestMessage &other)
{
	if (this != &other)
	{
		this->chunk_x = other.chunk_x;
		this->chunk_z = other.chunk_z;
	}
	return (*this);
}

int32_t ProtocolChunkRequestMessage::serialize(ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	error_code = buffer.append_u32_le(static_cast<uint32_t>(this->chunk_x));
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (buffer.append_u32_le(static_cast<uint32_t>(this->chunk_z)));
}

int32_t ProtocolChunkRequestMessage::deserialize(ft_byte_buffer &buffer) noexcept
{
	uint32_t raw_x;
	uint32_t raw_z;
	int32_t error_code;

	error_code = buffer.read_u32_le(&raw_x);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&raw_z);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->chunk_x = static_cast<int32_t>(raw_x);
	this->chunk_z = static_cast<int32_t>(raw_z);
	return (FT_ERR_SUCCESS);
}
