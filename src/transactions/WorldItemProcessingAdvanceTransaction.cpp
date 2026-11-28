#include "WorldItemProcessingAdvanceTransaction.hpp"

#include "../app/GameSessionPersistence.hpp"
#include "../crafting/MinecraftRecipeCatalog.hpp"
#include "../entities/WorldItemStore.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <limits>
#include <new>
#include <vector>

namespace
{
	static const uint64_t PROCESSING_ADVANCE_SCOPE = 0x574F524C44504144ULL;
	static const uint32_t PROCESSING_ADVANCE_TYPE = 7U;
	static const uint32_t PROCESSING_ADVANCE_VERSION_V1 = 1U;
	static const uint32_t PROCESSING_ADVANCE_VERSION_V2 = 2U;
	static const std::size_t PROCESSING_ADVANCE_V1_PAYLOAD_SIZE = 24U;
	static const std::size_t PROCESSING_ADVANCE_V2_BASE_SIZE = 28U;
	static const uint32_t PROCESSING_ADVANCE_MAXIMUM_JOBS = 64U;

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

	static int32_t decode(const GameTransactionJournal::Record &record,
		uint32_t *server_ticks, uint32_t *job_budget,
		std::vector<uint64_t> *authorized_request_ids_out,
		bool *all_jobs_out) noexcept
	{
		std::size_t offset = 4U;
		uint32_t version;
		uint64_t request_id;

		if (server_ticks == nullptr || job_budget == nullptr
			|| authorized_request_ids_out == nullptr || all_jobs_out == nullptr
			|| record.scope_id != PROCESSING_ADVANCE_SCOPE
			|| record.transaction_type != PROCESSING_ADVANCE_TYPE
			|| record.payload[0] != 'G' || record.payload[1] != 'T'
			|| record.payload[2] != 'P' || record.payload[3] != 'A'
			|| !read_u32(record.payload, &offset, &version))
			return (FT_ERR_INVALID_ARGUMENT);
		if ((version == PROCESSING_ADVANCE_VERSION_V1
				&& record.payload.size() != PROCESSING_ADVANCE_V1_PAYLOAD_SIZE)
			|| (version == PROCESSING_ADVANCE_VERSION_V2
				&& (record.payload.size() < PROCESSING_ADVANCE_V2_BASE_SIZE
					|| record.payload.size() > PROCESSING_ADVANCE_V2_BASE_SIZE
						+ PROCESSING_ADVANCE_MAXIMUM_JOBS * 8U))
			|| (version != PROCESSING_ADVANCE_VERSION_V1
				&& version != PROCESSING_ADVANCE_VERSION_V2)
			|| offset + 8U > record.payload.size())
			return (FT_ERR_INVALID_ARGUMENT);
		request_id = 0U;
		for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			request_id |= static_cast<uint64_t>(record.payload[offset++]) << shift;
		if (request_id != record.request_id
			|| !read_u32(record.payload, &offset, server_ticks)
			|| *server_ticks == 0U || *server_ticks > 1200U
			|| !read_u32(record.payload, &offset, job_budget)
			|| *job_budget == 0U
			|| *job_budget > PROCESSING_ADVANCE_MAXIMUM_JOBS)
			return (FT_ERR_INVALID_ARGUMENT);
		*all_jobs_out = version == PROCESSING_ADVANCE_VERSION_V1;
		if (*all_jobs_out)
			return (offset == record.payload.size()
				? FT_ERR_SUCCESS : FT_ERR_INVALID_ARGUMENT);
		uint32_t request_count = 0U;
		if (!read_u32(record.payload, &offset, &request_count)
			|| request_count == 0U
			|| request_count > PROCESSING_ADVANCE_MAXIMUM_JOBS
			|| record.payload.size() != PROCESSING_ADVANCE_V2_BASE_SIZE
				+ static_cast<std::size_t>(request_count) * 8U)
			return (FT_ERR_INVALID_ARGUMENT);
		try
		{
			authorized_request_ids_out->reserve(request_count);
			for (uint32_t index = 0U; index < request_count; ++index)
			{
				uint64_t id = 0U;
				for (uint32_t shift = 0U; shift < 64U; shift += 8U)
					id |= static_cast<uint64_t>(record.payload[offset++]) << shift;
				if (id == 0U || (!authorized_request_ids_out->empty()
						&& authorized_request_ids_out->back() >= id))
					return (FT_ERR_INVALID_ARGUMENT);
				authorized_request_ids_out->push_back(id);
			}
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		if (offset != record.payload.size())
			return (FT_ERR_INVALID_ARGUMENT);
		return (FT_ERR_SUCCESS);
	}

	static int32_t apply_prepared(const char *snapshot_path,
		game_character &character, WorldItemStore &world_items,
		MinecraftRecipeCatalog &recipe_catalog, GameTransactionJournal &journal,
		const GameTransactionJournal::Record &record,
		uint32_t *completed_out, uint32_t *pending_output_out,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept
	{
		game_inventory_snapshot inventory_before = {};
		std::vector<uint8_t> processing_before;
		std::vector<uint8_t> journal_before;
		uint32_t server_ticks = 0U;
		uint32_t job_budget = 0U;
		std::vector<uint64_t> authorized_request_ids;
		bool all_jobs = false;
		uint32_t completed = 0U;
		uint32_t pending = 0U;
		uint32_t job_count = 0U;
		int32_t result;
		int32_t rollback_result = FT_ERR_SUCCESS;

		if (snapshot_path == nullptr || snapshot_path[0] == '\0'
			|| record.state != GameTransactionJournal::PREPARED
			|| completed_out == nullptr || pending_output_out == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		result = decode(record, &server_ticks, &job_budget,
			&authorized_request_ids, &all_jobs);
		if (result == FT_ERR_SUCCESS)
			result = character.get_inventory().get_snapshot(inventory_before);
		if (result == FT_ERR_SUCCESS)
			result = recipe_catalog.serialize_processing_state(processing_before);
		if (result == FT_ERR_SUCCESS)
			result = journal.serialize(journal_before);
		if (result == FT_ERR_SUCCESS)
			result = recipe_catalog.get_processing_job_count(&job_count);
		if (result == FT_ERR_SUCCESS && job_count != 0U)
		{
			if (all_jobs)
				result = recipe_catalog.advance_processing(
					character.get_inventory(), server_ticks, job_budget,
					&completed, &pending);
			else
				result = recipe_catalog.advance_processing(
					character.get_inventory(), server_ticks,
					authorized_request_ids, job_budget, &completed, &pending);
		}
		if (result == FT_ERR_SUCCESS)
			result = journal.mark_committed(record.scope_id, record.request_id);
		if (result == FT_ERR_SUCCESS)
			result = journal.prune_terminal_scope(PROCESSING_ADVANCE_SCOPE,
				record.request_id);
		if (result == FT_ERR_SUCCESS)
		{
			if (snapshot_writer != nullptr)
				result = snapshot_writer->save();
			else
				result = GameSessionPersistence::save_file(snapshot_path,
					character, world_items, recipe_catalog, journal);
		}
		if (result != FT_ERR_SUCCESS && !processing_before.empty())
		{
			rollback_result = character.get_inventory().restore_snapshot(
				inventory_before);
			const int32_t processing_restore =
				recipe_catalog.restore_processing_state(processing_before.data(),
					processing_before.size());
			if (rollback_result == FT_ERR_SUCCESS
				&& processing_restore != FT_ERR_SUCCESS)
				rollback_result = processing_restore;
			const int32_t journal_restore = journal.deserialize(
				journal_before.data(), journal_before.size());
			if (rollback_result == FT_ERR_SUCCESS
				&& journal_restore != FT_ERR_SUCCESS)
				rollback_result = journal_restore;
		}
		if (rollback_result != FT_ERR_SUCCESS)
			return (rollback_result);
		if (result == FT_ERR_SUCCESS)
		{
			*completed_out = completed;
			*pending_output_out = pending;
		}
		return (result);
	}
}

uint64_t WorldItemProcessingAdvanceTransaction::journal_scope() noexcept
{
	return (PROCESSING_ADVANCE_SCOPE);
}

static int32_t world_item_processing_advance_execute_payload(
	const char *snapshot_path, game_character &character,
	WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal, uint64_t request_id,
	const std::vector<uint8_t> &payload, uint32_t server_ticks,
	uint32_t job_budget, uint32_t *completed_out,
	uint32_t *pending_output_out,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	GameTransactionJournal::Record record = {};
	uint32_t job_count = 0U;
	uint64_t sequence = 0U;
	uint64_t next_request_id = 0U;
	bool replayed = false;
	bool has_record = false;
	int32_t result;

	if (snapshot_path == nullptr || snapshot_path[0] == '\0'
		|| request_id == 0U || server_ticks == 0U || server_ticks > 1200U
		|| job_budget == 0U || job_budget > 64U || completed_out == nullptr
		|| pending_output_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*completed_out = 0U;
	*pending_output_out = 0U;
	if (payload.size() > GameTransactionJournal::MAXIMUM_PAYLOAD_BYTES)
		return (FT_ERR_OUT_OF_RANGE);
	result = WorldItemProcessingAdvanceTransaction::recover_pending(
		snapshot_path, character, world_items, recipe_catalog, journal,
		snapshot_writer);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = journal.find(PROCESSING_ADVANCE_SCOPE, request_id, &record);
	if (result == FT_ERR_NOT_FOUND)
	{
		result = journal.next_request_id(PROCESSING_ADVANCE_SCOPE,
			&next_request_id);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (request_id != next_request_id)
			return (FT_ERR_INVALID_ARGUMENT);
	}
	else if (result != FT_ERR_SUCCESS)
		return (result);
	else
		has_record = true;
	if (has_record)
	{
		if (record.transaction_type != PROCESSING_ADVANCE_TYPE
			|| record.payload != payload)
			return (FT_ERR_INVALID_ARGUMENT);
		if (record.state == GameTransactionJournal::COMMITTED)
			return (FT_ERR_SUCCESS);
		if (record.state == GameTransactionJournal::ABORTED)
			return (FT_ERR_INVALID_OPERATION);
		return (FT_ERR_INVALID_STATE);
	}
	result = recipe_catalog.get_processing_job_count(&job_count);
	if (result != FT_ERR_SUCCESS || job_count == 0U)
		return (result);
	result = journal.prepare(PROCESSING_ADVANCE_SCOPE, request_id,
		PROCESSING_ADVANCE_TYPE, payload.data(),
		static_cast<uint32_t>(payload.size()), &sequence, &replayed);
	if (result != FT_ERR_SUCCESS)
		return (result);
	(void)sequence;
	result = journal.find(PROCESSING_ADVANCE_SCOPE, request_id, &record);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (record.transaction_type != PROCESSING_ADVANCE_TYPE
		|| record.payload != payload)
		return (FT_ERR_INVALID_ARGUMENT);
	if (record.state == GameTransactionJournal::COMMITTED)
		return (FT_ERR_SUCCESS);
	if (record.state == GameTransactionJournal::ABORTED)
		return (FT_ERR_INVALID_OPERATION);
	if (record.state != GameTransactionJournal::PREPARED)
		return (FT_ERR_INVALID_STATE);
	if (!replayed)
	{
		if (snapshot_writer != nullptr)
			result = snapshot_writer->save();
		else
			result = GameSessionPersistence::save_file(snapshot_path,
				character, world_items, recipe_catalog, journal);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	return (apply_prepared(snapshot_path, character, world_items,
		recipe_catalog, journal, record, completed_out, pending_output_out,
		snapshot_writer));
}

int32_t WorldItemProcessingAdvanceTransaction::execute(
	const char *snapshot_path, game_character &character,
	WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal, uint64_t request_id,
	uint32_t server_ticks, uint32_t job_budget, uint32_t *completed_out,
	uint32_t *pending_output_out,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	std::vector<uint8_t> payload;
	if (request_id == 0U || server_ticks == 0U || server_ticks > 1200U
		|| job_budget == 0U || job_budget > PROCESSING_ADVANCE_MAXIMUM_JOBS
		|| completed_out == nullptr || pending_output_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		payload.reserve(PROCESSING_ADVANCE_V1_PAYLOAD_SIZE);
		payload.push_back('G'); payload.push_back('T');
		payload.push_back('P'); payload.push_back('A');
		append_u32(payload, PROCESSING_ADVANCE_VERSION_V1);
		append_u64(payload, request_id);
		append_u32(payload, server_ticks);
		append_u32(payload, job_budget);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (world_item_processing_advance_execute_payload(snapshot_path,
		character, world_items, recipe_catalog, journal, request_id, payload,
		server_ticks, job_budget, completed_out, pending_output_out,
		snapshot_writer));
}

int32_t WorldItemProcessingAdvanceTransaction::execute(
	const char *snapshot_path, game_character &character,
	WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal, uint64_t request_id,
	const std::vector<uint64_t> &authorized_request_ids,
	uint32_t server_ticks, uint32_t job_budget, uint32_t *completed_out,
	uint32_t *pending_output_out,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	std::vector<uint8_t> payload;
	if (request_id == 0U || authorized_request_ids.empty()
		|| authorized_request_ids.size() > PROCESSING_ADVANCE_MAXIMUM_JOBS
		|| server_ticks == 0U || server_ticks > 1200U
		|| job_budget == 0U || job_budget > PROCESSING_ADVANCE_MAXIMUM_JOBS
		|| completed_out == nullptr || pending_output_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	for (std::size_t index = 0U; index < authorized_request_ids.size(); ++index)
		if (authorized_request_ids[index] == 0U
			|| (index != 0U && authorized_request_ids[index - 1U]
				>= authorized_request_ids[index]))
			return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		payload.reserve(PROCESSING_ADVANCE_V2_BASE_SIZE
			+ authorized_request_ids.size() * 8U);
		payload.push_back('G'); payload.push_back('T');
		payload.push_back('P'); payload.push_back('A');
		append_u32(payload, PROCESSING_ADVANCE_VERSION_V2);
		append_u64(payload, request_id);
		append_u32(payload, server_ticks);
		append_u32(payload, job_budget);
		append_u32(payload,
			static_cast<uint32_t>(authorized_request_ids.size()));
		for (uint64_t id : authorized_request_ids)
			append_u64(payload, id);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (world_item_processing_advance_execute_payload(snapshot_path,
		character, world_items, recipe_catalog, journal, request_id, payload,
		server_ticks, job_budget, completed_out, pending_output_out,
		snapshot_writer));
}

int32_t WorldItemProcessingAdvanceTransaction::recover_pending(
	const char *snapshot_path, game_character &character,
	WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	std::vector<GameTransactionJournal::Record> records;
	int32_t result = journal.pending_records(records);
	if (result != FT_ERR_SUCCESS)
		return (result);
	for (const GameTransactionJournal::Record &record : records)
	{
		if (record.scope_id != PROCESSING_ADVANCE_SCOPE)
			continue ;
		uint32_t completed = 0U;
		uint32_t pending = 0U;
		result = apply_prepared(snapshot_path, character, world_items,
			recipe_catalog, journal, record, &completed, &pending,
			snapshot_writer);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	return (FT_ERR_SUCCESS);
}
