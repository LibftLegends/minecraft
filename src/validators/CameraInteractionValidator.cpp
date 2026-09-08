#include "../../src/validators/CameraInteractionValidator.hpp"
#include "../../src/interaction/BlockInteractor.hpp"
#include <cmath>
#include <cstdio>

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
	World world;
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
