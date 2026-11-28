#ifndef PROTOCOL_BOAT_MOVE_INTENT_MESSAGE_HPP
# define PROTOCOL_BOAT_MOVE_INTENT_MESSAGE_HPP

# include "../ft_vox.hpp"

class ProtocolBoatMoveIntentMessage
{
  public:
	uint16_t protocol_version;
	uint64_t session_id;
	uint64_t request_id;
	uint32_t entity_id;
	uint64_t expected_entity_revision;
	double next_x;
	double next_z;
	double yaw;

	ProtocolBoatMoveIntentMessage();
	ProtocolBoatMoveIntentMessage(
		const ProtocolBoatMoveIntentMessage &other);
	~ProtocolBoatMoveIntentMessage();
	ProtocolBoatMoveIntentMessage &operator=(
		const ProtocolBoatMoveIntentMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
