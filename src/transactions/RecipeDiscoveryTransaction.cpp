#include "RecipeDiscoveryTransaction.hpp"

int32_t RecipeDiscoveryTransaction::execute(
	MinecraftRecipeCatalog &recipe_catalog, uint32_t source_type_id,
	uint32_t source_id, uint64_t source_event_id,
	const GameSessionSnapshotWriter &snapshot_writer,
	game_recipe_unlock_result *result_out) noexcept
{
	std::vector<uint8_t> recipe_book_before;
	int32_t result;

	if (result_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	result_out->event_newly_recorded = FT_FALSE;
	result_out->recipe_newly_known = FT_FALSE;
	result = recipe_catalog.serialize_recipe_book(recipe_book_before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = recipe_catalog.grant_recipe_from_discovery_with_result(
		source_type_id, source_id, source_event_id, result_out);
	if (result != FT_ERR_SUCCESS
		|| result_out->event_newly_recorded != FT_TRUE)
		return (result);
	result = snapshot_writer.save();
	if (result == FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	const int32_t restore_result = recipe_catalog.restore_recipe_book(
		recipe_book_before.data(), recipe_book_before.size());
	if (restore_result != FT_ERR_SUCCESS)
	{
		result_out->event_newly_recorded = FT_FALSE;
		result_out->recipe_newly_known = FT_FALSE;
		return (FT_ERR_INVALID_STATE);
	}
	result_out->event_newly_recorded = FT_FALSE;
	result_out->recipe_newly_known = FT_FALSE;
	return (result);
}

int32_t RecipeDiscoveryTransaction::execute(
	MinecraftRecipeCatalog &recipe_catalog, uint32_t source_type_id,
	uint32_t source_id, uint64_t source_event_id,
	const GameSessionSnapshotWriter &snapshot_writer,
	ft_bool *newly_unlocked) noexcept
{
	game_recipe_unlock_result unlock_result = {};
	int32_t result;

	if (newly_unlocked == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*newly_unlocked = FT_FALSE;
	result = RecipeDiscoveryTransaction::execute(recipe_catalog,
		source_type_id, source_id, source_event_id, snapshot_writer,
		&unlock_result);
	if (result == FT_ERR_SUCCESS)
		*newly_unlocked = unlock_result.recipe_newly_known;
	return (result);
}
