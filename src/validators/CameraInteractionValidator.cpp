#include "../../src/validators/CameraInteractionValidator.hpp"
#include "../../src/interaction/BlockInteractor.hpp"
#include "../../src/app/GameSession.hpp"
#include <cmath>
#include <cstdio>
#include <memory>
#include <new>
#include <vector>

namespace
{
	static bool find_test_column(World &world, int32_t &world_x,
		int32_t &world_y, int32_t &world_z)
	{
		int32_t x;
		int32_t z;
		int32_t candidate_y;
		double surface_top;
		uint32_t block_id;
		uint32_t support_id;

		/* The fixed scan order and seed make this setup deterministic while
		 * allowing the validator to avoid a decorative block at the surface. */
		z = -8;
		while (z <= -1)
		{
			x = 0;
			while (x <= GAME_VOXEL_CHUNK_WIDTH - 1)
			{
				if (world.surface_top_at(x, z, &surface_top))
				{
					candidate_y = static_cast<int32_t>(std::floor(surface_top));
					if (candidate_y > 0
						&& candidate_y < GAME_VOXEL_CHUNK_HEIGHT - 1
						&& world.block_id_at(x, candidate_y, z, &block_id)
						&& block_id == GAME_VOXEL_AIR_BLOCK
						&& world.block_id_at(x, candidate_y - 1, z,
							&support_id)
						&& voxel_block_is_solid(support_id) == FT_TRUE)
					{
						world_x = x;
						world_y = candidate_y;
						world_z = z;
						return (true);
					}
				}
				x += 1;
			}
			z += 1;
		}
		return (false);
	}

	static int fail_validation(const char *stage, int32_t error_code)
	{
		std::fprintf(stderr,
			"camera-interaction: %s failed error=%d\n", stage, error_code);
		return (1);
	}
}

CameraInteractionValidator::CameraInteractionValidator()
{
}

CameraInteractionValidator::CameraInteractionValidator(
	const CameraInteractionValidator &other) : IValidator(other)
{
	*this = other;
}

CameraInteractionValidator::~CameraInteractionValidator()
{
}

CameraInteractionValidator &CameraInteractionValidator::operator=(
	const CameraInteractionValidator &other)
{
	(void)other;
	return (*this);
}

int CameraInteractionValidator::validate() const
{
	std::unique_ptr<World> world_storage(new (std::nothrow) World());
	if (world_storage == nullptr)
		return (ApplicationError::fail("camera interaction world allocation",
			FT_ERR_NO_MEMORY));
	World &world = *world_storage;
	Camera camera;
	CameraInput input;
	const double epsilon = 1.0e-9;
	const uint32_t test_block_id = VOXEL_GENERATOR_STONE_BLOCK;
	int32_t world_x;
	int32_t world_y;
	int32_t world_z;
	int32_t hit_x;
	int32_t hit_y;
	int32_t hit_z;
	int32_t place_x;
	int32_t place_y;
	int32_t place_z;
	int32_t error_code;
	uint32_t hit_id;
	uint32_t selected_id;
	uint32_t block_id;
	uint32_t support_id;
	uint64_t fluid_revision;
	std::vector<uint8_t> fluid_state;
	std::vector<uint8_t> corrupted_fluid_state;
	std::vector<uint8_t> restored_fluid_state;
	std::vector<uint8_t> furnace_block_state;
	double direction_x;
	double direction_y;
	double direction_z;

	error_code = world.initialize("camera-interaction-validator");
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail(
			"camera interaction world initialization", error_code));
	if (find_test_column(world, world_x, world_y, world_z) == false)
		return (fail_validation("deterministic test column", FT_ERR_NOT_FOUND));
	if (voxel_block_is_solid(test_block_id) != FT_TRUE
		|| voxel_block_is_breakable(test_block_id) != FT_TRUE)
		return (fail_validation("test block metadata", FT_ERR_INVALID_ARGUMENT));
	error_code = world.place_block_at(world_x, world_y, world_z,
		test_block_id);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("camera interaction setup block",
			error_code));
	if (world.block_id_at(world_x, world_y, world_z, &block_id) == false
		|| block_id != test_block_id)
		return (fail_validation("setup block storage", FT_ERR_INTERNAL));
	if (world.block_id_at(world_x, world_y - 1, world_z, &support_id) == false
		|| voxel_block_is_solid(support_id) != FT_TRUE)
		return (fail_validation("support block", FT_ERR_INTERNAL));

	/* Exercise the public orientation update and its pitch clamp. */
	input.clear();
	input.look_down = true;
	camera.update(input, 10.0);
	if (std::fabs(camera.pitch + Camera::max_pitch()) > epsilon)
		return (fail_validation("public downward pitch clamp",
			FT_ERR_INVALID_OPERATION));
	camera.x = static_cast<double>(world_x) + 0.5;
	camera.y = static_cast<double>(world_y + 1)
		+ PlayerController::PLAYER_EYE_HEIGHT;
	camera.z = static_cast<double>(world_z) + 0.5;
	camera.yaw = 0.0;
	PlayerController::camera_forward(camera, &direction_x, &direction_y,
		&direction_z);

	/* Confirm the ordinary raycaster sees the exact block under the player
	 * before the interaction facade is allowed to edit anything. */
	error_code = world.raycast_solid(camera.x, camera.y, camera.z,
		direction_x, direction_y, direction_z, 10.0, &hit_x, &hit_y,
		&hit_z);
	if (error_code != FT_ERR_SUCCESS || hit_x != world_x
		|| hit_y != world_y || hit_z != world_z)
	{
		std::fprintf(stderr,
			"camera-interaction: initial ray hit error=%d hit=(%d,%d,%d) "
			"expected=(%d,%d,%d)\n", error_code, hit_x, hit_y, hit_z,
			world_x, world_y, world_z);
		return (1);
	}
	selected_id = GAME_VOXEL_AIR_BLOCK;
	error_code = BlockInteractor::try_pick_target_block(&world, camera,
		&selected_id);
	if (error_code != FT_ERR_SUCCESS || selected_id != test_block_id)
		return (fail_validation("ordinary target selection", error_code));

	/* This is the same public path used by gameplay for block breaking. */
	error_code = BlockInteractor::try_delete_target_block(&world, camera);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("ordinary underfoot delete", error_code));
	if (world.block_id_at(world_x, world_y, world_z, &block_id) == false
		|| block_id != GAME_VOXEL_AIR_BLOCK)
		return (fail_validation("underfoot block removal", FT_ERR_INTERNAL));
	if (world.block_id_at(world_x, world_y - 1, world_z, &block_id) == false
		|| block_id != support_id)
		return (fail_validation("underfoot support preservation",
			FT_ERR_INTERNAL));

	/* After the break, the top face of the unchanged support must be the
	 * receiving face for the original cell. */
	error_code = world.raycast_edit_target(camera.x, camera.y, camera.z,
		direction_x, direction_y, direction_z, 10.0, &hit_x, &hit_y,
		&hit_z, &place_x, &place_y, &place_z, &hit_id);
	if (error_code != FT_ERR_SUCCESS || hit_x != world_x
		|| hit_y != world_y - 1 || hit_z != world_z
		|| place_x != world_x || place_y != world_y || place_z != world_z
		|| hit_id != support_id
		|| PlayerController::block_overlaps_player(camera, place_x, place_y,
			place_z))
	{
		std::fprintf(stderr,
			"camera-interaction: receiving cell invalid error=%d "
			"hit=(%d,%d,%d) place=(%d,%d,%d) expected_hit=(%d,%d,%d) "
			"expected_place=(%d,%d,%d)\n", error_code, hit_x, hit_y, hit_z,
			place_x, place_y, place_z, world_x, world_y - 1, world_z,
			world_x, world_y, world_z);
		return (1);
	}

	/* Restore the test platform through the ordinary placement interaction. */
	if (BlockInteractor::try_place_selected_block(&world, camera,
			VOXEL_GENERATOR_WATER_BLOCK) != FT_ERR_PERMISSION_DENIED
		|| BlockInteractor::try_place_selected_block(&world, camera,
			VOXEL_GENERATOR_LAVA_BLOCK) != FT_ERR_PERMISSION_DENIED
		|| BlockInteractor::try_place_selected_block(&world, camera,
			VOXEL_GENERATOR_FLOWING_WATER_BLOCK) != FT_ERR_PERMISSION_DENIED
		|| BlockInteractor::try_place_selected_block(&world, camera,
			VOXEL_GENERATOR_FLOWING_LAVA_BLOCK) != FT_ERR_PERMISSION_DENIED
		|| world.block_id_at(world_x, world_y, world_z, &block_id) == false
		|| block_id != GAME_VOXEL_AIR_BLOCK)
		return (fail_validation("fluid requires bucket transaction",
			FT_ERR_INVALID_STATE));
	{
		const int32_t fluid_chunk_x = WorldCoordinates::floor_divide(world_x,
			GAME_VOXEL_CHUNK_WIDTH);
		const int32_t fluid_chunk_z = WorldCoordinates::floor_divide(world_z,
			GAME_VOXEL_CHUNK_DEPTH);
		WorldChunk *fluid_chunk = world.find_chunk_mutable(fluid_chunk_x,
			fluid_chunk_z);

		if (fluid_chunk == nullptr)
			return (fail_validation("fluid transaction chunk",
				FT_ERR_NOT_FOUND));
		fluid_revision = fluid_chunk->voxel_revision;
		if (world.replace_fluid_source_at(world_x, world_y, world_z,
				GAME_VOXEL_AIR_BLOCK,
				VOXEL_GENERATOR_FLOWING_WATER_BLOCK_ID,
				fluid_revision) != FT_ERR_INVALID_ARGUMENT)
			return (fail_validation("flowing fluid source guard",
				FT_ERR_INVALID_STATE));
		error_code = world.replace_fluid_source_at(world_x, world_y, world_z,
			GAME_VOXEL_AIR_BLOCK, VOXEL_GENERATOR_WATER_BLOCK_ID,
			fluid_revision);
		if (error_code != FT_ERR_SUCCESS
			|| world.block_id_at(world_x, world_y, world_z, &block_id) == false
			|| block_id != VOXEL_GENERATOR_WATER_BLOCK_ID)
			return (fail_validation("fluid source placement",
				error_code == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE : error_code));
		if (world.replace_fluid_source_at(world_x, world_y, world_z,
				VOXEL_GENERATOR_WATER_BLOCK_ID, GAME_VOXEL_AIR_BLOCK,
				fluid_revision) != FT_ERR_INVALID_STATE
			|| world.block_id_at(world_x, world_y, world_z, &block_id) == false
			|| block_id != VOXEL_GENERATOR_WATER_BLOCK_ID)
			return (fail_validation("stale fluid transaction",
				FT_ERR_INVALID_STATE));
		error_code = world.replace_fluid_source_at(world_x, world_y, world_z,
			VOXEL_GENERATOR_WATER_BLOCK_ID, GAME_VOXEL_AIR_BLOCK,
			fluid_chunk->voxel_revision);
		if (error_code != FT_ERR_SUCCESS
			|| world.block_id_at(world_x, world_y, world_z, &block_id) == false
			|| block_id != GAME_VOXEL_AIR_BLOCK)
			return (fail_validation("fluid source removal",
				error_code == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE : error_code));
		error_code = world.serialize_fluid_state(fluid_state);
		if (error_code != FT_ERR_SUCCESS || fluid_state.size() < 20U)
			return (fail_validation("fluid state serialize", error_code));
		corrupted_fluid_state = fluid_state;
		corrupted_fluid_state[12U] ^= 0x01U;
		if (world.deserialize_fluid_state(corrupted_fluid_state.data(),
				corrupted_fluid_state.size()) != FT_ERR_INVALID_ARGUMENT)
			return (fail_validation("fluid state checksum", FT_ERR_INVALID_STATE));
		error_code = world.serialize_fluid_state(restored_fluid_state);
		if (error_code != FT_ERR_SUCCESS || restored_fluid_state != fluid_state)
			return (fail_validation("fluid state rollback", error_code));
		error_code = world.replace_fluid_source_at(world_x, world_y, world_z,
			GAME_VOXEL_AIR_BLOCK, VOXEL_GENERATOR_WATER_BLOCK_ID,
			fluid_chunk->voxel_revision);
		if (error_code != FT_ERR_SUCCESS)
			return (fail_validation("fluid state reapply setup", error_code));
		error_code = world.deserialize_fluid_state(fluid_state.data(),
			fluid_state.size());
		if (error_code != FT_ERR_SUCCESS
			|| world.block_id_at(world_x, world_y, world_z, &block_id) == false
			|| block_id != GAME_VOXEL_AIR_BLOCK)
			return (fail_validation("fluid state restore",
				error_code == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE : error_code));
		fluid_chunk = world.find_chunk_mutable(fluid_chunk_x, fluid_chunk_z);
		if (fluid_chunk == nullptr)
			return (fail_validation("block state chunk", FT_ERR_NOT_FOUND));
		error_code = world.replace_block_state_at(world_x, world_y, world_z,
			GAME_VOXEL_AIR_BLOCK, VOXEL_GENERATOR_FURNACE_BLOCK_ID,
			fluid_chunk->voxel_revision);
		if (error_code == FT_ERR_SUCCESS)
			error_code = world.serialize_fluid_state(furnace_block_state);
		if (error_code != FT_ERR_SUCCESS || furnace_block_state.size() < 20U
			|| furnace_block_state[4U] != 2U
			|| furnace_block_state[5U] != 0U
			|| furnace_block_state[6U] != 0U
			|| furnace_block_state[7U] != 0U)
			return (fail_validation("generic block state serialize",
				error_code == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE : error_code));
		error_code = world.deserialize_fluid_state(furnace_block_state.data(),
			furnace_block_state.size());
		if (error_code != FT_ERR_SUCCESS
			|| world.block_id_at(world_x, world_y, world_z, &block_id) == false
			|| block_id != VOXEL_GENERATOR_FURNACE_BLOCK_ID)
			return (fail_validation("generic block state restore",
				error_code == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE : error_code));
		const WorldChunk *station_chunk = world.find_chunk(fluid_chunk_x,
			fluid_chunk_z);
		if (station_chunk == nullptr
			|| !GameSession::authorize_local_processing_station(
				static_cast<uint32_t>(MinecraftRecipeCatalog::FURNACE_STATION_ID),
				world_x, world_y, world_z, station_chunk->voxel_revision,
				camera.x, camera.y, camera.z, &world)
			|| GameSession::authorize_local_processing_station(0U, world_x,
				world_y, world_z, station_chunk->voxel_revision, camera.x,
				camera.y, camera.z, &world)
			|| GameSession::authorize_local_processing_station(
				static_cast<uint32_t>(MinecraftRecipeCatalog::FURNACE_STATION_ID),
				world_x, world_y, world_z, station_chunk->voxel_revision - 1U,
				camera.x, camera.y, camera.z, &world)
			|| GameSession::authorize_local_processing_station(
				static_cast<uint32_t>(MinecraftRecipeCatalog::FURNACE_STATION_ID),
				world_x, world_y, world_z, 0U, camera.x, camera.y, camera.z,
				&world)
			|| GameSession::authorize_local_processing_station(
				static_cast<uint32_t>(MinecraftRecipeCatalog::FURNACE_STATION_ID),
				world_x, world_y, world_z, station_chunk->voxel_revision,
				camera.x + 7.0, camera.y, camera.z, &world))
			return (fail_validation("local furnace station authorization",
				FT_ERR_PERMISSION_DENIED));
		{
			const uint32_t station_ids[] = {
				static_cast<uint32_t>(
					MinecraftRecipeCatalog::WORKBENCH_STATION_ID),
				static_cast<uint32_t>(
					MinecraftRecipeCatalog::PAPER_MILL_STATION_ID),
				static_cast<uint32_t>(
					MinecraftRecipeCatalog::CARD_PRESS_STATION_ID)};
			const uint32_t station_blocks[] = {
				VOXEL_GENERATOR_WORKBENCH_BLOCK_ID,
				VOXEL_GENERATOR_PAPER_MILL_BLOCK_ID,
				VOXEL_GENERATOR_CARD_PRESS_BLOCK_ID};
			for (std::size_t index = 0U;
				index < sizeof(station_ids) / sizeof(station_ids[0]); ++index)
			{
				station_chunk = world.find_chunk(fluid_chunk_x, fluid_chunk_z);
				if (station_chunk == nullptr)
					return (fail_validation("station fixture chunk",
						FT_ERR_NOT_FOUND));
				error_code = world.replace_block_state_at(world_x, world_y,
					world_z, VOXEL_GENERATOR_FURNACE_BLOCK_ID,
					station_blocks[index], station_chunk->voxel_revision);
				if (error_code != FT_ERR_SUCCESS)
					return (fail_validation("replace workstation fixture",
						error_code));
				station_chunk = world.find_chunk(fluid_chunk_x, fluid_chunk_z);
				if (station_chunk == nullptr
					|| !GameSession::authorize_local_processing_station(
						station_ids[index], world_x, world_y, world_z,
						station_chunk == nullptr ? 0U
							: station_chunk->voxel_revision,
						camera.x, camera.y, camera.z, &world))
					return (fail_validation("workstation station authorization",
						FT_ERR_PERMISSION_DENIED));
				error_code = world.replace_block_state_at(world_x, world_y,
					world_z, station_blocks[index],
					VOXEL_GENERATOR_FURNACE_BLOCK_ID,
					station_chunk->voxel_revision);
				if (error_code != FT_ERR_SUCCESS)
					return (fail_validation("restore furnace fixture",
						error_code));
			}
		}
		error_code = world.set_chunk_protected(fluid_chunk_x,
			fluid_chunk_z, true);
		if (error_code != FT_ERR_SUCCESS)
			return (fail_validation("protect furnace station chunk",
				error_code));
		const bool protected_station_allowed =
			GameSession::authorize_local_processing_station(
				static_cast<uint32_t>(MinecraftRecipeCatalog::FURNACE_STATION_ID),
				world_x, world_y, world_z, station_chunk->voxel_revision,
				camera.x, camera.y, camera.z, &world);
		const int32_t unprotect_error = world.set_chunk_protected(
			fluid_chunk_x, fluid_chunk_z, false);
		if (!protected_station_allowed || unprotect_error != FT_ERR_SUCCESS)
			return (fail_validation("protected furnace station access",
				unprotect_error == FT_ERR_SUCCESS
					? FT_ERR_PERMISSION_DENIED : unprotect_error));
		WorldChunk *mutable_station_chunk = world.find_chunk_mutable(
			fluid_chunk_x, fluid_chunk_z);
		if (mutable_station_chunk == nullptr)
			return (fail_validation("mutable furnace station chunk",
				FT_ERR_NOT_FOUND));
		error_code = world.replace_block_state_at(world_x, world_y, world_z,
			VOXEL_GENERATOR_FURNACE_BLOCK_ID, GAME_VOXEL_AIR_BLOCK,
			mutable_station_chunk->voxel_revision);
		if (error_code != FT_ERR_SUCCESS)
			return (fail_validation("remove furnace authorization fixture",
				error_code));
		station_chunk = world.find_chunk(fluid_chunk_x, fluid_chunk_z);
		if (station_chunk == nullptr
			|| GameSession::authorize_local_processing_station(
				static_cast<uint32_t>(MinecraftRecipeCatalog::FURNACE_STATION_ID),
				world_x, world_y, world_z, station_chunk->voxel_revision,
				camera.x, camera.y, camera.z, &world))
			return (fail_validation("removed furnace station denial",
				FT_ERR_PERMISSION_DENIED));
		error_code = world.deserialize_fluid_state(fluid_state.data(),
			fluid_state.size());
		if (error_code != FT_ERR_SUCCESS
			|| world.block_id_at(world_x, world_y, world_z, &block_id) == false
			|| block_id != GAME_VOXEL_AIR_BLOCK)
			return (fail_validation("generic block state rollback",
				error_code == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE : error_code));
	}
	error_code = BlockInteractor::try_place_selected_block(&world, camera,
		test_block_id);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("ordinary underfoot placement",
			error_code));
	if (world.block_id_at(world_x, world_y, world_z, &block_id) == false
		|| block_id != test_block_id)
		return (fail_validation("underfoot block restoration", FT_ERR_INTERNAL));
	std::printf("camera-interaction: ok block=(%d,%d,%d) support=%u\n",
		world_x, world_y, world_z, support_id);
	return (0);
}
