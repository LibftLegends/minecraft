#include "../../src/network/ProtocolChunkInterestMessage.hpp"

ProtocolChunkInterestMessage::ProtocolChunkInterestMessage()
	: chunk_x(0), chunk_z(0), subscribed(0U)
{
}

ProtocolChunkInterestMessage::ProtocolChunkInterestMessage(
	const ProtocolChunkInterestMessage &other)
	: chunk_x(other.chunk_x), chunk_z(other.chunk_z),
	subscribed(other.subscribed)
{
}

ProtocolChunkInterestMessage::~ProtocolChunkInterestMessage()
{
}

ProtocolChunkInterestMessage &ProtocolChunkInterestMessage::operator=(
	const ProtocolChunkInterestMessage &other)
{
	if (this != &other)
	{
		this->chunk_x = other.chunk_x;
		this->chunk_z = other.chunk_z;
		this->subscribed = other.subscribed;
	}
	return (*this);
}

int32_t ProtocolChunkInterestMessage::serialize(
	ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	if (this->subscribed > 1U)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = buffer.append_u32_le(static_cast<uint32_t>(this->chunk_x));
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u32_le(static_cast<uint32_t>(this->chunk_z));
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u8(this->subscribed);
	return (error_code);
}

int32_t ProtocolChunkInterestMessage::deserialize(
	ft_byte_buffer &buffer) noexcept
{
	ProtocolChunkInterestMessage decoded_interest;
	uint32_t raw_x;
	uint32_t raw_z;
	ft_size_t initial_read_position;
	int32_t error_code;
	int32_t restore_error;

	initial_read_position = buffer.read_position();
	error_code = buffer.read_u32_le(&raw_x);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&raw_z);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u8(&decoded_interest.subscribed);
	if (error_code == FT_ERR_SUCCESS)
	{
		decoded_interest.chunk_x = static_cast<int32_t>(raw_x);
		decoded_interest.chunk_z = static_cast<int32_t>(raw_z);
		if (decoded_interest.subscribed > 1U || buffer.remaining() != 0U)
			error_code = FT_ERR_INVALID_ARGUMENT;
	}
	if (error_code != FT_ERR_SUCCESS)
	{
		restore_error = buffer.set_read_position(initial_read_position);
		if (restore_error != FT_ERR_SUCCESS)
			return (restore_error);
		return (error_code);
	}
	*this = decoded_interest;
	return (FT_ERR_SUCCESS);
}
