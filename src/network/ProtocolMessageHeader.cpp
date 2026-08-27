#include "../../src/network/ProtocolMessageHeader.hpp"

ProtocolMessageHeader::ProtocolMessageHeader() : message_type(0U),
	payload_size(0U)
{
}

ProtocolMessageHeader::ProtocolMessageHeader(
	const ProtocolMessageHeader &other) : message_type(other.message_type),
	payload_size(other.payload_size)
{
}

ProtocolMessageHeader::~ProtocolMessageHeader()
{
}

ProtocolMessageHeader &ProtocolMessageHeader::operator=(const ProtocolMessageHeader &other)
{
	if (this != &other)
	{
		this->message_type = other.message_type;
		this->payload_size = other.payload_size;
	}
	return (*this);
}

int32_t ProtocolMessageHeader::serialize(ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	error_code = buffer.append_u32_le(this->message_type);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (buffer.append_u32_le(this->payload_size));
}

int32_t ProtocolMessageHeader::deserialize(ft_byte_buffer &buffer) noexcept
{
	int32_t error_code;

	error_code = buffer.read_u32_le(&this->message_type);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (buffer.read_u32_le(&this->payload_size));
}
