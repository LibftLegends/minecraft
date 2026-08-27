#include "../../src/network/ProtocolPongMessage.hpp"

ProtocolPongMessage::ProtocolPongMessage() : client_send_tick(0ULL),
	server_tick(0ULL)
{
}

ProtocolPongMessage::ProtocolPongMessage(const ProtocolPongMessage &other)
	: client_send_tick(other.client_send_tick),
	server_tick(other.server_tick)
{
}

ProtocolPongMessage::~ProtocolPongMessage()
{
}

ProtocolPongMessage &ProtocolPongMessage::operator=(const ProtocolPongMessage &other)
{
	if (this != &other)
	{
		this->client_send_tick = other.client_send_tick;
		this->server_tick = other.server_tick;
	}
	return (*this);
}

int32_t ProtocolPongMessage::serialize(ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	error_code = buffer.append_u64_le(this->client_send_tick);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (buffer.append_u64_le(this->server_tick));
}

int32_t ProtocolPongMessage::deserialize(ft_byte_buffer &buffer) noexcept
{
	int32_t error_code;

	error_code = buffer.read_u64_le(&this->client_send_tick);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (buffer.read_u64_le(&this->server_tick));
}
