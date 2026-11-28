#ifndef WORLD_BOAT_TRANSFER_TRANSACTION_HPP
# define WORLD_BOAT_TRANSFER_TRANSACTION_HPP

# include "GameTransactionJournal.hpp"
# include "../app/GameSessionSnapshotWriter.hpp"
# include "../entities/WorldEntityStore.hpp"
# include "../entities/WorldItemStore.hpp"
# include "../inventory/MinecraftItemCatalog.hpp"
# include "../../Libft/Modules/Game/game_character.hpp"

class WorldBoatTransferTransaction
{
  public:
	struct Result
	{
		uint64_t request_id;
		uint64_t item_instance_id;
		uint32_t entity_id;
		int32_t inventory_slot;
		bool replayed;
	};

	static uint64_t journal_scope() noexcept;
	static int32_t place(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		WorldEntityStore &world_entities, GameTransactionJournal &journal,
		uint64_t request_id, int32_t inventory_slot,
		uint64_t expected_inventory_revision, const EntityState &boat_state,
		const GameSessionSnapshotWriter *snapshot_writer,
		Result *result_out) noexcept;
	static int32_t place_for_actor(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		WorldEntityStore &world_entities, GameTransactionJournal &journal,
		uint64_t request_id, uint64_t authenticated_actor_id,
		int32_t inventory_slot, uint64_t expected_inventory_revision,
		const EntityState &boat_state,
		const GameSessionSnapshotWriter *snapshot_writer,
		Result *result_out) noexcept;
	static int32_t retrieve(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		WorldEntityStore &world_entities, MinecraftItemCatalog &item_catalog,
		GameTransactionJournal &journal, uint64_t request_id,
		uint32_t entity_id, uint64_t expected_entity_revision,
		uint64_t expected_inventory_revision,
		const GameSessionSnapshotWriter *snapshot_writer,
		Result *result_out) noexcept;
	static int32_t retrieve_for_actor(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		WorldEntityStore &world_entities, MinecraftItemCatalog &item_catalog,
		GameTransactionJournal &journal, uint64_t request_id,
		uint64_t authenticated_actor_id, uint32_t entity_id,
		uint64_t expected_entity_revision,
		uint64_t expected_inventory_revision,
		const GameSessionSnapshotWriter *snapshot_writer,
		Result *result_out) noexcept;
	static int32_t move_for_actor(const char *snapshot_path,
		WorldEntityStore &world_entities, GameTransactionJournal &journal,
		uint64_t request_id, uint64_t authenticated_actor_id,
		uint32_t entity_id, uint64_t expected_entity_revision,
		const EntityState &next_state,
		const GameSessionSnapshotWriter *snapshot_writer,
		uint64_t *new_revision_out) noexcept;
	/* A restored PREPARED record always accompanies the pre-action checkpoint.
	 * Recovery aborts it before gameplay resumes. */
	static int32_t recover_pending(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		WorldEntityStore &world_entities, GameTransactionJournal &journal,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept;
};

#endif
