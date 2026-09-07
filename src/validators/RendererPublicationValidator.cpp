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
#include <thread>

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
	World world;
	const WorldChunk *chunk;
	int32_t error_code;
	int32_t edit_y;
	double surface_top;
	int32_t slot;
	uint64_t deadline;

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
	while (true)
	{
		(void)world.update_around(camera.x, camera.z, 0,
			WorldCoordinates::MIN_RENDER_DISTANCE);
		renderer.render_world(window.framebuffer(), camera, world,
			static_cast<const RenderDebug *>(nullptr));
		chunk = world.find_chunk(0, 0);
		if (chunk != nullptr && chunk->initialized
			&& chunk->mesh_revision != 0U)
			break ;
		if (static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count())
			>= deadline)
		{
			std::fprintf(stderr, "renderer-publication: initial mesh timeout\n");
			world.destroy();
			(void)window.destroy();
			return (1);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	slot = static_cast<int32_t>(chunk - world.chunks);
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
	while (true)
	{
		(void)world.update_around(camera.x, camera.z, 0,
			WorldCoordinates::MIN_RENDER_DISTANCE);
		renderer.render_world(window.framebuffer(), camera, world,
			static_cast<const RenderDebug *>(nullptr));
		chunk = world.find_chunk(0, 0);
		if (chunk != nullptr && renderer.get_gpu_renderer()
			->uploaded_identity_matches(slot, chunk->mesh_revision,
				chunk->chunk_x, chunk->chunk_z, chunk->voxel_revision)
			&& chunk->voxel_revision > 1U)
			break ;
		if (static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count())
			>= deadline)
		{
			std::fprintf(stderr,
				"renderer-publication: edit GPU upload timeout\n");
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
