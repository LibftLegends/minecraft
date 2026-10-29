#ifndef WORLD_PROJECTILE_LAUNCH_TRANSACTION_HPP
# define WORLD_PROJECTILE_LAUNCH_TRANSACTION_HPP

# include "GameTransactionJournal.hpp"
# include "../app/GameSessionSnapshotWriter.hpp"
# include "../entities/WorldEntityStore.hpp"
# include "../inventory/MinecraftItemCatalog.hpp"
# include "../../Libft/Modules/Game/game_character.hpp"
# include <cstdint>

class WorldProjectileLaunchTransaction
{
  public:
	static uint64_t journal_scope(uint64_t actor_id) noexcept;
	static uint32_t transaction_type() noexcept;
	static int32_t launch(const char *snapshot_path, game_character &character,
		WorldEntityStore &entities, MinecraftItemCatalog &catalog,
		GameTransactionJournal &journal, uint64_t actor_id,
		uint64_t request_id, uint64_t expected_inventory_revision,
		uint64_t server_tick, double origin_x, double origin_y, double origin_z,
		double direction_x, double direction_y, double direction_z,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept;
	static int32_t recover_pending(const char *snapshot_path,
		game_character &character, WorldEntityStore &entities,
		MinecraftItemCatalog &catalog, GameTransactionJournal &journal,
		uint64_t server_tick, double origin_x, double origin_y, double origin_z,
		double direction_x, double direction_y, double direction_z,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept;
};

#endif
