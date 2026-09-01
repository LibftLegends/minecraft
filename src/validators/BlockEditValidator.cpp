#include "../../src/validators/BlockEditValidator.hpp"

BlockEditValidator::BlockEditValidator()
{
}

BlockEditValidator::BlockEditValidator(const BlockEditValidator &other)
	: IValidator(other)
{
	*this = other;
}

BlockEditValidator::~BlockEditValidator()
{
}

BlockEditValidator &BlockEditValidator::operator=(const BlockEditValidator &other)
{
	(void)other;
	return (*this);
}

size_t BlockEditValidator::mesh_index_count_for_block(const World &world,
	int32_t world_x, int32_t world_z)
{
	int32_t	chunk_x;
	int32_t	chunk_z;
	int32_t	index;

	chunk_x = world_x >= 0 ? world_x / GAME_VOXEL_CHUNK_WIDTH : -(((-world_x)
				+ GAME_VOXEL_CHUNK_WIDTH - 1) / GAME_VOXEL_CHUNK_WIDTH);
	chunk_z = world_z >= 0 ? world_z / GAME_VOXEL_CHUNK_DEPTH : -(((-world_z)
				+ GAME_VOXEL_CHUNK_DEPTH - 1) / GAME_VOXEL_CHUNK_DEPTH);
	index = 0;
	while (index < world.chunk_count)
	{
		if (world.chunks[index].initialized == true
			&& world.chunks[index].chunk_x == chunk_x
			&& world.chunks[index].chunk_z == chunk_z)
			return (world.chunks[index].mesh.indices.size());
		index = index + 1;
	}
	return (0U);
}

int BlockEditValidator::prepare_edit_target(World &world, int32_t x, int32_t z,
	int32_t &y, size_t &mesh_before)
{
	double	surface_top;

	if (world.surface_top_at(x, z, &surface_top) == false)
	{
		std::fprintf(stderr, "block-edit: failed surface lookup\n");
		return (1);
	}
	y = static_cast<int32_t>(std::floor(surface_top + 1.0));
	mesh_before = mesh_index_count_for_block(world, x, z);
	return (0);
}

int BlockEditValidator::verify_place_block(World &world, int32_t x, int32_t y,
	int32_t z, size_t &mesh_after)
{
	uint32_t	block_id;
	int32_t		error_code;

	error_code = world.place_block_at(x, y, z, VOXEL_GENERATOR_STONE_BLOCK);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit place", error_code));
	if (world.block_id_at(x, y, z, &block_id) == false
		|| block_id != VOXEL_GENERATOR_STONE_BLOCK)
	{
		std::fprintf(stderr,
			"block-edit: placed block not visible in storage\n");
		return (1);
	}
	mesh_after = mesh_index_count_for_block(world, x, z);
	return (0);
}

int BlockEditValidator::verify_delete_block(World &world, int32_t x, int32_t y,
	int32_t z, size_t &mesh_after)
{
	uint32_t	block_id;
	int32_t		error_code;

	error_code = world.delete_block_at(x, y, z);
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit delete", error_code));
	if (world.block_id_at(x, y, z, &block_id) == false
		|| block_id != GAME_VOXEL_AIR_BLOCK)
	{
		std::fprintf(stderr,
			"block-edit: deleted block not visible in storage\n");
		return (1);
	}
	mesh_after = mesh_index_count_for_block(world, x, z);
	return (0);
}

int BlockEditValidator::report_mesh_result(size_t before, size_t after_place,
	size_t after_delete)
{
	if (after_place == before || after_delete != before)
	{
		std::fprintf(stderr,
						"block-edit: mesh counts did not update"
						" before=%zu place=%zu delete=%zu\n",
						before,
						after_place,
						after_delete);
		return (1);
	}
	std::printf("block-edit: ok before=%zu place=%zu delete=%zu\n", before,
		after_place, after_delete);
	return (0);
}

int BlockEditValidator::validate() const
{
	World world;
	int32_t edit_x;
	int32_t edit_y;
	int32_t edit_z;
	size_t mesh_before;
	size_t mesh_after_place;
	size_t mesh_after_delete;
	int32_t error_code;

	edit_x = 2;
	edit_z = -6;
	error_code = world.initialize("integration-seed");
	if (error_code != FT_ERR_SUCCESS)
		return (ApplicationError::fail("block-edit world initialization",
				error_code));
	if (prepare_edit_target(world, edit_x, edit_z, edit_y, mesh_before) != 0
		|| verify_place_block(world, edit_x, edit_y, edit_z,
			mesh_after_place) != 0 || verify_delete_block(world, edit_x, edit_y,
			edit_z, mesh_after_delete) != 0 || report_mesh_result(mesh_before,
			mesh_after_place, mesh_after_delete) != 0)
	{
		world.destroy();
		return (1);
	}
	world.destroy();
	return (0);
}
