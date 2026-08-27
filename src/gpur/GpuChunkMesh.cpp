#include "../../src/gpur/GpuChunkMesh.hpp"

GpuChunkMesh::GpuChunkMesh() : _solid_vao(0), _water_vao(0), _vbo(0),
	_solid_ebo(0), _water_ebo(0), _uploaded_revision(0U), _solid_index_count(0),
	_water_index_count(0), _gpu_bytes(0U)
{
}

GpuChunkMesh::GpuChunkMesh(const GpuChunkMesh &other) : _solid_vao(0),
	_water_vao(0), _vbo(0), _solid_ebo(0), _water_ebo(0),
	_uploaded_revision(0U), _solid_index_count(0), _water_index_count(0),
	_gpu_bytes(0U)
{
	(void)other;
}

GpuChunkMesh::~GpuChunkMesh()
{
	destroy();
}

GpuChunkMesh &GpuChunkMesh::operator=(const GpuChunkMesh &other)
{
	(void)other;
	return (*this);
}

void GpuChunkMesh::alloc_buffers()
{
	glGenVertexArrays(1, &_solid_vao);
	glGenVertexArrays(1, &_water_vao);
	glGenBuffers(1, &_vbo);
	glGenBuffers(1, &_solid_ebo);
	glGenBuffers(1, &_water_ebo);
}

void GpuChunkMesh::upload_geometry(const chunk_mesh &mesh,
	const std::vector<uint32_t> &solid_indices,
	const std::vector<uint32_t> &water_indices)
{
	const size_t	vertex_bytes = mesh.vertices.size()
			* sizeof(chunk_mesh_vertex);
	const size_t	solid_bytes = solid_indices.size() * sizeof(uint32_t);
	const size_t	water_bytes = water_indices.size() * sizeof(uint32_t);

	glBindBuffer(GL_ARRAY_BUFFER, _vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertex_bytes),
		vertex_bytes > 0U
			? static_cast<const void *>(mesh.vertices.begin()) : nullptr,
		GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _solid_ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(solid_bytes),
		solid_bytes > 0U
			? static_cast<const void *>(solid_indices.data()) : nullptr,
		GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _water_ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(water_bytes),
		water_bytes > 0U
			? static_cast<const void *>(water_indices.data()) : nullptr,
		GL_STATIC_DRAW);
	_gpu_bytes = vertex_bytes + solid_bytes + water_bytes;
}

void GpuChunkMesh::setup_attributes()
{
	constexpr GLsizei	stride = static_cast<GLsizei>(sizeof(chunk_mesh_vertex));

	glVertexAttribPointer(0, 3, GL_UNSIGNED_SHORT, GL_FALSE, stride, nullptr);
	glEnableVertexAttribArray(0);
	glVertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, stride,
		reinterpret_cast<const void *>(offsetof(chunk_mesh_vertex, texture_u)));
	glEnableVertexAttribArray(1);
	glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, stride,
		reinterpret_cast<const void *>(offsetof(chunk_mesh_vertex, block_id)));
	glEnableVertexAttribArray(2);
	glVertexAttribIPointer(3, 1, GL_UNSIGNED_BYTE, stride,
		reinterpret_cast<const void *>(offsetof(chunk_mesh_vertex, face)));
	glEnableVertexAttribArray(3);
}

void GpuChunkMesh::sync(const chunk_mesh &mesh, uint64_t revision)
{
	uint32_t	vertex_index;

	if (_solid_vao != 0 && revision == _uploaded_revision)
		return ;
	if (_solid_vao == 0)
		alloc_buffers();
	std::vector<uint32_t> solid_indices;
	std::vector<uint32_t> water_indices;
	solid_indices.reserve(mesh.indices.size());
	for (size_t index = 0; index < mesh.indices.size(); ++index)
	{
		vertex_index = mesh.indices[index];
		if (mesh.vertices[vertex_index].block_id == TERRAIN_GENERATOR_WATER_BLOCK)
			water_indices.push_back(vertex_index);
		else
			solid_indices.push_back(vertex_index);
	}
	upload_geometry(mesh, solid_indices, water_indices);
	glBindVertexArray(_solid_vao);
	glBindBuffer(GL_ARRAY_BUFFER, _vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _solid_ebo);
	setup_attributes();
	glBindVertexArray(_water_vao);
	glBindBuffer(GL_ARRAY_BUFFER, _vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _water_ebo);
	setup_attributes();
	glBindVertexArray(0);
	_uploaded_revision = revision;
	_solid_index_count = static_cast<int32_t>(solid_indices.size());
	_water_index_count = static_cast<int32_t>(water_indices.size());
}

void GpuChunkMesh::draw_solid() const
{
	if (_solid_vao == 0 || _solid_index_count <= 0)
		return ;
	glBindVertexArray(_solid_vao);
	glDrawElements(GL_TRIANGLES, _solid_index_count, GL_UNSIGNED_INT, nullptr);
}

void GpuChunkMesh::draw_water() const
{
	if (_water_vao == 0 || _water_index_count <= 0)
		return ;
	glBindVertexArray(_water_vao);
	glDrawElements(GL_TRIANGLES, _water_index_count, GL_UNSIGNED_INT, nullptr);
}

void GpuChunkMesh::destroy()
{
	if (_solid_vao != 0)
	{
		glDeleteVertexArrays(1, &_solid_vao);
		glDeleteVertexArrays(1, &_water_vao);
		glDeleteBuffers(1, &_vbo);
		glDeleteBuffers(1, &_solid_ebo);
		glDeleteBuffers(1, &_water_ebo);
		_solid_vao = 0;
		_water_vao = 0;
		_vbo = 0;
		_solid_ebo = 0;
		_water_ebo = 0;
	}
	_uploaded_revision = 0U;
	_solid_index_count = 0;
	_water_index_count = 0;
	_gpu_bytes = 0U;
}

bool GpuChunkMesh::has_geometry() const
{
	return (_solid_index_count > 0 || _water_index_count > 0);
}

size_t GpuChunkMesh::gpu_bytes() const
{
	return (_gpu_bytes);
}
