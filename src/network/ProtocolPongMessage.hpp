#ifndef PROTOCOL_PONG_MESSAGE_HPP
# define PROTOCOL_PONG_MESSAGE_HPP

# include "../ft_vox.hpp"

class ProtocolPongMessage
{
  public:
	uint64_t client_send_tick;
	uint64_t server_tick;

	ProtocolPongMessage();
	ProtocolPongMessage(const ProtocolPongMessage &other);
	~ProtocolPongMessage();
	ProtocolPongMessage &operator=(const ProtocolPongMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
