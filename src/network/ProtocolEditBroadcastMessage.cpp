#include "../../src/network/ProtocolEditBroadcastMessage.hpp"

ProtocolEditBroadcastMessage::ProtocolEditBroadcastMessage() : client_id(0U),
	edit()
{
}

ProtocolEditBroadcastMessage::ProtocolEditBroadcastMessage(
	const ProtocolEditBroadcastMessage &other) : client_id(other.client_id),
	edit(other.edit)
{
}

ProtocolEditBroadcastMessage::~ProtocolEditBroadcastMessage()
{
}

ProtocolEditBroadcastMessage &ProtocolEditBroadcastMessage::operator=(
	const ProtocolEditBroadcastMessage &other)
{
	if (this != &other)
	{
		this->client_id = other.client_id;
		this->edit = other.edit;
	}
	return (*this);
}

int32_t ProtocolEditBroadcastMessage::serialize(ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	error_code = buffer.append_u32_le(this->client_id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (game_block_edit_op_serialize(this->edit, buffer));
}

int32_t ProtocolEditBroadcastMessage::deserialize(ft_byte_buffer &buffer) noexcept
{
	int32_t error_code;

	error_code = buffer.read_u32_le(&this->client_id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (game_block_edit_op_deserialize(this->edit, buffer));
}
