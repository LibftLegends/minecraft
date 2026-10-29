#include "../../src/world/WorldRevisionServer.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include "../../Libft/Modules/Filesystem/filesystem.hpp"
#include <cstdio>
#include <limits>
#include <new>
#include <cstring>
#include <utility>
#include <algorithm>

namespace
{
	static int32_t world_revision_validate_actor_ids(
		const std::vector<uint64_t> &actor_ids) noexcept
	{
		std::vector<uint64_t> sorted;
		try
		{
			sorted = actor_ids;
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		for (uint64_t actor_id : sorted)
		{
			if (actor_id == 0U)
				return (FT_ERR_INVALID_STATE);
		}
		std::sort(sorted.begin(), sorted.end());
		if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
			return (FT_ERR_INVALID_STATE);
		return (FT_ERR_SUCCESS);
	}

	static int32_t world_revision_server_read_checkpoint_file(
		const char *file_path, std::vector<uint8_t> &bytes_out) noexcept
	{
		FILE *file = std::fopen(file_path, "rb");
		if (file == nullptr)
			return (FT_ERR_IO);
		int32_t result = FT_ERR_SUCCESS;
		if (std::fseek(file, 0, SEEK_END) != 0)
			result = FT_ERR_IO;
		const long file_length = result == FT_ERR_SUCCESS
			? std::ftell(file) : -1L;
		if (file_length < 0L)
			result = FT_ERR_IO;
		else if (static_cast<uint64_t>(file_length)
			> 64U * 1024U * 1024U)
			result = FT_ERR_OUT_OF_RANGE;
		if (result == FT_ERR_SUCCESS && std::fseek(file, 0, SEEK_SET) != 0)
			result = FT_ERR_IO;
		if (result == FT_ERR_SUCCESS)
		{
			try
			{
				bytes_out.resize(static_cast<std::size_t>(file_length));
			}
			catch (...)
			{
				result = FT_ERR_NO_MEMORY;
			}
		}
		if (result == FT_ERR_SUCCESS && !bytes_out.empty()
			&& std::fread(bytes_out.data(), 1U, bytes_out.size(), file)
				!= bytes_out.size())
			result = FT_ERR_IO;
		if (result == FT_ERR_SUCCESS && std::fgetc(file) != EOF)
			result = FT_ERR_IO;
		if (std::fclose(file) != 0 && result == FT_ERR_SUCCESS)
			result = FT_ERR_IO;
		return (result);
	}

	static void world_revision_state_append_u32(std::vector<uint8_t> &bytes,
		uint32_t value)
	{
		for (uint32_t shift = 0U; shift < 32U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	static void world_revision_state_append_u64(std::vector<uint8_t> &bytes,
		uint64_t value)
	{
		for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	static uint32_t world_revision_state_read_u32(const uint8_t *bytes,
		std::size_t offset) noexcept
	{
		return (static_cast<uint32_t>(bytes[offset])
			| (static_cast<uint32_t>(bytes[offset + 1U]) << 8U)
			| (static_cast<uint32_t>(bytes[offset + 2U]) << 16U)
			| (static_cast<uint32_t>(bytes[offset + 3U]) << 24U));
	}

	static uint64_t world_revision_state_read_u64(const uint8_t *bytes,
		std::size_t offset) noexcept
	{
		uint64_t value = 0U;
		for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			value |= static_cast<uint64_t>(bytes[offset + shift / 8U]) << shift;
		return (value);
	}

	static uint64_t world_revision_state_checksum(const uint8_t *bytes,
		std::size_t byte_count) noexcept
	{
		uint64_t hash = UINT64_C(14695981039346656037);
		for (std::size_t index = 0U; index < byte_count; ++index)
		{
			hash ^= bytes[index];
			hash *= UINT64_C(1099511628211);
		}
		return (hash);
	}

	int32_t world_revision_parse_u64(const char *text, uint64_t *value_out)
	{
		uint64_t value = 0U;
		uint32_t index = 0U;

		if (text == nullptr || value_out == nullptr || text[0] == '\0')
			return (FT_ERR_INVALID_ARGUMENT);
		while (text[index] != '\0')
		{
			const char digit = text[index];

			if (digit < '0' || digit > '9')
				return (FT_ERR_INVALID_ARGUMENT);
			if (value > (std::numeric_limits<uint64_t>::max()
					- static_cast<uint64_t>(digit - '0')) / 10U)
				return (FT_ERR_OUT_OF_RANGE);
			value = value * 10U + static_cast<uint64_t>(digit - '0');
			++index;
		}
		*value_out = value;
		return (FT_ERR_SUCCESS);
	}

	int32_t world_revision_parse_i32(const char *text, int32_t *value_out)
	{
		uint64_t magnitude = 0U;
		uint32_t index = 0U;
		bool negative = false;
		uint64_t limit;

		if (text == nullptr || value_out == nullptr || text[0] == '\0')
			return (FT_ERR_INVALID_ARGUMENT);
		if (text[index] == '-')
		{
			negative = true;
			++index;
		}
		if (text[index] == '\0')
			return (FT_ERR_INVALID_ARGUMENT);
		limit = negative ? static_cast<uint64_t>(
			std::numeric_limits<int32_t>::max()) + 1U
			: static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
		while (text[index] != '\0')
		{
			const char digit = text[index];

			if (digit < '0' || digit > '9')
				return (FT_ERR_INVALID_ARGUMENT);
			if (magnitude > (limit - static_cast<uint64_t>(digit - '0')) / 10U)
				return (FT_ERR_OUT_OF_RANGE);
			magnitude = magnitude * 10U + static_cast<uint64_t>(digit - '0');
			++index;
		}
		if (negative && magnitude == static_cast<uint64_t>(
				std::numeric_limits<int32_t>::max()) + 1U)
			*value_out = std::numeric_limits<int32_t>::min();
		else if (negative)
			*value_out = -static_cast<int32_t>(magnitude);
		else
			*value_out = static_cast<int32_t>(magnitude);
		return (FT_ERR_SUCCESS);
	}
}

WorldRevisionServer::WorldRevisionServer(World &world) : world_(&world),
	server_(), initialized_(false), authorizer_(nullptr),
	authorizer_user_data_(nullptr), instability_view_authorizer_(nullptr),
	instability_view_authorizer_user_data_(nullptr),
	station_access_authorizer_(nullptr),
	station_access_authorizer_user_data_(nullptr),
	occupancy_provider_(nullptr), occupancy_provider_user_data_(nullptr),
	epoch_provider_(nullptr), epoch_provider_user_data_(nullptr),
	start_committer_(nullptr),
	start_committer_user_data_(nullptr), start_policy_query_(nullptr),
	start_policy_query_user_data_(nullptr), checkpoint_committer_(nullptr),
	checkpoint_committer_user_data_(nullptr), card_play_handler_(nullptr),
	card_play_handler_user_data_(nullptr), card_discard_handler_(nullptr),
	card_discard_handler_user_data_(nullptr), card_draw_handler_(nullptr),
	card_draw_handler_user_data_(nullptr), card_expiry_handler_(nullptr),
	card_expiry_handler_user_data_(nullptr), card_hand_query_(nullptr),
	card_hand_query_user_data_(nullptr), card_choice_handler_(nullptr),
	card_choice_handler_user_data_(nullptr), response_sender_(nullptr),
	response_sender_user_data_(nullptr), draft_actor_id_(0U), draft_loom_id_(0U),
	draft_owned_(false), draft_start_committed_(false)
{
}

int32_t WorldRevisionServer::query_start_cost(uint64_t actor_id,
	uint64_t loom_id, uint64_t request_id,
	uint64_t expected_selection_revision,
	uint64_t expected_policy_revision, uint32_t opening_card_count,
	WorldRevisionCostQuote *quote_out) noexcept
{
	std::tuple<uint64_t, uint64_t, uint64_t> request_key;
	std::map<std::tuple<uint64_t, uint64_t, uint64_t>, CachedRequest>::iterator
		cached;
	std::string payload;
	WorldRevisionCostQuote quote = {};
	int32_t result;

	if (!this->initialized_ || this->world_ == nullptr || quote_out == nullptr
		|| actor_id == 0U || loom_id == 0U || request_id == 0U
		|| opening_card_count < 4U || opening_card_count > 7U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->draft_owned_ || actor_id != this->draft_actor_id_
		|| loom_id != this->draft_loom_id_)
		return (FT_ERR_PERMISSION_DENIED);
	try
	{
		payload = "quote|" + std::to_string(expected_selection_revision)
			+ "|" + std::to_string(expected_policy_revision) + "|"
			+ std::to_string(opening_card_count);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	request_key = std::make_tuple(actor_id, loom_id, request_id);
	cached = this->request_cache_.find(request_key);
	if (cached != this->request_cache_.end())
	{
		if (cached->second.payload != payload)
			return (FT_ERR_INVALID_ARGUMENT);
		if (!cached->second.completed)
			return (FT_ERR_INVALID_STATE);
		if (cached->second.result != FT_ERR_SUCCESS)
			return (cached->second.result);
		if (!cached->second.has_quote)
			return (FT_ERR_INVALID_STATE);
		*quote_out = cached->second.quote;
		return (this->commit_checkpoint());
	}
	if (this->request_cache_.size() >= WorldRevisionServer::REQUEST_CACHE_LIMIT)
		return (FT_ERR_FULL);
	try
	{
		CachedRequest staged = {};
		staged.payload = payload;
		staged.result = FT_ERR_INVALID_STATE;
		staged.completed = false;
		staged.has_quote = false;
		cached = this->request_cache_.emplace(request_key,
			std::move(staged)).first;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	result = this->refresh_occupied_chunks();
	if (result != FT_ERR_SUCCESS)
	{
		this->request_cache_.erase(cached);
		return (result);
	}
	result = this->world_->query_revision_start_cost(
		expected_selection_revision, expected_policy_revision,
		opening_card_count, &quote);
	cached->second.result = result;
	cached->second.completed = true;
	if (result == FT_ERR_SUCCESS)
	{
		cached->second.quote = quote;
		cached->second.has_quote = true;
		*quote_out = quote;
	}
	const int32_t checkpoint_result = this->commit_checkpoint();
	if (checkpoint_result != FT_ERR_SUCCESS)
		return (checkpoint_result);
	return (result);
}

int32_t WorldRevisionServer::set_occupancy_provider(
	WorldRevisionServerOccupancyProvider provider, void *user_data) noexcept
{
	if (this->initialized_ || provider == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->occupancy_provider_ = provider;
	this->occupancy_provider_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::set_epoch_provider(
	WorldRevisionServerEpochProvider provider, void *user_data) noexcept
{
	if (this->initialized_ || provider == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->epoch_provider_ = provider;
	this->epoch_provider_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::refresh_occupied_chunks()
{
	WorldRevisionOccupancySample sample = {};
	uint64_t selection_revision = 0U;
	uint64_t current_epoch_ms = 0U;
	int32_t result;

	if (!this->initialized_ || this->world_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	if (this->occupancy_provider_ == nullptr || this->epoch_provider_ == nullptr)
		return (FT_ERR_PERMISSION_DENIED);
	result = this->occupancy_provider_(sample,
		this->occupancy_provider_user_data_);
	if (result == FT_ERR_SUCCESS
		&& (sample.sampled_epoch_ms == 0U
			|| sample.active_player_count
				> WorldRevisionManager::MAXIMUM_OCCUPIED_CHUNKS
			|| sample.active_player_count != sample.player_chunks.size()
			|| sample.active_player_count != sample.player_actor_ids.size()))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = world_revision_validate_actor_ids(sample.player_actor_ids);
	if (result == FT_ERR_SUCCESS)
		result = this->epoch_provider_(&current_epoch_ms,
			this->epoch_provider_user_data_);
	if (result == FT_ERR_SUCCESS
		&& (current_epoch_ms < sample.sampled_epoch_ms
			|| current_epoch_ms - sample.sampled_epoch_ms
				> WORLD_REVISION_OCCUPANCY_MAX_AGE_MS))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = this->world_->set_revision_occupied_chunks(sample.player_chunks,
			&selection_revision);
	(void)selection_revision;
	return (result);
}

WorldRevisionServer::WorldRevisionServer(const WorldRevisionServer &other) : world_(nullptr),
	server_(), initialized_(false), authorizer_(nullptr),
	authorizer_user_data_(nullptr), instability_view_authorizer_(nullptr),
	instability_view_authorizer_user_data_(nullptr),
	station_access_authorizer_(nullptr),
	station_access_authorizer_user_data_(nullptr),
	occupancy_provider_(nullptr), occupancy_provider_user_data_(nullptr),
	epoch_provider_(nullptr), epoch_provider_user_data_(nullptr),
	start_committer_(nullptr),
	start_committer_user_data_(nullptr), start_policy_query_(nullptr),
	start_policy_query_user_data_(nullptr), checkpoint_committer_(nullptr),
	checkpoint_committer_user_data_(nullptr), card_play_handler_(nullptr),
	card_play_handler_user_data_(nullptr), card_discard_handler_(nullptr),
	card_discard_handler_user_data_(nullptr), card_draw_handler_(nullptr),
	card_draw_handler_user_data_(nullptr), card_expiry_handler_(nullptr),
	card_expiry_handler_user_data_(nullptr), card_hand_query_(nullptr),
	card_hand_query_user_data_(nullptr), card_choice_handler_(nullptr),
	card_choice_handler_user_data_(nullptr), response_sender_(nullptr),
	response_sender_user_data_(nullptr), draft_actor_id_(0U), draft_loom_id_(0U),
	draft_owned_(false), draft_start_committed_(false)
{
	(void)other;
}

WorldRevisionServer::~WorldRevisionServer()
{
	(void)this->destroy();
}

WorldRevisionServer &WorldRevisionServer::operator=(const WorldRevisionServer &other)
{
	(void)other;
	return (*this);
}

int32_t WorldRevisionServer::set_authorizer(
	WorldRevisionServerAuthorizer authorizer, void *user_data) noexcept
{
	if (this->initialized_ || authorizer == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->authorizer_ = authorizer;
	this->authorizer_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::set_instability_view_authorizer(
	WorldRevisionServerInstabilityViewAuthorizer authorizer,
	void *user_data) noexcept
{
	if (this->initialized_ || authorizer == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->instability_view_authorizer_ = authorizer;
	this->instability_view_authorizer_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::set_station_access_authorizer(
	WorldRevisionServerStationAccessAuthorizer authorizer,
	void *user_data) noexcept
{
	if (this->initialized_ || authorizer == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->station_access_authorizer_ = authorizer;
	this->station_access_authorizer_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::authorize_station_access(uint64_t actor_id,
	uint64_t loom_id, const char *message)
{
	int32_t station_chunk_x;
	int32_t station_chunk_z;
	bool has_station_block;
	int32_t station_block_x;
	int32_t station_block_y;
	int32_t station_block_z;
	uint32_t station_block_id;
	int32_t result;

	if (this->world_ == nullptr || message == nullptr
		|| this->station_access_authorizer_ == nullptr)
		return (FT_ERR_PERMISSION_DENIED);
	result = this->world_->get_loom_station_chunk(&station_chunk_x,
		&station_chunk_z);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->world_->get_loom_station_block(&has_station_block,
		&station_block_x, &station_block_y, &station_block_z,
		&station_block_id);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (has_station_block)
	{
		uint32_t current_station_block_id = 0U;

		if (!this->world_->block_id_at(station_block_x, station_block_y,
				station_block_z, &current_station_block_id))
		{
			(void)this->world_->invalidate_loom_station();
			return (FT_ERR_NOT_FOUND);
		}
		if (current_station_block_id != station_block_id)
		{
			(void)this->world_->invalidate_loom_station();
			return (FT_ERR_PERMISSION_DENIED);
		}
	}
	return (this->station_access_authorizer_(actor_id, loom_id,
		station_chunk_x, station_chunk_z, has_station_block,
		station_block_x, station_block_y, station_block_z, station_block_id,
		message,
		this->station_access_authorizer_user_data_));
}

int32_t WorldRevisionServer::set_start_committer(
	WorldRevisionServerStartCommitter committer, void *user_data) noexcept
{
	if (this->initialized_ || committer == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->start_committer_ = committer;
	this->start_committer_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::set_start_policy_query(
	WorldRevisionServerStartPolicyQuery query, void *user_data) noexcept
{
	if (this->initialized_ || query == nullptr || user_data == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->start_policy_query_ = query;
	this->start_policy_query_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::set_checkpoint_committer(
	WorldRevisionServerCheckpointCommitter committer,
	void *user_data) noexcept
{
	if (this->initialized_ || committer == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->checkpoint_committer_ = committer;
	this->checkpoint_committer_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::set_card_play_handler(
	WorldRevisionServerCardPlayHandler handler, void *user_data) noexcept
{
	if (this->initialized_ || handler == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->card_play_handler_ = handler;
	this->card_play_handler_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::set_card_discard_handler(
	WorldRevisionServerCardDiscardHandler handler, void *user_data) noexcept
{
	if (this->initialized_ || handler == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->card_discard_handler_ = handler;
	this->card_discard_handler_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::set_card_draw_handler(
	WorldRevisionServerCardDrawHandler handler, void *user_data) noexcept
{
	if (this->initialized_ || (handler == nullptr && user_data != nullptr))
		return (FT_ERR_INVALID_ARGUMENT);
	this->card_draw_handler_ = handler;
	this->card_draw_handler_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::set_card_expiry_handler(
	WorldRevisionServerCardExpiryHandler handler, void *user_data) noexcept
{
	if (this->initialized_ || (handler == nullptr && user_data != nullptr))
		return (FT_ERR_INVALID_ARGUMENT);
	this->card_expiry_handler_ = handler;
	this->card_expiry_handler_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::set_card_hand_query(
	WorldRevisionServerCardHandQuery query, void *user_data) noexcept
{
	if (this->initialized_ || (query == nullptr && user_data != nullptr))
		return (FT_ERR_INVALID_ARGUMENT);
	this->card_hand_query_ = query;
	this->card_hand_query_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::set_card_choice_handler(
	WorldRevisionServerCardChoiceHandler handler, void *user_data) noexcept
{
	if (this->initialized_ || (handler == nullptr && user_data != nullptr))
		return (FT_ERR_INVALID_ARGUMENT);
	this->card_choice_handler_ = handler;
	this->card_choice_handler_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::commit_checkpoint() noexcept
{
	if (this->checkpoint_committer_ == nullptr)
		return (FT_ERR_SUCCESS);
	std::vector<uint8_t> checkpoint;
	int32_t result = this->serialize_checkpoint(checkpoint);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (this->checkpoint_committer_(checkpoint.data(), checkpoint.size(),
		this->checkpoint_committer_user_data_));
}

int32_t WorldRevisionServer::initialize()
{
	int32_t	error_code;

	if (this->world_ == nullptr || this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (this->authorizer_ == nullptr
		|| this->station_access_authorizer_ == nullptr
		|| this->occupancy_provider_ == nullptr
		|| this->epoch_provider_ == nullptr
		|| this->start_committer_ == nullptr
		|| this->checkpoint_committer_ == nullptr)
		return (FT_ERR_PERMISSION_DENIED);
	error_code = this->server_.initialize();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->server_.set_message_callback(&WorldRevisionServer::handle_message,
		this);
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::serialize_draft_state(
	std::vector<uint8_t> &bytes_out) const noexcept
{
	std::vector<uint8_t> staged;
	const World::WorldRevision revision = this->world_ == nullptr
		? World::WorldRevision{} : this->world_->world_revision();
	if (this->world_ == nullptr || (this->draft_owned_
		&& (this->draft_actor_id_ == 0U || this->draft_loom_id_ == 0U))
		|| (!this->draft_owned_ && (this->draft_actor_id_ != 0U
			|| this->draft_loom_id_ != 0U || this->draft_start_committed_)))
		return (FT_ERR_INVALID_STATE);
	try
	{
		staged.reserve(48U);
		staged.insert(staged.end(), {'W', 'R', 'S', '1'});
		world_revision_state_append_u32(staged, 1U);
		world_revision_state_append_u32(staged,
			(this->draft_owned_ ? 1U : 0U)
			| (this->draft_start_committed_ ? 2U : 0U));
		world_revision_state_append_u64(staged, this->draft_actor_id_);
		world_revision_state_append_u64(staged, this->draft_loom_id_);
		world_revision_state_append_u32(staged, revision.identifier);
		world_revision_state_append_u64(staged, revision.selection_revision);
		world_revision_state_append_u64(staged,
			world_revision_state_checksum(staged.data(), staged.size()));
		bytes_out.swap(staged);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::restore_draft_state(const uint8_t *bytes,
	std::size_t byte_count) noexcept
{
	uint64_t stored_checksum;
	uint32_t flags;
	uint64_t actor_id;
	uint64_t loom_id;
	if (this->initialized_ || this->world_ == nullptr
		|| (bytes == nullptr && byte_count != 0U))
		return (FT_ERR_INVALID_STATE);
	if (byte_count != 48U || std::memcmp(bytes, "WRS1", 4U) != 0
		|| world_revision_state_read_u32(bytes, 4U) != 1U)
		return (FT_ERR_INVALID_ARGUMENT);
	flags = world_revision_state_read_u32(bytes, 8U);
	actor_id = world_revision_state_read_u64(bytes, 12U);
	loom_id = world_revision_state_read_u64(bytes, 20U);
	stored_checksum = world_revision_state_read_u64(bytes, 40U);
	if ((flags & ~3U) != 0U
		|| stored_checksum != world_revision_state_checksum(bytes, 40U)
		|| (((flags & 1U) != 0U) != (actor_id != 0U && loom_id != 0U))
		|| ((flags & 2U) != 0U && (flags & 1U) == 0U)
		|| ((actor_id == 0U) != (loom_id == 0U)))
		return (FT_ERR_INVALID_ARGUMENT);
	const World::WorldRevision revision = this->world_->world_revision();
	if (revision.identifier != world_revision_state_read_u32(bytes, 28U)
		|| revision.selection_revision
			!= world_revision_state_read_u64(bytes, 32U))
		return (FT_ERR_INVALID_STATE);
	this->draft_owned_ = (flags & 1U) != 0U;
	this->draft_start_committed_ = (flags & 2U) != 0U;
	this->draft_actor_id_ = actor_id;
	this->draft_loom_id_ = loom_id;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::serialize_request_cache(
	std::vector<uint8_t> &bytes_out) const noexcept
{
	std::vector<uint8_t> staged;
	uint32_t completed_request_count = 0U;
	if (this->request_cache_.size() > REQUEST_CACHE_LIMIT
		|| this->wire_request_cache_.size() > REQUEST_CACHE_LIMIT)
		return (FT_ERR_FULL);
	for (const auto &entry : this->request_cache_)
		if (entry.second.completed)
			++completed_request_count;
	try
	{
		staged.reserve(32U + (completed_request_count
			+ this->wire_request_cache_.size()) * 128U);
		staged.insert(staged.end(), {'W', 'R', 'C', '1'});
		world_revision_state_append_u32(staged, 6U);
		world_revision_state_append_u32(staged,
			completed_request_count);
		world_revision_state_append_u32(staged,
			static_cast<uint32_t>(this->wire_request_cache_.size()));
		for (const auto &entry : this->request_cache_)
		{
			if (!entry.second.completed)
				continue ;
			if (entry.second.payload.size() > 4096U)
				return (FT_ERR_OUT_OF_RANGE);
			world_revision_state_append_u64(staged, std::get<0>(entry.first));
			world_revision_state_append_u64(staged, std::get<1>(entry.first));
			world_revision_state_append_u64(staged, std::get<2>(entry.first));
			world_revision_state_append_u32(staged,
				static_cast<uint32_t>(entry.second.payload.size()));
			staged.insert(staged.end(), entry.second.payload.begin(),
				entry.second.payload.end());
			uint32_t flags = (entry.second.completed ? 1U : 0U)
				| (entry.second.has_quote ? 2U : 0U)
				| (entry.second.has_instability_view ? 4U : 0U)
				| (entry.second.has_revision_state ? 8U : 0U)
				| (entry.second.has_card_play_result ? 16U : 0U)
				| (entry.second.has_card_discard_result ? 32U : 0U)
				| (entry.second.has_card_draw_result ? 64U : 0U)
				| (entry.second.has_card_expiry_result ? 128U : 0U)
				| (entry.second.has_card_choice_result ? 256U : 0U);
			world_revision_state_append_u32(staged, flags);
			world_revision_state_append_u32(staged,
				static_cast<uint32_t>(entry.second.result));
			const WorldRevisionCostQuote &quote = entry.second.quote;
			world_revision_state_append_u32(staged, quote.revision_identifier);
			world_revision_state_append_u64(staged, quote.selection_revision);
			world_revision_state_append_u64(staged, quote.cost_policy_revision);
			world_revision_state_append_u32(staged, quote.selected_paid_chunks);
			world_revision_state_append_u32(staged, quote.selected_bonus_chunks);
			world_revision_state_append_u32(staged, quote.opening_card_count);
			world_revision_state_append_u32(staged, quote.currency_item_id);
			world_revision_state_append_u64(staged, quote.chunk_currency_cost);
			world_revision_state_append_u64(staged,
				quote.opening_card_currency_cost);
			world_revision_state_append_u64(staged, quote.total_currency_cost);
			const WorldChunkInstabilityPublicView &view =
				entry.second.instability_view;
			world_revision_state_append_u32(staged,
				static_cast<uint32_t>(view.chunk_x));
			world_revision_state_append_u32(staged,
				static_cast<uint32_t>(view.chunk_z));
			world_revision_state_append_u32(staged, view.reward_band);
			world_revision_state_append_u32(staged, view.danger_band);
			world_revision_state_append_u32(staged,
				entry.second.revision_identifier);
			world_revision_state_append_u64(staged,
				entry.second.selection_revision);
			world_revision_state_append_u64(staged,
				entry.second.cost_policy_revision);
			world_revision_state_append_u32(staged,
				entry.second.selected_count);
			if (entry.second.has_card_play_result)
			{
				const RegenerationPolicyIntent &card =
					entry.second.card_play_result;
				world_revision_state_append_u64(staged, card.request_id);
				world_revision_state_append_u64(staged,
					card.host_intent_sequence);
				world_revision_state_append_u32(staged, card.content_version);
				world_revision_state_append_u32(staged,
					card.card_definition_id);
				world_revision_state_append_u32(staged,
					card.card_instance_id);
				world_revision_state_append_u32(staged,
					static_cast<uint32_t>(card.effect_kind));
				world_revision_state_append_u32(staged,
					static_cast<uint32_t>(card.choice.kind));
				world_revision_state_append_u32(staged,
					card.choice.value_primary);
				world_revision_state_append_u32(staged,
					card.choice.value_secondary);
				world_revision_state_append_u32(staged,
					card.no_effect_choice_mask);
				world_revision_state_append_u32(staged,
					card.private_result);
				world_revision_state_append_u32(staged,
					card.private_ordinal);
				world_revision_state_append_u32(staged,
					card.replayed ? 1U : 0U);
			}
			if (entry.second.has_card_discard_result)
			{
				const RegenerationCardActionResult &card =
					entry.second.card_discard_result;
				world_revision_state_append_u64(staged, card.session_id);
				world_revision_state_append_u64(staged, card.request_id);
				world_revision_state_append_u64(staged,
					card.command_sequence);
				world_revision_state_append_u32(staged, card.count);
				world_revision_state_append_u32(staged,
					card.replayed ? 1U : 0U);
			}
			if (entry.second.has_card_draw_result)
			{
				const RegenerationCardActionResult &card =
					entry.second.card_draw_result;
				world_revision_state_append_u64(staged, card.session_id);
				world_revision_state_append_u64(staged, card.request_id);
				world_revision_state_append_u64(staged,
					card.command_sequence);
				world_revision_state_append_u32(staged, card.count);
				world_revision_state_append_u32(staged,
					card.replayed ? 1U : 0U);
			}
			if (entry.second.has_card_expiry_result)
			{
				world_revision_state_append_u32(staged,
					entry.second.card_expiry_resolved_count);
				world_revision_state_append_u32(staged,
					entry.second.card_expiry_replayed ? 1U : 0U);
			}
			if (entry.second.has_card_choice_result)
			{
				const RegenerationCardActionResult &card =
					entry.second.card_choice_result;
				world_revision_state_append_u64(staged, card.session_id);
				world_revision_state_append_u64(staged, card.request_id);
				world_revision_state_append_u64(staged,
					card.command_sequence);
				world_revision_state_append_u32(staged, card.count);
				world_revision_state_append_u32(staged,
					card.replayed ? 1U : 0U);
			}
		}
		for (const auto &entry : this->wire_request_cache_)
		{
			if (entry.second.size() > 4096U)
				return (FT_ERR_OUT_OF_RANGE);
			world_revision_state_append_u64(staged, std::get<0>(entry.first));
			world_revision_state_append_u64(staged, std::get<1>(entry.first));
			world_revision_state_append_u64(staged, std::get<2>(entry.first));
			world_revision_state_append_u32(staged,
				static_cast<uint32_t>(entry.second.size()));
			staged.insert(staged.end(), entry.second.begin(), entry.second.end());
		}
		if (staged.size() > 64U * 1024U * 1024U - 8U)
			return (FT_ERR_FULL);
		world_revision_state_append_u64(staged,
			world_revision_state_checksum(staged.data(), staged.size()));
		bytes_out.swap(staged);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::restore_request_cache(const uint8_t *bytes,
	std::size_t byte_count) noexcept
{
	std::map<std::tuple<uint64_t, uint64_t, uint64_t>, CachedRequest> staged;
	std::map<std::tuple<uint64_t, uint64_t, uint64_t>, std::string>
		staged_wire;
	std::size_t offset = 16U;
	uint32_t request_count;
	uint32_t wire_count;
	uint64_t stored_checksum;
	auto read_u32 = [&](uint32_t *value) -> bool
	{
		if (value == nullptr || offset > byte_count || byte_count - offset < 4U)
			return (false);
		*value = world_revision_state_read_u32(bytes, offset);
		offset += 4U;
		return (true);
	};
	auto read_u64 = [&](uint64_t *value) -> bool
	{
		if (value == nullptr || offset > byte_count || byte_count - offset < 8U)
			return (false);
		*value = world_revision_state_read_u64(bytes, offset);
		offset += 8U;
		return (true);
	};
	auto read_string = [&](std::string *value) -> bool
	{
		uint32_t length;
		if (value == nullptr || !read_u32(&length) || length > 4096U
			|| offset > byte_count || byte_count - offset < length)
			return (false);
		value->assign(reinterpret_cast<const char *>(bytes + offset), length);
		offset += length;
		return (true);
	};
	if (this->initialized_ || (bytes == nullptr && byte_count != 0U))
		return (FT_ERR_INVALID_STATE);
	if (byte_count < 24U || byte_count > 64U * 1024U * 1024U
		|| std::memcmp(bytes, "WRC1", 4U) != 0
		|| (world_revision_state_read_u32(bytes, 4U) != 1U
			&& world_revision_state_read_u32(bytes, 4U) != 2U
			&& world_revision_state_read_u32(bytes, 4U) != 3U
			&& world_revision_state_read_u32(bytes, 4U) != 4U
			&& world_revision_state_read_u32(bytes, 4U) != 5U
			&& world_revision_state_read_u32(bytes, 4U) != 6U))
		return (FT_ERR_INVALID_ARGUMENT);
	const uint32_t version = world_revision_state_read_u32(bytes, 4U);
	request_count = world_revision_state_read_u32(bytes, 8U);
	wire_count = world_revision_state_read_u32(bytes, 12U);
	if (request_count > REQUEST_CACHE_LIMIT || wire_count > REQUEST_CACHE_LIMIT)
		return (FT_ERR_INVALID_ARGUMENT);
	stored_checksum = world_revision_state_read_u64(bytes, byte_count - 8U);
	if (stored_checksum != world_revision_state_checksum(bytes, byte_count - 8U))
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		for (uint32_t index = 0U; index < request_count; ++index)
		{
			uint64_t actor_id, loom_id, request_id;
			CachedRequest entry = {};
			uint32_t flags, result_code, value;
			if (!read_u64(&actor_id) || !read_u64(&loom_id)
				|| !read_u64(&request_id) || actor_id == 0U || loom_id == 0U
				|| request_id == 0U || !read_string(&entry.payload)
				|| !read_u32(&flags)
				|| (flags & (version == 1U ? ~15U
					: (version == 2U ? ~31U
						: (version == 3U ? ~63U
							: (version == 4U ? ~127U
								: (version == 5U ? ~255U : ~511U)))))) != 0U
				|| !read_u32(&result_code))
				return (FT_ERR_INVALID_ARGUMENT);
			entry.completed = (flags & 1U) != 0U;
			entry.has_quote = (flags & 2U) != 0U;
			entry.has_instability_view = (flags & 4U) != 0U;
			entry.has_revision_state = (flags & 8U) != 0U;
			entry.has_card_play_result = (flags & 16U) != 0U;
			entry.has_card_discard_result = (flags & 32U) != 0U;
			entry.has_card_draw_result = (flags & 64U) != 0U;
			entry.has_card_expiry_result = (flags & 128U) != 0U;
			entry.has_card_choice_result = (flags & 256U) != 0U;
			if (version == 1U && entry.has_card_play_result)
				return (FT_ERR_INVALID_ARGUMENT);
			if (version < 3U && entry.has_card_discard_result)
				return (FT_ERR_INVALID_ARGUMENT);
			if (version < 4U && entry.has_card_draw_result)
				return (FT_ERR_INVALID_ARGUMENT);
			if (version < 5U && entry.has_card_expiry_result)
				return (FT_ERR_INVALID_ARGUMENT);
			if (version < 6U && entry.has_card_choice_result)
				return (FT_ERR_INVALID_ARGUMENT);
			entry.result = static_cast<int32_t>(result_code);
			if ((entry.has_card_play_result || entry.has_card_discard_result
					|| entry.has_card_draw_result
					|| entry.has_card_expiry_result
					|| entry.has_card_choice_result)
				&& entry.result != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_ARGUMENT);
			if (!entry.completed)
				return (FT_ERR_INVALID_ARGUMENT);
			WorldRevisionCostQuote &quote = entry.quote;
			if (!read_u32(&quote.revision_identifier)
				|| !read_u64(&quote.selection_revision)
				|| !read_u64(&quote.cost_policy_revision)
				|| !read_u32(&quote.selected_paid_chunks)
				|| !read_u32(&quote.selected_bonus_chunks)
				|| !read_u32(&quote.opening_card_count)
				|| !read_u32(&quote.currency_item_id)
				|| !read_u64(&quote.chunk_currency_cost)
				|| !read_u64(&quote.opening_card_currency_cost)
				|| !read_u64(&quote.total_currency_cost)
				|| !read_u32(&value))
				return (FT_ERR_INVALID_ARGUMENT);
			entry.instability_view.chunk_x = static_cast<int32_t>(value);
			if (!read_u32(&value))
				return (FT_ERR_INVALID_ARGUMENT);
			entry.instability_view.chunk_z = static_cast<int32_t>(value);
			if (!read_u32(&entry.instability_view.reward_band)
				|| !read_u32(&entry.instability_view.danger_band)
				|| !read_u32(&entry.revision_identifier)
				|| !read_u64(&entry.selection_revision)
				|| !read_u64(&entry.cost_policy_revision)
				|| !read_u32(&entry.selected_count))
				return (FT_ERR_INVALID_ARGUMENT);
			if (entry.has_card_play_result)
			{
				RegenerationPolicyIntent &card = entry.card_play_result;
				uint32_t effect_kind, choice_kind, replayed;
				if (!read_u64(&card.request_id)
					|| !read_u64(&card.host_intent_sequence)
					|| !read_u32(&card.content_version)
					|| !read_u32(&card.card_definition_id)
					|| !read_u32(&card.card_instance_id)
					|| !read_u32(&effect_kind)
					|| !read_u32(&choice_kind)
					|| !read_u32(&card.choice.value_primary)
					|| !read_u32(&card.choice.value_secondary)
					|| !read_u32(&card.no_effect_choice_mask)
					|| !read_u32(&card.private_result)
					|| !read_u32(&card.private_ordinal)
					|| !read_u32(&replayed)
					|| card.request_id != request_id
					|| card.card_instance_id == 0U
					|| card.content_version
						!= RegenerationCardSession::CONTENT_VERSION
					|| !RegenerationCardSession::is_supported_card_definition(
						card.card_definition_id)
					|| effect_kind < REGENERATION_CARD_EFFECT_BIOME_EXCLUSION
					|| effect_kind > REGENERATION_CARD_EFFECT_SPIDER_NEST_SEAL
					|| effect_kind != static_cast<uint32_t>(
						RegenerationCardSession::effect_kind_for_card(
							card.card_definition_id))
					|| choice_kind > REGENERATION_CARD_CHOICE_TARGET_CHUNK
					|| (card.no_effect_choice_mask & ~3U) != 0U
					|| (card.card_definition_id
						!= RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID
						&& card.no_effect_choice_mask != 0U)
					|| replayed > 1U)
					return (FT_ERR_INVALID_ARGUMENT);
				card.effect_kind = static_cast<RegenerationCardEffectKind>(
					effect_kind);
				card.choice.kind = static_cast<RegenerationCardChoiceKind>(
					choice_kind);
				card.replayed = replayed != 0U;
			}
			if (entry.has_card_discard_result)
			{
				RegenerationCardActionResult &card =
					entry.card_discard_result;
				uint32_t replayed;
				if (!read_u64(&card.session_id)
					|| !read_u64(&card.request_id)
					|| !read_u64(&card.command_sequence)
					|| !read_u32(&card.count)
					|| !read_u32(&replayed)
					|| card.request_id != request_id
					|| card.command_sequence == 0U || card.count != 1U
					|| replayed > 1U)
					return (FT_ERR_INVALID_ARGUMENT);
				card.replayed = replayed != 0U;
			}
			if (entry.has_card_draw_result)
			{
				RegenerationCardActionResult &card = entry.card_draw_result;
				uint32_t replayed;
				if (!read_u64(&card.session_id)
					|| !read_u64(&card.request_id)
					|| !read_u64(&card.command_sequence)
					|| !read_u32(&card.count) || !read_u32(&replayed)
					|| card.request_id != request_id
					|| card.command_sequence == 0U || card.count == 0U
					|| replayed > 1U)
					return (FT_ERR_INVALID_ARGUMENT);
				card.replayed = replayed != 0U;
			}
			if (entry.has_card_expiry_result)
			{
				uint32_t replayed;
				if (!read_u32(&entry.card_expiry_resolved_count)
					|| entry.card_expiry_resolved_count > 4096U
					|| !read_u32(&replayed) || replayed > 1U)
					return (FT_ERR_INVALID_ARGUMENT);
				entry.card_expiry_replayed = replayed != 0U;
			}
			if (entry.has_card_choice_result)
			{
				RegenerationCardActionResult &card =
					entry.card_choice_result;
				uint32_t replayed;
				if (!read_u64(&card.session_id)
					|| !read_u64(&card.request_id)
					|| !read_u64(&card.command_sequence)
					|| !read_u32(&card.count) || !read_u32(&replayed)
					|| card.request_id != request_id
					|| card.command_sequence == 0U || card.count != 1U
					|| replayed > 1U)
					return (FT_ERR_INVALID_ARGUMENT);
				card.replayed = replayed != 0U;
			}
			const auto key = std::make_tuple(actor_id, loom_id, request_id);
			if (!staged.emplace(key, std::move(entry)).second)
				return (FT_ERR_ALREADY_EXISTS);
		}
		for (uint32_t index = 0U; index < wire_count; ++index)
		{
			uint64_t actor_id, loom_id, request_id;
			std::string message;
			if (!read_u64(&actor_id) || !read_u64(&loom_id)
				|| !read_u64(&request_id) || actor_id == 0U || loom_id == 0U
				|| request_id == 0U || !read_string(&message))
				return (FT_ERR_INVALID_ARGUMENT);
			if (!staged_wire.emplace(std::make_tuple(actor_id, loom_id,
				request_id), std::move(message)).second)
				return (FT_ERR_ALREADY_EXISTS);
		}
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (offset != byte_count - 8U)
		return (FT_ERR_INVALID_ARGUMENT);
	this->request_cache_.swap(staged);
	this->wire_request_cache_.swap(staged_wire);
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::serialize_checkpoint(
	std::vector<uint8_t> &bytes_out) const noexcept
{
	std::vector<uint8_t> draft_state;
	std::vector<uint8_t> request_state;
	std::vector<uint8_t> staged;
	int32_t result = this->serialize_draft_state(draft_state);
	if (result == FT_ERR_SUCCESS)
		result = this->serialize_request_cache(request_state);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (draft_state.size() > UINT32_MAX || request_state.size() > UINT32_MAX
		|| draft_state.size() + request_state.size() > 64U * 1024U * 1024U - 24U)
		return (FT_ERR_FULL);
	try
	{
		staged.reserve(24U + draft_state.size() + request_state.size());
		staged.insert(staged.end(), {'W', 'R', 'P', '1'});
		world_revision_state_append_u32(staged, 1U);
		world_revision_state_append_u32(staged,
			static_cast<uint32_t>(draft_state.size()));
		world_revision_state_append_u32(staged,
			static_cast<uint32_t>(request_state.size()));
		staged.insert(staged.end(), draft_state.begin(), draft_state.end());
		staged.insert(staged.end(), request_state.begin(), request_state.end());
		world_revision_state_append_u64(staged,
			world_revision_state_checksum(staged.data(), staged.size()));
		bytes_out.swap(staged);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::restore_checkpoint(const uint8_t *bytes,
	std::size_t byte_count) noexcept
{
	if (this->initialized_ || this->world_ == nullptr
		|| (bytes == nullptr && byte_count != 0U))
		return (FT_ERR_INVALID_STATE);
	if (byte_count < 24U || byte_count > 64U * 1024U * 1024U
		|| std::memcmp(bytes, "WRP1", 4U) != 0
		|| world_revision_state_read_u32(bytes, 4U) != 1U)
		return (FT_ERR_INVALID_ARGUMENT);
	const uint32_t draft_size = world_revision_state_read_u32(bytes, 8U);
	const uint32_t request_size = world_revision_state_read_u32(bytes, 12U);
	if (static_cast<std::size_t>(draft_size) > byte_count - 24U
		|| static_cast<std::size_t>(request_size)
			!= byte_count - 24U - draft_size
		|| world_revision_state_read_u64(bytes, byte_count - 8U)
			!= world_revision_state_checksum(bytes, byte_count - 8U))
		return (FT_ERR_INVALID_ARGUMENT);
	WorldRevisionServer staged(*this->world_);
	int32_t result = staged.restore_draft_state(bytes + 16U, draft_size);
	if (result == FT_ERR_SUCCESS)
		result = staged.restore_request_cache(bytes + 16U + draft_size,
			request_size);
	if (result != FT_ERR_SUCCESS)
		return (result);
	this->draft_actor_id_ = staged.draft_actor_id_;
	this->draft_loom_id_ = staged.draft_loom_id_;
	this->draft_owned_ = staged.draft_owned_;
	this->draft_start_committed_ = staged.draft_start_committed_;
	this->request_cache_.swap(staged.request_cache_);
	this->wire_request_cache_.swap(staged.wire_request_cache_);
	/* The staging server borrows this same World; its destructor must not
	 * interpret the transferred draft as a real server shutdown. */
	staged.draft_owned_ = false;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::save_checkpoint_file(
	const char *file_path) const noexcept
{
	std::vector<uint8_t> bytes;
	if (file_path == nullptr || file_path[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	const int32_t result = this->serialize_checkpoint(bytes);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (filesystem_atomic_write(file_path, bytes.data(), bytes.size()));
}

int32_t WorldRevisionServer::load_checkpoint_file(
	const char *file_path) noexcept
{
	std::vector<uint8_t> bytes;
	if (file_path == nullptr || file_path[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	const int32_t result = world_revision_server_read_checkpoint_file(
		file_path, bytes);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (this->restore_checkpoint(bytes.data(), bytes.size()));
}

int32_t WorldRevisionServer::start(const char *ip, uint16_t port)
{
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	return (this->server_.start(ip, port));
}

void WorldRevisionServer::run_once()
{
	if (this->initialized_)
		this->server_.run_once();
}

int32_t WorldRevisionServer::destroy()
{
	int32_t result = FT_ERR_SUCCESS;

	if (this->initialized_)
	{
		result = this->server_.destroy();
		if (result != FT_ERR_SUCCESS)
			return (result);
		this->initialized_ = false;
	}
	if (this->world_ != nullptr && this->draft_owned_
		&& !this->draft_start_committed_)
	{
		const World::WorldRevision revision = this->world_->world_revision();

		if (revision.pending && !revision.regenerating)
		{
			result = this->world_->cancel_world_revision();
			if (result != FT_ERR_SUCCESS)
				return (result);
		}
	}
	this->draft_owned_ = false;
	this->draft_start_committed_ = false;
	this->draft_actor_id_ = 0U;
	this->draft_loom_id_ = 0U;
	this->request_cache_.clear();
	this->wire_request_cache_.clear();
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::handle_message(int32_t client_handle,
	const char *message, void *user_data)
{
	WorldRevisionServer *server;
	uint64_t actor_id = 0U;
	uint64_t loom_id = 0U;
	int32_t error_code;
	json_item *request_id_item;
	json_item *protocol_version_item;
	uint64_t request_id;
	uint64_t protocol_version;
	std::size_t message_length = 0U;

	if (user_data == nullptr || message == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	while (message_length <= 4096U && message[message_length] != '\0')
		++message_length;
	if (message_length > 4096U)
		return (FT_ERR_OUT_OF_RANGE);
	server = static_cast<WorldRevisionServer *>(user_data);
	if (server->authorizer_ == nullptr)
		return (FT_ERR_PERMISSION_DENIED);
	error_code = server->authorizer_(client_handle, message, &actor_id,
		&loom_id, server->authorizer_user_data_);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (actor_id == 0U || loom_id == 0U)
		return (FT_ERR_PERMISSION_DENIED);
	json_group *groups = json_read_from_string(message);
	if (groups == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	json_group *revision = json_find_group(groups, "world_revision");
	protocol_version_item = revision == nullptr ? nullptr
		: json_find_item(revision, "protocol_version");
	if (protocol_version_item == nullptr
		|| world_revision_parse_u64(protocol_version_item->value,
			&protocol_version) != FT_ERR_SUCCESS
		|| protocol_version != 1U)
	{
		json_free_groups(groups);
		return (FT_ERR_CONFIGURATION);
	}
	json_item *action_item = revision == nullptr ? nullptr
		: json_find_item(revision, "action");
	if (action_item != nullptr
		&& std::strcmp(action_item->value, "status") == 0)
	{
		error_code = server->handle_status_message(client_handle, revision,
			actor_id, loom_id);
		json_free_groups(groups);
		return (error_code);
	}
	if (action_item != nullptr
		&& std::strcmp(action_item->value, "card_hand") == 0)
	{
		error_code = server->handle_card_hand_message(client_handle, message,
			revision, actor_id, loom_id);
		json_free_groups(groups);
		return (error_code);
	}
	if (action_item != nullptr
		&& std::strcmp(action_item->value, "preview") == 0)
	{
		error_code = server->handle_preview_message(client_handle, message,
			revision, actor_id, loom_id);
		json_free_groups(groups);
		return (error_code);
	}
	if (action_item != nullptr
		&& std::strcmp(action_item->value, "quote") == 0)
	{
		error_code = server->handle_quote_message(client_handle, message,
			revision, actor_id, loom_id);
		json_free_groups(groups);
		return (error_code);
	}
	if (action_item != nullptr
		&& std::strcmp(action_item->value, "instability_view") == 0)
	{
		error_code = server->handle_instability_view_message(client_handle,
			message, revision, actor_id, loom_id);
		json_free_groups(groups);
		return (error_code);
	}
	request_id_item = revision == nullptr ? nullptr
		: json_find_item(revision, "request_id");
	if (action_item == nullptr || request_id_item == nullptr
		|| world_revision_parse_u64(request_id_item->value, &request_id)
			!= FT_ERR_SUCCESS
		|| request_id == 0U)
	{
		json_free_groups(groups);
		return (server->handle_revision_message(message, actor_id, loom_id));
	}
	error_code = server->handle_revision_message(message, actor_id, loom_id);
	int32_t send_error = server->send_action_response(client_handle, message,
		actor_id, loom_id, request_id, error_code);
	json_free_groups(groups);
	return (send_error == FT_ERR_SUCCESS ? error_code : send_error);
}

int32_t WorldRevisionServer::send_action_response(int32_t client_handle,
	const char *message, uint64_t actor_id, uint64_t loom_id,
	uint64_t request_id, int32_t result)
{
	World::WorldRevision revision = this->world_->world_revision();
	uint32_t selected_count = static_cast<uint32_t>(revision.selected_count);
	const std::tuple<uint64_t, uint64_t, uint64_t> request_key =
		std::make_tuple(actor_id, loom_id, request_id);
	std::map<std::tuple<uint64_t, uint64_t, uint64_t>, CachedRequest>::const_iterator
		cached = this->request_cache_.find(request_key);
	std::string payload;
	ft_string response;
	int32_t send_result;

	if (cached != this->request_cache_.end()
		&& cached->second.completed && cached->second.has_revision_state
		&& cached->second.payload == message)
	{
		revision.identifier = cached->second.revision_identifier;
		revision.selection_revision = cached->second.selection_revision;
		revision.cost_policy_revision = cached->second.cost_policy_revision;
		selected_count = cached->second.selected_count;
	}
	try
	{
		if (cached != this->request_cache_.end()
			&& cached->second.completed
			&& cached->second.has_card_play_result
			&& cached->second.payload == message)
		{
			const RegenerationPolicyIntent &card =
				cached->second.card_play_result;
			payload = "{\"world_revision_card_play_result\":{\"protocol_version\":1,\"request_id\":\""
				+ std::to_string(request_id) + "\",\"result\":"
				+ std::to_string(result) + ",\"card_definition_id\":"
				+ std::to_string(card.card_definition_id)
				+ ",\"card_instance_id\":"
				+ std::to_string(card.card_instance_id)
				+ ",\"content_version\":"
				+ std::to_string(card.content_version)
				+ ",\"effect_kind\":"
				+ std::to_string(static_cast<uint32_t>(card.effect_kind))
				+ ",\"host_intent_sequence\":\""
				+ std::to_string(card.host_intent_sequence)
				+ "\",\"choice_kind\":"
				+ std::to_string(static_cast<uint32_t>(card.choice.kind))
				+ ",\"choice_primary\":\""
				+ std::to_string(card.choice.value_primary)
				+ "\",\"choice_secondary\":\""
				+ std::to_string(card.choice.value_secondary)
				+ "\",\"no_effect_choice_mask\":"
				+ std::to_string(card.no_effect_choice_mask)
				+ ",\"replayed\":"
				+ (card.replayed ? "true" : "false") + "}}";
		}
		else if (cached != this->request_cache_.end()
			&& cached->second.completed
			&& cached->second.has_card_discard_result
			&& cached->second.payload == message)
		{
			const RegenerationCardActionResult &card =
				cached->second.card_discard_result;
			payload = "{\"world_revision_card_discard_result\":{\"protocol_version\":1,\"request_id\":\""
				+ std::to_string(request_id) + "\",\"result\":"
				+ std::to_string(result) + ",\"session_id\":\""
				+ std::to_string(card.session_id)
				+ "\",\"command_sequence\":\""
				+ std::to_string(card.command_sequence)
				+ "\",\"count\":" + std::to_string(card.count)
				+ ",\"replayed\":"
				+ (card.replayed ? "true" : "false") + "}}";
		}
		else if (cached != this->request_cache_.end()
			&& cached->second.completed
			&& cached->second.has_card_draw_result
			&& cached->second.payload == message)
		{
			const RegenerationCardActionResult &card =
				cached->second.card_draw_result;
			payload = "{\"world_revision_card_draw_result\":{\"protocol_version\":1,\"request_id\":\""
				+ std::to_string(request_id) + "\",\"result\":"
				+ std::to_string(result) + ",\"session_id\":\""
				+ std::to_string(card.session_id)
				+ "\",\"command_sequence\":\""
				+ std::to_string(card.command_sequence)
				+ "\",\"count\":" + std::to_string(card.count)
				+ ",\"replayed\":"
				+ (card.replayed ? "true" : "false") + "}}";
		}
		else if (cached != this->request_cache_.end()
			&& cached->second.completed
			&& cached->second.has_card_expiry_result
			&& cached->second.payload == message)
		{
			payload = "{\"world_revision_card_choice_expiry_result\":{\"protocol_version\":1,\"request_id\":\""
				+ std::to_string(request_id) + "\",\"result\":"
				+ std::to_string(result) + ",\"resolved_count\":"
				+ std::to_string(cached->second.card_expiry_resolved_count)
				+ ",\"replayed\":"
				+ (cached->second.card_expiry_replayed ? "true" : "false")
				+ "}}";
		}
		else if (cached != this->request_cache_.end()
			&& cached->second.completed
			&& cached->second.has_card_choice_result
			&& cached->second.payload == message)
		{
			const RegenerationCardActionResult &card =
				cached->second.card_choice_result;
			payload = "{\"world_revision_card_choice_result\":{\"protocol_version\":1,\"request_id\":\""
				+ std::to_string(request_id) + "\",\"result\":"
				+ std::to_string(result) + ",\"session_id\":\""
				+ std::to_string(card.session_id)
				+ "\",\"command_sequence\":\""
				+ std::to_string(card.command_sequence)
				+ "\",\"count\":" + std::to_string(card.count)
				+ ",\"replayed\":"
				+ (card.replayed ? "true" : "false") + "}}";
		}
		else
		payload = "{\"world_revision_result\":{\"protocol_version\":1,\"request_id\":\""
			+ std::to_string(request_id) + "\",\"result\":"
			+ std::to_string(result) + ",\"revision_identifier\":"
			+ std::to_string(revision.identifier)
			+ ",\"selection_revision\":\""
			+ std::to_string(revision.selection_revision)
			+ "\",\"policy_revision\":\""
			+ std::to_string(revision.cost_policy_revision)
			+ "\",\"selected_count\":"
			+ std::to_string(selected_count) + "}}";
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (response.initialize(payload.c_str()) != FT_ERR_SUCCESS)
		return (FT_ERR_NO_MEMORY);
	if (this->response_sender_ != nullptr)
		send_result = this->response_sender_(client_handle,
			response.c_str(), this->response_sender_user_data_);
	else
		send_result = this->server_.send_to_client(client_handle, response);
	(void)response.destroy();
	return (send_result);
}

int32_t WorldRevisionServer::handle_card_hand_message(int32_t client_handle,
	const char *message, json_group *revision, uint64_t actor_id,
	uint64_t loom_id)
{
	json_item *request_id_item = revision == nullptr ? nullptr
		: json_find_item(revision, "request_id");
	uint64_t request_id = 0U;
	RegenerationCardHandView hand = {};
	std::string payload;
	ft_string response;
	int32_t result;
	int32_t send_result;
	if (request_id_item == nullptr
		|| world_revision_parse_u64(request_id_item->value, &request_id)
			!= FT_ERR_SUCCESS || request_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->draft_owned_ || !this->draft_start_committed_
		|| actor_id != this->draft_actor_id_ || loom_id != this->draft_loom_id_)
		result = FT_ERR_PERMISSION_DENIED;
	else
		result = this->authorize_station_access(actor_id, loom_id, message);
	if (result == FT_ERR_SUCCESS && this->card_hand_query_ == nullptr)
		result = FT_ERR_INVALID_OPERATION;
	if (result == FT_ERR_SUCCESS)
		result = this->card_hand_query_(actor_id, loom_id, hand,
			this->card_hand_query_user_data_);
	if (result == FT_ERR_SUCCESS && (hand.card_session_id == 0U
		|| hand.cards.size() > FT_CARD_GAME_MAX_CARDS
		hand.required_discard_count > hand.cards.size()
		|| hand.pending_choices.size() > FT_CARD_GAME_MAX_CHOICES))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		for (const card_game_pending_choice_view &choice : hand.pending_choices)
		{
			if (choice.choice_id == 0U || choice.option_count == 0U
				|| choice.option_count > FT_CARD_GAME_MAX_CHOICE_OPTIONS)
			{
				result = FT_ERR_INVALID_STATE;
				break;
			}
			for (uint32_t index = 0U; index < choice.option_count; ++index)
				if (choice.options[index].option_id == 0U)
				{
					result = FT_ERR_INVALID_STATE;
					break;
				}
			if (result != FT_ERR_SUCCESS)
				break;
		}
	try
	{
		payload = "{\"world_revision_card_hand\":{\"protocol_version\":1,\"request_id\":\""
			+ std::to_string(request_id) + "\",\"result\":"
			+ std::to_string(result);
		if (result == FT_ERR_SUCCESS)
		{
			payload += ",\"session_id\":\""
				+ std::to_string(hand.card_session_id)
				+ "\",\"required_discard_count\":"
				+ std::to_string(hand.required_discard_count)
				+ ",\"cards\":[";
			for (std::size_t index = 0U; index < hand.cards.size(); ++index)
			{
				if (index != 0U)
					payload += ",";
				payload += "{\"instance_id\":"
					+ std::to_string(hand.cards[index].card_instance_id)
					+ ",\"definition_id\":"
					+ std::to_string(hand.cards[index].card_definition_id) + "}";
			}
			payload += "],\"pending_choices\":[";
			for (std::size_t choice_index = 0U;
				choice_index < hand.pending_choices.size(); ++choice_index)
			{
				const card_game_pending_choice_view &choice =
					hand.pending_choices[choice_index];
				if (choice_index != 0U)
					payload += ",";
				payload += "{\"choice_id\":"
					+ std::to_string(choice.choice_id)
					+ ",\"kind\":"
					+ std::to_string(static_cast<uint32_t>(choice.kind))
					+ ",\"deadline_epoch\":\""
					+ std::to_string(choice.deadline_epoch)
					+ "\",\"options\":[";
				for (uint32_t option_index = 0U;
					option_index < choice.option_count; ++option_index)
				{
					if (option_index != 0U)
						payload += ",";
					const card_game_choice_option &option =
						choice.options[option_index];
					payload += "{\"option_id\":"
						+ std::to_string(option.option_id)
						+ ",\"value_a\":"
						+ std::to_string(option.value_a)
						+ ",\"value_b\":"
						+ std::to_string(option.value_b) + "}";
				}
				payload += "]}";
			}
			payload += "]";
		}
		payload += "}}";
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (payload.size() > 4096U)
		return (FT_ERR_OUT_OF_RANGE);
	if (response.initialize(payload.c_str()) != FT_ERR_SUCCESS)
		return (FT_ERR_NO_MEMORY);
	if (this->response_sender_ != nullptr)
		send_result = this->response_sender_(client_handle, response.c_str(),
			this->response_sender_user_data_);
	else
		send_result = this->server_.send_to_client(client_handle, response);
	(void)response.destroy();
	return (send_result);
}

int32_t WorldRevisionServer::handle_quote_message(int32_t client_handle,
	const char *message, json_group *revision, uint64_t actor_id,
	uint64_t loom_id)
{
	json_item *request_id_item = json_find_item(revision, "request_id");
	json_item *selection_item = json_find_item(revision,
		"selection_revision");
	json_item *policy_item = json_find_item(revision, "policy_revision");
	json_item *count_item = json_find_item(revision, "opening_card_count");
	uint64_t request_id;
	uint64_t selection_revision;
	uint64_t policy_revision;
	uint64_t count;
	WorldRevisionCostQuote quote = {};
	int32_t result;
	ft_string response;
	std::string payload;
	std::tuple<uint64_t, uint64_t, uint64_t> request_key;
	std::map<std::tuple<uint64_t, uint64_t, uint64_t>, std::string>::iterator
		wire_request;

	if (request_id_item == nullptr || selection_item == nullptr
		|| policy_item == nullptr || count_item == nullptr
		|| world_revision_parse_u64(request_id_item->value, &request_id)
			!= FT_ERR_SUCCESS
		|| world_revision_parse_u64(selection_item->value,
			&selection_revision) != FT_ERR_SUCCESS
		|| world_revision_parse_u64(policy_item->value, &policy_revision)
			!= FT_ERR_SUCCESS
		|| world_revision_parse_u64(count_item->value, &count)
			!= FT_ERR_SUCCESS
		|| request_id == 0U || count < 4U || count > 7U)
		return (FT_ERR_INVALID_ARGUMENT);
	result = this->authorize_station_access(actor_id, loom_id, message);
	if (result != FT_ERR_SUCCESS)
		return (result);
	request_key = std::make_tuple(actor_id, loom_id, request_id);
	wire_request = this->wire_request_cache_.find(request_key);
	if (wire_request != this->wire_request_cache_.end())
	{
		if (wire_request->second != message)
			return (FT_ERR_INVALID_ARGUMENT);
	}
	else
	{
		if (this->wire_request_cache_.size()
			>= WorldRevisionServer::REQUEST_CACHE_LIMIT)
			return (FT_ERR_FULL);
		try
		{
			this->wire_request_cache_.emplace(request_key, message);
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	result = this->query_start_cost(actor_id, loom_id, request_id,
		selection_revision, policy_revision, static_cast<uint32_t>(count),
		&quote);
	try
	{
		payload = "{\"world_revision_quote\":{\"protocol_version\":1,\"request_id\":\""
			+ std::to_string(request_id) + "\",\"result\":"
			+ std::to_string(result);
		if (result == FT_ERR_SUCCESS)
		{
			payload += ",\"revision_identifier\":"
				+ std::to_string(quote.revision_identifier)
				+ ",\"selection_revision\":\""
				+ std::to_string(quote.selection_revision)
				+ "\",\"policy_revision\":\""
				+ std::to_string(quote.cost_policy_revision)
				+ "\",\"selected_paid_chunks\":"
				+ std::to_string(quote.selected_paid_chunks)
				+ ",\"selected_bonus_chunks\":"
				+ std::to_string(quote.selected_bonus_chunks)
				+ ",\"opening_card_count\":"
				+ std::to_string(quote.opening_card_count)
				+ ",\"currency_item_id\":"
				+ std::to_string(quote.currency_item_id)
				+ ",\"chunk_currency_cost\":\""
				+ std::to_string(quote.chunk_currency_cost)
				+ "\",\"opening_card_currency_cost\":\""
				+ std::to_string(quote.opening_card_currency_cost)
				+ "\",\"total_currency_cost\":\""
				+ std::to_string(quote.total_currency_cost) + "\"";
		}
		payload += "}}";
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (response.initialize(payload.c_str()) != FT_ERR_SUCCESS)
		return (FT_ERR_NO_MEMORY);
	if (this->response_sender_ != nullptr)
		result = this->response_sender_(client_handle, response.c_str(),
			this->response_sender_user_data_);
	else
		result = this->server_.send_to_client(client_handle, response);
	(void)response.destroy();
	return (result);
}

int32_t WorldRevisionServer::handle_status_message(int32_t client_handle,
	json_group *revision_group, uint64_t actor_id, uint64_t loom_id)
{
	json_item *request_id_item = revision_group == nullptr ? nullptr
		: json_find_item(revision_group, "request_id");
	uint64_t request_id = 0U;
	World::WorldRevision revision = {};
	std::vector<WorldRevisionManager::RevisionChunk> selected_chunks;
	WorldRevisionStartPolicyStatus start_policy = {};
	uint32_t status_revision_id = 0U;
	bool completed = false;
	bool has_start_policy = false;
	bool player_cancel_allowed = false;
	bool selection_edit_allowed = false;
	bool trusted_retry_available = false;
	uint64_t deadline_epoch_ms = 0U;
	const char *lifecycle_state = "UNKNOWN";
	std::string payload;
	ft_string response;
	int32_t result = FT_ERR_SUCCESS;
	int32_t send_result;

	if (request_id_item == nullptr
		|| world_revision_parse_u64(request_id_item->value, &request_id)
			!= FT_ERR_SUCCESS
		|| request_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->world_ == nullptr || !this->initialized_)
		result = FT_ERR_NOT_INITIALISED;
	else if (!this->draft_owned_ || actor_id == 0U || loom_id == 0U
		|| actor_id != this->draft_actor_id_
		|| loom_id != this->draft_loom_id_)
		result = FT_ERR_PERMISSION_DENIED;
	if (result == FT_ERR_SUCCESS)
	{
		revision = this->world_->world_revision();
		result = this->world_->revision_manager.copy_status_chunks(
			selected_chunks, &status_revision_id, &completed);
		deadline_epoch_ms = this->world_->revision_decision_deadline_epoch();
		if (result == FT_ERR_SUCCESS && this->start_policy_query_ != nullptr)
		{
			const int32_t policy_result = this->start_policy_query_(actor_id,
				loom_id, start_policy,
				this->start_policy_query_user_data_);
			if (policy_result == FT_ERR_SUCCESS)
				has_start_policy = true;
			else if (policy_result != FT_ERR_NOT_FOUND)
				result = policy_result;
		}
		if (result == FT_ERR_SUCCESS && completed)
		{
			revision.completed_jobs = selected_chunks.size();
			revision.total_jobs = selected_chunks.size();
			revision.regenerated_chunks = 0;
			revision.skipped_chunks = 0;
			revision.regeneration_error = FT_ERR_SUCCESS;
			for (const WorldRevisionManager::RevisionChunk &chunk
				: selected_chunks)
			{
				if (this->world_->revision_manager.status_chunk_is_published(
						chunk.chunk_x, chunk.chunk_z))
					++revision.regenerated_chunks;
				WorldRevisionManager::RegenerationChunkOutcome outcome = {};
				if (this->world_->revision_manager
					.get_regeneration_chunk_outcome(chunk.chunk_x, chunk.chunk_z,
						&outcome) == FT_ERR_SUCCESS)
				{
					if (outcome.skipped)
						++revision.skipped_chunks;
					else if (revision.regeneration_error == FT_ERR_SUCCESS)
						revision.regeneration_error = outcome.error_code;
				}
			}
		}
		if (result == FT_ERR_SUCCESS)
		{
			if (revision.regenerating)
				lifecycle_state = "GENERATING";
			else if (revision.pending && this->draft_start_committed_
				&& this->world_->revision_generation_requested())
				lifecycle_state = "GENERATION_QUEUED";
			else if (revision.pending && this->draft_start_committed_)
				lifecycle_state = "DECISION_WINDOW";
			else if (revision.pending)
			{
				lifecycle_state = "DRAFT_OPEN";
				player_cancel_allowed = true;
				selection_edit_allowed = true;
			}
			else if (completed && this->draft_start_committed_)
				lifecycle_state = "COMPLETE";
			else if (this->world_->revision_barriers_active())
			{
				lifecycle_state = "FAILED_RETRYABLE";
				trusted_retry_available = true;
			}
			else
				lifecycle_state = "NO_DRAFT";
			if (std::strcmp(lifecycle_state, "NO_DRAFT") == 0)
			{
				selected_chunks.clear();
				completed = false;
				status_revision_id = revision.identifier;
				revision.completed_jobs = 0U;
				revision.total_jobs = 0U;
				revision.regenerated_chunks = 0U;
				revision.skipped_chunks = 0U;
				revision.regeneration_error = FT_ERR_SUCCESS;
				start_policy = {};
				has_start_policy = false;
			}
		}
	}
	try
	{
		payload = "{\"world_revision_status\":{\"protocol_version\":1,\"request_id\":\""
			+ std::to_string(request_id) + "\",\"result\":"
			+ std::to_string(result);
		if (result == FT_ERR_SUCCESS)
		{
			payload += ",\"revision_identifier\":"
				+ std::to_string(status_revision_id)
				+ ",\"lifecycle_state\":\""
				+ lifecycle_state
				+ "\",\"player_cancel_allowed\":"
				+ (player_cancel_allowed ? "true" : "false")
				+ ",\"selection_edit_allowed\":"
				+ (selection_edit_allowed ? "true" : "false")
				+ ",\"trusted_retry_available\":"
				+ (trusted_retry_available ? "true" : "false")
				+ ",\"selection_revision\":\""
				+ std::to_string(revision.selection_revision)
				+ "\",\"policy_revision\":\""
				+ std::to_string(revision.cost_policy_revision)
				+ "\",\"pending\":"
				+ (revision.pending ? "true" : "false")
				+ ",\"completed\":"
				+ (completed ? "true" : "false")
				+ ",\"regenerating\":"
				+ (revision.regenerating ? "true" : "false")
				+ ",\"completed_jobs\":"
				+ std::to_string(revision.completed_jobs)
				+ ",\"total_jobs\":"
				+ std::to_string(revision.total_jobs)
				+ ",\"regenerated_chunks\":"
				+ std::to_string(revision.regenerated_chunks)
				+ ",\"skipped_chunks\":"
				+ std::to_string(revision.skipped_chunks)
				+ ",\"regeneration_error\":"
				+ std::to_string(revision.regeneration_error)
				+ ",\"barriers_active\":"
				+ (this->world_->revision_barriers_active()
					? "true" : "false")
				+ ",\"decision_deadline_epoch_ms\":\""
				+ std::to_string(deadline_epoch_ms)
				+ "\",\"decision_mode\":\""
				+ (has_start_policy && start_policy.mode == 1U ? "solo"
					: has_start_policy && start_policy.mode == 2U ? "multiplayer"
					: has_start_policy && start_policy.decision_timeout_ms != 0U
						? "legacy" : "unknown")
				+ "\",\"active_player_count\":"
				+ std::to_string(has_start_policy
					? start_policy.active_player_count : 0U)
				+ ",\"decision_timeout_ms\":\""
				+ std::to_string(has_start_policy
					? start_policy.decision_timeout_ms : 0U)
				+ "\",\"currency_item_id\":"
				+ std::to_string(has_start_policy
					? start_policy.currency_item_id : 0U)
				+ ",\"committed_currency_amount\":\""
				+ std::to_string(has_start_policy
					? start_policy.committed_currency_amount : 0U)
				+ "\",\"selected_paid_chunks\":"
				+ std::to_string(has_start_policy
					? start_policy.selected_paid_chunks : 0U)
				+ ",\"selected_bonus_chunks\":"
				+ std::to_string(has_start_policy
					? start_policy.selected_bonus_chunks : 0U)
				+ ",\"settlement_policy\":"
				+ std::to_string(has_start_policy
					? start_policy.settlement_policy : 0U)
				+ ",\"start_revision_identifier\":"
				+ std::to_string(has_start_policy
					? start_policy.revision_identifier : 0U)
				+ ",\"card_content_version\":"
				+ std::to_string(has_start_policy
					? start_policy.card_content_version : 0U)
				+ ",\"generation_manifest_hash\":\""
				+ std::to_string(has_start_policy
					? start_policy.generation_manifest_hash : 0U)
				+ "\",\"trusted_script_manifest_hash\":\""
				+ std::to_string(has_start_policy
					? start_policy.trusted_script_manifest_hash : 0U)
				+ "\",\"settlement_policy_name\":\""
				+ (has_start_policy
					&& start_policy.settlement_policy
						== WORLD_REVISION_SETTLEMENT_RETAIN_COMMITTED_COST
					? "retain_committed_cost" : "unknown")
				+ "\",\"participant_actor_ids\":[";
			for (std::size_t index = 0U;
				index < start_policy.participant_actor_ids.size(); ++index)
			{
				if (index != 0U)
					payload += ",";
				payload += "\""
					+ std::to_string(start_policy.participant_actor_ids[index])
					+ "\"";
			}
			payload += "],\"chunks\":[";
			for (std::size_t index = 0U; index < selected_chunks.size(); ++index)
			{
				const WorldRevisionManager::RevisionChunk &chunk =
					selected_chunks[index];
				const bool publication_committed =
					this->world_->revision_manager.status_chunk_is_published(
						chunk.chunk_x, chunk.chunk_z);
				WorldRevisionManager::RegenerationChunkOutcome outcome = {};
				const bool has_outcome = this->world_->revision_manager
					.get_regeneration_chunk_outcome(chunk.chunk_x, chunk.chunk_z,
						&outcome) == FT_ERR_SUCCESS;
				if (index != 0U)
					payload += ",";
				payload += "{\"x\":" + std::to_string(chunk.chunk_x)
					+ ",\"z\":" + std::to_string(chunk.chunk_z)
					+ ",\"state\":" + std::to_string(static_cast<int32_t>(
						this->world_->revision_state(chunk.chunk_x,
							chunk.chunk_z)))
					+ ",\"publication_status\":\""
					+ (publication_committed ? "committed\""
						: has_outcome ? (outcome.skipped ? "skipped\""
							: "failed\"") : "pending\"");
				if (has_outcome)
					payload += ",\"error_code\":"
						+ std::to_string(outcome.error_code);
				payload += "}";
			}
			payload += "]";
		}
		payload += "}}";
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (payload.size() > 64U * 1024U)
		return (FT_ERR_OUT_OF_RANGE);
	if (response.initialize(payload.c_str()) != FT_ERR_SUCCESS)
		return (FT_ERR_NO_MEMORY);
	if (this->response_sender_ != nullptr)
		send_result = this->response_sender_(client_handle,
			response.c_str(), this->response_sender_user_data_);
	else
		send_result = this->server_.send_to_client(client_handle, response);
	(void)response.destroy();
	return (send_result);
}

int32_t WorldRevisionServer::handle_preview_message(int32_t client_handle,
	const char *message, json_group *revision_group, uint64_t actor_id,
	uint64_t loom_id)
{
	static const uint64_t MAX_PREVIEW_RADIUS = 8U;
	json_item *request_id_item = revision_group == nullptr ? nullptr
		: json_find_item(revision_group, "request_id");
	json_item *center_x_item = revision_group == nullptr ? nullptr
		: json_find_item(revision_group, "center_chunk_x");
	json_item *center_z_item = revision_group == nullptr ? nullptr
		: json_find_item(revision_group, "center_chunk_z");
	json_item *radius_item = revision_group == nullptr ? nullptr
		: json_find_item(revision_group, "radius");
	uint64_t request_id = 0U;
	uint64_t radius = 0U;
	int32_t center_x = 0;
	int32_t center_z = 0;
	int32_t result = FT_ERR_SUCCESS;
	bool occupancy_refreshed = false;
	std::vector<World::RevisionPreviewEntry> entries;
	std::string payload;
	ft_string response;
	int32_t send_result;

	if (message == nullptr || this->world_ == nullptr
		|| request_id_item == nullptr || center_x_item == nullptr
		|| center_z_item == nullptr || radius_item == nullptr
		|| world_revision_parse_u64(request_id_item->value, &request_id)
			!= FT_ERR_SUCCESS || request_id == 0U
		|| world_revision_parse_i32(center_x_item->value, &center_x)
			!= FT_ERR_SUCCESS
		|| world_revision_parse_i32(center_z_item->value, &center_z)
			!= FT_ERR_SUCCESS
		|| world_revision_parse_u64(radius_item->value, &radius)
			!= FT_ERR_SUCCESS || radius > MAX_PREVIEW_RADIUS)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->initialized_)
		result = FT_ERR_NOT_INITIALISED;
	if (result == FT_ERR_SUCCESS)
		result = this->authorize_station_access(actor_id, loom_id, message);
	if (result == FT_ERR_SUCCESS)
	{
		result = this->refresh_occupied_chunks();
		occupancy_refreshed = result == FT_ERR_SUCCESS;
	}
	if (result == FT_ERR_SUCCESS)
		result = this->world_->build_revision_preview(center_x, center_z,
			static_cast<int32_t>(radius), entries);
	if (occupancy_refreshed)
	{
		const int32_t checkpoint_result = this->commit_checkpoint();
		if (checkpoint_result != FT_ERR_SUCCESS)
			result = checkpoint_result;
	}
	try
	{
		payload = "{\"world_revision_preview\":{\"protocol_version\":1,\"request_id\":\""
			+ std::to_string(request_id) + "\",\"result\":"
			+ std::to_string(result);
		if (result == FT_ERR_SUCCESS)
		{
			payload += ",\"center_chunk_x\":" + std::to_string(center_x)
				+ ",\"center_chunk_z\":" + std::to_string(center_z)
				+ ",\"radius\":" + std::to_string(radius)
				+ ",\"selection_revision\":\""
				+ std::to_string(this->world_->world_revision().selection_revision)
				+ "\",\"chunks\":[";
			for (std::size_t index = 0U; index < entries.size(); ++index)
			{
				const World::RevisionPreviewEntry &entry = entries[index];
				if (index != 0U)
					payload += ",";
				payload += "{\"x\":" + std::to_string(entry.chunk_x)
					+ ",\"z\":" + std::to_string(entry.chunk_z)
					+ ",\"state\":"
					+ std::to_string(static_cast<int32_t>(entry.state))
					+ ",\"generation_protected\":"
					+ (entry.protection_info.generation_protected ? "true" : "false")
					+ ",\"loom_safe_zone\":"
					+ (entry.protection_info.loom_safe_zone ? "true" : "false")
					+ ",\"manually_protected\":"
					+ (entry.protection_info.manually_protected ? "true" : "false")
					+ ",\"occupied_player_exclusion\":"
					+ (entry.protection_info.occupied_player_exclusion
						? "true}" : "false}");
			}
			payload += "]";
		}
		payload += "}}";
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (payload.size() > 64U * 1024U)
		return (FT_ERR_OUT_OF_RANGE);
	if (response.initialize(payload.c_str()) != FT_ERR_SUCCESS)
		return (FT_ERR_NO_MEMORY);
	if (this->response_sender_ != nullptr)
		send_result = this->response_sender_(client_handle,
			response.c_str(), this->response_sender_user_data_);
	else
		send_result = this->server_.send_to_client(client_handle, response);
	(void)response.destroy();
	return (send_result);
}

int32_t WorldRevisionServer::handle_instability_view_message(
	int32_t client_handle, const char *message, json_group *revision,
	uint64_t actor_id, uint64_t loom_id)
{
	json_item *request_id_item = json_find_item(revision, "request_id");
	json_item *chunk_x_item = json_find_item(revision, "chunk_x");
	json_item *chunk_z_item = json_find_item(revision, "chunk_z");
	uint64_t request_id = 0U;
	int32_t chunk_x = 0;
	int32_t chunk_z = 0;
	int32_t result;
	WorldChunkInstabilityPublicView view = {};
	std::tuple<uint64_t, uint64_t, uint64_t> request_key;
	std::map<std::tuple<uint64_t, uint64_t, uint64_t>, CachedRequest>::iterator
		cached;
	std::string payload;
	ft_string response;
	int32_t send_result;

	if (this->world_ == nullptr || request_id_item == nullptr
		|| chunk_x_item == nullptr || chunk_z_item == nullptr
		|| world_revision_parse_u64(request_id_item->value, &request_id)
			!= FT_ERR_SUCCESS || request_id == 0U
		|| world_revision_parse_i32(chunk_x_item->value, &chunk_x)
			!= FT_ERR_SUCCESS
		|| world_revision_parse_i32(chunk_z_item->value, &chunk_z)
			!= FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->instability_view_authorizer_ == nullptr)
		return (FT_ERR_PERMISSION_DENIED);
	result = this->instability_view_authorizer_(actor_id, loom_id, chunk_x,
		chunk_z, this->instability_view_authorizer_user_data_);
	if (result != FT_ERR_SUCCESS)
		return (result);
	request_key = std::make_tuple(actor_id, loom_id, request_id);
	cached = this->request_cache_.find(request_key);
	if (cached != this->request_cache_.end())
	{
		if (cached->second.payload != message)
			return (FT_ERR_INVALID_ARGUMENT);
		if (!cached->second.completed)
			return (FT_ERR_INVALID_STATE);
		result = cached->second.result;
		if (cached->second.has_instability_view)
			view = cached->second.instability_view;
	}
	else
	{
		if (this->request_cache_.size() >= WorldRevisionServer::REQUEST_CACHE_LIMIT)
			return (FT_ERR_FULL);
		try
		{
			CachedRequest staged = {};
			staged.payload = message;
			staged.result = FT_ERR_INVALID_STATE;
			staged.completed = false;
			staged.has_quote = false;
			staged.has_instability_view = false;
			staged.has_revision_state = false;
			cached = this->request_cache_.emplace(request_key,
				std::move(staged)).first;
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		result = this->world_->get_chunk_instability_public_view(chunk_x,
			chunk_z, &view);
		cached->second.result = result;
		cached->second.completed = true;
		if (result == FT_ERR_SUCCESS)
		{
			cached->second.has_instability_view = true;
			cached->second.instability_view = view;
		}
	}
	const int32_t checkpoint_result = this->commit_checkpoint();
	if (checkpoint_result != FT_ERR_SUCCESS)
		return (checkpoint_result);
	try
	{
		payload = "{\"world_chunk_instability_view\":{\"protocol_version\":1,\"request_id\":\""
			+ std::to_string(request_id) + "\",\"result\":"
			+ std::to_string(result);
		if (result == FT_ERR_SUCCESS)
			payload += ",\"chunk_x\":" + std::to_string(view.chunk_x)
				+ ",\"chunk_z\":" + std::to_string(view.chunk_z)
				+ ",\"reward_band\":"
				+ std::to_string(view.reward_band)
				+ ",\"danger_band\":"
				+ std::to_string(view.danger_band);
		payload += "}}";
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (response.initialize(payload.c_str()) != FT_ERR_SUCCESS)
		return (FT_ERR_NO_MEMORY);
	if (this->response_sender_ != nullptr)
		send_result = this->response_sender_(client_handle,
			response.c_str(), this->response_sender_user_data_);
	else
		send_result = this->server_.send_to_client(client_handle, response);
	(void)response.destroy();
	return (send_result);
}

int32_t WorldRevisionServer::apply_biome_size_range_override(json_group *revision,
	voxel_generation_config &revision_config)
{
	json_item	*size_min;
	json_item	*size_max;

	size_min = json_find_item(revision, "biome_size_min");
	size_max = json_find_item(revision, "biome_size_max");
	if (size_min == nullptr && size_max == nullptr)
		return (FT_ERR_SUCCESS);
	if (size_min == nullptr || size_max == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (revision_config.set_biome_size_range(ft_atoi(size_min->value),
			ft_atoi(size_max->value)));
}

int32_t WorldRevisionServer::apply_biome_specific_override(json_group *revision,
	voxel_generation_config &revision_config)
{
	json_item	*biome_index;
	json_item	*biome_min;
	json_item	*biome_max;
	json_item	*biome_override;
	uint32_t	index;
	int32_t		enabled;
	int32_t		result;

	biome_index = json_find_item(revision, "biome_index");
	biome_min = json_find_item(revision, "biome_min");
	biome_max = json_find_item(revision, "biome_max");
	biome_override = json_find_item(revision, "biome_size_override_enabled");
	if (biome_index == nullptr && biome_min == nullptr && biome_max == nullptr
		&& biome_override == nullptr)
		return (FT_ERR_SUCCESS);
	if (biome_index == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	index = static_cast<uint32_t>(ft_atoi(biome_index->value));
	if (index >= VOXEL_MAX_CUSTOM_BIOMES)
		return (FT_ERR_INVALID_ARGUMENT);
	result = FT_ERR_SUCCESS;
	if (biome_min != nullptr || biome_max != nullptr)
	{
		if (biome_min == nullptr || biome_max == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		result = revision_config.set_biome_size_range_for_biome(index,
				ft_atoi(biome_min->value), ft_atoi(biome_max->value));
	}
	if (result == FT_ERR_SUCCESS && biome_override != nullptr)
	{
		enabled = ft_atoi(biome_override->value);
		if (enabled < 0 || enabled > 1)
			return (FT_ERR_INVALID_ARGUMENT);
		result = revision_config.set_biome_size_override_enabled(index,
				static_cast<ft_bool>(enabled));
	}
	return (result);
}

int32_t WorldRevisionServer::apply_biome_config_overrides(json_group *revision,
	voxel_generation_config &revision_config)
{
	json_item	*sizes_enabled;
	int32_t		enabled;
	int32_t		result;

	sizes_enabled = json_find_item(revision, "biome_sizes_enabled");
	result = FT_ERR_SUCCESS;
	if (sizes_enabled != nullptr)
	{
		enabled = ft_atoi(sizes_enabled->value);
		if (enabled < 0 || enabled > 1)
			return (FT_ERR_INVALID_ARGUMENT);
		result = revision_config.set_biome_size_control_enabled(static_cast<ft_bool>(enabled));
	}
	if (result == FT_ERR_SUCCESS)
		result = this->apply_biome_size_range_override(revision,
				revision_config);
	if (result == FT_ERR_SUCCESS)
		result = this->apply_biome_specific_override(revision, revision_config);
	return (result);
}

int32_t WorldRevisionServer::handle_begin_action(json_group *revision)
{
	json_item					*mode_item;
	int32_t						mode;
	voxel_generation_config	revision_config;
	int32_t						result;

	if (!this->world_->has_loom_station())
		return (FT_ERR_CONFIGURATION);
	mode_item = json_find_item(revision, "mode");
	mode = mode_item == nullptr ? -1 : ft_atoi(mode_item->value);
	if (mode < World::REGEN_DECORATION_REFRESH || mode > World::REGEN_FULL)
		return (FT_ERR_INVALID_ARGUMENT);
	result = revision_config.initialize(this->world_->voxel_generation_settings());
	if (result == FT_ERR_SUCCESS)
		result = this->apply_biome_config_overrides(revision, revision_config);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (this->world_->begin_world_revision(revision_config,
			static_cast<World::RegenerationMode>(mode)));
}

int32_t WorldRevisionServer::handle_select_or_protect_action(json_group *revision,
	const char *action)
{
	json_item	*x_item;
	json_item	*z_item;
	json_item	*revision_item;
	int32_t		chunk_x;
	int32_t		chunk_z;
	uint64_t	expected_selection_revision;
	uint64_t	current_selection_revision;
	int32_t		error_code;

	x_item = json_find_item(revision, "chunk_x");
	z_item = json_find_item(revision, "chunk_z");
	if (x_item == nullptr || z_item == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	chunk_x = ft_atoi(x_item->value);
	chunk_z = ft_atoi(z_item->value);
	if (std::strcmp(action, "select") == 0
		|| std::strcmp(action, "unselect") == 0)
	{
		revision_item = json_find_item(revision, "selection_revision");
		if (revision_item == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		error_code = world_revision_parse_u64(revision_item->value,
			&expected_selection_revision);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		return (this->world_->update_revision_chunk_selection(
			expected_selection_revision,
			WorldRevisionManager::HARD_MAX_SELECTED_CHUNKS, chunk_x, chunk_z,
			std::strcmp(action, "select") == 0,
			&current_selection_revision));
	}
	revision_item = json_find_item(revision, "selection_revision");
	if (revision_item == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = world_revision_parse_u64(revision_item->value,
		&expected_selection_revision);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (this->world_->update_revision_chunk_protection(
		expected_selection_revision, chunk_x, chunk_z,
		std::strcmp(action, "protect") == 0,
		&current_selection_revision));
}

int32_t WorldRevisionServer::handle_revision_message(const char *message,
	uint64_t actor_id, uint64_t loom_id)
{
	json_group	*groups;
	json_group	*revision;
	json_item	*action_item;
	json_item	*request_id_item;
	const char	*action;
	uint64_t	request_id;
	int32_t		result;
	std::tuple<uint64_t, uint64_t, uint64_t> request_key;
	std::map<std::tuple<uint64_t, uint64_t, uint64_t>, CachedRequest>::iterator cached_request;

	if (message == nullptr || this->world_ == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	groups = json_read_from_string(message);
	if (groups == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	revision = json_find_group(groups, "world_revision");
	action_item = revision == nullptr ? nullptr : json_find_item(revision,
			"action");
	if (action_item == nullptr)
	{
		json_free_groups(groups);
		return (FT_ERR_INVALID_ARGUMENT);
	}
	request_id_item = json_find_item(revision, "request_id");
	if (request_id_item == nullptr
		|| world_revision_parse_u64(request_id_item->value, &request_id)
			!= FT_ERR_SUCCESS
		|| request_id == 0U)
	{
		json_free_groups(groups);
		return (FT_ERR_INVALID_ARGUMENT);
	}
	action = action_item->value;
	request_key = std::make_tuple(actor_id, loom_id, request_id);
	cached_request = this->request_cache_.find(request_key);
	if (cached_request != this->request_cache_.end())
	{
		if (cached_request->second.payload != message)
		{
			json_free_groups(groups);
			return (FT_ERR_INVALID_ARGUMENT);
		}
		if (std::strcmp(action, "begin") == 0
			|| std::strcmp(action, "select") == 0
			|| std::strcmp(action, "unselect") == 0
			|| std::strcmp(action, "protect") == 0
			|| std::strcmp(action, "unprotect") == 0
			|| std::strcmp(action, "cancel") == 0
			|| std::strcmp(action, "confirm") == 0
			|| std::strcmp(action, "play_card") == 0
			|| std::strcmp(action, "discard_card") == 0
			|| std::strcmp(action, "draw_cards") == 0
			|| std::strcmp(action, "resolve_expired_choices") == 0
			|| std::strcmp(action, "choose_card_option") == 0)
		{
			result = this->authorize_station_access(actor_id, loom_id, message);
			if (result != FT_ERR_SUCCESS)
			{
				json_free_groups(groups);
				return (result);
			}
		}
		result = cached_request->second.completed
			? cached_request->second.result : FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS
			&& cached_request->second.has_card_play_result)
			cached_request->second.card_play_result.replayed = true;
		if (result == FT_ERR_SUCCESS
			&& cached_request->second.has_card_discard_result)
			cached_request->second.card_discard_result.replayed = true;
		if (result == FT_ERR_SUCCESS
			&& cached_request->second.has_card_draw_result)
			cached_request->second.card_draw_result.replayed = true;
		if (result == FT_ERR_SUCCESS
			&& cached_request->second.has_card_expiry_result)
			cached_request->second.card_expiry_replayed = true;
		if (result == FT_ERR_SUCCESS
			&& cached_request->second.has_card_choice_result)
			cached_request->second.card_choice_result.replayed = true;
		const int32_t checkpoint_result = this->commit_checkpoint();
		if (checkpoint_result != FT_ERR_SUCCESS)
			result = checkpoint_result;
		json_free_groups(groups);
		return (result);
	}
	if ((std::strcmp(action, "begin") == 0
		|| std::strcmp(action, "select") == 0
		|| std::strcmp(action, "unselect") == 0
		|| std::strcmp(action, "protect") == 0
		|| std::strcmp(action, "unprotect") == 0
		|| std::strcmp(action, "confirm") == 0)
		&& this->request_cache_.size()
			>= WorldRevisionServer::REQUEST_CACHE_LIMIT)
	{
		json_free_groups(groups);
		return (FT_ERR_FULL);
	}
	if (this->request_cache_.size() >= WorldRevisionServer::REQUEST_CACHE_LIMIT)
	{
		json_free_groups(groups);
		return (FT_ERR_FULL);
	}
	try
	{
		CachedRequest staged_request;
		staged_request.payload = message;
		staged_request.result = FT_ERR_INVALID_STATE;
		staged_request.completed = false;
		staged_request.has_quote = false;
		staged_request.quote = {};
		staged_request.has_instability_view = false;
		staged_request.instability_view = {};
		staged_request.has_revision_state = false;
		staged_request.revision_identifier = 0U;
		staged_request.selection_revision = 0U;
		staged_request.cost_policy_revision = 0U;
		staged_request.selected_count = 0U;
		staged_request.has_card_play_result = false;
		staged_request.card_play_result = {};
		staged_request.has_card_discard_result = false;
		staged_request.card_discard_result = {};
		staged_request.has_card_draw_result = false;
		staged_request.card_draw_result = {};
		staged_request.has_card_expiry_result = false;
		staged_request.card_expiry_resolved_count = 0U;
		staged_request.card_expiry_replayed = false;
		staged_request.has_card_choice_result = false;
		staged_request.card_choice_result = {};
		cached_request = this->request_cache_.emplace(request_key,
			std::move(staged_request)).first;
	}
	catch (...)
	{
		json_free_groups(groups);
		return (FT_ERR_NO_MEMORY);
	}
	if (std::strcmp(action, "begin") == 0
		|| std::strcmp(action, "select") == 0
		|| std::strcmp(action, "unselect") == 0
		|| std::strcmp(action, "protect") == 0
		|| std::strcmp(action, "unprotect") == 0
		|| std::strcmp(action, "confirm") == 0)
	{
		result = this->refresh_occupied_chunks();
		if (result != FT_ERR_SUCCESS)
		{
			this->request_cache_.erase(cached_request);
			json_free_groups(groups);
			return (result);
		}
	}
	result = FT_ERR_INVALID_ARGUMENT;
	if (std::strcmp(action, "begin") == 0)
	{
		if (this->world_->world_revision().pending
			|| this->world_->world_revision().regenerating)
			result = FT_ERR_INVALID_OPERATION;
		else
		{
			result = this->authorize_station_access(actor_id, loom_id,
				message);
			if (result == FT_ERR_SUCCESS)
				result = this->handle_begin_action(revision);
			if (result == FT_ERR_SUCCESS)
			{
				this->draft_actor_id_ = actor_id;
				this->draft_loom_id_ = loom_id;
				this->draft_owned_ = true;
				this->draft_start_committed_ = false;
			}
		}
	}
	else if (!this->draft_owned_ || actor_id != this->draft_actor_id_
		|| loom_id != this->draft_loom_id_)
		result = FT_ERR_PERMISSION_DENIED;
	else if (this->draft_start_committed_
		&& (std::strcmp(action, "play_card") == 0
			|| std::strcmp(action, "discard_card") == 0
			|| std::strcmp(action, "draw_cards") == 0
			|| std::strcmp(action, "resolve_expired_choices") == 0
			|| std::strcmp(action, "choose_card_option") == 0))
	{
		result = this->authorize_station_access(actor_id, loom_id, message);
		if (result == FT_ERR_SUCCESS
			&& std::strcmp(action, "play_card") == 0)
		{
			json_item *card_instance_item = json_find_item(revision,
				"card_instance_id");
			json_item *choice_kind_item = json_find_item(revision, "choice_kind");
			json_item *choice_primary_item = json_find_item(revision,
				"choice_primary");
			json_item *choice_secondary_item = json_find_item(revision,
				"choice_secondary");
			uint64_t card_instance_id = 0U;
			uint64_t choice_kind = 0U;
			uint64_t choice_primary = 0U;
			uint64_t choice_secondary = 0U;
			RegenerationCardChoice choice = {};
			RegenerationPolicyIntent intent = {};

			if (this->card_play_handler_ == nullptr)
				result = FT_ERR_INVALID_OPERATION;
			else if (card_instance_item == nullptr || choice_kind_item == nullptr
				|| choice_primary_item == nullptr
				|| choice_secondary_item == nullptr
				|| world_revision_parse_u64(card_instance_item->value,
					&card_instance_id) != FT_ERR_SUCCESS
				|| world_revision_parse_u64(choice_kind_item->value,
					&choice_kind) != FT_ERR_SUCCESS
				|| world_revision_parse_u64(choice_primary_item->value,
					&choice_primary) != FT_ERR_SUCCESS
				|| world_revision_parse_u64(choice_secondary_item->value,
					&choice_secondary) != FT_ERR_SUCCESS
				|| card_instance_id == 0U || card_instance_id > UINT32_MAX
				|| choice_kind > REGENERATION_CARD_CHOICE_TARGET_CHUNK
				|| choice_primary > UINT32_MAX || choice_secondary > UINT32_MAX)
				result = FT_ERR_INVALID_ARGUMENT;
			else
			{
				choice.kind = static_cast<RegenerationCardChoiceKind>(choice_kind);
				choice.value_primary = static_cast<uint32_t>(choice_primary);
				choice.value_secondary = static_cast<uint32_t>(choice_secondary);
				result = this->card_play_handler_(actor_id, loom_id, request_id,
					static_cast<uint32_t>(card_instance_id), choice, &intent,
					this->card_play_handler_user_data_);
				if (result == FT_ERR_SUCCESS
					&& (intent.request_id != request_id
						|| intent.card_instance_id != card_instance_id
						|| !RegenerationCardSession::is_supported_card_definition(
							intent.card_definition_id)
						|| intent.content_version
							!= RegenerationCardSession::CONTENT_VERSION
						|| intent.effect_kind
							!= RegenerationCardSession::effect_kind_for_card(
								intent.card_definition_id)
						|| intent.choice.kind != choice.kind
						|| intent.choice.value_primary != choice.value_primary
						|| intent.choice.value_secondary
							!= choice.value_secondary))
					result = FT_ERR_INVALID_STATE;
				if (result == FT_ERR_SUCCESS)
				{
					cached_request->second.has_card_play_result = true;
					cached_request->second.card_play_result = intent;
				}
			}
		}
		else if (result == FT_ERR_SUCCESS
			&& std::strcmp(action, "discard_card") == 0)
		{
			json_item *source_draw_item = json_find_item(revision,
				"source_draw_request_id");
			json_item *card_instance_item = json_find_item(revision,
				"card_instance_id");
			uint64_t source_draw_request_id = 0U;
			uint64_t card_instance_id = 0U;
			RegenerationCardActionResult card_result = {};

			if (this->card_discard_handler_ == nullptr)
				result = FT_ERR_INVALID_OPERATION;
			else if (source_draw_item == nullptr || card_instance_item == nullptr
				|| world_revision_parse_u64(source_draw_item->value,
					&source_draw_request_id) != FT_ERR_SUCCESS
				|| world_revision_parse_u64(card_instance_item->value,
					&card_instance_id) != FT_ERR_SUCCESS
				|| source_draw_request_id == 0U || card_instance_id == 0U
				|| card_instance_id > UINT32_MAX)
				result = FT_ERR_INVALID_ARGUMENT;
			else
			{
				result = this->card_discard_handler_(actor_id, loom_id,
					request_id, source_draw_request_id,
					static_cast<uint32_t>(card_instance_id), &card_result,
					this->card_discard_handler_user_data_);
				if (result == FT_ERR_SUCCESS
					&& (card_result.request_id != request_id
						|| card_result.count != 1U
						|| card_result.command_sequence == 0U))
					result = FT_ERR_INVALID_STATE;
				if (result == FT_ERR_SUCCESS)
				{
					cached_request->second.has_card_discard_result = true;
					cached_request->second.card_discard_result = card_result;
				}
			}
		}
		else if (result == FT_ERR_SUCCESS
			&& std::strcmp(action, "draw_cards") == 0)
		{
			json_item *source_intent_item = json_find_item(revision,
				"source_host_intent_sequence");
			json_item *draw_count_item = json_find_item(revision, "draw_count");
			json_item *discard_count_item = json_find_item(revision,
				"required_discard_count");
			uint64_t source_intent = 0U;
			uint64_t draw_count = 0U;
			uint64_t discard_count = 0U;
			RegenerationCardActionResult card_result = {};

			if (this->card_draw_handler_ == nullptr)
				result = FT_ERR_INVALID_OPERATION;
			else if (source_intent_item == nullptr || draw_count_item == nullptr
				|| discard_count_item == nullptr
				|| world_revision_parse_u64(source_intent_item->value,
					&source_intent) != FT_ERR_SUCCESS
				|| world_revision_parse_u64(draw_count_item->value,
					&draw_count) != FT_ERR_SUCCESS
				|| world_revision_parse_u64(discard_count_item->value,
					&discard_count) != FT_ERR_SUCCESS
				|| source_intent == 0U || draw_count == 0U
				|| draw_count > UINT32_MAX || discard_count > UINT32_MAX)
				result = FT_ERR_INVALID_ARGUMENT;
			else
			{
				result = this->card_draw_handler_(actor_id, loom_id, request_id,
					source_intent, static_cast<uint32_t>(draw_count),
					static_cast<uint32_t>(discard_count), &card_result,
					this->card_draw_handler_user_data_);
				if (result == FT_ERR_SUCCESS
					&& (card_result.request_id != request_id
						|| card_result.count != draw_count
						|| card_result.command_sequence == 0U))
					result = FT_ERR_INVALID_STATE;
				if (result == FT_ERR_SUCCESS)
				{
					cached_request->second.has_card_draw_result = true;
					cached_request->second.card_draw_result = card_result;
				}
			}
		}
		else if (result == FT_ERR_SUCCESS
			&& std::strcmp(action, "resolve_expired_choices") == 0)
		{
			uint32_t resolved_count = 0U;
			bool replayed = false;
			if (this->card_expiry_handler_ == nullptr)
				result = FT_ERR_INVALID_OPERATION;
			else
			{
				result = this->card_expiry_handler_(actor_id, loom_id,
					request_id, &resolved_count, &replayed,
					this->card_expiry_handler_user_data_);
				if (result == FT_ERR_SUCCESS && resolved_count > 4096U)
					result = FT_ERR_INVALID_STATE;
				if (result == FT_ERR_SUCCESS)
				{
					cached_request->second.has_card_expiry_result = true;
					cached_request->second.card_expiry_resolved_count =
						resolved_count;
					cached_request->second.card_expiry_replayed = replayed;
				}
			}
		}
		else if (result == FT_ERR_SUCCESS)
		{
			json_item *choice_id_item = json_find_item(revision, "choice_id");
			json_item *option_id_item = json_find_item(revision, "option_id");
			uint64_t choice_id = 0U;
			uint64_t option_id = 0U;
			RegenerationCardActionResult card_result = {};
			if (this->card_choice_handler_ == nullptr)
				result = FT_ERR_INVALID_OPERATION;
			else if (choice_id_item == nullptr || option_id_item == nullptr
				|| world_revision_parse_u64(choice_id_item->value,
					&choice_id) != FT_ERR_SUCCESS
				|| world_revision_parse_u64(option_id_item->value,
					&option_id) != FT_ERR_SUCCESS
				|| choice_id == 0U || choice_id > UINT32_MAX
				|| option_id == 0U || option_id > UINT32_MAX)
				result = FT_ERR_INVALID_ARGUMENT;
			else
			{
				result = this->card_choice_handler_(actor_id, loom_id,
					request_id, static_cast<uint32_t>(choice_id),
					static_cast<uint32_t>(option_id), &card_result,
					this->card_choice_handler_user_data_);
				if (result == FT_ERR_SUCCESS
					&& (card_result.request_id != request_id
						|| card_result.count != 1U
						|| card_result.command_sequence == 0U))
					result = FT_ERR_INVALID_STATE;
				if (result == FT_ERR_SUCCESS)
				{
					cached_request->second.has_card_choice_result = true;
					cached_request->second.card_choice_result = card_result;
				}
			}
		}
	}
	else if (this->draft_start_committed_)
		result = FT_ERR_INVALID_OPERATION;
	else if (std::strcmp(action, "select") == 0
		|| std::strcmp(action, "unselect") == 0
		|| std::strcmp(action, "protect") == 0
		|| std::strcmp(action, "unprotect") == 0)
	{
		result = this->authorize_station_access(actor_id, loom_id, message);
		if (result == FT_ERR_SUCCESS)
			result = this->handle_select_or_protect_action(revision, action);
	}
	else if (std::strcmp(action, "cancel") == 0)
	{
		result = this->authorize_station_access(actor_id, loom_id, message);
		if (result == FT_ERR_SUCCESS)
			result = this->world_->cancel_world_revision();
		if (result == FT_ERR_SUCCESS)
		{
			/* Retain the terminal owner so an authenticated reconnect can query
			 * NO_DRAFT status. A later successful begin replaces this identity. */
			this->draft_owned_ = true;
			this->draft_start_committed_ = false;
		}
	}
	else if (std::strcmp(action, "confirm") == 0)
	{
		json_item *selection_revision_item = json_find_item(revision,
			"selection_revision");
		json_item *policy_revision_item = json_find_item(revision,
			"policy_revision");
		json_item *opening_card_count_item = json_find_item(revision,
			"opening_card_count");
		uint64_t selection_revision = 0U;
		uint64_t policy_revision = 0U;
		uint64_t opening_card_count = 0U;

		result = this->authorize_station_access(actor_id, loom_id, message);
		if (result == FT_ERR_SUCCESS && this->start_committer_ == nullptr)
			result = FT_ERR_INVALID_OPERATION;
		else if (result == FT_ERR_SUCCESS && (selection_revision_item == nullptr
			|| policy_revision_item == nullptr || opening_card_count_item == nullptr
			|| world_revision_parse_u64(selection_revision_item->value,
				&selection_revision) != FT_ERR_SUCCESS
			|| world_revision_parse_u64(policy_revision_item->value,
				&policy_revision) != FT_ERR_SUCCESS
			|| world_revision_parse_u64(opening_card_count_item->value,
				&opening_card_count) != FT_ERR_SUCCESS
			|| opening_card_count < 4U || opening_card_count > 7U))
			result = FT_ERR_INVALID_ARGUMENT;
		else if (result == FT_ERR_SUCCESS)
			result = this->start_committer_(actor_id, loom_id, request_id,
				selection_revision, policy_revision,
				static_cast<uint32_t>(opening_card_count),
				this->start_committer_user_data_);
		if (result == FT_ERR_SUCCESS)
			this->draft_start_committed_ = true;
	}
	if (std::strcmp(action, "resolve_expired_choices") == 0
		&& result == FT_ERR_INVALID_OPERATION)
	{
		/* An early timeout poll must not consume its request ID; the exact same
		 * request can be retried when the host clock reaches a deadline. */
		this->request_cache_.erase(cached_request);
		json_free_groups(groups);
		return (result);
	}
	if (std::strcmp(action, "confirm") == 0
		&& (result == FT_ERR_IO || result == FT_ERR_NO_MEMORY))
	{
		/* A transient failure from the host start transaction must not consume
		 * this request ID when the prepared checkpoint was not committed. The
		 * GameSession callback restores its journal to the pre-request snapshot
		 * on that boundary, so an exact retry can safely attempt preparation. */
		this->request_cache_.erase(cached_request);
		json_free_groups(groups);
		return (result);
	}
	cached_request->second.result = result;
	cached_request->second.completed = true;
	{
		const World::WorldRevision resulting_revision =
			this->world_->world_revision();
		cached_request->second.has_revision_state = true;
		cached_request->second.revision_identifier =
			resulting_revision.identifier;
		cached_request->second.selection_revision =
			resulting_revision.selection_revision;
		cached_request->second.cost_policy_revision =
			resulting_revision.cost_policy_revision;
		cached_request->second.selected_count =
			static_cast<uint32_t>(resulting_revision.selected_count);
	}
	const int32_t checkpoint_result = this->commit_checkpoint();
	if (checkpoint_result != FT_ERR_SUCCESS)
		result = checkpoint_result;
	json_free_groups(groups);
	return (result);
}
