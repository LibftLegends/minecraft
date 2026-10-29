#include "../../src/network/ProtocolEditResultMessage.hpp"

ProtocolEditResultMessage::ProtocolEditResultMessage()
	: protocol_version(GAME_WORLD_DELTA_PROTOCOL_VERSION), session_id(0U),
	request_id(0U), authoritative_revision(0U), accepted(0U),
	result_code(FT_ERR_INVALID_STATE), delta()
{
}

ProtocolEditResultMessage::ProtocolEditResultMessage(
	const ProtocolEditResultMessage &other)
	: protocol_version(other.protocol_version), session_id(other.session_id),
	request_id(other.request_id),
	authoritative_revision(other.authoritative_revision), accepted(other.accepted),
	result_code(other.result_code), delta(other.delta)
{
}

ProtocolEditResultMessage::~ProtocolEditResultMessage()
{
}

ProtocolEditResultMessage &ProtocolEditResultMessage::operator=(
	const ProtocolEditResultMessage &other)
{
	if (this != &other)
	{
		this->protocol_version = other.protocol_version;
		this->session_id = other.session_id;
		this->request_id = other.request_id;
		this->authoritative_revision = other.authoritative_revision;
		this->accepted = other.accepted;
		this->result_code = other.result_code;
		this->delta = other.delta;
	}
	return (*this);
}

int32_t ProtocolEditResultMessage::serialize(ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	if (this->protocol_version != GAME_WORLD_DELTA_PROTOCOL_VERSION
		|| this->session_id == 0U || this->request_id == 0U
		|| this->accepted > 1U
		|| (this->accepted != 0U && this->result_code != FT_ERR_SUCCESS)
		|| (this->accepted == 0U && this->result_code == FT_ERR_SUCCESS))
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->accepted != 0U
		&& (this->authoritative_revision == 0U
			|| this->authoritative_revision != this->delta.revision
			|| this->delta.session_id != this->session_id
			|| this->delta.request_id != this->request_id))
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = buffer.append_u16_le(this->protocol_version);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->session_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->request_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->authoritative_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u8(this->accepted);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u32_le(
		static_cast<uint32_t>(this->result_code));
	if (error_code == FT_ERR_SUCCESS && this->accepted != 0U)
		error_code = game_block_delta_serialize(this->delta, buffer);
	return (error_code);
}

int32_t ProtocolEditResultMessage::deserialize(ft_byte_buffer &buffer) noexcept
{
	ProtocolEditResultMessage decoded_result;
	uint32_t raw_result_code;
	ft_size_t initial_read_position;
	int32_t error_code;
	int32_t restore_error;

	initial_read_position = buffer.read_position();
	error_code = buffer.read_u16_le(&decoded_result.protocol_version);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_result.session_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_result.request_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(
		&decoded_result.authoritative_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u8(&decoded_result.accepted);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&raw_result_code);
	if (error_code == FT_ERR_SUCCESS)
		decoded_result.result_code = static_cast<int32_t>(raw_result_code);
	if (error_code == FT_ERR_SUCCESS && decoded_result.accepted != 0U)
		error_code = game_block_delta_deserialize(decoded_result.delta, buffer);
	if (error_code != FT_ERR_SUCCESS)
	{
		restore_error = buffer.set_read_position(initial_read_position);
		if (restore_error != FT_ERR_SUCCESS)
			return (restore_error);
		return (error_code);
	}
	if (decoded_result.protocol_version != GAME_WORLD_DELTA_PROTOCOL_VERSION
		|| decoded_result.session_id == 0U
		|| decoded_result.request_id == 0U
		|| decoded_result.accepted > 1U
		|| (decoded_result.accepted != 0U
			&& decoded_result.result_code != FT_ERR_SUCCESS)
		|| (decoded_result.accepted == 0U
			&& decoded_result.result_code == FT_ERR_SUCCESS))
	{
		restore_error = buffer.set_read_position(initial_read_position);
		if (restore_error != FT_ERR_SUCCESS)
			return (restore_error);
		return (FT_ERR_INVALID_ARGUMENT);
	}
	if (decoded_result.accepted != 0U
		&& (decoded_result.authoritative_revision == 0U
			|| decoded_result.authoritative_revision
			!= decoded_result.delta.revision
			|| decoded_result.delta.session_id != decoded_result.session_id
			|| decoded_result.delta.request_id != decoded_result.request_id))
	{
		restore_error = buffer.set_read_position(initial_read_position);
		if (restore_error != FT_ERR_SUCCESS)
			return (restore_error);
		return (FT_ERR_INVALID_ARGUMENT);
	}
	if (buffer.remaining() != 0U)
	{
		restore_error = buffer.set_read_position(initial_read_position);
		if (restore_error != FT_ERR_SUCCESS)
			return (restore_error);
		return (FT_ERR_INVALID_ARGUMENT);
	}
	this->protocol_version = decoded_result.protocol_version;
	this->session_id = decoded_result.session_id;
	this->request_id = decoded_result.request_id;
	this->authoritative_revision = decoded_result.authoritative_revision;
	this->accepted = decoded_result.accepted;
	this->result_code = decoded_result.result_code;
	this->delta = decoded_result.delta;
	return (FT_ERR_SUCCESS);
}
