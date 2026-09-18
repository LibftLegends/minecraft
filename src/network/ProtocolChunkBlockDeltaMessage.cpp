#include "../../src/network/ProtocolChunkBlockDeltaMessage.hpp"

ProtocolChunkBlockDeltaMessage::ProtocolChunkBlockDeltaMessage() : delta()
{
}

ProtocolChunkBlockDeltaMessage::ProtocolChunkBlockDeltaMessage(
	const ProtocolChunkBlockDeltaMessage &other) : delta(other.delta)
{
}

ProtocolChunkBlockDeltaMessage::~ProtocolChunkBlockDeltaMessage()
{
}

ProtocolChunkBlockDeltaMessage &ProtocolChunkBlockDeltaMessage::operator=(
	const ProtocolChunkBlockDeltaMessage &other)
{
	if (this != &other)
		this->delta = other.delta;
	return (*this);
}

int32_t ProtocolChunkBlockDeltaMessage::serialize(
	ft_byte_buffer &buffer) const noexcept
{
	return (game_block_delta_serialize(this->delta, buffer));
}

int32_t ProtocolChunkBlockDeltaMessage::deserialize(
	ft_byte_buffer &buffer) noexcept
{
	game_block_delta decoded_delta;
	ft_size_t initial_read_position;
	int32_t error_code;
	int32_t restore_error;

	initial_read_position = buffer.read_position();
	error_code = game_block_delta_deserialize(decoded_delta, buffer);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (buffer.remaining() != 0U)
	{
		restore_error = buffer.set_read_position(initial_read_position);
		if (restore_error != FT_ERR_SUCCESS)
			return (restore_error);
		return (FT_ERR_INVALID_ARGUMENT);
	}
	this->delta = decoded_delta;
	return (FT_ERR_SUCCESS);
}
