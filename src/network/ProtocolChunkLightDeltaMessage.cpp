#include "../../src/network/ProtocolChunkLightDeltaMessage.hpp"

int32_t protocol_chunk_light_payload_append(
	const std::vector<ProtocolChunkLightCell> &cells,
	ft_byte_buffer &output) noexcept
{
	ft_byte_buffer encoded_payload;
	uint32_t previous_cell_index;
	uint32_t cell_count;
	uint32_t cell_index;
	uint32_t index;
	int32_t error_code;

	if (cells.size() > PROTOCOL_CHUNK_LIGHT_CELL_COUNT)
		return (FT_ERR_OUT_OF_RANGE);
	cell_count = static_cast<uint32_t>(cells.size());
	index = 0U;
	previous_cell_index = 0U;
	while (index < cell_count)
	{
		cell_index = cells[index].cell_index;
		if (cell_index >= PROTOCOL_CHUNK_LIGHT_CELL_COUNT
			|| (index > 0U && cell_index <= previous_cell_index))
			return (FT_ERR_INVALID_ARGUMENT);
		previous_cell_index = cell_index;
		index += 1U;
	}
	error_code = encoded_payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = encoded_payload.append_u32_le(cell_count);
	index = 0U;
	while (index < cell_count && error_code == FT_ERR_SUCCESS)
	{
		error_code = encoded_payload.append_u32_le(cells[index].cell_index);
		if (error_code == FT_ERR_SUCCESS)
			error_code = encoded_payload.append_u8(cells[index].packed_light);
		index += 1U;
	}
	if (error_code == FT_ERR_SUCCESS
		&& encoded_payload.size() > PROTOCOL_CHUNK_LIGHT_DELTA_MAX_PAYLOAD)
		error_code = FT_ERR_OUT_OF_RANGE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = output.append_buffer(encoded_payload);
	{
		int32_t destroy_error = encoded_payload.destroy();
		if (error_code == FT_ERR_SUCCESS)
			error_code = destroy_error;
	}
	return (error_code);
}

int32_t protocol_chunk_light_payload_validate(const uint8_t *payload_data,
	ft_size_t payload_size) noexcept
{
	ft_byte_buffer payload;
	uint32_t cell_count;
	uint32_t cell_index;
	uint32_t previous_cell_index;
	uint32_t index;
	uint8_t packed_light;
	int32_t error_code;

	if (payload_size > PROTOCOL_CHUNK_LIGHT_DELTA_MAX_PAYLOAD
		|| (payload_size > 0U && payload_data == ft_nullptr))
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = payload.append(payload_data, payload_size);
	cell_count = 0U;
	previous_cell_index = 0U;
	index = 0U;
	if (error_code == FT_ERR_SUCCESS)
		error_code = payload.read_u32_le(&cell_count);
	if (error_code == FT_ERR_SUCCESS
		&& cell_count > PROTOCOL_CHUNK_LIGHT_CELL_COUNT)
		error_code = FT_ERR_OUT_OF_RANGE;
	while (index < cell_count && error_code == FT_ERR_SUCCESS)
	{
		error_code = payload.read_u32_le(&cell_index);
		if (error_code == FT_ERR_SUCCESS)
			error_code = payload.read_u8(&packed_light);
		if (error_code == FT_ERR_SUCCESS
			&& (cell_index >= PROTOCOL_CHUNK_LIGHT_CELL_COUNT
				|| (index > 0U && cell_index <= previous_cell_index)))
			error_code = FT_ERR_INVALID_ARGUMENT;
		previous_cell_index = cell_index;
		index += 1U;
	}
	if (error_code == FT_ERR_SUCCESS && payload.remaining() != 0U)
		error_code = FT_ERR_INVALID_ARGUMENT;
	{
		int32_t destroy_error = payload.destroy();
		if (error_code == FT_ERR_SUCCESS)
			error_code = destroy_error;
	}
	return (error_code);
}

ProtocolChunkLightDeltaMessage::ProtocolChunkLightDeltaMessage()
	: protocol_version(GAME_WORLD_DELTA_PROTOCOL_VERSION), session_id(0U),
	world_id(0U), chunk_x(0), chunk_z(0), base_light_revision(0U),
	final_light_revision(0U), source_block_revision(0U), generation_epoch(0U),
	light_payload()
{
}

ProtocolChunkLightDeltaMessage::ProtocolChunkLightDeltaMessage(
	const ProtocolChunkLightDeltaMessage &other)
	: protocol_version(other.protocol_version), session_id(other.session_id),
	world_id(other.world_id), chunk_x(other.chunk_x), chunk_z(other.chunk_z),
	base_light_revision(other.base_light_revision),
	final_light_revision(other.final_light_revision),
	source_block_revision(other.source_block_revision),
	generation_epoch(other.generation_epoch), light_payload(other.light_payload)
{
}

ProtocolChunkLightDeltaMessage::~ProtocolChunkLightDeltaMessage()
{
}

ProtocolChunkLightDeltaMessage &ProtocolChunkLightDeltaMessage::operator=(
	const ProtocolChunkLightDeltaMessage &other)
{
	if (this != &other)
	{
		this->protocol_version = other.protocol_version;
		this->session_id = other.session_id;
		this->world_id = other.world_id;
		this->chunk_x = other.chunk_x;
		this->chunk_z = other.chunk_z;
		this->base_light_revision = other.base_light_revision;
		this->final_light_revision = other.final_light_revision;
		this->source_block_revision = other.source_block_revision;
		this->generation_epoch = other.generation_epoch;
		this->light_payload = other.light_payload;
	}
	return (*this);
}

int32_t ProtocolChunkLightDeltaMessage::serialize(
	ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	if (this->protocol_version != GAME_WORLD_DELTA_PROTOCOL_VERSION
		|| this->session_id == 0U || this->world_id == 0U
		|| this->base_light_revision == 0U
		|| this->final_light_revision <= this->base_light_revision
		|| this->source_block_revision == 0U
		|| this->light_payload.size() > PROTOCOL_CHUNK_LIGHT_DELTA_MAX_PAYLOAD)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = protocol_chunk_light_payload_validate(
		this->light_payload.empty() ? ft_nullptr : &this->light_payload[0],
		this->light_payload.size());
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
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
		error_code = buffer.append_u64_le(this->base_light_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->final_light_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->source_block_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u64_le(this->generation_epoch);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.append_u32_le(static_cast<uint32_t>(
		this->light_payload.size()));
	if (error_code == FT_ERR_SUCCESS && this->light_payload.empty() == false)
		error_code = buffer.append(&this->light_payload[0],
		this->light_payload.size());
	return (error_code);
}

int32_t ProtocolChunkLightDeltaMessage::deserialize(
	ft_byte_buffer &buffer) noexcept
{
	ProtocolChunkLightDeltaMessage decoded_delta;
	uint32_t raw_chunk_x;
	uint32_t raw_chunk_z;
	uint32_t payload_size;
	const uint8_t *payload_data;
	ft_size_t initial_read_position;
	int32_t error_code;
	int32_t restore_error;

	initial_read_position = buffer.read_position();
	error_code = buffer.read_u16_le(&decoded_delta.protocol_version);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_delta.session_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_delta.world_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&raw_chunk_x);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&raw_chunk_z);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_delta.base_light_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_delta.final_light_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_delta.source_block_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&decoded_delta.generation_epoch);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&payload_size);
	if (error_code == FT_ERR_SUCCESS
		&& payload_size > PROTOCOL_CHUNK_LIGHT_DELTA_MAX_PAYLOAD)
		error_code = FT_ERR_OUT_OF_RANGE;
	if (error_code == FT_ERR_SUCCESS
		&& static_cast<ft_size_t>(payload_size) > buffer.remaining())
		error_code = FT_ERR_OUT_OF_RANGE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.view(buffer.read_position(), payload_size,
		&payload_data);
	if (error_code == FT_ERR_SUCCESS)
		error_code = protocol_chunk_light_payload_validate(payload_data,
		payload_size);
	if (error_code == FT_ERR_SUCCESS && payload_size > 0U)
		decoded_delta.light_payload.assign(payload_data,
			payload_data + payload_size);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.skip(payload_size);
	if (error_code == FT_ERR_SUCCESS)
	{
		decoded_delta.chunk_x = static_cast<int32_t>(raw_chunk_x);
		decoded_delta.chunk_z = static_cast<int32_t>(raw_chunk_z);
	}
	if (error_code == FT_ERR_SUCCESS
		&& (decoded_delta.protocol_version
			!= GAME_WORLD_DELTA_PROTOCOL_VERSION
			|| decoded_delta.session_id == 0U
			|| decoded_delta.world_id == 0U
			|| decoded_delta.base_light_revision == 0U
			|| decoded_delta.final_light_revision
			<= decoded_delta.base_light_revision
			|| decoded_delta.source_block_revision == 0U
			|| buffer.remaining() != 0U))
		error_code = FT_ERR_INVALID_ARGUMENT;
	if (error_code != FT_ERR_SUCCESS)
	{
		restore_error = buffer.set_read_position(initial_read_position);
		if (restore_error != FT_ERR_SUCCESS)
			return (restore_error);
		return (error_code);
	}
	*this = decoded_delta;
	return (FT_ERR_SUCCESS);
}
