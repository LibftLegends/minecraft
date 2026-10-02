#include "../../src/validators/WorldRevisionValidator.hpp"

#include <chrono>
#include <memory>
#include <new>
#include <thread>

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
	if (WorldRevisionValidator::wait_for_loaded_chunk(world, 1, 0)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (WorldRevisionValidator::quiesce_stream_pipeline(world)
		!= FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	if (world.find_chunk_mutable(0, 0)->chunk.write_block(0, 0, 0,
			VOXEL_GENERATOR_STONE_BLOCK) != FT_ERR_SUCCESS)
	{
		world.destroy();
		return (1);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionValidator::quiesce_stream_pipeline(World &world) noexcept
{
	std::unique_ptr<WorldGenerationPipeline::Result> discarded_result;
	int32_t iteration;

	world.chunk_streamer.cancel_pending_remeshes();
	iteration = 0;
	while (iteration < 500)
	{
		while (world.chunk_streamer.pipeline().poll(discarded_result)
			== FT_ERR_SUCCESS)
			discarded_result.reset();
		if (world.chunk_streamer.pipeline().queued_count() == 0U
			&& world.chunk_streamer.pipeline().completed_count() == 0U
			&& world.chunk_streamer.pipeline().active_count() == 0U)
			return (FT_ERR_SUCCESS);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		iteration += 1;
	}
	return (world.chunk_streamer.pipeline().queued_count() == 0U
		&& world.chunk_streamer.pipeline().completed_count() == 0U
		&& world.chunk_streamer.pipeline().active_count() == 0U
		? FT_ERR_SUCCESS : FT_ERR_TIMEOUT);
}

int32_t WorldRevisionValidator::wait_for_loaded_chunk(World &world,
	int32_t chunk_x, int32_t chunk_z) noexcept
{
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(10);
	int32_t error_code;

	while (std::chrono::steady_clock::now() < deadline
		&& world.find_chunk_mutable(chunk_x, chunk_z) == nullptr)
	{
		error_code = world.update_around(0.0, 0.0, 4);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (world.find_chunk_mutable(chunk_x, chunk_z) != nullptr)
		return (FT_ERR_SUCCESS);
	std::fprintf(stderr,
		"world-revision: timed out waiting for chunk=(%d,%d) queued=%zu "
		"active=%zu completed=%zu loaded=%d\n", chunk_x, chunk_z,
		world.chunk_streamer.pipeline().queued_count(),
		world.chunk_streamer.pipeline().active_count(),
		world.chunk_streamer.pipeline().completed_count(),
		world.loaded_chunk_count);
	return (FT_ERR_TIMEOUT);
}

WorldRevisionValidator::SelectionResults WorldRevisionValidator::apply_selection_actions(World &world,
	std::vector<World::RevisionPreviewEntry> &preview) noexcept
{
	voxel_generation_config config;
	SelectionResults results;

	voxel_default_generation_config(config);
	results.begin_result = world.begin_world_revision(config,
			World::REGEN_VOXEL_RESHAPING);
	results.edit_select_result = world.select_revision_chunk(0, 0, true);
	results.protect_result = world.set_chunk_protected(4, 0, true);
	results.select_loaded_result = world.select_revision_chunk(1, 0, true);
	results.select_unloaded_result = world.select_revision_chunk(1000, 1000,
			true);
	results.select_far_result = world.select_revision_chunk(1, 0, true);
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
		|| results.select_unloaded_result == FT_ERR_SUCCESS
		|| results.select_far_result != FT_ERR_SUCCESS
		|| results.preview_result != FT_ERR_SUCCESS || preview.empty()
		|| results.transition_state != World::REVISION_TRANSITION
		|| results.protected_state != World::REVISION_PROTECTED)
	{
		std::fprintf(stderr,
						"world-revision: setup failed begin=%d edit=%d protect=%d loaded=%d"
						" unloaded=%d repeat=%d preview=%d transition=%d protected=%d\n",
						results.begin_result,
						results.edit_select_result,
						results.protect_result,
						results.select_loaded_result,
						results.select_unloaded_result,
						results.select_far_result,
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
	if (WorldRevisionValidator::check_selection_results(results, preview)
		!= FT_ERR_SUCCESS)
		return (1);
	return (WorldRevisionValidator::validate_start_cost_api(world));
}

int32_t WorldRevisionValidator::validate_start_cost_api(World &world) noexcept
{
	WorldRevisionCostPolicy policy;
	WorldRevisionCostQuote quote;
	const uint64_t expected_costs[4] = {8U, 13U, 23U, 43U};
	uint32_t card_count;
	uint64_t original_selection_revision;
	uint64_t current_selection_revision;

	policy.maximum_selected_chunks = 4U;
	policy.currency_item_id = 9001U;
	policy.currency_units_per_chunk = 3U;
	policy.four_card_opening_cost = 5U;
	policy.maximum_total_currency_cost = 100U;
	card_count = 4U;
	while (card_count <= 7U)
	{
		if (world.query_revision_start_cost(card_count, policy, &quote)
			!= FT_ERR_SUCCESS
			|| quote.selected_paid_chunks != 1U
			|| quote.opening_card_count != card_count
			|| quote.chunk_currency_cost != 3U
			|| quote.currency_item_id != policy.currency_item_id
			|| quote.opening_card_currency_cost != expected_costs[card_count - 4U]
				- 3U
			|| quote.total_currency_cost != expected_costs[card_count - 4U])
			return (1);
		card_count += 1U;
	}
	original_selection_revision = quote.selection_revision;
	if (world.query_revision_start_cost(original_selection_revision + 1U,
			4U, policy, &quote) != FT_ERR_INVALID_STATE
		|| quote.selection_revision != original_selection_revision)
		return (1);
	if (world.query_revision_start_cost(3U, policy, &quote)
		!= FT_ERR_INVALID_ARGUMENT
		|| world.query_revision_start_cost(8U, policy, &quote)
		!= FT_ERR_INVALID_ARGUMENT)
		return (1);
	policy.maximum_total_currency_cost = 10U;
	if (world.query_revision_start_cost(5U, policy, &quote)
		!= FT_ERR_OUT_OF_RANGE)
		return (1);
	policy.currency_units_per_chunk = 0U;
	policy.four_card_opening_cost = 0U;
	policy.maximum_total_currency_cost = 0U;
	if (world.query_revision_start_cost(4U, policy, &quote)
		!= FT_ERR_SUCCESS || quote.total_currency_cost != 0U)
		return (1);
	policy.currency_units_per_chunk = 3U;
	policy.four_card_opening_cost = 5U;
	policy.maximum_total_currency_cost = 100U;
	if (world.update_revision_chunk_selection(original_selection_revision,
			1, 0, false, &current_selection_revision) != FT_ERR_SUCCESS
		|| world.world_revision().selected_count != 0U
		|| world.query_revision_start_cost(4U, policy, &quote)
			!= FT_ERR_INVALID_OPERATION
		|| current_selection_revision == original_selection_revision)
		return (1);
	if (world.update_revision_chunk_selection(original_selection_revision,
			1, 0, true, &current_selection_revision) != FT_ERR_INVALID_STATE
		|| current_selection_revision != world.world_revision().selection_revision
		|| world.world_revision().selected_count != 0U)
		return (1);
	if (world.update_revision_chunk_selection(current_selection_revision,
			1, 0, true, &current_selection_revision) != FT_ERR_SUCCESS)
		return (1);
	if (world.query_revision_start_cost(4U, policy, &quote)
		!= FT_ERR_SUCCESS
		|| quote.selection_revision == original_selection_revision)
		return (1);
	if (world.query_revision_start_cost(quote.selection_revision, 4U,
			policy, &quote) != FT_ERR_SUCCESS)
		return (1);
	original_selection_revision = world.world_revision().selection_revision;
	if (world.update_revision_chunk_selection(original_selection_revision,
			1U, 2, 0, true, &current_selection_revision)
		!= FT_ERR_OUT_OF_RANGE
		|| current_selection_revision != original_selection_revision
		|| world.world_revision().selected_count != 1U)
		return (1);
	if (world.set_chunk_protected(1, 0, true) != FT_ERR_SUCCESS
		|| world.world_revision().selection_revision
		== original_selection_revision
		|| world.world_revision().selected_count != 0U
		|| world.query_revision_start_cost(original_selection_revision, 4U,
			policy, &quote) != FT_ERR_INVALID_STATE)
		return (1);
	if (world.set_chunk_protected(1, 0, false) != FT_ERR_SUCCESS)
		return (1);
	current_selection_revision = world.world_revision().selection_revision;
	if (world.update_revision_chunk_selection(current_selection_revision, 1U,
			1, 0, true, &current_selection_revision) != FT_ERR_SUCCESS
		|| world.update_revision_chunk_selection(current_selection_revision, 1U,
			2, 0, true, &current_selection_revision) != FT_ERR_OUT_OF_RANGE
		|| world.world_revision().selected_count != 1U)
		return (1);
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionValidator::regenerate_and_check(World &world,
	int32_t *regenerated, int32_t *skipped) noexcept
{
	int32_t regenerate_result;
	World::WorldRevision revision;

	revision = world.world_revision();
	std::fprintf(stderr,
					"world-revision: before-regeneration pending=%d selected=%zu"
					" state_1_0=%d state_4_0=%d chunk_1_0=%s\n",
					revision.pending ? 1 : 0,
					revision.selected_count,
					static_cast<int>(world.revision_state(1, 0)),
					static_cast<int>(world.revision_state(4, 0)),
					world.find_chunk_mutable(1, 0) == nullptr ? "missing" : "present");
	regenerate_result = world.regenerate_selected_chunks(regenerated, skipped);
	if (regenerate_result != FT_ERR_SUCCESS || *regenerated < 1 || *skipped < 0
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
	{
		WorldChunk *regenerated_chunk = world.find_chunk_mutable(1, 0);
		if (regenerated_chunk == nullptr
			|| regenerated_chunk->light_buffer_is_valid() == false
			|| regenerated_chunk->light_is_current() == false
			|| regenerated_chunk->light_ready_for_render == false)
		{
			std::fprintf(stderr,
				"world-revision: regenerated chunk lost valid light "
				"chunk=%s valid=%d current=%d ready=%d\n",
				regenerated_chunk == nullptr ? "missing" : "present",
				regenerated_chunk != nullptr
					&& regenerated_chunk->light_buffer_is_valid() ? 1 : 0,
				regenerated_chunk != nullptr
					&& regenerated_chunk->light_is_current() ? 1 : 0,
				regenerated_chunk != nullptr
					&& regenerated_chunk->light_ready_for_render ? 1 : 0);
			return (1);
		}
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionValidator::roundtrip_metadata(World &world) noexcept
{
	const char *metadata_path;
	std::unique_ptr<World> restored_world(new (std::nothrow) World());

	metadata_path = "world_revision_validator.bin";
	if (restored_world == nullptr
		|| world.save_revision_metadata(metadata_path) != FT_ERR_SUCCESS)
		return (1);
	if (restored_world->initialize("revision-validator") != FT_ERR_SUCCESS
		|| restored_world->load_revision_metadata(metadata_path) != FT_ERR_SUCCESS
		|| !restored_world->is_chunk_protected(4, 0))
	{
		std::remove(metadata_path);
		restored_world->destroy();
		return (1);
	}
	std::remove(metadata_path);
	restored_world->destroy();
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionValidator::apply_request_test(World &world) noexcept
{
	World::RevisionRequest request;
	World::RevisionRequestResult request_result;

	voxel_default_generation_config(request.config);
	request.mode = World::REGEN_DECORATION_REFRESH;
	request.stage_mask = VOXEL_STAGE_DECORATION;
	request.selected_chunks.push_back({1, 0});
	if (world.apply_revision_request(request, &request_result) != FT_ERR_SUCCESS
		|| request_result.regenerated_count < 1
		|| request_result.stage_mask != VOXEL_STAGE_DECORATION)
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
	std::unique_ptr<World> world(new (std::nothrow) World());
	std::vector<World::RevisionPreviewEntry> preview;
	int32_t regenerated;
	int32_t skipped;

	if (world == nullptr
		|| WorldRevisionValidator::initialize_world_with_edit(*world)
			!= FT_ERR_SUCCESS)
		return (1);
	if (WorldRevisionValidator::fail_if_error(*world,
			WorldRevisionValidator::setup_revision_selection(*world, preview)))
		return (1);
	if (WorldRevisionValidator::fail_if_error(*world,
			WorldRevisionValidator::regenerate_and_check(*world, &regenerated,
				&skipped)))
		return (1);
	if (WorldRevisionValidator::fail_if_error(*world,
			WorldRevisionValidator::roundtrip_metadata(*world)))
		return (1);
	if (WorldRevisionValidator::fail_if_error(*world,
			WorldRevisionValidator::apply_request_test(*world)))
		return (1);
	std::printf("world-revision: ok regenerated=%d skipped=%d\n", regenerated,
		skipped);
	world->destroy();
	return (0);
}
