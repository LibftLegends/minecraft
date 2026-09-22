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
		PONG = 8U,
		EDIT_INTENT = 9U,
		EDIT_RESULT = 10U,
		CHUNK_BLOCK_DELTA = 11U,
		CHUNK_LIGHT_DELTA = 12U,
		CHUNK_SNAPSHOT = 13U,
		CHUNK_ACKNOWLEDGEMENT = 14U,
		CHUNK_INTEREST = 15U,
		CHUNK_HASH_MANIFEST = 16U,
		CHUNK_REPAIR_REQUEST = 17U,
		CHUNK_REPAIR_RESPONSE = 18U,
		CHUNK_SYNC_REQUEST = 19U
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
