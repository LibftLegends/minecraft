#ifndef PLAYER_SPAWNER_HPP
# define PLAYER_SPAWNER_HPP

# include "../../src/physics/PlayerCollision.hpp"
# include "../../src/physics/PlayerGround.hpp"
# include "../ft_vox.hpp"

class PlayerSpawner
{
  public:
	PlayerSpawner();
	PlayerSpawner(const PlayerSpawner &other);
	~PlayerSpawner();
	PlayerSpawner &operator=(const PlayerSpawner &other);

	static void spawn_player_on_ground(Camera *camera, const World &world);

  private:
	static bool try_spawn_at(Camera *camera, const World &world,
		double candidate_x, double candidate_z);
	static bool scan_spawn_ring(Camera *camera, const World &world,
		int32_t origin_x, int32_t origin_z, int32_t radius);
};

#endif
