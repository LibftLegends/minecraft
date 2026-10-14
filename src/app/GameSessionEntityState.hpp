#ifndef GAME_SESSION_ENTITY_STATE_HPP
# define GAME_SESSION_ENTITY_STATE_HPP

# include "../entities/WorldEntityStore.hpp"
# include "../entities/WorldSpawnerStore.hpp"
# include <cstddef>
# include <cstdint>
# include <vector>

class GameSessionEntityState
{
  public:
	static int32_t serialize(const WorldEntityStore &entities,
		const WorldSpawnerStore &spawners,
		std::vector<uint8_t> &bytes_out) noexcept;
	static int32_t restore(const uint8_t *bytes, std::size_t byte_count,
		WorldEntityStore &entities, WorldSpawnerStore &spawners) noexcept;
	static int32_t validate(const uint8_t *bytes,
		std::size_t byte_count) noexcept;
};

#endif
