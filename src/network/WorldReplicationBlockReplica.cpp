#include "../../src/network/WorldReplicationBlockReplica.hpp"
#include "../../Libft/Modules/Networking/networking_replication_protocol.hpp"

static uint32_t replica_canonical_repair_size(uint16_t section_mask)
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

WorldReplicationBlockReplica::WorldReplicationBlockReplica()
	: chunk_(), light_(), session_id_(0U), world_id_(0U), chunk_x_(0), chunk_z_(0),
	block_revision_(0U), light_revision_(0U), generation_epoch_(0U),
	initialized_(false)
{
}

WorldReplicationBlockReplica::WorldReplicationBlockReplica(
	const WorldReplicationBlockReplica &other)
	: chunk_(), light_(), session_id_(0U), world_id_(0U), chunk_x_(0), chunk_z_(0),
	block_revision_(0U), light_revision_(0U), generation_epoch_(0U),
	initialized_(false)
{
	(void)other;
}

WorldReplicationBlockReplica::~WorldReplicationBlockReplica()
{
	if (this->destroy() != FT_ERR_SUCCESS)
		return ;
}

WorldReplicationBlockReplica &WorldReplicationBlockReplica::operator=(
	const WorldReplicationBlockReplica &other)
{
	(void)other;
	return (*this);
}

int32_t WorldReplicationBlockReplica::initialize(uint64_t session_id,
	uint64_t world_id, int32_t chunk_x, int32_t chunk_z)
{
	int32_t error_code;

	if (this->initialized_)
		return (FT_ERR_ALREADY_INITIALISED);
	if (session_id == 0U || world_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = this->chunk_.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->light_.initialize(0U);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->session_id_ = session_id;
	this->world_id_ = world_id;
	this->chunk_x_ = chunk_x;
	this->chunk_z_ = chunk_z;
	this->block_revision_ = 0U;
	this->light_revision_ = 0U;
	this->generation_epoch_ = 0U;
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationBlockReplica::validate_snapshot(
	const ProtocolChunkSnapshotMessage &snapshot) const
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (snapshot.protocol_version != GAME_WORLD_DELTA_PROTOCOL_VERSION
		|| snapshot.session_id != this->session_id_
		|| snapshot.world_id != this->world_id_
		|| snapshot.chunk_x != this->chunk_x_
		|| snapshot.chunk_z != this->chunk_z_
		|| snapshot.block_revision == 0U
		|| snapshot.light_revision == 0U
		|| snapshot.generation_epoch == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (snapshot.generation_epoch < this->generation_epoch_)
		return (FT_ERR_INVALID_STATE);
	if (snapshot.generation_epoch == this->generation_epoch_
		&& snapshot.block_revision < this->block_revision_)
		return (FT_ERR_INVALID_STATE);
	if (snapshot.generation_epoch == this->generation_epoch_
		&& snapshot.block_revision == this->block_revision_
		&& snapshot.light_revision < this->light_revision_)
		return (FT_ERR_INVALID_STATE);
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationBlockReplica::apply_snapshot(
	const ProtocolChunkSnapshotMessage &snapshot)
{
	game_voxel_chunk temporary_chunk;
	voxel_light_chunk temporary_light;
	ft_byte_buffer payload;
	ft_byte_buffer light_payload;
	uint32_t light_cell_count;
	uint32_t light_cell_index;
	uint32_t light_cell_number;
	uint32_t light_local_x;
	uint32_t light_local_y;
	uint32_t light_local_z;
	uint8_t light_value;
	int32_t error_code;
	int32_t destroy_error;

	error_code = this->validate_snapshot(snapshot);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = temporary_chunk.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = temporary_light.initialize(0U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS && !snapshot.snapshot_payload.empty())
		error_code = payload.append(&snapshot.snapshot_payload[0],
			snapshot.snapshot_payload.size());
	if (error_code == FT_ERR_SUCCESS)
		error_code = game_world_delta_snapshot_deserialize(temporary_chunk,
		payload);
	if (error_code == FT_ERR_SUCCESS && payload.remaining() != 0U)
		error_code = FT_ERR_INVALID_ARGUMENT;
	if (error_code == FT_ERR_SUCCESS)
		error_code = protocol_chunk_light_payload_validate(
			snapshot.light_payload.empty() ? ft_nullptr
			: &snapshot.light_payload[0], snapshot.light_payload.size());
	if (error_code == FT_ERR_SUCCESS)
		error_code = light_payload.initialize();
	if (error_code == FT_ERR_SUCCESS && !snapshot.light_payload.empty())
		error_code = light_payload.append(&snapshot.light_payload[0],
			snapshot.light_payload.size());
	light_cell_count = 0U;
	light_cell_number = 0U;
	if (error_code == FT_ERR_SUCCESS)
		error_code = light_payload.read_u32_le(&light_cell_count);
	while (light_cell_number < light_cell_count
		&& error_code == FT_ERR_SUCCESS)
	{
		error_code = light_payload.read_u32_le(&light_cell_index);
		if (error_code == FT_ERR_SUCCESS)
			error_code = light_payload.read_u8(&light_value);
		if (error_code == FT_ERR_SUCCESS)
		{
			light_local_x = light_cell_index % GAME_VOXEL_CHUNK_WIDTH;
			light_local_y = (light_cell_index / GAME_VOXEL_CHUNK_WIDTH)
				% GAME_VOXEL_CHUNK_HEIGHT;
			light_local_z = light_cell_index
				/ (GAME_VOXEL_CHUNK_WIDTH * GAME_VOXEL_CHUNK_HEIGHT);
			error_code = temporary_light.set(
				static_cast<int32_t>(light_local_x),
				static_cast<int32_t>(light_local_y),
				static_cast<int32_t>(light_local_z), light_value);
		}
		light_cell_number += 1U;
	}
	if (error_code == FT_ERR_SUCCESS && light_payload.remaining() != 0U)
		error_code = FT_ERR_INVALID_ARGUMENT;
	if (error_code == FT_ERR_SUCCESS
		&& temporary_chunk.get_revision() != snapshot.block_revision)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->chunk_.move(temporary_chunk);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->light_.move(temporary_light);
	if (error_code == FT_ERR_SUCCESS)
	{
		this->block_revision_ = snapshot.block_revision;
		this->light_revision_ = snapshot.light_revision;
		this->generation_epoch_ = snapshot.generation_epoch;
	}
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = light_payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = temporary_chunk.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = temporary_light.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationBlockReplica::validate_delta(
	const ProtocolChunkBlockDeltaMessage &delta) const
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (delta.delta.protocol_version != GAME_WORLD_DELTA_PROTOCOL_VERSION
		|| delta.delta.session_id != this->session_id_
		|| delta.delta.world_id != this->world_id_
		|| delta.delta.chunk_x != this->chunk_x_
		|| delta.delta.chunk_z != this->chunk_z_
		|| delta.delta.previous_revision != this->block_revision_
		|| delta.delta.revision != delta.delta.previous_revision + 1U)
		return (FT_ERR_INVALID_STATE);
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationBlockReplica::apply_block_delta(
	const ProtocolChunkBlockDeltaMessage &delta)
{
	int32_t error_code;

	error_code = this->validate_delta(delta);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = this->chunk_.apply_authoritative_block_delta(delta.delta);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->block_revision_ = delta.delta.revision;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationBlockReplica::apply_light_delta(
	const ProtocolChunkLightDeltaMessage &delta)
{
	voxel_light_chunk temporary_light;
	ft_byte_buffer payload;
	uint32_t cell_count;
	uint32_t previous_index;
	uint32_t cell_index;
	uint32_t record_index;
	uint32_t local_x;
	uint32_t local_y;
	uint32_t local_z;
	uint32_t maximum_cells;
	uint8_t light_value;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (delta.protocol_version != GAME_WORLD_DELTA_PROTOCOL_VERSION
		|| delta.session_id != this->session_id_
		|| delta.world_id != this->world_id_
		|| delta.chunk_x != this->chunk_x_
		|| delta.chunk_z != this->chunk_z_
		|| delta.base_light_revision != this->light_revision_
		|| delta.final_light_revision <= delta.base_light_revision
		|| delta.source_block_revision != this->block_revision_
		|| delta.generation_epoch != this->generation_epoch_)
		return (FT_ERR_INVALID_STATE);
	if (protocol_chunk_light_payload_validate(
			delta.light_payload.empty() ? ft_nullptr : &delta.light_payload[0],
			delta.light_payload.size()) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_ARGUMENT);
	/* Payload: little-endian uint32 count followed by strictly ascending
	 * (little-endian uint32 linear cell index, uint8 packed light) records. */
	maximum_cells = static_cast<uint32_t>(GAME_VOXEL_CHUNK_WIDTH
		* GAME_VOXEL_CHUNK_HEIGHT * GAME_VOXEL_CHUNK_DEPTH);
	error_code = temporary_light.initialize(0U);
	for (local_z = 0U; local_z < GAME_VOXEL_CHUNK_DEPTH
		&& error_code == FT_ERR_SUCCESS; ++local_z)
	{
		for (local_y = 0U; local_y < GAME_VOXEL_CHUNK_HEIGHT
			&& error_code == FT_ERR_SUCCESS; ++local_y)
		{
			for (local_x = 0U; local_x < GAME_VOXEL_CHUNK_WIDTH;
				++local_x)
			{
				light_value = this->light_.get(
					static_cast<int32_t>(local_x),
					static_cast<int32_t>(local_y),
					static_cast<int32_t>(local_z));
				error_code = temporary_light.set(
					static_cast<int32_t>(local_x),
					static_cast<int32_t>(local_y),
					static_cast<int32_t>(local_z), light_value);
				if (error_code != FT_ERR_SUCCESS)
					break ;
			}
		}
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS && !delta.light_payload.empty())
		error_code = payload.append(&delta.light_payload[0],
			delta.light_payload.size());
	cell_count = 0U;
	previous_index = 0U;
	if (error_code == FT_ERR_SUCCESS)
		error_code = payload.read_u32_le(&cell_count);
	if (error_code == FT_ERR_SUCCESS && cell_count > maximum_cells)
		error_code = FT_ERR_OUT_OF_RANGE;
	record_index = 0U;
	while (record_index < cell_count && error_code == FT_ERR_SUCCESS)
	{
		error_code = payload.read_u32_le(&cell_index);
		if (error_code == FT_ERR_SUCCESS)
			error_code = payload.read_u8(&light_value);
		if (error_code == FT_ERR_SUCCESS
			&& (cell_index >= maximum_cells
				|| (record_index > 0U && cell_index <= previous_index)))
			error_code = FT_ERR_INVALID_ARGUMENT;
		if (error_code == FT_ERR_SUCCESS)
		{
			local_x = cell_index % GAME_VOXEL_CHUNK_WIDTH;
			local_y = (cell_index / GAME_VOXEL_CHUNK_WIDTH)
				% GAME_VOXEL_CHUNK_HEIGHT;
			local_z = cell_index / (GAME_VOXEL_CHUNK_WIDTH
				* GAME_VOXEL_CHUNK_HEIGHT);
			error_code = temporary_light.set(
				static_cast<int32_t>(local_x),
				static_cast<int32_t>(local_y),
				static_cast<int32_t>(local_z), light_value);
		}
		previous_index = cell_index;
		record_index += 1U;
	}
	if (error_code == FT_ERR_SUCCESS && payload.remaining() != 0U)
		error_code = FT_ERR_INVALID_ARGUMENT;
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = this->light_.move(temporary_light);
		if (error_code == FT_ERR_SUCCESS)
			this->light_revision_ = delta.final_light_revision;
	}
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = temporary_light.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationBlockReplica::apply_canonical_repair(
	const ProtocolChunkRepairResponseMessage &response)
{
	game_voxel_chunk temporary_chunk;
	ft_byte_buffer payload;
	uint8_t calculated_hash[PROTOCOL_CHUNK_HASH_SIZE];
	uint32_t block_id;
	uint32_t section_index;
	uint32_t local_x;
	uint32_t local_y;
	uint32_t local_z;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (response.protocol_version != GAME_WORLD_DELTA_PROTOCOL_VERSION
		|| response.session_id != this->session_id_
		|| response.world_id != this->world_id_
		|| response.chunk_x != this->chunk_x_
		|| response.chunk_z != this->chunk_z_
		|| response.block_revision == 0U
		|| response.light_revision == 0U
		|| response.generation_epoch == 0U
		|| response.repaired_section_mask == 0U
		|| response.payload_format
			!= PROTOCOL_CHUNK_REPAIR_PAYLOAD_CANONICAL_BLOCKS
		|| response.repair_payload.size()
			!= replica_canonical_repair_size(response.repaired_section_mask))
		return (FT_ERR_INVALID_ARGUMENT);
	if (response.generation_epoch < this->generation_epoch_
		|| (response.generation_epoch == this->generation_epoch_
			&& response.block_revision < this->block_revision_))
		return (FT_ERR_INVALID_STATE);
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = payload.append(&response.repair_payload[0],
			response.repair_payload.size());
	if (error_code == FT_ERR_SUCCESS)
		error_code = networking_replication_hash_payload(payload,
			calculated_hash);
	if (error_code == FT_ERR_SUCCESS
		&& ft_memcmp(calculated_hash, response.content_hash,
			PROTOCOL_CHUNK_HASH_SIZE) != 0)
		error_code = FT_ERR_INVALID_ARGUMENT;
	if (error_code == FT_ERR_SUCCESS)
		error_code = temporary_chunk.initialize();
	/* Copy untouched sections first so a section repair is non-destructive. */
	for (local_z = 0U; local_z < GAME_VOXEL_CHUNK_DEPTH
		&& error_code == FT_ERR_SUCCESS; ++local_z)
	{
		for (local_y = 0U; local_y < GAME_VOXEL_CHUNK_HEIGHT
			&& error_code == FT_ERR_SUCCESS; ++local_y)
		{
			for (local_x = 0U; local_x < GAME_VOXEL_CHUNK_WIDTH
				&& error_code == FT_ERR_SUCCESS; ++local_x)
			{
				error_code = this->chunk_.read_block(
					static_cast<int32_t>(local_x),
					static_cast<int32_t>(local_y),
					static_cast<int32_t>(local_z), &block_id);
				if (error_code == FT_ERR_SUCCESS)
					error_code = temporary_chunk.write_generated_block(
						static_cast<int32_t>(local_x),
						static_cast<int32_t>(local_y),
						static_cast<int32_t>(local_z), block_id);
			}
		}
	}
	for (section_index = 0U; section_index < GAME_VOXEL_CHUNK_SECTION_COUNT
		&& error_code == FT_ERR_SUCCESS; ++section_index)
	{
		if ((response.repaired_section_mask
			& static_cast<uint16_t>(1U << section_index)) == 0U)
			continue ;
		for (local_z = 0U; local_z < GAME_VOXEL_CHUNK_DEPTH
			&& error_code == FT_ERR_SUCCESS; ++local_z)
		{
			for (local_y = section_index * GAME_VOXEL_SECTION_EDGE;
				local_y < (section_index + 1U) * GAME_VOXEL_SECTION_EDGE
				&& error_code == FT_ERR_SUCCESS; ++local_y)
			{
				for (local_x = 0U; local_x < GAME_VOXEL_CHUNK_WIDTH
					&& error_code == FT_ERR_SUCCESS; ++local_x)
				{
					error_code = payload.read_u32_le(&block_id);
					if (error_code == FT_ERR_SUCCESS)
						error_code = temporary_chunk.write_generated_block(
							static_cast<int32_t>(local_x),
							static_cast<int32_t>(local_y),
							static_cast<int32_t>(local_z), block_id);
				}
			}
		}
	}
	if (error_code == FT_ERR_SUCCESS && payload.remaining() != 0U)
		error_code = FT_ERR_INVALID_ARGUMENT;
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->chunk_.move(temporary_chunk);
	if (error_code == FT_ERR_SUCCESS)
	{
		this->block_revision_ = response.block_revision;
		this->generation_epoch_ = response.generation_epoch;
	}
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = temporary_chunk.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationBlockReplica::read_block(int32_t local_x,
	int32_t local_y, int32_t local_z, uint32_t *block_id) const
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->chunk_.read_block(local_x, local_y, local_z, block_id));
}

uint64_t WorldReplicationBlockReplica::block_revision() const noexcept
{
	return (this->block_revision_);
}

uint64_t WorldReplicationBlockReplica::light_revision() const noexcept
{
	return (this->light_revision_);
}

uint64_t WorldReplicationBlockReplica::generation_epoch() const noexcept
{
	return (this->generation_epoch_);
}

int32_t WorldReplicationBlockReplica::chunk_x() const noexcept
{
	return (this->chunk_x_);
}

int32_t WorldReplicationBlockReplica::chunk_z() const noexcept
{
	return (this->chunk_z_);
}

const game_voxel_chunk &WorldReplicationBlockReplica::chunk() const noexcept
{
	return (this->chunk_);
}

const voxel_light_chunk &WorldReplicationBlockReplica::light() const noexcept
{
	return (this->light_);
}

int32_t WorldReplicationBlockReplica::destroy()
{
	int32_t error_code;

	error_code = this->chunk_.destroy();
	{
		int32_t light_error = this->light_.destroy();
		if (error_code == FT_ERR_SUCCESS)
			error_code = light_error;
	}
	this->session_id_ = 0U;
	this->world_id_ = 0U;
	this->chunk_x_ = 0;
	this->chunk_z_ = 0;
	this->block_revision_ = 0U;
	this->light_revision_ = 0U;
	this->generation_epoch_ = 0U;
	this->initialized_ = false;
	return (error_code);
}
