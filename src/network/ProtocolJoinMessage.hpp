#ifndef PROTOCOL_JOIN_MESSAGE_HPP
# define PROTOCOL_JOIN_MESSAGE_HPP

# include "../ft_vox.hpp"

class ProtocolJoinMessage
{
  public:
	uint32_t client_id;
	std::string player_name;
	std::string auth_token;

	ProtocolJoinMessage();
	ProtocolJoinMessage(const ProtocolJoinMessage &other);
	~ProtocolJoinMessage();
	ProtocolJoinMessage &operator=(const ProtocolJoinMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;

  private:
	static int32_t write_string(ft_byte_buffer &buffer,
		const std::string &value) noexcept;
	static int32_t read_string(ft_byte_buffer &buffer,
		std::string &value) noexcept;
};

#endif
