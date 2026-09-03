#include "../../src/gpur/GpuGeometryBatch.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"

#if defined(LIBFT_ENABLE_ANALYTICS)
# include <cstdio>
# include <chrono>
#endif

namespace
{
	/* Streaming can finish several chunks between render frames.  A two-mesh
	 * budget allowed the pending list to grow faster than it drained, which
	 * made generated chunks appear only after an edit caused that chunk to be
	 * selected again.  Keep a bounded budget, but make normal catch-up faster. */
	static const int32_t MAX_MESH_UPLOADS_PER_FRAME = 4;
	static const size_t MAX_MESH_UPLOAD_BYTES_PER_FRAME = 8U * 1024U * 1024U;

	static bool visibility_camera_value_matches(double left, double right)
	{
		return (std::abs(left - right) <= 1.0e-9);
	}
}

GpuGeometryBatch::GpuGeometryBatch() : _water(),
	_visibility_cache_valid(false), _visibility_geometry_signature(0U),
	_visibility_camera_x(0.0), _visibility_camera_y(0.0),
	_visibility_camera_z(0.0), _visibility_camera_yaw(0.0),
	_visibility_camera_pitch(0.0), _visibility_width(0), _visibility_height(0),
	_visibility_render_distance(0), _visibility_loaded_chunk_count(-1),
	_visibility_center_chunk_x(0), _visibility_center_chunk_z(0),
	_visibility_chunk_index_valid(false),
#if defined(LIBFT_ENABLE_ANALYTICS)
	_analytics_collect_frame(0U),
	_analytics_collect_diagnostics(false),
#endif
	_upload_cursor(0), _visible_upload_cursor_slot(-1),
	_visible_upload_cursor_chunk_x(0), _visible_upload_cursor_chunk_z(0)
{
}

GpuGeometryBatch::GpuGeometryBatch(const GpuGeometryBatch &other) : _water(),
	_visibility_cache_valid(false), _visibility_geometry_signature(0U),
	_visibility_camera_x(0.0), _visibility_camera_y(0.0),
	_visibility_camera_z(0.0), _visibility_camera_yaw(0.0),
	_visibility_camera_pitch(0.0), _visibility_width(0), _visibility_height(0),
	_visibility_render_distance(0), _visibility_loaded_chunk_count(-1),
	_visibility_center_chunk_x(0), _visibility_center_chunk_z(0),
	_visibility_chunk_index_valid(false),
#if defined(LIBFT_ENABLE_ANALYTICS)
	_analytics_collect_frame(0U),
	_analytics_collect_diagnostics(false),
#endif
	_upload_cursor(0), _visible_upload_cursor_slot(-1),
	_visible_upload_cursor_chunk_x(0), _visible_upload_cursor_chunk_z(0)
{
	(void)other;
}

GpuGeometryBatch::~GpuGeometryBatch()
{
	destroy();
}

GpuGeometryBatch &GpuGeometryBatch::operator=(const GpuGeometryBatch &other)
{
	(void)other;
	return (*this);
}

bool GpuGeometryBatch::initialize()
{
	destroy();
	return (true);
}

void GpuGeometryBatch::sync_pending_visible_meshes(const Camera &camera,
	const World &world,
	int32_t &uploaded_count, size_t &uploaded_bytes)
{
	const int32_t visible_slot_count = static_cast<int32_t>(
		_visible_chunk_slots.size());
	int32_t visible_start = 0;
	int32_t cursor_index = -1;
	int32_t cursor_slot;
	int32_t nearest_pending_index = -1;
	double nearest_pending_distance = 0.0;
	if (visible_slot_count > 0)
	{
		cursor_index = 0;
		while (cursor_index < visible_slot_count)
		{
			cursor_slot = _visible_chunk_slots[cursor_index];
			if (cursor_slot >= 0 && cursor_slot < world.chunk_count
				&& cursor_slot == _visible_upload_cursor_slot
				&& world.chunks[cursor_slot].chunk_x
					== _visible_upload_cursor_chunk_x
				&& world.chunks[cursor_slot].chunk_z
					== _visible_upload_cursor_chunk_z)
				break ;
			cursor_index += 1;
		}
		if (cursor_index < visible_slot_count)
			visible_start = (cursor_index + 1) % visible_slot_count;
		else
		{
			int32_t nearest_index = -1;
			double nearest_distance = 0.0;
			int32_t index = 0;
			while (index < visible_slot_count)
			{
				const int32_t slot = _visible_chunk_slots[index];
				if (slot >= 0 && slot < world.chunk_count)
				{
					const WorldChunk &chunk = world.chunks[slot];
					const double dx = static_cast<double>(chunk.world_x)
						+ (static_cast<double>(GAME_VOXEL_CHUNK_WIDTH) * 0.5)
						- camera.x;
					const double dz = static_cast<double>(chunk.world_z)
						+ (static_cast<double>(GAME_VOXEL_CHUNK_DEPTH) * 0.5)
						- camera.z;
					const double distance = dx * dx + dz * dz;
					if (nearest_index < 0 || distance < nearest_distance)
					{
						nearest_index = index;
						nearest_distance = distance;
					}
				}
				index += 1;
			}
			if (nearest_index >= 0)
				visible_start = nearest_index;
		}
		/* A newly streamed chunk is useful immediately around the camera. Find
		 * the nearest pending identity first, then let the normal circular walk
		 * preserve fairness for the rest of the visible set. */
		int32_t pending_index = 0;
		while (pending_index < visible_slot_count)
		{
			const int32_t pending_slot = _visible_chunk_slots[pending_index];
			if (pending_slot >= 0 && pending_slot < world.chunk_count)
			{
				const WorldChunk &pending_chunk = world.chunks[pending_slot];
				if (pending_chunk.initialized
					&& pending_chunk.mesh.has_occupied_bounds == FT_TRUE
					&& _chunk_meshes[pending_slot].needs_sync(
						pending_chunk.mesh_revision, pending_chunk.chunk_x,
						pending_chunk.chunk_z, pending_chunk.voxel_revision))
				{
					const double dx = static_cast<double>(pending_chunk.world_x)
						+ (static_cast<double>(GAME_VOXEL_CHUNK_WIDTH) * 0.5)
						- camera.x;
					const double dz = static_cast<double>(pending_chunk.world_z)
						+ (static_cast<double>(GAME_VOXEL_CHUNK_DEPTH) * 0.5)
						- camera.z;
					const double pending_distance = dx * dx + dz * dz;
					if (nearest_pending_index < 0
						|| pending_distance < nearest_pending_distance)
					{
						nearest_pending_index = pending_index;
						nearest_pending_distance = pending_distance;
					}
				}
			}
			pending_index += 1;
		}
		if (nearest_pending_index >= 0)
			visible_start = nearest_pending_index;
	}
	int32_t visible_offset = 0;

	uploaded_count = 0;
	uploaded_bytes = 0U;
	while (visible_offset < visible_slot_count)
	{
		const int32_t visible_index = (visible_start + visible_offset)
			% visible_slot_count;
		const int32_t slot = _visible_chunk_slots[visible_index];
		if (slot < 0 || slot >= world.chunk_count)
		{
			_visibility_cache_valid = false;
		}
		else
		{
			const WorldChunk &chunk = world.chunks[slot];
			if (!chunk.initialized
				|| chunk.mesh.has_occupied_bounds == FT_FALSE)
			{
				_chunk_meshes[slot].invalidate();
			}
			else if (_chunk_meshes[slot].needs_sync(chunk.mesh_revision,
				chunk.chunk_x, chunk.chunk_z, chunk.voxel_revision))
			{
				/* A storage slot can be reused after recentering. Do not draw the
				 * previous chunk's GPU geometry at the new chunk's coordinates while
				 * the replacement upload is waiting for its frame budget. */
				_chunk_meshes[slot].invalidate();
				const size_t mesh_bytes = chunk.mesh.vertices.size()
					* sizeof(chunk_mesh_vertex)
					+ chunk.mesh.solid_indices.size() * sizeof(uint32_t)
					+ chunk.mesh.water_indices.size() * sizeof(uint32_t);
				if (uploaded_count < MAX_MESH_UPLOADS_PER_FRAME
					&& (uploaded_count == 0
						|| uploaded_bytes + mesh_bytes
							<= MAX_MESH_UPLOAD_BYTES_PER_FRAME))
				{
#if defined(LIBFT_ENABLE_ANALYTICS)
					int32_t analytics_error = RuntimeAnalytics::begin_scope(
						RuntimeAnalyticsScope::GPU_MESH_UPLOAD);
					if (analytics_error != FT_ERR_SUCCESS)
						std::fprintf(stderr,
							"Analytics: scheduled mesh upload scope start failed (%d)\n",
							analytics_error);
					const auto upload_start = std::chrono::steady_clock::now();
#endif
					_chunk_meshes[slot].sync(chunk.mesh, chunk.mesh_revision,
						chunk.chunk_x, chunk.chunk_z, chunk.voxel_revision);
					uploaded_count += 1;
					uploaded_bytes += mesh_bytes;
#if defined(LIBFT_ENABLE_ANALYTICS)
					analytics_error = RuntimeAnalytics::end_scope();
					if (analytics_error != FT_ERR_SUCCESS)
						std::fprintf(stderr,
							"Analytics: scheduled mesh upload scope end failed (%d)\n",
							analytics_error);
					const uint64_t upload_us = static_cast<uint64_t>(
						std::chrono::duration_cast<std::chrono::microseconds>(
							std::chrono::steady_clock::now() - upload_start).count());
					if (upload_us >= 8000U)
						std::fprintf(stderr,
							"[Analytics][Render] slow scheduled mesh upload slot=%d "
							"chunk=(%d,%d) bytes=%zu duration_us=%llu\n", slot,
							chunk.chunk_x, chunk.chunk_z, mesh_bytes,
							static_cast<unsigned long long>(upload_us));
#endif
				}
			}
		}
		visible_offset += 1;
	}
	if (visible_slot_count > 0)
	{
		_visible_upload_cursor_slot = _visible_chunk_slots[
			(visible_start + visible_offset - 1 + visible_slot_count)
			% visible_slot_count];
		if (_visible_upload_cursor_slot >= 0
			&& _visible_upload_cursor_slot < world.chunk_count)
		{
			_visible_upload_cursor_chunk_x = world.chunks[
				_visible_upload_cursor_slot].chunk_x;
			_visible_upload_cursor_chunk_z = world.chunks[
				_visible_upload_cursor_slot].chunk_z;
		}
	}
}

void GpuGeometryBatch::destroy()
{
	_water.destroy();
	_visibility_cache_valid = false;
	_visible_chunk_slots.clear();
	_upload_cursor = 0;
	_visible_upload_cursor_slot = -1;
	_visible_upload_cursor_chunk_x = 0;
	_visible_upload_cursor_chunk_z = 0;
	for (int32_t index = 0; index < WorldCoordinates::CHUNK_COUNT; ++index)
		_chunk_meshes[index].destroy();
}

void GpuGeometryBatch::collect(const Camera &camera, const World &world,
	int width, int height)
{
	RenderCache	cache;
	int32_t		uploaded_count;
	int32_t		scanned_count;
	int32_t		scan_index;
	int32_t		scan_limit;
	int32_t		slot;
	int32_t		chunk_count;
	const WorldChunk	*scan_chunk;
	bool			visible;
#if defined(LIBFT_ENABLE_ANALYTICS)
	int32_t		analytics_error;
	const bool collect_diagnostics = (++_analytics_collect_frame % 120U == 0U);
	static bool startup_collect_reported = false;
	_analytics_collect_diagnostics = false;
#endif
#if defined(LIBFT_ENABLE_ANALYTICS)
	if (collect_diagnostics)
		_analytics_collect_diagnostics = true;
#endif
	chunk_count = world.chunk_count;
	if (chunk_count < 0)
		chunk_count = 0;
	if (chunk_count > WorldCoordinates::CHUNK_COUNT)
		chunk_count = WorldCoordinates::CHUNK_COUNT;
	if (_visibility_cache_valid
		&& _visibility_geometry_signature == world.geometry_revision
		&& visibility_camera_value_matches(_visibility_camera_x, camera.x)
		&& visibility_camera_value_matches(_visibility_camera_y, camera.y)
		&& visibility_camera_value_matches(_visibility_camera_z, camera.z)
		&& visibility_camera_value_matches(_visibility_camera_yaw, camera.yaw)
		&& visibility_camera_value_matches(_visibility_camera_pitch, camera.pitch)
		&& _visibility_width == width && _visibility_height == height
		&& _visibility_render_distance == world.active_render_distance
		&& _visibility_loaded_chunk_count == world.loaded_chunk_count
		&& _visibility_center_chunk_x == world.center_chunk_x
		&& _visibility_center_chunk_z == world.center_chunk_z
		&& _visibility_chunk_index_valid == world.chunk_index_valid)
	{
#if defined(LIBFT_ENABLE_ANALYTICS)
		analytics_error = RuntimeAnalytics::begin_scope(
			RuntimeAnalyticsScope::GPU_BATCH_CACHE_SYNC);
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr,
				"Analytics: batch cache-sync scope start failed (%d)\n",
				analytics_error);
#endif
		/* Visibility is unchanged, but a generated/remeshed chunk may still
		 * need its GPU representation uploaded. Keep this work bounded by both
		 * upload count and transfer size so a large mesh cannot consume the
		 * entire frame budget. Always admit the first pending upload so a newly
		 * visible world can make progress even when that mesh is large. */
		size_t uploaded_bytes = 0U;
		sync_pending_visible_meshes(camera, world, uploaded_count,
			uploaded_bytes);
		if (_visibility_cache_valid)
		{
#if defined(LIBFT_ENABLE_ANALYTICS)
			if (collect_diagnostics)
				std::fprintf(stderr,
					"[Analytics][Render] cache sample center=(%d,%d) "
					"loaded=%d visible=%zu\n", world.center_chunk_x,
					world.center_chunk_z, world.loaded_chunk_count,
					_visible_chunk_slots.size());
#endif
#if defined(LIBFT_ENABLE_ANALYTICS)
			analytics_error = RuntimeAnalytics::end_scope();
			if (analytics_error != FT_ERR_SUCCESS)
				std::fprintf(stderr,
					"Analytics: batch cache-sync scope end failed (%d)\n",
					analytics_error);
#endif
			return ;
		}
#if defined(LIBFT_ENABLE_ANALYTICS)
		analytics_error = RuntimeAnalytics::end_scope();
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr,
				"Analytics: batch cache-sync scope end failed (%d)\n",
				analytics_error);
#endif
	}
#if defined(LIBFT_ENABLE_ANALYTICS)
	const auto collect_start = collect_diagnostics
		? std::chrono::steady_clock::now()
		: std::chrono::steady_clock::time_point();
	uint64_t slowest_cull_us = 0U;
	int32_t slowest_cull_slot = -1;
	int32_t visible_count = 0;
	int32_t pending_upload_count = 0;
	int32_t deferred_visible_count = 0;
	int32_t deferred_visible_reported = 0;
	int32_t nearby_culled_count = 0;
	int32_t nearby_culled_reported = 0;
#endif

	cache.configure(camera, width, height, world.active_render_distance);
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (!startup_collect_reported)
		std::fprintf(stderr, "[Startup] GPU batch: visibility scan begin\n");
#endif
#if defined(LIBFT_ENABLE_ANALYTICS)
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::GPU_BATCH_VISIBILITY_SCAN);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"Analytics: batch visibility scope start failed (%d)\n",
			analytics_error);
#endif
	_visible_chunk_slots.clear();
	scanned_count = 0;
	/* The spatial index contains only loaded chunks. Fall back to the storage
	 * array if it has not been built yet, which keeps rendering safe during
	 * early initialization and teardown. */
	/* The spatial index is an acceleration aid, not the authoritative list of
	 * drawable chunks.  A streamed slot is published before every index update
	 * is necessarily observable by the renderer.  Scan storage here so a newly
	 * generated chunk cannot remain invisible until an unrelated block edit
	 * happens to rebuild the index.  The bounded cache is only 625 slots, so the
	 * correctness guarantee is preferable to trusting a transient index miss. */
	scan_limit = chunk_count;
	for (scan_index = 0; scan_index < scan_limit; ++scan_index)
	{
		scan_chunk = &world.chunks[(_upload_cursor + scan_index)
			% chunk_count];
		if (scan_chunk == nullptr)
		{
			scanned_count += 1;
			continue ;
		}
		slot = static_cast<int32_t>(scan_chunk - world.chunks);
		if (slot < 0 || slot >= chunk_count)
		{
			scanned_count += 1;
			continue ;
		}
		const WorldChunk &wc = *scan_chunk;
		if (!wc.initialized || wc.mesh.has_occupied_bounds == FT_FALSE)
		{
			_chunk_meshes[slot].invalidate();
			scanned_count += 1;
			continue ;
		}
		{
#if defined(LIBFT_ENABLE_ANALYTICS)
			const bool sample_cull = collect_diagnostics
				&& (scanned_count & 31) == 0;
			const auto cull_start = sample_cull
				? std::chrono::steady_clock::now()
				: std::chrono::steady_clock::time_point();
#endif
			visible = MeshCuller::chunk_is_visible(camera, wc, cache);
#if defined(LIBFT_ENABLE_ANALYTICS)
			if (sample_cull)
			{
				const uint64_t cull_us = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::microseconds>(
						std::chrono::steady_clock::now() - cull_start).count());
				if (cull_us > slowest_cull_us)
				{
					slowest_cull_us = cull_us;
					slowest_cull_slot = slot;
				}
			}
#endif
		}
		if (visible)
		{
#if defined(LIBFT_ENABLE_ANALYTICS)
			visible_count += 1;
#endif
#if defined(LIBFT_ENABLE_ANALYTICS)
			if (_chunk_meshes[slot].needs_sync(wc.mesh_revision,
				wc.chunk_x, wc.chunk_z, wc.voxel_revision))
				pending_upload_count += 1;
#endif
#if defined(LIBFT_ENABLE_ANALYTICS)
		if (collect_diagnostics && (wc.world_x != wc.chunk_x * GAME_VOXEL_CHUNK_WIDTH
				|| wc.world_z != wc.chunk_z * GAME_VOXEL_CHUNK_DEPTH)
			)
				std::fprintf(stderr,
					"[Analytics][Render] chunk offset mismatch slot=%d chunk=(%d,%d) world=(%d,%d)\n",
					slot, wc.chunk_x, wc.chunk_z, wc.world_x, wc.world_z);
		if (collect_diagnostics
			&& !_chunk_meshes[slot].needs_sync(wc.mesh_revision, wc.chunk_x,
				wc.chunk_z, wc.voxel_revision)
			&& !_chunk_meshes[slot].diagnostics_identity_matches(wc.mesh_revision,
				wc.chunk_x, wc.chunk_z, wc.voxel_revision))
			std::fprintf(stderr,
				"[Analytics][Render] uploaded identity mismatch slot=%d chunk=(%d,%d) voxel=%llu mesh=%llu\n",
					slot, wc.chunk_x, wc.chunk_z,
					static_cast<unsigned long long>(wc.voxel_revision),
					static_cast<unsigned long long>(wc.mesh_revision));
		if (collect_diagnostics
			&& !_chunk_meshes[slot].needs_sync(wc.mesh_revision, wc.chunk_x,
				wc.chunk_z, wc.voxel_revision)
			&& wc.mesh.has_occupied_bounds
				&& (_chunk_meshes[slot].diagnostics_min_y()
						!= wc.mesh.occupied_bounds.minimum_y
					|| _chunk_meshes[slot].diagnostics_max_y()
						!= wc.mesh.occupied_bounds.maximum_y))
				std::fprintf(stderr,
					"[Analytics][Render] uploaded Y mismatch slot=%d mesh=(%d,%d) uploaded=(%d,%d)\n",
					slot, wc.mesh.occupied_bounds.minimum_y,
					wc.mesh.occupied_bounds.maximum_y,
					_chunk_meshes[slot].diagnostics_min_y(),
					_chunk_meshes[slot].diagnostics_max_y());
#endif
			/* Keep visible chunks in the cached list even when their upload was
			 * deferred by the frame budget. The cache-sync path must be able to
			 * revisit those chunks on the next frame; otherwise a cache miss can
			 * permanently hide part of the world until the camera moves again. */
			if (wc.mesh.has_occupied_bounds == FT_TRUE)
			{
				_chunk_world_x[slot] = wc.world_x;
				_chunk_world_z[slot] = wc.world_z;
				_visible_chunk_slots.push_back(slot);
			}
		}
		else
		{
#if defined(LIBFT_ENABLE_ANALYTICS)
			if (collect_diagnostics)
			{
				const double dx = static_cast<double>(wc.world_x)
					+ (static_cast<double>(GAME_VOXEL_CHUNK_WIDTH) * 0.5)
					- camera.x;
				const double dz = static_cast<double>(wc.world_z)
					+ (static_cast<double>(GAME_VOXEL_CHUNK_DEPTH) * 0.5)
					- camera.z;
				const double nearby_distance = static_cast<double>(
					world.active_render_distance)
					+ static_cast<double>(GAME_VOXEL_CHUNK_WIDTH);
				if (dx * dx + dz * dz <= nearby_distance * nearby_distance)
				{
					nearby_culled_count += 1;
					if (nearby_culled_reported < 4)
					{
						std::fprintf(stderr,
							"[Analytics][Render] nearby chunk rejected by culling "
							"slot=%d chunk=(%d,%d) world=(%d,%d)\n", slot,
							wc.chunk_x, wc.chunk_z, wc.world_x, wc.world_z);
						nearby_culled_reported += 1;
					}
				}
			}
#endif
		}
		scanned_count += 1;
	}
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (!startup_collect_reported)
		std::fprintf(stderr, "[Startup] GPU batch: visibility scan end listed=%zu\n",
			_visible_chunk_slots.size());
#endif
	if (chunk_count > 0)
		_upload_cursor = (_upload_cursor + 1) % chunk_count;
	size_t uploaded_bytes = 0U;
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (!startup_collect_reported)
		std::fprintf(stderr, "[Startup] GPU batch: mesh sync begin\n");
	#endif
	sync_pending_visible_meshes(camera, world, uploaded_count, uploaded_bytes);
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (!startup_collect_reported)
		std::fprintf(stderr, "[Startup] GPU batch: mesh sync end uploads=%d\n",
			uploaded_count);
#endif
#if defined(LIBFT_ENABLE_ANALYTICS)
	if (collect_diagnostics)
	{
		int32_t deferred_index = 0;
		while (deferred_index
			< static_cast<int32_t>(_visible_chunk_slots.size()))
		{
			const int32_t deferred_slot =
				_visible_chunk_slots[deferred_index];
			if (deferred_slot >= 0 && deferred_slot < world.chunk_count)
			{
				const WorldChunk &deferred_chunk = world.chunks[deferred_slot];
				if (deferred_chunk.initialized
					&& _chunk_meshes[deferred_slot].needs_sync(
						deferred_chunk.mesh_revision, deferred_chunk.chunk_x,
						deferred_chunk.chunk_z, deferred_chunk.voxel_revision))
				{
					deferred_visible_count += 1;
					if (deferred_visible_reported < 4)
					{
						std::fprintf(stderr,
							"[Analytics][Render] deferred visible chunk slot=%d "
							"chunk=(%d,%d) voxel=%llu mesh=%llu vertices=%zu "
							"solid=%zu water=%zu\n", deferred_slot,
							deferred_chunk.chunk_x, deferred_chunk.chunk_z,
							static_cast<unsigned long long>(
								deferred_chunk.voxel_revision),
							static_cast<unsigned long long>(
								deferred_chunk.mesh_revision),
							deferred_chunk.mesh.vertices.size(),
							deferred_chunk.mesh.solid_indices.size(),
							deferred_chunk.mesh.water_indices.size());
						deferred_visible_reported += 1;
					}
				}
			}
			deferred_index += 1;
		}
	}
#endif
	_visibility_geometry_signature = world.geometry_revision;
	_visibility_camera_x = camera.x;
	_visibility_camera_y = camera.y;
	_visibility_camera_z = camera.z;
	_visibility_camera_yaw = camera.yaw;
	_visibility_camera_pitch = camera.pitch;
	_visibility_width = width;
	_visibility_height = height;
	_visibility_render_distance = world.active_render_distance;
	_visibility_loaded_chunk_count = world.loaded_chunk_count;
	_visibility_center_chunk_x = world.center_chunk_x;
	_visibility_center_chunk_z = world.center_chunk_z;
	_visibility_chunk_index_valid = world.chunk_index_valid;
	_visibility_cache_valid = true;
#if defined(LIBFT_ENABLE_ANALYTICS)
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr,
			"Analytics: batch visibility scope end failed (%d)\n",
			analytics_error);
#endif
#if defined(LIBFT_ENABLE_ANALYTICS)
	uint64_t collect_us = 0U;
	if (collect_diagnostics)
		collect_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - collect_start).count());
	if (collect_diagnostics && collect_us >= 8000U)
		std::fprintf(stderr,
			"[Analytics][Render] slow collect storage=%d candidates=%d "
			"visible=%d listed=%zu uploads=%d upload_bytes=%zu "
			"slowest_cull_slot=%d "
			"slowest_cull_us=%llu duration_us=%llu\n", chunk_count,
			scanned_count, visible_count, _visible_chunk_slots.size(),
			uploaded_count, uploaded_bytes, slowest_cull_slot,
			static_cast<unsigned long long>(slowest_cull_us),
			static_cast<unsigned long long>(collect_us));
	if (collect_diagnostics)
		std::fprintf(stderr,
			"[Analytics][Render] collect sample center=(%d,%d) loaded=%d "
			"distance=%d radius=%d "
			"index_valid=%d storage_scan=1 visited=%d visible=%d listed=%zu pending_uploads=%d "
			"uploads=%d upload_bytes=%zu deferred_visible=%d "
			"nearby_culled=%d "
			"duration_us=%llu\n",
			world.center_chunk_x, world.center_chunk_z,
			world.loaded_chunk_count, world.active_render_distance,
			WorldCoordinates::render_distance_to_chunk_radius(
				world.active_render_distance),
			world.chunk_index_valid ? 1 : 0,
			scanned_count, visible_count, _visible_chunk_slots.size(),
			pending_upload_count, uploaded_count, uploaded_bytes,
			deferred_visible_count,
			nearby_culled_count,
			static_cast<unsigned long long>(collect_us));
#endif
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	startup_collect_reported = true;
#endif
}

void GpuGeometryBatch::flush_solid(GLuint, GLint u_mvp, GLint u_chunk_offset,
	const float mvp[16], GpuTextureAtlas &atlas)
{
	float	chunk_offset[3];
#if defined(LIBFT_ENABLE_ANALYTICS)
	const auto flush_start = std::chrono::steady_clock::now();
	uint32_t draw_count = 0U;
#endif

	if (_visible_chunk_slots.empty())
		return ;
	glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
	atlas.bind(0);
	for (int32_t slot : _visible_chunk_slots)
	{
		chunk_offset[0] = static_cast<float>(_chunk_world_x[slot]);
		chunk_offset[1] = 0.0f;
		chunk_offset[2] = static_cast<float>(_chunk_world_z[slot]);
		glUniform3fv(u_chunk_offset, 1, chunk_offset);
		_chunk_meshes[slot].draw_solid();
#if defined(LIBFT_ENABLE_ANALYTICS)
		if (_chunk_meshes[slot].has_solid_geometry())
			draw_count += 1U;
#endif
	}
	glBindVertexArray(0);
#if defined(LIBFT_ENABLE_ANALYTICS)
	const uint64_t flush_us = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - flush_start).count());
	if (_analytics_collect_diagnostics || flush_us >= 8000U)
		std::fprintf(stderr,
			"[Analytics][Render] slow solid flush visible=%zu draws=%u "
			"duration_us=%llu\n", _visible_chunk_slots.size(), draw_count,
			static_cast<unsigned long long>(flush_us));
#endif
}

void GpuGeometryBatch::flush_water(GLuint, GLint u_mvp, GLint u_chunk_offset,
	const float mvp[16], GpuTextureAtlas &atlas)
{
#if defined(LIBFT_ENABLE_ANALYTICS)
	const auto flush_start = std::chrono::steady_clock::now();
	uint32_t draw_count = 0U;
	for (size_t index = 0; index < _visible_chunk_slots.size(); ++index)
	{
		if (_chunk_meshes[_visible_chunk_slots[index]].has_water_geometry())
			draw_count += 1U;
	}
#endif
	_water.flush(u_mvp, u_chunk_offset, mvp, atlas, _visible_chunk_slots,
		_chunk_meshes, _chunk_world_x, _chunk_world_z);
#if defined(LIBFT_ENABLE_ANALYTICS)
	const uint64_t flush_us = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - flush_start).count());
	if (_analytics_collect_diagnostics || flush_us >= 8000U)
		std::fprintf(stderr,
			"[Analytics][Render] slow water flush visible=%zu draws=%u "
			"duration_us=%llu\n", _visible_chunk_slots.size(), draw_count,
			static_cast<unsigned long long>(flush_us));
#endif
}

size_t GpuGeometryBatch::gpu_bytes() const
{
	size_t bytes = 0U;
	for (int32_t index = 0; index < WorldCoordinates::CHUNK_COUNT; ++index)
		bytes += _chunk_meshes[index].gpu_bytes();
	return (bytes);
}
