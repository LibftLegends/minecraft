#include "../../src/gpur/GpuWaterBatch.hpp"

GpuWaterBatch::GpuWaterBatch()
{
}

GpuWaterBatch::GpuWaterBatch(const GpuWaterBatch &other)
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

void GpuWaterBatch::destroy()
{
	return ;
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
