#ifndef PROTOCOL_CHUNK_REPAIR_RESPONSE_MESSAGE_HPP
# define PROTOCOL_CHUNK_REPAIR_RESPONSE_MESSAGE_HPP

# include "ProtocolChunkHashManifestMessage.hpp"
# include <vector>

# define PROTOCOL_CHUNK_REPAIR_MAX_PAYLOAD (4U * 1024U * 1024U)
# define PROTOCOL_CHUNK_REPAIR_PAYLOAD_CANONICAL_BLOCKS 0U
# define PROTOCOL_CHUNK_REPAIR_PAYLOAD_APPLICATION 1U

class ProtocolChunkRepairResponseMessage
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
	uint16_t repaired_section_mask;
	uint8_t payload_format;
	uint8_t content_hash[PROTOCOL_CHUNK_HASH_SIZE];
	std::vector<uint8_t> repair_payload;

	ProtocolChunkRepairResponseMessage();
	ProtocolChunkRepairResponseMessage(
		const ProtocolChunkRepairResponseMessage &other);
	~ProtocolChunkRepairResponseMessage();
	ProtocolChunkRepairResponseMessage &operator=(
		const ProtocolChunkRepairResponseMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
