#include "../../src/render/VoxelRenderer.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"
#include <cstdio>

const int VoxelRenderer::OVERLAY_W = 512;
const int VoxelRenderer::OVERLAY_H = 360;

VoxelRenderer::VoxelRenderer()
{
	_overlay_fb.width = OVERLAY_W;
	_overlay_fb.height = OVERLAY_H;
	_overlay_fb.pixels = _overlay_buf;
	_overlay_frame = 0U;
}

VoxelRenderer::VoxelRenderer(const VoxelRenderer &other)
{
	(void)other;
}

VoxelRenderer::~VoxelRenderer()
{
}

VoxelRenderer &VoxelRenderer::operator=(const VoxelRenderer &other)
{
	(void)other;
	return (*this);
}

void VoxelRenderer::render_gpu_overlay(const RenderDebug *debug)
{
	if (debug == nullptr)
		return ;
	_overlay_frame += 1U;
	if (_overlay_frame % 15U == 1U)
	{
		std::fill_n(_overlay_buf, OVERLAY_W * OVERLAY_H, 0U);
		DebugOverlayRenderer::draw_debug_overlay(_overlay_fb, debug);
		this->gpu.upload_overlay(_overlay_buf, OVERLAY_W, OVERLAY_H);
	}
	this->gpu.draw_overlay();
}

void VoxelRenderer::render_world_gpu(const Camera &camera, const World &world,
	const RenderDebug *debug)
{
	uint32_t	eye_block;
	int32_t analytics_error;

	analytics_error = RuntimeAnalytics::begin_scope(RuntimeAnalyticsScope::VOXEL_RENDER_GPU);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: GPU render scope start failed (%d)\n",
			analytics_error);
	eye_block = 0U;
	this->gpu.render(camera, world);
	if (world.block_id_at(static_cast<int32_t>(std::floor(camera.x)),
			static_cast<int32_t>(std::floor(camera.y)),
			static_cast<int32_t>(std::floor(camera.z)), &eye_block)
		&& eye_block == VOXEL_GENERATOR_WATER_BLOCK)
		this->gpu.draw_tint(0.05f, 0.35f, 0.75f, 0.35f);
	render_gpu_overlay(debug);
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: GPU render scope end failed (%d)\n",
			analytics_error);
}

void VoxelRenderer::render_world_software(ft_render_framebuffer &framebuffer,
	const Camera &camera, const World &world, const RenderDebug *debug)
{
	ft_render_framebuffer	render_target;
	RenderCache				render_cache;
	RenderDebug				local_debug;
	int32_t analytics_error;

	analytics_error = RuntimeAnalytics::begin_scope(RuntimeAnalyticsScope::VOXEL_RENDER_SOFTWARE);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: software render scope start failed (%d)\n",
			analytics_error);

	if (framebuffer.pixels == nullptr || framebuffer.width <= 0
		|| framebuffer.height <= 0)
	{
		analytics_error = RuntimeAnalytics::end_scope();
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr, "Analytics: software render scope end failed (%d)\n",
				analytics_error);
		return ;
	}
	render_target = this->target.prepare(framebuffer);
	render_cache.configure(camera, render_target.width, render_target.height,
		world.active_render_distance);
	local_debug.fps = 0.0;
	local_debug.frame_ms = 0.0;
	local_debug.visible_chunks = 0;
	local_debug.loaded_chunks = world.loaded_chunk_count;
	local_debug.render_distance = world.active_render_distance;
	local_debug.selected_block_id = 0U;
	if (debug != nullptr)
		local_debug = *debug;
	SkyboxRenderer::clear_frame(render_target, this->target.depth_buffer(),
		camera);
	for (int i = 0; i < world.chunk_count; i++)
	{
		if (this->mesh_renderer.draw_mesh(render_target,
				this->target.depth_buffer(), camera, render_cache,
				world.chunks[i]))
			local_debug.visible_chunks = local_debug.visible_chunks + 1;
	}
	this->target.blit_to(framebuffer);
	if (debug != nullptr)
	DebugOverlayRenderer::draw_debug_overlay(framebuffer, &local_debug);
	DebugOverlayRenderer::draw_crosshair(framebuffer);
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: software render scope end failed (%d)\n",
			analytics_error);
}

void VoxelRenderer::render_world(ft_render_framebuffer &framebuffer,
	const Camera &camera, const World &world, double display_fps)
{
	RenderDebug	debug;

	if (display_fps < 0.0)
	{
		this->render_world(framebuffer, camera, world,
			static_cast<const RenderDebug *>(nullptr));
		return ;
	}
	debug.fps = display_fps;
	debug.frame_ms = 0.0;
	debug.visible_chunks = 0;
	debug.loaded_chunks = world.loaded_chunk_count;
	debug.render_distance = world.active_render_distance;
	debug.selected_block_id = 0U;
	this->render_world(framebuffer, camera, world, &debug);
}

void VoxelRenderer::render_world(ft_render_framebuffer &framebuffer,
	const Camera &camera, const World &world, const RenderDebug *debug)
{
	int32_t analytics_error;

	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::VOXEL_RENDER_WORLD);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: world render scope start failed (%d)\n",
			analytics_error);
	if (this->gpu.is_ready())
	{
		render_world_gpu(camera, world, debug);
		analytics_error = RuntimeAnalytics::end_scope();
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr, "Analytics: world render scope end failed (%d)\n",
				analytics_error);
		return ;
	}
	render_world_software(framebuffer, camera, world, debug);
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: world render scope end failed (%d)\n",
			analytics_error);
}

void VoxelRenderer::preload_assets()
{
	(void)TextureAtlas::instance();
}

int32_t VoxelRenderer::initialize_gpu(int width, int height)
{
	if (this->gpu.initialize(width, height))
		return (FT_ERR_SUCCESS);
	return (FT_ERR_INVALID_STATE);
}

void VoxelRenderer::resize_gpu(int width, int height)
{
	if (this->gpu.is_ready())
		this->gpu.resize(width, height);
}

GpuRenderer *VoxelRenderer::get_gpu_renderer()
{
	return (gpu.is_ready() ? &gpu : nullptr);
}
