#ifndef GAME_SESSION_HPP
# define GAME_SESSION_HPP

#include "../ft_vox.hpp"
#include "../../src/camera/Camera.hpp"
#include "../../src/world/World.hpp"
#include "../../src/debug/RenderDebug.hpp"
#include "../../src/diagnostics/SystemMemoryInfo.hpp"
#include "../../src/platform/InputReader.hpp"
#include "../../src/platform/ApplicationWindow.hpp"
#include "../../src/render/VoxelRenderer.hpp"
#include "../../src/player/PlayerController.hpp"
#include "../../src/physics/PlayerCollision.hpp"
#include "../../src/interaction/BlockInteractor.hpp"
#include "../../src/inventory/MinecraftItemCatalog.hpp"
#include "../../src/crafting/MinecraftRecipeCatalog.hpp"
#include "../../src/entities/WorldItemStore.hpp"
#include "../../src/entities/WorldEntityStore.hpp"
#include "../../src/entities/WorldSpawnerStore.hpp"
#include "../../src/entities/WorldAnimalSimulation.hpp"
#include "../../src/transactions/GameTransactionJournal.hpp"
#include "../../src/transactions/WorldBoatTransferTransaction.hpp"
#include "../../src/transactions/WorldFarmlandTransaction.hpp"
#include "GameSessionSnapshotWriter.hpp"
#include "../../src/policy/RenderDistanceStrategy.hpp"
#include "../../src/settings/Settings.hpp"
#include "../../src/cards/RegenerationCardSession.hpp"
#include "../../src/cards/RegenerationWorldRules.hpp"
#include "../../Libft/Modules/Game/game_character.hpp"
#include <mutex>
#include <utility>
#include <vector>

class WorldRevisionServer;
class ProtocolBoatMoveIntentMessage;
class ProtocolBoatMoveResultMessage;
class WorldRevisionValidator;
class BlockEditValidator;

struct CropGrowthPolicy
{
	uint32_t tick_interval;
	uint32_t maturity_roll_divisor;
	uint8_t wheat_minimum_light_level;
	uint8_t paper_reed_minimum_light_level;
	uint8_t hydration_radius;
};

struct CropChunkGrowthDeadline
{
	int32_t chunk_x;
	int32_t chunk_z;
	uint64_t next_growth_tick;
};

/* Stable host delivery envelope. The host supplies/persists session_id and
 * restores the same value before exposing outputs after a process restart. */
struct RegenerationHostOutput
{
	uint64_t session_id;
	card_game_host_intent_record record;
};

struct MinecraftRecipeResearchResult
{
	uint64_t request_id;
	uint64_t inventory_revision;
	uint64_t random_value;
	uint32_t recipe_id;
	bool replayed;
};

struct MinecraftEquipmentRepairQuote
{
	int32_t equipment_slot;
	int32_t equipment_item_id;
	uint64_t equipment_instance_id;
	uint32_t current_durability;
	uint32_t maximum_durability;
	int32_t material_item_id;
	int32_t material_quantity;
	int32_t material_quantity_available;
	uint64_t inventory_revision;
	bool repairable;
};

struct MinecraftCombatActionResult
{
	uint64_t request_id;
	uint32_t target_entity_id;
	uint64_t target_revision;
	int32_t target_health;
	uint32_t damage;
	bool lethal;
	bool replayed;
};

class GameSession
{
	friend class WorldRevisionValidator;
	friend class BlockEditValidator;
  public:
	using RecipeDiscoveryAuthorizer = bool (*)(uint32_t source_type_id,
		uint32_t source_id, uint64_t source_event_id,
		void *user_data) noexcept;
	using ScriptCatalogAuthorizer = bool (*)(const uint8_t *bytes,
		uint32_t byte_count, uint64_t expected_manifest_hash,
		void *user_data) noexcept;
	using ScriptContextAuthorizer = ft_bool (*)(uint32_t key_id,
		int64_t value, ft_bool is_set, void *user_data) noexcept;
	using ProcessingStationAuthorizer = bool (*)(uint32_t station_id,
		int32_t station_x, int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision, double player_x, double player_y,
		double player_z, void *user_data) noexcept;
	using ProcessingStationContinuityValidator = bool (*)(
		uint32_t station_id, int32_t station_x, int32_t station_y,
		int32_t station_z, uint64_t expected_station_revision,
		void *user_data) noexcept;
	/* Supplies the authoritative occupied chunk for every connected player.
	 * The callback replaces the single-player camera-derived occupancy path. */
	using RevisionOccupancyProvider = int32_t (*)(
		std::vector<World::RevisionChunkCoordinate> &chunks_out,
		void *user_data) noexcept;
	/* Returns the host's restart-stable monotonic epoch in milliseconds. */
	using RevisionClockProvider = int32_t (*)(uint64_t *epoch_ms_out,
		void *user_data) noexcept;
	/* Host binds user_data to the authenticated session owner and checks the
	 * required late-game/boss progression for this card use. */
	using VolcanoMarkUseAuthorizer = bool (*)(uint32_t card_instance_id,
		uint32_t revision_identifier, void *user_data) noexcept;
	using BoatActorAuthorizer = bool (*)(uint64_t authenticated_actor_id,
		void *user_data) noexcept;
	using CombatActorAuthorizer = bool (*)(uint64_t authenticated_actor_id,
		void *user_data) noexcept;
	using BoatNetworkActorResolver = bool (*)(
		const uint8_t authenticated_public_key[32],
		uint64_t *authenticated_actor_id_out, void *user_data) noexcept;
	/* Host delivery callbacks must be idempotent and resumable by
	 * (session_id, sequence). A replayed prepare must return the same durable
	 * reservation, including after an earlier failed prepare was aborted. */
	using RegenerationHostOutputPrepare = int32_t (*)(
		const RegenerationHostOutput &output, void *user_data) noexcept;
	using RegenerationHostOutputCommit = int32_t (*)(
		const RegenerationHostOutput &output, void *user_data) noexcept;
	using RegenerationHostOutputAbort = int32_t (*)(
		const RegenerationHostOutput &output, void *user_data) noexcept;
	static bool authorize_recipe_discovery_event(
		RecipeDiscoveryAuthorizer authorizer, uint32_t source_type_id,
		uint32_t source_id, uint64_t source_event_id,
		void *user_data) noexcept;
	static bool authorize_script_catalog(
		ScriptCatalogAuthorizer authorizer, const uint8_t *bytes,
		uint32_t byte_count, uint64_t expected_manifest_hash,
		void *user_data) noexcept;
	static bool authorize_volcano_mark_use(
		VolcanoMarkUseAuthorizer authorizer, uint32_t card_instance_id,
		uint32_t revision_identifier, void *user_data) noexcept;
	static bool authorize_local_processing_station(uint32_t station_id,
		int32_t station_x, int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision, double player_x, double player_y,
		double player_z, void *user_data) noexcept;
	static bool validate_local_processing_station_continuity(
		uint32_t station_id, int32_t station_x, int32_t station_y,
		int32_t station_z, uint64_t expected_station_revision,
		void *user_data) noexcept;

	static const char *BIOME_NAMES[5];

	enum class Action
	{
		CONTINUE,
		OPEN_SETTINGS,
		EXIT_TO_MENU,
		FAILED
	};

	GameSession();
	GameSession(const GameSession &other);
	~GameSession();
	GameSession &operator=(const GameSession &other);

	int start(const std::string &seed, ApplicationWindow &window,
		VoxelRenderer &renderer);
	void set_voxel_generation_config(const voxel_generation_config &config);
	void stop();
	bool is_active() const;
	bool is_ready_to_play() const;
	void activate_configured_render_distance();
	int error_code() const;
	/* Configure a script catalogue only after the separately installed host
	 * authorizer approves its bytes and expected manifest hash. */
	int32_t configure_regeneration_script_catalog(const uint8_t *bytes,
		uint32_t byte_count, uint64_t trusted_manifest_hash) noexcept;
	/* The host owns user_data and keeps it alive until replacement/session
	 * destruction. A missing authorizer rejects all script catalogues. */
	int32_t configure_script_catalog_authorizer(
		ScriptCatalogAuthorizer authorizer, void *user_data) noexcept;
	/* The host owns user_data and keeps it alive until replacement/session
	 * destruction. Missing or rejecting context authorization fails closed. */
	int32_t configure_script_context_authorizer(
		ScriptContextAuthorizer authorizer, void *user_data) noexcept;
	/* Install the host authorizer before configuring the sorted context schema
	 * and initial values. Later value changes require the same authorization. */
	int32_t configure_regeneration_script_context(
		const uint32_t *key_ids, const int64_t *values,
		uint32_t entry_count) noexcept;
	int32_t set_regeneration_script_context_value(uint32_t key_id,
		int64_t value) noexcept;
	int32_t clear_regeneration_script_context_value(uint32_t key_id) noexcept;
	int32_t configure_chunk_instability_policy(
		const WorldChunkInstabilityPolicy &policy) noexcept;
	int32_t configure_crop_growth_policy(
		const CropGrowthPolicy &policy) noexcept;
	int32_t configure_ore_overgrowth_instability_delta(
		int32_t positive_delta) noexcept;
	int32_t configure_ultimate_greed_instability_delta(
		int32_t positive_delta) noexcept;
	int32_t configure_biome_rupture_instability_delta(
		int32_t positive_delta) noexcept;
	int32_t configure_volcano_mark_instability_delta(
		int32_t positive_delta) noexcept;
	int32_t configure_ruined_village_instability_delta(
		int32_t positive_delta) noexcept;
	int32_t configure_shipwreck_instability_delta(
		int32_t positive_delta) noexcept;
	int32_t configure_rare_structure_instability_deltas(
		int32_t pyramid_delta, int32_t canopy_delta,
		int32_t spider_nest_delta) noexcept;
	int32_t configure_unstable_catalyst_instability_delta(
		int32_t positive_delta) noexcept;
	int32_t configure_draw_instability_deltas(int32_t empty_draw_delta,
		int32_t hand_overflow_delta) noexcept;
	int32_t configure_volatile_stabilizer_policy(const uint32_t *risk_basis_points,
		uint32_t risk_tier_count, int32_t success_reduction,
		int32_t overload_surge) noexcept;
	int32_t configure_volcano_mark_footprint(uint32_t chunk_width) noexcept;
	/* Returns the card preview with this session's configured feature footprint. */
	int32_t get_regeneration_card_preview(uint32_t card_definition_id,
		RegenerationCardPreview *preview_out) const noexcept;
	/* Must be installed by the authenticated host before start(); missing or
	 * rejecting authorization prevents Volcano Mark from being consumed. The
	 * host owns user_data and keeps it alive until replacement/destruction. */
	int32_t configure_volcano_mark_use_authorizer(
		VolcanoMarkUseAuthorizer authorizer, void *user_data) noexcept;
	/* Network hosts install an authenticated actor validator before start.
	 * Actor-aware boat actions fail closed if this callback is absent or rejects
	 * the actor. */
	int32_t configure_boat_actor_authorizer(
		BoatActorAuthorizer authorizer, void *user_data) noexcept;
	/* Network hosts install this before start; player combat fails closed until
	 * the authenticated actor is accepted by this callback. */
	int32_t configure_combat_actor_authorizer(
		CombatActorAuthorizer authorizer, void *user_data) noexcept;
	/* Sets the authoritative melee cooldown in simulation ticks before start. */
	int32_t configure_melee_attack_interval(uint32_t interval_ticks) noexcept;
	int32_t configure_boat_network_actor_resolver(
		BoatNetworkActorResolver resolver, void *user_data) noexcept;
	static int32_t handle_network_boat_move(
		const uint8_t authenticated_public_key[32],
		const ProtocolBoatMoveIntentMessage &intent,
		ProtocolBoatMoveResultMessage &result, void *user_data) noexcept;
	/* The authenticated host installs this before start(). user_data must remain
	 * valid until the callback is replaced or the session is destroyed. Grants
	 * fail closed until a source event is approved by that authority. */
	int32_t configure_recipe_discovery_authorizer(
		RecipeDiscoveryAuthorizer authorizer, void *user_data) noexcept;
	/* Trusted host binds discovery evidence to a locked recipe before start. */
	int32_t configure_recipe_discovery_source(uint32_t source_type_id,
		uint32_t source_id, uint32_t recipe_id) noexcept;
	/* Set one furnace recipe's fuel and duration before start(); the fuel item
	 * must be in the Minecraft item catalogue. */
	int32_t configure_processing_recipe_policy(uint32_t recipe_id,
		int32_t fuel_item_id,
		uint32_t fuel_units_per_item, uint32_t ticks_per_item) noexcept;
	/* Host validates the live station block/revision, reach, claims and access.
	 * Configure before start(); keep user_data valid until replacement or
	 * session destruction. Processing fails closed until this is installed. */
	int32_t configure_processing_station_authorizer(
		ProcessingStationAuthorizer authorizer, void *user_data) noexcept;
	/* This callback checks station existence and ongoing host access for queued
	 * work; it must not require the player to stay within interaction range. */
	int32_t configure_processing_station_continuity_validator(
		ProcessingStationContinuityValidator validator,
		void *user_data) noexcept;
	/* Installs the single-player host policy. Network hosts must supply their
	 * own authenticated authorization callback instead. */
	int32_t configure_local_processing_station_authorizer() noexcept;
	/* Network hosts install an authoritative all-player occupancy feed before
	 * start and keep user_data alive through the session. Revision-server
	 * binding and start commits fail closed if the feed cannot be refreshed. */
	int32_t configure_revision_occupancy_provider(
		RevisionOccupancyProvider provider, void *user_data) noexcept;
	int32_t configure_revision_clock_provider(RevisionClockProvider provider,
		void *user_data) noexcept;

	int loading_tick(const RenderDistanceStrategy &strategy);
	Action update(double delta_seconds, const RenderDistanceStrategy &strategy,
		ApplicationWindow &window);
	void render(ApplicationWindow &window, VoxelRenderer &renderer,
		bool with_overlay);
	MinecraftItemCatalog &item_catalog();
	const MinecraftRecipeCatalog &recipe_catalog() const;
	WorldItemStore &world_items();
	/* Entity inspection is read-only through GameSession. Mutations must use
	 * authoritative session actions so ownership and regeneration barriers apply. */
	const WorldEntityStore &world_entities() const;
	WorldSpawnerStore &world_spawners();
	GameTransactionJournal &transaction_journal();
	const RegenerationCardSession &regeneration_cards() const;
	const RegenerationWorldRules &regeneration_world_rules() const;
	int32_t play_regeneration_card(uint64_t request_id,
		uint32_t card_instance_id, const RegenerationCardChoice &choice,
		RegenerationPolicyIntent *intent_out) noexcept;
	/* Read-only status for a physical hand-card instance and proposed choice.
	 * status_out receives the preflight result; no card/rule state is changed. */
	int32_t query_regeneration_card_effect_status(
		uint32_t card_instance_id, const RegenerationCardChoice &choice,
		uint32_t *card_definition_id_out, int32_t *status_out,
		uint32_t *no_effect_choice_mask_out) const noexcept;
	int32_t query_regeneration_card_effect_status(
		uint32_t card_instance_id, const RegenerationCardChoice &choice,
		uint32_t *card_definition_id_out, int32_t *status_out,
		uint32_t *no_effect_choice_mask_out,
		const char **reason_out) const noexcept;
	int32_t query_regeneration_card_effect_choices(
		uint32_t card_instance_id,
		/* Candidate coordinates are suggestions only; GameSession rechecks
		 * loaded/protected/selected state before returning any target. */
		const std::vector<RegenerationTargetChunk> &chunk_candidates,
		std::vector<RegenerationCardChoice> &choices_out) const noexcept;
	int32_t draw_regeneration_cards(uint64_t request_id,
		uint64_t source_host_intent_sequence, uint32_t draw_count,
		uint32_t required_discard_count,
		RegenerationCardActionResult *result_out) noexcept;
	int32_t discard_regeneration_hand_card(uint64_t request_id,
		uint64_t source_draw_request_id, uint32_t card_instance_id,
		RegenerationCardActionResult *result_out) noexcept;
	int32_t import_regeneration_card_item_to_deck(int32_t inventory_slot,
		uint64_t expected_inventory_revision, uint64_t request_id,
		card_game_deck_card *card_out) noexcept;
	int32_t export_regeneration_card_to_inventory(
		uint32_t card_instance_id, uint64_t expected_inventory_revision,
		uint64_t request_id,
		RegenerationCardTransferResult *result_out) noexcept;
	int32_t apply_pending_regeneration_card_intent(
		bool *replayed_out) noexcept;
	int32_t consume_pending_regeneration_draw_notice(
		card_game_host_intent_record *notice_out) noexcept;
	/* Host-only view of generic pending typed outputs. Consumers must durably
	 * accept a stable sequence before acknowledging it; retries after a saved
	 * acknowledgement return success for that same sequence. */
	int32_t query_pending_regeneration_host_intents(
		std::vector<card_game_host_intent_record> &records_out) const noexcept;
	/* Set a host ID before start; it is persisted in the session snapshot. */
	int32_t configure_regeneration_output_session(uint64_t session_id) noexcept;
	int32_t get_regeneration_output_session_id(
		uint64_t *session_id_out) const noexcept;
	int32_t query_pending_regeneration_host_outputs(
		std::vector<RegenerationHostOutput> &outputs_out) const noexcept;
	int32_t acknowledge_regeneration_host_output(uint64_t session_id,
		uint64_t host_intent_sequence) noexcept;
	int32_t configure_regeneration_host_output_consumer(
		RegenerationHostOutputPrepare prepare,
		RegenerationHostOutputCommit commit,
		RegenerationHostOutputAbort abort, void *user_data) noexcept;
	int32_t deliver_next_regeneration_host_output() noexcept;
	int32_t recover_regeneration_host_outputs() noexcept;
	int32_t resolve_expired_regeneration_choices(
		uint64_t request_id,
		uint32_t *resolved_count_out) noexcept;
	int32_t begin_card_regeneration_revision() noexcept;
	int32_t bind_world_revision_server(
		WorldRevisionServer &server) noexcept;
	int32_t commit_regeneration_start(uint64_t actor_id, uint64_t loom_id,
		uint64_t request_id, uint64_t expected_selection_revision,
		uint64_t expected_policy_revision,
		uint32_t opening_card_count) noexcept;
	int32_t finalize_regeneration_start(
		uint64_t authoritative_epoch) noexcept;
	/* Trusted host recovery path; retries a failed generation attempt without
	 * clearing its reserved-chunk barrier or charging/drawing again. */
	int32_t retry_failed_regeneration() noexcept;
	int32_t query_hand_recipe(uint32_t recipe_id,
		game_crafting_query_result *result,
		ft_vector<game_crafting_missing_ingredient> *missing_groups) const noexcept;
	int32_t query_station_recipe(uint32_t recipe_id, uint32_t station_id,
		int32_t station_x, int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision,
		game_crafting_query_result *result,
		ft_vector<game_crafting_missing_ingredient> *missing_groups) const noexcept;
	int32_t grant_recipe_discovery(uint32_t source_type_id,
		uint32_t source_id, uint64_t source_event_id,
		ft_bool *newly_unlocked) noexcept;
	int32_t grant_recipe_discovery_with_result(uint32_t source_type_id,
		uint32_t source_id, uint64_t source_event_id,
		game_recipe_unlock_result *result_out) noexcept;
	/* Card Press research spends two card-stock items to deterministically
	 * discover one still-locked rare card recipe. The host validates station
	 * reach, ownership and revision using the configured station authorizer. */
	int32_t research_card_recipe(uint64_t request_id,
		uint64_t expected_inventory_revision, int32_t station_x,
		int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision,
		MinecraftRecipeResearchResult *result_out) noexcept;
	int32_t craft_hand_recipe(uint64_t request_id, uint32_t recipe_id,
		int32_t batch_count, uint64_t expected_inventory_revision,
		MinecraftCraftActionResult *result_out) noexcept;
	int32_t craft_station_recipe(uint64_t request_id, uint32_t recipe_id,
		int32_t batch_count, uint64_t expected_inventory_revision,
		uint32_t station_id, int32_t station_x, int32_t station_y,
		int32_t station_z, uint64_t expected_station_revision,
		MinecraftCraftActionResult *result_out) noexcept;
	/* Moves one catalogued physical item from inventory into its matching
	 * character equipment slot, returning any replaced item to inventory. */
	int32_t equip_inventory_item(uint64_t request_id, int32_t inventory_slot,
		uint64_t expected_inventory_revision,
		uint32_t equipment_slot) noexcept;
	/* Returns the selected equipped physical item to inventory. */
	int32_t unequip_equipment_item(uint64_t request_id,
		uint64_t expected_inventory_revision,
		uint32_t equipment_slot) noexcept;
	/* Repairs one damaged physical equipment instance at an authorized
	 * workbench, consuming the catalog-defined tier material transactionally. */
	int32_t repair_equipment(uint64_t request_id, int32_t equipment_slot,
		uint64_t expected_inventory_revision, int32_t station_x,
		int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision) noexcept;
	int32_t query_equipment_repair(int32_t equipment_slot,
		uint64_t expected_inventory_revision, int32_t station_x,
		int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision,
		MinecraftEquipmentRepairQuote *quote_out) const noexcept;
	/* Applies a journalled, revision-checked melee hit with the equipped
	 * main-hand weapon after authoritative range, facing, and obstruction checks. */
	int32_t attack_entity(uint64_t request_id, uint32_t target_entity_id,
		uint64_t expected_entity_revision,
		MinecraftCombatActionResult *result_out) noexcept;
	int32_t attack_entity_for_actor(uint64_t request_id,
		uint64_t authenticated_actor_id, uint32_t target_entity_id,
		uint64_t expected_entity_revision,
		MinecraftCombatActionResult *result_out) noexcept;
	/* Consumes one arrow and launches a checkpointed projectile from the
	 * authoritative camera pose while a catalogued bow is equipped. */
	int32_t launch_arrow(uint64_t request_id,
		uint64_t expected_inventory_revision) noexcept;
	int32_t launch_arrow_for_actor(uint64_t request_id,
		uint64_t authenticated_actor_id,
		uint64_t expected_inventory_revision) noexcept;
	/* Starts or ends the bounded server-side shield guard action. The caller
	 * must forward authenticated input to the authoritative simulation thread. */
	int32_t set_shield_guarding(bool guarding) noexcept;
	int32_t start_recipe_processing(uint64_t request_id, uint32_t recipe_id,
		int32_t batch_count, uint64_t expected_inventory_revision,
		int32_t station_x, int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision,
		MinecraftProcessingActionResult *result_out) noexcept;
	int32_t transfer_fluid_bucket(uint64_t request_id, int32_t inventory_slot,
		uint64_t expected_inventory_revision, bool pickup_source,
		int32_t world_x, int32_t world_y, int32_t world_z,
		uint64_t expected_voxel_revision) noexcept;
	int32_t place_inventory_block(uint64_t request_id, int32_t inventory_slot,
		int32_t item_id, uint64_t expected_inventory_revision,
		int32_t world_x, int32_t world_y, int32_t world_z,
		uint64_t expected_voxel_revision) noexcept;
	int32_t till_farmland(uint64_t request_id, int32_t inventory_slot,
		uint64_t expected_inventory_revision, int32_t world_x, int32_t world_y,
		int32_t world_z, uint64_t expected_voxel_revision) noexcept;
	int32_t place_boat_from_inventory(uint64_t request_id,
		int32_t inventory_slot, uint64_t expected_inventory_revision,
		int32_t water_x, int32_t water_y, int32_t water_z,
		uint64_t expected_voxel_revision,
		WorldBoatTransferTransaction::Result *result_out) noexcept;
	/* Network hosts pass the authenticated actor ID here. An installed host
	 * authorizer validates it; the ID is persisted with the boat and must match
	 * on later retrieval. */
	int32_t place_boat_from_inventory_for_actor(uint64_t request_id,
		uint64_t authenticated_actor_id, int32_t inventory_slot,
		uint64_t expected_inventory_revision, int32_t water_x, int32_t water_y,
		int32_t water_z, uint64_t expected_voxel_revision,
		WorldBoatTransferTransaction::Result *result_out) noexcept;
	int32_t retrieve_boat_to_inventory(uint64_t request_id,
		uint32_t entity_id, uint64_t expected_entity_revision,
		uint64_t expected_inventory_revision,
		WorldBoatTransferTransaction::Result *result_out) noexcept;
	int32_t retrieve_boat_to_inventory_for_actor(uint64_t request_id,
		uint64_t authenticated_actor_id, uint32_t entity_id,
		uint64_t expected_entity_revision,
		uint64_t expected_inventory_revision,
		WorldBoatTransferTransaction::Result *result_out) noexcept;
	/* Applies one bounded server-authoritative step to an owned boat. The
	 * endpoint must remain on loaded water and outside protected/barrier chunks. */
	int32_t move_boat_for_actor(uint64_t request_id,
		uint64_t authenticated_actor_id, uint32_t entity_id,
		uint64_t expected_entity_revision, double next_x, double next_z,
		double yaw, uint64_t *new_entity_revision_out) noexcept;

  private:
	int32_t process_regeneration_host_output_transaction(
		const RegenerationHostOutput &output) noexcept;
	int32_t acknowledge_regeneration_host_intent_internal(
		uint64_t host_intent_sequence) noexcept;
	int32_t resolve_expired_regeneration_choices_internal(
		uint64_t request_id, uint64_t authoritative_epoch,
		uint32_t *resolved_count_out) noexcept;
	struct ConfiguredRecipeDiscoverySource
	{
		uint32_t source_type_id;
		uint32_t source_id;
		uint32_t recipe_id;
	};
	int32_t try_place_selected_inventory_block() noexcept;
	int32_t try_toggle_target_trapdoor() noexcept;
	int32_t try_till_selected_soil() noexcept;
	int32_t attack_entity_internal(uint64_t actor_id, uint64_t request_id,
		uint32_t target_entity_id, uint64_t expected_entity_revision,
		MinecraftCombatActionResult *result_out) noexcept;
	int32_t launch_arrow_internal(uint64_t actor_id, uint64_t request_id,
		uint64_t expected_inventory_revision) noexcept;
	int32_t try_launch_equipped_bow() noexcept;
	int32_t query_equipped_armor_cached(uint32_t *armor_points_out) noexcept;
	int32_t query_main_hand_equipment_rule_cached(const game_item &item,
		MinecraftEquipmentRule *rule_out) noexcept;
	int32_t try_attack_aimed_entity() noexcept;
	Camera camera_;
	game_character player_character_;
	MinecraftItemCatalog item_catalog_;
	MinecraftRecipeCatalog recipe_catalog_;
	WorldItemStore world_items_;
	WorldEntityStore world_entities_;
	WorldSpawnerStore world_spawners_;
	GameTransactionJournal transaction_journal_;
	/* Serializes session APIs that scan or move physical item IDs. */
	mutable std::mutex item_instance_transaction_mutex_;
	RegenerationCardSession regeneration_cards_;
	RegenerationWorldRules regeneration_world_rules_;
	RegenerationWorldRulesPolicy regeneration_world_rules_policy_;
	std::vector<uint8_t> regeneration_script_catalog_;
	std::vector<std::pair<uint32_t, int64_t>> _regeneration_script_context;
	std::vector<ft_bool> _regeneration_script_context_is_set;
	std::vector<uint8_t> fluid_state_;
	std::vector<uint8_t> entity_state_;
	std::vector<uint8_t> revision_server_checkpoint_state_;
	uint64_t trusted_script_manifest_hash_;
	uint64_t regeneration_output_session_id_;
	uint64_t configured_regeneration_output_session_id_;
	RegenerationHostOutputPrepare regeneration_host_output_prepare_ = nullptr;
	RegenerationHostOutputCommit regeneration_host_output_commit_ = nullptr;
	RegenerationHostOutputAbort regeneration_host_output_abort_ = nullptr;
	void *regeneration_host_output_user_data_ = nullptr;
	WorldChunkInstabilityPolicy chunk_instability_policy_;
	bool chunk_instability_policy_configured_;
	int32_t ore_overgrowth_instability_delta_;
	int32_t ultimate_greed_instability_delta_;
	int32_t biome_rupture_instability_delta_;
	int32_t volcano_mark_instability_delta_;
	int32_t ruined_village_instability_delta_ = 0;
	int32_t shipwreck_instability_delta_ = 0;
	int32_t pyramid_instability_delta_ = 0;
	int32_t canopy_instability_delta_ = 0;
	int32_t spider_nest_instability_delta_ = 0;
	int32_t unstable_catalyst_instability_delta_ = 0;
	int32_t empty_draw_instability_delta_;
	int32_t hand_overflow_instability_delta_;
	uint32_t volcano_mark_footprint_chunks_;
	CropGrowthPolicy crop_growth_policy_;
	std::vector<CropChunkGrowthDeadline> crop_chunk_growth_deadlines_;
	uint32_t volatile_stabilizer_risk_tier_count_ = 0U;
	uint32_t volatile_stabilizer_risk_basis_points_[
		RegenerationWorldRulesPolicy::MAXIMUM_STABILIZER_RISK_TIERS] = {};
	int32_t volatile_stabilizer_success_reduction_ = 0;
	int32_t volatile_stabilizer_overload_surge_ = 0;
	VolcanoMarkUseAuthorizer volcano_mark_use_authorizer_ = nullptr;
	void *volcano_mark_use_authorizer_user_data_ = nullptr;
	BoatActorAuthorizer boat_actor_authorizer_ = nullptr;
	void *boat_actor_authorizer_user_data_ = nullptr;
	CombatActorAuthorizer combat_actor_authorizer_ = nullptr;
	void *combat_actor_authorizer_user_data_ = nullptr;
	BoatNetworkActorResolver boat_network_actor_resolver_ = nullptr;
	void *boat_network_actor_resolver_user_data_ = nullptr;
	RecipeDiscoveryAuthorizer recipe_discovery_authorizer_;
	void *recipe_discovery_authorizer_user_data_;
	std::vector<ConfiguredRecipeDiscoverySource>
		recipe_discovery_sources_;
	ScriptCatalogAuthorizer script_catalog_authorizer_;
	void *script_catalog_authorizer_user_data_;
	ScriptContextAuthorizer _script_context_authorizer = nullptr;
	void *_script_context_authorizer_user_data = nullptr;
	ProcessingStationAuthorizer processing_station_authorizer_;
	void *processing_station_authorizer_user_data_;
	ProcessingStationContinuityValidator
		processing_station_continuity_validator_;
	void *processing_station_continuity_user_data_;
	RevisionOccupancyProvider revision_occupancy_provider_;
	void *revision_occupancy_provider_user_data_;
	RevisionClockProvider revision_clock_provider_;
	void *revision_clock_provider_user_data_;
	World world_;
	RenderDebug render_debug_;
	std::vector<World::RevisionChunkCoordinate> revision_occupied_chunks_;
	int32_t revision_occupied_chunk_x_;
	int32_t revision_occupied_chunk_z_;
	bool revision_occupancy_valid_;
	bool world_loom_start_committed_;

    int32_t active_render_distance_;
    double fps_accumulator_;
    double rd_accumulator_;
    double display_fps_;
    double frame_ms_;
    double performance_frame_ms_;
    uint64_t fps_frame_count_;
    uint32_t selected_block_id_;
    bool boost_enabled_;
    bool active_;
    bool revision_preview_visible_;
    uint64_t render_debug_frame_;
    uint32_t cached_ram_mb_;
    uint32_t cached_vram_mb_;
    int32_t cached_biome_world_x_;
    int32_t cached_biome_world_z_;
    bool cached_biome_valid_;
    char cached_biome_name_[24];
    int32_t revision_preview_center_x_;
    int32_t revision_preview_center_z_;
    uint32_t revision_preview_identifier_;
    bool revision_preview_cache_valid_;
    std::vector<World::RevisionPreviewEntry> revision_preview_cache_;
    uint64_t pickup_request_id_;
	double processing_tick_accumulator_;
	bool shield_guard_active_;
	uint64_t shield_guard_until_tick_;
	uint64_t shield_guard_cooldown_until_tick_;
	uint32_t melee_attack_interval_ticks_ = 10U;
	uint64_t next_melee_attack_tick_ = 0U;
	bool equipped_armor_cache_valid_ = false;
	uint64_t equipped_armor_fingerprint_[16] = {};
	uint32_t equipped_armor_points_cache_ = 0U;
	bool main_hand_rule_cache_valid_ = false;
	uint64_t main_hand_rule_fingerprint_[4] = {};
	MinecraftEquipmentRule main_hand_rule_cache_ = {};
    int32_t error_code_;
    char seed_[32];
    char world_item_save_path_[96];
	char world_revision_save_path_[96];
	double player_spawn_x_;
	double player_spawn_z_;

	Action handle_navigation(ApplicationWindow &window);
	Action tick_world(double delta_seconds,
		const RenderDistanceStrategy &strategy);
	int32_t advance_nearby_crop_growth() noexcept;
	void tick_fps_counter(double delta_seconds,
		const RenderDistanceStrategy &strategy);
	void build_render_debug(VoxelRenderer &renderer);
	void sync_player_character_location();
	int32_t update_revision_occupancy() noexcept;
	int32_t recover_pending_transactions() noexcept;
	int32_t recover_pending_recipe_research() noexcept;
	int32_t recover_pending_regeneration_draw_notices() noexcept;
	int32_t recover_pending_block_placements() noexcept;
	int32_t recover_pending_farmland_tillings() noexcept;
	int32_t synchronize_item_instance_id_allocator() noexcept;
	int32_t validate_physical_item_identity_uniqueness() const noexcept;
	int32_t initialize_regeneration_world_rules() noexcept;
	int32_t process_spawner_deaths(
		const std::vector<WorldSpawnerDeath> &deaths) noexcept;
	int32_t process_animal_production(
		const std::vector<WorldAnimalProductionEvent> &events) noexcept;
	int32_t save_session_snapshot() noexcept;
	static int32_t save_snapshot_callback(void *context) noexcept;
	static int32_t revision_start_commit_callback(uint64_t actor_id,
		uint64_t loom_id, uint64_t request_id,
		uint64_t expected_selection_revision,
		uint64_t expected_policy_revision, uint32_t opening_card_count,
		void *user_data) noexcept;
	static int32_t revision_occupancy_provider_callback(
		std::vector<World::RevisionChunkCoordinate> &chunks_out,
		void *user_data);
	static int32_t revision_checkpoint_committer_callback(
		const uint8_t *bytes, std::size_t byte_count, void *user_data) noexcept;
	int32_t pickup_nearby_ground_items() noexcept;
	void reset_session_state();
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	void report_loading_gaps() const;
#endif
};

#endif
