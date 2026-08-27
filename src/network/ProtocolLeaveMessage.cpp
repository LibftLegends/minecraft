#include "../../src/network/ProtocolLeaveMessage.hpp"

ProtocolLeaveMessage::ProtocolLeaveMessage() : client_id(0U)
{
}

ProtocolLeaveMessage::ProtocolLeaveMessage(const ProtocolLeaveMessage &other)
	: client_id(other.client_id)
{
}

ProtocolLeaveMessage::~ProtocolLeaveMessage()
{
}

ProtocolLeaveMessage &ProtocolLeaveMessage::operator=(const ProtocolLeaveMessage &other)
{
	if (this != &other)
		this->client_id = other.client_id;
	return (*this);
}

int32_t ProtocolLeaveMessage::serialize(ft_byte_buffer &buffer) const noexcept
{
	return (buffer.append_u32_le(this->client_id));
}

int32_t ProtocolLeaveMessage::deserialize(ft_byte_buffer &buffer) noexcept
{
	return (buffer.read_u32_le(&this->client_id));
}
