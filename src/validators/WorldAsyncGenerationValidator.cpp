#include "WorldAsyncGenerationValidator.hpp"

#include "../../src/world/World.hpp"

#include <cstdio>
#include <thread>

WorldAsyncGenerationValidator::WorldAsyncGenerationValidator()
{
}

WorldAsyncGenerationValidator::WorldAsyncGenerationValidator(
    const WorldAsyncGenerationValidator &other)
{
    (void)other;
}

WorldAsyncGenerationValidator::~WorldAsyncGenerationValidator()
{
}

WorldAsyncGenerationValidator &WorldAsyncGenerationValidator::operator=(
    const WorldAsyncGenerationValidator &other)
{
    (void)other;
    return (*this);
}

static bool world_async_chunks_equal(const game_voxel_chunk &left,
                                     const game_voxel_chunk &right)
{
    int32_t local_z = 0;
    while (local_z < GAME_VOXEL_CHUNK_DEPTH)
    {
        int32_t local_y = 0;
        while (local_y < GAME_VOXEL_CHUNK_HEIGHT)
        {
            int32_t local_x = 0;
            while (local_x < GAME_VOXEL_CHUNK_WIDTH)
            {
                uint32_t left_block = 0U;
                uint32_t right_block = 0U;
                if (left.read_block(local_x, local_y, local_z, &left_block)
                        != FT_ERR_SUCCESS
                    || right.read_block(local_x, local_y, local_z, &right_block)
                        != FT_ERR_SUCCESS
                    || left_block != right_block)
                {
                    std::fprintf(stderr, "async-worldgen: mismatch at %d,%d,%d left=%u right=%u\n",
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

int WorldAsyncGenerationValidator::validate() const
{
    World world;
    game_voxel_chunk expected;
    int32_t error_code = world.initialize("async-validator");
    if (error_code != FT_ERR_SUCCESS)
    {
        std::fprintf(stderr, "async-worldgen: initialize failed error=%d\n", error_code);
        return (1);
    }
    int32_t frame = 0;
    while (world.find_chunk(-4, 2) == nullptr && frame < 4000)
    {
        error_code = world.update_around(0.0, 0.0, 4,
            WorldCoordinates::REQUIRED_VISIBLE_DISTANCE);
        if (error_code != FT_ERR_SUCCESS)
        {
            world.destroy();
            return (1);
        }
        std::this_thread::yield();
        frame += 1;
    }
    const WorldChunk *generated = world.find_chunk(-4, 2);
    if (generated == nullptr || expected.initialize() != FT_ERR_SUCCESS
        || terrain_generate_chunk(expected, -64, 32, "async-validator")
            != FT_ERR_SUCCESS
        || !world_async_chunks_equal(generated->chunk, expected))
    {
        const World::StreamDiagnostics diagnostics = world.stream_diagnostics();
        std::fprintf(stderr, "async-worldgen: failed frame=%d loaded=%d\n",
            frame, world.loaded_chunk_count);
        std::fprintf(stderr, "async-worldgen: pending=%zu failed=%zu retry=%zu error=%d\n",
            diagnostics.pending_count, diagnostics.failed_count,
            diagnostics.retryable_count, diagnostics.last_error);
        (void)expected.destroy();
        world.destroy();
        return (1);
    }
    (void)expected.destroy();
    world.destroy();
    std::printf("async-worldgen: ok frame=%d\n", frame);
    return (0);
}
