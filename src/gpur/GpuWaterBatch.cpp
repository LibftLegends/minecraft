#include "../../src/gpur/GpuWaterBatch.hpp"

GpuWaterBatch::GpuWaterBatch() : _vao(0), _ebo(0), _ebo_cap(0)
{
}

GpuWaterBatch::GpuWaterBatch(const GpuWaterBatch &other) : _vao(0), _ebo(0),
	_ebo_cap(0)
{
	(void)other;
}

GpuWaterBatch::~GpuWaterBatch()
{
	destroy();
}

GpuWaterBatch &GpuWaterBatch::operator=(const GpuWaterBatch &other)
{
	(void)other;
	return (*this);
}

void GpuWaterBatch::setup(GLuint shared_vbo, GLsizei vertex_stride)
{
	/* Layout must match GpuGeometryBatch::Vertex, since this VAO reads
	 * from the same shared vertex buffer as the solid mesh. */
	glGenVertexArrays(1, &_vao);
	glGenBuffers(1, &_ebo);
	glBindVertexArray(_vao);
	glBindBuffer(GL_ARRAY_BUFFER, shared_vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ebo);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertex_stride, nullptr);
	glEnableVertexAttribArray(0);
	glVertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, vertex_stride,
		reinterpret_cast<const void *>(static_cast<uintptr_t>(12)));
	glEnableVertexAttribArray(1);
	glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, vertex_stride,
		reinterpret_cast<const void *>(static_cast<uintptr_t>(16)));
	glEnableVertexAttribArray(2);
	glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, vertex_stride,
		reinterpret_cast<const void *>(static_cast<uintptr_t>(20)));
	glEnableVertexAttribArray(3);
	glBindVertexArray(0);
}

void GpuWaterBatch::destroy()
{
	if (_vao != 0)
	{
		glDeleteVertexArrays(1, &_vao);
		_vao = 0;
	}
	if (_ebo != 0)
	{
		glDeleteBuffers(1, &_ebo);
		_ebo = 0;
	}
	_ebo_cap = 0;
}

void GpuWaterBatch::upload(const std::vector<uint32_t> &water_idxs)
{
	const GLsizeiptr	ib = static_cast<GLsizeiptr>(water_idxs.size()
				* sizeof(uint32_t));

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ebo);
	if (static_cast<size_t>(ib) > _ebo_cap)
	{
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib, nullptr, GL_DYNAMIC_DRAW);
		_ebo_cap = static_cast<size_t>(ib);
	}
	if (ib > 0)
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, ib, water_idxs.data());
}

void GpuWaterBatch::flush(GLint u_mvp, GLint u_chunk_offset,
	const float mvp[16], GpuTextureAtlas &atlas,
	const std::vector<int32_t> &visible_slots, GpuChunkMesh *chunk_meshes,
	const int32_t *chunk_world_x, const int32_t *chunk_world_z)
{
	float	chunk_offset[3];

	if (visible_slots.empty())
		return ;
	glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
	atlas.bind(0);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	for (int32_t slot : visible_slots)
	{
		chunk_offset[0] = static_cast<float>(chunk_world_x[slot]);
		chunk_offset[1] = 0.0f;
		chunk_offset[2] = static_cast<float>(chunk_world_z[slot]);
		glUniform3fv(u_chunk_offset, 1, chunk_offset);
		chunk_meshes[slot].draw_water();
	}
	glBindVertexArray(0);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}
