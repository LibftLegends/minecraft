#ifndef WORLD_EQUIPMENT_LOADOUT_TRANSACTION_HPP
# define WORLD_EQUIPMENT_LOADOUT_TRANSACTION_HPP

# include "GameTransactionJournal.hpp"
# include "../app/GameSessionSnapshotWriter.hpp"
# include "../inventory/MinecraftItemCatalog.hpp"
# include "../../Libft/Modules/Game/game_character.hpp"

class WorldEquipmentLoadoutTransaction
{
  public:
	static uint64_t journal_scope() noexcept;
	static uint32_t transaction_type() noexcept;
	static int32_t equip(const char *snapshot_path, game_character &character,
		MinecraftItemCatalog &catalog, GameTransactionJournal &journal,
		uint64_t request_id, int32_t inventory_slot,
		uint64_t expected_inventory_revision, uint32_t equipment_slot,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept;
	static int32_t equip(const char *snapshot_path, game_character &character,
		MinecraftItemCatalog &catalog, GameTransactionJournal &journal,
		uint64_t request_id, uint64_t authenticated_actor_id,
		int32_t inventory_slot, uint64_t expected_inventory_revision,
		uint32_t equipment_slot,
		const GameSessionSnapshotWriter *snapshot_writer,
		bool (*precommit_authorizer)(uint64_t authenticated_actor_id,
			void *user_data) noexcept,
		void *precommit_authorizer_user_data) noexcept;
	static int32_t unequip(const char *snapshot_path, game_character &character,
		MinecraftItemCatalog &catalog, GameTransactionJournal &journal,
		uint64_t request_id, uint64_t expected_inventory_revision,
		uint32_t equipment_slot,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept;
	static int32_t unequip(const char *snapshot_path, game_character &character,
		MinecraftItemCatalog &catalog, GameTransactionJournal &journal,
		uint64_t request_id, uint64_t authenticated_actor_id,
		uint64_t expected_inventory_revision, uint32_t equipment_slot,
		const GameSessionSnapshotWriter *snapshot_writer,
		bool (*precommit_authorizer)(uint64_t authenticated_actor_id,
			void *user_data) noexcept,
		void *precommit_authorizer_user_data) noexcept;
	static int32_t recover_pending(const char *snapshot_path,
		game_character &character, MinecraftItemCatalog &catalog,
		GameTransactionJournal &journal,
		const GameSessionSnapshotWriter *snapshot_writer,
		bool (*actor_authorizer)(uint64_t authenticated_actor_id,
			void *user_data) noexcept = nullptr,
		void *actor_authorizer_user_data = nullptr) noexcept;
};

#endif
