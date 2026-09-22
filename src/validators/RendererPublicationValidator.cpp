#include "../../src/validators/RendererPublicationValidator.hpp"
#include "../../src/camera/Camera.hpp"
#include "../../src/diagnostics/ApplicationError.hpp"
#include "../../src/player/PlayerController.hpp"
#include "../../src/platform/ApplicationWindow.hpp"
#include "../../src/frame/RendererColor.hpp"
#include "../../src/render/VoxelRenderer.hpp"
#include "../../src/world/World.hpp"
#include <chrono>
#include <cstdio>
#include <memory>
#include <new>
#include <thread>

namespace
{
	/* GPU upload includes driver synchronization and is sampled once here;
	 * keep this below a visible hitch while leaving the repeated block-edit
	 * percentile gate responsible for the tighter CPU-side budget. */
	static const uint64_t RENDERER_EDIT_PUBLICATION_MAX_LATENCY_MS = 500U;

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
		uint16_t boundary_x) noexcept
	{
		std::size_t index = 0U;

		while (index < chunk.mesh.vertices.size())
		{
			const chunk_mesh_vertex &vertex = chunk.mesh.vertices[index];
			if (vertex.face == face && vertex.coordinate_x == boundary_x)
				return (true);
			index += 1U;
		}
		return (false);
	}

	static int validate_boundary_delete_publication(ApplicationWindow &window,
		VoxelRenderer &renderer, Camera &camera, World &world) noexcept
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
		bool place_first = false;
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

				if (candidate->initialized)
				{
					east = world.find_chunk_mutable(candidate->chunk_x + 1,
						candidate->chunk_z);
					local_z = 0;
					while (east != nullptr && east->initialized
						&& local_z < GAME_VOXEL_CHUNK_DEPTH && edit_y < 0)
					{
						local_y = 0;
						while (local_y < GAME_VOXEL_CHUNK_HEIGHT && edit_y < 0)
						{
							if (candidate->chunk.read_block(
								GAME_VOXEL_CHUNK_WIDTH - 1, local_y, local_z,
								&target_block) == FT_ERR_SUCCESS
								&& east->chunk.read_block(0, local_y, local_z,
									&neighbour_block) == FT_ERR_SUCCESS
								&& neighbour_block != air_block
								&& (target_block == air_block
									|| voxel_block_is_breakable(target_block)
										!= FT_FALSE)
								&& voxel_block_occludes_faces(neighbour_block)
									!= FT_FALSE)
							{
								target_chunk_x = candidate->chunk_x;
								neighbour_chunk_x = east->chunk_x;
								chunk_z = candidate->chunk_z;
								target_x = candidate->world_x
									+ GAME_VOXEL_CHUNK_WIDTH - 1;
								world_z = candidate->world_z + local_z;
								edit_y = local_y;
								place_first = target_block == air_block;
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
				"renderer-publication: no breakable solid boundary pair "
				"after fixture wait ready=%zu pending=%zu active=%zu remesh=%zu\n",
				diagnostics.ready_count, diagnostics.pending_count,
				diagnostics.active_generation_count,
				diagnostics.remesh_priority_queue_depth);
			return (1);
		}
		if (place_first && world.place_block_at(target_x, edit_y, world_z,
			VOXEL_GENERATOR_STONE_BLOCK) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_OPERATION);
		target_chunk = world.find_chunk_mutable(target_chunk_x, chunk_z);
		neighbour_chunk = world.find_chunk_mutable(neighbour_chunk_x, chunk_z);
		if (target_chunk == nullptr || neighbour_chunk == nullptr
			|| !target_chunk->initialized || !neighbour_chunk->initialized)
			return (1);
		slot_target = static_cast<int32_t>(target_chunk - world.chunks);
		slot_neighbour = static_cast<int32_t>(neighbour_chunk - world.chunks);
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
			!= FT_ERR_SUCCESS)
			return (1);
		deadline = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count())
			+ 30000U;
		while (true)
		{
			if (world.update_around(camera.x, camera.z, 0,
					WorldCoordinates::MIN_RENDER_DISTANCE) != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_OPERATION);
			renderer.render_world(window.framebuffer(), camera, world,
				static_cast<const RenderDebug *>(nullptr));
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
			const bool neighbour_new = renderer.get_gpu_renderer()
				->uploaded_identity_matches(slot_neighbour,
					neighbour_chunk->mesh_revision, neighbour_chunk->chunk_x,
					neighbour_chunk->chunk_z, neighbour_chunk->voxel_revision);
			const bool neighbour_old = renderer.get_gpu_renderer()
				->uploaded_identity_matches(slot_neighbour, old_neighbour_mesh,
					neighbour_chunk->chunk_x, neighbour_chunk->chunk_z,
					old_neighbour_voxel);
			if ((!target_new && !target_old) || (!neighbour_new && !neighbour_old))
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
			if (target_new && neighbour_new)
			{
				if (!mesh_has_boundary_face(*neighbour_chunk,
					CHUNK_MESH_FACE_WEST, 0U))
					return (1);
				std::printf("renderer-publication: border delete pair stable "
					"target=(%d,%d) neighbour=(%d,%d)\n",
					target_chunk->chunk_x, target_chunk->chunk_z,
					neighbour_chunk->chunk_x, neighbour_chunk->chunk_z);
				return (FT_ERR_SUCCESS);
			}
			if (static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::steady_clock::now().time_since_epoch()).count())
				>= deadline)
				return (FT_ERR_TIMEOUT);
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
	edit_y = static_cast<int32_t>(surface_top + 1.0);
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
		world);
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
	previous_mesh_revision = chunk->mesh_revision;
	previous_voxel_revision = chunk->voxel_revision;
	error_code = world.delete_block_at(2, edit_y, 2);
	if (error_code != FT_ERR_SUCCESS)
	{
		world.destroy();
		(void)window.destroy();
		return (ApplicationError::fail("renderer-publication delete", error_code));
	}
	deadline = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count()) + 30000U;
	while (true)
	{
		error_code = world.update_around(camera.x, camera.z, 0,
			WorldCoordinates::MIN_RENDER_DISTANCE);
		if (error_code != FT_ERR_SUCCESS)
		{
			world.destroy();
			(void)window.destroy();
			return (ApplicationError::fail(
				"renderer-publication delete stream", error_code));
		}
		renderer.render_world(window.framebuffer(), camera, world,
			static_cast<const RenderDebug *>(nullptr));
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
			if (new_publication)
			{
				mesh_has_light(chunk->mesh, &mesh_light_minimum,
					&mesh_light_maximum, &mesh_light_nonzero);
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
		if (static_cast<uint64_t>(std::chrono::duration_cast<
			std::chrono::milliseconds>(std::chrono::steady_clock::now()
				.time_since_epoch()).count()) >= deadline)
		{
			std::fprintf(stderr,
				"renderer-publication: delete GPU upload timeout\n");
			world.destroy();
			(void)window.destroy();
			return (1);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	std::printf("renderer-publication: ok slot=%d voxel_revision=%llu\n",
		slot, static_cast<unsigned long long>(chunk->voxel_revision));
	world.destroy();
	(void)window.destroy();
	return (0);
}
