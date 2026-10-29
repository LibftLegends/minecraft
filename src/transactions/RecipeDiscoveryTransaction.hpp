#ifndef RECIPE_DISCOVERY_TRANSACTION_HPP
# define RECIPE_DISCOVERY_TRANSACTION_HPP

# include "../app/GameSessionSnapshotWriter.hpp"
# include "../crafting/MinecraftRecipeCatalog.hpp"

class RecipeDiscoveryTransaction
{
  public:
	static int32_t execute(MinecraftRecipeCatalog &recipe_catalog,
		uint32_t source_type_id, uint32_t source_id,
		uint64_t source_event_id,
		const GameSessionSnapshotWriter &snapshot_writer,
		game_recipe_unlock_result *result_out) noexcept;
	static int32_t execute(MinecraftRecipeCatalog &recipe_catalog,
		uint32_t source_type_id, uint32_t source_id,
		uint64_t source_event_id, const GameSessionSnapshotWriter &snapshot_writer,
		ft_bool *newly_unlocked) noexcept;
};

#endif
