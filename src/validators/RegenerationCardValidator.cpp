#include "../../src/validators/RegenerationCardValidator.hpp"
#include "../../src/cards/RegenerationCardSession.hpp"
#include "../../src/cards/RegenerationWorldRules.hpp"
#include "../../src/inventory/MinecraftItemCatalog.hpp"

#include <cstdio>
#include <new>
#include <memory>

static uint64_t regeneration_card_snapshot_checksum(const uint8_t *bytes,
	std::size_t byte_count) noexcept;
static void regeneration_card_snapshot_write_u64(uint8_t *bytes,
	std::size_t offset, uint64_t value) noexcept;

static uint32_t regeneration_card_command_payload_u32(
	const uint8_t *payload, uint32_t offset) noexcept
{
	return (static_cast<uint32_t>(payload[offset])
		| (static_cast<uint32_t>(payload[offset + 1U]) << 8U)
		| (static_cast<uint32_t>(payload[offset + 2U]) << 16U)
		| (static_cast<uint32_t>(payload[offset + 3U]) << 24U));
}

int RegenerationCardValidator::validate_sequenced_choice_expiry() noexcept
{
	std::unique_ptr<RegenerationCardSession> source(
		new (std::nothrow) RegenerationCardSession());
	std::unique_ptr<RegenerationCardSession> restored(
		new (std::nothrow) RegenerationCardSession());
	std::vector<uint8_t> snapshot;
	std::vector<uint8_t> retry_snapshot;
	std::vector<uint8_t> restored_snapshot;
	std::vector<uint8_t> legacy_snapshot;
	uint32_t resolved_count = UINT32_MAX;
	const auto fail_stage = [](const char *stage) noexcept
	{
		std::fprintf(stderr,
			"regeneration-cards: sequenced choice expiry stage=%s failed\n",
			stage);
		return (1);
	};
	if (!source || !restored
		|| source->initialize(4U, 0x455850495259ULL) != FT_ERR_SUCCESS)
		return (fail_stage("initialize"));
	if (source->resolve_expired_choices(0x4558504952590001ULL,
			100U, &resolved_count) != FT_ERR_SUCCESS || resolved_count != 0U)
		return (fail_stage("initial-empty-resolution"));
	if (source->serialize_snapshot(snapshot) != FT_ERR_SUCCESS)
		return (fail_stage("initial-snapshot"));
	if (source->resolve_expired_choices(0x4558504952590001ULL,
			100U, &resolved_count) != FT_ERR_SUCCESS || resolved_count != 0U)
		return (fail_stage("same-request-retry"));
	if (source->serialize_snapshot(retry_snapshot) != FT_ERR_SUCCESS
		|| snapshot != retry_snapshot)
		return (fail_stage("retry-idempotency"));
	if (source->resolve_expired_choices(0x4558504952590001ULL,
			200U, &resolved_count) != FT_ERR_INVALID_ARGUMENT)
		return (fail_stage("changed-epoch-rejection"));
	if (source->serialize_snapshot(retry_snapshot) != FT_ERR_SUCCESS
		|| snapshot != retry_snapshot)
		return (fail_stage("rejection-atomicity"));
	if (restored->restore_snapshot(snapshot.data(), snapshot.size())
		!= FT_ERR_SUCCESS)
		return (fail_stage("snapshot-restore"));
	if (restored->serialize_snapshot(restored_snapshot) != FT_ERR_SUCCESS)
		return (fail_stage("restored-snapshot"));
	if (restored->resolve_expired_choices(0x4558504952590001ULL,
			100U, &resolved_count) != FT_ERR_SUCCESS || resolved_count != 0U)
		return (fail_stage("restored-same-request-retry"));
	if (restored->serialize_snapshot(retry_snapshot) != FT_ERR_SUCCESS
		|| restored_snapshot != retry_snapshot)
		return (fail_stage("restored-retry-idempotency"));
	if (restored->resolve_expired_choices(0x4558504952590001ULL,
			300U, &resolved_count) != FT_ERR_INVALID_ARGUMENT
		|| restored->serialize_snapshot(retry_snapshot) != FT_ERR_SUCCESS
		|| restored_snapshot != retry_snapshot)
		return (fail_stage("restored-changed-epoch-rejection"));
	try
	{
		legacy_snapshot = snapshot;
		if (legacy_snapshot.size() < 40U)
			return (1);
		legacy_snapshot.erase(legacy_snapshot.end() - 40,
			legacy_snapshot.end() - 8);
		legacy_snapshot.erase(legacy_snapshot.begin() + 20,
			legacy_snapshot.begin() + 28);
		legacy_snapshot[4U] = 5U;
		legacy_snapshot[5U] = 0U;
		legacy_snapshot[6U] = 0U;
		legacy_snapshot[7U] = 0U;
		regeneration_card_snapshot_write_u64(legacy_snapshot.data(),
			legacy_snapshot.size() - 8U,
			regeneration_card_snapshot_checksum(legacy_snapshot.data(),
				legacy_snapshot.size() - 8U));
	}
	catch (const std::bad_alloc &)
	{
		return (1);
	}
	if (restored->destroy() != FT_ERR_SUCCESS
		|| restored->restore_snapshot(legacy_snapshot.data(),
			legacy_snapshot.size()) != FT_ERR_SUCCESS)
		return (1);
	return (0);
}

int32_t RegenerationCardValidator::validate_stabilizer_roll(
	uint32_t risk_basis_points, uint32_t expected_result,
	uint64_t seed) noexcept
{
	RegenerationCardSession session;
	RegenerationCardSession restored;
	voxel_generation_config world_config;
	card_game_deck_card removed = {};
	card_game_deck_card stabilizer = {};
	RegenerationCardChoice choice = {};
	RegenerationPolicyIntent public_intent = {};
	RegenerationPolicyIntent first_private_intent = {};
	RegenerationPolicyIntent restored_private_intent = {};
	RegenerationPolicyIntent replayed_public_intent = {};
	card_game_command_record play_record = {};
	std::vector<uint8_t> snapshot;
	std::vector<uint8_t> before_retry;
	std::vector<uint8_t> changed_retry_snapshot;
	std::vector<uint8_t> after_retry;
	uint32_t hand_count = 0U;
	uint32_t command_record_count = 0U;
	int32_t result = voxel_default_generation_config(world_config);
	if (risk_basis_points > 10000U || expected_result > 1U
		|| result != FT_ERR_SUCCESS
		|| session.set_action_session_id(0x53544142434F4E54ULL)
			!= FT_ERR_SUCCESS
		|| session.initialize(4U, seed) != FT_ERR_SUCCESS
		|| session.get_hand_count(&hand_count) != FT_ERR_SUCCESS || hand_count == 0U)
		return (FT_ERR_INVALID_STATE);
	if (session.engine_.hand_inspect(RegenerationCardSession::PLAYER_ID,
			0U, &removed) != FT_ERR_SUCCESS
		|| session.engine_.hand_remove_instance(
			RegenerationCardSession::PLAYER_ID, removed.instance_id,
			nullptr) != FT_ERR_SUCCESS
		|| session.engine_.deck_push_top_instance(
			RegenerationCardSession::PLAYER_ID, 0x7FFFFFFEU,
			RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
			!= FT_ERR_SUCCESS
		|| session.engine_.draw_to_hand(RegenerationCardSession::PLAYER_ID,
			&stabilizer) != FT_ERR_SUCCESS
		|| stabilizer.card_id
			!= RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
	{
		std::fprintf(stderr, "stabilizer validator: card setup failed risk=%u\n",
			risk_basis_points);
		return (FT_ERR_INVALID_STATE);
	}
	result = session.play_card(0x5354414200000001ULL, stabilizer.instance_id,
		choice, world_config, &public_intent, risk_basis_points, 1U);
	if (result != FT_ERR_SUCCESS || public_intent.private_result != 0U
		|| public_intent.private_ordinal != 0U
		|| session.peek_pending_card_intent(&first_private_intent)
			!= FT_ERR_SUCCESS
		|| first_private_intent.private_result != expected_result
		|| first_private_intent.private_ordinal != 1U
		|| session.serialize_snapshot(snapshot) != FT_ERR_SUCCESS
		|| session.engine_.get_command_record_count(&command_record_count)
			!= FT_ERR_SUCCESS
		|| command_record_count == 0U
		|| session.engine_.get_command_record(command_record_count - 1U,
			&play_record) != FT_ERR_SUCCESS
		|| play_record.command.type != CARD_GAME_INTENT_PLAY_EFFECT
		|| play_record.command.source_instance_id != stabilizer.instance_id
		|| play_record.command.card_id
			!= RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID
		|| play_record.command.destination_zone_id
			!= RegenerationCardSession::DISCARD_ZONE_ID
		|| play_record.session_id != 0x53544142434F4E54ULL
		|| play_record.request_id != 0x5354414200000001ULL
		|| regeneration_card_command_payload_u32(
			play_record.command.play_payload, 20U)
			!= risk_basis_points
		|| regeneration_card_command_payload_u32(
			play_record.command.play_payload, 60U)
			!= 0x52435031U)
	{
		std::fprintf(stderr,
			"stabilizer validator: play/restore failed risk=%u result=%d public=(%u,%u) private=(%u,%u)\n",
			risk_basis_points, result, public_intent.private_result,
			public_intent.private_ordinal, first_private_intent.private_result,
			first_private_intent.private_ordinal);
		return (FT_ERR_INVALID_STATE);
	}
	result = restored.restore_snapshot(snapshot.data(), snapshot.size());
	if (result != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"stabilizer validator: restore failed risk=%u error=%d bytes=%zu\n",
			risk_basis_points, result, snapshot.size());
		return (FT_ERR_INVALID_STATE);
	}
	result = restored.peek_pending_card_intent(&restored_private_intent);
	if (result != FT_ERR_SUCCESS
		|| restored_private_intent.request_id != first_private_intent.request_id
		|| restored_private_intent.private_result
			!= first_private_intent.private_result
		|| restored_private_intent.private_ordinal
			!= first_private_intent.private_ordinal
		|| restored.serialize_snapshot(before_retry) != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"stabilizer validator: restored pending intent failed risk=%u error=%d private=(%u,%u) expected=(%u,%u)\n",
			risk_basis_points, result, restored_private_intent.private_result,
			restored_private_intent.private_ordinal,
			first_private_intent.private_result,
			first_private_intent.private_ordinal);
		return (FT_ERR_INVALID_STATE);
	}
	const uint32_t changed_risk = risk_basis_points == 10000U
		? 9999U : risk_basis_points + 1U;
	result = restored.play_card(0x5354414200000001ULL, stabilizer.instance_id,
		choice, world_config, &replayed_public_intent, changed_risk, 1U);
	if (result != FT_ERR_INVALID_ARGUMENT
		|| restored.serialize_snapshot(changed_retry_snapshot)
			!= FT_ERR_SUCCESS
		|| changed_retry_snapshot != before_retry)
		return (FT_ERR_INVALID_STATE);
	result = restored.play_card(0x5354414200000001ULL, stabilizer.instance_id,
		choice, world_config, &replayed_public_intent, risk_basis_points, 1U);
	if (result != FT_ERR_SUCCESS || !replayed_public_intent.replayed
		|| replayed_public_intent.private_result != 0U
		|| replayed_public_intent.private_ordinal != 0U
		|| restored.peek_pending_card_intent(&restored_private_intent)
			!= FT_ERR_SUCCESS
		|| restored_private_intent.private_result != expected_result
		|| restored.serialize_snapshot(after_retry) != FT_ERR_SUCCESS
		|| before_retry != after_retry)
	{
		std::fprintf(stderr,
			"stabilizer validator: retry changed outcome risk=%u result=%d expected=%u got=%u replay=%u\n",
			risk_basis_points, result, expected_result,
			restored_private_intent.private_result,
			static_cast<uint32_t>(replayed_public_intent.replayed));
		return (FT_ERR_INVALID_STATE);
	}
	return (FT_ERR_SUCCESS);
}

static uint64_t regeneration_card_snapshot_checksum(const uint8_t *bytes,
	std::size_t byte_count) noexcept
{
	uint64_t checksum = 14695981039346656037ULL;
	for (std::size_t index = 0U; index < byte_count; ++index)
	{
		checksum ^= bytes[index];
		checksum *= 1099511628211ULL;
	}
	return (checksum);
}

static uint32_t regeneration_card_snapshot_read_u32(const uint8_t *bytes,
	std::size_t offset) noexcept
{
	return (static_cast<uint32_t>(bytes[offset])
		| (static_cast<uint32_t>(bytes[offset + 1U]) << 8U)
		| (static_cast<uint32_t>(bytes[offset + 2U]) << 16U)
		| (static_cast<uint32_t>(bytes[offset + 3U]) << 24U));
}

static void regeneration_card_snapshot_write_u64(uint8_t *bytes,
	std::size_t offset, uint64_t value) noexcept
{
	for (uint32_t index = 0U; index < 8U; ++index)
	{
		bytes[offset + index] = static_cast<uint8_t>(value);
		value >>= 8U;
	}
}

static int32_t regeneration_card_make_inventory_item(game_inventory &inventory,
	int32_t item_id, uint64_t instance_id)
{
	ft_sharedptr<game_item> item;
	int32_t error_code = item.initialize(new (std::nothrow) game_item());
	if (error_code == FT_ERR_SUCCESS)
		error_code = item->initialize();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	item->set_item_id(item_id);
	item->set_stack_size(1);
	item->set_max_stack(1);
	item->set_width(1);
	item->set_height(1);
	item->set_instance_id(instance_id);
	return (inventory.add_item(item));
}

static int32_t regeneration_card_find_inventory_slot(
	game_inventory &inventory, int32_t item_id, uint64_t instance_id,
	int32_t *slot_out)
{
	game_inventory_snapshot snapshot = {};
	if (slot_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*slot_out = -1;
	int32_t error_code = inventory.get_snapshot(snapshot);
	for (const game_inventory_item_snapshot &entry : snapshot.items)
	{
		if (error_code != FT_ERR_SUCCESS)
			break ;
		if (entry.item_id == item_id && entry.instance_id == instance_id)
		{
			*slot_out = entry.slot;
			return (FT_ERR_SUCCESS);
		}
	}
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (FT_ERR_NOT_FOUND);
}

static int32_t regeneration_card_validate_previews(void)
{
	static const uint32_t supported_ids[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U,
		RegenerationCardSession::ORE_OVERGROWTH_CARD_DEFINITION_ID,
		RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID,
		RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID,
		RegenerationCardSession::FORTUNES_DRAW_CARD_DEFINITION_ID,
		RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID,
		RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID,
		RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID,
		RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID,
		RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID,
		RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID,
		RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID,
		RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID};
	RegenerationCardPreview preview = {};
	std::size_t index = 0U;

	while (index < sizeof(supported_ids) / sizeof(supported_ids[0]))
	{
		if (RegenerationCardSession::get_card_preview(supported_ids[index],
				&preview) != FT_ERR_SUCCESS
			|| preview.card_definition_id != supported_ids[index]
			|| preview.title == nullptr || preview.title[0] == '\0'
			|| preview.description == nullptr || preview.description[0] == '\0'
			|| preview.choice_kind < REGENERATION_CARD_CHOICE_NONE
			|| preview.choice_kind > REGENERATION_CARD_CHOICE_TARGET_CHUNK
			|| preview.choice_count > 2U
			|| (supported_ids[index]
				== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID
				&& preview.required_chunk_width != 9U)
			|| (supported_ids[index]
				!= RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID
				&& preview.required_chunk_width != 0U)
			|| ((preview.choice_kind == REGENERATION_CARD_CHOICE_NONE)
				!= (preview.choice_count == 0U))
			|| (supported_ids[index]
				== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID
				&& preview.choice_count != 2U)
			|| (supported_ids[index]
				!= RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID
				&& preview.choice_kind != REGENERATION_CARD_CHOICE_NONE
				&& preview.choice_count != 1U))
			return (FT_ERR_INVALID_STATE);
		++index;
	}
	preview.card_definition_id = 0xA55AU;
	preview.title = "unchanged";
	preview.description = "unchanged";
	if (RegenerationCardSession::get_card_preview(8U, &preview)
		!= FT_ERR_NOT_FOUND || preview.card_definition_id != 0xA55AU
		|| preview.title[0] != 'u' || preview.description[0] != 'u')
		return (FT_ERR_INVALID_STATE);
	return (FT_ERR_SUCCESS);
}

static int32_t regeneration_card_validate_starting_hand_deal(void)
{
	RegenerationCardSession session;
	RegenerationCardSession restored_session;
	RegenerationCardActionResult dealt = {};
	RegenerationCardActionResult replayed = {};
	std::vector<uint8_t> snapshot;
	std::vector<uint8_t> restored_snapshot;
	uint32_t hand_count = 0U;
	uint32_t draw_count = 0U;
	int32_t result = session.initialize(4U, 0x5354415254444541ULL);

	if (result == FT_ERR_SUCCESS)
		result = session.deal_starting_hand(8801U, 6U, &dealt);
	if (result == FT_ERR_SUCCESS)
		result = session.get_hand_count(&hand_count);
	if (result == FT_ERR_SUCCESS)
		result = session.get_draw_count(&draw_count);
	if (result == FT_ERR_SUCCESS
		&& (hand_count != 6U || draw_count != 14U || dealt.count != 6U
			|| dealt.replayed))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = session.deal_starting_hand(8801U, 6U, &replayed);
	if (result == FT_ERR_SUCCESS
		&& (!replayed.replayed || replayed.count != 6U))
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = session.serialize_snapshot(snapshot);
	if (result == FT_ERR_SUCCESS)
		result = restored_session.restore_snapshot(snapshot.data(),
			snapshot.size());
	if (result == FT_ERR_SUCCESS)
		result = restored_session.serialize_snapshot(restored_snapshot);
	if (result == FT_ERR_SUCCESS && restored_snapshot != snapshot)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = restored_session.deal_starting_hand(8801U, 6U, &replayed);
	if (result == FT_ERR_SUCCESS
		&& (!replayed.replayed
			|| restored_session.get_hand_count(&hand_count) != FT_ERR_SUCCESS
			|| hand_count != 6U
			|| restored_session.deal_starting_hand(8802U, 6U, &replayed)
				!= FT_ERR_INVALID_ARGUMENT))
		result = FT_ERR_INVALID_STATE;
	(void)restored_session.destroy();
	(void)session.destroy();
	return (result);
}

static int32_t regeneration_card_validate_physical_item_import(void)
{
	std::unique_ptr<RegenerationCardSession> session(new (std::nothrow) RegenerationCardSession());
	std::unique_ptr<RegenerationCardSession> restored_session(new (std::nothrow) RegenerationCardSession());
	std::unique_ptr<RegenerationCardSession> exported_restored_session(new (std::nothrow) RegenerationCardSession());
	std::unique_ptr<RegenerationCardSession> legacy_source_session(new (std::nothrow) RegenerationCardSession());
	std::unique_ptr<RegenerationCardSession> legacy_restored_session(new (std::nothrow) RegenerationCardSession());
	game_inventory inventory;
	game_inventory full_inventory;
	card_game_deck_card imported = {};
	card_game_deck_card rare_imported = {};
	card_game_deck_card volcano_imported = {};
	card_game_deck_card replayed = {};
	RegenerationCardTransferResult exported = {};
	RegenerationCardTransferResult replayed_export = {};
	std::vector<uint8_t> session_snapshot;
	std::vector<uint8_t> restored_session_snapshot;
	std::vector<uint8_t> legacy_source_snapshot;
	std::vector<uint8_t> legacy_snapshot;
	uint64_t revision = 0U;
	uint64_t revision_after = 0U;
	uint64_t import_revision = 0U;
	uint64_t export_revision = 0U;
	std::vector<uint64_t> physical_item_ids;
	uint32_t deck_count = 0U;
	uint32_t mapped_instance = 0U;
	int32_t rare_slot = -1;
	int32_t volcano_slot = -1;
	int32_t biome_slot = -1;
	int32_t biome_import_result = FT_ERR_INVALID_STATE;
	int32_t rare_add_result;
	int32_t rare_import_result;
	if (!session || !restored_session || !exported_restored_session
		|| !legacy_source_session || !legacy_restored_session)
		return (FT_ERR_NO_MEMORY);
	int32_t error_code = session->initialize(4U, 0x504859534943414CULL);
	if (error_code == FT_ERR_SUCCESS)
		error_code = session->register_script_context_key(17U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = session->set_script_context_value(17U, -23);
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.initialize(4U, 0);
	if (error_code == FT_ERR_SUCCESS)
		error_code = regeneration_card_make_inventory_item(inventory,
			MinecraftItemCatalog::VILLAGE_CHARTER_CARD_ITEM_ID, 9001U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.get_revision(&revision);
	import_revision = revision;
	if (error_code == FT_ERR_SUCCESS)
		error_code = session->import_card_item_to_deck(inventory, 0, revision,
			1001U, &imported);
	if (error_code != FT_ERR_SUCCESS || imported.card_id != 3U
		|| imported.instance_id == 0U
		|| imported.instance_id == 9001U
		|| inventory.count_item(MinecraftItemCatalog::VILLAGE_CHARTER_CARD_ITEM_ID)
			!= 0
		|| session->resolve_physical_card_instance(9001U, &mapped_instance)
			!= FT_ERR_SUCCESS
		|| mapped_instance != imported.instance_id
		|| session->get_draw_count(&deck_count) != FT_ERR_SUCCESS
		|| deck_count != 17U)
	{
		std::fprintf(stderr,
			"physical-import: result=%d def=%u instance=%u inv=%d deck=%u\n",
			error_code, imported.card_id, imported.instance_id,
			inventory.count_item(MinecraftItemCatalog::VILLAGE_CHARTER_CARD_ITEM_ID),
			deck_count);
		return (FT_ERR_INVALID_STATE);
	}
	error_code = inventory.get_revision(&revision_after);
	if (error_code != FT_ERR_SUCCESS
		|| session->import_card_item_to_deck(inventory, 0, revision,
			1001U, &replayed) != FT_ERR_SUCCESS
		|| replayed.instance_id != imported.instance_id
		|| replayed.card_id != imported.card_id
		|| session->get_draw_count(&deck_count) != FT_ERR_SUCCESS
		|| deck_count != 17U
		|| inventory.get_revision(&revision) != FT_ERR_SUCCESS
		|| revision != revision_after
		|| session->import_card_item_to_deck(inventory, 1, revision_after,
			1001U, &replayed) != FT_ERR_INVALID_ARGUMENT)
	{
		std::fprintf(stderr, "physical-import: exact retry/reuse failed\n");
		return (FT_ERR_INVALID_STATE);
	}
	error_code = regeneration_card_make_inventory_item(inventory,
		MinecraftItemCatalog::ORE_OVERGROWTH_CARD_ITEM_ID, 9002U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = regeneration_card_find_inventory_slot(inventory,
			MinecraftItemCatalog::ORE_OVERGROWTH_CARD_ITEM_ID, 9002U,
			&rare_slot);
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.get_revision(&revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = session->import_card_item_to_deck(inventory, rare_slot, revision,
			1002U, &rare_imported);
	if (error_code != FT_ERR_SUCCESS
		|| rare_imported.card_id
			!= RegenerationCardSession::ORE_OVERGROWTH_CARD_DEFINITION_ID
		|| rare_imported.instance_id == 0U
		|| inventory.count_item(MinecraftItemCatalog::ORE_OVERGROWTH_CARD_ITEM_ID)
			!= 0
		|| session->resolve_physical_card_instance(9002U, &mapped_instance)
			!= FT_ERR_SUCCESS
		|| mapped_instance != rare_imported.instance_id
		|| session->get_draw_count(&deck_count) != FT_ERR_SUCCESS
		|| deck_count != 18U)
	{
		std::fprintf(stderr,
			"rare-card-import: result=%d def=%u instance=%u deck=%u\n",
			error_code, rare_imported.card_id, rare_imported.instance_id,
			deck_count);
		return (FT_ERR_INVALID_STATE);
	}
	error_code = regeneration_card_make_inventory_item(inventory,
		MinecraftItemCatalog::VOLCANO_MARK_CARD_ITEM_ID, 9003U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = regeneration_card_find_inventory_slot(inventory,
			MinecraftItemCatalog::VOLCANO_MARK_CARD_ITEM_ID, 9003U,
			&volcano_slot);
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.get_revision(&revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = session->import_card_item_to_deck(inventory, volcano_slot, revision,
			1003U, &volcano_imported);
	if (error_code != FT_ERR_SUCCESS
		|| volcano_imported.card_id
			!= RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID
		|| volcano_imported.instance_id == 0U
		|| inventory.count_item(MinecraftItemCatalog::VOLCANO_MARK_CARD_ITEM_ID)
			!= 0
		|| session->resolve_physical_card_instance(9003U, &mapped_instance)
			!= FT_ERR_SUCCESS
		|| mapped_instance != volcano_imported.instance_id
		|| session->get_draw_count(&deck_count) != FT_ERR_SUCCESS
		|| deck_count != 19U)
	{
		std::fprintf(stderr, "volcano import: result=%d definition=%u instance=%u mapped=%u deck=%u\n",
			error_code, volcano_imported.card_id, volcano_imported.instance_id,
			mapped_instance, deck_count);
		return (FT_ERR_INVALID_STATE);
	}
	error_code = session->copy_physical_item_instance_ids(physical_item_ids);
	if (error_code != FT_ERR_SUCCESS || physical_item_ids.size() != 3U
		|| physical_item_ids[0] != 9001U || physical_item_ids[1] != 9002U
		|| physical_item_ids[2] != 9003U)
		return (FT_ERR_INVALID_STATE);
	error_code = session->serialize_snapshot(session_snapshot);
	if (error_code == FT_ERR_SUCCESS)
		error_code = restored_session->restore_snapshot(session_snapshot.data(),
			session_snapshot.size());
	int32_t restored_mapping_result = FT_ERR_INVALID_STATE;
	int32_t restored_import_result = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		restored_mapping_result = restored_session->resolve_physical_card_instance(
			9001U, &mapped_instance);
		restored_import_result = restored_session->import_card_item_to_deck(
			inventory, 0, import_revision, 1001U, &replayed);
		if (restored_mapping_result != FT_ERR_SUCCESS
			|| mapped_instance != imported.instance_id
			|| restored_import_result != FT_ERR_SUCCESS
			|| replayed.instance_id != imported.instance_id
			|| replayed.card_id != imported.card_id
			|| restored_session->serialize_snapshot(
				restored_session_snapshot) != FT_ERR_SUCCESS
			|| restored_session_snapshot != session_snapshot)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = legacy_source_session->initialize(4U,
			0x4C4547414359534EU);
		if (error_code == FT_ERR_SUCCESS)
			error_code = legacy_source_session->register_script_context_key(17U);
		if (error_code == FT_ERR_SUCCESS)
			error_code = legacy_source_session->set_script_context_value(17U, 42);
		if (error_code == FT_ERR_SUCCESS)
			error_code = legacy_source_session->serialize_snapshot(
				legacy_source_snapshot);
		if (error_code == FT_ERR_SUCCESS)
		{
			if (legacy_source_snapshot.size() < 12U)
				error_code = FT_ERR_INVALID_STATE;
			else
			{
				/* Strip the empty v6 expiry-cache count before deriving v1. */
				legacy_source_snapshot.erase(
					legacy_source_snapshot.end() - 12,
					legacy_source_snapshot.end() - 8);
				regeneration_card_snapshot_write_u64(
					legacy_source_snapshot.data(),
					legacy_source_snapshot.size() - 8U,
					regeneration_card_snapshot_checksum(
						legacy_source_snapshot.data(),
						legacy_source_snapshot.size() - 8U));
			}
		}
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		const std::size_t checksum_bytes = 8U;
		const std::size_t starting_deal_bytes = 12U;

		if (legacy_source_snapshot.size()
			< checksum_bytes + starting_deal_bytes + 24U)
			error_code = FT_ERR_INVALID_STATE;
		else
		{
			std::size_t found_context = legacy_source_snapshot.size();
			for (std::size_t offset = 0U; offset + 4U
				<= legacy_source_snapshot.size(); ++offset)
				if (legacy_source_snapshot[offset] == 'C'
					&& legacy_source_snapshot[offset + 1U] == 'C'
					&& legacy_source_snapshot[offset + 2U] == 'V'
					&& legacy_source_snapshot[offset + 3U] == '1')
					found_context = offset;
			if (found_context == legacy_source_snapshot.size()
				|| found_context < 4U)
				error_code = FT_ERR_INVALID_STATE;
			const std::size_t context_start = found_context;
			const std::size_t context_length_offset = context_start - 4U;
			const uint32_t context_bytes = error_code == FT_ERR_SUCCESS
				? regeneration_card_snapshot_read_u32(
					legacy_source_snapshot.data(), context_length_offset) : 0U;

			if (error_code == FT_ERR_SUCCESS && (context_bytes < 20U
				|| context_start + context_bytes + starting_deal_bytes
					+ checksum_bytes != legacy_source_snapshot.size()
				|| legacy_source_snapshot[context_start] != 'C'
				|| legacy_source_snapshot[context_start + 1U] != 'C'
				|| legacy_source_snapshot[context_start + 2U] != 'V'
				|| legacy_source_snapshot[context_start + 3U] != '1'
				|| regeneration_card_snapshot_read_u32(
					legacy_source_snapshot.data(), context_length_offset)
					!= context_bytes))
				error_code = FT_ERR_INVALID_STATE;
			else if (error_code == FT_ERR_SUCCESS)
			{
				legacy_snapshot = legacy_source_snapshot;
				legacy_snapshot.erase(legacy_snapshot.begin()
					+ static_cast<std::ptrdiff_t>(context_length_offset),
					legacy_snapshot.end()
					- static_cast<std::ptrdiff_t>(checksum_bytes));
				legacy_snapshot[4U] = 1U;
				legacy_snapshot[5U] = 0U;
				legacy_snapshot[6U] = 0U;
				legacy_snapshot[7U] = 0U;
				/* RCS1 v1 predates the v4 manifest and v7 session IDs. */
				legacy_snapshot.erase(legacy_snapshot.begin() + 12,
					legacy_snapshot.begin() + 28);
				regeneration_card_snapshot_write_u64(legacy_snapshot.data(),
					legacy_snapshot.size() - checksum_bytes,
					regeneration_card_snapshot_checksum(legacy_snapshot.data(),
						legacy_snapshot.size() - checksum_bytes));
				error_code = legacy_restored_session->restore_snapshot(
					legacy_snapshot.data(), legacy_snapshot.size());
				if (error_code == FT_ERR_SUCCESS
					&& legacy_restored_session->get_draw_count(&deck_count)
						!= FT_ERR_SUCCESS)
				{
					std::fprintf(stderr, "legacy probe draw count failed\n");
					error_code = FT_ERR_INVALID_STATE;
				}
			}
		}
	}
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "physical-import: session snapshot restore failed=%d map=%d import=%d mapped=%u imported=%u replayed=%u/%u revisions=%llu/%llu\n",
			error_code, restored_mapping_result, restored_import_result,
			mapped_instance, imported.instance_id, replayed.instance_id,
			replayed.card_id,
			static_cast<unsigned long long>(import_revision),
			static_cast<unsigned long long>(revision_after));
		return (FT_ERR_INVALID_STATE);
	}
	(void)restored_session->destroy();
	(void)legacy_source_session->destroy();
	(void)legacy_restored_session->destroy();
	rare_add_result = regeneration_card_make_inventory_item(inventory,
		MinecraftItemCatalog::VOLATILE_STABILIZER_CARD_ITEM_ID, 9005U);
	if (rare_add_result == FT_ERR_SUCCESS)
		error_code = regeneration_card_find_inventory_slot(inventory,
			MinecraftItemCatalog::VOLATILE_STABILIZER_CARD_ITEM_ID, 9005U,
			&rare_slot);
	if (rare_add_result == FT_ERR_SUCCESS
		&& error_code == FT_ERR_SUCCESS)
		error_code = inventory.get_revision(&revision);
	else if (rare_add_result != FT_ERR_SUCCESS)
		error_code = rare_add_result;
	rare_import_result = error_code == FT_ERR_SUCCESS
		? session->import_card_item_to_deck(inventory, rare_slot, revision, 1004U,
			&replayed) : error_code;
	if (rare_import_result != FT_ERR_SUCCESS
		|| replayed.card_id
			!= RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID
		|| inventory.count_item(
			MinecraftItemCatalog::VOLATILE_STABILIZER_CARD_ITEM_ID) != 0)
	{
		std::fprintf(stderr,
			"physical-import: rare add=%d slot=%d setup=%d import=%d count=%d\n",
			rare_add_result, rare_slot, error_code, rare_import_result,
			inventory.count_item(
				MinecraftItemCatalog::VOLATILE_STABILIZER_CARD_ITEM_ID));
		return (FT_ERR_INVALID_STATE);
	}
	error_code = regeneration_card_make_inventory_item(inventory,
		MinecraftItemCatalog::BIOME_EXCLUSION_CARD_ITEM_ID, 9004U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = regeneration_card_find_inventory_slot(inventory,
			MinecraftItemCatalog::BIOME_EXCLUSION_CARD_ITEM_ID, 9004U,
			&biome_slot);
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.get_revision(&revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = session->get_draw_count(&deck_count);
	if (error_code == FT_ERR_SUCCESS)
		biome_import_result = session->import_card_item_to_deck(inventory,
			biome_slot, revision, 1007U, &replayed);
	if (error_code != FT_ERR_SUCCESS
		|| biome_import_result != FT_ERR_FULL
		|| inventory.count_item(
			MinecraftItemCatalog::BIOME_EXCLUSION_CARD_ITEM_ID) != 1)
	{
		std::fprintf(stderr, "physical-import: copy-cap check failed setup=%d import=%d slot=%d deck=%u count=%d\n",
			error_code, biome_import_result, biome_slot, deck_count,
			inventory.count_item(MinecraftItemCatalog::BIOME_EXCLUSION_CARD_ITEM_ID));
		return (FT_ERR_INVALID_STATE);
	}
	const uint64_t stale_revision = revision;
	inventory.resize(8U);
	if (inventory.get_revision(&revision) != FT_ERR_SUCCESS
		|| revision == stale_revision
		|| session->import_card_item_to_deck(inventory, biome_slot, stale_revision,
			1008U, &replayed) != FT_ERR_INVALID_STATE
		|| inventory.count_item(
			MinecraftItemCatalog::BIOME_EXCLUSION_CARD_ITEM_ID) != 1)
	{
		std::fprintf(stderr, "physical-import: stale-revision check failed result=%d revision=%llu old=%llu count=%d\n",
			error_code, static_cast<unsigned long long>(revision),
			static_cast<unsigned long long>(stale_revision),
			inventory.count_item(MinecraftItemCatalog::BIOME_EXCLUSION_CARD_ITEM_ID));
		return (FT_ERR_INVALID_STATE);
	}
	error_code = full_inventory.initialize(1U, 0);
	if (error_code == FT_ERR_SUCCESS)
		error_code = regeneration_card_make_inventory_item(full_inventory,
			42, 9999U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = full_inventory.get_revision(&revision_after);
	if (error_code != FT_ERR_SUCCESS
		|| session->export_card_to_inventory(full_inventory,
			imported.instance_id, revision_after, 1009U, &exported)
		!= FT_ERR_FULL
		|| session->get_draw_count(&deck_count) != FT_ERR_SUCCESS
		|| deck_count != 20U
		|| session->resolve_physical_card_instance(9001U, &mapped_instance)
			!= FT_ERR_SUCCESS
		|| mapped_instance != imported.instance_id
		|| full_inventory.count_item(42) != 1)
	{
		std::fprintf(stderr, "physical-import: full-inventory export check failed result=%d status=%d deck=%u\n",
			error_code, exported.status, deck_count);
		return (FT_ERR_INVALID_STATE);
	}
	if (session->export_card_to_inventory(full_inventory,
			imported.instance_id, revision_after, 1009U, &replayed_export)
		!= FT_ERR_FULL || !replayed_export.replayed
		|| full_inventory.count_item(42) != 1)
		return (FT_ERR_INVALID_STATE);
	error_code = inventory.get_revision(&revision);
	export_revision = revision;
	if (error_code == FT_ERR_SUCCESS)
		error_code = session->export_card_to_inventory(inventory,
			imported.instance_id, export_revision, 1010U, &exported);
	if (error_code != FT_ERR_SUCCESS || exported.status != FT_ERR_SUCCESS
		|| exported.physical_item_instance_id != 9001U
		|| exported.card_instance_id != imported.instance_id
		|| exported.inventory_slot < 0
		|| inventory.count_item(
			MinecraftItemCatalog::VILLAGE_CHARTER_CARD_ITEM_ID) != 1
		|| session->resolve_physical_card_instance(9001U, &mapped_instance)
			!= FT_ERR_NOT_FOUND
		|| session->get_draw_count(&deck_count) != FT_ERR_SUCCESS
		|| deck_count != 19U)
		return (FT_ERR_INVALID_STATE);
	if (inventory.get_revision(&revision_after) != FT_ERR_SUCCESS
		|| session->export_card_to_inventory(inventory, imported.instance_id,
			export_revision, 1010U, &replayed_export) != FT_ERR_SUCCESS
		|| !replayed_export.replayed
		|| replayed_export.inventory_slot != exported.inventory_slot
		|| replayed_export.physical_item_instance_id != 9001U
		|| inventory.get_revision(&revision) != FT_ERR_SUCCESS
		|| revision != revision_after
		|| session->export_card_to_inventory(inventory,
			imported.instance_id + 1U, revision_after, 1010U,
			&replayed_export) != FT_ERR_INVALID_ARGUMENT)
		return (FT_ERR_INVALID_STATE);
	error_code = session->serialize_snapshot(session_snapshot);
	if (error_code == FT_ERR_SUCCESS)
		error_code = exported_restored_session->restore_snapshot(
			session_snapshot.data(),
			session_snapshot.size());
	if (error_code != FT_ERR_SUCCESS
		|| exported_restored_session->resolve_physical_card_instance(9001U,
			&mapped_instance) != FT_ERR_NOT_FOUND
		|| exported_restored_session->export_card_to_inventory(inventory,
			imported.instance_id, export_revision, 1010U, &replayed_export)
			!= FT_ERR_SUCCESS
		|| !replayed_export.replayed
		|| replayed_export.physical_item_instance_id != 9001U
		|| replayed_export.card_instance_id != imported.instance_id)
		return (FT_ERR_INVALID_STATE);
	(void)exported_restored_session->destroy();
	error_code = session->destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = session->initialize(4U, 0x504859534943414CULL);
	if (error_code == FT_ERR_SUCCESS)
		error_code = session->destroy();
	return (error_code);
}

static RegenerationCardChoice regeneration_card_choice_for(uint32_t card_id,
	const voxel_generation_config &world_config)
{
	RegenerationCardChoice choice = {};

	if (card_id == 1U)
	{
		choice.kind = REGENERATION_CARD_CHOICE_BIOME_ID;
		choice.value_primary = 1U;
	}
	else if (card_id == 2U)
	{
		choice.kind = REGENERATION_CARD_CHOICE_ORE_ID;
		uint32_t index = 0U;
		while (index < world_config.ore_rule_count)
		{
			if (world_config.ores[index].enabled == FT_TRUE)
			{
				choice.value_primary = world_config.ores[index].block_id;
				break ;
			}
			++index;
		}
	}
	else if (card_id == 7U)
		choice.kind = REGENERATION_CARD_CHOICE_TARGET_CHUNK;
	return (choice);
}

static int32_t regeneration_card_validate_double_draft(void)
{
	uint64_t seed = 1U;

	while (seed <= 32U)
	{
		RegenerationCardSession session;
		RegenerationCardSession restored_session;
		RegenerationWorldRules rules;
		RegenerationWorldRulesPolicy rules_policy = {};
		voxel_generation_config world_config;
		RegenerationCardActionResult draw_result = {};
		RegenerationCardActionResult replayed_draw = {};
		RegenerationCardActionResult discard_result = {};
		RegenerationCardActionResult replayed_discard = {};
		RegenerationCardActionResult restored_draw = {};
		RegenerationCardActionResult restored_discard = {};
		RegenerationPolicyIntent intent = {};
		RegenerationPolicyIntent restored_intent = {};
		RegenerationPolicyIntent blocked_intent = {};
		bool rules_replayed;
		RegenerationCardChoice choice = {};
		card_game_deck_card chosen_card = {};
		card_game_deck_card played_card = {};
		card_game_deck_card discarded_card = {};
		std::vector<uint8_t> snapshot_bytes;
		uint32_t draw_count;
		uint32_t hand_count;
		uint32_t required_discard_count;
		uint32_t automatic_discard_count;
		uint32_t automatic_hand_count;
		uint32_t automatic_obligation_count;
		uint32_t pending_card_count;
		uint32_t index;
		int32_t error_code;

		error_code = voxel_default_generation_config(world_config);
		rules_policy.surface_magma_pools_per_card = 1U;
		rules_policy.maximum_village_plans = 4U;
		rules_policy.maximum_surface_magma_pools = 4U;
		rules_policy.maximum_added_target_chunks = 2U;
		rules_policy.maximum_ore_scale_numerator = 8U;
		rules_policy.maximum_ore_scale_denominator = 1U;
		rules_policy.ore_overgrowth_instability_delta = 5;
		rules_policy.maximum_biome_weight_adjustment = 10;
		rules_policy.generation_manifest_hash =
			RegenerationWorldRules::calculate_manifest_hash(world_config,
				rules_policy);
		if (error_code == FT_ERR_SUCCESS)
			error_code = rules.initialize(world_config, rules_policy);
		if (error_code == FT_ERR_SUCCESS)
			error_code = session.initialize(7U,
				0x444F55424C450000ULL + seed);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		hand_count = 0U;
		error_code = session.get_hand_count(&hand_count);
		index = 0U;
		while (error_code == FT_ERR_SUCCESS && index < hand_count)
		{
			card_game_deck_card card = {};

			error_code = session.hand_inspect(index, &card);
			if (error_code == FT_ERR_SUCCESS && card.card_id == 5U)
				chosen_card = card;
			++index;
		}
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (chosen_card.instance_id == 0U)
		{
			(void)session.destroy();
			++seed;
			continue ;
		}
		if (session.play_card(0x4444504C41590000ULL + seed,
				chosen_card.instance_id, choice, world_config, &intent)
			!= FT_ERR_SUCCESS
			|| session.count_pending_card_definition(5U, &pending_card_count)
				!= FT_ERR_SUCCESS || pending_card_count != 1U
			|| session.count_pending_card_definition(3U, &pending_card_count)
				!= FT_ERR_SUCCESS || pending_card_count != 0U
			|| intent.effect_kind != REGENERATION_CARD_EFFECT_DOUBLE_DRAFT
			|| session.get_hand_count(&hand_count) != FT_ERR_SUCCESS
			|| hand_count != 6U
			|| session.get_discard_count(&draw_count) != FT_ERR_SUCCESS
			|| draw_count != 1U
			|| session.discard_inspect(0U, &played_card) != FT_ERR_SUCCESS
			|| played_card.instance_id != chosen_card.instance_id
			|| played_card.card_id != chosen_card.card_id)
		{
			std::fprintf(stderr, "double-draft: play failed\n");
			return (FT_ERR_INVALID_STATE);
		}
		if (session.draw_cards(0x4444524157000001ULL,
				intent.host_intent_sequence, 2U, 1U, &draw_result)
			!= FT_ERR_INVALID_OPERATION
			|| session.play_card(0x4444504C41590020ULL + seed,
				chosen_card.instance_id, choice, world_config, &blocked_intent)
			!= FT_ERR_PERMISSION_DENIED
			|| rules.consume_pending_card_intent(session, &rules_replayed)
			!= FT_ERR_SUCCESS || rules_replayed
			|| session.count_pending_card_definition(5U, &pending_card_count)
				!= FT_ERR_SUCCESS || pending_card_count != 0U
			|| rules.apply_intent(intent, &rules_replayed) != FT_ERR_SUCCESS
			|| !rules_replayed)
			return (FT_ERR_INVALID_STATE);
		if (session.draw_cards(0x4444524157000001ULL,
			intent.host_intent_sequence, 2U, 1U, &draw_result)
			!= FT_ERR_SUCCESS || draw_result.count != 2U || draw_result.replayed)
		{
			std::fprintf(stderr, "double-draft: draw failed\n");
			return (FT_ERR_INVALID_STATE);
		}
		error_code = session.draw_cards(0x4444524157000001ULL,
			intent.host_intent_sequence, 2U, 1U, &replayed_draw);
		if (error_code != FT_ERR_SUCCESS || !replayed_draw.replayed
			|| replayed_draw.command_sequence != draw_result.command_sequence)
		{
			std::fprintf(stderr,
				"double-draft: replay error=%d replayed=%d seq=%llu expected=%llu\n",
				error_code, replayed_draw.replayed,
				static_cast<unsigned long long>(replayed_draw.command_sequence),
				static_cast<unsigned long long>(draw_result.command_sequence));
			return (FT_ERR_INVALID_STATE);
		}
		error_code = session.draw_cards(0x4444524157000001ULL,
			intent.host_intent_sequence, 1U, 0U, &replayed_draw);
		if (error_code != FT_ERR_INVALID_ARGUMENT)
		{
			std::fprintf(stderr,
				"double-draft: mismatched retry error=%d\n", error_code);
			return (FT_ERR_INVALID_STATE);
		}
		if (session.get_draw_count(&draw_count) != FT_ERR_SUCCESS
			|| session.get_hand_count(&hand_count) != FT_ERR_SUCCESS
			|| session.get_pending_required_discard_count(
				&required_discard_count) != FT_ERR_SUCCESS
			|| draw_count != 11U || hand_count != 8U
			|| required_discard_count != 1U
			|| session.hand_inspect(0U, &discarded_card) != FT_ERR_SUCCESS)
		{
			std::fprintf(stderr, "double-draft: count failed draw=%u hand=%u\n",
				draw_count, hand_count);
			return (FT_ERR_INVALID_STATE);
		}
		if (session.discard_hand_card(0x4444434152000002ULL,
				0xFFFFFFFFFFFFFFFFULL, discarded_card.instance_id,
				&discard_result) != FT_ERR_PERMISSION_DENIED)
			return (FT_ERR_INVALID_STATE);
		if (session.play_card(0x4444504C41590040ULL + seed,
				chosen_card.instance_id, choice, world_config, &blocked_intent)
			!= FT_ERR_PERMISSION_DENIED)
			return (FT_ERR_INVALID_STATE);
		error_code = session.serialize_snapshot(snapshot_bytes);
		if (error_code == FT_ERR_SUCCESS)
			error_code = restored_session.restore_snapshot(
				snapshot_bytes.data(), snapshot_bytes.size());
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		card_game_deck_card default_discard_card = {};
		uint32_t automatic_index = 0U;
		error_code = restored_session.get_hand_count(&automatic_hand_count);
		while (error_code == FT_ERR_SUCCESS
			&& automatic_index < automatic_hand_count)
		{
			card_game_deck_card candidate = {};
			error_code = restored_session.hand_inspect(automatic_index,
				&candidate);
			if (error_code == FT_ERR_SUCCESS
				&& (default_discard_card.instance_id == 0U
					|| candidate.instance_id < default_discard_card.instance_id))
				default_discard_card = candidate;
			++automatic_index;
		}
		automatic_discard_count = 0U;
		if (error_code == FT_ERR_SUCCESS)
			error_code = restored_session
				.resolve_pending_required_discards_to_default(
					0x444541444C494E45ULL, &automatic_discard_count);
		uint32_t verified_discard_count = 0U;
		bool default_discard_was_recorded = false;
		if (error_code == FT_ERR_SUCCESS)
			error_code = restored_session.get_discard_count(
				&verified_discard_count);
		uint32_t verified_discard_index = 0U;
		while (error_code == FT_ERR_SUCCESS
			&& verified_discard_index < verified_discard_count)
		{
			card_game_deck_card candidate = {};
			error_code = restored_session.discard_inspect(
				verified_discard_index, &candidate);
			if (error_code == FT_ERR_SUCCESS
				&& candidate.instance_id == default_discard_card.instance_id)
				default_discard_was_recorded = true;
			++verified_discard_index;
		}
		if (error_code != FT_ERR_SUCCESS
			|| restored_session.get_pending_required_discard_count(
				&automatic_obligation_count) != FT_ERR_SUCCESS
			|| restored_session.get_hand_count(&automatic_hand_count)
				!= FT_ERR_SUCCESS
			|| automatic_discard_count != 1U
			|| automatic_obligation_count != 0U
			|| automatic_hand_count != 7U
			|| verified_discard_count != 2U
			|| !default_discard_was_recorded
			|| restored_session.destroy() != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
		if (session.discard_hand_card(0x4444434152000002ULL,
				0x4444524157000001ULL, discarded_card.instance_id,
				&discard_result) != FT_ERR_SUCCESS
			|| discard_result.count != 1U || discard_result.replayed)
			return (FT_ERR_INVALID_STATE);
		if (session.discard_hand_card(0x4444434152000002ULL,
				0x4444524157000001ULL, discarded_card.instance_id,
				&replayed_discard) != FT_ERR_SUCCESS || !replayed_discard.replayed
			|| replayed_discard.command_sequence
				!= discard_result.command_sequence
			|| session.get_hand_count(&hand_count) != FT_ERR_SUCCESS
			|| hand_count != 7U
			|| session.get_pending_required_discard_count(
				&required_discard_count) != FT_ERR_SUCCESS
			|| required_discard_count != 0U)
			return (FT_ERR_INVALID_STATE);
		error_code = session.serialize_snapshot(snapshot_bytes);
		if (error_code == FT_ERR_SUCCESS)
			error_code = restored_session.restore_snapshot(
				snapshot_bytes.data(), snapshot_bytes.size());
		if (error_code == FT_ERR_SUCCESS)
			error_code = restored_session.play_card(
				0x4444504C41590000ULL + seed, chosen_card.instance_id,
				choice, world_config, &restored_intent);
		if (error_code == FT_ERR_SUCCESS)
			error_code = restored_session.draw_cards(
				0x4444524157000001ULL, intent.host_intent_sequence, 2U, 1U,
				&restored_draw);
		if (error_code == FT_ERR_SUCCESS)
			error_code = restored_session.discard_hand_card(
				0x4444434152000002ULL, 0x4444524157000001ULL,
				discarded_card.instance_id, &restored_discard);
		if (error_code != FT_ERR_SUCCESS || !restored_intent.replayed
			|| restored_intent.host_intent_sequence != intent.host_intent_sequence
			|| !restored_draw.replayed
			|| restored_draw.command_sequence != draw_result.command_sequence
			|| !restored_discard.replayed
			|| restored_discard.command_sequence != discard_result.command_sequence)
			return (FT_ERR_INVALID_STATE);
		(void)restored_session.destroy();
		return (session.destroy());
	}
	return (FT_ERR_NOT_FOUND);
}

static RegenerationPolicyIntent regeneration_test_intent(uint64_t sequence,
	uint32_t card_id, uint32_t instance_id,
	const RegenerationCardChoice &choice)
{
	RegenerationPolicyIntent intent = {};

	intent.request_id = 0x52554C4553000000ULL + sequence;
	intent.host_intent_sequence = sequence;
	intent.content_version = RegenerationCardSession::CONTENT_VERSION;
	intent.card_definition_id = card_id;
	intent.card_instance_id = instance_id;
	if (card_id == 1U)
		intent.effect_kind = REGENERATION_CARD_EFFECT_BIOME_EXCLUSION;
	else if (card_id == 2U)
		intent.effect_kind = REGENERATION_CARD_EFFECT_DEEP_SEAM;
	else if (card_id == 3U)
		intent.effect_kind = REGENERATION_CARD_EFFECT_VILLAGE_CHARTER;
	else if (card_id == 4U)
		intent.effect_kind = REGENERATION_CARD_EFFECT_SURFACE_MAGMA;
	else if (card_id == 5U)
		intent.effect_kind = REGENERATION_CARD_EFFECT_DOUBLE_DRAFT;
	else if (card_id == 6U)
		intent.effect_kind = REGENERATION_CARD_EFFECT_BIOME_COMPASS;
	else if (card_id == 7U)
		intent.effect_kind = REGENERATION_CARD_EFFECT_SURVEYORS_REACH;
	else if (card_id
		== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID)
		intent.effect_kind = REGENERATION_CARD_EFFECT_VOLCANO_MARK;
	else if (card_id
		== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
		intent.effect_kind = REGENERATION_CARD_EFFECT_VOLATILE_STABILIZER;
	intent.choice = choice;
	return (intent);
}

static int32_t regeneration_card_validate_world_rules(void)
{
	static const char *RULES_FILE_PATH =
		"regeneration_world_rules_validator.bin";
	voxel_generation_config world_config;
	voxel_generation_config generation_config;
	RegenerationWorldRules rules;
	RegenerationWorldRules restored_rules;
	RegenerationWorldRulesPolicy policy = {};
	RegenerationWorldRulesPolicy mismatched_policy = {};
	std::vector<uint8_t> rules_bytes;
	RegenerationCardChoice choice = {};
	RegenerationPolicyIntent intent = {};
	bool replayed;
	bool biome_enabled;
	int32_t weight_adjustment;
	uint32_t ore_id;
	uint32_t ore_index;
	uint32_t active_biomes;
	uint32_t ore_numerator;
	uint32_t ore_denominator;
	uint32_t villages;
	uint32_t magma_pools;
	uint32_t target_chunks;
	int32_t error_code;
	int32_t effect_preflight_result;
	int32_t effect_apply_result;

	error_code = voxel_default_generation_config(world_config);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	policy.surface_magma_pools_per_card = 3U;
	policy.maximum_village_plans = 2U;
	policy.maximum_surface_magma_pools = 4U;
	policy.maximum_added_target_chunks = 1U;
	policy.maximum_ore_scale_numerator = 4U;
	policy.maximum_ore_scale_denominator = 1U;
	policy.ore_overgrowth_instability_delta = 5;
	policy.volcano_mark_instability_delta = 5;
	policy.maximum_volcano_plans = 1U;
	policy.volcano_footprint_chunks = 9U;
	policy.volatile_stabilizer_risk_tier_count = 2U;
	policy.volatile_stabilizer_overload_basis_points[0] = 500U;
	policy.volatile_stabilizer_overload_basis_points[1] = 1500U;
	policy.volatile_stabilizer_success_reduction = 2;
	policy.volatile_stabilizer_overload_surge = 5;
	policy.maximum_biome_weight_adjustment = 5;
	ore_index = 0U;
	while (ore_index < world_config.biome_count)
	{
		policy.biome_compass_adjustments[ore_index] = 1;
		++ore_index;
	}
	policy.generation_manifest_hash =
		RegenerationWorldRules::calculate_manifest_hash(world_config, policy);
	ore_index = 0U;
	while (ore_index < world_config.ore_rule_count)
	{
		if (world_config.ores[ore_index].enabled == FT_TRUE)
			break ;
		++ore_index;
	}
	if (ore_index >= world_config.ore_rule_count)
		return (FT_ERR_CONFIGURATION);
	ore_id = world_config.ores[ore_index].block_id;
	error_code = rules.initialize(world_config, policy);
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "world-rules: initialize error=%d\n", error_code);
		return (error_code);
	}
	choice.kind = REGENERATION_CARD_CHOICE_BIOME_ID;
	choice.value_primary = 1U;
	intent = regeneration_test_intent(1U, 1U, 101U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_SUCCESS || replayed
		|| rules.apply_intent(intent, &replayed) != FT_ERR_SUCCESS || !replayed
		|| rules.get_active_biome_count(&active_biomes) != FT_ERR_SUCCESS
		|| active_biomes != 4U)
		return (FT_ERR_INVALID_STATE);
	intent.choice.value_primary = 2U;
	if (rules.apply_intent(intent, &replayed) != FT_ERR_INVALID_ARGUMENT
		|| rules.get_active_biome_count(&active_biomes) != FT_ERR_SUCCESS
		|| active_biomes != 4U)
		return (FT_ERR_INVALID_STATE);
	intent.choice.value_primary = 1U;
	intent.host_intent_sequence = 2U;
	if (rules.apply_intent(intent, &replayed) != FT_ERR_INVALID_ARGUMENT)
		return (FT_ERR_INVALID_STATE);
	intent = regeneration_test_intent(2U, 1U, 102U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_SUCCESS || replayed
		|| rules.get_active_biome_count(&active_biomes) != FT_ERR_SUCCESS
		|| active_biomes != 4U)
		return (FT_ERR_INVALID_STATE);
	choice.value_primary = 2U;
	intent = regeneration_test_intent(3U, 1U, 103U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	choice.value_primary = 3U;
	intent = regeneration_test_intent(4U, 1U, 104U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	choice.value_primary = 4U;
	intent = regeneration_test_intent(5U, 1U, 105U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	choice.value_primary = 5U;
	intent = regeneration_test_intent(6U, 1U, 106U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_OUT_OF_RANGE
		|| rules.get_active_biome_count(&active_biomes) != FT_ERR_SUCCESS
		|| active_biomes != 1U)
		return (FT_ERR_INVALID_STATE);
	choice.kind = REGENERATION_CARD_CHOICE_ORE_ID;
	choice.value_primary = ore_id;
	choice.value_secondary = 0U;
	intent = regeneration_test_intent(7U, 2U, 107U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_SUCCESS
		|| rules.get_ore_scale(ore_id, &ore_numerator, &ore_denominator)
		!= FT_ERR_SUCCESS || ore_numerator != 3U || ore_denominator != 2U)
		return (FT_ERR_INVALID_STATE);
	intent = regeneration_test_intent(8U, 2U, 108U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	intent = regeneration_test_intent(9U, 2U, 109U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	intent = regeneration_test_intent(10U, 2U, 110U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_OUT_OF_RANGE
		|| rules.get_ore_scale(ore_id, &ore_numerator, &ore_denominator)
		!= FT_ERR_SUCCESS || ore_numerator != 27U || ore_denominator != 8U)
		return (FT_ERR_INVALID_STATE);
	if (generation_config.initialize(world_config) != FT_ERR_SUCCESS)
		return (FT_ERR_CONFIGURATION);
	uint32_t original_minimum = world_config.ores[ore_index].veins_per_chunk_min;
	uint32_t original_maximum = world_config.ores[ore_index].veins_per_chunk_max;
	uint32_t expected_minimum = static_cast<uint32_t>(
		(static_cast<uint64_t>(original_minimum) * 27U + 7U) / 8U);
	uint32_t expected_maximum = static_cast<uint32_t>(
		(static_cast<uint64_t>(original_maximum) * 27U + 7U) / 8U);
	if (rules.apply_ore_scales(generation_config) != FT_ERR_SUCCESS
		|| generation_config.ores[ore_index].veins_per_chunk_min
			!= expected_minimum
		|| generation_config.ores[ore_index].veins_per_chunk_max
			!= expected_maximum
		|| world_config.ores[ore_index].veins_per_chunk_min != original_minimum
		|| world_config.ores[ore_index].veins_per_chunk_max != original_maximum)
		return (FT_ERR_INVALID_STATE);
	choice = {};
	intent = regeneration_test_intent(11U, 3U, 111U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	intent = regeneration_test_intent(12U, 3U, 112U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	intent = regeneration_test_intent(13U, 3U, 113U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_FULL)
		return (FT_ERR_INVALID_STATE);
	intent = regeneration_test_intent(14U, 4U, 114U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	intent = regeneration_test_intent(15U, 4U, 115U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_FULL)
		return (FT_ERR_INVALID_STATE);
	intent = regeneration_test_intent(16U, 6U, 116U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_SUCCESS
		|| rules.get_biome_state(5U, &biome_enabled, &weight_adjustment)
		!= FT_ERR_SUCCESS || !biome_enabled || weight_adjustment != 1
		|| rules.get_biome_state(1U, &biome_enabled, &weight_adjustment)
		!= FT_ERR_SUCCESS || biome_enabled || weight_adjustment != 0)
		return (FT_ERR_INVALID_STATE);
	choice.kind = REGENERATION_CARD_CHOICE_TARGET_CHUNK;
	choice.value_primary = 25U;
	choice.value_secondary = 31U;
	intent = regeneration_test_intent(17U, 7U, 117U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	choice.value_primary = 26U;
	intent = regeneration_test_intent(18U, 7U, 118U, choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_FULL
		|| rules.get_plan_counts(&villages, &magma_pools, &target_chunks)
		!= FT_ERR_SUCCESS || villages != 2U || magma_pools != 3U
		|| target_chunks != 1U)
		return (FT_ERR_INVALID_STATE);
	choice = {};
	intent = regeneration_test_intent(19U,
		RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID, 119U,
		choice);
	effect_preflight_result = rules.preflight_card_effect(
		intent.card_definition_id, choice);
	effect_apply_result = rules.apply_intent(intent, &replayed);
	if (effect_preflight_result != FT_ERR_SUCCESS
		|| effect_apply_result != FT_ERR_SUCCESS || replayed)
	{
		std::fprintf(stderr,
			"world-rules: first Volcano Mark preflight=%d apply=%d replay=%u\n",
			effect_preflight_result, effect_apply_result,
			static_cast<uint32_t>(replayed));
		return (FT_ERR_INVALID_STATE);
	}
	intent = regeneration_test_intent(20U,
		RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID, 120U,
		choice);
	if (rules.apply_intent(intent, &replayed) != FT_ERR_FULL)
		return (FT_ERR_INVALID_STATE);
	std::vector<RegenerationWorldFeaturePlan> volcano_plans;
	if (rules.copy_feature_plans(volcano_plans) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	uint32_t volcano_plan_count = 0U;
	for (const RegenerationWorldFeaturePlan &plan : volcano_plans)
	{
		if (plan.kind == REGENERATION_WORLD_FEATURE_VOLCANO)
			++volcano_plan_count;
	}
	if (volcano_plan_count != 1U)
		return (FT_ERR_INVALID_STATE);
	intent = regeneration_test_intent(21U,
		RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID,
		121U, choice);
	intent.private_result = 1U;
	intent.private_ordinal = 1U;
	effect_preflight_result = rules.preflight_card_effect(
		intent.card_definition_id, choice);
	effect_apply_result = rules.apply_intent(intent, &replayed);
	if (effect_preflight_result != FT_ERR_SUCCESS
		|| effect_apply_result != FT_ERR_SUCCESS || replayed)
	{
		std::fprintf(stderr,
			"world-rules: stabilizer first preflight=%d apply=%d replay=%u\n",
			effect_preflight_result, effect_apply_result,
			static_cast<uint32_t>(replayed));
		return (FT_ERR_INVALID_STATE);
	}
	intent = regeneration_test_intent(22U,
		RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID,
		122U, choice);
	intent.private_result = 0U;
	intent.private_ordinal = 2U;
	effect_apply_result = rules.apply_intent(intent, &replayed);
	if (effect_apply_result != FT_ERR_SUCCESS || replayed
		|| rules.get_volatile_stabilizer_use_count(&ore_numerator)
			!= FT_ERR_SUCCESS || ore_numerator != 2U)
	{
		std::fprintf(stderr,
			"world-rules: stabilizer second apply=%d replay=%u uses=%u\n",
			effect_apply_result, static_cast<uint32_t>(replayed), ore_numerator);
		return (FT_ERR_INVALID_STATE);
	}
	int32_t serialize_result = rules.serialize(rules_bytes);
	int32_t restore_result = serialize_result == FT_ERR_SUCCESS
		? restored_rules.deserialize(rules_bytes.data(), rules_bytes.size(),
			world_config, policy) : serialize_result;
	if (restore_result != FT_ERR_SUCCESS
		|| restored_rules.get_plan_counts(&villages, &magma_pools,
			&target_chunks) != FT_ERR_SUCCESS || villages != 2U
		|| magma_pools != 3U || target_chunks != 1U
		|| restored_rules.get_ore_scale(ore_id, &ore_numerator,
			&ore_denominator) != FT_ERR_SUCCESS || ore_numerator != 27U
		|| ore_denominator != 8U
		|| restored_rules.get_volatile_stabilizer_use_count(&ore_numerator)
			!= FT_ERR_SUCCESS || ore_numerator != 2U)
	{
		std::fprintf(stderr,
			"world-rules: snapshot serialize=%d restore=%d uses=%u bytes=%zu\n",
			serialize_result, restore_result, ore_numerator, rules_bytes.size());
		return (FT_ERR_INVALID_STATE);
	}
	if (restored_rules.apply_intent(intent, &replayed) != FT_ERR_SUCCESS
		|| !replayed
		|| restored_rules.get_volatile_stabilizer_use_count(&ore_numerator)
			!= FT_ERR_SUCCESS || ore_numerator != 2U)
		return (FT_ERR_INVALID_STATE);
	if (restored_rules.copy_feature_plans(volcano_plans) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	volcano_plan_count = 0U;
	for (const RegenerationWorldFeaturePlan &plan : volcano_plans)
	{
		if (plan.kind == REGENERATION_WORLD_FEATURE_VOLCANO)
			++volcano_plan_count;
	}
	if (volcano_plan_count != 1U)
		return (FT_ERR_INVALID_STATE);
	error_code = rules.save_file(RULES_FILE_PATH);
	if (error_code == FT_ERR_SUCCESS)
		error_code = restored_rules.load_file(RULES_FILE_PATH, world_config,
			policy);
	(void)std::remove(RULES_FILE_PATH);
	if (error_code != FT_ERR_SUCCESS
		|| restored_rules.get_plan_counts(&villages, &magma_pools,
			&target_chunks) != FT_ERR_SUCCESS || villages != 2U
		|| magma_pools != 3U || target_chunks != 1U)
		return (FT_ERR_INVALID_STATE);
	if (restored_rules.copy_feature_plans(volcano_plans) != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	volcano_plan_count = 0U;
	for (const RegenerationWorldFeaturePlan &plan : volcano_plans)
	{
		if (plan.kind == REGENERATION_WORLD_FEATURE_VOLCANO)
			++volcano_plan_count;
	}
	if (volcano_plan_count != 1U)
		return (FT_ERR_INVALID_STATE);
	mismatched_policy = policy;
	mismatched_policy.maximum_village_plans += 1U;
	mismatched_policy.generation_manifest_hash =
		RegenerationWorldRules::calculate_manifest_hash(world_config,
			mismatched_policy);
	if (mismatched_policy.generation_manifest_hash
		== policy.generation_manifest_hash)
		return (FT_ERR_INVALID_STATE);
	if (restored_rules.deserialize(rules_bytes.data(), rules_bytes.size(),
			world_config, mismatched_policy) != FT_ERR_INVALID_STATE
		|| restored_rules.get_plan_counts(&villages, &magma_pools,
			&target_chunks) != FT_ERR_SUCCESS || villages != 2U
		|| magma_pools != 3U || target_chunks != 1U)
		return (FT_ERR_INVALID_STATE);
	if (generation_config.initialize(world_config) != FT_ERR_SUCCESS
		|| generation_config.set_sea_level(world_config.sea_level + 1)
			!= FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	mismatched_policy = policy;
	mismatched_policy.generation_manifest_hash =
		RegenerationWorldRules::calculate_manifest_hash(generation_config,
			mismatched_policy);
	if (mismatched_policy.generation_manifest_hash
		== policy.generation_manifest_hash
		|| restored_rules.deserialize(rules_bytes.data(), rules_bytes.size(),
			generation_config, mismatched_policy) != FT_ERR_INVALID_STATE
		|| restored_rules.get_plan_counts(&villages, &magma_pools,
			&target_chunks) != FT_ERR_SUCCESS || villages != 2U
		|| magma_pools != 3U || target_chunks != 1U)
		return (FT_ERR_INVALID_STATE);
	rules_bytes.back() ^= 0x01U;
	if (restored_rules.deserialize(rules_bytes.data(), rules_bytes.size(),
			world_config, policy) != FT_ERR_INVALID_ARGUMENT
		|| restored_rules.get_plan_counts(&villages, &magma_pools,
			&target_chunks) != FT_ERR_SUCCESS || villages != 2U
		|| magma_pools != 3U || target_chunks != 1U)
		return (FT_ERR_INVALID_STATE);
	(void)restored_rules.destroy();
	return (rules.destroy());
}

RegenerationCardValidator::RegenerationCardValidator()
{
}

RegenerationCardValidator::RegenerationCardValidator(
	const RegenerationCardValidator &other) : IValidator(other)
{
	(void)other;
}

RegenerationCardValidator::~RegenerationCardValidator()
{
}

RegenerationCardValidator &RegenerationCardValidator::operator=(
	const RegenerationCardValidator &other)
{
	(void)other;
	return (*this);
}

int RegenerationCardValidator::validate_opening_hand(
	uint32_t opening_hand_size, uint64_t seed) noexcept
{
	static const uint32_t EXPECTED_CARD_COUNTS[7] =
		{4U, 4U, 2U, 2U, 2U, 4U, 2U};
	RegenerationCardSession session;
	voxel_generation_config world_config;
	uint32_t observed_card_counts[7] = {};
	uint32_t instance_ids[RegenerationCardSession::STARTER_DECK_INSTANCE_COUNT]
		= {};
	uint32_t hand_count;
	uint32_t draw_count;
	uint32_t zone_index;
	uint32_t index;
	uint32_t *zone_count;
	int32_t error_code;

	hand_count = 0U;
	draw_count = 0U;
	error_code = voxel_default_generation_config(world_config);
	if (error_code == FT_ERR_SUCCESS)
		error_code = session.initialize(opening_hand_size, seed);
	if (error_code != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "regeneration-cards: initialize failed error=%d\n",
			error_code);
		return (1);
	}
	error_code = session.get_hand_count(&hand_count);
	if (error_code == FT_ERR_SUCCESS)
		error_code = session.get_draw_count(&draw_count);
	if (error_code != FT_ERR_SUCCESS
		|| hand_count != opening_hand_size
		|| hand_count + draw_count
			!= RegenerationCardSession::STARTER_DECK_INSTANCE_COUNT)
	{
		std::fprintf(stderr,
			"regeneration-cards: zone counts failed error=%d hand=%u draw=%u\n",
			error_code, hand_count, draw_count);
		(void)session.destroy();
		return (1);
	}
	zone_index = 0U;
	while (zone_index < 2U)
	{
		zone_count = zone_index == 0U ? &draw_count : &hand_count;
		index = 0U;
		while (index < *zone_count)
		{
			card_game_deck_card card = {};
			uint32_t instance_index;

			if (zone_index == 0U)
				error_code = session.draw_inspect(index, &card);
			else
				error_code = session.hand_inspect(index, &card);
			if (error_code != FT_ERR_SUCCESS || card.card_id < 1U
				|| card.card_id > RegenerationCardSession::STARTER_CARD_DEFINITION_COUNT
				|| card.instance_id == 0U
				|| observed_card_counts[card.card_id - 1U]
					>= EXPECTED_CARD_COUNTS[card.card_id - 1U]
				|| index + (zone_index == 0U ? 0U : draw_count)
					>= RegenerationCardSession::STARTER_DECK_INSTANCE_COUNT)
			{
				std::fprintf(stderr,
					"regeneration-cards: invalid card error=%d zone=%u index=%u id=%u instance=%u\n",
					error_code, zone_index, index, card.card_id,
					card.instance_id);
				(void)session.destroy();
				return (1);
			}
			instance_index = 0U;
			while (instance_index < index
				+ (zone_index == 0U ? 0U : draw_count))
			{
				if (instance_ids[instance_index] == card.instance_id)
				{
					std::fprintf(stderr,
						"regeneration-cards: duplicate instance=%u\n",
						card.instance_id);
					(void)session.destroy();
					return (1);
				}
				++instance_index;
			}
			instance_ids[index + (zone_index == 0U ? 0U : draw_count)] =
				card.instance_id;
			++observed_card_counts[card.card_id - 1U];
			++index;
		}
		++zone_index;
	}
	index = 0U;
	while (index < 7U)
	{
		if (observed_card_counts[index] != EXPECTED_CARD_COUNTS[index])
		{
			std::fprintf(stderr,
				"regeneration-cards: copy count definition=%u actual=%u expected=%u\n",
				index + 1U, observed_card_counts[index],
				EXPECTED_CARD_COUNTS[index]);
			(void)session.destroy();
			return (1);
		}
		++index;
	}
	{
		card_game_deck_card chosen_card = {};
		RegenerationCardChoice invalid_choice = {};
		RegenerationCardChoice valid_choice;
		RegenerationPolicyIntent intent = {};
		RegenerationPolicyIntent replayed_intent = {};
		card_game_host_intent_record host_intent = {};
		card_game_host_intent_record draw_notice = {};
		bool is_draw_notice = false;
		uint32_t pending_intent_count;
		uint32_t chosen_index;

		invalid_choice.kind = REGENERATION_CARD_CHOICE_NONE;
		invalid_choice.value_primary = 1U;
		error_code = session.hand_inspect(0U, &chosen_card);
		if (error_code != FT_ERR_SUCCESS
			|| session.play_card(0x524547454E000000ULL + opening_hand_size,
				chosen_card.instance_id, invalid_choice, world_config, &intent)
			!= FT_ERR_INVALID_ARGUMENT)
		{
			(void)session.destroy();
			return (1);
		}
		chosen_index = 0U;
		while (chosen_index < hand_count)
		{
			card_game_deck_card current_card = {};

			error_code = session.hand_inspect(chosen_index, &current_card);
			if (error_code != FT_ERR_SUCCESS)
			{
				(void)session.destroy();
				return (1);
			}
			if (current_card.instance_id == chosen_card.instance_id)
				break ;
			++chosen_index;
		}
		if (chosen_index == hand_count)
		{
			(void)session.destroy();
			return (1);
		}
		if (chosen_card.card_id == 1U || chosen_card.card_id == 2U)
		{
			RegenerationCardChoice invalid_domain_choice = {};
			card_game_deck_card retained_card = {};
			uint32_t retained_hand_count;

			invalid_domain_choice.kind = chosen_card.card_id == 1U
				? REGENERATION_CARD_CHOICE_BIOME_ID
				: REGENERATION_CARD_CHOICE_ORE_ID;
			invalid_domain_choice.value_primary = chosen_card.card_id == 1U
				? world_config.biome_count + 1U : UINT32_MAX;
			if (session.play_card(
					0x524547454E100000ULL + opening_hand_size,
					chosen_card.instance_id, invalid_domain_choice,
					world_config, &intent) != FT_ERR_NOT_FOUND
				|| session.get_hand_count(&retained_hand_count)
				!= FT_ERR_SUCCESS
				|| retained_hand_count != hand_count
				|| session.hand_inspect(chosen_index, &retained_card)
				!= FT_ERR_SUCCESS
				|| retained_card.instance_id != chosen_card.instance_id)
			{
				(void)session.destroy();
				return (1);
			}
		}
		valid_choice = regeneration_card_choice_for(chosen_card.card_id,
			world_config);
		if (session.play_card(0x524547454E000000ULL + opening_hand_size,
				chosen_card.instance_id, valid_choice, world_config, &intent)
			!= FT_ERR_SUCCESS
			|| intent.card_instance_id != chosen_card.instance_id
			|| intent.card_definition_id != chosen_card.card_id
			|| intent.choice.kind != valid_choice.kind
			|| intent.choice.value_primary != valid_choice.value_primary
			|| intent.choice.value_secondary != valid_choice.value_secondary
			|| intent.content_version != RegenerationCardSession::CONTENT_VERSION
			|| session.peek_pending_intent(&host_intent) != FT_ERR_SUCCESS
			|| host_intent.sequence != intent.host_intent_sequence
			|| host_intent.intent.field_count != 8U
			|| host_intent.intent.fields[0].value != chosen_card.card_id
			|| host_intent.intent.fields[1].value != chosen_card.instance_id
			|| host_intent.intent.fields[2].value
				!= static_cast<uint32_t>(intent.effect_kind)
			|| host_intent.intent.fields[3].value
				!= static_cast<uint32_t>(valid_choice.kind)
			|| host_intent.intent.fields[4].value != valid_choice.value_primary
			|| host_intent.intent.fields[5].value != valid_choice.value_secondary
			|| host_intent.intent.fields[6].value
				!= RegenerationCardSession::CONTENT_VERSION
			|| host_intent.intent.fields[7].value
				!= 0x524547454E000000ULL + opening_hand_size)
		{
			(void)session.destroy();
			return (1);
		}
		draw_notice.sequence = 1U;
		draw_notice.intent.type_id =
			RegenerationCardSession::EMPTY_DRAW_INTENT_TYPE_ID;
		draw_notice.intent.version = RegenerationCardSession::CONTENT_VERSION;
		draw_notice.intent.field_count = 2U;
		draw_notice.intent.fields[0].type = CARD_GAME_HOST_INTENT_U32;
		draw_notice.intent.fields[0].value = 0U;
		draw_notice.intent.fields[1].type = CARD_GAME_HOST_INTENT_U32;
		if (session.is_draw_notice(draw_notice, &is_draw_notice)
			!= FT_ERR_SUCCESS || !is_draw_notice)
		{
			(void)session.destroy();
			return (1);
		}
		draw_notice.intent.type_id =
			RegenerationCardSession::HAND_OVERFLOW_INTENT_TYPE_ID;
		draw_notice.intent.fields[1].value = 1U;
		if (session.is_draw_notice(draw_notice, &is_draw_notice)
			!= FT_ERR_SUCCESS || !is_draw_notice)
		{
			(void)session.destroy();
			return (1);
		}
		draw_notice.intent.fields[0].value = 1U;
		if (session.is_draw_notice(draw_notice, &is_draw_notice)
			!= FT_ERR_INVALID_ARGUMENT)
		{
			(void)session.destroy();
			return (1);
		}
		if (session.play_card(0x524547454E000000ULL + opening_hand_size,
				chosen_card.instance_id, valid_choice, world_config,
				&replayed_intent)
			!= FT_ERR_SUCCESS || replayed_intent.replayed != true
			|| replayed_intent.host_intent_sequence
				!= intent.host_intent_sequence)
		{
			(void)session.destroy();
			return (1);
		}
		if (chosen_card.card_id == 7U)
			valid_choice.value_primary = 1U;
		else
			valid_choice.value_primary += 1U;
		if (session.play_card(0x524547454E000000ULL + opening_hand_size,
				chosen_card.instance_id, valid_choice, world_config,
				&replayed_intent)
			!= FT_ERR_INVALID_ARGUMENT
			|| session.get_pending_intent_count(
				&pending_intent_count) != FT_ERR_SUCCESS
			|| pending_intent_count != 1U
			|| session.acknowledge_intent(intent.host_intent_sequence)
			!= FT_ERR_SUCCESS)
		{
			(void)session.destroy();
			return (1);
		}
	}
	(void)session.destroy();
	return (FT_ERR_SUCCESS);
}

int RegenerationCardValidator::validate() const
{
	uint32_t opening_hand_size;
	if (RegenerationCardValidator::validate_sequenced_choice_expiry() != 0)
	{
		std::fprintf(stderr,
			"regeneration-cards: sequenced choice expiry check failed\n");
		return (1);
	}
	if (RegenerationCardValidator::validate_stabilizer_roll(0U, 0U,
			0x5354414253554343ULL) != FT_ERR_SUCCESS
		|| RegenerationCardValidator::validate_stabilizer_roll(10000U, 1U,
			0x535441424F564552ULL) != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"regeneration-cards: stabilizer RNG/redaction/retry check failed\n");
		return (1);
	}

	if (regeneration_card_validate_previews() != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"regeneration-cards: preview metadata validation failed\n");
		return (1);
	}
	opening_hand_size = 4U;
	while (opening_hand_size <= 7U)
	{
		if (RegenerationCardValidator::validate_opening_hand(
				opening_hand_size, 0x4D43520000000000ULL + opening_hand_size)
			!= FT_ERR_SUCCESS)
		{
			std::fprintf(stderr,
				"regeneration-cards: starter deck check failed for hand=%u\n",
				opening_hand_size);
			return (1);
		}
		++opening_hand_size;
	}
	if (regeneration_card_validate_double_draft() != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"regeneration-cards: draw/discard policy check failed\n");
		return (1);
	}
	if (regeneration_card_validate_starting_hand_deal() != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"regeneration-cards: starting-hand deal was not idempotent/persistent\n");
		return (1);
	}
	if (regeneration_card_validate_physical_item_import() != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"regeneration-cards: physical card import check failed\n");
		return (1);
	}
	if (regeneration_card_validate_world_rules() != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"regeneration-cards: session-world-rules check failed\n");
		return (1);
	}
	std::printf("regeneration-cards: ok deck=20 definitions=7 opening=4..7 draw=2 discard=1 physical-transfer=ok world-rules=ok\n");
	return (FT_ERR_SUCCESS);
}
