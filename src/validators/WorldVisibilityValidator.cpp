#include "../../src/validators/WorldVisibilityValidator.hpp"
#include <cmath>
#include <chrono>
#include <cstdio>

static void visibility_validation_phase(const char *phase,
	const std::chrono::steady_clock::time_point &started) noexcept
{
	const uint64_t elapsed_ms = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started).count());
	std::fprintf(stderr, "visible-distance: phase=%s elapsed_ms=%llu\n",
		phase, static_cast<unsigned long long>(elapsed_ms));
}

WorldVisibilityValidator::WorldVisibilityValidator()
{
}

WorldVisibilityValidator::WorldVisibilityValidator(const WorldVisibilityValidator &other)
	: IValidator(other)
{
	*this = other;
}

WorldVisibilityValidator::~WorldVisibilityValidator()
{
}

WorldVisibilityValidator &WorldVisibilityValidator::operator=(const WorldVisibilityValidator &other)
{
	(void)other;
	return (*this);
}

int WorldVisibilityValidator::validate() const
{
	World world;
	Camera validation_camera;
	int32_t error_code;
	const std::chrono::steady_clock::time_point validation_started =
		std::chrono::steady_clock::now();

	visibility_validation_phase("initialize-start", validation_started);
	error_code = world.initialize("integration-seed");
	if (error_code != FT_ERR_SUCCESS)
		return (1);
	visibility_validation_phase("initialize-complete", validation_started);
	validation_camera.initialize();
	PlayerController::spawn_player_on_ground(&validation_camera, world);
	visibility_validation_phase("full-stream-start", validation_started);
	error_code = world.update_around(validation_camera.x, validation_camera.z,
		WorldCoordinates::CHUNK_COUNT);
	visibility_validation_phase("full-stream-complete", validation_started);
	if (error_code != FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (validate_visible_distance(world, validation_camera.x,
			validation_camera.z, validation_camera.yaw,
			WorldCoordinates::REQUIRED_VISIBLE_DISTANCE) == false)
	{
		std::fprintf(stderr,
			"visible-distance: failed required=%d chunks_loaded=%d\n",
			WorldCoordinates::REQUIRED_VISIBLE_DISTANCE,
			world.loaded_chunk_count);
		world.destroy();
		return (1);
	}
	visibility_validation_phase("visible-distance-complete", validation_started);
	if (validate_height_invariant(world) == false)
	{
		world.destroy();
		return (1);
	}
	visibility_validation_phase("height-invariant-complete", validation_started);
	if (validate_streamed_mesh_drawability(world) == false)
	{
		world.destroy();
		return (1);
	}
	visibility_validation_phase("drawability-complete", validation_started);
	if (validate_streamed_culling_admission(world, validation_camera)
		== false)
	{
		world.destroy();
		return (1);
	}
	visibility_validation_phase("culling-admission-complete", validation_started);
	if (validate_same_coordinate_slot_reuse(world, validation_camera)
		== false)
	{
		world.destroy();
		return (1);
	}
	visibility_validation_phase("slot-reuse-complete", validation_started);
	/* Move the stream center by one chunk and rebuild it synchronously. This
	 * exercises storage-slot reuse and verifies that newly assigned chunks keep
	 * their coordinates, mesh bounds, and solid/water index partitions. */
	error_code = world.update_around(validation_camera.x
		+ static_cast<double>(GAME_VOXEL_CHUNK_WIDTH), validation_camera.z,
		WorldCoordinates::CHUNK_COUNT);
	visibility_validation_phase("recenter-complete", validation_started);
	if (error_code != FT_ERR_SUCCESS
		|| validate_height_invariant(world) == false
		|| validate_streamed_mesh_drawability(world) == false
		|| validate_streamed_culling_admission(world, validation_camera)
			== false)
	{
		std::fprintf(stderr,
			"visible-distance: recenter validation failed error=%d loaded=%d\n",
			error_code, world.loaded_chunk_count);
		world.destroy();
		return (1);
	}
	std::printf("visible-distance: ok required=%d chunks_loaded=%d\n",
		WorldCoordinates::REQUIRED_VISIBLE_DISTANCE, world.loaded_chunk_count);
	world.destroy();
	return (0);
}

bool WorldVisibilityValidator::validate_height_invariant(const World &world)
{
	int32_t chunk_index;
	int32_t local_x;
	int32_t local_y;
	int32_t local_z;
	int32_t highest_solid_y;
	uint32_t block_id;
	double collision_surface_y;
	int32_t center_x;
	int32_t center_z;

	chunk_index = 0;
	while (chunk_index < world.chunk_count)
	{
		const WorldChunk &chunk = world.chunks[chunk_index];

		if (chunk.initialized == true)
		{
			if (chunk.world_x != chunk.chunk_x * GAME_VOXEL_CHUNK_WIDTH
				|| chunk.world_z != chunk.chunk_z * GAME_VOXEL_CHUNK_DEPTH)
			{
				std::fprintf(stderr,
					"height-invariant: offset mismatch slot=%d chunk=(%d,%d) world=(%d,%d)\n",
					chunk_index, chunk.chunk_x, chunk.chunk_z,
					chunk.world_x, chunk.world_z);
				return (false);
			}
			highest_solid_y = -1;
			local_z = 0;
			while (local_z < GAME_VOXEL_CHUNK_DEPTH)
			{
				local_x = 0;
				while (local_x < GAME_VOXEL_CHUNK_WIDTH)
				{
					local_y = GAME_VOXEL_CHUNK_HEIGHT - 1;
					while (local_y >= 0)
					{
						if (chunk.chunk.read_block(local_x, local_y,
							local_z, &block_id) != FT_ERR_SUCCESS)
							return (false);
						if (block_id != GAME_VOXEL_AIR_BLOCK)
						{
							if (local_y > highest_solid_y)
								highest_solid_y = local_y;
							break;
						}
						local_y -= 1;
					}
					local_x += 1;
				}
				local_z += 1;
			}
			if (highest_solid_y >= 0
				&& (chunk.mesh.has_occupied_bounds == FT_FALSE
					|| chunk.mesh.occupied_bounds.maximum_y
						!= highest_solid_y + 1))
			{
				std::fprintf(stderr,
					"height-invariant: mesh mismatch slot=%d chunk=(%d,%d) solid_top=%d mesh_max_y=%d\n",
					chunk_index, chunk.chunk_x, chunk.chunk_z,
					highest_solid_y,
					chunk.mesh.occupied_bounds.maximum_y);
				return (false);
			}
			if (chunk.mesh.indices.size()
				!= chunk.mesh.solid_indices.size()
					+ chunk.mesh.water_indices.size())
			{
				std::fprintf(stderr,
					"mesh-invariant: partition mismatch slot=%d chunk=(%d,%d) "
					"indices=%zu solid=%zu water=%zu\n", chunk_index,
					chunk.chunk_x, chunk.chunk_z, chunk.mesh.indices.size(),
					chunk.mesh.solid_indices.size(),
					chunk.mesh.water_indices.size());
				return (false);
			}
			center_x = chunk.world_x + (GAME_VOXEL_CHUNK_WIDTH / 2);
			center_z = chunk.world_z + (GAME_VOXEL_CHUNK_DEPTH / 2);
			if (world.surface_top_at(center_x, center_z,
					&collision_surface_y) == false)
				return (false);
		}
		chunk_index += 1;
	}
	return (true);
}

bool WorldVisibilityValidator::validate_streamed_mesh_drawability(
	const World &world)
{
	int32_t index;

	index = 0;
	while (index < world.chunk_count)
	{
		const WorldChunk &chunk = world.chunks[index];
		if (chunk.initialized == true
			&& chunk.mesh.has_occupied_bounds == FT_TRUE
			&& (chunk.mesh.vertices.empty() == FT_TRUE
				|| (chunk.mesh.solid_indices.empty() == FT_TRUE
					&& chunk.mesh.water_indices.empty() == FT_TRUE)))
		{
			std::fprintf(stderr,
				"drawability-invariant: empty draw payload slot=%d "
				"chunk=(%d,%d) vertices=%zu solid=%zu water=%zu\n", index,
				chunk.chunk_x, chunk.chunk_z, chunk.mesh.vertices.size(),
				chunk.mesh.solid_indices.size(),
				chunk.mesh.water_indices.size());
			return (false);
		}
		index += 1;
	}
	return (true);
}

bool WorldVisibilityValidator::validate_streamed_culling_admission(
	const World &world, const Camera &camera)
{
	RenderCache cache;
	int32_t index;
	double closest_x;
	double closest_z;
	double distance_squared;
	double envelope;

	cache.configure(camera, 1280, 720, world.active_render_distance);
	envelope = cache.render_distance + std::sqrt(static_cast<double>(
		GAME_VOXEL_CHUNK_WIDTH * GAME_VOXEL_CHUNK_WIDTH
		+ GAME_VOXEL_CHUNK_DEPTH * GAME_VOXEL_CHUNK_DEPTH));
	index = 0;
	while (index < world.chunk_count)
	{
		const WorldChunk &chunk = world.chunks[index];
		if (chunk.initialized == true
			&& chunk.mesh.has_occupied_bounds == FT_TRUE)
		{
			closest_x = camera.x;
			if (closest_x < static_cast<double>(chunk.world_x))
				closest_x = static_cast<double>(chunk.world_x);
			else if (closest_x > static_cast<double>(chunk.world_x
				+ GAME_VOXEL_CHUNK_WIDTH))
				closest_x = static_cast<double>(chunk.world_x
					+ GAME_VOXEL_CHUNK_WIDTH);
			closest_z = camera.z;
			if (closest_z < static_cast<double>(chunk.world_z))
				closest_z = static_cast<double>(chunk.world_z);
			else if (closest_z > static_cast<double>(chunk.world_z
				+ GAME_VOXEL_CHUNK_DEPTH))
				closest_z = static_cast<double>(chunk.world_z
					+ GAME_VOXEL_CHUNK_DEPTH);
			distance_squared = (closest_x - camera.x)
			* (closest_x - camera.x)
			+ (closest_z - camera.z) * (closest_z - camera.z);
			if (distance_squared <= envelope * envelope
				&& !MeshCuller::chunk_is_visible(camera, chunk, cache))
			{
				std::fprintf(stderr,
					"culling-admission: occupied streamed chunk rejected "
					"slot=%d chunk=(%d,%d) world=(%d,%d) camera=(%.2f,%.2f) "
					"render_distance=%.2f\n", index, chunk.chunk_x,
					chunk.chunk_z, chunk.world_x, chunk.world_z, camera.x,
					camera.z, cache.render_distance);
				return (false);
			}
		}
		index += 1;
	}
	return (true);
}

bool WorldVisibilityValidator::validate_same_coordinate_slot_reuse(
	World &world, const Camera &camera)
{
	const WorldChunk *before;
	const WorldChunk *after;
	const uint64_t before_revision = world.find_chunk(0, 0) != nullptr
		? world.find_chunk(0, 0)->mesh_revision : 0U;
	int32_t error_code;

	before = world.find_chunk(0, 0);
	if (before == nullptr || before_revision == 0U)
	{
		std::fprintf(stderr,
			"slot-reuse: initial origin chunk is unavailable\n");
		return (false);
	}
	error_code = world.update_around(
		camera.x + static_cast<double>(GAME_VOXEL_CHUNK_WIDTH * 13),
		camera.z, WorldCoordinates::CHUNK_COUNT);
	if (error_code != FT_ERR_SUCCESS || world.find_chunk(0, 0) != nullptr)
	{
		std::fprintf(stderr,
			"slot-reuse: origin chunk was not evicted error=%d\n", error_code);
		return (false);
	}
	error_code = world.update_around(camera.x, camera.z,
		WorldCoordinates::CHUNK_COUNT);
	after = world.find_chunk(0, 0);
	if (error_code != FT_ERR_SUCCESS || after == nullptr
		|| after->mesh_revision == before_revision
		|| after->mesh.has_occupied_bounds == FT_FALSE)
	{
		std::fprintf(stderr,
			"slot-reuse: regenerated origin identity invalid error=%d "
			"before=%llu after=%llu present=%d occupied=%d\n", error_code,
			static_cast<unsigned long long>(before_revision),
			after == nullptr ? 0ULL
				: static_cast<unsigned long long>(after->mesh_revision),
			after != nullptr ? 1 : 0,
			after != nullptr && after->mesh.has_occupied_bounds
				== FT_TRUE ? 1 : 0);
		return (false);
	}
	return (true);
}

bool WorldVisibilityValidator::validate_visible_distance(const World &world,
	double camera_x, double camera_z, double yaw, int32_t required_distance)
{
	double	forward_x;
	double	forward_z;
	double	center_x;
	double	center_z;
	double	delta_x;
	double	delta_z;
	double	forward_distance;
	double	best_distance;
	int32_t	index;

	if (required_distance <= 0)
		return (false);
	forward_x = std::sin(yaw);
	forward_z = std::cos(yaw);
	best_distance = 0.0;
	index = 0;
	while (index < world.chunk_count)
	{
		if (world.chunks[index].initialized == true)
		{
			center_x = static_cast<double>(world.chunks[index].world_x)
				+ (static_cast<double>(GAME_VOXEL_CHUNK_WIDTH) * 0.5);
			center_z = static_cast<double>(world.chunks[index].world_z)
				+ (static_cast<double>(GAME_VOXEL_CHUNK_DEPTH) * 0.5);
			delta_x = center_x - camera_x;
			delta_z = center_z - camera_z;
			forward_distance = (delta_x * forward_x) + (delta_z * forward_z);
			if (forward_distance > best_distance)
				best_distance = forward_distance;
		}
		index = index + 1;
	}
	return (best_distance >= static_cast<double>(required_distance));
}
