#ifndef PROTOCOL_EDIT_BROADCAST_MESSAGE_HPP
# define PROTOCOL_EDIT_BROADCAST_MESSAGE_HPP

# include "../ft_vox.hpp"

class ProtocolEditBroadcastMessage
{
  public:
	uint32_t client_id;
	game_block_edit_op edit;

	ProtocolEditBroadcastMessage();
	ProtocolEditBroadcastMessage(const ProtocolEditBroadcastMessage &other);
	~ProtocolEditBroadcastMessage();
	ProtocolEditBroadcastMessage &operator=(const ProtocolEditBroadcastMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
