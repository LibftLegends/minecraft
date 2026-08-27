#ifndef PROTOCOL_CHUNK_REQUEST_MESSAGE_HPP
# define PROTOCOL_CHUNK_REQUEST_MESSAGE_HPP

# include "../ft_vox.hpp"

class ProtocolChunkRequestMessage
{
  public:
	int32_t chunk_x;
	int32_t chunk_z;

	ProtocolChunkRequestMessage();
	ProtocolChunkRequestMessage(const ProtocolChunkRequestMessage &other);
	~ProtocolChunkRequestMessage();
	ProtocolChunkRequestMessage &operator=(const ProtocolChunkRequestMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
