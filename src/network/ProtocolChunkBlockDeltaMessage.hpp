#ifndef PROTOCOL_CHUNK_BLOCK_DELTA_MESSAGE_HPP
# define PROTOCOL_CHUNK_BLOCK_DELTA_MESSAGE_HPP

# include "../ft_vox.hpp"

class ProtocolChunkBlockDeltaMessage
{
  public:
	game_block_delta delta;

	ProtocolChunkBlockDeltaMessage();
	ProtocolChunkBlockDeltaMessage(
		const ProtocolChunkBlockDeltaMessage &other);
	~ProtocolChunkBlockDeltaMessage();
	ProtocolChunkBlockDeltaMessage &operator=(
		const ProtocolChunkBlockDeltaMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
