#include "../../src/network/ProtocolEntityUpdateMessage.hpp"

ProtocolEntityUpdateMessage::ProtocolEntityUpdateMessage() : entity()
{
}

ProtocolEntityUpdateMessage::ProtocolEntityUpdateMessage(
	const ProtocolEntityUpdateMessage &other) : entity(other.entity)
{
}

ProtocolEntityUpdateMessage::~ProtocolEntityUpdateMessage()
{
}

ProtocolEntityUpdateMessage &ProtocolEntityUpdateMessage::operator=(
	const ProtocolEntityUpdateMessage &other)
{
	if (this != &other)
		this->entity = other.entity;
	return (*this);
}

int32_t ProtocolEntityUpdateMessage::serialize(ft_byte_buffer &buffer) const noexcept
{
	return (this->entity.serialize(buffer));
}

int32_t ProtocolEntityUpdateMessage::deserialize(ft_byte_buffer &buffer) noexcept
{
	return (this->entity.deserialize(buffer));
}
