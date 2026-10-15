#ifndef REGENERATION_CARD_SESSION_HPP
# define REGENERATION_CARD_SESSION_HPP

# include "../../Libft/Modules/CardGame/card_game.hpp"
# include "../../Libft/Modules/CardGame/card_game_script_bridge.hpp"
# include "../../Libft/Modules/Game/game_inventory.hpp"
# include "../../Libft/Modules/Voxel/voxel_api.hpp"
# include <cstdint>
# include <cstddef>
# include <map>
# include <vector>

enum RegenerationCardEffectKind
{
	REGENERATION_CARD_EFFECT_BIOME_EXCLUSION = 1,
	REGENERATION_CARD_EFFECT_DEEP_SEAM = 2,
	REGENERATION_CARD_EFFECT_VILLAGE_CHARTER = 3,
	REGENERATION_CARD_EFFECT_SURFACE_MAGMA = 4,
	REGENERATION_CARD_EFFECT_DOUBLE_DRAFT = 5,
	REGENERATION_CARD_EFFECT_BIOME_COMPASS = 6,
	REGENERATION_CARD_EFFECT_SURVEYORS_REACH = 7,
	REGENERATION_CARD_EFFECT_ORE_OVERGROWTH = 8,
	REGENERATION_CARD_EFFECT_ULTIMATE_GREED = 9,
	REGENERATION_CARD_EFFECT_BIOME_RUPTURE = 10,
	REGENERATION_CARD_EFFECT_FORTUNES_DRAW = 11,
	REGENERATION_CARD_EFFECT_VOLCANO_MARK = 12,
	REGENERATION_CARD_EFFECT_VOLATILE_STABILIZER = 13,
	REGENERATION_CARD_EFFECT_RUINED_VILLAGE_RELIC = 14,
	REGENERATION_CARD_EFFECT_SHIPWRECK_CHART = 15,
	REGENERATION_CARD_EFFECT_UNSTABLE_CATALYST = 16,
	REGENERATION_CARD_EFFECT_DESERT_PYRAMID_SEAL = 17,
	REGENERATION_CARD_EFFECT_ANCIENT_CANOPY_SEAL = 18,
	REGENERATION_CARD_EFFECT_SPIDER_NEST_SEAL = 19
};

enum RegenerationCardChoiceKind
{
	REGENERATION_CARD_CHOICE_NONE = 0,
	REGENERATION_CARD_CHOICE_BIOME_ID = 1,
	REGENERATION_CARD_CHOICE_ORE_ID = 2,
	REGENERATION_CARD_CHOICE_TARGET_CHUNK = 3
};

struct RegenerationCardChoice
{
	RegenerationCardChoiceKind kind;
	/* Chunk coordinates use their 32-bit modulo representation here. */
	uint32_t value_primary;
	uint32_t value_secondary;
};

struct RegenerationCardPreview
{
	uint32_t card_definition_id;
	const char *title;
	const char *description;
	RegenerationCardChoiceKind choice_kind;
	uint32_t choice_count;
	/* Zero means the card has no reserved chunk footprint. */
	uint32_t required_chunk_width;
};

struct RegenerationPolicyIntent
{
	uint64_t request_id;
	uint64_t host_intent_sequence;
	uint32_t content_version;
	uint32_t card_definition_id;
	uint32_t card_instance_id;
	RegenerationCardEffectKind effect_kind;
	RegenerationCardChoice choice;
	/* Response metadata: bit 0/1 marks an already-excluded Biome Rupture target. */
	uint32_t no_effect_choice_mask;
	/* Host-only result payload used by hidden, irreversible card outcomes. */
	uint32_t private_result;
	uint32_t private_ordinal;
	bool replayed;
};

struct RegenerationCardActionResult
{
	uint64_t session_id;
	uint64_t request_id;
	uint64_t command_sequence;
	uint32_t count;
	bool replayed;
};

struct RegenerationCardTransferResult
{
	uint64_t request_id;
	uint64_t physical_item_instance_id;
	uint32_t card_instance_id;
	int32_t inventory_slot;
	int32_t status;
	bool replayed;
};

class RegenerationCardSession
{
  public:
	static const uint32_t CONTENT_VERSION = 2U;
	static const uint32_t STARTER_CARD_DEFINITION_COUNT = 7U;
	static const uint32_t ORE_OVERGROWTH_CARD_DEFINITION_ID = 11U;
	static const uint32_t ULTIMATE_GREED_CARD_DEFINITION_ID = 12U;
	static const uint32_t BIOME_RUPTURE_CARD_DEFINITION_ID = 13U;
	static const uint32_t FORTUNES_DRAW_CARD_DEFINITION_ID = 14U;
	static const uint32_t VOLCANO_MARK_CARD_DEFINITION_ID = 15U;
	static const uint32_t VOLATILE_STABILIZER_CARD_DEFINITION_ID = 16U;
	static const uint32_t RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID = 17U;
	static const uint32_t SHIPWRECK_CHART_CARD_DEFINITION_ID = 18U;
	static const uint32_t UNSTABLE_CATALYST_CARD_DEFINITION_ID = 19U;
	static const uint32_t DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID = 20U;
	static const uint32_t ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID = 21U;
	static const uint32_t SPIDER_NEST_SEAL_CARD_DEFINITION_ID = 22U;
	static const uint32_t STARTER_DECK_INSTANCE_COUNT = 20U;
	static const uint32_t MINIMUM_DECK_SIZE = 20U;
	static const uint32_t MAXIMUM_DECK_SIZE = 100U;
	static const uint32_t MAXIMUM_HAND_SIZE = 10U;
	static const uint32_t MAXIMUM_PLAY_REQUESTS = 4096U;
	static const uint32_t MAXIMUM_IMPORTED_CARD_REQUESTS = 4096U;
	static const uint32_t EMPTY_DRAW_INTENT_TYPE_ID;
	static const uint32_t HAND_OVERFLOW_INTENT_TYPE_ID;
	static const uint32_t POLICY_INTENT_TYPE_ID;

	RegenerationCardSession();
	RegenerationCardSession(const RegenerationCardSession &other) = delete;
	~RegenerationCardSession();
	RegenerationCardSession &operator=(
		const RegenerationCardSession &other) = delete;

	int32_t initialize(uint32_t opening_hand_size,
		uint64_t shuffle_seed) noexcept;
	/* Binds sequenced host actions to the owning persisted GameSession. */
	int32_t set_action_session_id(uint64_t session_id) noexcept;
	/* Extends a pristine four-card bootstrap hand to the quoted start size.
	 * The request and result survive adapter snapshot save/restore. */
	int32_t deal_starting_hand(uint64_t request_id,
		uint32_t opening_hand_count,
		RegenerationCardActionResult *result_out) noexcept;
	int32_t destroy() noexcept;
	bool is_initialized() const noexcept;
	int32_t register_script_context_key(uint32_t key_id) noexcept;
	int32_t get_script_context_keys(
		std::vector<uint32_t> &keys_out) const noexcept;
	int32_t load_script_catalog(const uint8_t *bytes,
		uint32_t byte_count, uint64_t trusted_manifest_hash,
		uint64_t policy_manifest_hash) noexcept;
	int32_t get_script_manifest_hash(uint64_t *hash_out) const noexcept;
	bool has_cached_play_request(uint64_t request_id) const noexcept;
	int32_t set_script_context_value(uint32_t key_id,
		int64_t value) noexcept;
	int32_t clear_script_context_value(uint32_t key_id) noexcept;
	int32_t serialize_snapshot(std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t restore_snapshot(const uint8_t *bytes,
		std::size_t byte_count, const uint8_t *script_catalog = nullptr,
		uint32_t script_catalog_size = 0U,
		uint64_t trusted_script_manifest_hash = 0U,
		uint64_t policy_manifest_hash = 0U) noexcept;
	int32_t get_draw_count(uint32_t *count_out) const noexcept;
	int32_t draw_inspect(uint32_t index,
		card_game_deck_card *card_out) const noexcept;
	int32_t get_hand_count(uint32_t *count_out) const noexcept;
	int32_t hand_inspect(uint32_t index,
		card_game_deck_card *card_out) const noexcept;
	int32_t get_discard_count(uint32_t *count_out) const noexcept;
	int32_t discard_inspect(uint32_t index,
		card_game_deck_card *card_out) const noexcept;
	int32_t get_pending_intent_count(uint32_t *count_out) const noexcept;
	/* A sequence below the persisted next-sequence high-water mark that is no
	 * longer queued has already been acknowledged. */
	int32_t is_host_intent_pending(uint64_t sequence,
		ft_bool *pending_out) const noexcept;
	int32_t copy_pending_intents(
		std::vector<card_game_host_intent_record> &records_out) const noexcept;
	int32_t count_pending_card_definition(uint32_t card_definition_id,
		uint32_t *count_out) const noexcept;
	int32_t get_pending_required_discard_count(
		uint32_t *count_out) const noexcept;
	/* Applies the deterministic deadline default (lowest physical hand ID).
	 * The owning GameSession snapshots and checkpoints this operation. */
	int32_t resolve_pending_required_discards_to_default(
		uint64_t request_namespace, uint32_t *resolved_count_out) noexcept;
	int32_t get_pending_choice_count(uint32_t *count_out) const noexcept;
	int32_t peek_pending_intent(
		card_game_host_intent_record *record_out) const noexcept;
	int32_t peek_pending_card_intent(
		RegenerationPolicyIntent *intent_out) const noexcept;
	int32_t is_draw_notice(const card_game_host_intent_record &record,
		bool *is_notice_out) const noexcept;
	int32_t acknowledge_draw_notice(uint64_t host_intent_sequence) noexcept;
	/* Recovery helper: confirms whether a notice is still queued and only
	 * acknowledges it when it remains the queue head. */
	int32_t acknowledge_draw_notice_if_pending(
		uint64_t host_intent_sequence, bool *was_pending_out) noexcept;
	int32_t play_card(uint64_t request_id, uint32_t card_instance_id,
		const RegenerationCardChoice &choice,
		const voxel_generation_config &world_config,
		RegenerationPolicyIntent *intent_out,
		uint32_t stabilizer_risk_basis_points = 0U,
		uint32_t stabilizer_ordinal = 0U,
		const uint32_t *catalyst_options = nullptr,
		uint32_t catalyst_option_count = 0U) noexcept;
	int32_t set_cached_play_no_effect_choice_mask(uint64_t request_id,
		uint32_t no_effect_choice_mask) noexcept;
	int32_t acknowledge_intent(uint64_t host_intent_sequence) noexcept;
	int32_t resolve_expired_choices(uint64_t request_id,
		uint64_t authoritative_epoch,
		uint32_t *resolved_count_out) noexcept;
	int32_t resolve_all_pending_choices_to_default(
		uint32_t *resolved_count_out) noexcept;
	int32_t draw_cards(uint64_t request_id,
		uint64_t source_host_intent_sequence, uint32_t draw_count,
		uint32_t required_discard_count,
		RegenerationCardActionResult *result_out) noexcept;
	int32_t discard_hand_card(uint64_t request_id,
		uint64_t source_draw_request_id, uint32_t card_instance_id,
		RegenerationCardActionResult *result_out) noexcept;
	/* Consumes a crafted physical card item and adds that same owned card to
	 * the draw pile. Request IDs are session-local and monotonically increasing. */
	int32_t import_card_item_to_deck(game_inventory &inventory,
		int32_t inventory_slot, uint64_t expected_inventory_revision,
		uint64_t request_id, card_game_deck_card *card_out) noexcept;
	int32_t resolve_physical_card_instance(uint64_t physical_item_instance_id,
		uint32_t *card_instance_id_out) const noexcept;
	int32_t get_max_physical_item_instance_id(
		uint64_t *instance_id_out) const noexcept;
	int32_t copy_physical_item_instance_ids(
		std::vector<uint64_t> &instance_ids_out) const noexcept;
	static int32_t get_card_preview(uint32_t card_definition_id,
		RegenerationCardPreview *preview_out) noexcept;
	static RegenerationCardEffectKind effect_kind_for_card(
		uint32_t card_definition_id) noexcept;
	static uint32_t card_definition_for_item(
		int32_t item_definition_id) noexcept;
	/* Returns an imported card only when removing it leaves the owned session
	 * deck at or above its 20-card minimum. Simulation-owner-thread only. */
	int32_t export_card_to_inventory(game_inventory &inventory,
		uint32_t card_instance_id, uint64_t expected_inventory_revision,
		uint64_t request_id,
		RegenerationCardTransferResult *result_out) noexcept;

  private:
	friend class RegenerationCardValidator;
	friend class WorldRevisionValidator;
	friend class GameSession;
	int32_t get_cached_play_parameters(uint64_t request_id,
		uint32_t *stabilizer_risk_basis_points_out,
		uint32_t *stabilizer_ordinal_out, uint32_t catalyst_options_out[7],
		uint32_t *catalyst_option_count_out) const noexcept;

	struct CachedPlay
	{
		uint32_t card_instance_id;
		RegenerationCardChoice choice;
		uint32_t stabilizer_risk_basis_points;
		uint32_t stabilizer_ordinal;
		uint32_t catalyst_option_count;
		uint32_t catalyst_options[7];
		bool has_retry_payload;
		RegenerationPolicyIntent result;
	};
	struct CachedDraw
	{
		uint64_t source_host_intent_sequence;
		uint32_t draw_count;
		uint32_t required_discard_count;
		RegenerationCardActionResult result;
	};
	struct CachedDiscard
	{
		uint64_t source_draw_request_id;
		uint32_t card_instance_id;
		RegenerationCardActionResult result;
	};
	struct CachedExpiry
	{
		uint64_t authoritative_epoch;
		uint64_t command_sequence;
		uint32_t resolved_count;
	};
	struct CachedCardImport
	{
		int32_t inventory_slot;
		uint64_t expected_inventory_revision;
		uint64_t physical_item_instance_id;
		int32_t item_definition_id;
		int32_t result_code;
		card_game_deck_card result_card;
	};
	struct PhysicalCardLink
	{
		uint32_t card_instance_id;
		game_inventory_item_snapshot item;
	};
	struct CachedCardExport
	{
		uint32_t card_instance_id;
		uint64_t expected_inventory_revision;
		RegenerationCardTransferResult result;
	};

	static const uint32_t PLAYER_ID;
	static const uint32_t CARD_TYPE_ID;
	static const uint32_t DRAW_ZONE_ID;
	static const uint32_t HAND_ZONE_ID;
	static const uint32_t RESOLUTION_ZONE_ID;
	static const uint32_t DISCARD_ZONE_ID;
	static const uint32_t BURNED_ZONE_ID;
	static const uint32_t CARD_EFFECT_EVENT_BASE;
	card_game_engine engine_;
	card_game_script_bridge script_bridge_;
	std::map<uint64_t, CachedPlay> play_cache_;
	std::map<uint64_t, CachedDraw> draw_cache_;
	std::map<uint64_t, CachedDiscard> discard_cache_;
	std::map<uint64_t, CachedExpiry> expiry_cache_;
	std::map<uint64_t, CachedCardImport> card_import_cache_;
	std::map<uint64_t, CachedCardExport> card_export_cache_;
	std::map<uint64_t, PhysicalCardLink> physical_card_instances_;
	uint64_t highest_card_transfer_request_id_;
	uint64_t action_session_id_;
	uint32_t next_imported_card_instance_id_;
	uint64_t next_command_sequence_;
	uint64_t starting_hand_request_id_;
	uint32_t starting_hand_count_;
	uint64_t pending_double_draft_intent_sequence_;
	uint32_t pending_required_discard_count_;
	uint64_t active_request_id_;
	RegenerationCardChoice active_choice_;
	uint32_t active_private_result_;
	uint32_t active_private_ordinal_;
	uint32_t active_catalyst_options_[7] = {};
	uint32_t active_catalyst_option_count_ = 0U;
	bool initialized_;

	static int32_t effect_callback(const card_game_engine &engine,
		const card_game_effect_context &context,
		card_game_operation_buffer &operations, void *user_data) noexcept;
	static uint32_t card_definition_id_for_event(uint32_t event_type) noexcept;
	static bool is_supported_card_definition(
		uint32_t card_definition_id) noexcept;
	static ft_bool choice_is_valid(uint32_t card_definition_id,
		const RegenerationCardChoice &choice) noexcept;
	static int32_t decode_policy_intent(
		const card_game_host_intent_record &record,
		RegenerationPolicyIntent *intent_out) noexcept;
	static int32_t validate_choice_in_world(uint32_t card_definition_id,
		const RegenerationCardChoice &choice,
		const voxel_generation_config &world_config) noexcept;
	int32_t register_profile() noexcept;
	int32_t count_card_definition(uint32_t card_definition_id,
		uint32_t *count_out) const noexcept;
	int32_t count_owned_session_cards(uint32_t *count_out) const noexcept;
	int32_t seed_starter_deck(uint32_t opening_hand_size,
		uint64_t shuffle_seed) noexcept;
	int32_t locate_hand_card(uint32_t card_instance_id,
		card_game_deck_card *card_out) const noexcept;
};

#endif
