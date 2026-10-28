#include "WorldItemCraftTransaction.hpp"

#include "../crafting/MinecraftRecipeCatalog.hpp"
#include "../entities/WorldItemStore.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <limits>
#include <new>
#include <vector>

namespace
{
	static const uint64_t WORLD_ITEM_CRAFT_SCOPE = 0x574F524C44435246ULL;
	static const uint32_t WORLD_ITEM_CRAFT_TYPE = 2U;
	static const uint32_t WORLD_ITEM_CRAFT_VERSION = 6U;
	static const std::size_t WORLD_ITEM_CRAFT_V1_PAYLOAD_SIZE = 32U;
	static const std::size_t WORLD_ITEM_CRAFT_V2_PAYLOAD_SIZE = 56U;
	static const std::size_t WORLD_ITEM_CRAFT_V3_PAYLOAD_SIZE = 72U;
	static const std::size_t WORLD_ITEM_CRAFT_V4_PAYLOAD_SIZE = 80U;
	static const std::size_t WORLD_ITEM_CRAFT_V5_PAYLOAD_SIZE = 88U;
	static const std::size_t WORLD_ITEM_CRAFT_PAYLOAD_SIZE = 104U;

	static void append_u32(std::vector<uint8_t> &bytes, uint32_t value)
	{
		for (uint32_t shift = 0U; shift < 32U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	static void append_u64(std::vector<uint8_t> &bytes, uint64_t value)
	{
		for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	static bool read_u32(const std::vector<uint8_t> &bytes,
		std::size_t *offset, uint32_t *value) noexcept
	{
		if (offset == nullptr || value == nullptr || *offset > bytes.size()
			|| bytes.size() - *offset < 4U)
			return (false);
		*value = static_cast<uint32_t>(bytes[*offset])
			| (static_cast<uint32_t>(bytes[*offset + 1U]) << 8U)
			| (static_cast<uint32_t>(bytes[*offset + 2U]) << 16U)
			| (static_cast<uint32_t>(bytes[*offset + 3U]) << 24U);
		*offset += 4U;
		return (true);
	}

	static bool read_u64(const std::vector<uint8_t> &bytes,
		std::size_t *offset, uint64_t *value) noexcept
	{
		uint32_t low;
		uint32_t high;

		if (!read_u32(bytes, offset, &low)
			|| !read_u32(bytes, offset, &high))
			return (false);
		*value = static_cast<uint64_t>(low)
			| (static_cast<uint64_t>(high) << 32U);
		return (true);
	}

	static int32_t decode(const GameTransactionJournal::Record &record,
		uint32_t *recipe_id, int32_t *batch_count,
		uint64_t *expected_revision, uint64_t *expected_catalog_revision,
		uint64_t *expected_recipe_book_revision,
		uint64_t *expected_character_state_revision,
		uint64_t *authenticated_actor_id, uint32_t *station_id,
		int32_t *station_x, int32_t *station_y, int32_t *station_z,
		uint64_t *expected_station_revision, uint64_t *expected_session_id,
		uint64_t *expected_session_revision) noexcept
	{
		std::size_t offset = 4U;
		uint32_t version;
		uint32_t raw_batch;
		uint32_t raw_x = 0U, raw_y = 0U, raw_z = 0U;
		uint64_t request_id;

		if (recipe_id == nullptr || batch_count == nullptr
			|| expected_revision == nullptr
			|| expected_catalog_revision == nullptr
			|| expected_recipe_book_revision == nullptr
			|| expected_character_state_revision == nullptr
			|| authenticated_actor_id == nullptr
			|| station_id == nullptr
			|| station_x == nullptr || station_y == nullptr || station_z == nullptr
			|| expected_station_revision == nullptr
			|| expected_session_id == nullptr
			|| expected_session_revision == nullptr
			|| record.scope_id != WORLD_ITEM_CRAFT_SCOPE
			|| record.transaction_type != WORLD_ITEM_CRAFT_TYPE
			|| (record.payload.size() != WORLD_ITEM_CRAFT_V1_PAYLOAD_SIZE
				&& record.payload.size() != WORLD_ITEM_CRAFT_V2_PAYLOAD_SIZE
				&& record.payload.size() != WORLD_ITEM_CRAFT_V3_PAYLOAD_SIZE
				&& record.payload.size() != WORLD_ITEM_CRAFT_V4_PAYLOAD_SIZE
				&& record.payload.size() != WORLD_ITEM_CRAFT_V5_PAYLOAD_SIZE
				&& record.payload.size() != WORLD_ITEM_CRAFT_PAYLOAD_SIZE)
			|| record.payload[0] != 'G' || record.payload[1] != 'T'
			|| record.payload[2] != 'C' || record.payload[3] != 'R'
			|| !read_u32(record.payload, &offset, &version)
			|| (version < 1U || version > WORLD_ITEM_CRAFT_VERSION)
			|| !read_u64(record.payload, &offset, &request_id)
			|| !read_u32(record.payload, &offset, recipe_id)
			|| !read_u32(record.payload, &offset, &raw_batch)
			|| !read_u64(record.payload, &offset, expected_revision)
			|| request_id != record.request_id || *recipe_id == 0U
			|| raw_batch == 0U || raw_batch > static_cast<uint32_t>(
				std::numeric_limits<int32_t>::max()))
			return (FT_ERR_INVALID_ARGUMENT);
		*station_id = 0U;
		*station_x = 0;
		*station_y = 0;
		*station_z = 0;
		*expected_station_revision = 0U;
		*expected_session_id = 0U;
		*expected_session_revision = 0U;
		*expected_catalog_revision = 0U;
		*expected_recipe_book_revision = 0U;
		*expected_character_state_revision = 0U;
		*authenticated_actor_id = 0U;
		if (version >= 3U
			&& (!read_u64(record.payload, &offset, expected_catalog_revision)
				|| !read_u64(record.payload, &offset,
					expected_recipe_book_revision)
				|| *expected_catalog_revision == 0U
				|| *expected_recipe_book_revision == 0U))
			return (FT_ERR_INVALID_ARGUMENT);
		if (version >= 2U
			&& (!read_u32(record.payload, &offset, station_id)
				|| !read_u32(record.payload, &offset, &raw_x)
				|| !read_u32(record.payload, &offset, &raw_y)
				|| !read_u32(record.payload, &offset, &raw_z)
				|| !read_u64(record.payload, &offset,
					expected_station_revision)
				|| *station_id > static_cast<uint32_t>(
					MinecraftRecipeCatalog::FURNACE_STATION_ID)
				|| (*station_id != static_cast<uint32_t>(
					MinecraftRecipeCatalog::HAND_STATION_ID)
					&& *expected_station_revision == 0U)))
			return (FT_ERR_INVALID_ARGUMENT);
		*station_x = static_cast<int32_t>(raw_x);
		*station_y = static_cast<int32_t>(raw_y);
		*station_z = static_cast<int32_t>(raw_z);
		if (version >= 4U
			&& !read_u64(record.payload, &offset,
				expected_character_state_revision))
			return (FT_ERR_INVALID_ARGUMENT);
		if (version >= 5U
			&& (!read_u64(record.payload, &offset, authenticated_actor_id)
				|| *expected_character_state_revision == 0U
				|| *authenticated_actor_id == 0U))
			return (FT_ERR_INVALID_ARGUMENT);
		if (version >= 6U
			&& (!read_u64(record.payload, &offset, expected_session_id)
				|| !read_u64(record.payload, &offset,
					expected_session_revision)
				|| *expected_session_id == 0U
				|| *expected_session_revision == 0U))
			return (FT_ERR_INVALID_ARGUMENT);
		if (offset != record.payload.size())
			return (FT_ERR_INVALID_ARGUMENT);
		*batch_count = static_cast<int32_t>(raw_batch);
		return (FT_ERR_SUCCESS);
	}

	static int32_t synchronize_output_instance_allocator(
		const game_character &character, const WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog) noexcept
	{
		game_inventory_snapshot inventory = {};
		uint64_t maximum_instance_id = 0U;
		int32_t error_code = world_items.get_max_item_instance_id(
			&maximum_instance_id);

		if (error_code == FT_ERR_SUCCESS)
			error_code = character.get_inventory().get_snapshot(inventory);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		for (const game_inventory_item_snapshot &item : inventory.items)
		{
			if (item.instance_id > maximum_instance_id)
				maximum_instance_id = item.instance_id;
		}
		return (recipe_catalog.ensure_next_output_instance_id_above(
			maximum_instance_id));
	}

	static int32_t abort_prepared_and_checkpoint(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal,
		const GameTransactionJournal::Record &record,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept
	{
		int32_t error_code = journal.mark_aborted(record.scope_id,
			record.request_id);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (snapshot_writer != nullptr)
			return (snapshot_writer->save());
		return (GameSessionPersistence::save_file(snapshot_path, character,
			world_items, recipe_catalog, journal));
	}

	static int32_t apply_prepared(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal,
		const GameTransactionJournal::Record &record,
		MinecraftCraftActionResult *result_out,
		const GameSessionSnapshotWriter *snapshot_writer,
		WorldItemCraftTransaction::CharacterStateRevisionProvider
			character_revision_provider,
		void *character_revision_user_data,
		WorldItemCraftTransaction::CraftPrecommitAuthorizer
			precommit_authorizer,
		void *precommit_authorizer_user_data) noexcept
	{
		uint32_t recipe_id;
		int32_t batch_count;
		uint64_t expected_revision;
		uint64_t expected_catalog_revision;
		uint64_t expected_recipe_book_revision;
		uint64_t expected_character_state_revision;
		uint64_t authenticated_actor_id;
		uint32_t station_id;
		int32_t station_x, station_y, station_z;
		uint64_t expected_station_revision;
		uint64_t expected_session_id;
		uint64_t expected_session_revision;
		int32_t result;
		int32_t error_code;

		if (snapshot_path == nullptr || snapshot_path[0] == '\0'
			|| result_out == nullptr
			|| record.state != GameTransactionJournal::PREPARED
			|| decode(record, &recipe_id, &batch_count,
				&expected_revision, &expected_catalog_revision,
				&expected_recipe_book_revision,
				&expected_character_state_revision, &authenticated_actor_id,
				&station_id, &station_x,
				&station_y, &station_z,
				&expected_station_revision, &expected_session_id,
				&expected_session_revision) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_ARGUMENT);
		if (expected_character_state_revision != 0U)
		{
			uint64_t current_character_state_revision = 0U;
			if (character_revision_provider == nullptr
				|| !character_revision_provider(character,
					&current_character_state_revision,
					character_revision_user_data)
				|| current_character_state_revision
					!= expected_character_state_revision)
			{
				error_code = abort_prepared_and_checkpoint(snapshot_path,
					character, world_items, recipe_catalog, journal, record,
					snapshot_writer);
				return (error_code == FT_ERR_SUCCESS
					? FT_ERR_INVALID_STATE : error_code);
			}
		}
		if (authenticated_actor_id != 0U && (precommit_authorizer == nullptr
			|| !precommit_authorizer(authenticated_actor_id, station_id,
				station_x, station_y, station_z, expected_station_revision,
				expected_session_id, expected_session_revision,
				precommit_authorizer_user_data)))
		{
			error_code = abort_prepared_and_checkpoint(snapshot_path,
				character, world_items, recipe_catalog, journal, record,
				snapshot_writer);
			return (error_code == FT_ERR_SUCCESS
				? FT_ERR_PERMISSION_DENIED : error_code);
		}
		result = synchronize_output_instance_allocator(character, world_items,
			recipe_catalog);
		if (result != FT_ERR_SUCCESS && result != FT_ERR_OUT_OF_RANGE)
			return (result);
		if (expected_catalog_revision == 0U
			|| expected_recipe_book_revision == 0U)
			result = recipe_catalog.craft_recipe_at_station(
				character.get_inventory(), record.request_id, recipe_id,
				batch_count, expected_revision,
				static_cast<MinecraftRecipeCatalog::StationId>(station_id),
				station_x, station_y, station_z, expected_station_revision,
				result_out);
		else
			result = recipe_catalog.craft_recipe_at_station(
				character.get_inventory(), record.request_id, recipe_id,
				batch_count, expected_revision, expected_catalog_revision,
				expected_recipe_book_revision,
				static_cast<MinecraftRecipeCatalog::StationId>(station_id),
				station_x, station_y, station_z, expected_station_revision,
				result_out);
		if (result == FT_ERR_SUCCESS)
			error_code = journal.mark_committed(record.scope_id,
				record.request_id);
		else
			error_code = journal.mark_aborted(record.scope_id,
				record.request_id);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (snapshot_writer != nullptr)
			error_code = snapshot_writer->save();
		else
			error_code = GameSessionPersistence::save_file(snapshot_path,
				character, world_items, recipe_catalog, journal);
		return (error_code == FT_ERR_SUCCESS ? result : error_code);
	}
}

uint64_t WorldItemCraftTransaction::journal_scope() noexcept
{
	return (WORLD_ITEM_CRAFT_SCOPE);
}

int32_t WorldItemCraftTransaction::prepare(const char *snapshot_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal, uint64_t request_id,
	uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision,
	uint32_t station_id, int32_t station_x, int32_t station_y,
	int32_t station_z, uint64_t expected_station_revision,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	uint64_t catalog_revision = 0U;
	uint64_t recipe_book_revision = 0U;
	int32_t result = recipe_catalog.get_catalog_revision(&catalog_revision);
	if (result == FT_ERR_SUCCESS)
		result = recipe_catalog.recipe_book().get_revision(
			&recipe_book_revision);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (WorldItemCraftTransaction::prepare(snapshot_path, character,
		world_items, recipe_catalog, journal, request_id, recipe_id,
		batch_count, expected_inventory_revision, catalog_revision,
		recipe_book_revision, station_id, station_x, station_y, station_z,
		expected_station_revision, snapshot_writer));
}

int32_t WorldItemCraftTransaction::prepare(const char *snapshot_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal, uint64_t request_id,
	uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision,
	uint64_t expected_catalog_revision,
	uint64_t expected_recipe_book_revision,
	uint32_t station_id, int32_t station_x, int32_t station_y,
	int32_t station_z, uint64_t expected_station_revision,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	return (WorldItemCraftTransaction::prepare(snapshot_path, character,
		world_items, recipe_catalog, journal, request_id, recipe_id, batch_count,
		expected_inventory_revision, expected_catalog_revision,
		expected_recipe_book_revision, 0U, 0U, station_id, station_x, station_y,
		station_z, expected_station_revision, snapshot_writer));
}

int32_t WorldItemCraftTransaction::prepare(const char *snapshot_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal, uint64_t request_id,
	uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision,
	uint64_t expected_catalog_revision,
	uint64_t expected_recipe_book_revision,
	uint64_t expected_character_state_revision,
	uint64_t authenticated_actor_id,
	uint32_t station_id, int32_t station_x, int32_t station_y,
	int32_t station_z, uint64_t expected_station_revision,
	const GameSessionSnapshotWriter *snapshot_writer,
	uint64_t expected_session_id,
	uint64_t expected_session_revision) noexcept
{
	std::vector<uint8_t> payload;
	std::vector<uint8_t> journal_before;
	uint64_t sequence;
	bool replayed;
	int32_t error_code;
	const uint32_t payload_version = authenticated_actor_id != 0U
		? (expected_session_id != 0U ? 6U : 5U) : 3U;
	const std::size_t expected_payload_size = payload_version == 6U
		? WORLD_ITEM_CRAFT_PAYLOAD_SIZE
		: (payload_version == 5U ? WORLD_ITEM_CRAFT_V5_PAYLOAD_SIZE
			: WORLD_ITEM_CRAFT_V3_PAYLOAD_SIZE);

	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| request_id == 0U || recipe_id == 0U || batch_count <= 0
		|| expected_catalog_revision == 0U
		|| expected_recipe_book_revision == 0U
		|| ((authenticated_actor_id == 0U)
			!= (expected_character_state_revision == 0U))
		|| ((expected_session_id == 0U)
			!= (expected_session_revision == 0U))
		|| (expected_session_id != 0U && authenticated_actor_id == 0U)
		|| station_id > static_cast<uint32_t>(
			MinecraftRecipeCatalog::FURNACE_STATION_ID)
		|| (station_id == static_cast<uint32_t>(
			MinecraftRecipeCatalog::HAND_STATION_ID)
			&& (station_x != 0 || station_y != 0 || station_z != 0
				|| expected_station_revision != 0U))
		|| (station_id != static_cast<uint32_t>(
			MinecraftRecipeCatalog::HAND_STATION_ID)
			&& expected_station_revision == 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		payload.reserve(expected_payload_size);
		payload.push_back('G');
		payload.push_back('T');
		payload.push_back('C');
		payload.push_back('R');
		append_u32(payload, payload_version);
		append_u64(payload, request_id);
		append_u32(payload, recipe_id);
		append_u32(payload, static_cast<uint32_t>(batch_count));
		append_u64(payload, expected_inventory_revision);
		append_u64(payload, expected_catalog_revision);
		append_u64(payload, expected_recipe_book_revision);
		append_u32(payload, station_id);
		append_u32(payload, static_cast<uint32_t>(station_x));
		append_u32(payload, static_cast<uint32_t>(station_y));
		append_u32(payload, static_cast<uint32_t>(station_z));
		append_u64(payload, expected_station_revision);
		if (expected_character_state_revision != 0U)
			append_u64(payload, expected_character_state_revision);
		if (authenticated_actor_id != 0U)
			append_u64(payload, authenticated_actor_id);
		if (payload_version >= 6U)
		{
			append_u64(payload, expected_session_id);
			append_u64(payload, expected_session_revision);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (payload.size() != expected_payload_size)
		return (FT_ERR_INVALID_STATE);
	error_code = journal.serialize(journal_before);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = journal.prepare(WORLD_ITEM_CRAFT_SCOPE, request_id,
		WORLD_ITEM_CRAFT_TYPE, payload.data(),
		static_cast<uint32_t>(payload.size()), &sequence, &replayed);
	(void)sequence;
	if (error_code != FT_ERR_SUCCESS || replayed)
		return (error_code);
	if (snapshot_writer != nullptr)
		error_code = snapshot_writer->save();
	else
		error_code = GameSessionPersistence::save_file(snapshot_path, character,
			world_items, recipe_catalog, journal);
	if (error_code != FT_ERR_SUCCESS)
	{
		const int32_t rollback_result = journal.deserialize(
			journal_before.data(), journal_before.size());
		if (rollback_result != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
	}
	return (error_code);
}

int32_t WorldItemCraftTransaction::execute(const char *snapshot_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog, GameTransactionJournal &journal,
	uint64_t request_id, uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision,
	uint32_t station_id, int32_t station_x, int32_t station_y,
	int32_t station_z, uint64_t expected_station_revision,
	MinecraftCraftActionResult *result_out,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	uint64_t catalog_revision = 0U;
	uint64_t recipe_book_revision = 0U;
	int32_t result = recipe_catalog.get_catalog_revision(&catalog_revision);
	if (result == FT_ERR_SUCCESS)
		result = recipe_catalog.recipe_book().get_revision(
			&recipe_book_revision);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (WorldItemCraftTransaction::execute(snapshot_path, character,
		world_items, recipe_catalog, journal, request_id, recipe_id,
		batch_count, expected_inventory_revision, catalog_revision,
		recipe_book_revision, station_id, station_x, station_y, station_z,
		expected_station_revision, result_out, snapshot_writer));
}

int32_t WorldItemCraftTransaction::execute(const char *snapshot_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog, GameTransactionJournal &journal,
	uint64_t request_id, uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision,
	uint64_t expected_catalog_revision,
	uint64_t expected_recipe_book_revision,
	uint32_t station_id, int32_t station_x, int32_t station_y,
	int32_t station_z, uint64_t expected_station_revision,
	MinecraftCraftActionResult *result_out,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	return (WorldItemCraftTransaction::execute(snapshot_path, character,
		world_items, recipe_catalog, journal, request_id, recipe_id, batch_count,
		expected_inventory_revision, expected_catalog_revision,
		expected_recipe_book_revision, 0U, 0U, nullptr, nullptr, station_id,
		station_x, station_y, station_z, expected_station_revision, result_out,
		snapshot_writer));
}

int32_t WorldItemCraftTransaction::execute(const char *snapshot_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog, GameTransactionJournal &journal,
	uint64_t request_id, uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision,
	uint64_t expected_catalog_revision,
	uint64_t expected_recipe_book_revision,
	uint64_t authenticated_actor_id,
	uint64_t expected_character_state_revision,
	CharacterStateRevisionProvider character_revision_provider,
		void *character_revision_user_data,
		CraftPrecommitAuthorizer precommit_authorizer,
		void *precommit_authorizer_user_data,
	uint32_t station_id, int32_t station_x, int32_t station_y,
	int32_t station_z, uint64_t expected_station_revision,
	MinecraftCraftActionResult *result_out,
	const GameSessionSnapshotWriter *snapshot_writer,
	uint64_t expected_session_id,
	uint64_t expected_session_revision) noexcept
{
	GameTransactionJournal::Record record = {};
	int32_t error_code;

	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| request_id == 0U || recipe_id == 0U || batch_count <= 0
		|| result_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*result_out = {};
	if (expected_character_state_revision != 0U)
	{
		GameTransactionJournal::Record previous = {};
		const int32_t previous_result = journal.find(WORLD_ITEM_CRAFT_SCOPE,
			request_id, &previous);
		if (previous_result != FT_ERR_SUCCESS
			&& previous_result != FT_ERR_NOT_FOUND)
			return (previous_result);
		if (previous_result == FT_ERR_NOT_FOUND
			|| previous.state == GameTransactionJournal::PREPARED)
		{
			uint64_t current_character_state_revision = 0U;
			if (character_revision_provider == nullptr
				|| !character_revision_provider(character,
					&current_character_state_revision,
					character_revision_user_data))
				return (FT_ERR_CONFIGURATION);
			if (current_character_state_revision
				!= expected_character_state_revision)
				return (FT_ERR_INVALID_STATE);
		}
	}
	error_code = WorldItemCraftTransaction::prepare(snapshot_path, character,
		world_items, recipe_catalog, journal, request_id, recipe_id,
		batch_count, expected_inventory_revision, expected_catalog_revision,
		expected_recipe_book_revision, expected_character_state_revision,
		authenticated_actor_id,
		station_id, station_x, station_y, station_z,
		expected_station_revision, snapshot_writer, expected_session_id,
		expected_session_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = journal.find(WORLD_ITEM_CRAFT_SCOPE, request_id, &record);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (record.state == GameTransactionJournal::PREPARED)
		return (apply_prepared(snapshot_path, character, world_items,
			recipe_catalog, journal, record, result_out, snapshot_writer,
			character_revision_provider, character_revision_user_data,
			precommit_authorizer, precommit_authorizer_user_data));
	if (record.state != GameTransactionJournal::COMMITTED
		&& record.state != GameTransactionJournal::ABORTED)
		return (FT_ERR_INVALID_STATE);
	return (recipe_catalog.replay_craft_action(request_id, recipe_id,
		batch_count, expected_inventory_revision, expected_catalog_revision,
		expected_recipe_book_revision,
		static_cast<MinecraftRecipeCatalog::StationId>(station_id),
		station_x, station_y, station_z, expected_station_revision, result_out));
}

int32_t WorldItemCraftTransaction::recover_pending(
	const char *snapshot_path, game_character &character,
	WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal,
	const GameSessionSnapshotWriter *snapshot_writer,
	WorldItemCraftTransaction::StationContinuityValidator station_validator,
	void *station_validator_user_data,
	WorldItemCraftTransaction::CharacterStateRevisionProvider
		character_revision_provider,
	void *character_revision_user_data,
	WorldItemCraftTransaction::CraftPrecommitAuthorizer precommit_authorizer,
	void *precommit_authorizer_user_data) noexcept
{
	std::vector<GameTransactionJournal::Record> pending;
	if (snapshot_path == nullptr || snapshot_path[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	int32_t error_code = journal.pending_records(pending);

	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	for (const GameTransactionJournal::Record &record : pending)
	{
		MinecraftCraftActionResult result = {};
		uint32_t recipe_id = 0U;
		int32_t batch_count = 0;
		uint64_t expected_inventory_revision = 0U;
		uint64_t expected_catalog_revision = 0U;
		uint64_t expected_recipe_book_revision = 0U;
		uint64_t expected_character_state_revision = 0U;
		uint64_t authenticated_actor_id = 0U;
		uint32_t station_id = 0U;
		int32_t station_x = 0;
		int32_t station_y = 0;
		int32_t station_z = 0;
		uint64_t expected_station_revision = 0U;
		uint64_t expected_session_id = 0U;
		uint64_t expected_session_revision = 0U;

		if (record.scope_id != WORLD_ITEM_CRAFT_SCOPE)
			continue ;
		error_code = decode(record, &recipe_id, &batch_count,
			&expected_inventory_revision, &expected_catalog_revision,
			&expected_recipe_book_revision,
				&expected_character_state_revision, &authenticated_actor_id,
				&station_id, &station_x,
			&station_y, &station_z, &expected_station_revision,
			&expected_session_id, &expected_session_revision);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (expected_character_state_revision != 0U)
		{
			uint64_t current_character_state_revision = 0U;
			if (character_revision_provider == nullptr
				|| !character_revision_provider(character,
					&current_character_state_revision,
					character_revision_user_data))
			{
				error_code = abort_prepared_and_checkpoint(snapshot_path,
					character, world_items, recipe_catalog, journal, record,
					snapshot_writer);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				continue ;
			}
			if (current_character_state_revision
				!= expected_character_state_revision)
			{
				error_code = abort_prepared_and_checkpoint(snapshot_path,
					character, world_items, recipe_catalog, journal, record,
					snapshot_writer);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				continue ;
			}
		}
		if (authenticated_actor_id != 0U && (precommit_authorizer == nullptr
			|| !precommit_authorizer(authenticated_actor_id, station_id,
				station_x, station_y, station_z, expected_station_revision,
				expected_session_id, expected_session_revision,
				precommit_authorizer_user_data)))
		{
			error_code = journal.mark_aborted(record.scope_id,
				record.request_id);
			if (error_code == FT_ERR_SUCCESS && snapshot_writer != nullptr)
				error_code = snapshot_writer->save();
			else if (error_code == FT_ERR_SUCCESS)
				error_code = GameSessionPersistence::save_file(snapshot_path,
					character, world_items, recipe_catalog, journal);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			continue ;
		}
		if (station_id != static_cast<uint32_t>(
				MinecraftRecipeCatalog::HAND_STATION_ID))
		{
			if (station_validator == nullptr
				|| !station_validator(station_id, station_x, station_y, station_z,
					expected_station_revision, station_validator_user_data))
			{
				error_code = abort_prepared_and_checkpoint(snapshot_path,
					character, world_items, recipe_catalog, journal, record,
					snapshot_writer);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				continue ;
			}
		}
		error_code = apply_prepared(snapshot_path, character, world_items,
			recipe_catalog, journal, record, &result, snapshot_writer,
			character_revision_provider, character_revision_user_data,
			precommit_authorizer, precommit_authorizer_user_data);
		if (error_code != FT_ERR_SUCCESS
			&& error_code != FT_ERR_PERMISSION_DENIED
			&& error_code != FT_ERR_INVALID_STATE
			&& error_code != FT_ERR_NOT_FOUND
			&& error_code != FT_ERR_FULL)
			return (error_code);
	}
	return (FT_ERR_SUCCESS);
}
