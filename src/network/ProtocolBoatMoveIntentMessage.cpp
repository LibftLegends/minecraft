#include "ProtocolBoatMoveIntentMessage.hpp"

#include <cmath>
#include <cstring>

static_assert(sizeof(double) == sizeof(uint64_t),
	"Boat move protocol requires 64-bit doubles");

namespace
{
	static const uint16_t BOAT_MOVE_PROTOCOL_VERSION = 1U;

	static uint64_t boat_move_double_to_bits(double value) noexcept
	{
		uint64_t bits = 0U;
		std::memcpy(&bits, &value, sizeof(bits));
		return (bits);
	}

	static double boat_move_bits_to_double(uint64_t bits) noexcept
	{
		double value = 0.0;
		std::memcpy(&value, &bits, sizeof(value));
		return (value);
	}

	static ft_bool boat_move_values_valid(
		const ProtocolBoatMoveIntentMessage &message) noexcept
	{
		if (std::isfinite(message.next_x) == 0
			|| std::isfinite(message.next_z) == 0
			|| std::isfinite(message.yaw) == 0)
			return (FT_FALSE);
		return (FT_TRUE);
	}
}

ProtocolBoatMoveIntentMessage::ProtocolBoatMoveIntentMessage()
	: protocol_version(BOAT_MOVE_PROTOCOL_VERSION), session_id(0U),
	request_id(0U), entity_id(0U), expected_entity_revision(0U),
	next_x(0.0), next_z(0.0), yaw(0.0)
{
}

ProtocolBoatMoveIntentMessage::ProtocolBoatMoveIntentMessage(
	const ProtocolBoatMoveIntentMessage &other)
	: protocol_version(other.protocol_version), session_id(other.session_id),
	request_id(other.request_id), entity_id(other.entity_id),
	expected_entity_revision(other.expected_entity_revision),
	next_x(other.next_x), next_z(other.next_z), yaw(other.yaw)
{
}

ProtocolBoatMoveIntentMessage::~ProtocolBoatMoveIntentMessage()
{
}

ProtocolBoatMoveIntentMessage &ProtocolBoatMoveIntentMessage::operator=(
	const ProtocolBoatMoveIntentMessage &other)
{
	if (this != &other)
	{
		this->protocol_version = other.protocol_version;
		this->session_id = other.session_id;
		this->request_id = other.request_id;
		this->entity_id = other.entity_id;
		this->expected_entity_revision = other.expected_entity_revision;
		this->next_x = other.next_x;
		this->next_z = other.next_z;
		this->yaw = other.yaw;
	}
	return (*this);
}

int32_t ProtocolBoatMoveIntentMessage::serialize(
	ft_byte_buffer &buffer) const noexcept
{
	int32_t result;

	if (this->protocol_version != BOAT_MOVE_PROTOCOL_VERSION
		|| this->session_id == 0U || this->request_id == 0U
		|| this->entity_id == 0U || this->expected_entity_revision == 0U
		|| boat_move_values_valid(*this) == FT_FALSE)
		return (FT_ERR_INVALID_ARGUMENT);
	result = buffer.append_u16_le(this->protocol_version);
	if (result == FT_ERR_SUCCESS)
		result = buffer.append_u64_le(this->session_id);
	if (result == FT_ERR_SUCCESS)
		result = buffer.append_u64_le(this->request_id);
	if (result == FT_ERR_SUCCESS)
		result = buffer.append_u32_le(this->entity_id);
	if (result == FT_ERR_SUCCESS)
		result = buffer.append_u64_le(this->expected_entity_revision);
	if (result == FT_ERR_SUCCESS)
		result = buffer.append_u64_le(boat_move_double_to_bits(this->next_x));
	if (result == FT_ERR_SUCCESS)
		result = buffer.append_u64_le(boat_move_double_to_bits(this->next_z));
	if (result == FT_ERR_SUCCESS)
		result = buffer.append_u64_le(boat_move_double_to_bits(this->yaw));
	return (result);
}

int32_t ProtocolBoatMoveIntentMessage::deserialize(
	ft_byte_buffer &buffer) noexcept
{
	ProtocolBoatMoveIntentMessage decoded;
	uint64_t next_x_bits = 0U;
	uint64_t next_z_bits = 0U;
	uint64_t yaw_bits = 0U;
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
		result = buffer.read_u64_le(&decoded.expected_entity_revision);
	if (result == FT_ERR_SUCCESS)
		result = buffer.read_u64_le(&next_x_bits);
	if (result == FT_ERR_SUCCESS)
		result = buffer.read_u64_le(&next_z_bits);
	if (result == FT_ERR_SUCCESS)
		result = buffer.read_u64_le(&yaw_bits);
	if (result != FT_ERR_SUCCESS)
	{
		restore_result = buffer.set_read_position(initial_position);
		if (restore_result != FT_ERR_SUCCESS)
			return (restore_result);
		return (result);
	}
	decoded.next_x = boat_move_bits_to_double(next_x_bits);
	decoded.next_z = boat_move_bits_to_double(next_z_bits);
	decoded.yaw = boat_move_bits_to_double(yaw_bits);
	if (buffer.remaining() != 0U
		|| decoded.protocol_version != BOAT_MOVE_PROTOCOL_VERSION
		|| decoded.session_id == 0U || decoded.request_id == 0U
		|| decoded.entity_id == 0U
		|| decoded.expected_entity_revision == 0U
		|| boat_move_values_valid(decoded) == FT_FALSE)
	{
		restore_result = buffer.set_read_position(initial_position);
		if (restore_result != FT_ERR_SUCCESS)
			return (restore_result);
		return (FT_ERR_INVALID_ARGUMENT);
	}
	*this = decoded;
	return (FT_ERR_SUCCESS);
}
