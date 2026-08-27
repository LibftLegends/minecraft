#ifndef GPUR_CHUNK_MESH_HPP
# define GPUR_CHUNK_MESH_HPP

# include "../ft_vox.hpp"

class GpuChunkMesh
{
	GLuint _solid_vao;
	GLuint _water_vao;
	GLuint _vbo;
	GLuint _solid_ebo;
	GLuint _water_ebo;
	uint64_t _uploaded_revision;
	int32_t _solid_index_count;
	int32_t _water_index_count;
	size_t _gpu_bytes;

  public:
	GpuChunkMesh();
	GpuChunkMesh(const GpuChunkMesh &other);
	~GpuChunkMesh();
	GpuChunkMesh &operator=(const GpuChunkMesh &other);

	void sync(const chunk_mesh &mesh, uint64_t revision);
	void draw_solid() const;
	void draw_water() const;
	void destroy();
	bool has_geometry() const;
	size_t gpu_bytes() const;

  private:
	void alloc_buffers();
	void upload_geometry(const chunk_mesh &mesh,
		const std::vector<uint32_t> &solid_indices,
		const std::vector<uint32_t> &water_indices);
	void setup_attributes();
};

#endif
