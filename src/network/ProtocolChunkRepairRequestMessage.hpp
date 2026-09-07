#ifndef PROTOCOL_CHUNK_REPAIR_REQUEST_MESSAGE_HPP
# define PROTOCOL_CHUNK_REPAIR_REQUEST_MESSAGE_HPP

# include "ProtocolChunkHashManifestMessage.hpp"

class ProtocolChunkRepairRequestMessage
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
	uint16_t requested_section_mask;
	uint8_t local_content_hash[PROTOCOL_CHUNK_HASH_SIZE];

	ProtocolChunkRepairRequestMessage();
	ProtocolChunkRepairRequestMessage(
		const ProtocolChunkRepairRequestMessage &other);
	~ProtocolChunkRepairRequestMessage();
	ProtocolChunkRepairRequestMessage &operator=(
		const ProtocolChunkRepairRequestMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
