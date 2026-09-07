#include "../../src/network/ProtocolEditIntentMessage.hpp"

ProtocolEditIntentMessage::ProtocolEditIntentMessage() : request()
{
}

ProtocolEditIntentMessage::ProtocolEditIntentMessage(
	const ProtocolEditIntentMessage &other) : request(other.request)
{
}

ProtocolEditIntentMessage::~ProtocolEditIntentMessage()
{
}

ProtocolEditIntentMessage &ProtocolEditIntentMessage::operator=(
	const ProtocolEditIntentMessage &other)
{
	if (this != &other)
		this->request = other.request;
	return (*this);
}

int32_t ProtocolEditIntentMessage::serialize(ft_byte_buffer &buffer) const noexcept
{
	return (game_block_change_request_serialize(this->request, buffer));
}

int32_t ProtocolEditIntentMessage::deserialize(ft_byte_buffer &buffer) noexcept
{
	game_block_change_request decoded_request;
	ft_size_t initial_read_position;
	int32_t error_code;
	int32_t restore_error;

	initial_read_position = buffer.read_position();
	error_code = game_block_change_request_deserialize(decoded_request, buffer);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (buffer.remaining() != 0U)
	{
		restore_error = buffer.set_read_position(initial_read_position);
		if (restore_error != FT_ERR_SUCCESS)
			return (restore_error);
		return (FT_ERR_INVALID_ARGUMENT);
	}
	this->request = decoded_request;
	return (FT_ERR_SUCCESS);
}
