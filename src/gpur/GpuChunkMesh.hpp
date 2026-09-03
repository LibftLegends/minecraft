#ifndef GPUR_CHUNK_MESH_HPP
# define GPUR_CHUNK_MESH_HPP

# include "../ft_vox.hpp"
# include "../../Libft/Modules/Template/vector.hpp"

class GpuChunkMesh
{
	GLuint _solid_vao;
	GLuint _water_vao;
	GLuint _vbo;
	GLuint _solid_ebo;
	GLuint _water_ebo;
	int32_t _uploaded_chunk_x;
	int32_t _uploaded_chunk_z;
	uint64_t _uploaded_voxel_revision;
	uint64_t _uploaded_revision;
	bool _has_uploaded_geometry;
#if defined(LIBFT_ENABLE_ANALYTICS)
	int32_t _uploaded_min_y;
	int32_t _uploaded_max_y;
#endif
	int32_t _solid_index_count;
	int32_t _water_index_count;
	size_t _gpu_bytes;

  public:
	GpuChunkMesh();
	GpuChunkMesh(const GpuChunkMesh &other);
	~GpuChunkMesh();
	GpuChunkMesh &operator=(const GpuChunkMesh &other);

	void sync(const chunk_mesh &mesh, uint64_t revision,
		int32_t chunk_x, int32_t chunk_z, uint64_t voxel_revision);
	void draw_solid() const;
	void draw_water() const;
	void destroy();
	void invalidate();
	bool has_geometry() const;
	bool has_solid_geometry() const;
	bool has_water_geometry() const;
	bool needs_sync(uint64_t revision, int32_t chunk_x, int32_t chunk_z,
		uint64_t voxel_revision) const;
	size_t gpu_bytes() const;
#if defined(LIBFT_ENABLE_ANALYTICS)
	bool diagnostics_identity_matches(uint64_t revision, int32_t chunk_x,
		int32_t chunk_z, uint64_t voxel_revision) const;
	int32_t diagnostics_min_y() const;
	int32_t diagnostics_max_y() const;
#endif

  private:
	void alloc_buffers();
	void upload_geometry(const chunk_mesh &mesh,
		const ft_vector<uint32_t> &solid_indices,
		const ft_vector<uint32_t> &water_indices);
	void setup_attributes();
};

#endif
