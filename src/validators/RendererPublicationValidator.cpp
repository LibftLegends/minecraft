#include "../../src/validators/RendererPublicationValidator.hpp"
#include "../../src/camera/Camera.hpp"
#include "../../src/diagnostics/ApplicationError.hpp"
#include "../../src/font/FontRenderer.hpp"
#include "../../src/player/PlayerController.hpp"
#include "../../src/platform/ApplicationWindow.hpp"
#include "../../src/frame/RendererColor.hpp"
#include "../../src/render/VoxelRenderer.hpp"
#include "../../src/world/World.hpp"
#include "../../src/world/WorldGenerationResultCommitter.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <thread>

namespace
{
	/* GPU upload includes driver synchronization and is sampled once here;
	 * keep this below a visible hitch while leaving the repeated block-edit
	 * percentile gate responsible for the tighter CPU-side budget. */
	static const uint64_t RENDERER_EDIT_PUBLICATION_MAX_LATENCY_MS = 500U;

	static bool cached_debug_overlay_is_drawn(ApplicationWindow &window,
		VoxelRenderer &renderer, const World &world)
	{
		const int width = 128;
		const int height = 28;
		const int y = window.get_gpu_window()->get_height() - height;
		uint8_t background[width * height * 4];
		uint8_t refreshed[width * height * 4];
		uint8_t cached[width * height * 4];
		Camera camera;
		RenderDebug debug;

		camera.initialize();
		camera.y = GAME_VOXEL_CHUNK_HEIGHT * 4.0;
		debug.fps = 60.0;
		if (!FontRenderer::instance().is_loaded())
			FontRenderer::instance().load(22.0f);
		renderer.render_world(window.framebuffer(), camera, world, nullptr);
		glReadPixels(0, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, background);
		renderer.render_world(window.framebuffer(), camera, world, &debug);
		glReadPixels(0, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, refreshed);
		renderer.render_world(window.framebuffer(), camera, world, &debug);
		glReadPixels(0, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, cached);
		return (std::memcmp(background, refreshed, sizeof(background)) != 0
			&& std::memcmp(refreshed, cached, sizeof(cached)) == 0);
	}

	static bool overlay_region_preserves_surrounding_pixels(
		ApplicationWindow &window, GpuRenderer &gpu, const World &world)
	{
		const int size = 48;
		uint32_t pixels[size * size] = {};
		uint8_t before[8] = {};
		uint8_t updated[8] = {};
		uint8_t restored[8] = {};
		Camera camera;

		camera.initialize();
		camera.y = GAME_VOXEL_CHUNK_HEIGHT * 4.0;
		/* The right pixel is outside the region and must survive both uploads. */
		pixels[26 * size + 24] = 0x0000FFU;
		gpu.upload_overlay(pixels, size, size);
		const auto sample_frame = [&](uint8_t *sample)
		{
			gpu.render(camera, world);
			gpu.draw_overlay();
			glReadPixels(23, window.get_gpu_window()->get_height() - 27,
				2, 1, GL_RGBA, GL_UNSIGNED_BYTE, sample);
		};
		sample_frame(before);
		pixels[26 * size + 23] = 0xFF0000U;
		gpu.upload_overlay_region(pixels, size, size, 21, 25, 3, 2);
		sample_frame(updated);
		pixels[26 * size + 23] = 0U;
		gpu.upload_overlay_region(pixels, size, size, 21, 25, 3, 2);
		sample_frame(restored);
		return (updated[0] == 255U && updated[1] == 0U && updated[2] == 0U
			&& updated[4] == 0U && updated[5] == 0U && updated[6] == 255U
			&& std::memcmp(before, restored, sizeof(before)) == 0);
	}

	static bool item_markers_draw_without_altering_overlay(ApplicationWindow &window,
		GpuRenderer &gpu)
	{
		const int size = 48;
		const int y = window.get_gpu_window()->get_height() - 27;
		uint32_t pixels[size * size] = {};
		uint8_t background[8] = {};
		uint8_t drawn[8] = {};
		uint8_t restored[8] = {};
		const GpuOverlayRenderer::ItemMarker marker{size - 1, 26, 2, 0xFF0000U};

		pixels[26 * size + size - 1] = 0x0000FFU;
		gpu.upload_overlay(pixels, size, size);
		gpu.clear_screen();
		gpu.draw_overlay();
		glReadPixels(size - 1, y, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE, background);
		gpu.draw_item_markers(&marker, 1U, size, size);
		glReadPixels(size - 1, y, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE, drawn);
		gpu.clear_screen();
		gpu.draw_overlay();
		gpu.draw_item_markers(nullptr, 0U, size, size);
		glReadPixels(size - 1, y, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE, restored);
		return (background[2] == 255U && drawn[0] == 255U && drawn[2] == 0U
			&& std::memcmp(background + 4, drawn + 4, 4U) == 0
			&& std::memcmp(background, restored, sizeof(background)) == 0);
	}

	static bool mesh_has_light(const chunk_mesh &mesh, uint8_t *minimum,
		uint8_t *maximum, std::size_t *nonzero) noexcept
	{
		std::size_t index;
		uint8_t min_value;
		uint8_t max_value;
		std::size_t nonzero_count;

		if (minimum == nullptr || maximum == nullptr || nonzero == nullptr)
			return (false);
		min_value = 255U;
		max_value = 0U;
		nonzero_count = 0U;
		index = 0U;
		while (index < mesh.vertices.size())
		{
			const uint8_t value = mesh.vertices[index].packed_light;

			if (value < min_value)
				min_value = value;
			if (value > max_value)
				max_value = value;
			if (value != 0U)
				nonzero_count += 1U;
			index += 1U;
		}
		if (mesh.vertices.empty())
			min_value = 0U;
		*minimum = min_value;
		*maximum = max_value;
		*nonzero = nonzero_count;
		return (nonzero_count != 0U);
	}

	static bool mesh_has_boundary_face(const WorldChunk &chunk, uint8_t face,
		uint16_t boundary_x, int32_t local_y, int32_t local_z,
		uint8_t *packed_light = nullptr) noexcept
	{
		for (std::size_t index = 0U; index + 3U < chunk.mesh.vertices.size();
			index += 4U)
		{
			const auto &first = chunk.mesh.vertices[index];
			if (first.face != face || first.coordinate_x != boundary_x)
				continue ;
			int32_t minimum_y = first.coordinate_y;
			int32_t maximum_y = minimum_y;
			int32_t minimum_z = first.coordinate_z;
			int32_t maximum_z = minimum_z;
			for (std::size_t corner = 1U; corner < 4U; ++corner)
			{
				const auto &vertex = chunk.mesh.vertices[index + corner];
				minimum_y = std::min<int32_t>(minimum_y, vertex.coordinate_y);
				maximum_y = std::max<int32_t>(maximum_y, vertex.coordinate_y);
				minimum_z = std::min<int32_t>(minimum_z, vertex.coordinate_z);
				maximum_z = std::max<int32_t>(maximum_z, vertex.coordinate_z);
			}
			if (local_y >= minimum_y && local_y < maximum_y
				&& local_z >= minimum_z && local_z < maximum_z)
			{
				if (packed_light != nullptr)
					*packed_light = first.packed_light;
				return (true);
			}
		}
		return (false);
	}

	static int validate_boundary_delete_publication(ApplicationWindow &window,
		VoxelRenderer &renderer, Camera &camera, World &world,
		bool hold_receiver) noexcept
	{
		int32_t target_x = 0;
		int32_t target_chunk_x = 0;
		int32_t neighbour_chunk_x = 0;
		int32_t world_z = 0;
		int32_t chunk_z = 0;
		const uint32_t air_block = GAME_VOXEL_AIR_BLOCK;
		WorldChunk *target_chunk;
		WorldChunk *neighbour_chunk;
		uint32_t target_block = air_block;
		uint32_t neighbour_block = air_block;
		int32_t unrelated_chunk_z = 0;
		int32_t edit_y = -1;
		int32_t slot_index;
		int32_t local_z;
		int32_t local_y;
		int32_t slot_target;
		int32_t slot_neighbour;
		uint64_t old_target_mesh;
		uint64_t old_target_voxel;
		uint64_t old_neighbour_mesh;
		uint64_t old_neighbour_voxel;
		uint64_t deadline;
		int32_t error_code;

		/* The initial center upload is intentionally allowed before the whole
		 * startup ring exists.  Warm the stream a few bounded frames before
		 * selecting the boundary fixture; otherwise this validator races the
		 * spiral candidate order and reports a false "no boundary pair" failure
		 * when the east neighbor simply has not arrived yet. */
		for (int32_t warmup_frame = 0; warmup_frame < 8; ++warmup_frame)
		{
			error_code = world.update_around(camera.x, camera.z, 4,
				WorldCoordinates::MIN_RENDER_DISTANCE);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			renderer.render_world(window.framebuffer(), camera, world,
				static_cast<const RenderDebug *>(nullptr));
			if (window.poll_events() != FT_ERR_SUCCESS
				|| window.present() != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_OPERATION);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		deadline = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count())
			+ 30000U;
		while (edit_y < 0 && static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count())
			< deadline)
		{
			slot_index = 0;
			while (slot_index < world.chunk_count && edit_y < 0)
			{
				WorldChunk *candidate = &world.chunks[slot_index];
				WorldChunk *east = nullptr;

				if (candidate->initialized && candidate->light_buffer_is_valid())
				{
					const WorldChunk *unrelated = world.find_chunk(candidate->chunk_x,
						candidate->chunk_z - 1);
					if (unrelated == nullptr || !unrelated->initialized)
						unrelated = world.find_chunk(candidate->chunk_x,
							candidate->chunk_z + 1);
					east = world.find_chunk_mutable(candidate->chunk_x + 1,
						candidate->chunk_z);
					local_z = 1;
					while (east != nullptr && east->initialized
						&& unrelated != nullptr && unrelated->initialized
						&& local_z < GAME_VOXEL_CHUNK_DEPTH - 1 && edit_y < 0)
					{
						local_y = 0;
						while (local_y + 1 < GAME_VOXEL_CHUNK_HEIGHT && edit_y < 0)
						{
							if (candidate->chunk.read_block(
								GAME_VOXEL_CHUNK_WIDTH - 1, local_y, local_z,
								&target_block) == FT_ERR_SUCCESS
								&& east->chunk.read_block(0, local_y, local_z,
									&neighbour_block) == FT_ERR_SUCCESS
								&& voxel_block_is_breakable(target_block) != FT_FALSE
								&& voxel_block_occludes_faces(target_block) != FT_FALSE
								&& voxel_light_sky(candidate->light.get(
									GAME_VOXEL_CHUNK_WIDTH - 1, local_y + 1, local_z)) == 15U
								&& voxel_block_occludes_faces(neighbour_block)
									!= FT_FALSE
								&& local_y > 0
								&& candidate->chunk.read_block(GAME_VOXEL_CHUNK_WIDTH - 1,
									local_y - 1, local_z, &target_block) == FT_ERR_SUCCESS
								&& voxel_block_is_breakable(target_block) != FT_FALSE
								&& voxel_block_occludes_faces(target_block) != FT_FALSE
								&& east->chunk.read_block(0, local_y - 1, local_z,
									&neighbour_block) == FT_ERR_SUCCESS
								&& voxel_block_occludes_faces(neighbour_block)
									!= FT_FALSE)
							{
								target_chunk_x = candidate->chunk_x;
								neighbour_chunk_x = east->chunk_x;
								chunk_z = candidate->chunk_z;
								unrelated_chunk_z = unrelated->chunk_z;
								target_x = candidate->world_x
									+ GAME_VOXEL_CHUNK_WIDTH - 1;
								world_z = candidate->world_z + local_z;
								edit_y = local_y;
							}
							local_y += 1;
						}
						local_z += 1;
					}
				}
				slot_index += 1;
			}
			if (edit_y >= 0)
				break ;
			error_code = world.update_around(camera.x, camera.z, 4,
				WorldCoordinates::MIN_RENDER_DISTANCE);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			renderer.render_world(window.framebuffer(), camera, world,
				static_cast<const RenderDebug *>(nullptr));
			if (window.poll_events() != FT_ERR_SUCCESS
				|| window.present() != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_OPERATION);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		if (edit_y < 0)
		{
			World::StreamDiagnostics diagnostics = world.stream_diagnostics();
			std::fprintf(stderr,
				"renderer-publication: no skylit breakable boundary pair "
				"after fixture wait ready=%zu pending=%zu active=%zu remesh=%zu\n",
				diagnostics.ready_count, diagnostics.pending_count,
				diagnostics.active_generation_count,
				diagnostics.remesh_priority_queue_depth);
			return (1);
		}
		target_chunk = world.find_chunk_mutable(target_chunk_x, chunk_z);
		neighbour_chunk = world.find_chunk_mutable(neighbour_chunk_x, chunk_z);
		if (target_chunk == nullptr || neighbour_chunk == nullptr
			|| !target_chunk->initialized || !neighbour_chunk->initialized)
			return (1);
		slot_target = static_cast<int32_t>(target_chunk - world.chunks);
		slot_neighbour = static_cast<int32_t>(neighbour_chunk - world.chunks);
		const int32_t edit_local_z = world_z - neighbour_chunk->world_z;
		deadline = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count())
			+ 30000U;
		while (true)
		{
			error_code = world.update_around(camera.x, camera.z, 0,
				WorldCoordinates::MIN_RENDER_DISTANCE);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			renderer.render_world(window.framebuffer(), camera, world,
				static_cast<const RenderDebug *>(nullptr));
			if (window.poll_events() != FT_ERR_SUCCESS
				|| window.present() != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_OPERATION);
			target_chunk = world.find_chunk_mutable(target_chunk_x, chunk_z);
			neighbour_chunk = world.find_chunk_mutable(neighbour_chunk_x, chunk_z);
			if (target_chunk != nullptr && neighbour_chunk != nullptr
				&& !mesh_has_boundary_face(*neighbour_chunk, CHUNK_MESH_FACE_WEST,
					0U, edit_y, edit_local_z)
				&& renderer.get_gpu_renderer()->uploaded_identity_matches(
					slot_target, target_chunk->mesh_revision,
					target_chunk->chunk_x, target_chunk->chunk_z,
					target_chunk->voxel_revision)
				&& renderer.get_gpu_renderer()->uploaded_identity_matches(
					slot_neighbour, neighbour_chunk->mesh_revision,
					neighbour_chunk->chunk_x, neighbour_chunk->chunk_z,
					neighbour_chunk->voxel_revision))
				break ;
			if (static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::steady_clock::now().time_since_epoch()).count())
				>= deadline)
				return (FT_ERR_TIMEOUT);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		old_target_mesh = target_chunk->mesh_revision;
		old_target_voxel = target_chunk->voxel_revision;
		old_neighbour_mesh = neighbour_chunk->mesh_revision;
		old_neighbour_voxel = neighbour_chunk->voxel_revision;
		if (world.delete_block_at(target_x, edit_y, world_z)
			!= FT_ERR_SUCCESS || world.delete_block_at(target_x, edit_y - 1, world_z)
				!= FT_ERR_SUCCESS)
			return (1);
		if (!world.block_id_at(target_x, edit_y, world_z, &target_block)
			|| target_block != air_block
			|| target_chunk->last_mesh_publication_voxel_revision != old_target_voxel)
			return (FT_ERR_INTERNAL);
		if (hold_receiver)
		{
			/* Force the edited mesh to finish while the exposed faces are still
			 * absent from the receiver's mesh, then render that exact frame. */
			neighbour_chunk->cancel_remesh_work();
			neighbour_chunk->set_pending_remesh_request(UINT64_MAX, FT_FALSE);
			const auto finish_by = std::chrono::steady_clock::now()
				+ std::chrono::milliseconds(RENDERER_EDIT_PUBLICATION_MAX_LATENCY_MS);
			while (target_chunk->last_mesh_publication_voxel_revision != target_chunk->voxel_revision)
			{
				if (std::chrono::steady_clock::now() >= finish_by)
					return (FT_ERR_TIMEOUT);
				error_code = world.chunk_streamer.queue_chunk_remesh(*target_chunk);
				if (error_code != FT_ERR_SUCCESS && error_code != FT_ERR_FULL)
					return (error_code);
				error_code = WorldGenerationResultCommitter::drain(world.chunk_streamer, world, true);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			if (neighbour_chunk->mesh_revision != old_neighbour_mesh)
				return (FT_ERR_INVALID_STATE);
			renderer.render_world(window.framebuffer(), camera, world,
				static_cast<const RenderDebug *>(nullptr));
			if (!renderer.get_gpu_renderer()->uploaded_identity_matches(slot_target,
					old_target_mesh, target_chunk->chunk_x, target_chunk->chunk_z, old_target_voxel))
			{
				std::fprintf(stderr, "renderer-publication: deleted border was drawn before its receiving faces\n");
				return (FT_ERR_INVALID_STATE);
			}
			neighbour_chunk->clear_pending_remesh_request();
		}
		/* Hold the edited chunk's relight while exercising the receiver's
		 * actual capture, meshing, commit, and GPU upload paths. */
		bool hold_source_light = !hold_receiver;
		if (hold_source_light)
		{
			if (target_chunk->light_is_current())
				return (FT_ERR_INVALID_STATE);
			target_chunk->cancel_remesh_work();
			target_chunk->set_pending_remesh_request(UINT64_MAX, FT_FALSE);
		}
		deadline = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count())
			+ RENDERER_EDIT_PUBLICATION_MAX_LATENCY_MS;
		while (true)
		{
			if (hold_source_light)
			{
				/* Drain the real worker results without running orphan recovery
				 * on the synthetic source marker. */
				error_code = world.chunk_streamer.queue_chunk_remesh(*neighbour_chunk);
				if (error_code != FT_ERR_SUCCESS && error_code != FT_ERR_FULL)
					return (error_code);
				error_code = WorldGenerationResultCommitter::drain(
					world.chunk_streamer, world, true);
			}
			else
				error_code = world.update_around(camera.x, camera.z, 0,
					WorldCoordinates::MIN_RENDER_DISTANCE);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			WorldChunk *unrelated = world.find_chunk_mutable(target_chunk_x,
				unrelated_chunk_z);
			if (unrelated == nullptr || !unrelated->initialized)
				return (FT_ERR_INVALID_STATE);
			/* An unrelated cardinal neighbor still relighting must not keep
			 * the deleted block visible after its own mesh has committed. */
			const bool previous_mesh_dirty = unrelated->mesh_dirty;
			unrelated->mesh_dirty = true;
			renderer.render_world(window.framebuffer(), camera, world,
				static_cast<const RenderDebug *>(nullptr));
			unrelated->mesh_dirty = previous_mesh_dirty;
			if (window.poll_events() != FT_ERR_SUCCESS
				|| window.present() != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_OPERATION);
			target_chunk = world.find_chunk_mutable(target_chunk_x, chunk_z);
			neighbour_chunk = world.find_chunk_mutable(neighbour_chunk_x, chunk_z);
			if (target_chunk == nullptr || neighbour_chunk == nullptr)
				return (1);
			const bool target_new = renderer.get_gpu_renderer()
				->uploaded_identity_matches(slot_target, target_chunk->mesh_revision,
					target_chunk->chunk_x, target_chunk->chunk_z,
					target_chunk->voxel_revision);
			const bool target_old = renderer.get_gpu_renderer()
				->uploaded_identity_matches(slot_target, old_target_mesh,
					target_chunk->chunk_x, target_chunk->chunk_z, old_target_voxel);
			const bool neighbour_current = renderer.get_gpu_renderer()
				->uploaded_identity_matches(slot_neighbour,
					neighbour_chunk->mesh_revision, neighbour_chunk->chunk_x,
					neighbour_chunk->chunk_z, neighbour_chunk->voxel_revision);
			const bool neighbour_old = renderer.get_gpu_renderer()
				->uploaded_identity_matches(slot_neighbour, old_neighbour_mesh,
					neighbour_chunk->chunk_x, neighbour_chunk->chunk_z,
					old_neighbour_voxel);
			uint8_t face_light = 0U;
			const bool neighbour_new = neighbour_current
				&& neighbour_chunk->mesh_revision != old_neighbour_mesh
				&& mesh_has_boundary_face(*neighbour_chunk, CHUNK_MESH_FACE_WEST,
					0U, edit_y, edit_local_z, &face_light);
			if ((!target_new && !target_old) || (!neighbour_current && !neighbour_old))
			{
				std::fprintf(stderr,
					"renderer-publication: border GPU publication gap "
					"target=(%d,%d) neighbour=(%d,%d) target_new=%d "
					"target_old=%d neighbour_new=%d neighbour_old=%d\n",
					target_chunk->chunk_x, target_chunk->chunk_z,
					neighbour_chunk->chunk_x, neighbour_chunk->chunk_z,
					target_new ? 1 : 0, target_old ? 1 : 0,
					neighbour_new ? 1 : 0, neighbour_old ? 1 : 0);
				return (1);
			}
			if (hold_receiver && target_new && !neighbour_new)
				return (ApplicationError::fail("renderer-publication missing receiving faces", 1));
			if (hold_source_light && neighbour_new)
			{
				uint8_t lower_face_light = 0U;
				if (!target_old || target_chunk->light_is_current()
					|| !neighbour_chunk->mesh_dirty || !neighbour_chunk->waits_for_neighbor_light)
					return (FT_ERR_INVALID_STATE);
				if (!mesh_has_boundary_face(*neighbour_chunk, CHUNK_MESH_FACE_WEST,
						0U, edit_y - 1, edit_local_z, &lower_face_light)
					|| voxel_light_sky(face_light) != 15U
					|| voxel_light_sky(lower_face_light) != 15U)
				{
					std::fprintf(stderr, "renderer-publication: border preview sky=%u,%u expected=15,15\n",
						static_cast<unsigned int>(voxel_light_sky(face_light)),
						static_cast<unsigned int>(voxel_light_sky(lower_face_light)));
					return (FT_ERR_INVALID_STATE);
				}
				std::printf("renderer-publication: skylit border face uploaded while source light pending\n");
				target_chunk->clear_pending_remesh_request();
				hold_source_light = false;
			}
			if (target_new && neighbour_new)
			{
				std::printf("renderer-publication: border delete pair stable "
					"target=(%d,%d) neighbour=(%d,%d)\n",
					target_chunk->chunk_x, target_chunk->chunk_z,
					neighbour_chunk->chunk_x, neighbour_chunk->chunk_z);
				if (hold_receiver)
				{
					/* Lose the receiver's upload after the border edit was already
					 * shown. A later interior edit has no dependency on it. */
					const auto render_without_receiver = [&]()
					{
						const bool light_ready = neighbour_chunk->light_ready_for_render;
						const bool mesh_dirty = neighbour_chunk->mesh_dirty;
						neighbour_chunk->light_ready_for_render = false;
						neighbour_chunk->mesh_dirty = false;
						renderer.render_world(window.framebuffer(), camera, world,
							static_cast<const RenderDebug *>(nullptr));
						neighbour_chunk->light_ready_for_render = light_ready;
						neighbour_chunk->mesh_dirty = mesh_dirty;
					};
					render_without_receiver();
					error_code = world.place_block_at(target_chunk->world_x + 8,
						GAME_VOXEL_CHUNK_HEIGHT - 6, target_chunk->world_z + 8,
						VOXEL_GENERATOR_STONE_BLOCK);
					const auto finish_by = std::chrono::steady_clock::now()
						+ std::chrono::milliseconds(RENDERER_EDIT_PUBLICATION_MAX_LATENCY_MS);
					while (error_code == FT_ERR_SUCCESS
						&& target_chunk->last_mesh_publication_voxel_revision != target_chunk->voxel_revision)
					{
						if (std::chrono::steady_clock::now() >= finish_by)
							return (FT_ERR_TIMEOUT);
						error_code = world.publish_pending_remeshes();
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
					}
					if (error_code != FT_ERR_SUCCESS)
						return (error_code);
					/* Simulate a newer border edit awaiting its own source mesh.
					 * Its receiver must not delay the already built interior mesh. */
					auto &pending_east = target_chunk->border_delete_dependencies[1U];
					const auto previous_east = pending_east;
					pending_east = {target_chunk->voxel_revision + 1U, neighbour_chunk->mesh_revision + 1U};
					render_without_receiver();
					pending_east = previous_east;
					if (!renderer.get_gpu_renderer()->uploaded_identity_matches(slot_target,
							target_chunk->mesh_revision, target_chunk->chunk_x, target_chunk->chunk_z,
							target_chunk->voxel_revision))
						return (ApplicationError::fail("renderer-publication obsolete border dependency", FT_ERR_INVALID_STATE));
					std::puts("renderer-publication: interior edit uploaded independently of old border receiver");
				}
				return (FT_ERR_SUCCESS);
			}
			if (static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::steady_clock::now().time_since_epoch()).count())
				>= deadline)
			{
				std::fprintf(stderr,
					"renderer-publication: border delete stalled at (%d,%d,%d) "
					"target_gpu=%d target_dirty=%d target_pending=%llu "
					"neighbour_gpu=%d neighbour_face=%d neighbour_dirty=%d "
					"neighbour_pending=%llu neighbour_waiting_for_light=%d\n",
					target_x, edit_y, world_z, target_new ? 1 : 0,
					target_chunk->mesh_dirty ? 1 : 0,
					static_cast<unsigned long long>(target_chunk->pending_mesh_request_id),
					neighbour_current ? 1 : 0,
					mesh_has_boundary_face(*neighbour_chunk, CHUNK_MESH_FACE_WEST,
						0U, edit_y, edit_local_z) ? 1 : 0,
					neighbour_chunk->mesh_dirty ? 1 : 0,
					static_cast<unsigned long long>(neighbour_chunk->pending_mesh_request_id),
					neighbour_chunk->waits_for_neighbor_light ? 1 : 0);
				return (FT_ERR_TIMEOUT);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
}

RendererPublicationValidator::RendererPublicationValidator()
{
}

RendererPublicationValidator::RendererPublicationValidator(
	const RendererPublicationValidator &other)
	: IValidator(other)
{
	(void)other;
}

RendererPublicationValidator::~RendererPublicationValidator()
{
}

RendererPublicationValidator &RendererPublicationValidator::operator=(
	const RendererPublicationValidator &other)
{
	(void)other;
	return (*this);
}

int RendererPublicationValidator::validate_software_lighting_contract() noexcept
{
	const uint32_t dark = RendererColor::shade_color(
		RendererColor::rgba(200U, 100U, 50U),
		RendererColor::light_shade(voxel_light_pack(0U, 0U)));
	const uint32_t intermediate = RendererColor::shade_color(
		RendererColor::rgba(200U, 100U, 50U),
		RendererColor::light_shade(voxel_light_pack(7U, 3U)));
	const uint32_t full = RendererColor::shade_color(
		RendererColor::rgba(200U, 100U, 50U),
		RendererColor::light_shade(voxel_light_pack(15U, 0U)));

	if (dark != RendererColor::rgba(10U, 5U, 2U)
		|| intermediate != RendererColor::rgba(88U, 44U, 22U)
		|| full != RendererColor::rgba(200U, 100U, 50U))
	{
		std::fprintf(stderr,
			"renderer-publication: software lighting contract failed "
			"dark=%06x intermediate=%06x full=%06x\n",
			dark, intermediate, full);
		return (1);
	}
	return (0);
}

int RendererPublicationValidator::validate() const
{
	ApplicationWindow window;
	Camera camera;
	VoxelRenderer renderer;
	std::unique_ptr<World> world_storage(new (std::nothrow) World());
	if (world_storage == nullptr)
		return (ApplicationError::fail("renderer-publication world allocation",
			FT_ERR_NO_MEMORY));
	World &world = *world_storage;
	const WorldChunk *chunk;
	int32_t error_code;
	int32_t edit_y;
	double surface_top;
	int32_t slot;
	uint64_t deadline;
	uint64_t edit_started_milliseconds;
	uint64_t next_progress_report;
	uint64_t previous_mesh_revision;
	uint64_t previous_voxel_revision;
	uint8_t mesh_light_minimum;
	uint8_t mesh_light_maximum;
	std::size_t mesh_light_nonzero;
	World::StreamDiagnostics stream_diagnostics;

	if (RendererPublicationValidator::validate_software_lighting_contract()
		!= 0)
		return (1);
	if (window.initialize(false, 320, 240, false, true) != 0
		|| !window.is_gpu_mode())
	{
		std::fprintf(stderr,
			"renderer-publication: no graphics context available\n");
		(void)window.destroy();
		return (2);
	}
	error_code = world.initialize("renderer-publication-seed");
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("renderer-publication world", error_code));
	error_code = renderer.initialize_gpu(window.get_gpu_window()->get_width(),
		window.get_gpu_window()->get_height());
	if (error_code != FT_ERR_SUCCESS)
	{
		world.destroy();
		(void)window.destroy();
		return (ApplicationError::fail("renderer-publication GPU", error_code));
	}
	if (!overlay_region_preserves_surrounding_pixels(window,
			*renderer.get_gpu_renderer(), world)
		|| !item_markers_draw_without_altering_overlay(window,
			*renderer.get_gpu_renderer())
		|| !cached_debug_overlay_is_drawn(window, renderer, world))
	{
		world.destroy();
		(void)window.destroy();
		return (ApplicationError::fail("renderer-publication cached debug overlay",
			FT_ERR_INTERNAL));
	}
	camera.initialize();
	PlayerController::spawn_player_on_ground(&camera, world);
	deadline = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count()) + 30000U;
	edit_started_milliseconds = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	while (true)
	{
		error_code = world.update_around(camera.x, camera.z, 0,
			WorldCoordinates::MIN_RENDER_DISTANCE);
		if (error_code != FT_ERR_SUCCESS)
		{
			std::fprintf(stderr,
				"renderer-publication: initial stream update failed error=%d\n",
				error_code);
			world.destroy();
			(void)window.destroy();
			return (ApplicationError::fail("renderer-publication initial stream",
				error_code));
		}
		renderer.render_world(window.framebuffer(), camera, world,
			static_cast<const RenderDebug *>(nullptr));
		error_code = window.poll_events();
		if (error_code != FT_ERR_SUCCESS)
		{
			world.destroy();
			(void)window.destroy();
			return (ApplicationError::fail("renderer-publication event polling",
				error_code));
		}
		error_code = window.present();
		if (error_code != FT_ERR_SUCCESS)
		{
			world.destroy();
			(void)window.destroy();
			return (ApplicationError::fail("renderer-publication present",
				error_code));
		}
		chunk = world.find_chunk(0, 0);
		if (chunk != nullptr && chunk->initialized
			&& chunk->mesh_revision != 0U)
		{
			slot = static_cast<int32_t>(chunk - world.chunks);
			if (renderer.get_gpu_renderer()->uploaded_identity_matches(slot,
				chunk->mesh_revision, chunk->chunk_x, chunk->chunk_z,
				chunk->voxel_revision))
				break ;
		}
		if (static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count())
			>= deadline)
		{
			stream_diagnostics = world.stream_diagnostics();
			std::fprintf(stderr,
				"renderer-publication: initial mesh timeout "
				"chunk_present=%d initialized=%d mesh_revision=%llu "
				"voxel_revision=%llu pending=%llu dirty=%d "
				"ready=%zu pending_stream=%zu active=%zu remesh=%zu\n",
				chunk != nullptr ? 1 : 0,
				chunk != nullptr && chunk->initialized ? 1 : 0,
				chunk != nullptr ? static_cast<unsigned long long>(chunk->mesh_revision) : 0ULL,
				chunk != nullptr ? static_cast<unsigned long long>(chunk->voxel_revision) : 0ULL,
				chunk != nullptr ? static_cast<unsigned long long>(chunk->pending_mesh_request_id) : 0ULL,
				chunk != nullptr && chunk->mesh_dirty ? 1 : 0,
				stream_diagnostics.ready_count, stream_diagnostics.pending_count,
				stream_diagnostics.active_generation_count,
				stream_diagnostics.remesh_priority_queue_depth);
			world.destroy();
			(void)window.destroy();
			return (1);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	slot = static_cast<int32_t>(chunk - world.chunks);
	if (!mesh_has_light(chunk->mesh, &mesh_light_minimum,
			&mesh_light_maximum, &mesh_light_nonzero))
	{
		std::fprintf(stderr,
			"renderer-publication: initial uploaded mesh has no light "
			"chunk=(%d,%d) vertices=%zu min=%u max=%u nonzero=%zu\n",
			chunk->chunk_x, chunk->chunk_z, chunk->mesh.vertices.size(),
			static_cast<unsigned int>(mesh_light_minimum),
			static_cast<unsigned int>(mesh_light_maximum), mesh_light_nonzero);
		world.destroy();
		(void)window.destroy();
		return (1);
	}
	previous_mesh_revision = chunk->mesh_revision;
	previous_voxel_revision = chunk->voxel_revision;
	if (!world.surface_top_at(2, 2, &surface_top))
	{
		world.destroy();
		(void)window.destroy();
		return (1);
	}
	edit_y = static_cast<int32_t>(surface_top + 2.0);
	error_code = world.place_block_at(2, edit_y - 1, 2,
		VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code == FT_ERR_SUCCESS)
		error_code = world.place_block_at(2, edit_y, 2,
			VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code != FT_ERR_SUCCESS)
	{
		world.destroy();
		(void)window.destroy();
		return (ApplicationError::fail("renderer-publication edit", error_code));
	}
	deadline = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count()) + 30000U;
	next_progress_report = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count()) + 1000U;
	while (true)
	{
		error_code = world.update_around(camera.x, camera.z, 0,
			WorldCoordinates::MIN_RENDER_DISTANCE);
		if (error_code != FT_ERR_SUCCESS)
		{
			world.destroy();
			(void)window.destroy();
			return (ApplicationError::fail("renderer-publication edit stream",
				error_code));
		}
		renderer.render_world(window.framebuffer(), camera, world,
			static_cast<const RenderDebug *>(nullptr));
		error_code = window.poll_events();
		if (error_code != FT_ERR_SUCCESS)
		{
			world.destroy();
			(void)window.destroy();
			return (ApplicationError::fail("renderer-publication event polling",
				error_code));
		}
		error_code = window.present();
		if (error_code != FT_ERR_SUCCESS)
		{
			world.destroy();
			(void)window.destroy();
			return (ApplicationError::fail("renderer-publication present",
				error_code));
		}
		chunk = world.find_chunk(0, 0);
		if (chunk != nullptr && chunk->voxel_revision > 1U)
		{
			const bool new_publication = renderer.get_gpu_renderer()
				->uploaded_identity_matches(slot, chunk->mesh_revision,
					chunk->chunk_x, chunk->chunk_z, chunk->voxel_revision);
			const bool old_publication = renderer.get_gpu_renderer()
				->uploaded_identity_matches(slot, previous_mesh_revision,
					chunk->chunk_x, chunk->chunk_z, previous_voxel_revision);
			if (!new_publication && !old_publication)
			{
				std::fprintf(stderr,
					"renderer-publication: GPU publication gap after edit "
					"chunk=(%d,%d) old_mesh=%llu old_voxel=%llu "
					"current_mesh=%llu current_voxel=%llu\n",
					chunk->chunk_x, chunk->chunk_z,
					static_cast<unsigned long long>(previous_mesh_revision),
					static_cast<unsigned long long>(previous_voxel_revision),
					static_cast<unsigned long long>(chunk->mesh_revision),
					static_cast<unsigned long long>(chunk->voxel_revision));
				world.destroy();
				(void)window.destroy();
				return (1);
			}
			if (!new_publication)
			{
				/* The previous committed mesh is intentionally retained while
				 * the replacement upload is deferred. */
				continue ;
			}
			mesh_has_light(chunk->mesh, &mesh_light_minimum,
				&mesh_light_maximum, &mesh_light_nonzero);
			std::printf("renderer-publication: edit_light chunk=(%d,%d) "
				"voxel=%llu mesh=%llu light=%u input=%u computed=%u "
				"pending=%llu min=%u max=%u nonzero=%zu\n",
				chunk->chunk_x, chunk->chunk_z,
				static_cast<unsigned long long>(chunk->voxel_revision),
				static_cast<unsigned long long>(chunk->mesh_revision),
				static_cast<unsigned int>(chunk->light_version),
				static_cast<unsigned int>(chunk->light_input_version),
				static_cast<unsigned int>(chunk->computed_light_input_version),
				static_cast<unsigned long long>(chunk->pending_mesh_request_id),
				static_cast<unsigned int>(mesh_light_minimum),
				static_cast<unsigned int>(mesh_light_maximum),
				mesh_light_nonzero);
			uint64_t publication_latency_ms = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count())
				- edit_started_milliseconds;
			if (publication_latency_ms > RENDERER_EDIT_PUBLICATION_MAX_LATENCY_MS)
			{
				stream_diagnostics = world.stream_diagnostics();
				std::fprintf(stderr,
					"renderer-publication: latency bound failed latency_ms=%llu "
					"limit_ms=%llu voxel=%llu mesh=%llu pending=%llu dirty=%d "
					"remesh=%zu active=%zu stale=%zu stale_capture=%zu "
					"stale_dependency=%zu stale_pending=%zu stale_revision=%zu\n",
					static_cast<unsigned long long>(publication_latency_ms),
					static_cast<unsigned long long>(
						RENDERER_EDIT_PUBLICATION_MAX_LATENCY_MS),
					static_cast<unsigned long long>(chunk->voxel_revision),
					static_cast<unsigned long long>(chunk->mesh_revision),
					static_cast<unsigned long long>(
						chunk->pending_mesh_request_id),
					chunk->mesh_dirty ? 1 : 0,
					stream_diagnostics.remesh_priority_queue_depth,
					stream_diagnostics.active_generation_count,
					stream_diagnostics.stale_remesh_result_count,
					stream_diagnostics.stale_remesh_capture_count,
					stream_diagnostics.stale_remesh_dependency_count,
					stream_diagnostics.stale_remesh_pending_count,
					stream_diagnostics.stale_remesh_revision_count);
				world.destroy();
				(void)window.destroy();
				return (1);
			}
	std::printf("renderer-publication: edit_latency_ms=%llu\n",
				static_cast<unsigned long long>(publication_latency_ms));
			break ;
		}
		uint64_t now_milliseconds = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
		if (now_milliseconds >= next_progress_report)
		{
			stream_diagnostics = world.stream_diagnostics();
			std::fprintf(stderr,
				"renderer-publication: waiting chunk=(%d,%d) voxel=%llu "
				"mesh=%llu uploaded=%d pending=%llu dirty=%d remesh=%zu "
				"active=%zu\n",
				chunk != nullptr ? chunk->chunk_x : 0,
				chunk != nullptr ? chunk->chunk_z : 0,
				chunk != nullptr ? static_cast<unsigned long long>(chunk->voxel_revision) : 0ULL,
				chunk != nullptr ? static_cast<unsigned long long>(chunk->mesh_revision) : 0ULL,
				chunk != nullptr && renderer.get_gpu_renderer()->uploaded_identity_matches(
					slot, chunk->mesh_revision, chunk->chunk_x, chunk->chunk_z,
					chunk->voxel_revision) ? 1 : 0,
				chunk != nullptr ? static_cast<unsigned long long>(chunk->pending_mesh_request_id) : 0ULL,
				chunk != nullptr && chunk->mesh_dirty ? 1 : 0,
				stream_diagnostics.remesh_priority_queue_depth,
				stream_diagnostics.active_generation_count);
			next_progress_report = now_milliseconds + 1000U;
		}
		if (static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count())
			>= deadline)
		{
			stream_diagnostics = world.stream_diagnostics();
			std::fprintf(stderr,
				"renderer-publication: edit GPU upload timeout "
				"chunk_present=%d voxel=%llu mesh=%llu pending=%llu dirty=%d "
				"remesh=%zu active=%zu stale=%zu last_error=%d\n",
				chunk != nullptr ? 1 : 0,
				chunk != nullptr ? static_cast<unsigned long long>(chunk->voxel_revision) : 0ULL,
				chunk != nullptr ? static_cast<unsigned long long>(chunk->mesh_revision) : 0ULL,
				chunk != nullptr ? static_cast<unsigned long long>(chunk->pending_mesh_request_id) : 0ULL,
				chunk != nullptr && chunk->mesh_dirty ? 1 : 0,
				stream_diagnostics.remesh_priority_queue_depth,
				stream_diagnostics.active_generation_count,
				stream_diagnostics.stale_remesh_result_count,
				stream_diagnostics.last_error);
			world.destroy();
			(void)window.destroy();
			return (1);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	error_code = validate_boundary_delete_publication(window, renderer, camera,
		world, false);
	if (error_code == FT_ERR_SUCCESS)
		error_code = validate_boundary_delete_publication(window, renderer, camera,
			world, true);
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"renderer-publication: border delete validation failed error=%d\n",
			error_code);
		world.destroy();
		(void)window.destroy();
		return (error_code);
	}
	/* Repeat the publication contract for deletion.  A delete changes the
	 * light frontier as well as the mesh, so the renderer must retain the
	 * previous complete GPU publication until the replacement is ready. */
	WorldChunk *edited_chunk = world.find_chunk_mutable(0, 0);
	WorldChunk *light_source = world.find_chunk_mutable(1, 0);
	if (edited_chunk == nullptr || light_source == nullptr)
	{
		world.destroy();
		(void)window.destroy();
		return (ApplicationError::fail("renderer-publication delete dependency",
			FT_ERR_NOT_FOUND));
	}
	const auto settle_deadline = std::chrono::steady_clock::now()
		+ std::chrono::seconds(3);
	while (!light_source->light_is_current() || edited_chunk->mesh_dirty
		|| edited_chunk->pending_mesh_request_id != 0U
		|| !renderer.get_gpu_renderer()->uploaded_identity_matches(slot,
			edited_chunk->mesh_revision, edited_chunk->chunk_x,
			edited_chunk->chunk_z, edited_chunk->voxel_revision))
	{
		error_code = world.publish_pending_remeshes();
		if (error_code != FT_ERR_SUCCESS
			|| std::chrono::steady_clock::now() >= settle_deadline)
		{
			world.destroy();
			(void)window.destroy();
			return (ApplicationError::fail("renderer-publication dependency baseline",
				error_code == FT_ERR_SUCCESS ? FT_ERR_TIMEOUT : error_code));
		}
		renderer.render_world(window.framebuffer(), camera, world,
			static_cast<const RenderDebug *>(nullptr));
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	previous_mesh_revision = edited_chunk->mesh_revision;
	previous_voxel_revision = edited_chunk->voxel_revision;
	/* Hold the neighbour's publication until the renderer has shown the
	 * deletion. Its request marker models a solve that spans several frames. */
	light_source->cancel_remesh_work();
	light_source->clear_light_dependency();
	const uint16_t published_source_input = light_source->light_input_version;
	light_source->mark_light_input_changed();
	light_source->set_pending_remesh_request(UINT64_MAX, FT_FALSE);
	edited_chunk->set_light_dependency(light_source->chunk_x, light_source->chunk_z);
	const auto delete_started = std::chrono::steady_clock::now();
	error_code = world.delete_block_at(2, edit_y, 2);
	if (error_code != FT_ERR_SUCCESS)
	{
		world.destroy();
		(void)window.destroy();
		return (ApplicationError::fail("renderer-publication delete", error_code));
	}
	uint32_t deleted_block = VOXEL_GENERATOR_STONE_BLOCK;
	if (!world.block_id_at(2, edit_y, 2, &deleted_block)
		|| deleted_block != GAME_VOXEL_AIR_BLOCK)
	{
		world.destroy();
		(void)window.destroy();
		return (ApplicationError::fail("renderer-publication delete collision",
			FT_ERR_INVALID_STATE));
	}
	const auto delete_deadline = delete_started
		+ std::chrono::milliseconds(RENDERER_EDIT_PUBLICATION_MAX_LATENCY_MS);
	while (true)
	{
		/* Skip orphan recovery for the fixture's held neighbour marker. */
		error_code = world.chunk_streamer.queue_chunk_remesh(*edited_chunk);
		if (error_code == FT_ERR_SUCCESS || error_code == FT_ERR_FULL)
			error_code = WorldGenerationResultCommitter::drain(world.chunk_streamer,
				world, true);
		if (error_code != FT_ERR_SUCCESS)
		{
			world.destroy();
			(void)window.destroy();
			return (ApplicationError::fail(
				"renderer-publication delete stream", error_code));
		}
		const bool deletion_mesh_ready =
			edited_chunk->last_mesh_publication_voxel_revision == edited_chunk->voxel_revision;
		const ft_bool edit_priority = edited_chunk->last_mesh_publication_interactive;
		if (deletion_mesh_ready)
		{
			/* Model a light-only result replacing the edit's publication before
			 * upload, while another visible chunk also asks for priority. */
			edited_chunk->last_mesh_publication_interactive = FT_FALSE;
			light_source->mesh_revision += 1U;
			light_source->last_mesh_publication_interactive = FT_TRUE;
		}
		renderer.render_world(window.framebuffer(), camera, world,
			static_cast<const RenderDebug *>(nullptr));
		edited_chunk->last_mesh_publication_interactive = edit_priority;
		error_code = window.poll_events();
		if (error_code != FT_ERR_SUCCESS)
		{
			world.destroy();
			(void)window.destroy();
			return (ApplicationError::fail(
				"renderer-publication delete event polling", error_code));
		}
		error_code = window.present();
		if (error_code != FT_ERR_SUCCESS)
		{
			world.destroy();
			(void)window.destroy();
			return (ApplicationError::fail(
				"renderer-publication delete present", error_code));
		}
		chunk = world.find_chunk(0, 0);
		if (chunk != nullptr && chunk->voxel_revision > previous_voxel_revision)
		{
			const bool new_publication = renderer.get_gpu_renderer()
				->uploaded_identity_matches(slot, chunk->mesh_revision,
					chunk->chunk_x, chunk->chunk_z, chunk->voxel_revision);
			const bool old_publication = renderer.get_gpu_renderer()
				->uploaded_identity_matches(slot, previous_mesh_revision,
					chunk->chunk_x, chunk->chunk_z, previous_voxel_revision);
			if (deletion_mesh_ready && !new_publication)
			{
				world.destroy();
				(void)window.destroy();
				return (ApplicationError::fail("renderer-publication stale edited voxels retained",
					FT_ERR_INVALID_STATE));
			}
			if (!new_publication && !old_publication)
			{
				std::fprintf(stderr,
					"renderer-publication: GPU publication gap after delete "
					"chunk=(%d,%d) old_mesh=%llu old_voxel=%llu "
					"current_mesh=%llu current_voxel=%llu\n",
					chunk->chunk_x, chunk->chunk_z,
					static_cast<unsigned long long>(previous_mesh_revision),
					static_cast<unsigned long long>(previous_voxel_revision),
					static_cast<unsigned long long>(chunk->mesh_revision),
					static_cast<unsigned long long>(chunk->voxel_revision));
				world.destroy();
				(void)window.destroy();
				return (1);
			}
			if (new_publication && std::chrono::steady_clock::now() <= delete_deadline)
			{
				if (!chunk->waits_for_neighbor_light || !chunk->mesh_dirty)
				{
					world.destroy();
					(void)window.destroy();
					return (ApplicationError::fail("renderer-publication preview lost relight",
						FT_ERR_INVALID_STATE));
				}
				bool exposed_face_has_light = false;
				for (std::size_t index = 0U; index < chunk->mesh.vertices.size(); ++index)
				{
					const chunk_mesh_vertex &vertex = chunk->mesh.vertices[index];
					if (vertex.face == CHUNK_MESH_FACE_UP && vertex.coordinate_y == edit_y
						&& vertex.coordinate_x >= 2U && vertex.coordinate_x <= 3U
						&& vertex.coordinate_z >= 2U && vertex.coordinate_z <= 3U
						&& voxel_light_sky(vertex.packed_light) > 0U)
						exposed_face_has_light = true;
				}
				if (!exposed_face_has_light)
				{
					world.destroy();
					(void)window.destroy();
					return (ApplicationError::fail("renderer-publication exposed face light",
						FT_ERR_INVALID_STATE));
				}
				mesh_has_light(chunk->mesh, &mesh_light_minimum,
					&mesh_light_maximum, &mesh_light_nonzero);
				const auto delete_latency = std::chrono::duration_cast<
					std::chrono::milliseconds>(std::chrono::steady_clock::now()
						- delete_started);
				std::printf("renderer-publication: dependency_delete_latency_ms=%llu\n",
					static_cast<unsigned long long>(delete_latency.count()));
				std::printf("renderer-publication: delete_light chunk=(%d,%d) "
					"voxel=%llu mesh=%llu light=%u input=%u computed=%u "
					"pending=%llu min=%u max=%u nonzero=%zu\n",
					chunk->chunk_x, chunk->chunk_z,
					static_cast<unsigned long long>(chunk->voxel_revision),
					static_cast<unsigned long long>(chunk->mesh_revision),
					static_cast<unsigned int>(chunk->light_version),
					static_cast<unsigned int>(chunk->light_input_version),
					static_cast<unsigned int>(chunk->computed_light_input_version),
					static_cast<unsigned long long>(chunk->pending_mesh_request_id),
					static_cast<unsigned int>(mesh_light_minimum),
					static_cast<unsigned int>(mesh_light_maximum),
					mesh_light_nonzero);
				break ;
			}
		}
		if (std::chrono::steady_clock::now() >= delete_deadline)
		{
			std::fprintf(stderr,
				"renderer-publication: delete GPU upload timeout while light was pending\n");
			world.destroy();
			(void)window.destroy();
			return (1);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	/* Model the light becoming current while the neighbour still has geometry
	 * queued. Keep the marker and bypass orphan recovery to exercise the gate. */
	light_source->light_input_version = published_source_input;
	const auto relight_deadline = std::chrono::steady_clock::now()
		+ std::chrono::seconds(3);
	while (edited_chunk->mesh_dirty || edited_chunk->waits_for_neighbor_light
		|| !edited_chunk->light_is_current())
	{
		error_code = world.chunk_streamer.queue_chunk_remesh(*edited_chunk);
		if (error_code == FT_ERR_SUCCESS || error_code == FT_ERR_FULL)
			error_code = WorldGenerationResultCommitter::drain(world.chunk_streamer,
				world, true);
		if (error_code != FT_ERR_SUCCESS
			|| std::chrono::steady_clock::now() >= relight_deadline)
		{
			world.destroy();
			(void)window.destroy();
			return (ApplicationError::fail("renderer-publication finish delayed light",
				error_code == FT_ERR_SUCCESS ? FT_ERR_TIMEOUT : error_code));
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	light_source->clear_pending_remesh_request();
	/* Recenter after capture has handed a geometry refresh to the pipeline.
	 * Cancelling that refresh must leave the dependency identity unchanged. */
	const uint64_t published_light_revision = edited_chunk->light_revision;
	world.chunk_streamer.mark_remesh_dirty(*edited_chunk);
	const auto recenter_deadline = std::chrono::steady_clock::now()
		+ std::chrono::seconds(3);
	while (std::chrono::steady_clock::now() < recenter_deadline)
	{
		error_code = world.chunk_streamer.queue_chunk_remesh(*edited_chunk);
		if (error_code != FT_ERR_SUCCESS && error_code != FT_ERR_FULL)
			break ;
		const uint64_t request_id = edited_chunk->pending_mesh_request_id;
		if (request_id != 0U && world.chunk_streamer.pipeline().has_request_or_result(request_id))
		{
			const double next_center_x = (static_cast<double>(world.center_chunk_x) + 1.5)
				* GAME_VOXEL_CHUNK_WIDTH;
			error_code = world.update_around(next_center_x, camera.z, 0,
				WorldCoordinates::MIN_RENDER_DISTANCE);
			break ;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (std::chrono::steady_clock::now() >= recenter_deadline
		|| error_code != FT_ERR_SUCCESS || edited_chunk->light_revision != published_light_revision)
	{
		world.destroy();
		(void)window.destroy();
		return (ApplicationError::fail("renderer-publication recenter light identity",
			FT_ERR_INVALID_STATE));
	}
	std::printf("renderer-publication: ok slot=%d voxel_revision=%llu\n",
		slot, static_cast<unsigned long long>(chunk->voxel_revision));
	world.destroy();
	(void)window.destroy();
	return (0);
}
