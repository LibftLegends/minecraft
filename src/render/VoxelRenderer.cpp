#include "../../src/render/VoxelRenderer.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"
#include "../../src/physics/PlayerGeometry.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cmath>
#include <vector>
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
	_debug_overlay_fb.width = OVERLAY_W;
	_debug_overlay_fb.height = OVERLAY_H;
	_debug_overlay_fb.pixels = _debug_overlay_buf;
	_overlay_frame = 0U;
	_gpu_debug_overlay_valid = false;
	_gpu_item_overlay_visible = false;
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
	_debug_overlay_fb.width = OVERLAY_W;
	_debug_overlay_fb.height = OVERLAY_H;
	_debug_overlay_fb.pixels = _debug_overlay_buf;
	_overlay_frame = 0U;
	_gpu_debug_overlay_valid = false;
	_gpu_item_overlay_visible = false;
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

void VoxelRenderer::render_gpu_overlay(const RenderDebug *debug,
	const Camera &camera, const World &world)
{
	_overlay_frame += 1U;
	bool debug_overlay_changed = false;
	if (debug == nullptr)
	{
		debug_overlay_changed = _gpu_debug_overlay_valid;
		std::fill_n(_debug_overlay_buf, OVERLAY_W * OVERLAY_H, 0U);
		_gpu_debug_overlay_valid = false;
	}
	else if (!_gpu_debug_overlay_valid || _overlay_frame % 15U == 1U)
	{
		int32_t analytics_error = RuntimeAnalytics::begin_scope(
			RuntimeAnalyticsScope::GPU_OVERLAY);
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr, "Analytics: overlay scope start failed (%d)\n",
				analytics_error);
		std::fill_n(_debug_overlay_buf, OVERLAY_W * OVERLAY_H, 0U);
		DebugOverlayRenderer::draw_debug_overlay(_debug_overlay_fb, debug);
		_gpu_debug_overlay_valid = true;
		debug_overlay_changed = true;
		analytics_error = RuntimeAnalytics::end_scope();
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr, "Analytics: overlay scope end failed (%d)\n",
				analytics_error);
	}
	std::copy(_debug_overlay_buf,
		_debug_overlay_buf + OVERLAY_W * OVERLAY_H, _overlay_buf);
	const bool items_visible = draw_world_item_markers(_overlay_fb,
		camera, world);
	if (debug_overlay_changed || items_visible || _gpu_item_overlay_visible)
	{
		this->gpu.upload_overlay(_overlay_buf, OVERLAY_W, OVERLAY_H);
		this->gpu.draw_overlay();
	}
	_gpu_item_overlay_visible = items_visible;
}

bool VoxelRenderer::draw_world_item_markers(ft_render_framebuffer &framebuffer,
	const Camera &camera, const World &world) const
{
	const WorldItemStore *items = world.item_store();
	std::vector<WorldItemSnapshot> visible_items;
	if (framebuffer.pixels == nullptr || framebuffer.width <= 0
		|| framebuffer.height <= 0 || items == nullptr
		|| items->query_renderable(camera.x, camera.y, camera.z, 32.0,
			&visible_items) != FT_ERR_SUCCESS)
		return (false);
	if (visible_items.empty())
		return (false);
	std::sort(visible_items.begin(), visible_items.end(),
		[&camera](const WorldItemSnapshot &left,
			const WorldItemSnapshot &right)
		{
			const double left_x = left.x - camera.x;
			const double left_y = left.y - camera.y;
			const double left_z = left.z - camera.z;
			const double right_x = right.x - camera.x;
			const double right_y = right.y - camera.y;
			const double right_z = right.z - camera.z;
			const double left_distance = left_x * left_x + left_y * left_y
				+ left_z * left_z;
			const double right_distance = right_x * right_x
				+ right_y * right_y + right_z * right_z;
			return (left_distance < right_distance
				|| (left_distance == right_distance
					&& left.entity_id < right.entity_id));
		});
	const std::size_t marker_count = std::min<std::size_t>(
		visible_items.size(), 48U);
	static const double tangent_half_fov = 0.83909963117728;
	double forward_x = 0.0;
	double forward_y = 0.0;
	double forward_z = 0.0;
	PlayerGeometry::camera_forward(camera, &forward_x, &forward_y, &forward_z);
	const double right_x = std::cos(camera.yaw);
	const double right_z = -std::sin(camera.yaw);
	const double up_x = -forward_y * std::sin(camera.yaw);
	const double up_y = std::cos(camera.pitch);
	const double up_z = -forward_y * std::cos(camera.yaw);
	const double focal_length = static_cast<double>(framebuffer.width)
		/ (2.0 * tangent_half_fov);
	bool rendered = false;
	for (std::size_t index = 0U; index < marker_count; ++index)
	{
		const WorldItemSnapshot &item = visible_items[index];
		const double offset_x = item.x - camera.x;
		const double offset_y = item.y + 0.12 - camera.y;
		const double offset_z = item.z - camera.z;
		const double depth = offset_x * forward_x + offset_y * forward_y
			+ offset_z * forward_z;
		if (depth < 0.25 || depth > 32.0)
			continue ;
		const double distance = std::sqrt(offset_x * offset_x
			+ offset_y * offset_y + offset_z * offset_z);
		if (distance > 0.45)
		{
			int32_t block_x = 0;
			int32_t block_y = 0;
			int32_t block_z = 0;
			const int32_t ray_result = world.raycast_solid(camera.x, camera.y,
				camera.z, offset_x / distance, offset_y / distance,
				offset_z / distance, distance - 0.3, &block_x, &block_y,
				&block_z);
			if (ray_result == FT_ERR_SUCCESS
				|| (ray_result != FT_ERR_INVALID_ARGUMENT
					&& ray_result != FT_ERR_NOT_FOUND))
				continue ;
		}
		const double right_projection = offset_x * right_x
			+ offset_z * right_z;
		const double up_projection = offset_x * up_x
			+ offset_y * up_y + offset_z * up_z;
		const int32_t screen_x = static_cast<int32_t>(
			static_cast<double>(framebuffer.width) * 0.5
			+ right_projection * focal_length / depth);
		const int32_t screen_y = static_cast<int32_t>(
			static_cast<double>(framebuffer.height) * 0.5
			- up_projection * focal_length / depth);
		const int32_t radius = std::max(2, std::min(5,
			static_cast<int32_t>(7.0 / std::sqrt(depth))));
		if (screen_x + radius < 0 || screen_y + radius < 0
			|| screen_x - radius >= framebuffer.width
			|| screen_y - radius >= framebuffer.height)
			continue ;
		const uint32_t hash = static_cast<uint32_t>(item.item_id) * 2654435761U;
		const uint32_t color = RendererColor::rgba(
			static_cast<uint8_t>(75U + (hash & 0x9FU)),
			static_cast<uint8_t>(75U + ((hash >> 8U) & 0x9FU)),
			static_cast<uint8_t>(75U + ((hash >> 16U) & 0x9FU)));
		for (int32_t y = screen_y - radius; y <= screen_y + radius; ++y)
		{
			if (y < 0 || y >= framebuffer.height)
				continue ;
			for (int32_t x = screen_x - radius; x <= screen_x + radius; ++x)
			{
				if (x < 0 || x >= framebuffer.width)
					continue ;
				const bool border = x == screen_x - radius
					|| x == screen_x + radius || y == screen_y - radius
					|| y == screen_y + radius;
				framebuffer.pixels[static_cast<std::size_t>(y)
					* static_cast<std::size_t>(framebuffer.width)
					+ static_cast<std::size_t>(x)] = border
					? RendererColor::rgba(20U, 20U, 20U) : color;
			}
		}
		rendered = true;
	}
	return (rendered);
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
	render_gpu_overlay(debug, camera, world);
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
	std::size_t software_timed_chunks;
	std::chrono::steady_clock::time_point chunk_start;
	uint64_t chunk_us;
	ft_bool collect_software_detail;
	ft_bool timed_chunk;
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
	collect_software_detail = RuntimeAnalytics::is_enabled()
		&& this->_software_diagnostic_frame % 120U == 0U
		? FT_TRUE : FT_FALSE;
	software_candidates = 0U;
	software_visible_chunks = 0U;
	software_visible_triangles = 0U;
	software_slowest_chunk_us = 0U;
	software_slowest_chunk_x = 0;
	software_slowest_chunk_z = 0;
	software_timed_chunks = 0U;
	#endif
	for (int32_t visible_index = 0; visible_index
		< static_cast<int32_t>(_software_visible_chunk_slots.size());
		visible_index++)
	{
		const int32_t i = _software_visible_chunk_slots[visible_index];
		#if defined(LIBFT_ENABLE_ANALYTICS)
		timed_chunk = FT_FALSE;
		if (collect_software_detail != FT_FALSE
			&& software_timed_chunks < 16U)
		{
			software_candidates += 1U;
			software_timed_chunks += 1U;
			timed_chunk = FT_TRUE;
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
		if (timed_chunk != FT_FALSE)
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
	(void)this->draw_world_item_markers(framebuffer, camera, world);
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
