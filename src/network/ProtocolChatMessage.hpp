#ifndef PROTOCOL_CHAT_MESSAGE_HPP
# define PROTOCOL_CHAT_MESSAGE_HPP

# include "../ft_vox.hpp"

class ProtocolChatMessage
{
  public:
	uint32_t client_id;
	std::string text;

	ProtocolChatMessage();
	ProtocolChatMessage(const ProtocolChatMessage &other);
	~ProtocolChatMessage();
	ProtocolChatMessage &operator=(const ProtocolChatMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;

  private:
	static int32_t write_string(ft_byte_buffer &buffer,
		const std::string &value) noexcept;
	static int32_t read_string(ft_byte_buffer &buffer,
		std::string &value) noexcept;
};

#endif
