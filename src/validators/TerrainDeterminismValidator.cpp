#include "../../src/validators/TerrainDeterminismValidator.hpp"

TerrainDeterminismValidator::TerrainDeterminismValidator()
{
}

TerrainDeterminismValidator::TerrainDeterminismValidator(const TerrainDeterminismValidator &other)
	: IValidator(other)
{
	*this = other;
}

TerrainDeterminismValidator::~TerrainDeterminismValidator()
{
}

TerrainDeterminismValidator &TerrainDeterminismValidator::operator=(const TerrainDeterminismValidator &other)
{
	(void)other;
	return (*this);
}

int TerrainDeterminismValidator::compare_chunks(const game_voxel_chunk &left,
	const game_voxel_chunk &right)
{
	int32_t		local_x;
	int32_t		local_y;
	int32_t		local_z;
	uint32_t	left_block;
	uint32_t	right_block;

	local_z = 0;
	while (local_z < GAME_VOXEL_CHUNK_DEPTH)
	{
		local_y = 0;
		while (local_y < GAME_VOXEL_CHUNK_HEIGHT)
		{
			local_x = 0;
			while (local_x < GAME_VOXEL_CHUNK_WIDTH)
			{
				if (left.read_block(local_x, local_y, local_z,
						&left_block) != FT_ERR_SUCCESS
					|| right.read_block(local_x, local_y, local_z,
						&right_block) != FT_ERR_SUCCESS
					|| left_block != right_block)
					return (1);
				local_x = local_x + 1;
			}
			local_y = local_y + 1;
		}
		local_z = local_z + 1;
	}
	return (0);
}

int TerrainDeterminismValidator::generate_pair(int32_t world_x, int32_t world_z,
	const char *seed)
{
	game_voxel_chunk	first;
	game_voxel_chunk	second;
	int32_t				error_code;

	error_code = first.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = second.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = terrain_generate_chunk(first, world_x, world_z, seed);
	if (error_code == FT_ERR_SUCCESS)
		error_code = terrain_generate_chunk(second, world_x, world_z, seed);
	if (error_code == FT_ERR_SUCCESS)
		error_code = compare_chunks(first, second);
	(void)first.destroy();
	(void)second.destroy();
	return (error_code);
}

int TerrainDeterminismValidator::validate() const
{
	int32_t positions[6];
	int32_t index;
	int32_t error_code;

	positions[0] = 0;
	positions[1] = 0;
	positions[2] = 8192;
	positions[3] = -8192;
	positions[4] = -16384;
	positions[5] = 16368;
	index = 0;
	while (index < 6)
	{
		error_code = generate_pair(positions[index], positions[index + 1],
				"integration-seed");
		if (error_code != FT_ERR_SUCCESS)
		{
			std::fprintf(stderr,
				"terrain-determinism: failed origin=(%d,%d) error=%d\n",
				positions[index], positions[index + 1], error_code);
			return (1);
		}
		index = index + 2;
	}
	std::printf("terrain-determinism: ok sampled_chunks=3\n");
	return (0);
}
