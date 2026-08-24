#ifndef GAME_SESSION_DEBUG_INFO_HPP
# define GAME_SESSION_DEBUG_INFO_HPP

# include "../../Libft/Modules/Game/game_character.hpp"
# include "../../src/camera/Camera.hpp"
# include "../../src/debug/RenderDebug.hpp"
# include "../../src/diagnostics/SystemMemoryInfo.hpp"
# include "../../src/render/VoxelRenderer.hpp"
# include "../../src/world/World.hpp"
# include "../ft_vox.hpp"

class GameSessionDebugInfo
{
  public:
	GameSessionDebugInfo();
	GameSessionDebugInfo(const GameSessionDebugInfo &other);
	~GameSessionDebugInfo();
	GameSessionDebugInfo &operator=(const GameSessionDebugInfo &other);

	static void build(RenderDebug &out, const Camera &camera,
		const World &world, const game_character &player_character,
		VoxelRenderer &renderer, double display_fps, double frame_ms,
		uint32_t selected_block_id, const char *seed);

  private:
	static const char *BIOME_NAMES[5];

	static const char *biome_name_for_index(uint32_t biome_index);
};

#endif
