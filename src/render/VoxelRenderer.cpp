#include "../../src/render/VoxelRenderer.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"
#include <cstdio>
#if defined(LIBFT_ENABLE_ANALYTICS)
# include <chrono>
#endif

const int VoxelRenderer::OVERLAY_W = 512;
const int VoxelRenderer::OVERLAY_H = 360;

VoxelRenderer::VoxelRenderer()
{
	_overlay_fb.width = OVERLAY_W;
	_overlay_fb.height = OVERLAY_H;
	_overlay_fb.pixels = _overlay_buf;
	_overlay_frame = 0U;
	_software_diagnostic_frame = 0U;
	_software_visibility_cache_valid = false;
	_software_visibility_geometry_signature = 0U;
	_software_visibility_camera_x = 0.0;
	_software_visibility_camera_y = 0.0;
	_software_visibility_camera_z = 0.0;
	_software_visibility_camera_yaw = 0.0;
	_software_visibility_camera_pitch = 0.0;
	_software_visibility_width = 0;
	_software_visibility_height = 0;
	_software_visibility_render_distance = 0;
	_software_visibility_loaded_chunk_count = -1;
	_software_visibility_center_chunk_x = 0;
	_software_visibility_center_chunk_z = 0;
}

VoxelRenderer::VoxelRenderer(const VoxelRenderer &other)
{
	(void)other;
	_overlay_fb.width = OVERLAY_W;
	_overlay_fb.height = OVERLAY_H;
	_overlay_fb.pixels = _overlay_buf;
	_overlay_frame = 0U;
	_software_diagnostic_frame = 0U;
	_software_visibility_cache_valid = false;
	_software_visibility_geometry_signature = 0U;
	_software_visibility_camera_x = 0.0;
	_software_visibility_camera_y = 0.0;
	_software_visibility_camera_z = 0.0;
	_software_visibility_camera_yaw = 0.0;
	_software_visibility_camera_pitch = 0.0;
	_software_visibility_width = 0;
	_software_visibility_height = 0;
	_software_visibility_render_distance = 0;
	_software_visibility_loaded_chunk_count = -1;
	_software_visibility_center_chunk_x = 0;
	_software_visibility_center_chunk_z = 0;
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
		int32_t analytics_error = RuntimeAnalytics::begin_scope(
			RuntimeAnalyticsScope::GPU_OVERLAY);
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr, "Analytics: overlay scope start failed (%d)\n",
				analytics_error);
		std::fill_n(_overlay_buf, OVERLAY_W * OVERLAY_H, 0U);
		DebugOverlayRenderer::draw_debug_overlay(_overlay_fb, debug);
		this->gpu.upload_overlay(_overlay_buf, OVERLAY_W, OVERLAY_H);
		analytics_error = RuntimeAnalytics::end_scope();
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr, "Analytics: overlay scope end failed (%d)\n",
				analytics_error);
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
#if defined(LIBFT_ENABLE_ANALYTICS)
	std::size_t software_candidates;
	std::size_t software_visible_chunks;
	std::size_t software_visible_triangles;
	uint64_t software_slowest_chunk_us;
	int32_t software_slowest_chunk_x;
	int32_t software_slowest_chunk_z;
	std::chrono::steady_clock::time_point chunk_start;
	uint64_t chunk_us;
	ft_bool collect_software_detail;
#endif

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
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::SOFTWARE_PREPARE);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: software prepare scope start failed (%d)\n",
			analytics_error);
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
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: software prepare scope end failed (%d)\n",
			analytics_error);
	SkyboxRenderer::clear_frame(render_target, this->target.depth_buffer(),
		camera);
	const bool visibility_cache_matches =
		_software_visibility_cache_valid
		&& _software_visibility_geometry_signature == world.geometry_revision
		&& std::abs(_software_visibility_camera_x - camera.x) <= 1.0e-9
		&& std::abs(_software_visibility_camera_y - camera.y) <= 1.0e-9
		&& std::abs(_software_visibility_camera_z - camera.z) <= 1.0e-9
		&& std::abs(_software_visibility_camera_yaw - camera.yaw) <= 1.0e-9
		&& std::abs(_software_visibility_camera_pitch - camera.pitch) <= 1.0e-9
		&& _software_visibility_width == render_target.width
		&& _software_visibility_height == render_target.height
		&& _software_visibility_render_distance == world.active_render_distance
		&& _software_visibility_loaded_chunk_count == world.loaded_chunk_count
		&& _software_visibility_center_chunk_x == world.center_chunk_x
		&& _software_visibility_center_chunk_z == world.center_chunk_z;
	if (!visibility_cache_matches)
	{
		_software_visible_chunk_slots.clear();
		for (int32_t index = 0; index < world.chunk_count; ++index)
		{
			const WorldChunk &chunk = world.chunks[index];
			if (chunk.initialized
				&& MeshCuller::chunk_is_visible(camera, chunk, render_cache))
			{
				_software_visible_chunk_slots.push_back(index);
			}
		}
		_software_visibility_geometry_signature = world.geometry_revision;
		_software_visibility_camera_x = camera.x;
		_software_visibility_camera_y = camera.y;
		_software_visibility_camera_z = camera.z;
		_software_visibility_camera_yaw = camera.yaw;
		_software_visibility_camera_pitch = camera.pitch;
		_software_visibility_width = render_target.width;
		_software_visibility_height = render_target.height;
		_software_visibility_render_distance = world.active_render_distance;
		_software_visibility_loaded_chunk_count = world.loaded_chunk_count;
		_software_visibility_center_chunk_x = world.center_chunk_x;
		_software_visibility_center_chunk_z = world.center_chunk_z;
		_software_visibility_cache_valid = true;
	}
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::SOFTWARE_MESHES);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: software mesh scope start failed (%d)\n",
			analytics_error);
	#if defined(LIBFT_ENABLE_ANALYTICS)
	this->_software_diagnostic_frame += 1U;
	collect_software_detail = this->_software_diagnostic_frame % 120U == 0U
		? FT_TRUE : FT_FALSE;
	software_candidates = 0U;
	software_visible_chunks = 0U;
	software_visible_triangles = 0U;
	software_slowest_chunk_us = 0U;
	software_slowest_chunk_x = 0;
	software_slowest_chunk_z = 0;
	#endif
	for (int32_t visible_index = 0; visible_index
		< static_cast<int32_t>(_software_visible_chunk_slots.size());
		visible_index++)
	{
		const int32_t i = _software_visible_chunk_slots[visible_index];
		#if defined(LIBFT_ENABLE_ANALYTICS)
		if (collect_software_detail != FT_FALSE)
		{
			software_candidates += 1U;
			chunk_start = std::chrono::steady_clock::now();
		}
		#endif
		if (this->mesh_renderer.draw_mesh_visible(render_target,
				this->target.depth_buffer(), camera, render_cache,
				world.chunks[i]))
		{
			local_debug.visible_chunks = local_debug.visible_chunks + 1;
			#if defined(LIBFT_ENABLE_ANALYTICS)
			if (collect_software_detail != FT_FALSE)
			{
				software_visible_chunks += 1U;
				software_visible_triangles +=
					world.chunks[i].mesh.indices.size() / 3U;
			}
			#endif
		}
		#if defined(LIBFT_ENABLE_ANALYTICS)
		if (collect_software_detail != FT_FALSE)
		{
			chunk_us = static_cast<uint64_t>(std::chrono::duration_cast<
				std::chrono::microseconds>(std::chrono::steady_clock::now()
					- chunk_start).count());
			if (chunk_us > software_slowest_chunk_us)
			{
				software_slowest_chunk_us = chunk_us;
				software_slowest_chunk_x = world.chunks[i].chunk_x;
				software_slowest_chunk_z = world.chunks[i].chunk_z;
			}
		}
		#endif
	}
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: software mesh scope end failed (%d)\n",
			analytics_error);
#if defined(LIBFT_ENABLE_ANALYTICS)
	if (collect_software_detail != FT_FALSE
		&& software_slowest_chunk_us >= 8000U)
		std::fprintf(stderr,
			"[Analytics][Render] software_mesh_breakdown candidates=%zu "
			"visible_chunks=%zu visible_triangles=%zu slowest_chunk=(%d,%d) "
			"slowest_us=%llu\n", software_candidates, software_visible_chunks,
			software_visible_triangles, software_slowest_chunk_x,
			software_slowest_chunk_z,
			static_cast<unsigned long long>(software_slowest_chunk_us));
#endif
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::SOFTWARE_POSTPROCESS);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: software postprocess scope start failed (%d)\n",
			analytics_error);
	this->target.blit_to(framebuffer);
	if (debug != nullptr)
	DebugOverlayRenderer::draw_debug_overlay(framebuffer, &local_debug);
	DebugOverlayRenderer::draw_crosshair(framebuffer);
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: software postprocess scope end failed (%d)\n",
			analytics_error);
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
