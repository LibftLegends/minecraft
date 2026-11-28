#ifndef PROTOCOL_CHUNK_RESPONSE_MESSAGE_HPP
# define PROTOCOL_CHUNK_RESPONSE_MESSAGE_HPP

# include "../ft_vox.hpp"

# define PROTOCOL_CHUNK_RESPONSE_MAX_PAYLOAD (4U * 1024U * 1024U)

class ProtocolChunkResponseMessage
{
  public:
	int32_t chunk_x;
	int32_t chunk_z;
	std::vector<uint8_t> chunk_payload;

	ProtocolChunkResponseMessage();
	ProtocolChunkResponseMessage(const ProtocolChunkResponseMessage &other);
	~ProtocolChunkResponseMessage();
	ProtocolChunkResponseMessage &operator=(const ProtocolChunkResponseMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
