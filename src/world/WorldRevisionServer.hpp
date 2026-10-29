#ifndef WORLD_REVISION_SERVER_HPP
# define WORLD_REVISION_SERVER_HPP

# include "../../Libft/Modules/Game/game_server.hpp"
# include "../../src/world/World.hpp"
# include "../cards/RegenerationCardSession.hpp"
# include "../ft_vox.hpp"
# include <cstddef>
# include <cstdint>
# include <map>
# include <string>
# include <tuple>
# include <vector>

enum { WORLD_REVISION_OCCUPANCY_MAX_AGE_MS = 1000U };

typedef int32_t (*WorldRevisionServerAuthorizer)(int32_t client_handle,
	const char *message, uint64_t *actor_id_out, uint64_t *loom_id_out,
	void *user_data);
typedef int32_t (*WorldRevisionServerInstabilityViewAuthorizer)(
	uint64_t actor_id, uint64_t loom_id, int32_t chunk_x, int32_t chunk_z,
	void *user_data);
typedef int32_t (*WorldRevisionServerStationAccessAuthorizer)(
	uint64_t actor_id, uint64_t loom_id, int32_t station_chunk_x,
	int32_t station_chunk_z, bool has_station_block, int32_t station_block_x,
	int32_t station_block_y, int32_t station_block_z,
	uint32_t station_block_id, const char *request_message, void *user_data);
struct WorldRevisionOccupancySample
{
	/* One entry per active player. Keep duplicate chunk coordinates when
	 * players share a chunk; the revision manager performs spatial dedupe. */
	std::vector<World::RevisionChunkCoordinate> player_chunks;
	/* Stable authenticated identities parallel to the active-player list. */
	std::vector<uint64_t> player_actor_ids;
	uint32_t active_player_count;
	/* Must use the same restart-stable epoch as the GameSession clock. */
	uint64_t sampled_epoch_ms;
};
typedef int32_t (*WorldRevisionServerOccupancyProvider)(
	WorldRevisionOccupancySample &sample_out, void *user_data);
typedef int32_t (*WorldRevisionServerEpochProvider)(uint64_t *epoch_ms_out,
	void *user_data);
typedef int32_t (*WorldRevisionServerResponseSender)(int32_t client_handle,
	const char *message, void *user_data);
typedef int32_t (*WorldRevisionServerStartCommitter)(uint64_t actor_id,
	uint64_t loom_id, uint64_t request_id,
	uint64_t expected_selection_revision,
	uint64_t expected_policy_revision, uint32_t opening_card_count,
	void *user_data);
struct WorldRevisionStartPolicyStatus
{
	uint32_t mode; /* 0 legacy/unknown, 1 solo, 2 multiplayer */
	uint32_t active_player_count;
	uint64_t decision_timeout_ms;
	uint32_t currency_item_id;
	uint64_t committed_currency_amount;
	uint32_t selected_paid_chunks;
	uint32_t selected_bonus_chunks;
	uint32_t settlement_policy; /* 0 legacy/unknown, 1 retain after commit */
	uint32_t revision_identifier;
	uint32_t card_content_version;
	uint64_t generation_manifest_hash;
	uint64_t trusted_script_manifest_hash;
	std::vector<uint64_t> participant_actor_ids;
};
enum
{
	WORLD_REVISION_SETTLEMENT_UNKNOWN = 0U,
	WORLD_REVISION_SETTLEMENT_RETAIN_COMMITTED_COST = 1U
};
typedef int32_t (*WorldRevisionServerStartPolicyQuery)(
	uint64_t actor_id, uint64_t loom_id,
	WorldRevisionStartPolicyStatus &status_out, void *user_data);
typedef int32_t (*WorldRevisionServerCheckpointCommitter)(
	const uint8_t *checkpoint_bytes, std::size_t checkpoint_byte_count,
	void *user_data);
typedef int32_t (*WorldRevisionServerCardPlayHandler)(uint64_t actor_id,
	uint64_t loom_id, uint64_t request_id, uint32_t card_instance_id,
	const RegenerationCardChoice &choice, RegenerationPolicyIntent *intent_out,
	void *user_data);
typedef int32_t (*WorldRevisionServerCardDiscardHandler)(uint64_t actor_id,
	uint64_t loom_id, uint64_t request_id, uint64_t source_draw_request_id,
	uint32_t card_instance_id, RegenerationCardActionResult *result_out,
	void *user_data);
typedef int32_t (*WorldRevisionServerCardDrawHandler)(uint64_t actor_id,
	uint64_t loom_id, uint64_t request_id,
	uint64_t source_host_intent_sequence, uint32_t draw_count,
	uint32_t required_discard_count,
	RegenerationCardActionResult *result_out, void *user_data);
typedef int32_t (*WorldRevisionServerCardExpiryHandler)(uint64_t actor_id,
	uint64_t loom_id, uint64_t request_id, uint32_t *resolved_count_out,
	bool *replayed_out, void *user_data);
typedef int32_t (*WorldRevisionServerCardHandQuery)(uint64_t actor_id,
	uint64_t loom_id, RegenerationCardHandView &view_out, void *user_data);
typedef int32_t (*WorldRevisionServerCardChoiceHandler)(uint64_t actor_id,
	uint64_t loom_id, uint64_t request_id, uint32_t choice_id,
	uint32_t option_id, RegenerationCardActionResult *result_out,
	void *user_data);

class WorldRevisionServer
{
	friend class WorldRevisionValidator;
  public:
	WorldRevisionServer(World &world);
	WorldRevisionServer(const WorldRevisionServer &other);
	~WorldRevisionServer();
	WorldRevisionServer &operator=(const WorldRevisionServer &other);

	/* The wire handler accepts a JSON `world_revision` object with
	 * `protocol_version: 1`; messages are capped at 4096 bytes. Every response
	 * envelope includes protocol_version 1. The authenticated read-only `status`
	 * action includes logical lifecycle state and permitted actions, the
	 * persisted start policy and participant actor IDs when available, and each
	 * selected chunk's state and
	 * publication status plus
	 * completed/total jobs, regenerated/skipped chunk counts, and the current
	 * regeneration error code. The authenticated `preview` query accepts a
	 * center chunk and radius up to eight, refreshes occupancy, and reports the
	 * state and protection reasons for every chunk in the bounded circle.
	 * Draft actions include `protect` and `unprotect`, both compare-and-set
	 * against the current selection revision. After start, owner-bound
	 * `card_hand`, `play_card`, `choose_card_option`, `draw_cards`,
	 * `discard_card`, and `resolve_expired_choices`
	 * requests run after start and bind to the draft owner. Expiry uses the
	 * GameSession host clock and accepts only due choices. The server caches
	 * exact results in its checkpoint and omits
	 * private card-result fields from responses. Unsupported versions fail
	 * closed. */
	int32_t set_authorizer(WorldRevisionServerAuthorizer authorizer,
		void *user_data) noexcept;
	/* Instability views fail closed until the host applies its exploration and
	 * per-chunk visibility policy for the authorized actor and Loom. */
	int32_t set_instability_view_authorizer(
		WorldRevisionServerInstabilityViewAuthorizer authorizer,
		void *user_data) noexcept;
	/* Host checks reach, claims and permissions for quotes and all draft
	 * actions. Exact block identity is rechecked by the server when configured.
	 * Required; fails closed. */
	int32_t set_station_access_authorizer(
		WorldRevisionServerStationAccessAuthorizer authorizer,
		void *user_data) noexcept;
	/* Multiplayer selection fails closed until a complete, timestamped sample
	 * with unique nonzero actor IDs is supplied before selection, quote, and
	 * start actions. Through the
	 * GameSession binding, samples are checked against its authoritative host
	 * clock; direct hosts must install the matching epoch provider as well. */
	int32_t set_occupancy_provider(
		WorldRevisionServerOccupancyProvider provider,
		void *user_data) noexcept;
	/* Required authoritative time source in the sample's epoch domain. */
	int32_t set_epoch_provider(WorldRevisionServerEpochProvider provider,
		void *user_data) noexcept;
	/* Required before initialization. Confirm must pass through the host that
	 * coordinates resources, cards and world revisions. */
	int32_t set_start_committer(
		WorldRevisionServerStartCommitter committer,
		void *user_data) noexcept;
	/* Optional actor/Loom-scoped durable policy source for owner-only status. */
	int32_t set_start_policy_query(
		WorldRevisionServerStartPolicyQuery query,
		void *user_data) noexcept;
	/* Required before initialization. Persists request/draft state through the
	 * host before replying. */
	int32_t set_checkpoint_committer(
		WorldRevisionServerCheckpointCommitter committer,
		void *user_data) noexcept;
	/* Routes an owner-authenticated CardGame play through GameSession. Wire
	 * results redact private_result and private_ordinal. */
	int32_t set_card_play_handler(WorldRevisionServerCardPlayHandler handler,
		void *user_data) noexcept;
	int32_t set_card_discard_handler(
		WorldRevisionServerCardDiscardHandler handler,
		void *user_data) noexcept;
	int32_t set_card_draw_handler(WorldRevisionServerCardDrawHandler handler,
		void *user_data) noexcept;
	int32_t set_card_expiry_handler(
		WorldRevisionServerCardExpiryHandler handler, void *user_data) noexcept;
	int32_t set_card_hand_query(WorldRevisionServerCardHandQuery query,
		void *user_data) noexcept;
	int32_t set_card_choice_handler(
		WorldRevisionServerCardChoiceHandler handler,
		void *user_data) noexcept;
	/* Owner-bound read-only quote. Reusing an ID with changed revisions/count is
	 * rejected; exact retries return the original quote or error. */
	int32_t query_start_cost(uint64_t actor_id, uint64_t loom_id,
		uint64_t request_id, uint64_t expected_selection_revision,
		uint64_t expected_policy_revision, uint32_t opening_card_count,
		WorldRevisionCostQuote *quote_out) noexcept;
	int32_t initialize();
	/* Host-owned checkpoints. Restore both before initialize(); the host must
	 * durably save them alongside the world mutation they describe. */
	int32_t serialize_draft_state(std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t restore_draft_state(const uint8_t *bytes,
		std::size_t byte_count) noexcept;
	int32_t serialize_request_cache(std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t restore_request_cache(const uint8_t *bytes,
		std::size_t byte_count) noexcept;
	/* Combined host checkpoint. Restore the matching World revision first,
	 * then restore this bundle before initialize(). */
	int32_t serialize_checkpoint(std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t restore_checkpoint(const uint8_t *bytes,
		std::size_t byte_count) noexcept;
	int32_t save_checkpoint_file(const char *file_path) const noexcept;
	int32_t load_checkpoint_file(const char *file_path) noexcept;
	int32_t start(const char *ip, uint16_t port);
	void run_once();
	int32_t destroy();

  private:
	struct CachedRequest
	{
		std::string payload;
		int32_t result;
		bool completed;
		bool has_quote;
		WorldRevisionCostQuote quote;
		bool has_instability_view;
		WorldChunkInstabilityPublicView instability_view;
		bool has_revision_state;
		uint32_t revision_identifier;
		uint64_t selection_revision;
		uint64_t cost_policy_revision;
		uint32_t selected_count;
		bool has_card_play_result;
		RegenerationPolicyIntent card_play_result;
		bool has_card_discard_result;
		RegenerationCardActionResult card_discard_result;
		bool has_card_draw_result;
		RegenerationCardActionResult card_draw_result;
		bool has_card_expiry_result;
		uint32_t card_expiry_resolved_count;
		bool card_expiry_replayed;
		bool has_card_choice_result;
		RegenerationCardActionResult card_choice_result;
	};
	static const std::size_t REQUEST_CACHE_LIMIT = 4096U;
	World *world_;
	game_server server_;
	bool initialized_;
	WorldRevisionServerAuthorizer authorizer_;
	void *authorizer_user_data_;
	WorldRevisionServerInstabilityViewAuthorizer instability_view_authorizer_;
	void *instability_view_authorizer_user_data_;
	WorldRevisionServerStationAccessAuthorizer station_access_authorizer_;
	void *station_access_authorizer_user_data_;
	WorldRevisionServerOccupancyProvider occupancy_provider_;
	void *occupancy_provider_user_data_;
	WorldRevisionServerEpochProvider epoch_provider_;
	void *epoch_provider_user_data_;
	WorldRevisionServerStartCommitter start_committer_;
	void *start_committer_user_data_;
	WorldRevisionServerStartPolicyQuery start_policy_query_;
	void *start_policy_query_user_data_;
	WorldRevisionServerCheckpointCommitter checkpoint_committer_;
	void *checkpoint_committer_user_data_;
	WorldRevisionServerCardPlayHandler card_play_handler_;
	void *card_play_handler_user_data_;
	WorldRevisionServerCardDiscardHandler card_discard_handler_;
	void *card_discard_handler_user_data_;
	WorldRevisionServerCardDrawHandler card_draw_handler_;
	void *card_draw_handler_user_data_;
	WorldRevisionServerCardExpiryHandler card_expiry_handler_;
	void *card_expiry_handler_user_data_;
	WorldRevisionServerCardHandQuery card_hand_query_;
	void *card_hand_query_user_data_;
	WorldRevisionServerCardChoiceHandler card_choice_handler_;
	void *card_choice_handler_user_data_;
	WorldRevisionServerResponseSender response_sender_;
	void *response_sender_user_data_;
	uint64_t draft_actor_id_;
	uint64_t draft_loom_id_;
	bool draft_owned_;
	bool draft_start_committed_;
	std::map<std::tuple<uint64_t, uint64_t, uint64_t>, CachedRequest>
		request_cache_;
	std::map<std::tuple<uint64_t, uint64_t, uint64_t>, std::string>
		wire_request_cache_;

	static int32_t handle_message(int32_t client_handle, const char *message,
		void *user_data);
	int32_t handle_revision_message(const char *message, uint64_t actor_id,
		uint64_t loom_id);
	int32_t handle_quote_message(int32_t client_handle, const char *message,
		json_group *revision, uint64_t actor_id, uint64_t loom_id);
	int32_t handle_status_message(int32_t client_handle, json_group *revision,
		uint64_t actor_id, uint64_t loom_id);
	int32_t handle_preview_message(int32_t client_handle, const char *message,
		json_group *revision, uint64_t actor_id, uint64_t loom_id);
	int32_t handle_instability_view_message(int32_t client_handle,
		const char *message, json_group *revision, uint64_t actor_id,
		uint64_t loom_id);
	int32_t handle_card_hand_message(int32_t client_handle, const char *message,
		json_group *revision, uint64_t actor_id, uint64_t loom_id);
	int32_t send_action_response(int32_t client_handle, const char *message,
		uint64_t actor_id, uint64_t loom_id, uint64_t request_id,
		int32_t result);
	int32_t handle_begin_action(json_group *revision);
	int32_t authorize_station_access(uint64_t actor_id, uint64_t loom_id,
		const char *message);
	int32_t refresh_occupied_chunks();
	int32_t commit_checkpoint() noexcept;
	int32_t apply_biome_config_overrides(json_group *revision,
		voxel_generation_config &revision_config);
	int32_t apply_biome_size_range_override(json_group *revision,
		voxel_generation_config &revision_config);
	int32_t apply_biome_specific_override(json_group *revision,
		voxel_generation_config &revision_config);
	int32_t handle_select_or_protect_action(json_group *revision,
		const char *action);
};

#endif
