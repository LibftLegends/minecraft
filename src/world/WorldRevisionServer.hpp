#ifndef WORLD_REVISION_SERVER_HPP
# define WORLD_REVISION_SERVER_HPP

# include "../../Libft/Modules/Game/game_server.hpp"
# include "../../src/world/World.hpp"
# include "../ft_vox.hpp"

class WorldRevisionServer
{
  public:
	WorldRevisionServer(World &world);
	WorldRevisionServer(const WorldRevisionServer &other);
	~WorldRevisionServer();
	WorldRevisionServer &operator=(const WorldRevisionServer &other);

	int32_t initialize();
	int32_t start(const char *ip, uint16_t port);
	void run_once();
	void destroy();

  private:
	World *world_;
	game_server server_;
	bool initialized_;

	static int32_t handle_message(int32_t client_handle, const char *message,
		void *user_data);
	int32_t handle_revision_message(const char *message);
	int32_t handle_begin_action(json_group *revision);
	int32_t apply_biome_config_overrides(json_group *revision,
		terrain_generation_config &revision_config);
	int32_t apply_biome_size_range_override(json_group *revision,
		terrain_generation_config &revision_config);
	int32_t apply_biome_specific_override(json_group *revision,
		terrain_generation_config &revision_config);
	int32_t handle_select_or_protect_action(json_group *revision,
		const char *action);
};

#endif
