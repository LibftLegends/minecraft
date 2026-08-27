#ifndef PROTOCOL_PING_MESSAGE_HPP
# define PROTOCOL_PING_MESSAGE_HPP

# include "../ft_vox.hpp"

class ProtocolPingMessage
{
  public:
	uint64_t client_send_tick;

	ProtocolPingMessage();
	ProtocolPingMessage(const ProtocolPingMessage &other);
	~ProtocolPingMessage();
	ProtocolPingMessage &operator=(const ProtocolPingMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
