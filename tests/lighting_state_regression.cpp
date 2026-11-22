#include "../src/chunks/WorldChunk.hpp"
#include "../src/edits/WorldBlockLighting.hpp"
#include <cstdio>

int main()
{
	WorldChunk chunk;
	chunk.content_version = 7U;
	chunk.light_version = 7U;
	chunk.light_input_version = 9U;
	chunk.computed_light_input_version = 9U;
	chunk.mark_content_changed(false);
	if (!chunk.light_is_current() || chunk.light_input_version != 9U)
		return (1);
	chunk.mark_content_changed();
	if (chunk.light_is_current() || chunk.light_input_version != 10U)
		return (2);
	chunk.mark_content_changed(false);
	if (chunk.light_is_current() || chunk.light_input_version != 10U)
		return (3);
	chunk.content_version = 65535U;
	chunk.light_version = 65535U;
	chunk.computed_light_input_version = chunk.light_input_version;
	chunk.mark_content_changed(false);
	if (!chunk.light_is_current() || chunk.content_version != 1U)
		return (4);
	if (world_block_lighting::inputs_differ(VOXEL_GENERATOR_STONE_BLOCK_ID,
			VOXEL_GENERATOR_DIRT_BLOCK_ID))
		return (5);
	if (!world_block_lighting::inputs_differ(VOXEL_GENERATOR_STONE_BLOCK_ID,
			GAME_VOXEL_AIR_BLOCK))
		return (6);
	std::puts("lighting state regression: passed");
	return (0);
}
