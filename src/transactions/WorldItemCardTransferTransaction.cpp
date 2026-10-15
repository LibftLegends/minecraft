#include "WorldItemCardTransferTransaction.hpp"

#include "../app/GameSessionPersistence.hpp"
#include "../cards/RegenerationCardSession.hpp"
#include "../entities/WorldItemStore.hpp"
#include "../transactions/GameTransactionJournal.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <limits>
#include <new>
#include <vector>

namespace
{
	static const uint64_t CARD_TRANSFER_SCOPE = 0x574F524C44435452ULL;
	static const uint32_t CARD_TRANSFER_TYPE = 4U;
	static const uint32_t CARD_TRANSFER_VERSION = 1U;
	static const uint32_t CARD_TRANSFER_IMPORT = 1U;
	static const uint32_t CARD_TRANSFER_EXPORT = 2U;
	static const std::size_t CARD_TRANSFER_PAYLOAD_SIZE = 32U;

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

	struct CardTransferAction
	{
		uint32_t operation;
		uint32_t value;
		uint64_t expected_inventory_revision;
	};

	static int32_t decode(const GameTransactionJournal::Record &record,
		CardTransferAction *action_out) noexcept
	{
		std::size_t offset = 4U;
		uint32_t version;
		uint64_t request_id;
		CardTransferAction action = {};

		if (action_out == nullptr || record.scope_id != CARD_TRANSFER_SCOPE
			|| record.transaction_type != CARD_TRANSFER_TYPE
			|| record.payload.size() != CARD_TRANSFER_PAYLOAD_SIZE
			|| record.payload[0] != 'G' || record.payload[1] != 'T'
			|| record.payload[2] != 'C' || record.payload[3] != 'T'
			|| !read_u32(record.payload, &offset, &version)
			|| version != CARD_TRANSFER_VERSION
			|| !read_u32(record.payload, &offset, &action.operation)
			|| !read_u64(record.payload, &offset, &request_id)
			|| !read_u32(record.payload, &offset, &action.value)
			|| !read_u64(record.payload, &offset,
				&action.expected_inventory_revision)
			|| offset != record.payload.size()
			|| request_id != record.request_id
			|| (action.operation != CARD_TRANSFER_IMPORT
				&& action.operation != CARD_TRANSFER_EXPORT)
			|| (action.operation == CARD_TRANSFER_IMPORT
				&& action.value > static_cast<uint32_t>(
					std::numeric_limits<int32_t>::max()))
			|| (action.operation == CARD_TRANSFER_EXPORT && action.value == 0U))
			return (FT_ERR_INVALID_ARGUMENT);
		*action_out = action;
		return (FT_ERR_SUCCESS);
	}

	static int32_t save_checkpoint(const char *snapshot_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept
	{
		if (snapshot_path == nullptr || snapshot_path[0] == '\0')
			return (FT_ERR_INVALID_ARGUMENT);
		if (snapshot_writer != nullptr)
			return (snapshot_writer->save());
		return (GameSessionPersistence::save_file(snapshot_path, character,
			world_items, recipe_catalog, journal));
	}

	static int32_t prepare(const char *snapshot_path,
		const game_character &character, const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal, uint32_t operation, uint32_t value,
		uint64_t expected_inventory_revision, uint64_t request_id,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept
	{
		std::vector<uint8_t> payload;
		uint64_t sequence;
		bool replayed;
		int32_t result;

		if (snapshot_path == nullptr || snapshot_path[0] == '\0'
			|| request_id == 0U
			|| (operation == CARD_TRANSFER_EXPORT && value == 0U)
			|| (operation != CARD_TRANSFER_IMPORT
				&& operation != CARD_TRANSFER_EXPORT))
			return (FT_ERR_INVALID_ARGUMENT);
		try
		{
			payload.reserve(CARD_TRANSFER_PAYLOAD_SIZE);
			payload.push_back('G');
			payload.push_back('T');
			payload.push_back('C');
			payload.push_back('T');
			append_u32(payload, CARD_TRANSFER_VERSION);
			append_u32(payload, operation);
			append_u64(payload, request_id);
			append_u32(payload, value);
			append_u64(payload, expected_inventory_revision);
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		if (payload.size() != CARD_TRANSFER_PAYLOAD_SIZE)
			return (FT_ERR_INVALID_STATE);
		result = journal.prepare(CARD_TRANSFER_SCOPE, request_id,
			CARD_TRANSFER_TYPE, payload.data(),
			static_cast<uint32_t>(payload.size()), &sequence, &replayed);
		(void)sequence;
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (replayed)
		{
			GameTransactionJournal::Record existing = {};
			result = journal.find(CARD_TRANSFER_SCOPE, request_id, &existing);
			if (result != FT_ERR_SUCCESS)
				return (result);
			if (existing.state != GameTransactionJournal::PREPARED)
				return (FT_ERR_SUCCESS);
		}
		result = save_checkpoint(snapshot_path, character, world_items,
			recipe_catalog, journal, snapshot_writer);
		return (result);
	}

	static int32_t apply_prepared(const char *snapshot_path,
		game_character &character, RegenerationCardSession &card_session,
		const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal,
		const GameTransactionJournal::Record &record,
		card_game_deck_card *card_out,
		RegenerationCardTransferResult *transfer_out,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept
	{
		CardTransferAction action = {};
		game_inventory_snapshot inventory_before = {};
		std::vector<uint8_t> card_session_before;
		std::vector<uint8_t> journal_before;
		int32_t result;
		int32_t journal_result;
		int32_t rollback_result;

		if (record.state != GameTransactionJournal::PREPARED
			|| decode(record, &action) != FT_ERR_SUCCESS
			|| (action.operation == CARD_TRANSFER_IMPORT && card_out == nullptr)
			|| (action.operation == CARD_TRANSFER_EXPORT
				&& transfer_out == nullptr))
			return (FT_ERR_INVALID_ARGUMENT);
		result = character.get_inventory().get_snapshot(inventory_before);
		if (result == FT_ERR_SUCCESS)
			result = card_session.serialize_snapshot(card_session_before);
		if (result == FT_ERR_SUCCESS)
			result = journal.serialize(journal_before);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (action.operation == CARD_TRANSFER_IMPORT)
			result = card_session.import_card_item_to_deck(
				character.get_inventory(), static_cast<int32_t>(action.value),
				action.expected_inventory_revision, record.request_id, card_out);
		else
			result = card_session.export_card_to_inventory(
				character.get_inventory(), action.value,
				action.expected_inventory_revision, record.request_id,
				transfer_out);
		if (result == FT_ERR_SUCCESS)
			journal_result = journal.mark_committed(record.scope_id,
				record.request_id);
		else
			journal_result = journal.mark_aborted(record.scope_id,
				record.request_id);
		if (journal_result != FT_ERR_SUCCESS)
			result = journal_result;
		if (journal_result == FT_ERR_SUCCESS)
			journal_result = save_checkpoint(snapshot_path, character,
				world_items, recipe_catalog, journal, snapshot_writer);
		if (journal_result == FT_ERR_SUCCESS)
			return (result);
		rollback_result = character.get_inventory().restore_snapshot(
			inventory_before);
		if (card_session.destroy() == FT_ERR_SUCCESS)
		{
			const int32_t restore_card_result = card_session.restore_snapshot(
				card_session_before.data(), card_session_before.size());
			if (rollback_result == FT_ERR_SUCCESS
				&& restore_card_result != FT_ERR_SUCCESS)
				rollback_result = restore_card_result;
		}
		else if (rollback_result == FT_ERR_SUCCESS)
			rollback_result = FT_ERR_INVALID_STATE;
		const int32_t restore_journal_result = journal.deserialize(
			journal_before.data(), journal_before.size());
		if (rollback_result == FT_ERR_SUCCESS
			&& restore_journal_result != FT_ERR_SUCCESS)
			rollback_result = restore_journal_result;
		if (card_out != nullptr)
			*card_out = {};
		if (transfer_out != nullptr)
			*transfer_out = {};
		if (rollback_result != FT_ERR_SUCCESS)
			return (rollback_result);
		return (journal_result);
	}

	static int32_t execute(const char *snapshot_path,
		game_character &character, RegenerationCardSession &card_session,
		const WorldItemStore &world_items,
		const MinecraftRecipeCatalog &recipe_catalog,
		GameTransactionJournal &journal, uint32_t operation, uint32_t value,
		uint64_t expected_inventory_revision, uint64_t request_id,
		card_game_deck_card *card_out,
		RegenerationCardTransferResult *transfer_out,
		const GameSessionSnapshotWriter *snapshot_writer) noexcept
	{
		GameTransactionJournal::Record record = {};
		int32_t result;

		if (operation == CARD_TRANSFER_IMPORT && card_out != nullptr)
			*card_out = {};
		if (operation == CARD_TRANSFER_EXPORT && transfer_out != nullptr)
			*transfer_out = {};
		if ((operation == CARD_TRANSFER_IMPORT && card_out == nullptr)
			|| (operation == CARD_TRANSFER_EXPORT && transfer_out == nullptr))
			return (FT_ERR_INVALID_ARGUMENT);
		if (operation == CARD_TRANSFER_IMPORT)
			result = WorldItemCardTransferTransaction::prepare_import(
				snapshot_path, character, world_items, recipe_catalog, journal,
				static_cast<int32_t>(value), expected_inventory_revision,
				request_id, snapshot_writer);
		else
			result = WorldItemCardTransferTransaction::prepare_export(
				snapshot_path, character, world_items, recipe_catalog, journal,
				value, expected_inventory_revision, request_id, snapshot_writer);
		if (result != FT_ERR_SUCCESS)
			return (result);
		result = journal.find(CARD_TRANSFER_SCOPE, request_id, &record);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (record.state == GameTransactionJournal::PREPARED)
			return (apply_prepared(snapshot_path, character, card_session,
				world_items, recipe_catalog, journal, record, card_out,
				transfer_out, snapshot_writer));
		if (record.state != GameTransactionJournal::COMMITTED
			&& record.state != GameTransactionJournal::ABORTED)
			return (FT_ERR_INVALID_STATE);
		CardTransferAction stored_action = {};
		if (decode(record, &stored_action) != FT_ERR_SUCCESS
			|| operation != stored_action.operation
			|| value != stored_action.value
			|| expected_inventory_revision
				!= stored_action.expected_inventory_revision)
			return (FT_ERR_INVALID_ARGUMENT);
		if (operation == CARD_TRANSFER_IMPORT)
			result = card_session.import_card_item_to_deck(
				character.get_inventory(), static_cast<int32_t>(value),
				expected_inventory_revision, request_id, card_out);
		else
			result = card_session.export_card_to_inventory(
				character.get_inventory(), value, expected_inventory_revision,
				request_id, transfer_out);
		const int32_t checkpoint_result = save_checkpoint(snapshot_path,
			character, world_items, recipe_catalog, journal, snapshot_writer);
		return (checkpoint_result == FT_ERR_SUCCESS ? result
			: checkpoint_result);
	}
}

uint64_t WorldItemCardTransferTransaction::journal_scope() noexcept
{
	return (CARD_TRANSFER_SCOPE);
}

int32_t WorldItemCardTransferTransaction::prepare_import(
	const char *snapshot_path, const game_character &character,
	const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal, int32_t inventory_slot,
	uint64_t expected_inventory_revision, uint64_t request_id,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	if (inventory_slot < 0)
		return (FT_ERR_INVALID_ARGUMENT);
	return (prepare(snapshot_path, character, world_items, recipe_catalog,
		journal, CARD_TRANSFER_IMPORT, static_cast<uint32_t>(inventory_slot),
		expected_inventory_revision, request_id, snapshot_writer));
}

int32_t WorldItemCardTransferTransaction::prepare_export(
	const char *snapshot_path, const game_character &character,
	const WorldItemStore &world_items,
	const MinecraftRecipeCatalog &recipe_catalog,
	GameTransactionJournal &journal, uint32_t card_instance_id,
	uint64_t expected_inventory_revision, uint64_t request_id,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	return (prepare(snapshot_path, character, world_items, recipe_catalog,
		journal, CARD_TRANSFER_EXPORT, card_instance_id,
		expected_inventory_revision, request_id, snapshot_writer));
}

int32_t WorldItemCardTransferTransaction::import_to_deck(
	const char *snapshot_path, game_character &character,
	WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
	RegenerationCardSession &card_session, GameTransactionJournal &journal,
	int32_t inventory_slot, uint64_t expected_inventory_revision,
	uint64_t request_id, card_game_deck_card *card_out,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	return (execute(snapshot_path, character, card_session, world_items,
		recipe_catalog, journal, CARD_TRANSFER_IMPORT,
		static_cast<uint32_t>(inventory_slot), expected_inventory_revision,
		request_id, card_out, nullptr, snapshot_writer));
}

int32_t WorldItemCardTransferTransaction::export_to_inventory(
	const char *snapshot_path, game_character &character,
	WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
	RegenerationCardSession &card_session, GameTransactionJournal &journal,
	uint32_t card_instance_id, uint64_t expected_inventory_revision,
	uint64_t request_id, RegenerationCardTransferResult *result_out,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	return (execute(snapshot_path, character, card_session, world_items,
		recipe_catalog, journal, CARD_TRANSFER_EXPORT,
		card_instance_id, expected_inventory_revision, request_id, nullptr,
		result_out, snapshot_writer));
}

int32_t WorldItemCardTransferTransaction::recover_pending(
	const char *snapshot_path, game_character &character,
	WorldItemStore &world_items, MinecraftRecipeCatalog &recipe_catalog,
	RegenerationCardSession &card_session, GameTransactionJournal &journal,
	const GameSessionSnapshotWriter *snapshot_writer) noexcept
{
	std::vector<GameTransactionJournal::Record> pending;
	int32_t result = journal.pending_records(pending);

	if (result != FT_ERR_SUCCESS)
		return (result);
	for (const GameTransactionJournal::Record &record : pending)
	{
		if (record.scope_id != CARD_TRANSFER_SCOPE)
			continue ;
		CardTransferAction action = {};
		if (decode(record, &action) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_ARGUMENT);
		card_game_deck_card card = {};
		RegenerationCardTransferResult transfer = {};
		result = apply_prepared(snapshot_path, character, card_session,
			world_items, recipe_catalog, journal, record,
			&card, &transfer, snapshot_writer);
		if (result != FT_ERR_SUCCESS
			&& result != FT_ERR_INVALID_STATE
			&& result != FT_ERR_NOT_FOUND
			&& result != FT_ERR_FULL
			&& result != FT_ERR_INVALID_ARGUMENT)
			return (result);
	}
	return (FT_ERR_SUCCESS);
}
