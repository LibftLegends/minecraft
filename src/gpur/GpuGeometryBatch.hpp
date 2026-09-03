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
	GpuGeometryBatch();
	GpuGeometryBatch(const GpuGeometryBatch &other);
	~GpuGeometryBatch();
	GpuGeometryBatch &operator=(const GpuGeometryBatch &other);

	bool initialize();
	void destroy();

	void collect(const Camera &camera, const World &world, int width,
		int height);
	void flush_solid(GLuint world_shader_prog, GLint u_mvp,
		GLint u_chunk_offset, const float mvp[16], GpuTextureAtlas &atlas);
	void flush_water(GLuint world_shader_prog, GLint u_mvp,
		GLint u_chunk_offset, const float mvp[16], GpuTextureAtlas &atlas);
	size_t gpu_bytes() const;

	private:
	void sync_pending_visible_meshes(const Camera &camera, const World &world,
		int32_t &uploaded_count, size_t &uploaded_bytes);
	GpuWaterBatch	_water;
	bool			_visibility_cache_valid;
	uint64_t		_visibility_geometry_signature;
	double			_visibility_camera_x;
	double			_visibility_camera_y;
	double			_visibility_camera_z;
	double			_visibility_camera_yaw;
	double			_visibility_camera_pitch;
	int			_visibility_width;
	int			_visibility_height;
	int			_visibility_render_distance;
	int32_t		_visibility_loaded_chunk_count;
	int32_t		_visibility_center_chunk_x;
	int32_t		_visibility_center_chunk_z;
	bool			_visibility_chunk_index_valid;
	GpuChunkMesh	_chunk_meshes[WorldCoordinates::CHUNK_COUNT];
	std::vector<int32_t> _visible_chunk_slots;
#if defined(LIBFT_ENABLE_ANALYTICS)
	uint64_t		_analytics_collect_frame;
	bool			_analytics_collect_diagnostics;
#endif
	int32_t _upload_cursor;
	int32_t _visible_upload_cursor_slot;
	int32_t _visible_upload_cursor_chunk_x;
	int32_t _visible_upload_cursor_chunk_z;
	int32_t			_chunk_world_x[WorldCoordinates::CHUNK_COUNT];
	int32_t			_chunk_world_z[WorldCoordinates::CHUNK_COUNT];

};

#endif
