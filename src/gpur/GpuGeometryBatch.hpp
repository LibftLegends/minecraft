#ifndef GPU_GEOMETRY_BATCH_HPP
# define GPU_GEOMETRY_BATCH_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../../src/camera/Camera.hpp"
# include "../../src/chunks/WorldChunk.hpp"
# include "../../src/frame/RenderCache.hpp"
# include "../../src/gpur/GpuChunkMesh.hpp"
# include "../../src/gpur/GpuTextureAtlas.hpp"
# include "../../src/gpur/GpuWaterBatch.hpp"
# include "../../src/meshes/MeshCuller.hpp"
# include "../../src/world/World.hpp"
# include "../ft_vox.hpp"

class GpuGeometryBatch
{
  public:
	struct			Vertex
	{
		float x, y, z;
		uint16_t tu, tv;
		uint32_t	block_id;
		uint32_t	face;
	};

	GpuGeometryBatch();
	GpuGeometryBatch(const GpuGeometryBatch &other);
	~GpuGeometryBatch();
	GpuGeometryBatch &operator=(const GpuGeometryBatch &other);

	bool initialize(GLuint shared_vbo);
	void destroy();

	void collect(const Camera &camera, const World &world, int width,
		int height);
	void flush_solid(GLuint world_shader_prog, GLint u_mvp,
		GLint u_chunk_offset, const float mvp[16], GpuTextureAtlas &atlas);
	void flush_water(GLuint world_shader_prog, GLint u_mvp,
		GLint u_chunk_offset, const float mvp[16], GpuTextureAtlas &atlas);
	size_t gpu_bytes() const;

  private:
	GLuint			_mega_vao;
	GLuint			_mega_vbo;
	GLuint			_mega_ebo;
	size_t			_mega_vbo_cap;
	size_t			_mega_ebo_cap;
	std::vector<Vertex> _mega_verts;
	std::vector<uint32_t> _mega_idxs;

	GpuWaterBatch	_water;
	std::vector<uint32_t> _water_idxs;

	bool			_owns_vbo;
	uint64_t		_geometry_signature;
	bool			_cache_valid;
	bool			_buffers_dirty;
	GpuChunkMesh	_chunk_meshes[WorldCoordinates::CHUNK_COUNT];
	std::vector<int32_t> _visible_chunk_slots;
	int32_t			_chunk_world_x[WorldCoordinates::CHUNK_COUNT];
	int32_t			_chunk_world_z[WorldCoordinates::CHUNK_COUNT];

	void setup_world_vao();
	void bind_vertex_attribs();
	void destroy_mega_buffers();
	void add_chunk(const WorldChunk &wc, uint32_t base);
	void upload_solid_buffers();
	void upload_buffers_if_dirty();
	static uint64_t geometry_signature(const World &world);
};

#endif
