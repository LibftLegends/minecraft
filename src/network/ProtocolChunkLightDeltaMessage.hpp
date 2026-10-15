#ifndef PROTOCOL_CHUNK_LIGHT_DELTA_MESSAGE_HPP
# define PROTOCOL_CHUNK_LIGHT_DELTA_MESSAGE_HPP

# include "../ft_vox.hpp"

# define PROTOCOL_CHUNK_LIGHT_DELTA_MAX_PAYLOAD (4U * 1024U * 1024U)
# define PROTOCOL_CHUNK_LIGHT_CELL_COUNT \
	(GAME_VOXEL_CHUNK_WIDTH * GAME_VOXEL_CHUNK_HEIGHT \
	* GAME_VOXEL_CHUNK_DEPTH)

struct ProtocolChunkLightCell
{
	uint32_t cell_index;
	uint8_t packed_light;
};

/* light_payload is a little-endian sparse-cell stream:
 * uint32 cell_count, followed by cell_count records of uint32 linear cell
 * index and uint8 packed light. Indices must be strictly ascending and lie
 * in the 16*256*16 chunk volume. A zero-count payload is encoded as four zero
 * bytes and represents a revision with no changed cells. */

/* Appends one canonical sparse-cell payload to output. The input must already
 * be sorted by strictly ascending cell_index. output is unchanged when input
 * validation or encoding fails. */
int32_t protocol_chunk_light_payload_append(
	const std::vector<ProtocolChunkLightCell> &cells,
	ft_byte_buffer &output) noexcept;

int32_t protocol_chunk_light_payload_validate(const uint8_t *payload_data,
	ft_size_t payload_size) noexcept;

class ProtocolChunkLightDeltaMessage
{
  public:
	uint16_t protocol_version;
	uint64_t session_id;
	uint64_t world_id;
	int32_t chunk_x;
	int32_t chunk_z;
	uint64_t base_light_revision;
	uint64_t final_light_revision;
	uint64_t source_block_revision;
	uint64_t generation_epoch;
	std::vector<uint8_t> light_payload;

	ProtocolChunkLightDeltaMessage();
	ProtocolChunkLightDeltaMessage(const ProtocolChunkLightDeltaMessage &other);
	~ProtocolChunkLightDeltaMessage();
	ProtocolChunkLightDeltaMessage &operator=(
		const ProtocolChunkLightDeltaMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
