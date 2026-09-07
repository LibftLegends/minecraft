#include "../../src/chunks/WorldChunk.hpp"

WorldChunk::WorldChunk() : chunk_x(0), chunk_z(0), world_x(0), world_z(0),
	mesh_revision(0U), voxel_revision(0U), light_revision(0U), pending_mesh_request_id(0U),
	mesh_dirty(false), initialized(false), chunk(), light(), mesh()
{
}

WorldChunk::WorldChunk(const WorldChunk &other) : chunk_x(0), chunk_z(0),
	world_x(0), world_z(0), mesh_revision(0U), voxel_revision(0U), light_revision(0U),
	pending_mesh_request_id(0U), mesh_dirty(false), initialized(false),
	chunk(), light(), mesh()
{
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
	this->pending_mesh_request_id = other.pending_mesh_request_id;
	this->mesh_dirty = other.mesh_dirty;
	this->initialized = true;
	other.reset_coordinates();
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
	this->pending_mesh_request_id = 0U;
	this->mesh_dirty = false;
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
	this->pending_mesh_request_id = 0U;
	this->mesh_dirty = false;
}
