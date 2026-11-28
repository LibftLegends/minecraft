#ifndef WORLD_SAVE_CATALOG_HPP
# define WORLD_SAVE_CATALOG_HPP

# include <cstdint>
# include <array>
# include <mutex>
# include <string>
# include <vector>

enum WorldSaveOwnershipMode : uint32_t
{
	WORLD_SAVE_OWNER_LOCAL = 1U,
	WORLD_SAVE_OWNER_SHARED = 2U
};

enum WorldSaveAccessPolicy : uint32_t
{
	WORLD_SAVE_ACCESS_PRIVATE = 1U,
	WORLD_SAVE_ACCESS_INVITE_ONLY = 2U,
	WORLD_SAVE_ACCESS_FRIENDS_ONLY = 3U,
	WORLD_SAVE_ACCESS_ALLOWLIST = 4U,
	WORLD_SAVE_ACCESS_PUBLIC = 5U
};

enum WorldSaveSpawnPolicy : uint32_t
{
	WORLD_SAVE_SPAWN_DEFAULT = 1U,
	WORLD_SAVE_SPAWN_OWNER_DEFINED = 2U,
	WORLD_SAVE_SPAWN_BED_BOUND = 3U
};

enum { WORLD_SAVE_CURRENT_RULESET_VERSION = 1U };

enum WorldSaveCharacterAdmission : uint32_t
{
	WORLD_SAVE_CHARACTER_WORLD_BOUND_ONLY = 1U,
	WORLD_SAVE_CHARACTER_PORTABLE_ALLOWED = 2U,
	WORLD_SAVE_CHARACTER_APPROVAL_REQUIRED = 3U,
	WORLD_SAVE_CHARACTER_FRESH_ONLY = 4U
};

enum WorldSaveCharacterMode : uint32_t
{
	WORLD_SAVE_CHARACTER_WORLD_BOUND = 1U,
	WORLD_SAVE_CHARACTER_PORTABLE = 2U
};

enum WorldSaveCharacterImport : uint32_t
{
	WORLD_SAVE_IMPORT_INVENTORY = 1U << 0U,
	WORLD_SAVE_IMPORT_RECIPES = 1U << 1U,
	WORLD_SAVE_IMPORT_ABILITIES = 1U << 2U,
	WORLD_SAVE_IMPORT_EQUIPMENT = 1U << 3U,
	WORLD_SAVE_IMPORT_COSMETICS = 1U << 4U,
	WORLD_SAVE_IMPORT_PROGRESSION = 1U << 5U,
	WORLD_SAVE_IMPORT_ALL = (1U << 6U) - 1U
};

struct WorldSaveCharacterImportSummary
{
	uint32_t import_mask;
	uint32_t progression_level;
	uint32_t item_instance_count;
	uint64_t item_value;
};

struct WorldSaveDefinition
{
	uint8_t world_id[32];
	std::string name;
	std::string seed;
	uint32_t generator_version;
	uint32_t ruleset_version;
	uint32_t difficulty;
	uint32_t ownership_mode;
	uint32_t access_policy;
	uint32_t spawn_policy;
	uint64_t created_epoch_ms;
	uint64_t modified_epoch_ms;
	uint64_t owner_actor_id;
	std::vector<uint64_t> allowed_actor_ids;
	std::vector<std::array<uint8_t, 32> > banned_character_ids;
	std::vector<std::array<uint8_t, 32> > approved_character_ids;
	uint32_t character_admission;
	uint32_t maximum_players;
	uint32_t minimum_generator_version;
	uint32_t maximum_generator_version;
	uint32_t minimum_ruleset_version;
	uint32_t maximum_ruleset_version;
	uint32_t character_import_mask;
	uint32_t maximum_progression_level;
	uint32_t maximum_imported_item_instances;
	uint64_t maximum_imported_item_value;
};

class GameSessionPersistence;

/* Persistent index for stable world identity and creation policy. The catalog
 * stores metadata only; GameSession stores world state in the ID-derived save. */
class WorldSaveCatalog
{
  public:
	static const uint32_t MAX_WORLDS = 4096U;

	WorldSaveCatalog();
	WorldSaveCatalog(const WorldSaveCatalog &other);
	~WorldSaveCatalog();
	WorldSaveCatalog &operator=(const WorldSaveCatalog &other);

	int32_t initialize(const char *catalog_path) noexcept;
	int32_t list(std::vector<WorldSaveDefinition> &entries_out) const noexcept;
	int32_t find(const uint8_t world_id[32],
		WorldSaveDefinition *entry_out) const noexcept;
	/* Destructive restore requires the owner to type the exact world name and
	 * is denied while any admitted character holds a join lease. */
	int32_t begin_world_maintenance(const uint8_t world_id[32],
		uint64_t actor_id, const std::string &typed_world_name) noexcept;
	int32_t end_world_maintenance(const uint8_t world_id[32],
		uint64_t actor_id) noexcept;
	int32_t create(const std::string &name, const std::string &seed,
		uint64_t owner_actor_id, uint32_t difficulty,
		uint32_t ownership_mode,
		uint32_t access_policy, uint32_t spawn_policy,
		uint32_t character_admission, uint32_t maximum_players,
		WorldSaveDefinition *entry_out) noexcept;
	/* Mutations require the authenticated owner actor ID. Ownership-mode
	 * migration is allowed only while the catalog has no live join leases. */
	int32_t rename(const uint8_t world_id[32], uint64_t actor_id,
		const std::string &new_name) noexcept;
	int32_t update_policy(const uint8_t world_id[32], uint64_t actor_id,
		uint32_t difficulty,
		uint32_t ownership_mode, uint32_t access_policy,
		uint32_t spawn_policy) noexcept;
	int32_t set_allowed_actors(const uint8_t world_id[32], uint64_t actor_id,
		const std::vector<uint64_t> &actor_ids) noexcept;
	int32_t set_banned_characters(const uint8_t world_id[32],
		uint64_t actor_id,
		const std::vector<std::array<uint8_t, 32> > &character_ids) noexcept;
	int32_t set_approved_characters(const uint8_t world_id[32],
		uint64_t actor_id,
		const std::vector<std::array<uint8_t, 32> > &character_ids) noexcept;
	/* The friendship bit must come from the authenticated host. */
	int32_t authorize_access(const uint8_t world_id[32], uint64_t actor_id,
		bool host_reports_friend) const noexcept;
	int32_t update_admission_policy(const uint8_t world_id[32],
		uint64_t actor_id,
		uint32_t character_admission, uint32_t maximum_players) noexcept;
	int32_t update_compatibility_policy(const uint8_t world_id[32],
		uint64_t actor_id, uint32_t minimum_generator_version,
		uint32_t maximum_generator_version,
		uint32_t minimum_ruleset_version,
		uint32_t maximum_ruleset_version) noexcept;
	int32_t update_character_import_policy(const uint8_t world_id[32],
		uint64_t actor_id, uint32_t import_mask,
		uint32_t maximum_progression_level,
		uint32_t maximum_item_instances,
		uint64_t maximum_item_value) noexcept;
	int32_t authorize_join(const uint8_t world_id[32], uint64_t actor_id,
		const uint8_t character_id[32],
		bool host_reports_friend, uint32_t character_mode,
		const uint8_t source_world_id[32], bool character_is_new,
		uint32_t generator_version, uint32_t ruleset_version,
		uint32_t active_player_count) const noexcept;
	/* Summary data must be calculated by the trusted host from the saved
	 * character profile; never accept client-reported totals. */
	int32_t authorize_character_import(const uint8_t world_id[32],
		uint64_t actor_id, const uint8_t character_id[32],
		bool host_reports_friend,
		const WorldSaveCharacterImportSummary &summary) const noexcept;
	/* Reserves one concurrent-player slot and one character identity for this
	 * live host process. The caller must release the returned lease on logout. */
	int32_t reserve_join(const uint8_t world_id[32], uint64_t actor_id,
		uint64_t request_id, bool host_reports_friend,
		const uint8_t character_id[32],
		uint32_t character_mode, const uint8_t source_world_id[32],
		bool character_is_new, uint32_t generator_version,
		uint32_t ruleset_version, uint8_t lease_id_out[32]) noexcept;
	int32_t release_join(const uint8_t lease_id[32]) noexcept;
	/* Rechecks mutable world policy immediately before session publication. */
	int32_t validate_join_lease(const uint8_t lease_id[32]) const noexcept;

  private:
	friend class GameSessionPersistence;
	/* Removes the catalog entry only after the persistence wrapper has removed
	 * its save files under the owner-held maintenance lease. */
	int32_t delete_world(const uint8_t world_id[32], uint64_t actor_id,
		const std::string &typed_world_name) noexcept;
	mutable std::mutex mutex_;
	std::string catalog_path_;
	std::vector<WorldSaveDefinition> entries_;
	std::vector<std::array<uint8_t, 32> > active_world_maintenance_;
	struct ActiveJoinLease
	{
		uint8_t lease_id[32];
		uint8_t world_id[32];
		uint8_t character_id[32];
		uint8_t source_world_id[32];
		uint64_t actor_id;
		uint64_t request_id;
		uint32_t character_mode;
		uint32_t generator_version;
		uint32_t ruleset_version;
		bool host_reports_friend;
		bool character_is_new;
	};
	std::vector<ActiveJoinLease> active_join_leases_;
	bool catalog_path_registered_;
	bool initialized_;

	int32_t save_locked(const std::vector<WorldSaveDefinition> &entries) const
		noexcept;
	void unregister_catalog_path() noexcept;
};

#endif
