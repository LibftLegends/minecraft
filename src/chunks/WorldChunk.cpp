#include "../../src/chunks/WorldChunk.hpp"

WorldChunk::WorldChunk() : chunk_x(0), chunk_z(0), world_x(0), world_z(0),
	mesh_revision(0U), initialized(false)
{
}

WorldChunk::WorldChunk(const WorldChunk &other) : chunk_x(0), chunk_z(0),
	world_x(0), world_z(0), mesh_revision(0U), initialized(false)
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

void WorldChunk::reset_coordinates()
{
	this->chunk_x = 0;
	this->chunk_z = 0;
	this->world_x = 0;
	this->world_z = 0;
}

void WorldChunk::destroy()
{
	if (!this->initialized)
		return ;
	(void)chunk_mesh_destroy(this->mesh);
	(void)this->chunk.destroy();
	this->initialized = false;
}
