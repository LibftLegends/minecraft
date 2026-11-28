#ifndef PROTOCOL_CHUNK_INTEREST_MESSAGE_HPP
# define PROTOCOL_CHUNK_INTEREST_MESSAGE_HPP

# include "../ft_vox.hpp"

class ProtocolChunkInterestMessage
{
  public:
	int32_t chunk_x;
	int32_t chunk_z;
	uint8_t subscribed;

	ProtocolChunkInterestMessage();
	ProtocolChunkInterestMessage(const ProtocolChunkInterestMessage &other);
	~ProtocolChunkInterestMessage();
	ProtocolChunkInterestMessage &operator=(
		const ProtocolChunkInterestMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
