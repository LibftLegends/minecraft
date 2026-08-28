#include "../../src/validators/WorldRevisionValidator.hpp"

WorldRevisionValidator::WorldRevisionValidator()
{
}

WorldRevisionValidator::WorldRevisionValidator(const WorldRevisionValidator &other)
	: IValidator(other)
{
	(void)other;
}

WorldRevisionValidator::~WorldRevisionValidator()
{
}

WorldRevisionValidator &WorldRevisionValidator::operator=(const WorldRevisionValidator &other)
{
	(void)other;
	return (*this);
}

int32_t WorldRevisionValidator::initialize_world_with_edit(World &world) noexcept
{
	int32_t error_code;

	error_code = world.initialize("revision-validator");
	if (error_code != FT_ERR_SUCCESS)
		return (1);
	if (world.find_chunk_mutable(0, 0) == nullptr)
	{
		world.destroy();
		return (1);
	}
	if (world.find_chunk_mutable(0, 0)->chunk.write_block(0, 0, 0,
			TERRAIN_GENERATOR_STONE_BLOCK) != FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	return (FT_ERR_SUCCESS);
}

WorldRevisionValidator::SelectionResults WorldRevisionValidator::apply_selection_actions(World &world,
	std::vector<World::RevisionPreviewEntry> &preview) noexcept
{
	terrain_generation_config config;
	SelectionResults results;

	terrain_default_generation_config(config);
	results.begin_result = world.begin_world_revision(config,
			World::REGEN_TERRAIN_RESHAPING);
	results.edit_select_result = world.select_revision_chunk(0, 0, true);
	results.protect_result = world.set_chunk_protected(4, 0, true);
	results.select_loaded_result = world.select_revision_chunk(1, 0, true);
	results.select_unloaded_result = world.select_revision_chunk(1000, 1000,
			true);
	results.select_far_result = world.select_revision_chunk(1000, 1000, true);
	results.transition_state = world.revision_state(2, 0);
	results.protected_state = world.revision_state(4, 0);
	results.preview_result = world.build_revision_preview(2, 0, 4, preview);
	return (results);
}

int32_t WorldRevisionValidator::check_selection_results(const SelectionResults &results,
	const std::vector<World::RevisionPreviewEntry> &preview) noexcept
{
	if (results.begin_result != FT_ERR_SUCCESS
		|| results.edit_select_result == FT_ERR_SUCCESS
		|| results.protect_result != FT_ERR_SUCCESS
		|| results.select_loaded_result != FT_ERR_SUCCESS
		|| results.select_unloaded_result != FT_ERR_SUCCESS
		|| results.select_far_result != FT_ERR_SUCCESS
		|| results.preview_result != FT_ERR_SUCCESS || preview.empty()
		|| results.transition_state != World::REVISION_TRANSITION
		|| results.protected_state != World::REVISION_PROTECTED)
	{
		std::fprintf(stderr,
						"world-revision: setup failed begin=%d edit=%d protect=%d loaded=%d"
						" unloaded=%d preview=%d transition=%d protected=%d\n",
						results.begin_result,
						results.edit_select_result,
						results.protect_result,
						results.select_loaded_result,
						results.select_unloaded_result,
						results.preview_result,
						results.transition_state,
						results.protected_state);
		return (1);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionValidator::setup_revision_selection(World &world,
	std::vector<World::RevisionPreviewEntry> &preview) noexcept
{
	SelectionResults results;

	results = WorldRevisionValidator::apply_selection_actions(world, preview);
	return (WorldRevisionValidator::check_selection_results(results, preview));
}

int32_t WorldRevisionValidator::regenerate_and_check(World &world,
	int32_t *regenerated, int32_t *skipped) noexcept
{
	int32_t regenerate_result;

	regenerate_result = world.regenerate_selected_chunks(regenerated, skipped);
	if (regenerate_result != FT_ERR_SUCCESS || *regenerated < 1 || *skipped < 1
		|| world.world_revision().pending)
	{
		std::fprintf(stderr,
						"world-revision: regeneration failed result=%d regenerated=%d"
						" skipped=%d pending=%d\n",
						regenerate_result,
						*regenerated,
						*skipped,
						world.world_revision().pending ? 1 : 0);
		return (1);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionValidator::roundtrip_metadata(World &world) noexcept
{
	const char *metadata_path;
	World restored_world;

	metadata_path = "world_revision_validator.bin";
	if (world.save_revision_metadata(metadata_path) != FT_ERR_SUCCESS)
		return (1);
	if (restored_world.initialize("revision-validator") != FT_ERR_SUCCESS
		|| restored_world.load_revision_metadata(metadata_path) != FT_ERR_SUCCESS
		|| !restored_world.is_chunk_protected(4, 0))
	{
		std::remove(metadata_path);
		restored_world.destroy();
		return (1);
	}
	std::remove(metadata_path);
	restored_world.destroy();
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionValidator::apply_request_test(World &world) noexcept
{
	World::RevisionRequest request;
	World::RevisionRequestResult request_result;

	terrain_default_generation_config(request.config);
	request.mode = World::REGEN_DECORATION_REFRESH;
	request.stage_mask = TERRAIN_STAGE_DECORATION;
	request.selected_chunks.push_back({1, 0});
	if (world.apply_revision_request(request, &request_result) != FT_ERR_SUCCESS
		|| request_result.regenerated_count < 1
		|| request_result.stage_mask != TERRAIN_STAGE_DECORATION)
		return (1);
	return (FT_ERR_SUCCESS);
}

bool WorldRevisionValidator::fail_if_error(World &world,
	int32_t error_code) noexcept
{
	if (error_code == FT_ERR_SUCCESS)
		return (false);
	world.destroy();
	return (true);
}

int WorldRevisionValidator::validate() const
{
	World world;
	std::vector<World::RevisionPreviewEntry> preview;
	int32_t regenerated;
	int32_t skipped;

	if (WorldRevisionValidator::initialize_world_with_edit(world) != FT_ERR_SUCCESS)
		return (1);
	if (WorldRevisionValidator::fail_if_error(world,
			WorldRevisionValidator::setup_revision_selection(world, preview)))
		return (1);
	if (WorldRevisionValidator::fail_if_error(world,
			WorldRevisionValidator::regenerate_and_check(world, &regenerated,
				&skipped)))
		return (1);
	if (WorldRevisionValidator::fail_if_error(world,
			WorldRevisionValidator::roundtrip_metadata(world)))
		return (1);
	if (WorldRevisionValidator::fail_if_error(world,
			WorldRevisionValidator::apply_request_test(world)))
		return (1);
	std::printf("world-revision: ok regenerated=%d skipped=%d\n", regenerated,
		skipped);
	world.destroy();
	return (0);
}
