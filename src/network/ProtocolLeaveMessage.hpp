#ifndef PROTOCOL_LEAVE_MESSAGE_HPP
# define PROTOCOL_LEAVE_MESSAGE_HPP

# include "../ft_vox.hpp"

class ProtocolLeaveMessage
{
  public:
	uint32_t client_id;

	ProtocolLeaveMessage();
	ProtocolLeaveMessage(const ProtocolLeaveMessage &other);
	~ProtocolLeaveMessage();
	ProtocolLeaveMessage &operator=(const ProtocolLeaveMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
