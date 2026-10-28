#include "GameTransactionJournal.hpp"

#include "../../Libft/Modules/Filesystem/filesystem.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace
{
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

	static uint64_t checksum(const uint8_t *bytes,
		std::size_t byte_count) noexcept
	{
		uint64_t value = 14695981039346656037ULL;
		for (std::size_t index = 0U; index < byte_count; ++index)
		{
			value ^= bytes[index];
			value *= 1099511628211ULL;
		}
		return (value);
	}

	struct Reader
	{
		const uint8_t *bytes;
		std::size_t size;
		std::size_t offset;

		bool read_u32(uint32_t *value) noexcept
		{
			if (value == nullptr || offset > size || size - offset < 4U)
				return (false);
			*value = static_cast<uint32_t>(bytes[offset])
				| (static_cast<uint32_t>(bytes[offset + 1U]) << 8U)
				| (static_cast<uint32_t>(bytes[offset + 2U]) << 16U)
				| (static_cast<uint32_t>(bytes[offset + 3U]) << 24U);
			offset += 4U;
			return (true);
		}

		bool read_u64(uint64_t *value) noexcept
		{
			uint32_t low;
			uint32_t high;

			if (!read_u32(&low) || !read_u32(&high))
				return (false);
			*value = static_cast<uint64_t>(low)
				| (static_cast<uint64_t>(high) << 32U);
			return (true);
		}
	};

	static int32_t read_file(const char *path,
		std::vector<uint8_t> &bytes) noexcept
	{
		FILE *file = std::fopen(path, "rb");
		long file_size;
		int32_t result = FT_ERR_SUCCESS;
		int extra_byte;

		if (file == nullptr)
			return (FT_ERR_IO);
		if (std::fseek(file, 0, SEEK_END) != 0)
			result = FT_ERR_IO;
		file_size = result == FT_ERR_SUCCESS ? std::ftell(file) : -1L;
		if (file_size < 0L)
			result = FT_ERR_IO;
		else if (static_cast<uint64_t>(file_size)
			> GameTransactionJournal::MAXIMUM_SNAPSHOT_BYTES)
			result = FT_ERR_OUT_OF_RANGE;
		if (result == FT_ERR_SUCCESS && std::fseek(file, 0, SEEK_SET) != 0)
			result = FT_ERR_IO;
		if (result == FT_ERR_SUCCESS)
		{
			try
			{
				bytes.resize(static_cast<std::size_t>(file_size));
			}
			catch (const std::bad_alloc &)
			{
				result = FT_ERR_NO_MEMORY;
			}
		}
		if (result == FT_ERR_SUCCESS && !bytes.empty()
			&& std::fread(bytes.data(), 1U, bytes.size(), file) != bytes.size())
			result = FT_ERR_IO;
		if (result == FT_ERR_SUCCESS)
		{
			extra_byte = std::fgetc(file);
			if (extra_byte != EOF || std::ferror(file))
				result = FT_ERR_IO;
		}
		if (std::fclose(file) != 0 && result == FT_ERR_SUCCESS)
			result = FT_ERR_IO;
		return (result);
	}
}

GameTransactionJournal::GameTransactionJournal() noexcept : initialized_(false),
	next_sequence_(1U), records_()
{
}

int32_t GameTransactionJournal::initialize() noexcept
{
	if (this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	this->records_.clear();
	this->next_sequence_ = 1U;
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t GameTransactionJournal::destroy() noexcept
{
	this->records_.clear();
	this->next_sequence_ = 1U;
	this->initialized_ = false;
	return (FT_ERR_SUCCESS);
}

bool GameTransactionJournal::is_initialized() const noexcept
{
	return (this->initialized_);
}

int32_t GameTransactionJournal::replace_from(
	GameTransactionJournal &staged) noexcept
{
	if (!this->initialized_ || !staged.initialized_ || &staged == this)
		return (FT_ERR_INVALID_OPERATION);
	this->records_.swap(staged.records_);
	std::swap(this->next_sequence_, staged.next_sequence_);
	return (FT_ERR_SUCCESS);
}

int32_t GameTransactionJournal::prepare(uint64_t scope_id,
	uint64_t request_id, uint32_t transaction_type, const uint8_t *payload,
	uint32_t payload_size, uint64_t *sequence_out,
	bool *replayed_out) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (scope_id == 0U || request_id == 0U || transaction_type == 0U
		|| payload_size > MAXIMUM_PAYLOAD_BYTES
		|| (payload_size != 0U && payload == nullptr)
		|| sequence_out == nullptr || replayed_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*replayed_out = false;
	for (const Record &record : this->records_)
	{
		if (record.scope_id == scope_id && record.request_id == request_id)
		{
			if (record.transaction_type != transaction_type
				|| record.payload.size() != payload_size
				|| (payload_size != 0U
					&& std::memcmp(record.payload.data(), payload,
						payload_size) != 0))
				return (FT_ERR_INVALID_ARGUMENT);
			*sequence_out = record.sequence;
			*replayed_out = true;
			return (FT_ERR_SUCCESS);
		}
	}
	std::size_t payload_bytes = 0U;
	for (const Record &record : this->records_)
		payload_bytes += record.payload.size();
	const std::size_t record_headers = (this->records_.size() + 1U) * 33U;
	if (record_headers > MAXIMUM_SNAPSHOT_BYTES - 24U
		|| payload_size > MAXIMUM_SNAPSHOT_BYTES - 24U - record_headers
		|| payload_bytes > MAXIMUM_SNAPSHOT_BYTES - 24U - record_headers
			- payload_size)
		return (FT_ERR_OUT_OF_RANGE);
	if (this->records_.size() >= MAXIMUM_RECORDS)
		return (FT_ERR_FULL);
	if (this->next_sequence_ == 0U
		|| this->next_sequence_ == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	try
	{
		Record record = {};
		record.sequence = this->next_sequence_;
		record.scope_id = scope_id;
		record.request_id = request_id;
		record.transaction_type = transaction_type;
		record.state = PREPARED;
		if (payload_size > 0U)
			record.payload.assign(payload, payload + payload_size);
		this->records_.push_back(std::move(record));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	*sequence_out = this->next_sequence_++;
	return (FT_ERR_SUCCESS);
}

int32_t GameTransactionJournal::mark_committed(uint64_t scope_id,
	uint64_t request_id) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (scope_id == 0U || request_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	for (Record &record : this->records_)
	{
		if (record.scope_id != scope_id || record.request_id != request_id)
			continue ;
		if (record.state == COMMITTED)
			return (FT_ERR_SUCCESS);
		if (record.state != PREPARED)
			return (FT_ERR_INVALID_OPERATION);
		record.state = COMMITTED;
		return (FT_ERR_SUCCESS);
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t GameTransactionJournal::mark_aborted(uint64_t scope_id,
	uint64_t request_id) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (scope_id == 0U || request_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	for (Record &record : this->records_)
	{
		if (record.scope_id != scope_id || record.request_id != request_id)
			continue ;
		if (record.state == ABORTED)
			return (FT_ERR_SUCCESS);
		if (record.state != PREPARED)
			return (FT_ERR_INVALID_OPERATION);
		record.state = ABORTED;
		return (FT_ERR_SUCCESS);
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t GameTransactionJournal::retry_aborted(uint64_t scope_id,
	uint64_t request_id) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (scope_id == 0U || request_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	for (Record &record : this->records_)
	{
		if (record.scope_id != scope_id || record.request_id != request_id)
			continue ;
		if (record.state != ABORTED)
			return (FT_ERR_INVALID_OPERATION);
		record.state = PREPARED;
		return (FT_ERR_SUCCESS);
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t GameTransactionJournal::find(uint64_t scope_id,
	uint64_t request_id, Record *record_out) const noexcept
{
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (scope_id == 0U || request_id == 0U || record_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	for (const Record &record : this->records_)
	{
		if (record.scope_id == scope_id && record.request_id == request_id)
		{
			try
			{
				*record_out = record;
			}
			catch (const std::bad_alloc &)
			{
				return (FT_ERR_NO_MEMORY);
			}
			return (FT_ERR_SUCCESS);
		}
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t GameTransactionJournal::latest(uint64_t scope_id,
	uint32_t transaction_type, Record *record_out) const noexcept
{
	const Record *latest_record = nullptr;
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (scope_id == 0U || transaction_type == 0U || record_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	for (const Record &record : this->records_)
	{
		if (record.scope_id != scope_id
			|| record.transaction_type != transaction_type)
			continue ;
		if (latest_record == nullptr || record.sequence > latest_record->sequence)
			latest_record = &record;
	}
	if (latest_record == nullptr)
		return (FT_ERR_NOT_FOUND);
	try
	{
		*record_out = *latest_record;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t GameTransactionJournal::next_request_id(uint64_t scope_id,
	uint64_t *request_id_out) const noexcept
{
	uint64_t maximum_request_id = 0U;

	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (scope_id == 0U || request_id_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	for (const Record &record : this->records_)
		if (record.scope_id == scope_id
			&& record.request_id > maximum_request_id)
			maximum_request_id = record.request_id;
	if (maximum_request_id == std::numeric_limits<uint64_t>::max())
		return (FT_ERR_OUT_OF_RANGE);
	*request_id_out = maximum_request_id + 1U;
	return (FT_ERR_SUCCESS);
}

int32_t GameTransactionJournal::prune_terminal_scope(uint64_t scope_id,
	uint64_t retained_request_id) noexcept
{
	bool retained = false;
	uint64_t maximum_request_id = 0U;
	std::size_t index = 0U;

	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (scope_id == 0U || retained_request_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	while (index < this->records_.size())
	{
		const Record &record = this->records_[index];
		if (record.scope_id == scope_id)
		{
			if (record.request_id > maximum_request_id)
				maximum_request_id = record.request_id;
			if (record.request_id == retained_request_id)
			{
				if (record.state != COMMITTED)
					return (FT_ERR_INVALID_STATE);
				retained = true;
			}
			else if (record.state == PREPARED)
				return (FT_ERR_INVALID_OPERATION);
		}
		++index;
	}
	if (!retained || retained_request_id != maximum_request_id)
		return (FT_ERR_NOT_FOUND);
	index = 0U;
	while (index < this->records_.size())
	{
		if (this->records_[index].scope_id == scope_id
			&& this->records_[index].request_id != retained_request_id)
			this->records_.erase(this->records_.begin()
				+ static_cast<std::ptrdiff_t>(index));
		else
			++index;
	}
	return (FT_ERR_SUCCESS);
}

int32_t GameTransactionJournal::pending_records(
	std::vector<Record> &records_out) const noexcept
{
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	try
	{
		std::vector<Record> staged;
		for (const Record &record : this->records_)
			if (record.state == PREPARED)
				staged.push_back(record);
		records_out.swap(staged);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t GameTransactionJournal::serialize(
	std::vector<uint8_t> &bytes_out) const noexcept
{
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	try
	{
		std::vector<uint8_t> bytes;
		bytes.reserve(24U + this->records_.size() * 40U);
		bytes.push_back('G');
		bytes.push_back('T');
		bytes.push_back('J');
		bytes.push_back('1');
		append_u32(bytes, FORMAT_VERSION);
		append_u32(bytes, static_cast<uint32_t>(this->records_.size()));
		append_u64(bytes, this->next_sequence_);
		for (const Record &record : this->records_)
		{
			append_u64(bytes, record.sequence);
			append_u64(bytes, record.scope_id);
			append_u64(bytes, record.request_id);
			append_u32(bytes, record.transaction_type);
			bytes.push_back(static_cast<uint8_t>(record.state));
			append_u32(bytes, static_cast<uint32_t>(record.payload.size()));
			bytes.insert(bytes.end(), record.payload.begin(),
				record.payload.end());
			if (bytes.size() + sizeof(uint64_t) > MAXIMUM_SNAPSHOT_BYTES)
				return (FT_ERR_OUT_OF_RANGE);
		}
		append_u64(bytes, checksum(bytes.data(), bytes.size()));
		bytes_out.swap(bytes);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t GameTransactionJournal::deserialize(const uint8_t *bytes,
	std::size_t byte_count) noexcept
{
	Reader reader = {bytes, byte_count, 4U};
	std::vector<Record> staged;
	uint32_t version;
	uint32_t count;
	uint64_t staged_next_sequence;
	uint64_t largest_sequence = 0U;
	uint64_t stored_checksum = 0U;
	std::size_t checksum_offset;

	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (bytes == nullptr || byte_count < 28U
		|| byte_count > MAXIMUM_SNAPSHOT_BYTES
		|| bytes[0] != 'G' || bytes[1] != 'T' || bytes[2] != 'J'
		|| bytes[3] != '1')
		return (FT_ERR_INVALID_ARGUMENT);
	checksum_offset = byte_count - sizeof(uint64_t);
	reader.size = checksum_offset;
	if (!reader.read_u32(&version) || !reader.read_u32(&count)
		|| !reader.read_u64(&staged_next_sequence))
		return (FT_ERR_INVALID_ARGUMENT);
	if (version != FORMAT_VERSION || count > MAXIMUM_RECORDS
		|| staged_next_sequence == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (checksum(bytes, checksum_offset) != [&]() noexcept
		{
			Reader checksum_reader = {bytes, byte_count, checksum_offset};
			return (checksum_reader.read_u64(&stored_checksum)
				? stored_checksum : 0U);
		}())
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		staged.reserve(count);
		for (uint32_t index = 0U; index < count; ++index)
		{
			Record record = {};
			uint8_t state;
			uint32_t payload_size;

			if (!reader.read_u64(&record.sequence)
				|| !reader.read_u64(&record.scope_id)
				|| !reader.read_u64(&record.request_id)
				|| !reader.read_u32(&record.transaction_type)
				|| reader.offset >= reader.size)
				return (FT_ERR_INVALID_ARGUMENT);
			state = bytes[reader.offset++];
			if (!reader.read_u32(&payload_size)
				|| payload_size > MAXIMUM_PAYLOAD_BYTES
				|| reader.offset > reader.size
				|| reader.size - reader.offset < payload_size
				|| record.sequence == 0U || record.sequence <= largest_sequence
				|| record.scope_id == 0U || record.request_id == 0U
				|| record.transaction_type == 0U
				|| state < PREPARED || state > ABORTED)
				return (FT_ERR_INVALID_ARGUMENT);
			for (const Record &existing : staged)
				if (existing.scope_id == record.scope_id
					&& existing.request_id == record.request_id)
					return (FT_ERR_INVALID_ARGUMENT);
			record.state = static_cast<State>(state);
			if (payload_size != 0U)
			{
				record.payload.assign(bytes + reader.offset,
					bytes + reader.offset + payload_size);
				reader.offset += payload_size;
			}
			staged.push_back(std::move(record));
			largest_sequence = staged.back().sequence;
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (reader.offset != checksum_offset
		|| staged_next_sequence <= largest_sequence)
		return (FT_ERR_INVALID_ARGUMENT);
	this->records_.swap(staged);
	this->next_sequence_ = staged_next_sequence;
	return (FT_ERR_SUCCESS);
}

int32_t GameTransactionJournal::save_file(const char *path) const noexcept
{
	std::vector<uint8_t> bytes;
	int32_t result;

	if (path == nullptr || path[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	result = this->serialize(bytes);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (filesystem_atomic_write(path,
		reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}

int32_t GameTransactionJournal::load_file(const char *path) noexcept
{
	std::vector<uint8_t> bytes;
	int32_t result;

	if (path == nullptr || path[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	result = read_file(path, bytes);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (this->deserialize(bytes.data(), bytes.size()));
}
