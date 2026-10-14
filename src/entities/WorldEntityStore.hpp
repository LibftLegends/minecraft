#ifndef WORLD_ENTITY_STORE_HPP
# define WORLD_ENTITY_STORE_HPP

# include "EntityState.hpp"
# include <cstddef>
# include <cstdint>
# include <map>
# include <mutex>
# include <vector>

struct WorldEntitySnapshot
{
	EntityState state;
	uint64_t revision;
	/* Nonzero only for entities that represent a single physical inventory
	 * item, such as a placed boat. */
	uint64_t item_instance_id;
	/* Authenticated owner for player-placed entities; zero denotes legacy or
	 * host-owned entities. */
	uint64_t owner_actor_id;
};

/* Authoritative, revision-checked storage for simulated world entities.
 * Entity definitions remain data-driven through EntityState::type. */
class WorldEntityStore
{
  public:
	static const uint32_t MAXIMUM_ENTITIES = 65536U;

	WorldEntityStore() noexcept;
	WorldEntityStore(const WorldEntityStore &) = delete;
	WorldEntityStore &operator=(const WorldEntityStore &) = delete;

	int32_t initialize() noexcept;
	int32_t destroy() noexcept;
	bool is_initialized() const noexcept;
	int32_t spawn(const EntityState &state, uint32_t *entity_id_out) noexcept;
	int32_t spawn_item(const EntityState &state, uint64_t item_instance_id,
		uint32_t *entity_id_out) noexcept;
	int32_t spawn_item(const EntityState &state, uint64_t item_instance_id,
		uint64_t owner_actor_id, uint32_t *entity_id_out) noexcept;
	/* Adds a complete entity group atomically. On failure neither the store nor
	 * ids_out changes. */
	int32_t spawn_batch(const std::vector<EntityState> &states,
		std::vector<uint32_t> &ids_out) noexcept;
	int32_t update(uint32_t entity_id, uint64_t expected_revision,
		const EntityState &state, uint64_t *new_revision_out) noexcept;
	/* Applies bounded positive damage to a live non-player, non-boat entity
	 * under its current revision. Lethal damage sets health to zero and marks
	 * the entity DEAD; the updated value is returned atomically. */
	int32_t apply_damage(uint32_t entity_id, uint64_t expected_revision,
		int32_t damage, WorldEntitySnapshot *snapshot_out) noexcept;
	/* Compensates a transaction only if the entity still has the revision
	 * produced by that transaction; the prior revision is restored exactly. */
	int32_t rollback_update(const WorldEntitySnapshot &prior_snapshot,
		uint64_t expected_current_revision) noexcept;
	int32_t despawn(uint32_t entity_id,
		uint64_t expected_revision) noexcept;
	int32_t get(uint32_t entity_id,
		WorldEntitySnapshot *snapshot_out) const noexcept;
	int32_t snapshot(std::vector<WorldEntitySnapshot> &snapshots_out) const noexcept;
	/* Returns entities within a finite 3D radius in stable ID order. If more
	 * than maximum_results match, the output is left unchanged and FULL is
	 * returned so simulation callers cannot mistake a truncated view for all
	 * nearby entities. */
	int32_t query_radius(double center_x, double center_y, double center_z,
		double radius, std::size_t maximum_results,
		std::vector<WorldEntitySnapshot> &snapshots_out) const noexcept;
	int32_t serialize(std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t get_max_item_instance_id(uint64_t *instance_id_out) const noexcept;
	int32_t deserialize(const uint8_t *bytes,
		std::size_t byte_count) noexcept;

  private:
	struct Entry
	{
		EntityState state;
		uint64_t revision;
		uint64_t item_instance_id;
		uint64_t owner_actor_id;
	};
	mutable std::mutex mutex_;
	std::map<uint32_t, Entry> entities_;
	uint32_t next_entity_id_;
	bool initialized_;

	static bool state_is_valid(const EntityState &state) noexcept;
};

#endif
