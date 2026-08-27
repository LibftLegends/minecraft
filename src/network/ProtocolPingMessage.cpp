#include "../../src/network/ProtocolPingMessage.hpp"

ProtocolPingMessage::ProtocolPingMessage() : client_send_tick(0ULL)
{
}

ProtocolPingMessage::ProtocolPingMessage(const ProtocolPingMessage &other)
	: client_send_tick(other.client_send_tick)
{
}

ProtocolPingMessage::~ProtocolPingMessage()
{
}

ProtocolPingMessage &ProtocolPingMessage::operator=(const ProtocolPingMessage &other)
{
	if (this != &other)
		this->client_send_tick = other.client_send_tick;
	return (*this);
}

int32_t ProtocolPingMessage::serialize(ft_byte_buffer &buffer) const noexcept
{
	return (buffer.append_u64_le(this->client_send_tick));
}

int32_t ProtocolPingMessage::deserialize(ft_byte_buffer &buffer) noexcept
{
	return (buffer.read_u64_le(&this->client_send_tick));
}
