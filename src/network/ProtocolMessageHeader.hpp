#ifndef PROTOCOL_MESSAGE_HEADER_HPP
# define PROTOCOL_MESSAGE_HEADER_HPP

# include "../ft_vox.hpp"

class ProtocolMessageHeader
{
  public:
	enum class Type
	{
		JOIN = 0U,
		LEAVE = 1U,
		CHUNK_REQUEST = 2U,
		CHUNK_RESPONSE = 3U,
		EDIT_BROADCAST = 4U,
		ENTITY_UPDATE = 5U,
		CHAT = 6U,
		PING = 7U,
		PONG = 8U
	};

	uint32_t message_type;
	uint32_t payload_size;

	ProtocolMessageHeader();
	ProtocolMessageHeader(const ProtocolMessageHeader &other);
	~ProtocolMessageHeader();
	ProtocolMessageHeader &operator=(const ProtocolMessageHeader &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
