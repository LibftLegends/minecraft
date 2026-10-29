#include "ProtocolBoatMoveResultMessage.hpp"

namespace
{
	static const uint16_t BOAT_MOVE_PROTOCOL_VERSION = 1U;
}

ProtocolBoatMoveResultMessage::ProtocolBoatMoveResultMessage()
	: protocol_version(BOAT_MOVE_PROTOCOL_VERSION), session_id(0U),
	request_id(0U), entity_id(0U), result_code(FT_ERR_INVALID_STATE),
	entity_revision(0U), entity_state()
{
}

ProtocolBoatMoveResultMessage::ProtocolBoatMoveResultMessage(
	const ProtocolBoatMoveResultMessage &other)
	: protocol_version(other.protocol_version), session_id(other.session_id),
	request_id(other.request_id), entity_id(other.entity_id),
	result_code(other.result_code), entity_revision(other.entity_revision),
	entity_state(other.entity_state)
{
}

ProtocolBoatMoveResultMessage::~ProtocolBoatMoveResultMessage()
{
}

ProtocolBoatMoveResultMessage &ProtocolBoatMoveResultMessage::operator=(
	const ProtocolBoatMoveResultMessage &other)
{
	if (this != &other)
	{
		this->protocol_version = other.protocol_version;
		this->session_id = other.session_id;
		this->request_id = other.request_id;
		this->entity_id = other.entity_id;
		this->result_code = other.result_code;
		this->entity_revision = other.entity_revision;
		this->entity_state = other.entity_state;
	}
	return (*this);
}

int32_t ProtocolBoatMoveResultMessage::serialize(
	ft_byte_buffer &buffer) const noexcept
{
	int32_t result;

	if (this->protocol_version != BOAT_MOVE_PROTOCOL_VERSION
		|| this->session_id == 0U || this->request_id == 0U
		|| this->entity_id == 0U
		|| (this->result_code == FT_ERR_SUCCESS
			&& (this->entity_revision == 0U
				|| this->entity_state.id != this->entity_id
				|| this->entity_state.type != static_cast<uint32_t>(
					EntityState::Kind::BOAT)))
		|| (this->result_code != FT_ERR_SUCCESS
			&& this->entity_revision != 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	result = buffer.append_u16_le(this->protocol_version);
	if (result == FT_ERR_SUCCESS)
		result = buffer.append_u64_le(this->session_id);
	if (result == FT_ERR_SUCCESS)
		result = buffer.append_u64_le(this->request_id);
	if (result == FT_ERR_SUCCESS)
		result = buffer.append_u32_le(this->entity_id);
	if (result == FT_ERR_SUCCESS)
		result = buffer.append_u32_le(
			static_cast<uint32_t>(this->result_code));
	if (result == FT_ERR_SUCCESS)
		result = buffer.append_u64_le(this->entity_revision);
	if (result == FT_ERR_SUCCESS && this->result_code == FT_ERR_SUCCESS)
		result = this->entity_state.serialize(buffer);
	return (result);
}

int32_t ProtocolBoatMoveResultMessage::deserialize(
	ft_byte_buffer &buffer) noexcept
{
	ProtocolBoatMoveResultMessage decoded;
	uint32_t raw_result_code = 0U;
	ft_size_t initial_position;
	int32_t result;
	int32_t restore_result;

	initial_position = buffer.read_position();
	result = buffer.read_u16_le(&decoded.protocol_version);
	if (result == FT_ERR_SUCCESS)
		result = buffer.read_u64_le(&decoded.session_id);
	if (result == FT_ERR_SUCCESS)
		result = buffer.read_u64_le(&decoded.request_id);
	if (result == FT_ERR_SUCCESS)
		result = buffer.read_u32_le(&decoded.entity_id);
	if (result == FT_ERR_SUCCESS)
		result = buffer.read_u32_le(&raw_result_code);
	if (result == FT_ERR_SUCCESS)
		result = buffer.read_u64_le(&decoded.entity_revision);
	if (result == FT_ERR_SUCCESS && raw_result_code == FT_ERR_SUCCESS)
		result = decoded.entity_state.deserialize(buffer);
	if (result != FT_ERR_SUCCESS)
	{
		restore_result = buffer.set_read_position(initial_position);
		if (restore_result != FT_ERR_SUCCESS)
			return (restore_result);
		return (result);
	}
	decoded.result_code = static_cast<int32_t>(raw_result_code);
	if (buffer.remaining() != 0U
		|| decoded.protocol_version != BOAT_MOVE_PROTOCOL_VERSION
		|| decoded.session_id == 0U || decoded.request_id == 0U
		|| decoded.entity_id == 0U
		|| (decoded.result_code == FT_ERR_SUCCESS
			&& (decoded.entity_revision == 0U
				|| decoded.entity_state.id != decoded.entity_id
				|| decoded.entity_state.type != static_cast<uint32_t>(
					EntityState::Kind::BOAT)))
		|| (decoded.result_code != FT_ERR_SUCCESS
			&& decoded.entity_revision != 0U))
	{
		restore_result = buffer.set_read_position(initial_position);
		if (restore_result != FT_ERR_SUCCESS)
			return (restore_result);
		return (FT_ERR_INVALID_ARGUMENT);
	}
	*this = decoded;
	return (FT_ERR_SUCCESS);
}
