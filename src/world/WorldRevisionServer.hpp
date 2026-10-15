#ifndef WORLD_REVISION_SERVER_HPP
# define WORLD_REVISION_SERVER_HPP

# include "../../Libft/Modules/Game/game_server.hpp"
# include "../../src/world/World.hpp"
# include "../ft_vox.hpp"
# include <cstddef>
# include <cstdint>
# include <map>
# include <string>
# include <tuple>
# include <vector>

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
typedef int32_t (*WorldRevisionServerOccupancyProvider)(
	std::vector<World::RevisionChunkCoordinate> &occupied_chunks_out,
	void *user_data);
typedef int32_t (*WorldRevisionServerResponseSender)(int32_t client_handle,
	const char *message, void *user_data);
typedef int32_t (*WorldRevisionServerStartCommitter)(uint64_t actor_id,
	uint64_t loom_id, uint64_t request_id,
	uint64_t expected_selection_revision,
	uint64_t expected_policy_revision, uint32_t opening_card_count,
	void *user_data);
typedef int32_t (*WorldRevisionServerCheckpointCommitter)(
	const uint8_t *checkpoint_bytes, std::size_t checkpoint_byte_count,
	void *user_data);

class WorldRevisionServer
{
	friend class WorldRevisionValidator;
  public:
	WorldRevisionServer(World &world);
	WorldRevisionServer(const WorldRevisionServer &other);
	~WorldRevisionServer();
	WorldRevisionServer &operator=(const WorldRevisionServer &other);

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
	/* Multiplayer selection fails closed until all active player chunks are
	 * supplied and refreshed before selection, quote, and start actions. */
	int32_t set_occupancy_provider(
		WorldRevisionServerOccupancyProvider provider,
		void *user_data) noexcept;
	/* Confirm must pass through the host that coordinates resources, cards and
	 * world revisions. The world network adapter never starts generation alone. */
	int32_t set_start_committer(
		WorldRevisionServerStartCommitter committer,
		void *user_data) noexcept;
	/* Persists completed request/draft state through the host before replying. */
	int32_t set_checkpoint_committer(
		WorldRevisionServerCheckpointCommitter committer,
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
	WorldRevisionServerStartCommitter start_committer_;
	void *start_committer_user_data_;
	WorldRevisionServerCheckpointCommitter checkpoint_committer_;
	void *checkpoint_committer_user_data_;
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
	int32_t handle_instability_view_message(int32_t client_handle,
		const char *message, json_group *revision, uint64_t actor_id,
		uint64_t loom_id);
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
