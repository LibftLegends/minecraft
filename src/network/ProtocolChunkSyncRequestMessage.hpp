#ifndef PROTOCOL_CHUNK_SYNC_REQUEST_MESSAGE_HPP
# define PROTOCOL_CHUNK_SYNC_REQUEST_MESSAGE_HPP

# include "../ft_vox.hpp"

class ProtocolChunkSyncRequestMessage
{
  public:
	uint16_t protocol_version;
	uint64_t session_id;
	uint64_t world_id;
	int32_t chunk_x;
	int32_t chunk_z;
	uint64_t block_revision;
	uint64_t light_revision;

	ProtocolChunkSyncRequestMessage();
	ProtocolChunkSyncRequestMessage(
		const ProtocolChunkSyncRequestMessage &other);
	~ProtocolChunkSyncRequestMessage();
	ProtocolChunkSyncRequestMessage &operator=(
		const ProtocolChunkSyncRequestMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
