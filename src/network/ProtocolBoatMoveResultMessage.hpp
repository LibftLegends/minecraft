#ifndef PROTOCOL_BOAT_MOVE_RESULT_MESSAGE_HPP
# define PROTOCOL_BOAT_MOVE_RESULT_MESSAGE_HPP

# include "../ft_vox.hpp"
# include "../entities/EntityState.hpp"

class ProtocolBoatMoveResultMessage
{
  public:
	uint16_t protocol_version;
	uint64_t session_id;
	uint64_t request_id;
	uint32_t entity_id;
	int32_t result_code;
	uint64_t entity_revision;
	EntityState entity_state;

	ProtocolBoatMoveResultMessage();
	ProtocolBoatMoveResultMessage(
		const ProtocolBoatMoveResultMessage &other);
	~ProtocolBoatMoveResultMessage();
	ProtocolBoatMoveResultMessage &operator=(
		const ProtocolBoatMoveResultMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
