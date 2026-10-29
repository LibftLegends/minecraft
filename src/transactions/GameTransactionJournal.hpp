#ifndef GAME_TRANSACTION_JOURNAL_HPP
# define GAME_TRANSACTION_JOURNAL_HPP

# include <cstddef>
# include <cstdint>
# include <vector>

class GameTransactionJournal
{
  public:
	enum State : uint8_t
	{
		PREPARED = 1U,
		COMMITTED = 2U,
		ABORTED = 3U
	};
	static const uint32_t FORMAT_VERSION = 1U;
	static const uint32_t MAXIMUM_RECORDS = 4096U;
	static const uint32_t MAXIMUM_PAYLOAD_BYTES = 65536U;
	static const std::size_t MAXIMUM_SNAPSHOT_BYTES = 64U * 1024U * 1024U;

	struct Record
	{
		uint64_t sequence;
		uint64_t scope_id;
		uint64_t request_id;
		uint32_t transaction_type;
		State state;
		std::vector<uint8_t> payload;
	};

	GameTransactionJournal() noexcept;
	GameTransactionJournal(const GameTransactionJournal &) = delete;
	GameTransactionJournal &operator=(const GameTransactionJournal &) = delete;

	int32_t initialize() noexcept;
	int32_t destroy() noexcept;
	bool is_initialized() const noexcept;
	int32_t replace_from(GameTransactionJournal &staged) noexcept;
	/* Simulation-owner-thread API. Persist a successful prepare with save_file
	 * before applying participant mutations; persist terminal state afterward. */
	int32_t prepare(uint64_t scope_id, uint64_t request_id,
		uint32_t transaction_type, const uint8_t *payload,
		uint32_t payload_size, uint64_t *sequence_out,
		bool *replayed_out) noexcept;
	int32_t mark_committed(uint64_t scope_id, uint64_t request_id) noexcept;
	int32_t mark_aborted(uint64_t scope_id, uint64_t request_id) noexcept;
	/* Explicit operator retry only: reopen a terminal aborted operation after
	 * its external preconditions have been repaired. */
	int32_t retry_aborted(uint64_t scope_id, uint64_t request_id) noexcept;
	int32_t find(uint64_t scope_id, uint64_t request_id,
		Record *record_out) const noexcept;
	/* Returns the highest-sequence retained record for this scope and action
	 * type, including committed records used to restore short-lived policies. */
	int32_t latest(uint64_t scope_id, uint32_t transaction_type,
		Record *record_out) const noexcept;
	int32_t next_request_id(uint64_t scope_id,
		uint64_t *request_id_out) const noexcept;
	/* Retains one committed private sequence record as the durable high-water
	 * mark and removes older terminal records in that scope. */
	int32_t prune_terminal_scope(uint64_t scope_id,
		uint64_t retained_request_id) noexcept;
	int32_t pending_records(std::vector<Record> &records_out) const noexcept;
	int32_t serialize(std::vector<uint8_t> &bytes_out) const noexcept;
	int32_t deserialize(const uint8_t *bytes, std::size_t byte_count) noexcept;
	int32_t save_file(const char *path) const noexcept;
	int32_t load_file(const char *path) noexcept;

  private:
	bool initialized_;
	uint64_t next_sequence_;
	std::vector<Record> records_;
};

#endif
