#include "../../src/validators/WorldScaleValidator.hpp"

WorldScaleValidator::WorldScaleValidator()
{
}

WorldScaleValidator::WorldScaleValidator(const WorldScaleValidator &other)
	: IValidator(other)
{
	*this = other;
}

WorldScaleValidator::~WorldScaleValidator()
{
}

WorldScaleValidator &WorldScaleValidator::operator=(const WorldScaleValidator &other)
{
	(void)other;
	return (*this);
}

int WorldScaleValidator::validate_chunk(int32_t chunk_x, int32_t chunk_z)
{
	WorldChunk	chunk;
	int32_t		error_code;

	error_code = WorldChunkLoader::initialize_chunk(&chunk, chunk_x, chunk_z,
			"integration-seed");
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "world-scale: failed chunk=(%d,%d) error=%d\n",
			chunk_x, chunk_z, error_code);
		chunk.destroy();
		return (1);
	}
	if (chunk.world_x != chunk_x * GAME_VOXEL_CHUNK_WIDTH
		|| chunk.world_z != chunk_z * GAME_VOXEL_CHUNK_DEPTH)
	{
		std::fprintf(stderr,
			"world-scale: coordinate mismatch chunk=(%d,%d) world=(%d,%d)\n",
			chunk_x, chunk_z, chunk.world_x, chunk.world_z);
		chunk.destroy();
		return (1);
	}
	chunk.destroy();
	return (0);
}

int WorldScaleValidator::validate() const
{
	if (GAME_VOXEL_CHUNK_HEIGHT != 256)
	{
		std::fprintf(stderr, "world-scale: failed height=%d required=256\n",
			GAME_VOXEL_CHUNK_HEIGHT);
		return (1);
	}
	if (validate_chunk(0, 0) != 0 || validate_chunk(511, 511) != 0
		|| validate_chunk(-512, -512) != 0)
		return (1);
	std::printf("world-scale: ok horizontal=16384 height=256 chunk_edge=%d\n",
		GAME_VOXEL_CHUNK_WIDTH);
	return (0);
}
