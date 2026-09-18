#include "../../src/chunks/WorldChunk.hpp"

namespace
{
	/* A light-only publication does not introduce a new block version.  Keep
	 * its block parent anchored at the most recent state that actually owns a
	 * block snapshot (full or overlay), otherwise repeated light publications
	 * create an unbounded read-state chain and every worker capture becomes
	 * progressively more expensive. */
	static std::shared_ptr<const WorldChunkReadState> block_owner(
		const std::shared_ptr<const WorldChunkReadState> &state) noexcept
	{
		std::shared_ptr<const WorldChunkReadState> owner = state;
		while (owner != nullptr && owner->block_parent != nullptr
			&& owner->has_block_overlay == FT_FALSE)
			owner = owner->block_parent;
		return (owner);
	}
}

int32_t WorldChunkReadState::materialize_blocks(
	std::vector<uint32_t> &output) const noexcept
{
	try
	{
		if (this->block_parent != nullptr)
		{
			if (this->block_parent->materialize_blocks(output)
				!= FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_OPERATION);
			if (this->has_block_overlay != FT_FALSE)
			{
				if (this->block_overlay_index >= output.size())
					return (FT_ERR_INVALID_ARGUMENT);
				output[this->block_overlay_index] = this->block_overlay_value;
			}
			return (FT_ERR_SUCCESS);
		}
		output = this->blocks;
	}
	catch (...)
	{
		output.clear();
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

WorldChunk::WorldChunk() : chunk_x(0), chunk_z(0), world_x(0), world_z(0),
	mesh_revision(0U), voxel_revision(0U), light_revision(0U), content_version(1U),
	light_version(world_light_version::INVALID_VERSION), light_input_version(1U),
	computed_light_input_version(world_light_version::INVALID_VERSION),
	pending_mesh_request_id(0U),
	pending_mesh_request_interactive(FT_FALSE),
	pending_mesh_request_voxel_revision(0U),
	pending_mesh_request_content_version(0U),
	pending_mesh_request_light_input_version(0U),
	last_mesh_publication_interactive(FT_FALSE),
	last_mesh_publication_voxel_revision(0U),
	last_light_remesh_incremental(FT_FALSE),
	last_incremental_light_content_version(0U),
	last_incremental_light_voxel_revision(0U),
	incremental_light_protection_until_frame(0U), light_ready_for_render(false),
	remesh_cancellation_token(std::make_shared<std::atomic<uint64_t>>(0U)),
	mesh_dirty(false), border_mesh_publication_pending(false),
	border_mesh_publication_voxel_revision(0U),
	initialized(false), waits_for_neighbor_light(false),
	light_dependency_chunk_x(0), light_dependency_chunk_z(0), chunk(), light(), mesh(),
	read_state()
{
	ft_memset(this->boundary_source_light_max, 0,
		sizeof(this->boundary_source_light_max));
	ft_memset(this->boundary_target_has_transparent, 0,
		sizeof(this->boundary_target_has_transparent));
}

WorldChunk::WorldChunk(const WorldChunk &other) : chunk_x(0), chunk_z(0),
	world_x(0), world_z(0), mesh_revision(0U), voxel_revision(0U), light_revision(0U),
	content_version(1U), light_version(world_light_version::INVALID_VERSION),
	light_input_version(1U),
	computed_light_input_version(world_light_version::INVALID_VERSION),
	pending_mesh_request_id(0U),
	pending_mesh_request_interactive(FT_FALSE),
	pending_mesh_request_voxel_revision(0U),
	pending_mesh_request_content_version(0U),
	pending_mesh_request_light_input_version(0U),
	last_mesh_publication_interactive(FT_FALSE),
	last_mesh_publication_voxel_revision(0U),
	last_light_remesh_incremental(FT_FALSE),
	last_incremental_light_content_version(0U),
	last_incremental_light_voxel_revision(0U),
	incremental_light_protection_until_frame(0U), light_ready_for_render(false),
	remesh_cancellation_token(std::make_shared<std::atomic<uint64_t>>(0U)),
	mesh_dirty(false), border_mesh_publication_pending(false),
	border_mesh_publication_voxel_revision(0U),
	initialized(false), waits_for_neighbor_light(false),
	light_dependency_chunk_x(0), light_dependency_chunk_z(0),
	chunk(), light(), mesh(), read_state()
{
	ft_memset(this->boundary_source_light_max, 0,
		sizeof(this->boundary_source_light_max));
	ft_memset(this->boundary_target_has_transparent, 0,
		sizeof(this->boundary_target_has_transparent));
	(void)other;
}

WorldChunk::~WorldChunk()
{
	this->destroy();
}

WorldChunk &WorldChunk::operator=(const WorldChunk &other)
{
	(void)other;
	return (*this);
}

namespace
{
	static int32_t move_mesh_payload(chunk_mesh &destination,
		chunk_mesh &source) noexcept
	{
		int32_t error_code;

		error_code = destination.vertices.move(source.vertices);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		error_code = destination.indices.move(source.indices);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		error_code = destination.solid_indices.move(source.solid_indices);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		error_code = destination.water_indices.move(source.water_indices);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		destination.bounds = source.bounds;
		destination.occupied_bounds = source.occupied_bounds;
		destination.has_occupied_bounds = source.has_occupied_bounds;
		return (FT_ERR_SUCCESS);
	}
}

int32_t WorldChunk::move(WorldChunk &other) noexcept
{
	int32_t error_code;

	if (this == &other)
		return (FT_ERR_SUCCESS);
	if (other.initialized == false)
	{
		this->reset_coordinates();
		this->initialized = false;
		return (FT_ERR_SUCCESS);
	}
	if (this->initialized == true)
		this->destroy();
	error_code = this->chunk.move(other.chunk);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = this->light.move(other.light);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = move_mesh_payload(this->mesh, other.mesh);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->chunk_x = other.chunk_x;
	this->chunk_z = other.chunk_z;
	this->world_x = other.world_x;
	this->world_z = other.world_z;
	this->mesh_revision = other.mesh_revision;
	this->voxel_revision = other.voxel_revision;
	this->light_revision = other.light_revision;
	this->content_version = other.content_version;
	this->light_version = other.light_version;
	this->light_input_version = other.light_input_version;
	this->computed_light_input_version = other.computed_light_input_version;
	this->pending_mesh_request_id = other.pending_mesh_request_id;
	this->pending_mesh_request_interactive =
		other.pending_mesh_request_interactive;
	this->pending_mesh_request_voxel_revision =
		other.pending_mesh_request_voxel_revision;
	this->pending_mesh_request_content_version =
		other.pending_mesh_request_content_version;
	this->pending_mesh_request_light_input_version =
		other.pending_mesh_request_light_input_version;
	this->last_mesh_publication_interactive =
		other.last_mesh_publication_interactive;
	this->last_mesh_publication_voxel_revision =
		other.last_mesh_publication_voxel_revision;
	this->last_light_remesh_incremental = other.last_light_remesh_incremental;
	this->last_incremental_light_content_version =
		other.last_incremental_light_content_version;
	this->last_incremental_light_voxel_revision =
		other.last_incremental_light_voxel_revision;
	this->incremental_light_protection_until_frame =
		other.incremental_light_protection_until_frame;
	this->light_ready_for_render = other.light_ready_for_render;
	this->remesh_cancellation_token = std::move(
		other.remesh_cancellation_token);
	if (this->remesh_cancellation_token == nullptr)
		this->remesh_cancellation_token =
			std::make_shared<std::atomic<uint64_t>>(0U);
	this->mesh_dirty = other.mesh_dirty;
	this->border_mesh_publication_pending = other.border_mesh_publication_pending;
	this->border_mesh_publication_voxel_revision =
		other.border_mesh_publication_voxel_revision;
	this->read_state = std::move(other.read_state);
	this->waits_for_neighbor_light = other.waits_for_neighbor_light;
	this->light_dependency_chunk_x = other.light_dependency_chunk_x;
	this->light_dependency_chunk_z = other.light_dependency_chunk_z;
	ft_memcpy(this->boundary_source_light_max,
		other.boundary_source_light_max,
		sizeof(this->boundary_source_light_max));
	ft_memcpy(this->boundary_target_has_transparent,
		other.boundary_target_has_transparent,
		sizeof(this->boundary_target_has_transparent));
	this->initialized = true;
	other.reset_coordinates();
	other.remesh_cancellation_token =
		std::make_shared<std::atomic<uint64_t>>(0U);
	other.initialized = false;
	return (FT_ERR_SUCCESS);
}

bool WorldChunk::mesh_is_drawable(const chunk_mesh &mesh) noexcept
{
	ft_size_t index;

	if (mesh.has_occupied_bounds != FT_TRUE || mesh.vertices.empty()
		|| (mesh.solid_indices.empty() && mesh.water_indices.empty())
		|| mesh.indices.size() != mesh.solid_indices.size()
			+ mesh.water_indices.size())
		return (false);
	index = 0U;
	while (index < mesh.solid_indices.size())
	{
		if (mesh.solid_indices[index] >= mesh.vertices.size())
			return (false);
		index += 1U;
	}
	index = 0U;
	while (index < mesh.water_indices.size())
	{
		if (mesh.water_indices[index] >= mesh.vertices.size())
			return (false);
		index += 1U;
	}
	return (true);
}

void WorldChunk::reset_coordinates()
{
	this->chunk_x = 0;
	this->chunk_z = 0;
	this->world_x = 0;
	this->world_z = 0;
	this->voxel_revision = 0U;
	this->light_revision = 0U;
	this->content_version = 1U;
	this->light_version = world_light_version::INVALID_VERSION;
	this->light_input_version = 1U;
	this->computed_light_input_version = world_light_version::INVALID_VERSION;
	this->read_state.reset();
	this->clear_pending_remesh_request();
	this->last_mesh_publication_interactive = FT_FALSE;
	this->last_mesh_publication_voxel_revision = 0U;
	this->last_light_remesh_incremental = FT_FALSE;
	this->last_incremental_light_content_version = 0U;
	this->last_incremental_light_voxel_revision = 0U;
	this->incremental_light_protection_until_frame = 0U;
	this->light_ready_for_render = false;
	this->mesh_dirty = false;
	this->border_mesh_publication_pending = false;
	this->border_mesh_publication_voxel_revision = 0U;
	this->waits_for_neighbor_light = false;
	this->light_dependency_chunk_x = 0;
	this->light_dependency_chunk_z = 0;
	ft_memset(this->boundary_source_light_max, 0,
		sizeof(this->boundary_source_light_max));
	ft_memset(this->boundary_target_has_transparent, 0,
		sizeof(this->boundary_target_has_transparent));
}

void WorldChunk::cancel_remesh_work() noexcept
{
	if (this->remesh_cancellation_token != nullptr)
		this->remesh_cancellation_token->fetch_add(1U,
			std::memory_order_acq_rel);
	this->clear_pending_remesh_request();
	this->last_mesh_publication_interactive = FT_FALSE;
	this->last_mesh_publication_voxel_revision = 0U;
	this->last_light_remesh_incremental = FT_FALSE;
	this->last_incremental_light_content_version = 0U;
	this->last_incremental_light_voxel_revision = 0U;
}

void WorldChunk::destroy()
{
	if (!this->initialized)
		return ;
	(void)chunk_mesh_destroy(this->mesh);
	(void)this->light.destroy();
	(void)this->chunk.destroy();
	this->initialized = false;
	this->voxel_revision = 0U;
	this->light_revision = 0U;
	this->content_version = 1U;
	this->light_version = world_light_version::INVALID_VERSION;
	this->light_input_version = 1U;
	this->computed_light_input_version = world_light_version::INVALID_VERSION;
	this->clear_pending_remesh_request();
	this->last_light_remesh_incremental = FT_FALSE;
	this->last_incremental_light_content_version = 0U;
	this->last_incremental_light_voxel_revision = 0U;
	this->incremental_light_protection_until_frame = 0U;
	this->light_ready_for_render = false;
	this->cancel_remesh_work();
	this->mesh_dirty = false;
	this->border_mesh_publication_voxel_revision = 0U;
	this->clear_light_dependency();
	ft_memset(this->boundary_source_light_max, 0,
		sizeof(this->boundary_source_light_max));
	ft_memset(this->boundary_target_has_transparent, 0,
		sizeof(this->boundary_target_has_transparent));
	this->read_state.reset();
}

void WorldChunk::set_pending_remesh_request(uint64_t request_id,
	ft_bool interactive) noexcept
{
	this->pending_mesh_request_id = request_id;
	this->pending_mesh_request_interactive = interactive;
	this->pending_mesh_request_voxel_revision = this->voxel_revision;
	this->pending_mesh_request_content_version = this->content_version;
	this->pending_mesh_request_light_input_version =
		this->light_input_version;
}

void WorldChunk::clear_pending_remesh_request() noexcept
{
	this->pending_mesh_request_id = 0U;
	this->pending_mesh_request_interactive = FT_FALSE;
	this->pending_mesh_request_voxel_revision = 0U;
	this->pending_mesh_request_content_version = 0U;
	this->pending_mesh_request_light_input_version = 0U;
}

bool WorldChunk::owns_current_interactive_remesh() const noexcept
{
	if (this->pending_mesh_request_id == 0U
		|| this->pending_mesh_request_interactive == FT_FALSE)
		return (false);
	if (this->pending_mesh_request_voxel_revision != this->voxel_revision)
		return (false);
	if (this->pending_mesh_request_content_version
		!= this->content_version)
		return (false);
	if (this->pending_mesh_request_light_input_version
		!= this->light_input_version)
		return (false);
	return (true);
}

void WorldChunk::set_border_mesh_publication_pending(bool pending) noexcept
{
	this->border_mesh_publication_pending = pending;
	this->border_mesh_publication_voxel_revision = pending
		? this->voxel_revision : 0U;
}

bool WorldChunk::border_mesh_publication_is_current() const noexcept
{
	return (this->border_mesh_publication_pending
		&& this->border_mesh_publication_voxel_revision != 0U
		&& this->border_mesh_publication_voxel_revision
		== this->voxel_revision);
}

int32_t WorldChunk::publish_read_state() noexcept
{
	std::shared_ptr<WorldChunkReadState> next_state;
	ft_memset(this->boundary_source_light_max, 0,
		sizeof(this->boundary_source_light_max));
	ft_memset(this->boundary_target_has_transparent, 0,
		sizeof(this->boundary_target_has_transparent));
	try
	{
		next_state = std::make_shared<WorldChunkReadState>();
		next_state->chunk_x = this->chunk_x;
		next_state->chunk_z = this->chunk_z;
		next_state->world_x = this->world_x;
		next_state->world_z = this->world_z;
		next_state->voxel_revision = this->voxel_revision;
		next_state->light_revision = this->light_revision;
		next_state->content_version = this->content_version;
		next_state->light_input_version = this->light_input_version;
		next_state->light_version = this->light_version;
		next_state->computed_light_input_version =
		this->computed_light_input_version;
		next_state->initialized = this->initialized ? FT_TRUE : FT_FALSE;
		next_state->light_valid = this->light_buffer_is_valid() ? FT_TRUE : FT_FALSE;
		next_state->generation_metadata = this->chunk.get_generation_metadata();
		next_state->block_parent.reset();
		next_state->block_overlay_index = 0U;
		next_state->block_overlay_value = GAME_VOXEL_AIR_BLOCK;
		next_state->has_block_overlay = FT_FALSE;
		if (this->initialized == false)
		{
			this->read_state = std::move(next_state);
			return (FT_ERR_SUCCESS);
		}
		const std::size_t full_size = static_cast<std::size_t>(
			GAME_VOXEL_CHUNK_WIDTH) * static_cast<std::size_t>(
			GAME_VOXEL_CHUNK_HEIGHT) * static_cast<std::size_t>(
			GAME_VOXEL_CHUNK_DEPTH);
		const std::shared_ptr<const WorldChunkReadState> previous_block_owner =
			block_owner(this->read_state);
		const bool reuse_previous_blocks = previous_block_owner != nullptr
			&& this->read_state->initialized != FT_FALSE
			&& this->read_state->voxel_revision == this->voxel_revision
			&& (previous_block_owner->block_parent != nullptr
				|| previous_block_owner->blocks.size() == full_size);
		if (reuse_previous_blocks)
		{
			next_state->block_parent = previous_block_owner;
			next_state->has_block_overlay = FT_FALSE;
		}
		else
			next_state->blocks.resize(full_size);
		next_state->light.resize(full_size);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (next_state->block_parent == nullptr
		&& this->chunk.copy_blocks(&next_state->blocks[0],
			static_cast<uint32_t>(next_state->blocks.size())) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_OPERATION);
	/* Light publication still copies the complete immutable light field, but
	 * boundary transparency is independent of it.  Recompute only the four
	 * exposed faces so a light-only commit does not scan the whole block field. */
	ft_memset(this->boundary_target_has_transparent, 0,
		sizeof(this->boundary_target_has_transparent));
	for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
		for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
			for (int32_t x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
			{
				std::size_t block_index = (static_cast<std::size_t>(z)
					* GAME_VOXEL_CHUNK_HEIGHT + y) * GAME_VOXEL_CHUNK_WIDTH + x;
				uint8_t light_value = this->light.get(x, y, z);

				next_state->light[block_index]
					= light_value;
				if (x == GAME_VOXEL_CHUNK_WIDTH - 1)
				{
					if (voxel_light_sky(light_value)
						> this->boundary_source_light_max[0])
						this->boundary_source_light_max[0] =
							voxel_light_sky(light_value);
					if (voxel_light_block(light_value)
						> this->boundary_source_light_max[0])
						this->boundary_source_light_max[0] =
							voxel_light_block(light_value);
				}
				if (x == 0)
				{
					if (voxel_light_sky(light_value)
						> this->boundary_source_light_max[1])
						this->boundary_source_light_max[1] =
							voxel_light_sky(light_value);
					if (voxel_light_block(light_value)
						> this->boundary_source_light_max[1])
						this->boundary_source_light_max[1] =
							voxel_light_block(light_value);
				}
				if (z == GAME_VOXEL_CHUNK_DEPTH - 1)
				{
					if (voxel_light_sky(light_value)
						> this->boundary_source_light_max[2])
						this->boundary_source_light_max[2] =
							voxel_light_sky(light_value);
					if (voxel_light_block(light_value)
						> this->boundary_source_light_max[2])
						this->boundary_source_light_max[2] =
							voxel_light_block(light_value);
				}
				if (z == 0)
				{
					if (voxel_light_sky(light_value)
						> this->boundary_source_light_max[3])
						this->boundary_source_light_max[3] =
							voxel_light_sky(light_value);
					if (voxel_light_block(light_value)
						> this->boundary_source_light_max[3])
						this->boundary_source_light_max[3] =
							voxel_light_block(light_value);
				}
			}
	for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
		for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
		{
			uint32_t boundary_block;
			if (this->chunk.read_block(0, y, z, &boundary_block)
				== FT_ERR_SUCCESS
				&& voxel_block_is_transparent(boundary_block) != FT_FALSE)
				this->boundary_target_has_transparent[0] = true;
			if (this->chunk.read_block(GAME_VOXEL_CHUNK_WIDTH - 1, y, z,
				&boundary_block) == FT_ERR_SUCCESS
				&& voxel_block_is_transparent(boundary_block) != FT_FALSE)
				this->boundary_target_has_transparent[1] = true;
		}
	for (int32_t x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
		for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
		{
			uint32_t boundary_block;
			if (this->chunk.read_block(x, y, 0, &boundary_block)
				== FT_ERR_SUCCESS
				&& voxel_block_is_transparent(boundary_block) != FT_FALSE)
				this->boundary_target_has_transparent[2] = true;
			if (this->chunk.read_block(x, y, GAME_VOXEL_CHUNK_DEPTH - 1,
				&boundary_block) == FT_ERR_SUCCESS
				&& voxel_block_is_transparent(boundary_block) != FT_FALSE)
				this->boundary_target_has_transparent[3] = true;
		}
	this->read_state = std::move(next_state);
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunk::publish_read_state_after_block_edit(int32_t local_x,
	int32_t local_y, int32_t local_z, uint32_t block_id) noexcept
{
	const std::size_t full_size = static_cast<std::size_t>(
		GAME_VOXEL_CHUNK_WIDTH) * static_cast<std::size_t>(
		GAME_VOXEL_CHUNK_HEIGHT) * static_cast<std::size_t>(
		GAME_VOXEL_CHUNK_DEPTH);
	std::shared_ptr<WorldChunkReadState> next_state;
	std::shared_ptr<const WorldChunkReadState> previous_state =
		this->read_state;
	std::size_t overlay_index;

	if (local_x < 0 || local_x >= GAME_VOXEL_CHUNK_WIDTH
		|| local_y < 0 || local_y >= GAME_VOXEL_CHUNK_HEIGHT
		|| local_z < 0 || local_z >= GAME_VOXEL_CHUNK_DEPTH)
		return (FT_ERR_INVALID_ARGUMENT);
	/* A delta state is valid only when the previous immutable state describes
	 * the same chunk.  Loader/recovery paths can publish without a parent, so
	 * retain the complete publication as a safe fallback for those callers. */
	if (previous_state == nullptr || previous_state->initialized == FT_FALSE
		|| previous_state->chunk_x != this->chunk_x
		|| previous_state->chunk_z != this->chunk_z
		|| previous_state->light.size() != full_size)
		return (this->publish_read_state());
	try
	{
		next_state = std::make_shared<WorldChunkReadState>();
		next_state->chunk_x = this->chunk_x;
		next_state->chunk_z = this->chunk_z;
		next_state->world_x = this->world_x;
		next_state->world_z = this->world_z;
		next_state->voxel_revision = this->voxel_revision;
		next_state->light_revision = this->light_revision;
		next_state->content_version = this->content_version;
		next_state->light_input_version = this->light_input_version;
		next_state->light_version = this->light_version;
		next_state->computed_light_input_version =
			this->computed_light_input_version;
		next_state->initialized = this->initialized ? FT_TRUE : FT_FALSE;
		next_state->light_valid = this->light_buffer_is_valid()
			? FT_TRUE : FT_FALSE;
		next_state->generation_metadata = this->chunk.get_generation_metadata();
		next_state->light = previous_state->light;
		next_state->block_parent = block_owner(previous_state);
		overlay_index = (static_cast<std::size_t>(local_z)
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
			+ static_cast<std::size_t>(local_y))
			* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
			+ static_cast<std::size_t>(local_x);
		next_state->block_overlay_index = overlay_index;
		next_state->block_overlay_value = block_id;
		next_state->has_block_overlay = FT_TRUE;
		/* The live light field is unchanged by a block edit.  Only the target
		 * transparency summaries need to be rescanned, and that is four boundary
		 * faces rather than the complete chunk. */
		ft_memset(this->boundary_target_has_transparent, 0,
			sizeof(this->boundary_target_has_transparent));
		for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
			for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
			{
				uint32_t boundary_block;
				if (this->chunk.read_block(0, y, z, &boundary_block)
					== FT_ERR_SUCCESS
					&& voxel_block_is_transparent(boundary_block) != FT_FALSE)
					this->boundary_target_has_transparent[0] = true;
				if (this->chunk.read_block(GAME_VOXEL_CHUNK_WIDTH - 1, y, z,
					&boundary_block) == FT_ERR_SUCCESS
					&& voxel_block_is_transparent(boundary_block) != FT_FALSE)
					this->boundary_target_has_transparent[1] = true;
			}
		for (int32_t x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
			for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
			{
				uint32_t boundary_block;
				if (this->chunk.read_block(x, y, 0, &boundary_block)
					== FT_ERR_SUCCESS
					&& voxel_block_is_transparent(boundary_block) != FT_FALSE)
					this->boundary_target_has_transparent[2] = true;
				if (this->chunk.read_block(x, y, GAME_VOXEL_CHUNK_DEPTH - 1,
					&boundary_block) == FT_ERR_SUCCESS
					&& voxel_block_is_transparent(boundary_block) != FT_FALSE)
					this->boundary_target_has_transparent[3] = true;
			}
		this->read_state = std::move(next_state);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

void WorldChunk::mark_content_changed() noexcept
{
	this->content_version = world_light_version::next(this->content_version);
	this->voxel_revision += 1U;
	this->mark_light_input_changed();
	return ;
}

void WorldChunk::mark_light_input_changed() noexcept
{
	this->light_input_version = world_light_version::next(
		this->light_input_version);
	return ;
}

void WorldChunk::set_light_dependency(int32_t dependency_x,
	int32_t dependency_z) noexcept
{
	this->waits_for_neighbor_light = true;
	this->light_dependency_chunk_x = dependency_x;
	this->light_dependency_chunk_z = dependency_z;
}

void WorldChunk::clear_light_dependency() noexcept
{
	this->waits_for_neighbor_light = false;
	this->light_dependency_chunk_x = 0;
	this->light_dependency_chunk_z = 0;
}

bool WorldChunk::light_buffer_is_valid() const noexcept
{
	return (this->light_version != world_light_version::INVALID_VERSION
		&& this->computed_light_input_version
			!= world_light_version::INVALID_VERSION);
}

bool WorldChunk::light_is_current() const noexcept
{
	return (world_light_version::matches(this->light_version,
		this->content_version)
		&& world_light_version::matches(this->computed_light_input_version,
			this->light_input_version));
}

uint8_t WorldChunk::get_boundary_source_light_max(uint8_t face) const noexcept
{
	if (face >= 4U)
		return (0U);
	return (this->boundary_source_light_max[face]);
}

bool WorldChunk::boundary_has_transparent_target(uint8_t face) const noexcept
{
	if (face >= 4U)
		return (false);
	return (this->boundary_target_has_transparent[face]);
}
