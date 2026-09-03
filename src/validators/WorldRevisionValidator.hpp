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
		int32_t	select_unloaded_result;
		int32_t	select_far_result;
		int32_t	preview_result;
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
	static int32_t regenerate_and_check(World &world, int32_t *regenerated,
		int32_t *skipped) noexcept;
	static int32_t roundtrip_metadata(World &world) noexcept;
	static int32_t apply_request_test(World &world) noexcept;
	static bool fail_if_error(World &world, int32_t error_code) noexcept;

  public:
	WorldRevisionValidator();
	WorldRevisionValidator(const WorldRevisionValidator &other);
	~WorldRevisionValidator();
	WorldRevisionValidator &operator=(const WorldRevisionValidator &other);

	int validate() const override;
};

#endif
