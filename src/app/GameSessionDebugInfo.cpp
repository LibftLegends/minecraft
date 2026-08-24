#include "../../src/app/GameSessionDebugInfo.hpp"

GameSessionDebugInfo::GameSessionDebugInfo()
{
}

GameSessionDebugInfo::GameSessionDebugInfo(const GameSessionDebugInfo &other)
{
	(void)other;
}

GameSessionDebugInfo::~GameSessionDebugInfo()
{
}

GameSessionDebugInfo &GameSessionDebugInfo::operator=(const GameSessionDebugInfo &other)
{
	(void)other;
	return (*this);
}

const char *GameSessionDebugInfo::BIOME_NAMES[5] = {"PLAINS", "HILLS", "DESERT",
	"SNOW", "MOUNTAINS"};

const char *GameSessionDebugInfo::biome_name_for_index(uint32_t biome_index)
{
	if (biome_index < 5U)
		return (BIOME_NAMES[biome_index]);
	return (nullptr);
}

void GameSessionDebugInfo::build(RenderDebug &out, const Camera &camera,
	const World &world, const game_character &player_character,
	VoxelRenderer &renderer, double display_fps, double frame_ms,
	uint32_t selected_block_id, const char *seed)
{
	uint32_t	biome_index;
	const char	*bname;

	out.fps = display_fps;
	out.frame_ms = frame_ms;
	out.visible_chunks = 0;
	out.loaded_chunks = world.loaded_chunk_count;
	out.render_distance = world.active_render_distance;
	out.selected_block_id = selected_block_id;
	out.camera_x = camera.x;
	out.camera_y = camera.y;
	out.camera_z = camera.z;
	out.camera_speed = camera.speed;
	out.boost_speed = camera.speed * 20.0;
	out.ram_mb = SystemMemoryInfo::resident_set_mb();
	out.vram_approx_mb = renderer.get_gpu_renderer() ? renderer.get_gpu_renderer()->gpu_mb_approx() : 0U;
	std::strncpy(out.seed, seed, sizeof(out.seed) - 1);
	out.seed[sizeof(out.seed) - 1] = '\0';
	biome_index = terrain_get_biome_index(world.terrain_generation_settings(),
			player_character.get_x(), player_character.get_z(), seed);
	bname = biome_name_for_index(biome_index);
	if (bname != nullptr)
		std::strncpy(out.biome_name, bname, sizeof(out.biome_name) - 1);
	else
		std::snprintf(out.biome_name, sizeof(out.biome_name), "CUSTOM_%u",
			biome_index);
	out.biome_name[sizeof(out.biome_name) - 1] = '\0';
}
