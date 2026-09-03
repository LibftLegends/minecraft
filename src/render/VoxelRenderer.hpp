#ifndef VOXEL_RENDERER_HPP
# define VOXEL_RENDERER_HPP

# include "../../src/assets/TextureAtlas.hpp"
# include "../../src/camera/Camera.hpp"
# include "../../src/debug/DebugOverlayRenderer.hpp"
# include "../../src/debug/RenderDebug.hpp"
# include "../../src/frame/RenderCache.hpp"
# include "../../src/frame/RenderTarget.hpp"
# include "../../src/frame/RendererColor.hpp"
# include "../../src/geometry/ScreenVertex.hpp"
# include "../../src/geometry/TriangleTexture.hpp"
# include "../../src/gpur/GpuRenderer.hpp"
# include "../../src/meshes/ChunkMeshRenderer.hpp"
# include "../../src/sky/SkyboxRenderer.hpp"
# include "../../src/world/World.hpp"
# include "../ft_vox.hpp"

class VoxelRenderer
{
  public:
	VoxelRenderer();
	VoxelRenderer(const VoxelRenderer &other);
	~VoxelRenderer();
	VoxelRenderer &operator=(const VoxelRenderer &other);

	void render_world(ft_render_framebuffer &framebuffer, const Camera &camera,
		const World &world, double display_fps);
	void render_world(ft_render_framebuffer &framebuffer, const Camera &camera,
		const World &world, const RenderDebug *debug);
	void preload_assets();
	int32_t initialize_gpu(int width, int height);
	void resize_gpu(int width, int height);
	GpuRenderer *get_gpu_renderer();

  private:
	static const int OVERLAY_W;
	static const int OVERLAY_H;

	uint32_t _overlay_buf[512 * 360];
	ft_render_framebuffer _overlay_fb;
	uint64_t _overlay_frame;
	uint64_t _software_diagnostic_frame;
	bool _software_visibility_cache_valid;
	uint64_t _software_visibility_geometry_signature;
	double _software_visibility_camera_x;
	double _software_visibility_camera_y;
	double _software_visibility_camera_z;
	double _software_visibility_camera_yaw;
	double _software_visibility_camera_pitch;
	int _software_visibility_width;
	int _software_visibility_height;
	int _software_visibility_render_distance;
	int32_t _software_visibility_loaded_chunk_count;
	int32_t _software_visibility_center_chunk_x;
	int32_t _software_visibility_center_chunk_z;
	std::vector<int32_t> _software_visible_chunk_slots;
	RenderTarget target;
	ChunkMeshRenderer mesh_renderer;
	GpuRenderer gpu;

	void render_world_gpu(const Camera &camera, const World &world,
		const RenderDebug *debug);
	void render_gpu_overlay(const RenderDebug *debug);
	void render_world_software(ft_render_framebuffer &framebuffer,
		const Camera &camera, const World &world, const RenderDebug *debug);
};

#endif
