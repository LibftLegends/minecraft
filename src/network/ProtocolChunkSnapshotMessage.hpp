#ifndef PROTOCOL_CHUNK_SNAPSHOT_MESSAGE_HPP
# define PROTOCOL_CHUNK_SNAPSHOT_MESSAGE_HPP

# include "../ft_vox.hpp"
# include "ProtocolChunkLightDeltaMessage.hpp"

# define PROTOCOL_CHUNK_SNAPSHOT_MAX_PAYLOAD (4U * 1024U * 1024U)

class ProtocolChunkSnapshotMessage
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
	std::vector<uint8_t> snapshot_payload;
	std::vector<uint8_t> light_payload;

	ProtocolChunkSnapshotMessage();
	ProtocolChunkSnapshotMessage(const ProtocolChunkSnapshotMessage &other);
	~ProtocolChunkSnapshotMessage();
	ProtocolChunkSnapshotMessage &operator=(
		const ProtocolChunkSnapshotMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
