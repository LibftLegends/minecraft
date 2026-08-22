#ifndef WORLD_REVISION_SERVER_HPP
#define WORLD_REVISION_SERVER_HPP

#include "../ft_vox.hpp"
#include "World.hpp"
#include "../../Libft/Modules/Game/game_server.hpp"

class WorldRevisionServer
{
  public:
    explicit WorldRevisionServer(World &world);
    WorldRevisionServer(const WorldRevisionServer &other) = delete;
    WorldRevisionServer &operator=(const WorldRevisionServer &other) = delete;
    ~WorldRevisionServer();

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
};

#endif
