#include "RegenerationCardSession.hpp"

#include <new>

namespace
{
	static const std::size_t CARD_SESSION_MAX_BYTES = 64U * 1024U * 1024U;
	static const uint32_t CARD_SESSION_SERIAL_VERSION = 8U;
	static const uint32_t CARD_SESSION_CHECKSUM_BYTES = 8U;

	class card_session_writer
	{
	  public:
		std::vector<uint8_t> &bytes;

		explicit card_session_writer(std::vector<uint8_t> &output) noexcept
			: bytes(output) {}

		bool u8(uint8_t value)
		{
			if (bytes.size() >= CARD_SESSION_MAX_BYTES
				- CARD_SESSION_CHECKSUM_BYTES)
				return (false);
			bytes.push_back(value);
			return (true);
		}
		bool u32(uint32_t value)
		{
			for (uint32_t shift = 0U; shift < 32U; shift += 8U)
				if (!u8(static_cast<uint8_t>(value >> shift)))
					return (false);
			return (true);
		}
		bool i32(int32_t value)
		{
			return (u32(static_cast<uint32_t>(value)));
		}
		bool u64(uint64_t value)
		{
			for (uint32_t shift = 0U; shift < 64U; shift += 8U)
				if (!u8(static_cast<uint8_t>(value >> shift)))
					return (false);
			return (true);
		}
	};

	class card_session_reader
	{
	  public:
		const uint8_t *bytes;
		std::size_t size;
		std::size_t offset;

		card_session_reader(const uint8_t *input, std::size_t input_size) noexcept
			: bytes(input), size(input_size), offset(0U) {}

		bool u8(uint8_t *value) noexcept
		{
			if (value == nullptr || offset >= size)
				return (false);
			*value = bytes[offset++];
			return (true);
		}
		bool u32(uint32_t *value) noexcept
		{
			uint32_t result = 0U;
			uint8_t byte;

			if (value == nullptr)
				return (false);
			for (uint32_t shift = 0U; shift < 32U; shift += 8U)
			{
				if (!u8(&byte))
					return (false);
				result |= static_cast<uint32_t>(byte) << shift;
			}
			*value = result;
			return (true);
		}
		bool i32(int32_t *value) noexcept
		{
			uint32_t raw;

			if (value == nullptr || !u32(&raw))
				return (false);
			*value = static_cast<int32_t>(raw);
			return (true);
		}
		bool u64(uint64_t *value) noexcept
		{
			uint64_t result = 0U;
			uint8_t byte;

			if (value == nullptr)
				return (false);
			for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			{
				if (!u8(&byte))
					return (false);
				result |= static_cast<uint64_t>(byte) << shift;
			}
			*value = result;
			return (true);
		}
	};

	static uint64_t card_session_checksum(const uint8_t *bytes,
		std::size_t size) noexcept
	{
		uint64_t hash = 14695981039346656037ULL;

		for (std::size_t index = 0U; index < size; ++index)
		{
			hash ^= bytes[index];
			hash *= 1099511628211ULL;
		}
		return (hash);
	}

	static bool write_choice(card_session_writer &writer,
		const RegenerationCardChoice &choice)
	{
		return (writer.u32(static_cast<uint32_t>(choice.kind))
			&& writer.u32(choice.value_primary)
			&& writer.u32(choice.value_secondary));
	}

	static bool read_choice(card_session_reader &reader,
		RegenerationCardChoice *choice) noexcept
	{
		uint32_t kind;

		if (choice == nullptr || !reader.u32(&kind)
			|| kind > REGENERATION_CARD_CHOICE_TARGET_CHUNK
			|| !reader.u32(&choice->value_primary)
			|| !reader.u32(&choice->value_secondary))
			return (false);
		choice->kind = static_cast<RegenerationCardChoiceKind>(kind);
		return (true);
	}

	static bool write_policy_intent(card_session_writer &writer,
		const RegenerationPolicyIntent &intent)
	{
		return (writer.u64(intent.request_id)
			&& writer.u64(intent.host_intent_sequence)
			&& writer.u32(intent.content_version)
			&& writer.u32(intent.card_definition_id)
			&& writer.u32(intent.card_instance_id)
			&& writer.u32(static_cast<uint32_t>(intent.effect_kind))
			&& write_choice(writer, intent.choice)
			&& writer.u32(intent.no_effect_choice_mask)
			&& writer.u8(intent.replayed ? 1U : 0U));
	}

	static bool read_policy_intent(card_session_reader &reader,
		RegenerationPolicyIntent *intent, bool has_no_effect_mask) noexcept
	{
		uint32_t effect_kind;
		uint8_t replayed;

		if (intent == nullptr || !reader.u64(&intent->request_id)
			|| !reader.u64(&intent->host_intent_sequence)
			|| !reader.u32(&intent->content_version)
			|| !reader.u32(&intent->card_definition_id)
			|| !reader.u32(&intent->card_instance_id)
			|| !reader.u32(&effect_kind)
			|| effect_kind < REGENERATION_CARD_EFFECT_BIOME_EXCLUSION
			|| effect_kind > REGENERATION_CARD_EFFECT_SPIDER_NEST_SEAL
			|| !read_choice(reader, &intent->choice)
			|| (has_no_effect_mask
				&& !reader.u32(&intent->no_effect_choice_mask))
			|| !reader.u8(&replayed) || replayed > 1U)
			return (false);
		intent->effect_kind = static_cast<RegenerationCardEffectKind>(
			effect_kind);
		intent->replayed = replayed != 0U;
		return (true);
	}

	static bool write_action_result(card_session_writer &writer,
		const RegenerationCardActionResult &result)
	{
		return (writer.u64(result.session_id)
			&& writer.u64(result.request_id)
			&& writer.u64(result.command_sequence)
			&& writer.u32(result.count)
			&& writer.u8(result.replayed ? 1U : 0U));
	}

	static bool read_action_result(card_session_reader &reader,
		RegenerationCardActionResult *result,
		bool has_session_id) noexcept
	{
		uint8_t replayed;

		if (result == nullptr
			|| (has_session_id && !reader.u64(&result->session_id))
			|| !reader.u64(&result->request_id)
			|| !reader.u64(&result->command_sequence)
			|| !reader.u32(&result->count) || !reader.u8(&replayed)
			|| replayed > 1U)
			return (false);
		result->replayed = replayed != 0U;
		return (true);
	}

	static bool write_card(card_session_writer &writer,
		const card_game_deck_card &card)
	{
		return (writer.u32(card.instance_id) && writer.u32(card.card_id));
	}

	static bool read_card(card_session_reader &reader,
		card_game_deck_card *card) noexcept
	{
		return (card != nullptr && reader.u32(&card->instance_id)
			&& reader.u32(&card->card_id));
	}

	static bool write_transfer_result(card_session_writer &writer,
		const RegenerationCardTransferResult &result)
	{
		return (writer.u64(result.request_id)
			&& writer.u64(result.physical_item_instance_id)
			&& writer.u32(result.card_instance_id)
			&& writer.i32(result.inventory_slot) && writer.i32(result.status)
			&& writer.u8(result.replayed ? 1U : 0U));
	}

	static bool read_transfer_result(card_session_reader &reader,
		RegenerationCardTransferResult *result) noexcept
	{
		uint8_t replayed;

		if (result == nullptr || !reader.u64(&result->request_id)
			|| !reader.u64(&result->physical_item_instance_id)
			|| !reader.u32(&result->card_instance_id)
			|| !reader.i32(&result->inventory_slot)
			|| !reader.i32(&result->status) || !reader.u8(&replayed)
			|| replayed > 1U)
			return (false);
		result->replayed = replayed != 0U;
		return (true);
	}

	static bool write_item(card_session_writer &writer,
		const game_inventory_item_snapshot &item)
	{
		return (writer.i32(item.slot) && writer.i32(item.item_id)
			&& writer.u64(item.instance_id) && writer.i32(item.stack_size)
			&& writer.i32(item.max_stack) && writer.i32(item.rarity)
			&& writer.i32(item.width) && writer.i32(item.height)
			&& writer.i32(item.modifier1_id)
			&& writer.i32(item.modifier1_value)
			&& writer.i32(item.modifier2_id)
			&& writer.i32(item.modifier2_value)
			&& writer.i32(item.modifier3_id)
			&& writer.i32(item.modifier3_value)
			&& writer.i32(item.modifier4_id)
			&& writer.i32(item.modifier4_value));
	}

	static bool read_item(card_session_reader &reader,
		game_inventory_item_snapshot *item) noexcept
	{
		return (item != nullptr && reader.i32(&item->slot)
			&& reader.i32(&item->item_id) && reader.u64(&item->instance_id)
			&& reader.i32(&item->stack_size) && reader.i32(&item->max_stack)
			&& reader.i32(&item->rarity) && reader.i32(&item->width)
			&& reader.i32(&item->height)
			&& reader.i32(&item->modifier1_id)
			&& reader.i32(&item->modifier1_value)
			&& reader.i32(&item->modifier2_id)
			&& reader.i32(&item->modifier2_value)
			&& reader.i32(&item->modifier3_id)
			&& reader.i32(&item->modifier3_value)
			&& reader.i32(&item->modifier4_id)
			&& reader.i32(&item->modifier4_value));
	}
}

int32_t RegenerationCardSession::serialize_snapshot(
	std::vector<uint8_t> &bytes_out) const noexcept
{
	std::vector<uint8_t> engine_bytes;
	std::vector<uint8_t> script_context_bytes;
	std::vector<uint8_t> staged;
	card_session_writer writer(staged);
	uint64_t script_manifest_hash = 0U;
	int32_t result;

	if (!this->initialized_ || this->active_request_id_ != 0U
		|| this->starting_hand_count_ < 4U
		|| this->starting_hand_count_ > 7U
		|| this->play_cache_.size() > MAXIMUM_PLAY_REQUESTS
		|| this->draw_cache_.size() > MAXIMUM_PLAY_REQUESTS
		|| this->discard_cache_.size() > MAXIMUM_PLAY_REQUESTS
		|| this->expiry_cache_.size() > MAXIMUM_PLAY_REQUESTS
		|| this->play_cache_.size() + this->draw_cache_.size()
			+ this->discard_cache_.size() + this->expiry_cache_.size()
			> MAXIMUM_PLAY_REQUESTS
		|| this->card_import_cache_.size() > MAXIMUM_IMPORTED_CARD_REQUESTS
		|| this->card_export_cache_.size() > MAXIMUM_IMPORTED_CARD_REQUESTS
		|| this->physical_card_instances_.size() > MAXIMUM_DECK_SIZE)
		return (FT_ERR_INVALID_STATE);
	result = this->engine_.serialize_snapshot(engine_bytes);
	if (result == FT_ERR_SUCCESS)
		result = this->script_bridge_.get_manifest_hash(
			&script_manifest_hash);
	if (result == FT_ERR_SUCCESS)
		result = this->script_bridge_.serialize_context_values(
			script_context_bytes);
	if (result != FT_ERR_SUCCESS)
		return (result);
	try
	{
		staged.reserve(engine_bytes.size() + 512U);
		if (!writer.u8('R') || !writer.u8('C') || !writer.u8('S')
			|| !writer.u8('1') || !writer.u32(CARD_SESSION_SERIAL_VERSION)
			|| engine_bytes.size() > UINT32_MAX
			|| !writer.u32(static_cast<uint32_t>(engine_bytes.size()))
			|| !writer.u64(script_manifest_hash)
			|| !writer.u64(this->action_session_id_))
			return (FT_ERR_OUT_OF_RANGE);
		staged.insert(staged.end(), engine_bytes.begin(), engine_bytes.end());
		if (!writer.u64(this->highest_card_transfer_request_id_)
			|| !writer.u32(this->next_imported_card_instance_id_)
			|| !writer.u64(this->next_command_sequence_)
			|| !writer.u64(this->pending_double_draft_intent_sequence_)
			|| !writer.u32(this->pending_required_discard_count_)
			|| !writer.u64(this->active_request_id_)
			|| !write_choice(writer, this->active_choice_)
			|| !writer.u32(static_cast<uint32_t>(this->play_cache_.size())))
			return (FT_ERR_OUT_OF_RANGE);
		for (const auto &entry : this->play_cache_)
		{
			if (!writer.u64(entry.first)
				|| !writer.u32(entry.second.card_instance_id)
				|| !write_choice(writer, entry.second.choice)
				|| !writer.u32(entry.second.stabilizer_risk_basis_points)
				|| !writer.u32(entry.second.stabilizer_ordinal)
				|| !writer.u32(entry.second.catalyst_option_count)
				|| !writer.u8(entry.second.has_retry_payload ? 1U : 0U))
				return (FT_ERR_OUT_OF_RANGE);
			for (uint32_t option_index = 0U; option_index < 7U; ++option_index)
				if (!writer.u32(entry.second.catalyst_options[option_index]))
					return (FT_ERR_OUT_OF_RANGE);
			if (!write_policy_intent(writer, entry.second.result))
				return (FT_ERR_OUT_OF_RANGE);
		}
		if (!writer.u32(static_cast<uint32_t>(this->draw_cache_.size())))
			return (FT_ERR_OUT_OF_RANGE);
		for (const auto &entry : this->draw_cache_)
			if (!writer.u64(entry.first)
				|| !writer.u64(entry.second.source_host_intent_sequence)
				|| !writer.u32(entry.second.draw_count)
				|| !writer.u32(entry.second.required_discard_count)
				|| !write_action_result(writer, entry.second.result))
				return (FT_ERR_OUT_OF_RANGE);
		if (!writer.u32(static_cast<uint32_t>(this->discard_cache_.size())))
			return (FT_ERR_OUT_OF_RANGE);
		for (const auto &entry : this->discard_cache_)
			if (!writer.u64(entry.first)
				|| !writer.u64(entry.second.source_draw_request_id)
				|| !writer.u32(entry.second.card_instance_id)
				|| !write_action_result(writer, entry.second.result))
				return (FT_ERR_OUT_OF_RANGE);
		if (!writer.u32(static_cast<uint32_t>(this->card_import_cache_.size())))
			return (FT_ERR_OUT_OF_RANGE);
		for (const auto &entry : this->card_import_cache_)
			if (!writer.u64(entry.first)
				|| !writer.i32(entry.second.inventory_slot)
				|| !writer.u64(entry.second.expected_inventory_revision)
				|| !writer.u64(entry.second.physical_item_instance_id)
				|| !writer.i32(entry.second.item_definition_id)
				|| !writer.i32(entry.second.result_code)
				|| !write_card(writer, entry.second.result_card))
				return (FT_ERR_OUT_OF_RANGE);
		if (!writer.u32(static_cast<uint32_t>(this->card_export_cache_.size())))
			return (FT_ERR_OUT_OF_RANGE);
		for (const auto &entry : this->card_export_cache_)
			if (!writer.u64(entry.first)
				|| !writer.u32(entry.second.card_instance_id)
				|| !writer.u64(entry.second.expected_inventory_revision)
				|| !write_transfer_result(writer, entry.second.result))
				return (FT_ERR_OUT_OF_RANGE);
		if (!writer.u32(static_cast<uint32_t>(
			this->physical_card_instances_.size())))
			return (FT_ERR_OUT_OF_RANGE);
		for (const auto &entry : this->physical_card_instances_)
			if (!writer.u64(entry.first)
				|| !writer.u32(entry.second.card_instance_id)
				|| !write_item(writer, entry.second.item))
				return (FT_ERR_OUT_OF_RANGE);
		if (script_context_bytes.size() > UINT32_MAX
			|| !writer.u32(static_cast<uint32_t>(script_context_bytes.size())))
			return (FT_ERR_OUT_OF_RANGE);
		staged.insert(staged.end(), script_context_bytes.begin(),
			script_context_bytes.end());
		if (!writer.u64(this->starting_hand_request_id_)
			|| !writer.u32(this->starting_hand_count_))
			return (FT_ERR_OUT_OF_RANGE);
		if (!writer.u32(static_cast<uint32_t>(this->expiry_cache_.size())))
			return (FT_ERR_OUT_OF_RANGE);
		for (const auto &entry : this->expiry_cache_)
			if (!writer.u64(entry.first)
				|| !writer.u64(entry.second.authoritative_epoch)
				|| !writer.u64(entry.second.command_sequence)
				|| !writer.u32(entry.second.resolved_count))
				return (FT_ERR_OUT_OF_RANGE);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (staged.size() > CARD_SESSION_MAX_BYTES - CARD_SESSION_CHECKSUM_BYTES)
		return (FT_ERR_OUT_OF_RANGE);
	const uint64_t checksum = card_session_checksum(staged.data(), staged.size());
	try
	{
		for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			staged.push_back(static_cast<uint8_t>(checksum >> shift));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	bytes_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::restore_snapshot(const uint8_t *bytes,
	std::size_t byte_count, const uint8_t *script_catalog,
	uint32_t script_catalog_size, uint64_t trusted_script_manifest_hash,
	uint64_t policy_manifest_hash) noexcept
{
	card_session_reader reader(bytes, byte_count);
	std::map<uint64_t, CachedPlay> staged_plays;
	std::map<uint64_t, CachedDraw> staged_draws;
	std::map<uint64_t, CachedDiscard> staged_discards;
	std::map<uint64_t, CachedExpiry> staged_expiries;
	std::map<uint64_t, CachedCardImport> staged_imports;
	std::map<uint64_t, CachedCardExport> staged_exports;
	std::map<uint64_t, PhysicalCardLink> staged_physical_cards;
	std::map<uint32_t, uint64_t> staged_card_owners;
	std::vector<uint8_t> engine_bytes;
	std::vector<uint8_t> script_context_bytes;
	uint8_t signature[4];
	uint32_t version;
	uint32_t engine_size;
	uint64_t stored_script_manifest_hash = 0U;
	uint64_t stored_action_session_id = 0U;
	uint32_t script_context_size = 0U;
	uint32_t count;
	uint32_t index;
	uint64_t stored_checksum = 0U;
	uint64_t highest_transfer_request;
	uint32_t next_imported_instance;
	uint64_t next_command_sequence;
	uint64_t pending_intent_sequence;
	uint32_t pending_discard_count;
	uint64_t active_request;
	uint64_t starting_hand_request = 0U;
	uint32_t starting_hand_count = 4U;
	RegenerationCardChoice active_choice = {};
	int32_t result;

	if (this->initialized_ || bytes == nullptr
		|| byte_count < 4U + 4U + 4U + CARD_SESSION_CHECKSUM_BYTES
		|| byte_count > CARD_SESSION_MAX_BYTES)
		return (FT_ERR_INVALID_ARGUMENT);
	for (index = 0U; index < CARD_SESSION_CHECKSUM_BYTES; ++index)
		stored_checksum |= static_cast<uint64_t>(bytes[byte_count
			- CARD_SESSION_CHECKSUM_BYTES + index]) << (index * 8U);
	if (stored_checksum != card_session_checksum(bytes,
		byte_count - CARD_SESSION_CHECKSUM_BYTES))
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		for (index = 0U; index < 4U; ++index)
			if (!reader.u8(&signature[index]))
				return (FT_ERR_INVALID_ARGUMENT);
		if (signature[0] != 'R' || signature[1] != 'C'
			|| signature[2] != 'S' || signature[3] != '1'
			|| !reader.u32(&version)
			|| version == 0U || version > CARD_SESSION_SERIAL_VERSION
			|| !reader.u32(&engine_size)
			|| (version >= 4U
				&& !reader.u64(&stored_script_manifest_hash))
			|| (version >= 7U
				&& !reader.u64(&stored_action_session_id))
			|| (version >= 7U && this->action_session_id_ != 0U
				&& this->action_session_id_ != stored_action_session_id)
			|| engine_size > reader.size - reader.offset)
			return (FT_ERR_INVALID_ARGUMENT);
		engine_bytes.assign(bytes + reader.offset,
			bytes + reader.offset + engine_size);
		reader.offset += engine_size;
		if (!reader.u64(&highest_transfer_request)
			|| !reader.u32(&next_imported_instance)
			|| !reader.u64(&next_command_sequence)
			|| !reader.u64(&pending_intent_sequence)
			|| !reader.u32(&pending_discard_count)
			|| !reader.u64(&active_request)
			|| !read_choice(reader, &active_choice)
			|| active_request != 0U
			|| active_choice.kind != REGENERATION_CARD_CHOICE_NONE
			|| active_choice.value_primary != 0U
			|| active_choice.value_secondary != 0U
			|| next_imported_instance < 0x80000000U
			|| next_command_sequence == 0U
			|| pending_discard_count > MAXIMUM_HAND_SIZE
			|| !reader.u32(&count) || count > MAXIMUM_PLAY_REQUESTS)
			return (FT_ERR_INVALID_ARGUMENT);
		for (index = 0U; index < count; ++index)
		{
			uint64_t request_id;
			CachedPlay cached = {};
			uint8_t has_retry_payload = 0U;

			if (!reader.u64(&request_id) || request_id == 0U
				|| !reader.u32(&cached.card_instance_id)
				|| cached.card_instance_id == 0U
				|| !read_choice(reader, &cached.choice)
				|| (version >= 8U
					&& (!reader.u32(&cached.stabilizer_risk_basis_points)
						|| !reader.u32(&cached.stabilizer_ordinal)
						|| !reader.u32(&cached.catalyst_option_count)
						|| !reader.u8(&has_retry_payload)
						|| has_retry_payload > 1U))
				|| (version >= 8U && cached.catalyst_option_count > 7U))
				return (FT_ERR_INVALID_ARGUMENT);
			if (version >= 8U)
			{
				for (uint32_t option_index = 0U; option_index < 7U;
					++option_index)
					if (!reader.u32(&cached.catalyst_options[option_index]))
						return (FT_ERR_INVALID_ARGUMENT);
				cached.has_retry_payload = has_retry_payload != 0U;
			}
			if (!read_policy_intent(reader, &cached.result, version >= 5U)
				|| cached.result.request_id != request_id
				|| cached.result.card_instance_id != cached.card_instance_id
				|| cached.result.content_version != CONTENT_VERSION
				|| !RegenerationCardSession::is_supported_card_definition(
					cached.result.card_definition_id)
				|| cached.result.effect_kind != effect_kind_for_card(
					cached.result.card_definition_id)
				|| cached.result.choice.kind != cached.choice.kind
				|| cached.result.choice.value_primary
					!= cached.choice.value_primary
				|| cached.result.choice.value_secondary
					!= cached.choice.value_secondary
				|| (cached.result.card_definition_id
					!= RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID
				&& cached.result.no_effect_choice_mask != 0U)
				|| (cached.result.no_effect_choice_mask & ~3U) != 0U
				|| choice_is_valid(cached.result.card_definition_id,
					cached.choice) != FT_TRUE
				|| !staged_plays.emplace(request_id, cached).second)
				return (FT_ERR_INVALID_ARGUMENT);
		}
		if (!reader.u32(&count) || count > MAXIMUM_PLAY_REQUESTS)
			return (FT_ERR_INVALID_ARGUMENT);
		for (index = 0U; index < count; ++index)
		{
			uint64_t request_id;
			CachedDraw cached = {};

			if (!reader.u64(&request_id) || request_id == 0U
				|| !reader.u64(&cached.source_host_intent_sequence)
				|| cached.source_host_intent_sequence == 0U
				|| !reader.u32(&cached.draw_count)
				|| cached.draw_count == 0U || cached.draw_count > 100U
				|| !reader.u32(&cached.required_discard_count)
				|| cached.required_discard_count > MAXIMUM_HAND_SIZE
				|| !read_action_result(reader, &cached.result, version >= 7U)
				|| cached.result.request_id != request_id
				|| (version >= 7U
					&& cached.result.session_id != stored_action_session_id)
				|| cached.result.command_sequence == 0U
				|| cached.result.count != cached.draw_count
				|| !staged_draws.emplace(request_id, cached).second)
				return (FT_ERR_INVALID_ARGUMENT);
		}
		if (!reader.u32(&count) || count > MAXIMUM_PLAY_REQUESTS)
			return (FT_ERR_INVALID_ARGUMENT);
		for (index = 0U; index < count; ++index)
		{
			uint64_t request_id;
			CachedDiscard cached = {};

			if (!reader.u64(&request_id) || request_id == 0U
				|| !reader.u64(&cached.source_draw_request_id)
				|| cached.source_draw_request_id == 0U
				|| !reader.u32(&cached.card_instance_id)
				|| cached.card_instance_id == 0U
				|| !read_action_result(reader, &cached.result, version >= 7U)
				|| cached.result.request_id != request_id
				|| (version >= 7U
					&& cached.result.session_id != stored_action_session_id)
				|| cached.result.command_sequence == 0U
				|| cached.result.count != 1U
				|| !staged_discards.emplace(request_id, cached).second)
				return (FT_ERR_INVALID_ARGUMENT);
		}
		if (!reader.u32(&count) || count > MAXIMUM_IMPORTED_CARD_REQUESTS)
			return (FT_ERR_INVALID_ARGUMENT);
		for (index = 0U; index < count; ++index)
		{
			uint64_t request_id;
			CachedCardImport cached = {};

			if (!reader.u64(&request_id) || request_id == 0U
				|| !reader.i32(&cached.inventory_slot)
				|| cached.inventory_slot < 0
				|| !reader.u64(&cached.expected_inventory_revision)
				|| !reader.u64(&cached.physical_item_instance_id)
				|| !reader.i32(&cached.item_definition_id)
				|| !reader.i32(&cached.result_code)
				|| !read_card(reader, &cached.result_card)
				|| (cached.result_code == FT_ERR_SUCCESS
					&& (cached.physical_item_instance_id == 0U
						|| cached.result_card.instance_id == 0U
						|| cached.result_card.card_id == 0U
						|| RegenerationCardSession::card_definition_for_item(
							cached.item_definition_id)
							!= cached.result_card.card_id))
				|| !staged_imports.emplace(request_id, cached).second)
				return (FT_ERR_INVALID_ARGUMENT);
		}
		if (!reader.u32(&count) || count > MAXIMUM_IMPORTED_CARD_REQUESTS)
			return (FT_ERR_INVALID_ARGUMENT);
		for (index = 0U; index < count; ++index)
		{
			uint64_t request_id;
			CachedCardExport cached = {};

			if (!reader.u64(&request_id) || request_id == 0U
				|| !reader.u32(&cached.card_instance_id)
				|| cached.card_instance_id == 0U
				|| !reader.u64(&cached.expected_inventory_revision)
				|| !read_transfer_result(reader, &cached.result)
				|| cached.result.request_id != request_id
				|| cached.result.card_instance_id != cached.card_instance_id
				|| !staged_exports.emplace(request_id, cached).second)
				return (FT_ERR_INVALID_ARGUMENT);
		}
		if (!reader.u32(&count) || count > MAXIMUM_DECK_SIZE)
			return (FT_ERR_INVALID_ARGUMENT);
		for (index = 0U; index < count; ++index)
		{
			uint64_t physical_id;
			PhysicalCardLink link = {};

			if (!reader.u64(&physical_id) || physical_id == 0U
				|| !reader.u32(&link.card_instance_id)
				|| link.card_instance_id == 0U || !read_item(reader, &link.item)
				|| link.item.instance_id != physical_id
				|| link.item.stack_size != 1
				|| link.item.max_stack < 1
				|| RegenerationCardSession::card_definition_for_item(
					link.item.item_id) == 0U
				|| !staged_physical_cards.emplace(physical_id, link).second
				|| !staged_card_owners.emplace(link.card_instance_id,
					physical_id).second)
				return (FT_ERR_INVALID_ARGUMENT);
		}
		if (version >= 2U)
		{
			if (!reader.u32(&script_context_size)
				|| script_context_size < 20U
				|| reader.offset > reader.size - CARD_SESSION_CHECKSUM_BYTES
				|| script_context_size > reader.size
					- CARD_SESSION_CHECKSUM_BYTES - reader.offset)
				return (FT_ERR_INVALID_ARGUMENT);
			script_context_bytes.assign(bytes + reader.offset,
				bytes + reader.offset + script_context_size);
			reader.offset += script_context_size;
		}
		if (version >= 3U
			&& (!reader.u64(&starting_hand_request)
				|| !reader.u32(&starting_hand_count)
				|| starting_hand_count < 4U || starting_hand_count > 7U))
			return (FT_ERR_INVALID_ARGUMENT);
		if (version >= 6U)
		{
			if (!reader.u32(&count) || count > MAXIMUM_PLAY_REQUESTS)
				return (FT_ERR_INVALID_ARGUMENT);
			for (index = 0U; index < count; ++index)
			{
				uint64_t request_id = 0U;
				CachedExpiry cached = {};
				if (!reader.u64(&request_id) || request_id == 0U
					|| !reader.u64(&cached.authoritative_epoch)
					|| cached.authoritative_epoch == 0U
					|| !reader.u64(&cached.command_sequence)
					|| cached.command_sequence == 0U
					|| cached.command_sequence >= next_command_sequence
					|| !reader.u32(&cached.resolved_count)
					|| cached.resolved_count > FT_CARD_GAME_MAX_CHOICES
					|| !staged_expiries.emplace(request_id, cached).second)
					return (FT_ERR_INVALID_ARGUMENT);
			}
			for (const auto &entry : staged_expiries)
				if (staged_plays.find(entry.first) != staged_plays.end()
					|| staged_draws.find(entry.first) != staged_draws.end()
					|| staged_discards.find(entry.first)
						!= staged_discards.end())
					return (FT_ERR_INVALID_ARGUMENT);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (reader.offset != byte_count - CARD_SESSION_CHECKSUM_BYTES
		|| staged_imports.size() + staged_exports.size()
			> MAXIMUM_IMPORTED_CARD_REQUESTS
		|| staged_plays.size() + staged_draws.size()
			+ staged_discards.size() + staged_expiries.size()
			> MAXIMUM_PLAY_REQUESTS)
	{
		return (FT_ERR_INVALID_ARGUMENT);
	}
	for (const auto &entry : staged_imports)
		if (entry.first > highest_transfer_request)
			return (FT_ERR_INVALID_ARGUMENT);
	for (const auto &entry : staged_exports)
		if (entry.first > highest_transfer_request)
			return (FT_ERR_INVALID_ARGUMENT);
	result = this->initialize(4U, 1U);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (!script_context_bytes.empty())
	{
		const uint32_t context_count =
			static_cast<uint32_t>(script_context_bytes[8U])
			| (static_cast<uint32_t>(script_context_bytes[9U]) << 8U)
			| (static_cast<uint32_t>(script_context_bytes[10U]) << 16U)
			| (static_cast<uint32_t>(script_context_bytes[11U]) << 24U);
		if (script_context_bytes.size() < 20U
			|| script_context_bytes[0] != 'C'
			|| script_context_bytes[1] != 'C'
			|| script_context_bytes[2] != 'V'
			|| script_context_bytes[3] != '1'
			|| context_count > FT_CARD_GAME_SCRIPT_MAX_CONTEXT_KEYS
			|| script_context_bytes.size() != 20U
				+ static_cast<std::size_t>(context_count) * 16U)
		{
			(void)this->destroy();
			return (FT_ERR_INVALID_ARGUMENT);
		}
		uint32_t previous_key_id = 0U;
		for (index = 0U; index < context_count; ++index)
		{
			const std::size_t offset = 12U
				+ static_cast<std::size_t>(index) * 16U;
			const uint32_t key_id =
				static_cast<uint32_t>(script_context_bytes[offset])
				| (static_cast<uint32_t>(script_context_bytes[offset + 1U]) << 8U)
				| (static_cast<uint32_t>(script_context_bytes[offset + 2U]) << 16U)
				| (static_cast<uint32_t>(script_context_bytes[offset + 3U]) << 24U);

			if (key_id == 0U || key_id <= previous_key_id)
			{
				(void)this->destroy();
				return (FT_ERR_INVALID_ARGUMENT);
			}
			result = this->script_bridge_.register_context_key(key_id);
			if (result != FT_ERR_SUCCESS)
			{
				(void)this->destroy();
				return (result);
			}
			previous_key_id = key_id;
		}
		result = this->script_bridge_.restore_context_values(
			script_context_bytes.data(),
			static_cast<uint32_t>(script_context_bytes.size()));
		if (result != FT_ERR_SUCCESS)
		{
			(void)this->destroy();
			return (result);
		}
	}
	if (script_catalog != nullptr || script_catalog_size != 0U)
	{
		if (version < 4U || script_catalog == nullptr
			|| script_catalog_size == 0U
			|| trusted_script_manifest_hash == 0U)
		{
			(void)this->destroy();
			return (FT_ERR_INVALID_ARGUMENT);
		}
		result = this->load_script_catalog(script_catalog,
			script_catalog_size, trusted_script_manifest_hash,
			policy_manifest_hash);
		if (result != FT_ERR_SUCCESS)
		{
			(void)this->destroy();
			return (result);
		}
	}
	if (version >= 4U)
	{
		uint64_t current_manifest_hash = 0U;

		result = this->get_script_manifest_hash(&current_manifest_hash);
		if (result != FT_ERR_SUCCESS
			|| current_manifest_hash != stored_script_manifest_hash)
		{
			(void)this->destroy();
			return (result == FT_ERR_SUCCESS
				? FT_ERR_CONFIGURATION : result);
		}
	}
	result = this->engine_.restore_snapshot(engine_bytes.data(),
		engine_bytes.size());
	if (result != FT_ERR_SUCCESS)
	{
		(void)this->destroy();
		return (result);
	}
	for (const auto &entry : staged_physical_cards)
	{
		const game_inventory_item_snapshot &item = entry.second.item;
		const uint32_t definition_id = RegenerationCardSession::
			card_definition_for_item(item.item_id);
		bool found = false;
		bool import_record_found = false;

		for (const auto &import_entry : staged_imports)
			if (import_entry.second.result_code == FT_ERR_SUCCESS
				&& import_entry.second.physical_item_instance_id == entry.first
				&& import_entry.second.result_card.instance_id
					== entry.second.card_instance_id)
				import_record_found = true;

		if (definition_id == 0U || !import_record_found)
		{
			(void)this->destroy();
			return (FT_ERR_INVALID_ARGUMENT);
		}
		uint32_t deck_count = 0U;
		uint32_t hand_count = 0U;
		if (this->engine_.get_deck_count(PLAYER_ID, &deck_count)
			== FT_ERR_SUCCESS)
			for (uint32_t card_index = 0U; card_index < deck_count; ++card_index)
			{
				card_game_deck_card card = {};
				if (this->engine_.deck_inspect(PLAYER_ID, card_index, &card)
					== FT_ERR_SUCCESS
					&& card.instance_id == entry.second.card_instance_id
					&& card.card_id == definition_id)
					found = true;
			}
		if (this->engine_.get_hand_count(PLAYER_ID, &hand_count)
			== FT_ERR_SUCCESS)
			for (uint32_t card_index = 0U; card_index < hand_count; ++card_index)
			{
				card_game_deck_card card = {};
				if (this->engine_.hand_inspect(PLAYER_ID, card_index, &card)
					== FT_ERR_SUCCESS
					&& card.instance_id == entry.second.card_instance_id
					&& card.card_id == definition_id)
					found = true;
			}
		for (uint32_t zone_id = RESOLUTION_ZONE_ID;
			zone_id <= BURNED_ZONE_ID;
			++zone_id)
		{
			uint32_t zone_count = 0U;

			if (this->engine_.get_zone_count(PLAYER_ID, zone_id, &zone_count)
				!= FT_ERR_SUCCESS)
				continue;
			for (uint32_t card_index = 0U; card_index < zone_count;
				++card_index)
			{
				card_game_deck_card card = {};

				if (this->engine_.zone_inspect(PLAYER_ID, zone_id, card_index,
						&card) == FT_ERR_SUCCESS
					&& card.instance_id == entry.second.card_instance_id
					&& card.card_id == definition_id)
					found = true;
			}
		}
		if (!found)
		{
			(void)this->destroy();
			return (FT_ERR_INVALID_ARGUMENT);
		}
	}
	this->play_cache_.swap(staged_plays);
	this->draw_cache_.swap(staged_draws);
	this->discard_cache_.swap(staged_discards);
	this->expiry_cache_.swap(staged_expiries);
	this->card_import_cache_.swap(staged_imports);
	this->card_export_cache_.swap(staged_exports);
	this->physical_card_instances_.swap(staged_physical_cards);
	this->highest_card_transfer_request_id_ = highest_transfer_request;
	this->next_imported_card_instance_id_ = next_imported_instance;
	this->next_command_sequence_ = next_command_sequence;
	this->starting_hand_request_id_ = starting_hand_request;
	this->starting_hand_count_ = starting_hand_count;
	if (stored_action_session_id != 0U)
		this->action_session_id_ = stored_action_session_id;
	if (version < 7U)
	{
		for (auto &entry : this->draw_cache_)
			entry.second.result.session_id = this->action_session_id_;
		for (auto &entry : this->discard_cache_)
			entry.second.result.session_id = this->action_session_id_;
	}
	this->pending_double_draft_intent_sequence_ = pending_intent_sequence;
	this->pending_required_discard_count_ = pending_discard_count;
	this->active_request_id_ = 0U;
	this->active_choice_ = {};
	return (FT_ERR_SUCCESS);
}

