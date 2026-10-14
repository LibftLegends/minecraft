#include "../../src/validators/TerrainDeterminismValidator.hpp"

static int chunk_contains_paper_reed(const game_voxel_chunk &chunk,
	bool *found_out)
{
	uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
	if (found_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*found_out = false;
	for (int32_t z = 0; z < GAME_VOXEL_CHUNK_DEPTH; ++z)
	{
		for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
		{
			for (int32_t x = 0; x < GAME_VOXEL_CHUNK_WIDTH; ++x)
			{
				const int32_t result = chunk.read_block(x, y, z, &block_id);
				if (result != FT_ERR_SUCCESS)
					return (result);
				if (block_id == VOXEL_GENERATOR_PAPER_REED_BLOCK_ID
					|| block_id
						== VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID)
				{
					*found_out = true;
					return (FT_ERR_SUCCESS);
				}
			}
		}
	}
	return (FT_ERR_SUCCESS);
}

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
		error_code = voxel_generate_chunk(first, world_x, world_z, seed);
	if (error_code == FT_ERR_SUCCESS)
		error_code = voxel_generate_chunk(second, world_x, world_z, seed);
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
	bool paper_reed_found = false;
	voxel_generation_config wet_generation_config;
	if (voxel_default_generation_config(wet_generation_config)
		!= FT_ERR_SUCCESS
		|| wet_generation_config.set_water_chance_percent(100U)
			!= FT_ERR_SUCCESS)
		return (1);
	for (uint32_t seed_index = 0U; seed_index < 16U && !paper_reed_found;
		++seed_index)
	{
		game_voxel_chunk chunk;
		char seed[48];
		if (chunk.initialize() != FT_ERR_SUCCESS)
			return (1);
		std::snprintf(seed, sizeof(seed), "paper-reed-seed-%u", seed_index);
		const int32_t sample_x = static_cast<int32_t>(seed_index % 4U) * 4096;
		const int32_t sample_z = static_cast<int32_t>(seed_index / 4U) * 4096;
		error_code = voxel_generate_chunk(chunk, sample_x, sample_z, seed,
			wet_generation_config);
		if (error_code == FT_ERR_SUCCESS)
			error_code = chunk_contains_paper_reed(chunk, &paper_reed_found);
		(void)chunk.destroy();
		if (error_code != FT_ERR_SUCCESS)
			return (1);
	}
	if (!paper_reed_found)
	{
		std::fprintf(stderr,
			"terrain-determinism: paper reed did not spawn in 16 seeded samples\n");
		return (1);
	}
	std::printf("terrain-determinism: ok sampled_chunks=3 paper_reed=natural\n");
	return (0);
}
