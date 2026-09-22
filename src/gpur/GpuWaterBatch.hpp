#ifndef GPU_WATER_BATCH_HPP
# define GPU_WATER_BATCH_HPP

# include "../../src/gpur/GpuChunkMesh.hpp"
# include "../../src/gpur/GpuTextureAtlas.hpp"
# include "../ft_vox.hpp"

class GpuWaterBatch
{
  public:
	GpuWaterBatch();
	GpuWaterBatch(const GpuWaterBatch &other);
	~GpuWaterBatch();
	GpuWaterBatch &operator=(const GpuWaterBatch &other);

	void destroy();
	void flush(GLint u_mvp, GLint u_chunk_offset, const float mvp[16],
		GpuTextureAtlas &atlas, const std::vector<int32_t> &visible_slots,
		GpuChunkMesh *chunk_meshes, const int32_t *chunk_world_x,
		const int32_t *chunk_world_z);

};

#endif
