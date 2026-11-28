#include "../../src/chunks/WorldChunk.hpp"
#include <algorithm>

namespace
{
	static bool boundary_has_transparent_block(const WorldChunkReadState &state,
		uint8_t face) noexcept
	{
		const bool x_boundary = face < 2U;
		const int32_t fixed = (face % 2U == 0U) ? 0
			: (x_boundary ? GAME_VOXEL_CHUNK_WIDTH - 1
				: GAME_VOXEL_CHUNK_DEPTH - 1);
		const int32_t span = x_boundary
			? GAME_VOXEL_CHUNK_DEPTH : GAME_VOXEL_CHUNK_WIDTH;
		for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1; y >= 0; --y)
		{
			for (int32_t offset = 0; offset < span; ++offset)
			{
				const uint32_t block_id = state.block_at(x_boundary ? fixed : offset,
					y, x_boundary ? offset : fixed);
				if (voxel_block_is_transparent(block_id) != FT_FALSE)
					return (true);
			}
		}
		return (false);
	}

	constexpr int32_t SECTION_HEIGHT = GAME_VOXEL_CHUNK_HEIGHT
		/ GAME_VOXEL_CHUNK_SECTION_COUNT;
	constexpr std::size_t CHUNK_CELLS = WorldChunkReadState::LIGHT_SAMPLE_COUNT;

	uint8_t boundary_light_maximum(const std::vector<uint8_t> &samples,
		uint8_t face) noexcept
	{
		const bool x_boundary = face < 2U;
		const int32_t fixed = (face % 2U != 0U) ? 0
			: (x_boundary ? GAME_VOXEL_CHUNK_WIDTH - 1
				: GAME_VOXEL_CHUNK_DEPTH - 1);
		const int32_t span = x_boundary
			? GAME_VOXEL_CHUNK_DEPTH : GAME_VOXEL_CHUNK_WIDTH;
		uint8_t maximum = 0U;
		for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1; y >= 0; --y)
		{
			for (int32_t offset = 0; offset < span; ++offset)
			{
				const uint8_t packed = samples[WorldChunkReadState::light_offset(
					x_boundary ? fixed : offset, y, x_boundary ? offset : fixed)];
				maximum = std::max(maximum, voxel_light_combined(packed, 0U));
				if (maximum == 15U)
					return (maximum);
			}
		}
		return (maximum);
	}

	std::size_t block_section_offset(int32_t x, int32_t y, int32_t z) noexcept
	{
		return ((static_cast<std::size_t>(z) * SECTION_HEIGHT
			+ y % SECTION_HEIGHT) * GAME_VOXEL_CHUNK_WIDTH + x);
	}
}

uint32_t WorldChunkReadState::block_at(int32_t x, int32_t y, int32_t z)
const noexcept
{
	return ((*this->block_sections[y / SECTION_HEIGHT])[
		block_section_offset(x, y, z)]);
}

int32_t WorldChunkReadState::materialize_blocks(
	std::vector<uint32_t> &output) const noexcept
{
	if (this->initialized == FT_FALSE)
		return (FT_ERR_NOT_INITIALISED);
	try
	{
		output.resize(CHUNK_CELLS);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
	{
		for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
		{
			const BlockSection &section = *this->block_sections[y / SECTION_HEIGHT];
			const std::size_t destination =
				(static_cast<std::size_t>(z) * GAME_VOXEL_CHUNK_HEIGHT + y)
				* GAME_VOXEL_CHUNK_WIDTH;
			std::copy_n(section.data() + block_section_offset(0, y, z),
				GAME_VOXEL_CHUNK_WIDTH, output.data() + destination);
		}
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
	mesh_dirty(false),
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
	mesh_dirty(false),
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
	this->pending_mesh_request_preview_only = other.pending_mesh_request_preview_only;
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
	this->border_delete_dependencies = other.border_delete_dependencies;
	this->published_border_delete_dependencies = other.published_border_delete_dependencies;
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
	this->border_delete_dependencies = {};
	this->published_border_delete_dependencies = {};
	this->last_light_remesh_incremental = FT_FALSE;
	this->last_incremental_light_content_version = 0U;
	this->last_incremental_light_voxel_revision = 0U;
	this->incremental_light_protection_until_frame = 0U;
	this->light_ready_for_render = false;
	this->mesh_dirty = false;
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
	/* Cancelling pending work does not change the mesh already published. */
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
	this->last_mesh_publication_interactive = FT_FALSE;
	this->last_mesh_publication_voxel_revision = 0U;
	this->border_delete_dependencies = {};
	this->published_border_delete_dependencies = {};
	this->mesh_dirty = false;
	this->clear_light_dependency();
	ft_memset(this->boundary_source_light_max, 0,
		sizeof(this->boundary_source_light_max));
	ft_memset(this->boundary_target_has_transparent, 0,
		sizeof(this->boundary_target_has_transparent));
	this->read_state.reset();
}

void WorldChunk::record_mesh_publication(uint64_t revision) noexcept
{
	this->last_mesh_publication_voxel_revision = revision;
	this->published_border_delete_dependencies = this->border_delete_dependencies;
}

void WorldChunk::set_pending_remesh_request(uint64_t request_id,
	ft_bool interactive) noexcept
{
	this->pending_mesh_request_id = request_id;
	this->pending_mesh_request_interactive = interactive;
	this->pending_mesh_request_preview_only = false;
	this->pending_mesh_request_voxel_revision = this->voxel_revision;
	this->pending_mesh_request_content_version = this->content_version;
	this->pending_mesh_request_light_input_version =
		this->light_input_version;
}

void WorldChunk::clear_pending_remesh_request() noexcept
{
	this->pending_mesh_request_id = 0U;
	this->pending_mesh_request_interactive = FT_FALSE;
	this->pending_mesh_request_preview_only = false;
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

int32_t WorldChunk::publish_read_state() noexcept
{
	std::shared_ptr<WorldChunkReadState> next_state;
	std::shared_ptr<std::vector<uint8_t>> samples;
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
		if (!this->initialized)
		{
			this->read_state = std::move(next_state);
			return (FT_ERR_SUCCESS);
		}
		next_state->generation_metadata = this->chunk.get_generation_metadata();
		if (this->read_state != nullptr
			&& this->read_state->initialized != FT_FALSE
			&& this->read_state->chunk_x == this->chunk_x
			&& this->read_state->chunk_z == this->chunk_z
			&& this->read_state->voxel_revision == this->voxel_revision
			&& this->read_state->content_version == this->content_version)
			next_state->block_sections = this->read_state->block_sections;
		else
		{
			std::vector<uint32_t> blocks(CHUNK_CELLS);
			const int32_t copy_error = this->chunk.copy_blocks(blocks.data(),
				static_cast<uint32_t>(blocks.size()));
			if (copy_error != FT_ERR_SUCCESS)
				return (copy_error);
			for (int32_t index = 0; index < GAME_VOXEL_CHUNK_SECTION_COUNT; ++index)
			{
				auto section = std::make_shared<WorldChunkReadState::BlockSection>();
				for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
				{
					const std::size_t source =
						(static_cast<std::size_t>(z) * GAME_VOXEL_CHUNK_HEIGHT
							+ index * SECTION_HEIGHT) * GAME_VOXEL_CHUNK_WIDTH;
					std::copy_n(blocks.data() + source,
						SECTION_HEIGHT * GAME_VOXEL_CHUNK_WIDTH,
						section->data() + block_section_offset(0, 0, z));
				}
				next_state->block_sections[index] = std::move(section);
			}
		}
		samples = std::make_shared<std::vector<uint8_t>>(CHUNK_CELLS);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
		for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
			for (int32_t x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
				(*samples)[WorldChunkReadState::light_offset(x, y, z)] = this->light.get(x, y, z);
	for (uint8_t face = 0U; face < 4U; ++face)
	{
		this->boundary_source_light_max[face] = boundary_light_maximum(*samples, face);
		this->boundary_target_has_transparent[face] =
			boundary_has_transparent_block(*next_state, face);
	}
	next_state->light = std::move(samples);
	this->read_state = std::move(next_state);
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunk::prepare_read_state_after_light_patch(
	const std::shared_ptr<const std::vector<uint8_t>> &updated_samples,
	std::shared_ptr<WorldChunkReadState> &prepared_state) const noexcept
{
	prepared_state.reset();
	if (this->read_state == nullptr || this->read_state->initialized == FT_FALSE
		|| this->read_state->chunk_x != this->chunk_x
		|| this->read_state->chunk_z != this->chunk_z
		|| this->read_state->voxel_revision != this->voxel_revision
		|| this->read_state->content_version != this->content_version)
		return (FT_ERR_NOT_FOUND);
	const auto &samples = updated_samples != nullptr ? updated_samples : this->read_state->light;
	if (samples == nullptr || samples->size() != CHUNK_CELLS
		|| (updated_samples == nullptr && this->read_state->light_valid == FT_FALSE))
		return (FT_ERR_NOT_FOUND);
	try
	{
		prepared_state = std::make_shared<WorldChunkReadState>(*this->read_state);
		prepared_state->light = samples;
		prepared_state->edited_faces = 0U;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldChunk::prepare_read_state_after_block_edit(int32_t local_x,
	int32_t local_y, int32_t local_z, uint32_t block_id,
	std::shared_ptr<WorldChunkReadState> &prepared_state) noexcept
{
	const WorldChunkBlockEdit edit = {local_x, local_y, local_z, 0U, block_id};
	return (this->prepare_read_state_after_block_edits(&edit, 1U, prepared_state));
}

int32_t WorldChunk::prepare_read_state_after_block_edits(const WorldChunkBlockEdit *edits,
	std::size_t count, std::shared_ptr<WorldChunkReadState> &prepared_state) noexcept
{
	prepared_state.reset();
	if (!this->initialized)
		return (FT_ERR_NOT_INITIALISED);
	if (edits == nullptr || count == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	for (std::size_t index = 0U; index < count; ++index)
	{
		const WorldChunkBlockEdit &edit = edits[index];
		if (edit.local_x < 0 || edit.local_x >= GAME_VOXEL_CHUNK_WIDTH
			|| edit.local_y < 0 || edit.local_y >= GAME_VOXEL_CHUNK_HEIGHT
			|| edit.local_z < 0 || edit.local_z >= GAME_VOXEL_CHUNK_DEPTH)
			return (FT_ERR_INVALID_ARGUMENT);
	}
	if (this->read_state == nullptr || this->read_state->initialized == FT_FALSE
		|| this->read_state->chunk_x != this->chunk_x
		|| this->read_state->chunk_z != this->chunk_z
		|| this->read_state->voxel_revision != this->voxel_revision
		|| this->read_state->content_version != this->content_version)
	{
		const int32_t publish_error = this->publish_read_state();
		if (publish_error != FT_ERR_SUCCESS)
			return (publish_error);
	}
	try
	{
		auto next_state = std::make_shared<WorldChunkReadState>(*this->read_state);
		std::array<std::shared_ptr<WorldChunkReadState::BlockSection>,
			GAME_VOXEL_CHUNK_SECTION_COUNT> touched_sections;
		next_state->edited_faces = 0U;
		for (std::size_t index = 0U; index < count; ++index)
		{
			const WorldChunkBlockEdit &edit = edits[index];
			const int32_t section_index = edit.local_y / SECTION_HEIGHT;
			auto &section = touched_sections[section_index];
			if (section == nullptr)
			{
				section = std::make_shared<WorldChunkReadState::BlockSection>(
					*next_state->block_sections[section_index]);
				next_state->block_sections[section_index] = section;
			}
			(*section)[block_section_offset(edit.local_x, edit.local_y, edit.local_z)] =
				edit.replacement_block_id;
			if (edit.local_x == 0)
				next_state->edited_faces |= 1U << 0U;
			if (edit.local_x == GAME_VOXEL_CHUNK_WIDTH - 1)
				next_state->edited_faces |= 1U << 1U;
			if (edit.local_z == 0)
				next_state->edited_faces |= 1U << 2U;
			if (edit.local_z == GAME_VOXEL_CHUNK_DEPTH - 1)
				next_state->edited_faces |= 1U << 3U;
		}
		prepared_state = std::move(next_state);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

void WorldChunk::commit_prepared_read_state(
	std::shared_ptr<WorldChunkReadState> &prepared_state,
	uint8_t changed_light_faces) noexcept
{
	if (prepared_state == nullptr)
		return ;
	prepared_state->voxel_revision = this->voxel_revision;
	prepared_state->light_revision = this->light_revision;
	prepared_state->content_version = this->content_version;
	prepared_state->light_input_version = this->light_input_version;
	prepared_state->light_version = this->light_version;
	prepared_state->computed_light_input_version =
		this->computed_light_input_version;
	prepared_state->light_valid = this->light_buffer_is_valid()
		? FT_TRUE : FT_FALSE;
	/* Interior edits cannot change a face summary. Boundary edits only
	 * resample the affected faces, starting in the usually open sky. */
	for (uint8_t face = 0U; face < 4U; ++face)
	{
		if ((prepared_state->edited_faces & (1U << face)) != 0U)
			this->boundary_target_has_transparent[face] =
				boundary_has_transparent_block(*prepared_state, face);
		/* Source summaries index the opposite face of this chunk. */
		if ((changed_light_faces & (1U << (face ^ 1U))) != 0U)
			this->boundary_source_light_max[face] =
				boundary_light_maximum(*prepared_state->light, face);
	}
	this->read_state = std::move(prepared_state);
}

int32_t WorldChunk::publish_read_state_after_block_edit(int32_t local_x,
	int32_t local_y, int32_t local_z, uint32_t block_id) noexcept
{
	std::shared_ptr<WorldChunkReadState> prepared_state;
	int32_t error_code = this->prepare_read_state_after_block_edit(local_x,
		local_y, local_z, block_id, prepared_state);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->commit_prepared_read_state(prepared_state);
	return (FT_ERR_SUCCESS);
}

void WorldChunk::mark_content_changed() noexcept
{
	this->mark_content_changed(true);
}

void WorldChunk::mark_content_changed(bool light_input_changed) noexcept
{
	const bool light_was_current = this->light_is_current();
	this->content_version = world_light_version::next(this->content_version);
	this->voxel_revision += 1U;
	if (light_input_changed)
		this->mark_light_input_changed();
	else if (light_was_current)
		this->light_version = this->content_version;
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
