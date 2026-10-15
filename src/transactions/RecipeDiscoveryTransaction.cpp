#include "RecipeDiscoveryTransaction.hpp"

int32_t RecipeDiscoveryTransaction::execute(
	MinecraftRecipeCatalog &recipe_catalog, uint32_t source_type_id,
	uint32_t source_id, uint64_t source_event_id,
	const GameSessionSnapshotWriter &snapshot_writer,
	ft_bool *newly_unlocked) noexcept
{
	std::vector<uint8_t> recipe_book_before;
	int32_t result;

	if (newly_unlocked == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*newly_unlocked = FT_FALSE;
	result = recipe_catalog.serialize_recipe_book(recipe_book_before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = recipe_catalog.grant_recipe_from_discovery(source_type_id,
		source_id, source_event_id, newly_unlocked);
	if (result != FT_ERR_SUCCESS || *newly_unlocked != FT_TRUE)
		return (result);
	result = snapshot_writer.save();
	if (result == FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	const int32_t restore_result = recipe_catalog.restore_recipe_book(
		recipe_book_before.data(), recipe_book_before.size());
	if (restore_result != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	*newly_unlocked = FT_FALSE;
	return (result);
}
