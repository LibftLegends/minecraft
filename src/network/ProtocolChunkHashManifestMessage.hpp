#ifndef PROTOCOL_CHUNK_HASH_MANIFEST_MESSAGE_HPP
# define PROTOCOL_CHUNK_HASH_MANIFEST_MESSAGE_HPP

# include "../ft_vox.hpp"

# define PROTOCOL_CHUNK_HASH_SIZE 32U

class ProtocolChunkHashManifestMessage
{
  public:
	uint16_t protocol_version;
	uint64_t session_id;
	uint64_t world_id;
	int32_t chunk_x;
	int32_t chunk_z;
	uint64_t block_revision;
	uint64_t light_revision;
	uint64_t generation_epoch;
	uint8_t content_hash[PROTOCOL_CHUNK_HASH_SIZE];

	ProtocolChunkHashManifestMessage();
	ProtocolChunkHashManifestMessage(
		const ProtocolChunkHashManifestMessage &other);
	~ProtocolChunkHashManifestMessage();
	ProtocolChunkHashManifestMessage &operator=(
		const ProtocolChunkHashManifestMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
