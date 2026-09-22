#include "../../src/network/ProtocolChunkRepairResponseMessage.hpp"

static uint32_t repair_canonical_payload_size(uint16_t section_mask)
{
	uint32_t section_count;
	uint32_t section_index;

	section_count = 0U;
	section_index = 0U;
	while (section_index < GAME_VOXEL_CHUNK_SECTION_COUNT)
	{
		if ((section_mask & static_cast<uint16_t>(1U << section_index)) != 0U)
			section_count += 1U;
		section_index += 1U;
	}
	return (section_count * GAME_VOXEL_SECTION_BLOCKS * sizeof(uint32_t));
}

ProtocolChunkRepairResponseMessage::ProtocolChunkRepairResponseMessage()
	: protocol_version(GAME_WORLD_DELTA_PROTOCOL_VERSION), session_id(0U),
	world_id(0U), chunk_x(0), chunk_z(0), block_revision(0U),
	light_revision(0U), generation_epoch(0U), repaired_section_mask(0U),
	payload_format(PROTOCOL_CHUNK_REPAIR_PAYLOAD_CANONICAL_BLOCKS),
	content_hash(), repair_payload()
{
}

ProtocolChunkRepairResponseMessage::ProtocolChunkRepairResponseMessage(
	const ProtocolChunkRepairResponseMessage &other)
	: protocol_version(other.protocol_version), session_id(other.session_id),
	world_id(other.world_id), chunk_x(other.chunk_x), chunk_z(other.chunk_z),
	block_revision(other.block_revision), light_revision(other.light_revision),
	generation_epoch(other.generation_epoch),
	repaired_section_mask(other.repaired_section_mask),
	payload_format(other.payload_format), content_hash(),
	repair_payload(other.repair_payload)
{
	ft_memcpy(this->content_hash, other.content_hash,
		PROTOCOL_CHUNK_HASH_SIZE);
}

ProtocolChunkRepairResponseMessage::~ProtocolChunkRepairResponseMessage()
{
}

ProtocolChunkRepairResponseMessage &ProtocolChunkRepairResponseMessage::operator=(
	const ProtocolChunkRepairResponseMessage &other)
{
	if (this != &other)
	{
		this->protocol_version = other.protocol_version;
		this->session_id = other.session_id;
		this->world_id = other.world_id;
		this->chunk_x = other.chunk_x;
		this->chunk_z = other.chunk_z;
		this->block_revision = other.block_revision;
		this->light_revision = other.light_revision;
		this->generation_epoch = other.generation_epoch;
		this->repaired_section_mask = other.repaired_section_mask;
		this->payload_format = other.payload_format;
		ft_memcpy(this->content_hash, other.content_hash,
			PROTOCOL_CHUNK_HASH_SIZE);
		this->repair_payload = other.repair_payload;
	}
	return (*this);
}

int32_t ProtocolChunkRepairResponseMessage::serialize(
	ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	if (this->protocol_version != GAME_WORLD_DELTA_PROTOCOL_VERSION
		|| this->session_id == 0U || this->world_id == 0U
		|| this->block_revision == 0U || this->light_revision == 0U
		|| this->generation_epoch == 0U || this->repaired_section_mask == 0U
		|| this->payload_format > PROTOCOL_CHUNK_REPAIR_PAYLOAD_APPLICATION
		|| this->repair_payload.size() > PROTOCOL_CHUNK_REPAIR_MAX_PAYLOAD)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->payload_format == PROTOCOL_CHUNK_REPAIR_PAYLOAD_CANONICAL_BLOCKS
		&& this->repair_payload.size()
			!= repair_canonical_payload_size(this->repaired_section_mask))
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = buffer.append_u16_le(this->protocol_version);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->session_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->world_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u32_le(static_cast<uint32_t>(this->chunk_x));
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u32_le(static_cast<uint32_t>(this->chunk_z));
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->block_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->light_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->generation_epoch);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u16_le(this->repaired_section_mask);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u8(this->payload_format);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append(this->content_hash,
		PROTOCOL_CHUNK_HASH_SIZE);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u32_le(static_cast<uint32_t>(
		this->repair_payload.size()));
	if (error_code == FT_ERR_SUCCESS && !this->repair_payload.empty())
		error_code = buffer.append(&this->repair_payload[0],
		this->repair_payload.size());
	return (error_code);
}

int32_t ProtocolChunkRepairResponseMessage::deserialize(
	ft_byte_buffer &buffer) noexcept
{
	ProtocolChunkRepairResponseMessage decoded_response;
	uint32_t raw_x;
	uint32_t raw_z;
	uint32_t payload_size;
	const uint8_t *payload_data;
	ft_size_t initial_read_position;
	int32_t error_code;
	int32_t restore_error;

	initial_read_position = buffer.read_position();
	error_code = buffer.read_u16_le(&decoded_response.protocol_version);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_response.session_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_response.world_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&raw_x);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&raw_z);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_response.block_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_response.light_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_response.generation_epoch);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u16_le(&decoded_response.repaired_section_mask);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u8(&decoded_response.payload_format);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read(decoded_response.content_hash,
		PROTOCOL_CHUNK_HASH_SIZE);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&payload_size);
	if (error_code == FT_ERR_SUCCESS
		&& payload_size > PROTOCOL_CHUNK_REPAIR_MAX_PAYLOAD)
		error_code = FT_ERR_OUT_OF_RANGE;
	if (error_code == FT_ERR_SUCCESS
		&& static_cast<ft_size_t>(payload_size) > buffer.remaining())
		error_code = FT_ERR_OUT_OF_RANGE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.view(buffer.read_position(), payload_size,
		&payload_data);
	if (error_code == FT_ERR_SUCCESS && payload_size > 0U)
		decoded_response.repair_payload.assign(payload_data,
			payload_data + payload_size);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.skip(payload_size);
	if (error_code == FT_ERR_SUCCESS)
	{
		decoded_response.chunk_x = static_cast<int32_t>(raw_x);
		decoded_response.chunk_z = static_cast<int32_t>(raw_z);
		if (decoded_response.protocol_version
			!= GAME_WORLD_DELTA_PROTOCOL_VERSION
			|| decoded_response.session_id == 0U
			|| decoded_response.world_id == 0U
			|| decoded_response.block_revision == 0U
			|| decoded_response.light_revision == 0U
			|| decoded_response.generation_epoch == 0U
			|| decoded_response.repaired_section_mask == 0U
			|| decoded_response.payload_format
				> PROTOCOL_CHUNK_REPAIR_PAYLOAD_APPLICATION
			|| (decoded_response.payload_format
				== PROTOCOL_CHUNK_REPAIR_PAYLOAD_CANONICAL_BLOCKS
				&& decoded_response.repair_payload.size()
					!= repair_canonical_payload_size(
						decoded_response.repaired_section_mask))
			|| buffer.remaining() != 0U)
			error_code = FT_ERR_INVALID_ARGUMENT;
	}
	if (error_code != FT_ERR_SUCCESS)
	{
		restore_error = buffer.set_read_position(initial_read_position);
		if (restore_error != FT_ERR_SUCCESS)
			return (restore_error);
		return (error_code);
	}
	*this = decoded_response;
	return (FT_ERR_SUCCESS);
}
