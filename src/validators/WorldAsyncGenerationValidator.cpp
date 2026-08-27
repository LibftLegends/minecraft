#include "../../src/validators/WorldAsyncGenerationValidator.hpp"

WorldAsyncGenerationValidator::WorldAsyncGenerationValidator()
{
}

WorldAsyncGenerationValidator::WorldAsyncGenerationValidator(const WorldAsyncGenerationValidator &other)
{
	(void)other;
}

WorldAsyncGenerationValidator::~WorldAsyncGenerationValidator()
{
}

WorldAsyncGenerationValidator &WorldAsyncGenerationValidator::operator=(const WorldAsyncGenerationValidator &other)
{
	(void)other;
	return (*this);
}

bool WorldAsyncGenerationValidator::chunks_equal(const game_voxel_chunk &left,
	const game_voxel_chunk &right) noexcept
{
	int32_t local_x;
	int32_t local_y;
	int32_t local_z;
	uint32_t left_block;
	uint32_t right_block;

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
				{
					std::fprintf(stderr,
						"async-worldgen: mismatch at %d,%d,%d left=%u right=%u\n",
						local_x, local_y, local_z, left_block, right_block);
					return (false);
				}
				local_x += 1;
			}
			local_y += 1;
		}
		local_z += 1;
	}
	return (true);
}

const WorldChunk *WorldAsyncGenerationValidator::stream_until_ready(World &world,
	int32_t *frame) noexcept
{
	int32_t error_code;

	while (world.find_chunk(-4, 2) == nullptr && *frame < 4000)
	{
		error_code = world.update_around(0.0, 0.0, 4,
				WorldCoordinates::REQUIRED_VISIBLE_DISTANCE);
		if (error_code != FT_ERR_SUCCESS)
			return (nullptr);
		std::this_thread::yield();
		*frame += 1;
	}
	return (world.find_chunk(-4, 2));
}

void WorldAsyncGenerationValidator::report_failure(const World &world,
	int32_t frame) noexcept
{
	World::StreamDiagnostics diagnostics;

	diagnostics = world.stream_diagnostics();
	std::fprintf(stderr, "async-worldgen: failed frame=%d loaded=%d\n", frame,
		world.loaded_chunk_count);
	std::fprintf(stderr,
		"async-worldgen: pending=%zu failed=%zu retry=%zu error=%d\n",
		diagnostics.pending_count, diagnostics.failed_count,
		diagnostics.retryable_count, diagnostics.last_error);
}

int WorldAsyncGenerationValidator::validate() const
{
	World world;
	game_voxel_chunk expected;
	const WorldChunk *generated;
	int32_t error_code;
	int32_t frame;

	error_code = world.initialize("async-validator");
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "async-worldgen: initialize failed error=%d\n",
			error_code);
		return (1);
	}
	frame = 0;
	generated = WorldAsyncGenerationValidator::stream_until_ready(world,
			&frame);
	if (generated == nullptr || expected.initialize() != FT_ERR_SUCCESS
		|| terrain_generate_chunk(expected, -64, 32,
			"async-validator") != FT_ERR_SUCCESS
		|| !WorldAsyncGenerationValidator::chunks_equal(generated->chunk,
			expected))
	{
		WorldAsyncGenerationValidator::report_failure(world, frame);
		(void)expected.destroy();
		world.destroy();
		return (1);
	}
	(void)expected.destroy();
	world.destroy();
	std::printf("async-worldgen: ok frame=%d\n", frame);
	return (0);
}
