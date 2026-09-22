#include "../../src/validators/TerrainCaveValidator.hpp"

TerrainCaveValidator::TerrainCaveValidator()
{
}

TerrainCaveValidator::TerrainCaveValidator(const TerrainCaveValidator &other)
	: IValidator(other)
{
	*this = other;
}

TerrainCaveValidator::~TerrainCaveValidator()
{
}

TerrainCaveValidator &TerrainCaveValidator::operator=(const TerrainCaveValidator &other)
{
	(void)other;
	return (*this);
}

bool TerrainCaveValidator::chunk_has_cave(const game_voxel_chunk &chunk)
{
	int32_t		local_x;
	int32_t		local_y;
	int32_t		local_z;
	int32_t		surface_y;
	uint32_t	block_id;

	local_z = 0;
	while (local_z < GAME_VOXEL_CHUNK_DEPTH)
	{
		local_x = 0;
		while (local_x < GAME_VOXEL_CHUNK_WIDTH)
		{
			surface_y = GAME_VOXEL_CHUNK_HEIGHT - 1;
			while (surface_y >= 0)
			{
				if (chunk.read_block(local_x, surface_y, local_z,
						&block_id) != FT_ERR_SUCCESS)
					return (false);
				if (block_id != GAME_VOXEL_AIR_BLOCK)
					break ;
				surface_y = surface_y - 1;
			}
			local_y = VOXEL_GENERATOR_SURFACE_HEIGHT / 4;
			while (local_y < surface_y - 8)
			{
				if (chunk.read_block(local_x, local_y, local_z,
						&block_id) != FT_ERR_SUCCESS)
					return (false);
				if (block_id == GAME_VOXEL_AIR_BLOCK)
					return (true);
				local_y = local_y + 1;
			}
			local_x = local_x + 1;
		}
		local_z = local_z + 1;
	}
	return (false);
}

int TerrainCaveValidator::validate_origin(int32_t world_x, int32_t world_z,
	bool *found_cave)
{
	game_voxel_chunk	chunk;
	int32_t				error_code;

	error_code = chunk.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = voxel_generate_chunk(chunk, world_x, world_z,
				"integration-seed");
	if (error_code == FT_ERR_SUCCESS && chunk_has_cave(chunk) == true)
		*found_cave = true;
	(void)chunk.destroy();
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "terrain-caves: failed origin=(%d,%d) error=%d\n",
			world_x, world_z, error_code);
		return (1);
	}
	return (0);
}

int TerrainCaveValidator::validate() const
{
	bool found_cave;

	found_cave = false;
	if (validate_origin(0, 0, &found_cave) != 0 || validate_origin(512, 512,
			&found_cave) != 0 || validate_origin(-1024, 768, &found_cave) != 0
		|| validate_origin(4096, -4096, &found_cave) != 0)
		return (1);
	if (found_cave == false)
	{
		std::fprintf(stderr,
			"terrain-caves: failed no underground air found\n");
		return (1);
	}
	std::printf("terrain-caves: ok sampled_chunks=4\n");
	return (0);
}
