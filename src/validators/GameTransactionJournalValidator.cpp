#include "../../src/validators/GameTransactionJournalValidator.hpp"
#include "../../src/transactions/GameTransactionJournal.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <cstdio>
#include <vector>

GameTransactionJournalValidator::GameTransactionJournalValidator()
{
}

GameTransactionJournalValidator::GameTransactionJournalValidator(
	const GameTransactionJournalValidator &other) : IValidator(other)
{
	(void)other;
}

GameTransactionJournalValidator::~GameTransactionJournalValidator()
{
}

GameTransactionJournalValidator &GameTransactionJournalValidator::operator=(
	const GameTransactionJournalValidator &other)
{
	(void)other;
	return (*this);
}

int GameTransactionJournalValidator::validate() const
{
	const char *snapshot_path = "game_transaction_journal_validator.tmp";
	const uint8_t first_payload[] = {0x11U, 0x22U, 0x33U};
	const uint8_t changed_payload[] = {0x11U, 0x22U, 0x34U};
	GameTransactionJournal journal;
	GameTransactionJournal restored;
	GameTransactionJournal loaded;
	GameTransactionJournal::Record record = {};
	std::vector<GameTransactionJournal::Record> pending;
	std::vector<uint8_t> bytes;
	std::vector<uint8_t> corrupted;
	uint64_t first_sequence = 0U;
	uint64_t second_sequence = 0U;
	uint64_t third_sequence = 0U;
	uint64_t next_request_id = 0U;
	bool replayed = false;
	int32_t result;

	std::remove(snapshot_path);
	result = journal.initialize();
	if (result == FT_ERR_SUCCESS)
		result = journal.next_request_id(10U, &next_request_id);
	if (result == FT_ERR_SUCCESS && next_request_id != 1U)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = journal.prepare(10U, 1U, 7U, first_payload,
			sizeof(first_payload), &first_sequence, &replayed);
	if (result == FT_ERR_SUCCESS && replayed)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = journal.next_request_id(10U, &next_request_id);
	if (result == FT_ERR_SUCCESS && next_request_id != 2U)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = journal.prepare(10U, 1U, 7U, first_payload,
			sizeof(first_payload), &second_sequence, &replayed);
	if (result == FT_ERR_SUCCESS
		&& (!replayed || second_sequence != first_sequence))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS
		&& journal.prepare(10U, 1U, 7U, changed_payload,
			sizeof(changed_payload), &second_sequence, &replayed)
			!= FT_ERR_INVALID_ARGUMENT)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = journal.prepare(20U, 2U, 8U, nullptr, 0U,
			&second_sequence, &replayed);
	if (result == FT_ERR_SUCCESS)
		result = journal.mark_committed(10U, 1U);
	if (result == FT_ERR_SUCCESS)
		result = journal.mark_committed(10U, 1U);
	if (result == FT_ERR_SUCCESS
		&& journal.mark_aborted(10U, 1U) != FT_ERR_INVALID_OPERATION)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = journal.mark_aborted(20U, 2U);
	if (result == FT_ERR_SUCCESS)
		result = journal.mark_aborted(20U, 2U);
	if (result == FT_ERR_SUCCESS)
		result = journal.prepare(30U, 3U, 9U, first_payload,
			sizeof(first_payload), &third_sequence, &replayed);
	if (result == FT_ERR_SUCCESS)
		result = journal.serialize(bytes);
	if (result == FT_ERR_SUCCESS)
		result = restored.initialize();
	if (result == FT_ERR_SUCCESS)
		result = restored.deserialize(bytes.data(), bytes.size());
	if (result == FT_ERR_SUCCESS)
		result = restored.find(10U, 1U, &record);
	if (result == FT_ERR_SUCCESS && record.state != GameTransactionJournal::COMMITTED)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = restored.find(20U, 2U, &record);
	if (result == FT_ERR_SUCCESS && record.state != GameTransactionJournal::ABORTED)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = restored.pending_records(pending);
	if (result == FT_ERR_SUCCESS
		&& (pending.size() != 1U || pending[0].scope_id != 30U
			|| pending[0].request_id != 3U
			|| pending[0].sequence != third_sequence))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = restored.next_request_id(30U, &next_request_id);
	if (result == FT_ERR_SUCCESS && next_request_id != 4U)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
	{
		try
		{
			corrupted = bytes;
		}
		catch (...)
		{
			result = FT_ERR_NO_MEMORY;
		}
	}
	if (result == FT_ERR_SUCCESS)
	{
		corrupted[corrupted.size() - 1U] ^= 0x01U;
		if (restored.deserialize(corrupted.data(), corrupted.size())
			!= FT_ERR_INVALID_ARGUMENT
			|| restored.find(30U, 3U, &record) != FT_ERR_SUCCESS
			|| record.state != GameTransactionJournal::PREPARED)
			result = FT_ERR_INVALID_STATE;
	}
	if (result == FT_ERR_SUCCESS)
		result = journal.save_file(snapshot_path);
	if (result == FT_ERR_SUCCESS)
		result = loaded.initialize();
	if (result == FT_ERR_SUCCESS)
		result = loaded.load_file(snapshot_path);
	if (result == FT_ERR_SUCCESS)
		result = loaded.find(30U, 3U, &record);
	if (result == FT_ERR_SUCCESS && record.state != GameTransactionJournal::PREPARED)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = loaded.next_request_id(30U, &next_request_id);
	if (result == FT_ERR_SUCCESS && next_request_id != 4U)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = loaded.prepare(40U, 1U, 10U, first_payload,
			sizeof(first_payload), &first_sequence, &replayed);
	if (result == FT_ERR_SUCCESS)
		result = loaded.mark_committed(40U, 1U);
	if (result == FT_ERR_SUCCESS)
		result = loaded.prepare(40U, 2U, 10U, first_payload,
			sizeof(first_payload), &second_sequence, &replayed);
	if (result == FT_ERR_SUCCESS)
		result = loaded.mark_committed(40U, 2U);
	if (result == FT_ERR_SUCCESS)
		result = loaded.prune_terminal_scope(40U, 2U);
	if (result == FT_ERR_SUCCESS)
		result = loaded.next_request_id(40U, &next_request_id);
	if (result == FT_ERR_SUCCESS && next_request_id != 3U)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS
		&& loaded.find(40U, 1U, &record) != FT_ERR_NOT_FOUND)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = loaded.find(40U, 2U, &record);
	if (result == FT_ERR_SUCCESS
		&& record.state != GameTransactionJournal::COMMITTED)
		result = FT_ERR_INVALID_STATE;
	std::remove(snapshot_path);
	if (result != FT_ERR_SUCCESS)
		std::fprintf(stderr, "transaction-journal: failed result=%d\n", result);
	else
		std::fprintf(stderr,
			"transaction-journal: ok replay=stable transitions=checked"
			" pending=restored ids=monotonic checksum=rollback"
			" file=roundtrip\n");
	return (result == FT_ERR_SUCCESS ? 0 : 1);
}
