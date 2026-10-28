#include "WorldItemProcessingTransaction.hpp"

#include "../crafting/MinecraftRecipeCatalog.hpp"
#include "../entities/WorldItemStore.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <limits>
#include <new>
#include <vector>

namespace
{
	static const uint64_t PROCESSING_SCOPE = 0x574F524C44505243ULL;
	static const uint32_t PROCESSING_TYPE = 3U;
	static const uint32_t PROCESSING_VERSION = 4U;
	static const std::size_t PROCESSING_V1_PAYLOAD_SIZE = 32U;
	static const std::size_t PROCESSING_V2_PAYLOAD_SIZE = 52U;
	static const std::size_t PROCESSING_V3_PAYLOAD_SIZE = 60U;
	static const std::size_t PROCESSING_PAYLOAD_SIZE = 76U;

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
		uint64_t *expected_revision, int32_t *station_x,
		int32_t *station_y, int32_t *station_z,
		uint64_t *expected_station_revision,
		uint64_t *expected_catalog_revision,
		uint64_t *expected_recipe_book_revision,
		uint64_t *authenticated_actor_id) noexcept
	{
		std::size_t offset = 4U;
		uint32_t version;
		uint32_t raw_batch;
		uint64_t request_id;
		uint32_t raw_station_x = 0U;
		uint32_t raw_station_y = 0U;
		uint32_t raw_station_z = 0U;
		uint64_t station_revision = 0U;
		uint64_t catalog_revision = 0U;
		uint64_t recipe_book_revision = 0U;

		if (recipe_id == nullptr || batch_count == nullptr
			|| expected_revision == nullptr || station_x == nullptr
			|| station_y == nullptr || station_z == nullptr
			|| expected_station_revision == nullptr
			|| expected_catalog_revision == nullptr
			|| expected_recipe_book_revision == nullptr
			|| authenticated_actor_id == nullptr
			|| record.scope_id != PROCESSING_SCOPE
			|| record.transaction_type != PROCESSING_TYPE
			|| (record.payload.size() != PROCESSING_V1_PAYLOAD_SIZE
				&& record.payload.size() != PROCESSING_V2_PAYLOAD_SIZE
				&& record.payload.size() != PROCESSING_V3_PAYLOAD_SIZE
				&& record.payload.size() != PROCESSING_PAYLOAD_SIZE)
			|| record.payload[0] != 'G' || record.payload[1] != 'T'
			|| record.payload[2] != 'P' || record.payload[3] != 'R'
			|| !read_u32(record.payload, &offset, &version)
			|| (version < 1U || version > PROCESSING_VERSION)
			|| (version == 1U
				&& record.payload.size() != PROCESSING_V1_PAYLOAD_SIZE)
			|| (version == 2U
				&& record.payload.size() != PROCESSING_V2_PAYLOAD_SIZE)
			|| (version == 3U
				&& record.payload.size() != PROCESSING_V3_PAYLOAD_SIZE)
			|| (version == 4U
				&& record.payload.size() != PROCESSING_PAYLOAD_SIZE)
			|| !read_u64(record.payload, &offset, &request_id)
			|| !read_u32(record.payload, &offset, recipe_id)
			|| !read_u32(record.payload, &offset, &raw_batch)
			|| !read_u64(record.payload, &offset, expected_revision)
			|| (version >= 2U
				&& (!read_u32(record.payload, &offset, &raw_station_x)
					|| !read_u32(record.payload, &offset, &raw_station_y)
					|| !read_u32(record.payload, &offset, &raw_station_z)
					|| !read_u64(record.payload, &offset, &station_revision)
					|| station_revision == 0U))
			|| (version >= 4U
				&& (!read_u64(record.payload, &offset, &catalog_revision)
					|| !read_u64(record.payload, &offset,
						&recipe_book_revision)
					|| catalog_revision == 0U || recipe_book_revision == 0U))
			|| (version >= 3U
				&& (!read_u64(record.payload, &offset, authenticated_actor_id)
					|| *authenticated_actor_id == 0U))
			|| offset != record.payload.size()
			|| request_id != record.request_id || *recipe_id == 0U
			|| *expected_revision == 0U || raw_batch == 0U
			|| raw_batch > static_cast<uint32_t>(
				std::numeric_limits<int32_t>::max()))
			return (FT_ERR_INVALID_ARGUMENT);
		*batch_count = static_cast<int32_t>(raw_batch);
		*station_x = static_cast<int32_t>(raw_station_x);
		*station_y = static_cast<int32_t>(raw_station_y);
		*station_z = static_cast<int32_t>(raw_station_z);
		*expected_station_revision = station_revision;
		*expected_catalog_revision = catalog_revision;
		*expected_recipe_book_revision = recipe_book_revision;
		if (version < 3U)
			*authenticated_actor_id = 0U;
		return (FT_ERR_SUCCESS);
	}

	static int32_t save_processing_checkpoint(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept
	{
		if (snapshot_writer != nullptr)
			return (snapshot_writer->save());
		return (GameSessionPersistence::save_file(snapshot_path, character,
			world_items, recipe_catalog, journal));
	}

	struct PreparedProcessingAuthorization
	{
		bool (*station_validator)(uint32_t, int32_t, int32_t, int32_t,
			uint64_t, void *) noexcept;
		void *station_user_data;
		bool (*actor_authorizer)(uint64_t, void *) noexcept;
		void *actor_user_data;
	};

	static bool authorize_prepared_processing(uint64_t actor_id,
		int32_t station_x, int32_t station_y, int32_t station_z,
		uint64_t station_revision, void *user_data) noexcept
	{
		if (user_data == nullptr)
			return (false);
		PreparedProcessingAuthorization *authorization =
			static_cast<PreparedProcessingAuthorization *>(user_data);
		return (actor_id != 0U && authorization->actor_authorizer != nullptr
			&& authorization->actor_authorizer(actor_id,
				authorization->actor_user_data)
			&& authorization->station_validator != nullptr
			&& authorization->station_validator(
				static_cast<uint32_t>(MinecraftRecipeCatalog::FURNACE_STATION_ID),
				station_x, station_y, station_z, station_revision,
				authorization->station_user_data));
	}

	static int32_t apply_prepared(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal,
		const GameTransactionJournal::Record &record,
		MinecraftProcessingActionResult *result_out,
		const GameSessionSnapshotWriter *snapshot_writer,
		bool (*precommit_authorizer)(uint64_t, int32_t, int32_t, int32_t,
			uint64_t, void *) noexcept,
		void *precommit_authorizer_user_data) noexcept
	{
		uint32_t recipe_id;
		int32_t batch_count;
		uint64_t expected_revision;
		int32_t station_x;
		int32_t station_y;
		int32_t station_z;
		uint64_t expected_station_revision;
		uint64_t expected_catalog_revision;
		uint64_t expected_recipe_book_revision;
		uint64_t authenticated_actor_id;
		int32_t result;
		int32_t journal_result;

		if (snapshot_path == nullptr || snapshot_path[0] == '\0'
			|| result_out == nullptr
			|| record.state != GameTransactionJournal::PREPARED
			|| decode(record, &recipe_id, &batch_count, &expected_revision,
				&station_x, &station_y, &station_z,
				&expected_station_revision, &expected_catalog_revision,
				&expected_recipe_book_revision, &authenticated_actor_id)
				!= FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_ARGUMENT);
		if (authenticated_actor_id != 0U
			&& (precommit_authorizer == nullptr
				|| !precommit_authorizer(authenticated_actor_id, station_x,
					station_y, station_z, expected_station_revision,
					precommit_authorizer_user_data)))
		{
			result = journal.mark_aborted(record.scope_id, record.request_id);
			if (result == FT_ERR_SUCCESS)
				result = save_processing_checkpoint(snapshot_path, character,
					world_items, recipe_catalog, journal, snapshot_writer);
			return (result == FT_ERR_SUCCESS ? FT_ERR_PERMISSION_DENIED : result);
		}
		result = recipe_catalog.start_processing(
			character.get_inventory(), record.request_id, recipe_id, batch_count,
			expected_revision, expected_catalog_revision,
			expected_recipe_book_revision, station_x, station_y, station_z,
			expected_station_revision, result_out);
		if (result == FT_ERR_SUCCESS)
			journal_result = journal.mark_committed(record.scope_id,
				record.request_id);
		else
			journal_result = journal.mark_aborted(record.scope_id,
				record.request_id);
		if (journal_result != FT_ERR_SUCCESS)
			return (journal_result);
		if (snapshot_writer != nullptr)
			journal_result = snapshot_writer->save();
		else
			journal_result = GameSessionPersistence::save_file(snapshot_path,
				character, world_items, recipe_catalog, journal);
		return (journal_result == FT_ERR_SUCCESS ? result : journal_result);
	}
}

uint64_t WorldItemProcessingTransaction::journal_scope() noexcept
{
	return (PROCESSING_SCOPE);
}

int32_t WorldItemProcessingTransaction::prepare(const char *snapshot_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal, uint64_t request_id,
	uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision, int32_t station_x,
	int32_t station_y, int32_t station_z,
	uint64_t expected_station_revision,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	return (WorldItemProcessingTransaction::prepare(snapshot_path, character,
		world_items, recipe_catalog, journal, request_id, recipe_id, batch_count,
		expected_inventory_revision, 0U, 0U, 0U, station_x,
		station_y, station_z,
		expected_station_revision, snapshot_writer));
}

int32_t WorldItemProcessingTransaction::prepare(const char *snapshot_path,
	const game_character &character, const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal, uint64_t request_id,
	uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision,
	uint64_t expected_catalog_revision,
	uint64_t expected_recipe_book_revision,
	uint64_t authenticated_actor_id,
	int32_t station_x, int32_t station_y, int32_t station_z,
	uint64_t expected_station_revision,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	std::vector<uint8_t> payload;
	std::vector<uint8_t> journal_before;
	uint64_t sequence;
	bool replayed;
	int32_t error_code;

	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| request_id == 0U || recipe_id == 0U || batch_count <= 0
		|| expected_inventory_revision == 0U
		|| expected_station_revision == 0U
		|| ((expected_catalog_revision == 0U)
			!= (expected_recipe_book_revision == 0U))
		|| ((authenticated_actor_id == 0U)
			!= (expected_catalog_revision == 0U)))
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		const uint32_t version = authenticated_actor_id == 0U ? 2U
			: PROCESSING_VERSION;
		const std::size_t expected_payload_size = version == 2U
			? PROCESSING_V2_PAYLOAD_SIZE : (version == 3U
				? PROCESSING_V3_PAYLOAD_SIZE : PROCESSING_PAYLOAD_SIZE);
		payload.reserve(expected_payload_size);
		payload.push_back('G');
		payload.push_back('T');
		payload.push_back('P');
		payload.push_back('R');
		append_u32(payload, version);
		append_u64(payload, request_id);
		append_u32(payload, recipe_id);
		append_u32(payload, static_cast<uint32_t>(batch_count));
		append_u64(payload, expected_inventory_revision);
		append_u32(payload, static_cast<uint32_t>(station_x));
		append_u32(payload, static_cast<uint32_t>(station_y));
		append_u32(payload, static_cast<uint32_t>(station_z));
		append_u64(payload, expected_station_revision);
		if (version >= 4U)
		{
			append_u64(payload, expected_catalog_revision);
			append_u64(payload, expected_recipe_book_revision);
		}
		if (authenticated_actor_id != 0U)
			append_u64(payload, authenticated_actor_id);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (payload.size() != (authenticated_actor_id == 0U
		? PROCESSING_V2_PAYLOAD_SIZE : PROCESSING_PAYLOAD_SIZE))
		return (FT_ERR_INVALID_STATE);
	error_code = journal.serialize(journal_before);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = journal.prepare(PROCESSING_SCOPE, request_id,
		PROCESSING_TYPE, payload.data(),
		static_cast<uint32_t>(payload.size()), &sequence, &replayed);
	(void)sequence;
	if (error_code != FT_ERR_SUCCESS || replayed)
		return (error_code);
	if (snapshot_writer != nullptr)
		error_code = snapshot_writer->save();
	else
		error_code = GameSessionPersistence::save_file(snapshot_path,
			character, world_items, recipe_catalog, journal);
	if (error_code != FT_ERR_SUCCESS)
	{
		const int32_t rollback_result = journal.deserialize(
			journal_before.data(), journal_before.size());
		if (rollback_result != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
	}
	return (error_code);
}

int32_t WorldItemProcessingTransaction::execute(const char *snapshot_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog, GameTransactionJournal &journal,
	uint64_t request_id, uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision, int32_t station_x,
	int32_t station_y, int32_t station_z,
	uint64_t expected_station_revision,
	MinecraftProcessingActionResult *result_out,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	return (WorldItemProcessingTransaction::execute(snapshot_path, character,
		world_items, recipe_catalog, journal, request_id, recipe_id, batch_count,
		expected_inventory_revision, 0U, 0U, 0U, station_x, station_y, station_z,
		expected_station_revision, result_out, snapshot_writer));
}

int32_t WorldItemProcessingTransaction::execute(const char *snapshot_path,
	game_character &character, WorldItemStore &world_items,
	MinecraftRecipeCatalog &recipe_catalog, GameTransactionJournal &journal,
	uint64_t request_id, uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision,
	uint64_t expected_catalog_revision,
	uint64_t expected_recipe_book_revision,
	uint64_t authenticated_actor_id,
	int32_t station_x, int32_t station_y, int32_t station_z,
	uint64_t expected_station_revision,
	MinecraftProcessingActionResult *result_out,
	const GameSessionSnapshotWriter *snapshot_writer,
	bool (*precommit_authorizer)(uint64_t authenticated_actor_id,
		int32_t station_x, int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision, void *user_data) noexcept,
	void *precommit_authorizer_user_data) noexcept
{
	GameTransactionJournal::Record record = {};
	int32_t error_code;

	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| request_id == 0U || recipe_id == 0U || batch_count <= 0
		|| result_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*result_out = {};
	error_code = WorldItemProcessingTransaction::prepare(snapshot_path,
		character, world_items, recipe_catalog, journal, request_id,
		recipe_id, batch_count, expected_inventory_revision,
		expected_catalog_revision, expected_recipe_book_revision,
		authenticated_actor_id, station_x,
		station_y, station_z, expected_station_revision, snapshot_writer);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = journal.find(PROCESSING_SCOPE, request_id, &record);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (authenticated_actor_id != 0U
		&& (precommit_authorizer == nullptr
			|| !precommit_authorizer(authenticated_actor_id, station_x,
				station_y, station_z, expected_station_revision,
				precommit_authorizer_user_data)))
	{
		if (record.state == GameTransactionJournal::PREPARED)
		{
			error_code = journal.mark_aborted(PROCESSING_SCOPE, request_id);
			if (error_code == FT_ERR_SUCCESS)
				error_code = save_processing_checkpoint(snapshot_path,
					character, world_items, recipe_catalog, journal,
					snapshot_writer);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
		}
		return (FT_ERR_PERMISSION_DENIED);
	}
	if (record.state == GameTransactionJournal::PREPARED)
		return (apply_prepared(snapshot_path, character, world_items,
			recipe_catalog, journal, record, result_out, snapshot_writer,
			precommit_authorizer, precommit_authorizer_user_data));
	if (record.state == GameTransactionJournal::ABORTED)
		return (FT_ERR_INVALID_OPERATION);
	if (record.state != GameTransactionJournal::COMMITTED)
		return (FT_ERR_INVALID_STATE);
	return (recipe_catalog.start_processing(character.get_inventory(),
		request_id, recipe_id, batch_count, expected_inventory_revision,
		expected_catalog_revision, expected_recipe_book_revision,
		station_x, station_y, station_z, expected_station_revision,
		result_out));
}

int32_t WorldItemProcessingTransaction::recover_pending(
	const char *snapshot_path, game_character &character,
	WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal,
	const GameSessionSnapshotWriter *snapshot_writer,
	bool (*station_continuity_validator)(uint32_t station_id,
		int32_t station_x, int32_t station_y, int32_t station_z,
		uint64_t expected_station_revision, void *user_data) noexcept,
	void *station_continuity_user_data,
	bool (*actor_authorizer)(uint64_t authenticated_actor_id,
		void *user_data) noexcept,
	void *actor_authorizer_user_data) noexcept
{
	std::vector<GameTransactionJournal::Record> pending;
	int32_t error_code = journal.pending_records(pending);

	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	for (const GameTransactionJournal::Record &record : pending)
	{
		MinecraftProcessingActionResult result = {};
		uint32_t recipe_id = 0U;
		int32_t batch_count = 0;
		uint64_t expected_revision = 0U;
		int32_t station_x = 0;
		int32_t station_y = 0;
		int32_t station_z = 0;
		uint64_t expected_station_revision = 0U;
		uint64_t expected_catalog_revision = 0U;
		uint64_t expected_recipe_book_revision = 0U;
		uint64_t authenticated_actor_id = 0U;

		if (record.scope_id != PROCESSING_SCOPE)
			continue ;
		if (decode(record, &recipe_id, &batch_count, &expected_revision,
				&station_x, &station_y, &station_z,
				&expected_station_revision, &expected_catalog_revision,
				&expected_recipe_book_revision, &authenticated_actor_id)
			!= FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_ARGUMENT);
		if (authenticated_actor_id != 0U && (actor_authorizer == nullptr
			|| !actor_authorizer(authenticated_actor_id,
				actor_authorizer_user_data)))
		{
			error_code = journal.mark_aborted(PROCESSING_SCOPE,
				record.request_id);
			if (error_code == FT_ERR_SUCCESS)
				error_code = save_processing_checkpoint(snapshot_path,
					character, world_items, recipe_catalog, journal,
					snapshot_writer);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			continue ;
		}
		if (station_continuity_validator == nullptr
			|| !station_continuity_validator(static_cast<uint32_t>(
				MinecraftRecipeCatalog::FURNACE_STATION_ID), station_x,
				station_y, station_z, expected_station_revision,
				station_continuity_user_data))
			continue ;
		PreparedProcessingAuthorization authorization = {
			station_continuity_validator, station_continuity_user_data,
			actor_authorizer, actor_authorizer_user_data};
		error_code = apply_prepared(snapshot_path, character, world_items,
			recipe_catalog, journal, record, &result, snapshot_writer,
			&authorize_prepared_processing, &authorization);
		if (error_code != FT_ERR_SUCCESS
			&& error_code != FT_ERR_INVALID_STATE
			&& error_code != FT_ERR_NOT_FOUND
			&& error_code != FT_ERR_FULL
			&& error_code != FT_ERR_PERMISSION_DENIED)
			return (error_code);
	}
	return (FT_ERR_SUCCESS);
}
