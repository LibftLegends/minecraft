#ifndef PROTOCOL_EDIT_INTENT_MESSAGE_HPP
# define PROTOCOL_EDIT_INTENT_MESSAGE_HPP

# include "../ft_vox.hpp"

class ProtocolEditIntentMessage
{
  public:
	game_block_change_request request;

	ProtocolEditIntentMessage();
	ProtocolEditIntentMessage(const ProtocolEditIntentMessage &other);
	~ProtocolEditIntentMessage();
	ProtocolEditIntentMessage &operator=(
		const ProtocolEditIntentMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
