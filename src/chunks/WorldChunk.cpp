#include "../../src/chunks/WorldChunk.hpp"

WorldChunk::WorldChunk() : chunk_x(0), chunk_z(0), world_x(0), world_z(0),
	mesh_revision(0U), voxel_revision(0U), pending_mesh_request_id(0U),
	mesh_dirty(false), initialized(false), chunk(), mesh()
{
}

WorldChunk::WorldChunk(const WorldChunk &other) : chunk_x(0), chunk_z(0),
	world_x(0), world_z(0), mesh_revision(0U), voxel_revision(0U),
	pending_mesh_request_id(0U), mesh_dirty(false), initialized(false),
	chunk(), mesh()
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
	this->pending_mesh_request_id = 0U;
	this->mesh_dirty = false;
}

void WorldChunk::destroy()
{
	if (!this->initialized)
		return ;
	(void)chunk_mesh_destroy(this->mesh);
	(void)this->chunk.destroy();
	this->initialized = false;
	this->voxel_revision = 0U;
	this->pending_mesh_request_id = 0U;
	this->mesh_dirty = false;
}
