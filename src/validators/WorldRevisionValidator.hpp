#ifndef WORLD_REVISION_VALIDATOR_HPP
# define WORLD_REVISION_VALIDATOR_HPP

# include "../../src/validators/IValidator.hpp"
# include "../../src/world/World.hpp"

class WorldRevisionValidator : public IValidator
{
  private:
	struct		SelectionResults
	{
		int32_t	begin_result;
		int32_t	edit_select_result;
		int32_t	protect_result;
		int32_t	select_loaded_result;
		int32_t	bonus_unselect_result;
		int32_t	bonus_select_result;
		int32_t	select_unloaded_result;
		int32_t	select_far_result;
		int32_t	preview_result;
		int32_t	cost_policy_result;
		int32_t	stale_cost_policy_result;
		int32_t	cost_policy_update_result;
		int32_t	cost_quote_result;
		int32_t	stale_cost_quote_result;
		uint64_t	cost_policy_revision;
		WorldRevisionCostQuote cost_quote;
		World::ChunkRevisionState transition_state;
		World::ChunkRevisionState protected_state;
	};

	static int32_t initialize_world_with_edit(World &world) noexcept;
	static int32_t wait_for_loaded_chunk(World &world, int32_t chunk_x,
		int32_t chunk_z) noexcept;
	static int32_t quiesce_stream_pipeline(World &world) noexcept;
	static SelectionResults apply_selection_actions(World &world,
		std::vector<World::RevisionPreviewEntry> &preview) noexcept;
	static int32_t check_selection_results(const SelectionResults &results,
		const std::vector<World::RevisionPreviewEntry> &preview) noexcept;
	static int32_t setup_revision_selection(World &world,
		std::vector<World::RevisionPreviewEntry> &preview) noexcept;
	static int32_t validate_start_cost_api(World &world) noexcept;
	static int32_t regenerate_and_check(World &world, int32_t *regenerated,
		int32_t *skipped) noexcept;
	static int32_t roundtrip_metadata(World &world) noexcept;
	static int32_t apply_request_test(World &world) noexcept;
	static bool fail_if_error(World &world, int32_t error_code) noexcept;
	static int32_t test_atomic_generation_batch_capacity() noexcept;
	static int32_t test_magma_feature_generation() noexcept;
	static int32_t test_village_feature_generation() noexcept;
	static int32_t test_ruined_village_feature_generation() noexcept;
	static int32_t test_shipwreck_feature_generation() noexcept;
	static int32_t test_volcano_feature_generation() noexcept;
	static int32_t test_volcano_configured_footprint_generation() noexcept;
	static int32_t test_server_draft_authorization(World &world) noexcept;
	static int32_t test_game_session_committed_start_retry() noexcept;
	static int32_t test_game_session_volatile_stabilizer() noexcept;

  public:
	WorldRevisionValidator();
	WorldRevisionValidator(const WorldRevisionValidator &other);
	~WorldRevisionValidator();
	WorldRevisionValidator &operator=(const WorldRevisionValidator &other);

	int validate() const override;
};

#endif
