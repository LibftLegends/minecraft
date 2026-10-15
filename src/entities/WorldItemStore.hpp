#ifndef WORLD_ITEM_STORE_HPP
# define WORLD_ITEM_STORE_HPP

# include "../../Libft/Modules/Game/game_inventory.hpp"
# include <cstddef>
# include <cstdint>
# include <deque>
# include <map>
# include <memory>
# include <mutex>
# include <unordered_map>
# include <vector>

class WorldEntityStore;
class MinecraftItemCatalog;

struct WorldItemSnapshot
{
	uint64_t entity_id;
	uint64_t entity_revision;
	uint64_t spawned_tick;
	uint64_t owner_id;
	uint64_t owner_lock_until_tick;
	int32_t chunk_x;
	int32_t chunk_z;
	int32_t item_id;
	int32_t quantity;
	int32_t max_stack;
	uint64_t item_instance_id;
	int32_t rarity;
	int32_t width;
	int32_t height;
	int32_t modifier1_id;
	int32_t modifier1_value;
	int32_t modifier2_id;
	int32_t modifier2_value;
	int32_t modifier3_id;
	int32_t modifier3_value;
	int32_t modifier4_id;
	int32_t modifier4_value;
	double x;
	double y;
	double z;
};

struct WorldItemPickupResult
{
	uint64_t entity_id;
	uint64_t entity_revision;
	uint64_t inventory_revision;
	int32_t transferred_quantity;
	int32_t remaining_quantity;
	bool entity_removed;
};

class WorldItemStore
{
  public:
	WorldItemStore();
	WorldItemStore(const WorldItemStore &other) = delete;
	~WorldItemStore();
	WorldItemStore &operator=(const WorldItemStore &other) = delete;

	int32_t initialize() noexcept;
	int32_t destroy() noexcept;
	bool is_initialized() const noexcept;
	int32_t spawn(const ft_sharedptr<game_item> &item, double x, double y,
		double z, uint64_t spawned_tick, uint64_t owner_id,
		uint64_t owner_lock_until_tick, uint64_t *entity_id_out) noexcept;
	int32_t spawn_pending(const ft_sharedptr<game_item> &item, double x,
		double y, double z, uint64_t spawned_tick, uint64_t owner_id,
		uint64_t owner_lock_until_tick, uint64_t *entity_id_out) noexcept;
	int32_t publish_spawn(uint64_t entity_id,
		uint64_t expected_entity_revision) noexcept;
	int32_t despawn(uint64_t entity_id,
		uint64_t expected_entity_revision) noexcept;
	int32_t merge_stacks(uint64_t source_entity_id,
		uint64_t expected_source_revision, uint64_t destination_entity_id,
		uint64_t expected_destination_revision,
		int32_t *transferred_quantity_out) noexcept;
	int32_t merge_into_nearby(uint64_t source_entity_id,
		uint64_t expected_source_revision, double x, double y, double z,
		uint64_t actor_id, uint64_t server_tick,
		int32_t *transferred_quantity_out) noexcept;
	int32_t query_nearby(double x, double y, double z, double radius,
		uint64_t actor_id, uint64_t server_tick,
		std::vector<WorldItemSnapshot> *snapshots_out) const noexcept;
	/* Renderer-only view. Includes published items regardless of temporary
	 * owner pickup locks; pending transaction entries remain hidden. */
	int32_t query_renderable(double x, double y, double z, double radius,
		std::vector<WorldItemSnapshot> *snapshots_out) const noexcept;
	int32_t serialize(std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t get_max_item_instance_id(uint64_t *instance_id_out) const noexcept;
	uint64_t change_revision() const noexcept;
	int32_t validate_instance_id_uniqueness(
		const game_inventory &inventory) const noexcept;
	int32_t validate_instance_id_uniqueness(
		const game_inventory &inventory,
		const WorldEntityStore *entities) const noexcept;
	int32_t validate_instance_id_uniqueness(
		const game_inventory &inventory, const WorldEntityStore *entities,
		const std::vector<uint64_t> &additional_instance_ids) const noexcept;
	int32_t validate_item_instance_policy(
		const MinecraftItemCatalog &catalog) const noexcept;
	int32_t save_file(const char *file_path) const noexcept;
	int32_t load_file(const char *file_path) noexcept;
	int32_t deserialize(const uint8_t *bytes, std::size_t byte_count) noexcept;
	int32_t pickup(uint64_t entity_id, uint64_t expected_entity_revision,
		uint64_t actor_id, uint64_t request_id, uint64_t expected_inventory_revision,
		int32_t requested_quantity, double actor_x, double actor_y,
		double actor_z, uint64_t server_tick,
		game_inventory &destination, WorldItemPickupResult *result_out) noexcept;

  private:
	struct Entry;
	struct CachedPickup
	{
		uint64_t entity_id;
		uint64_t expected_entity_revision;
		uint64_t expected_inventory_revision;
		int32_t requested_quantity;
		int32_t result_code;
		WorldItemPickupResult result;
	};

	static const std::size_t PICKUP_CACHE_LIMIT = 4096U;
	static const std::size_t MAX_QUERY_RESULTS = 256U;
	static const std::size_t MAX_QUERY_SCAN = 1024U;
	static const double MAX_QUERY_RADIUS;
	static const double MAX_PICKUP_REACH;
	mutable std::mutex mutex_;
	std::unordered_map<uint64_t, std::unique_ptr<Entry> > entries_;
	std::unordered_map<uint64_t, std::vector<uint64_t> > chunk_index_;
	std::map<std::pair<uint64_t, uint64_t>, CachedPickup> pickup_cache_;
	std::deque<std::pair<uint64_t, uint64_t> > pickup_order_;
	uint64_t next_entity_id_;
	uint64_t next_transfer_transaction_id_;
	uint64_t change_revision_;
	bool initialized_;
	int32_t query_internal(double x, double y, double z, double radius,
		uint64_t actor_id, uint64_t server_tick, bool respect_owner_lock,
		std::vector<WorldItemSnapshot> *snapshots_out) const noexcept;

	static uint64_t chunk_key(int32_t chunk_x, int32_t chunk_z) noexcept;
	static int32_t world_coordinate_to_chunk(double coordinate,
		int32_t *chunk_out) noexcept;
	static bool pickup_matches(const CachedPickup &cached,
		uint64_t entity_id, uint64_t expected_entity_revision,
		uint64_t expected_inventory_revision, int32_t requested_quantity) noexcept;
	void remove_entity_locked(uint64_t entity_id, const Entry &entry) noexcept;
};

#endif
