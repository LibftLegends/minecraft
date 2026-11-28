#include "../../src/cards/RegenerationCardSession.hpp"
#include "../../src/inventory/MinecraftItemCatalog.hpp"

#include <algorithm>
#include <new>

const uint32_t RegenerationCardSession::PLAYER_ID = 0U;
const uint32_t RegenerationCardSession::CARD_TYPE_ID = 4U;
const uint32_t RegenerationCardSession::DRAW_ZONE_ID = 2U;
const uint32_t RegenerationCardSession::HAND_ZONE_ID = 3U;
const uint32_t RegenerationCardSession::RESOLUTION_ZONE_ID = 4U;
const uint32_t RegenerationCardSession::DISCARD_ZONE_ID = 5U;
const uint32_t RegenerationCardSession::BURNED_ZONE_ID = 6U;
const uint32_t RegenerationCardSession::POLICY_INTENT_TYPE_ID = 0x4D435250U;
const uint32_t RegenerationCardSession::CARD_EFFECT_EVENT_BASE = 0x4D430100U;
const uint32_t RegenerationCardSession::EMPTY_DRAW_INTENT_TYPE_ID = 0x4D435245U;
const uint32_t RegenerationCardSession::HAND_OVERFLOW_INTENT_TYPE_ID = 0x4D43524FU;

static uint32_t regeneration_card_type_id(uint32_t type_base,
	uint32_t card_definition_id) noexcept
{
	return (type_base + card_definition_id - 1U);
}

static void regeneration_payload_write_u32(uint8_t *payload,
	uint32_t offset, uint32_t value) noexcept
{
	for (uint32_t byte_index = 0U; byte_index < 4U; ++byte_index)
		payload[offset + byte_index] = static_cast<uint8_t>(value
			>> (byte_index * 8U));
}

static uint32_t regeneration_payload_read_u32(const uint8_t *payload,
	uint32_t offset) noexcept
{
	return (static_cast<uint32_t>(payload[offset])
		| (static_cast<uint32_t>(payload[offset + 1U]) << 8U)
		| (static_cast<uint32_t>(payload[offset + 2U]) << 16U)
		| (static_cast<uint32_t>(payload[offset + 3U]) << 24U));
}

static void regeneration_payload_write_u64(uint8_t *payload,
	uint32_t offset, uint64_t value) noexcept
{
	regeneration_payload_write_u32(payload, offset,
		static_cast<uint32_t>(value));
	regeneration_payload_write_u32(payload, offset + 4U,
		static_cast<uint32_t>(value >> 32U));
}

static uint64_t regeneration_payload_read_u64(const uint8_t *payload,
	uint32_t offset) noexcept
{
	return (static_cast<uint64_t>(regeneration_payload_read_u32(payload, offset))
		| (static_cast<uint64_t>(regeneration_payload_read_u32(payload,
			offset + 4U)) << 32U));
}

RegenerationCardSession::RegenerationCardSession() : engine_(), script_bridge_(),
	play_cache_(), draw_cache_(), discard_cache_(), expiry_cache_(),
	choice_cache_(),
	card_import_cache_(),
	card_export_cache_(), physical_card_instances_(),
	highest_card_transfer_request_id_(0U),
	action_session_id_(0U),
	next_imported_card_instance_id_(0x80000000U), next_command_sequence_(1U),
	starting_hand_request_id_(0U), starting_hand_count_(4U),
	pending_double_draft_intent_sequence_(0U),
	pending_required_discard_count_(0U),
	active_request_id_(0U), active_choice_(), active_private_result_(0U),
	active_private_ordinal_(0U), initialized_(false)
{
}

int32_t RegenerationCardSession::set_action_session_id(
	uint64_t session_id) noexcept
{
	if (session_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	this->action_session_id_ = session_id;
	return (FT_ERR_SUCCESS);
}

RegenerationCardSession::~RegenerationCardSession()
{
	(void)this->destroy();
}

uint32_t RegenerationCardSession::card_definition_id_for_event(
	uint32_t event_type) noexcept
{
	if (event_type < RegenerationCardSession::CARD_EFFECT_EVENT_BASE + 1U
		|| (event_type > RegenerationCardSession::CARD_EFFECT_EVENT_BASE
				+ RegenerationCardSession::STARTER_CARD_DEFINITION_COUNT
			&& !RegenerationCardSession::is_supported_card_definition(
				event_type
					- RegenerationCardSession::CARD_EFFECT_EVENT_BASE)))
		return (0U);
	return (event_type - RegenerationCardSession::CARD_EFFECT_EVENT_BASE);
}

RegenerationCardEffectKind RegenerationCardSession::effect_kind_for_card(
	uint32_t card_definition_id) noexcept
{
	switch (card_definition_id)
	{
		case 1U:
			return (REGENERATION_CARD_EFFECT_BIOME_EXCLUSION);
		case 2U:
			return (REGENERATION_CARD_EFFECT_DEEP_SEAM);
		case 3U:
			return (REGENERATION_CARD_EFFECT_VILLAGE_CHARTER);
		case 4U:
			return (REGENERATION_CARD_EFFECT_SURFACE_MAGMA);
		case 5U:
			return (REGENERATION_CARD_EFFECT_DOUBLE_DRAFT);
		case 6U:
			return (REGENERATION_CARD_EFFECT_BIOME_COMPASS);
		case 7U:
			return (REGENERATION_CARD_EFFECT_SURVEYORS_REACH);
		case RegenerationCardSession::ORE_OVERGROWTH_CARD_DEFINITION_ID:
			return (REGENERATION_CARD_EFFECT_ORE_OVERGROWTH);
		case RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID:
			return (REGENERATION_CARD_EFFECT_ULTIMATE_GREED);
		case RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID:
			return (REGENERATION_CARD_EFFECT_BIOME_RUPTURE);
		case RegenerationCardSession::FORTUNES_DRAW_CARD_DEFINITION_ID:
			return (REGENERATION_CARD_EFFECT_FORTUNES_DRAW);
		case RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID:
			return (REGENERATION_CARD_EFFECT_VOLCANO_MARK);
		case RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID:
			return (REGENERATION_CARD_EFFECT_VOLATILE_STABILIZER);
		case RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID:
			return (REGENERATION_CARD_EFFECT_RUINED_VILLAGE_RELIC);
		case RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID:
			return (REGENERATION_CARD_EFFECT_SHIPWRECK_CHART);
		case RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID:
			return (REGENERATION_CARD_EFFECT_UNSTABLE_CATALYST);
		case RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID:
			return (REGENERATION_CARD_EFFECT_DESERT_PYRAMID_SEAL);
		case RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID:
			return (REGENERATION_CARD_EFFECT_ANCIENT_CANOPY_SEAL);
		case RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID:
			return (REGENERATION_CARD_EFFECT_SPIDER_NEST_SEAL);
		default:
			return (static_cast<RegenerationCardEffectKind>(0));
	}
}

bool RegenerationCardSession::is_supported_card_definition(
	uint32_t card_definition_id) noexcept
{
	return ((card_definition_id >= 1U
			&& card_definition_id
				<= RegenerationCardSession::STARTER_CARD_DEFINITION_COUNT)
		|| card_definition_id
			== RegenerationCardSession::ORE_OVERGROWTH_CARD_DEFINITION_ID
		|| card_definition_id
			== RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID
		|| card_definition_id
			== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID
		|| card_definition_id
			== RegenerationCardSession::FORTUNES_DRAW_CARD_DEFINITION_ID
		|| card_definition_id
			== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID
		|| card_definition_id
			== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID
		|| card_definition_id
			== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID
		|| card_definition_id
			== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID
		|| (card_definition_id >=
				RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID
			&& card_definition_id <=
				RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID));
}

int32_t RegenerationCardSession::get_card_preview(uint32_t card_definition_id,
	RegenerationCardPreview *preview_out) noexcept
{
	static const RegenerationCardPreview previews[] = {
		{1U, "Biome Exclusion", "Exclude the selected biome from eligible generation zones.", REGENERATION_CARD_CHOICE_BIOME_ID, 1U, 0U},
		{2U, "Deep Seam", "Increase the configured generation weight for the selected ore.", REGENERATION_CARD_CHOICE_ORE_ID, 1U, 0U},
		{3U, "Village Charter", "Add the configured village plan to the selected revision.", REGENERATION_CARD_CHOICE_NONE, 0U, 0U},
		{4U, "Surface Magma", "Add bounded surface magma pools to the selected revision.", REGENERATION_CARD_CHOICE_NONE, 0U, 0U},
		{5U, "Double Draft", "Draw two cards and then discard one hand card.", REGENERATION_CARD_CHOICE_NONE, 0U, 0U},
		{6U, "Biome Compass", "Adjust eligible biome selection weights by the configured amount.", REGENERATION_CARD_CHOICE_NONE, 0U, 0U},
		{7U, "Surveyor's Reach", "Extend the revision scope from the selected chunk.", REGENERATION_CARD_CHOICE_TARGET_CHUNK, 1U, 0U},
		{RegenerationCardSession::ORE_OVERGROWTH_CARD_DEFINITION_ID, "Ore Overgrowth", "Increase ore generation and add the configured instability cost.", REGENERATION_CARD_CHOICE_ORE_ID, 1U, 0U},
		{RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID, "Ultimate Greed", "Draw three cards and apply the configured instability cost.", REGENERATION_CARD_CHOICE_NONE, 0U, 0U},
		{RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID, "Biome Rupture", "Remove two selected biomes and apply the configured instability cost.", REGENERATION_CARD_CHOICE_BIOME_ID, 2U, 0U},
		{RegenerationCardSession::FORTUNES_DRAW_CARD_DEFINITION_ID, "Fortune's Draw", "Draw one card.", REGENERATION_CARD_CHOICE_NONE, 0U, 0U},
		{RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID, "Volcano Mark", "Reserve a configured paid chunk square for the volcanic feature.", REGENERATION_CARD_CHOICE_NONE, 0U, 9U},
		{RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID, "Volatile Stabilizer", "Immediately reduce instability or trigger an irreversible overload.", REGENERATION_CARD_CHOICE_NONE, 0U, 0U},
		{RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID, "Ruined Village Relic", "Reserve a chunk for a ruined village with hostile spawners and rare loot.", REGENERATION_CARD_CHOICE_NONE, 0U, 0U},
		{RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID, "Shipwreck Chart", "Generate a navigable underwater shipwreck with hostile spawners and loot.", REGENERATION_CARD_CHOICE_NONE, 0U, 0U},
		{RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID, "Unstable Catalyst", "Add a hidden permanent danger modifier and instability.", REGENERATION_CARD_CHOICE_NONE, 0U, 0U},
		{RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID, "Desert Pyramid Seal", "Generate a sandstone pyramid with reachable central loot and hostile spawners.", REGENERATION_CARD_CHOICE_NONE, 0U, 0U},
		{RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID, "Ancient Canopy Seal", "Generate a climbable giant forest tree with hostile chambers and loot.", REGENERATION_CARD_CHOICE_NONE, 0U, 0U},
		{RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID, "Spider Nest Seal", "Connect a spider nest to an existing cave system.", REGENERATION_CARD_CHOICE_NONE, 0U, 0U}};
	std::size_t index = 0U;

	if (preview_out == nullptr || card_definition_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	while (index < sizeof(previews) / sizeof(previews[0]))
	{
		if (previews[index].card_definition_id == card_definition_id)
		{
			*preview_out = previews[index];
			return (FT_ERR_SUCCESS);
		}
		++index;
	}
	return (FT_ERR_NOT_FOUND);
}

ft_bool RegenerationCardSession::choice_is_valid(
	uint32_t card_definition_id,
	const RegenerationCardChoice &choice) noexcept
{
	if (card_definition_id == 1U)
		return (choice.kind == REGENERATION_CARD_CHOICE_BIOME_ID
			&& choice.value_primary != 0U && choice.value_secondary == 0U
			? FT_TRUE : FT_FALSE);
	if (card_definition_id == 2U
		|| card_definition_id
			== RegenerationCardSession::ORE_OVERGROWTH_CARD_DEFINITION_ID)
		return (choice.kind == REGENERATION_CARD_CHOICE_ORE_ID
			&& choice.value_primary != 0U && choice.value_secondary == 0U
			? FT_TRUE : FT_FALSE);
	if (card_definition_id == 7U)
		return (choice.kind == REGENERATION_CARD_CHOICE_TARGET_CHUNK
			? FT_TRUE : FT_FALSE);
	if (card_definition_id >= 3U && card_definition_id <= 6U)
		return (choice.kind == REGENERATION_CARD_CHOICE_NONE
			&& choice.value_primary == 0U && choice.value_secondary == 0U
			? FT_TRUE : FT_FALSE);
	if (card_definition_id
		== RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID)
		return (choice.kind == REGENERATION_CARD_CHOICE_NONE
			&& choice.value_primary == 0U && choice.value_secondary == 0U
			? FT_TRUE : FT_FALSE);
	if (card_definition_id
		== RegenerationCardSession::FORTUNES_DRAW_CARD_DEFINITION_ID)
		return (choice.kind == REGENERATION_CARD_CHOICE_NONE
			&& choice.value_primary == 0U && choice.value_secondary == 0U
			? FT_TRUE : FT_FALSE);
	if (card_definition_id
		== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID)
		return (choice.kind == REGENERATION_CARD_CHOICE_NONE
			&& choice.value_primary == 0U && choice.value_secondary == 0U
			? FT_TRUE : FT_FALSE);
	if (card_definition_id
		== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
		return (choice.kind == REGENERATION_CARD_CHOICE_NONE
			&& choice.value_primary == 0U && choice.value_secondary == 0U
			? FT_TRUE : FT_FALSE);
	if (card_definition_id
		== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID)
		return (choice.kind == REGENERATION_CARD_CHOICE_NONE
			&& choice.value_primary == 0U && choice.value_secondary == 0U
			? FT_TRUE : FT_FALSE);
	if (card_definition_id
		== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID)
		return (choice.kind == REGENERATION_CARD_CHOICE_NONE
			&& choice.value_primary == 0U && choice.value_secondary == 0U
			? FT_TRUE : FT_FALSE);
	if (card_definition_id >=
			RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID
		&& card_definition_id <=
			RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID)
		return (choice.kind == REGENERATION_CARD_CHOICE_NONE
			&& choice.value_primary == 0U && choice.value_secondary == 0U
			? FT_TRUE : FT_FALSE);
	if (card_definition_id
		== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID)
		return (choice.kind == REGENERATION_CARD_CHOICE_BIOME_ID
			&& choice.value_primary != 0U && choice.value_secondary != 0U
			&& choice.value_primary != choice.value_secondary
			? FT_TRUE : FT_FALSE);
	return (FT_FALSE);
}

int32_t RegenerationCardSession::validate_choice_in_world(
	uint32_t card_definition_id, const RegenerationCardChoice &choice,
	const voxel_generation_config &world_config) noexcept
{
	uint32_t index;

	if (voxel_generation_config_is_valid(world_config) != FT_TRUE)
		return (FT_ERR_CONFIGURATION);
	if (RegenerationCardSession::choice_is_valid(card_definition_id, choice)
		!= FT_TRUE)
		return (FT_ERR_INVALID_ARGUMENT);
	if (card_definition_id == 1U)
	{
		if (world_config.biome_count <= 1U)
			return (FT_ERR_OUT_OF_RANGE);
		if (choice.value_primary > world_config.biome_count)
			return (FT_ERR_NOT_FOUND);
		return (FT_ERR_SUCCESS);
	}
	if (card_definition_id
		== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID)
	{
		if (world_config.biome_count <= 1U)
			return (FT_ERR_OUT_OF_RANGE);
		if (choice.value_primary > world_config.biome_count
			|| choice.value_secondary > world_config.biome_count)
			return (FT_ERR_NOT_FOUND);
		return (FT_ERR_SUCCESS);
	}
	if (card_definition_id == 2U
		|| card_definition_id
			== RegenerationCardSession::ORE_OVERGROWTH_CARD_DEFINITION_ID)
	{
		index = 0U;
		while (index < world_config.ore_rule_count)
		{
			if (world_config.ores[index].enabled == FT_TRUE
				&& world_config.ores[index].block_id == choice.value_primary)
				return (FT_ERR_SUCCESS);
			++index;
		}
		return (FT_ERR_NOT_FOUND);
	}
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::effect_callback(
	const card_game_engine &engine, const card_game_effect_context &context,
	card_game_operation_buffer &operations, void *user_data) noexcept
{
	RegenerationCardSession *session;
	card_game_host_intent intent = {};
	uint32_t stabilizer_roll = 0U;
	const uint32_t card_definition_id =
		RegenerationCardSession::card_definition_id_for_event(context.event_type);

	(void)engine;
	if (user_data == nullptr || context.source_instance == 0U
		|| context.target_instance != card_definition_id || card_definition_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	session = static_cast<RegenerationCardSession *>(user_data);
	const auto clear_active_context = [session]() noexcept
	{
		session->active_request_id_ = 0U;
		session->active_choice_ = {};
		session->active_private_result_ = 0U;
		session->active_private_ordinal_ = 0U;
		session->active_catalyst_option_count_ = 0U;
		std::fill(session->active_catalyst_options_,
			session->active_catalyst_options_ + 7U, 0U);
	};
	if (regeneration_payload_read_u32(context.play_payload, 60U)
		== 0x52435031U)
	{
		const uint64_t payload_request_id = regeneration_payload_read_u64(
			context.play_payload, 0U);
		if (context.action_request_id != 0U
			&& context.action_request_id != payload_request_id)
		{
			clear_active_context();
			return (FT_ERR_INVALID_ARGUMENT);
		}
		session->active_request_id_ = regeneration_payload_read_u64(
			context.play_payload, 0U);
		session->active_choice_.kind = static_cast<RegenerationCardChoiceKind>(
			regeneration_payload_read_u32(context.play_payload, 8U));
		session->active_choice_.value_primary =
			regeneration_payload_read_u32(context.play_payload, 12U);
		session->active_choice_.value_secondary =
			regeneration_payload_read_u32(context.play_payload, 16U);
		session->active_private_result_ =
			regeneration_payload_read_u32(context.play_payload, 20U);
		session->active_private_ordinal_ =
			regeneration_payload_read_u32(context.play_payload, 24U);
		session->active_catalyst_option_count_ =
			regeneration_payload_read_u32(context.play_payload, 28U);
		if (session->active_catalyst_option_count_ > 7U)
		{
			clear_active_context();
			return (FT_ERR_INVALID_ARGUMENT);
		}
		for (uint32_t option_index = 0U; option_index < 7U; ++option_index)
			session->active_catalyst_options_[option_index] =
				regeneration_payload_read_u32(context.play_payload,
					32U + option_index * 4U);
		if (session->active_request_id_ == 0U
			|| RegenerationCardSession::choice_is_valid(card_definition_id,
				session->active_choice_) != FT_TRUE)
		{
			clear_active_context();
			return (FT_ERR_INVALID_ARGUMENT);
		}
		if (card_definition_id
			== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID
			&& (session->active_private_result_ > 10000U
				|| session->active_private_ordinal_ == 0U))
		{
			clear_active_context();
			return (FT_ERR_CONFIGURATION);
		}
		if (card_definition_id
			== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID
			&& (session->active_private_result_ != 0U
				|| session->active_private_ordinal_ == 0U))
		{
			clear_active_context();
			return (FT_ERR_INVALID_ARGUMENT);
		}
		if (card_definition_id
			!= RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID
			&& card_definition_id
			!= RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID
			&& (session->active_private_result_ != 0U
				|| session->active_private_ordinal_ != 0U))
		{
			clear_active_context();
			return (FT_ERR_INVALID_ARGUMENT);
		}
		if (card_definition_id
			== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
		{
			if (session->active_catalyst_option_count_ == 0U)
			{
				clear_active_context();
				return (FT_ERR_CONFIGURATION);
			}
			for (uint32_t option_index = 0U;
				option_index < session->active_catalyst_option_count_;
				++option_index)
			{
				const uint32_t selected_option =
					session->active_catalyst_options_[option_index];
				if (selected_option == 0U || selected_option > 7U)
				{
					clear_active_context();
					return (FT_ERR_INVALID_ARGUMENT);
				}
				for (uint32_t prior_index = 0U; prior_index < option_index;
					++prior_index)
					if (session->active_catalyst_options_[prior_index]
						== selected_option)
					{
						clear_active_context();
						return (FT_ERR_ALREADY_EXISTS);
					}
			}
		}
		else if (session->active_catalyst_option_count_ != 0U)
		{
			clear_active_context();
			return (FT_ERR_INVALID_ARGUMENT);
		}
	}
	if (!session->initialized_ || session->active_request_id_ == 0U)
	{
		clear_active_context();
		return (FT_ERR_INVALID_STATE);
	}
	if (card_definition_id
		== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
	{
		int32_t random_result = session->engine_.random_bounded(10000U,
			&stabilizer_roll);
		if (random_result != FT_ERR_SUCCESS)
		{
			clear_active_context();
			return (random_result);
		}
		session->active_private_result_ = stabilizer_roll
			< session->active_private_result_ ? 1U : 0U;
	}
	else if (card_definition_id
		== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
	{
		if (session->active_catalyst_option_count_ == 0U
			|| session->active_catalyst_option_count_ > 7U)
		{
			clear_active_context();
			return (FT_ERR_CONFIGURATION);
		}
		int32_t random_result = session->engine_.random_bounded(
			session->active_catalyst_option_count_,
			&stabilizer_roll);
		if (random_result != FT_ERR_SUCCESS)
		{
			clear_active_context();
			return (random_result);
		}
		session->active_private_result_ =
			session->active_catalyst_options_[stabilizer_roll];
	}
	intent.type_id = RegenerationCardSession::POLICY_INTENT_TYPE_ID;
	intent.version = RegenerationCardSession::CONTENT_VERSION;
	intent.field_count = 8U;
	intent.fields[0].type = CARD_GAME_HOST_INTENT_U32;
	intent.fields[0].value = card_definition_id;
	intent.fields[1].type = CARD_GAME_HOST_INTENT_U32;
	intent.fields[1].value = context.source_instance;
	intent.fields[2].type = CARD_GAME_HOST_INTENT_U32;
	intent.fields[2].value = static_cast<uint32_t>(
		RegenerationCardSession::effect_kind_for_card(card_definition_id));
	intent.fields[3].type = CARD_GAME_HOST_INTENT_U32;
	intent.fields[3].value = static_cast<uint32_t>(
		session->active_choice_.kind);
	intent.fields[4].type = CARD_GAME_HOST_INTENT_U32;
	intent.fields[4].value = card_definition_id
		== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID
		|| card_definition_id
			== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID
		? session->active_private_result_
		: session->active_choice_.value_primary;
	intent.fields[5].type = CARD_GAME_HOST_INTENT_U32;
	intent.fields[5].value = card_definition_id
		== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID
		|| card_definition_id
			== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID
		? session->active_private_ordinal_
		: session->active_choice_.value_secondary;
	intent.fields[6].type = CARD_GAME_HOST_INTENT_U32;
	intent.fields[6].value = RegenerationCardSession::CONTENT_VERSION;
	intent.fields[7].type = CARD_GAME_HOST_INTENT_U64;
	intent.fields[7].value = session->active_request_id_;
	const int32_t append_result = operations.append_host_intent(intent);
	session->active_request_id_ = 0U;
	session->active_choice_ = {};
	session->active_private_result_ = 0U;
	session->active_private_ordinal_ = 0U;
	session->active_catalyst_option_count_ = 0U;
	std::fill(session->active_catalyst_options_,
		session->active_catalyst_options_ + 7U, 0U);
	return (append_result);
}

int32_t RegenerationCardSession::register_profile() noexcept
{
	card_game_rules rules = {};
	card_game_card_type_definition card_type = {};
	card_game_zone_definition zone = {};
	card_game_host_intent_definition intent_definition = {};
	card_game_match_start_config start_config = {};
	uint32_t card_definition_id;
	uint32_t effect_id;
	int32_t error_code;

	rules.max_board_spaces = 1U;
	rules.max_hand_size = RegenerationCardSession::MAXIMUM_HAND_SIZE;
	rules.starting_health = 20U;
	rules.starting_mana = 0U;
	rules.max_mana = 1U;
	rules.max_turns = 1U;
	error_code = this->engine_.initialize(rules);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	card_type.allowed_zone_mask = (1U << RegenerationCardSession::DRAW_ZONE_ID)
		| (1U << RegenerationCardSession::HAND_ZONE_ID)
		| (1U << RegenerationCardSession::RESOLUTION_ZONE_ID)
		| (1U << RegenerationCardSession::DISCARD_ZONE_ID)
		| (1U << RegenerationCardSession::BURNED_ZONE_ID);
	card_type.max_copies_per_player = 4U;
	error_code = FT_ERR_SUCCESS;
	for (uint32_t definition_id = 1U;
		definition_id
			<= RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID
			&& error_code == FT_ERR_SUCCESS; ++definition_id)
	{
		card_type.type_id = regeneration_card_type_id(
			RegenerationCardSession::CARD_TYPE_ID, definition_id);
		error_code = this->engine_.register_card_type(card_type);
	}
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	card_type.allowed_zone_mask = 0U;
	for (uint32_t definition_id = 1U;
		definition_id
			<= RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID;
		++definition_id)
		card_type.allowed_zone_mask |= 1U
			<< regeneration_card_type_id(
				RegenerationCardSession::CARD_TYPE_ID, definition_id);
	zone.capacity = RegenerationCardSession::MAXIMUM_DECK_SIZE;
	zone.allowed_card_type_mask = card_type.allowed_zone_mask;
	zone.owner_scoped = FT_TRUE;
	zone.zone_id = RegenerationCardSession::DRAW_ZONE_ID;
	error_code = this->engine_.register_zone(zone);
	if (error_code == FT_ERR_SUCCESS)
	{
		zone.zone_id = RegenerationCardSession::HAND_ZONE_ID;
		zone.capacity = RegenerationCardSession::MAXIMUM_HAND_SIZE;
		error_code = this->engine_.register_zone(zone);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		zone.zone_id = RegenerationCardSession::RESOLUTION_ZONE_ID;
		zone.capacity = RegenerationCardSession::MAXIMUM_DECK_SIZE;
		error_code = this->engine_.register_zone(zone);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		zone.zone_id = RegenerationCardSession::DISCARD_ZONE_ID;
		error_code = this->engine_.register_zone(zone);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		zone.zone_id = RegenerationCardSession::BURNED_ZONE_ID;
		error_code = this->engine_.register_zone(zone);
	}
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	intent_definition.type_id =
		RegenerationCardSession::POLICY_INTENT_TYPE_ID;
	intent_definition.version = RegenerationCardSession::CONTENT_VERSION;
	intent_definition.field_count = 8U;
	intent_definition.field_types[0] = CARD_GAME_HOST_INTENT_U32;
	intent_definition.field_types[1] = CARD_GAME_HOST_INTENT_U32;
	intent_definition.field_types[2] = CARD_GAME_HOST_INTENT_U32;
	intent_definition.field_types[3] = CARD_GAME_HOST_INTENT_U32;
	intent_definition.field_types[4] = CARD_GAME_HOST_INTENT_U32;
	intent_definition.field_types[5] = CARD_GAME_HOST_INTENT_U32;
	intent_definition.field_types[6] = CARD_GAME_HOST_INTENT_U32;
	intent_definition.field_types[7] = CARD_GAME_HOST_INTENT_U64;
	error_code = this->engine_.register_host_intent_type(intent_definition);
	intent_definition.type_id = RegenerationCardSession::EMPTY_DRAW_INTENT_TYPE_ID;
	intent_definition.field_count = 2U;
	intent_definition.field_types[0] = CARD_GAME_HOST_INTENT_U32;
	intent_definition.field_types[1] = CARD_GAME_HOST_INTENT_U32;
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->engine_.register_host_intent_type(intent_definition);
	intent_definition.type_id =
		RegenerationCardSession::HAND_OVERFLOW_INTENT_TYPE_ID;
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->engine_.register_host_intent_type(intent_definition);
	card_definition_id = 1U;
	while (card_definition_id
		<= RegenerationCardSession::STARTER_CARD_DEFINITION_COUNT
		&& error_code == FT_ERR_SUCCESS)
	{
		card_game_card_definition definition = {};

		definition.card_id = card_definition_id;
		definition.type = CARD_GAME_SPELL;
		definition.cost = 0U;
		definition.attack = 0;
		definition.health = 0;
		definition.effect_id = CARD_GAME_NO_EFFECT;
		error_code = this->engine_.register_card_with_type(definition,
			regeneration_card_type_id(RegenerationCardSession::CARD_TYPE_ID, definition.card_id));
		if (error_code == FT_ERR_SUCCESS)
			error_code = this->engine_.register_effect_callback(
				&RegenerationCardSession::effect_callback, this,
				RegenerationCardSession::CARD_EFFECT_EVENT_BASE
					+ card_definition_id, &effect_id);
		++card_definition_id;
	}
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	{
		card_game_card_definition definition = {};

		definition.card_id =
			RegenerationCardSession::ORE_OVERGROWTH_CARD_DEFINITION_ID;
		definition.type = CARD_GAME_SPELL;
		definition.effect_id = CARD_GAME_NO_EFFECT;
		error_code = this->engine_.register_card_with_type(definition,
			regeneration_card_type_id(RegenerationCardSession::CARD_TYPE_ID, definition.card_id));
		if (error_code == FT_ERR_SUCCESS)
			error_code = this->engine_.register_effect_callback(
				&RegenerationCardSession::effect_callback, this,
				RegenerationCardSession::CARD_EFFECT_EVENT_BASE
					+ RegenerationCardSession::ORE_OVERGROWTH_CARD_DEFINITION_ID,
				&effect_id);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		card_game_card_definition definition = {};
		definition.card_id =
			RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID;
		definition.type = CARD_GAME_SPELL;
		definition.effect_id = CARD_GAME_NO_EFFECT;
		error_code = this->engine_.register_card_with_type(definition,
			regeneration_card_type_id(RegenerationCardSession::CARD_TYPE_ID, definition.card_id));
		if (error_code == FT_ERR_SUCCESS)
			error_code = this->engine_.register_effect_callback(
				&RegenerationCardSession::effect_callback, this,
				RegenerationCardSession::CARD_EFFECT_EVENT_BASE
					+ RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID,
				&effect_id);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		card_game_card_definition definition = {};

		definition.card_id =
			RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID;
		definition.type = CARD_GAME_SPELL;
		definition.effect_id = CARD_GAME_NO_EFFECT;
		error_code = this->engine_.register_card_with_type(definition,
			regeneration_card_type_id(RegenerationCardSession::CARD_TYPE_ID, definition.card_id));
		if (error_code == FT_ERR_SUCCESS)
			error_code = this->engine_.register_effect_callback(
				&RegenerationCardSession::effect_callback, this,
				RegenerationCardSession::CARD_EFFECT_EVENT_BASE
					+ RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID,
				&effect_id);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		card_game_card_definition definition = {};

		definition.card_id =
			RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID;
		definition.type = CARD_GAME_SPELL;
		definition.effect_id = CARD_GAME_NO_EFFECT;
		error_code = this->engine_.register_card_with_type(definition,
			regeneration_card_type_id(RegenerationCardSession::CARD_TYPE_ID, definition.card_id));
		if (error_code == FT_ERR_SUCCESS)
			error_code = this->engine_.register_effect_callback(
				&RegenerationCardSession::effect_callback, this,
				RegenerationCardSession::CARD_EFFECT_EVENT_BASE
					+ RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID,
				&effect_id);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		card_game_card_definition definition = {};

		definition.card_id =
			RegenerationCardSession::FORTUNES_DRAW_CARD_DEFINITION_ID;
		definition.type = CARD_GAME_SPELL;
		definition.effect_id = CARD_GAME_NO_EFFECT;
		error_code = this->engine_.register_card_with_type(definition,
			regeneration_card_type_id(RegenerationCardSession::CARD_TYPE_ID, definition.card_id));
		if (error_code == FT_ERR_SUCCESS)
			error_code = this->engine_.register_effect_callback(
				&RegenerationCardSession::effect_callback, this,
				RegenerationCardSession::CARD_EFFECT_EVENT_BASE
					+ RegenerationCardSession::FORTUNES_DRAW_CARD_DEFINITION_ID,
				&effect_id);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		card_game_card_definition definition = {};

		definition.card_id =
			RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID;
		definition.type = CARD_GAME_SPELL;
		definition.effect_id = CARD_GAME_NO_EFFECT;
		error_code = this->engine_.register_card_with_type(definition,
			regeneration_card_type_id(RegenerationCardSession::CARD_TYPE_ID, definition.card_id));
		if (error_code == FT_ERR_SUCCESS)
			error_code = this->engine_.register_effect_callback(
				&RegenerationCardSession::effect_callback, this,
				RegenerationCardSession::CARD_EFFECT_EVENT_BASE
					+ RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID,
				&effect_id);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		card_game_card_definition definition = {};

		definition.card_id =
			RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID;
		definition.type = CARD_GAME_SPELL;
		definition.effect_id = CARD_GAME_NO_EFFECT;
		error_code = this->engine_.register_card_with_type(definition,
			regeneration_card_type_id(RegenerationCardSession::CARD_TYPE_ID, definition.card_id));
		if (error_code == FT_ERR_SUCCESS)
			error_code = this->engine_.register_effect_callback(
				&RegenerationCardSession::effect_callback, this,
				RegenerationCardSession::CARD_EFFECT_EVENT_BASE
					+ RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID,
				&effect_id);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		card_game_card_definition definition = {};
		definition.card_id =
			RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID;
		definition.type = CARD_GAME_SPELL;
		definition.effect_id = CARD_GAME_NO_EFFECT;
		error_code = this->engine_.register_card_with_type(definition,
			regeneration_card_type_id(RegenerationCardSession::CARD_TYPE_ID, definition.card_id));
		if (error_code == FT_ERR_SUCCESS)
			error_code = this->engine_.register_effect_callback(
				&RegenerationCardSession::effect_callback, this,
				RegenerationCardSession::CARD_EFFECT_EVENT_BASE
					+ RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID,
				&effect_id);
	}
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	{
		const uint32_t rare_card_ids[] = {
			RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID,
			RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID,
			RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID,
			RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID};
		for (uint32_t rare_card_id : rare_card_ids)
		{
			card_game_card_definition definition = {};
			definition.card_id = rare_card_id;
			definition.type = CARD_GAME_SPELL;
			definition.effect_id = CARD_GAME_NO_EFFECT;
			error_code = this->engine_.register_card_with_type(definition,
				regeneration_card_type_id(RegenerationCardSession::CARD_TYPE_ID, definition.card_id));
			if (error_code == FT_ERR_SUCCESS)
				error_code = this->engine_.register_effect_callback(
					&RegenerationCardSession::effect_callback, this,
					RegenerationCardSession::CARD_EFFECT_EVENT_BASE + rare_card_id,
					&effect_id);
			if (error_code != FT_ERR_SUCCESS)
				break ;
		}
	}
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	card_game_draw_policy draw_policy = {};
	draw_policy.discard_zone_id = RegenerationCardSession::DISCARD_ZONE_ID;
	draw_policy.burned_zone_id = RegenerationCardSession::BURNED_ZONE_ID;
	draw_policy.reshuffle_discard = FT_TRUE;
	draw_policy.empty_draw_event_type_id =
		RegenerationCardSession::EMPTY_DRAW_INTENT_TYPE_ID;
	draw_policy.empty_draw_event_version = RegenerationCardSession::CONTENT_VERSION;
	draw_policy.overflow_event_type_id =
		RegenerationCardSession::HAND_OVERFLOW_INTENT_TYPE_ID;
	draw_policy.overflow_event_version = RegenerationCardSession::CONTENT_VERSION;
	error_code = this->engine_.configure_draw_policy(draw_policy);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	start_config.opening_hand_size = 0U;
	start_config.starting_health = 20U;
	start_config.starting_mana = 0U;
	start_config.first_player = RegenerationCardSession::PLAYER_ID;
	start_config.random_first_player = FT_FALSE;
	start_config.deal_opening_hand = FT_FALSE;
	return (this->engine_.start_match(1U, start_config));
}

int32_t RegenerationCardSession::seed_starter_deck(
	uint32_t opening_hand_size, uint64_t shuffle_seed) noexcept
{
	uint32_t card_definition_id = 1U;
	const uint32_t starter_card_copies[RegenerationCardSession::STARTER_CARD_DEFINITION_COUNT]
		= {4U, 4U, 2U, 2U, 2U, 4U, 2U};
	uint32_t index;
	uint32_t starter_deck_total = 0U;
	uint32_t actual_deck_total = 0U;
	uint32_t actual_card_copies[
		RegenerationCardSession::STARTER_CARD_DEFINITION_COUNT] = {};
	uint32_t actual_card_instance_ids[
		RegenerationCardSession::STARTER_DECK_INSTANCE_COUNT] = {};
	uint64_t initial_random_state;
	card_game_deck_card card = {};
	int32_t error_code;

	index = 0U;
	while (index < RegenerationCardSession::STARTER_CARD_DEFINITION_COUNT)
	{
		if (starter_card_copies[index] == 0U
			|| starter_card_copies[index] > 4U
			|| starter_deck_total > UINT32_MAX - starter_card_copies[index])
			return (FT_ERR_CONFIGURATION);
		starter_deck_total += starter_card_copies[index];
		++index;
	}
	if (starter_deck_total
		!= RegenerationCardSession::STARTER_DECK_INSTANCE_COUNT
		|| opening_hand_size > starter_deck_total)
		return (FT_ERR_CONFIGURATION);
	error_code = this->engine_.set_random_seed(shuffle_seed);
	while (card_definition_id
		<= RegenerationCardSession::STARTER_CARD_DEFINITION_COUNT
		&& error_code == FT_ERR_SUCCESS)
	{
		index = 0U;
		while (index < starter_card_copies[card_definition_id - 1U]
			&& error_code == FT_ERR_SUCCESS)
		{
			error_code = this->engine_.deck_push_bottom(
				RegenerationCardSession::PLAYER_ID, card_definition_id);
			++index;
		}
		++card_definition_id;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->engine_.get_deck_count(
			RegenerationCardSession::PLAYER_ID, &actual_deck_total);
	if (error_code == FT_ERR_SUCCESS
		&& actual_deck_total
			!= RegenerationCardSession::STARTER_DECK_INSTANCE_COUNT)
		error_code = FT_ERR_INVALID_STATE;
	index = 0U;
	while (error_code == FT_ERR_SUCCESS && index < actual_deck_total)
	{
		error_code = this->engine_.deck_inspect(
			RegenerationCardSession::PLAYER_ID, index, &card);
		if (error_code != FT_ERR_SUCCESS)
			break ;
		if (card.instance_id == 0U || card.card_id == 0U
			|| card.card_id > RegenerationCardSession::STARTER_CARD_DEFINITION_COUNT)
			error_code = FT_ERR_INVALID_STATE;
		else
		{
			uint32_t prior_index = 0U;
			while (prior_index < index
				&& actual_card_instance_ids[prior_index] != card.instance_id)
				++prior_index;
			if (prior_index != index)
				error_code = FT_ERR_ALREADY_EXISTS;
			else
				actual_card_instance_ids[index] = card.instance_id;
			actual_card_copies[card.card_id - 1U] += 1U;
		}
		++index;
	}
	index = 0U;
	while (error_code == FT_ERR_SUCCESS
		&& index < RegenerationCardSession::STARTER_CARD_DEFINITION_COUNT)
	{
		if (actual_card_copies[index] != starter_card_copies[index])
			error_code = FT_ERR_CONFIGURATION;
		++index;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->engine_.shuffle_deck(
			RegenerationCardSession::PLAYER_ID);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->engine_.get_random_state(&initial_random_state);
	if (error_code != FT_ERR_SUCCESS || initial_random_state == 0U)
		return (error_code == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE
			: error_code);
	index = 0U;
	while (index < opening_hand_size)
	{
		error_code = this->engine_.draw_to_hand(
			RegenerationCardSession::PLAYER_ID, &card);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		++index;
	}
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::initialize(uint32_t opening_hand_size,
	uint64_t shuffle_seed) noexcept
{
	int32_t error_code;

	if (this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (opening_hand_size < 4U || opening_hand_size > 7U
		|| opening_hand_size > RegenerationCardSession::MAXIMUM_HAND_SIZE
		|| shuffle_seed == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = this->register_profile();
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->seed_starter_deck(opening_hand_size, shuffle_seed);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->script_bridge_.initialize(this->engine_);
	if (error_code != FT_ERR_SUCCESS)
	{
		(void)this->script_bridge_.destroy();
		(void)this->engine_.destroy();
		return (error_code);
	}
	this->starting_hand_request_id_ = 0U;
	this->starting_hand_count_ = opening_hand_size;
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::get_starting_hand_deal_identity(
	uint64_t *request_id_out, uint32_t *opening_hand_count_out) const noexcept
{
	if (request_id_out == nullptr || opening_hand_count_out == nullptr
		|| !this->initialized_)
		return (FT_ERR_INVALID_ARGUMENT);
	*request_id_out = this->starting_hand_request_id_;
	*opening_hand_count_out = this->starting_hand_count_;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::preflight_starting_hand_deal(
	uint64_t request_id, uint32_t opening_hand_count) noexcept
{
	uint32_t hand_count = 0U;
	uint32_t draw_count = 0U;
	uint32_t pending_intent_count = 0U;
	int32_t error_code;

	if (!this->initialized_ || request_id == 0U
		|| opening_hand_count < 4U || opening_hand_count > 7U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->starting_hand_request_id_ != 0U)
		return (this->starting_hand_request_id_ == request_id
			&& this->starting_hand_count_ == opening_hand_count
			? FT_ERR_SUCCESS : FT_ERR_INVALID_ARGUMENT);
	if (!this->play_cache_.empty() || !this->draw_cache_.empty()
		|| !this->discard_cache_.empty() || !this->card_import_cache_.empty()
		|| !this->card_export_cache_.empty()
		|| this->highest_card_transfer_request_id_ != 0U
		|| this->next_command_sequence_ != 1U
		|| this->pending_required_discard_count_ != 0U)
		return (FT_ERR_INVALID_OPERATION);
	error_code = this->engine_.get_pending_host_intent_count(
		&pending_intent_count);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->engine_.get_hand_count(PLAYER_ID, &hand_count);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->engine_.get_deck_count(PLAYER_ID, &draw_count);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (pending_intent_count != 0U || hand_count != this->starting_hand_count_
		|| hand_count < 4U || opening_hand_count < hand_count
		|| draw_count < opening_hand_count - hand_count)
		return (FT_ERR_INVALID_OPERATION);
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::deal_starting_hand(uint64_t request_id,
	uint32_t opening_hand_count,
	RegenerationCardActionResult *result_out) noexcept
{
	RegenerationCardActionResult result = {};
	uint32_t hand_count = 0U;
	int32_t error_code = this->preflight_starting_hand_deal(request_id,
		opening_hand_count);

	if (result_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (this->starting_hand_request_id_ != 0U)
	{
		result.request_id = request_id;
		result.session_id = this->action_session_id_;
		result.count = opening_hand_count;
		result.replayed = true;
		*result_out = result;
		return (FT_ERR_SUCCESS);
	}
	error_code = this->engine_.get_hand_count(PLAYER_ID, &hand_count);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	result.request_id = request_id;
	result.session_id = this->action_session_id_;
	result.count = opening_hand_count;
	result.replayed = false;
	while (hand_count < opening_hand_count)
	{
		card_game_deck_card card = {};

		error_code = this->engine_.draw_to_hand(PLAYER_ID, &card);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		++hand_count;
	}
	this->starting_hand_request_id_ = request_id;
	this->starting_hand_count_ = opening_hand_count;
	*result_out = result;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::destroy() noexcept
{
	int32_t error_code;

	this->initialized_ = false;
	this->play_cache_.clear();
	this->draw_cache_.clear();
	this->discard_cache_.clear();
	this->expiry_cache_.clear();
	this->choice_cache_.clear();
	this->card_import_cache_.clear();
	this->card_export_cache_.clear();
	this->physical_card_instances_.clear();
	this->highest_card_transfer_request_id_ = 0U;
	this->next_imported_card_instance_id_ = 0x80000000U;
	this->next_command_sequence_ = 1U;
	this->starting_hand_request_id_ = 0U;
	this->starting_hand_count_ = 4U;
	this->pending_double_draft_intent_sequence_ = 0U;
	this->pending_required_discard_count_ = 0U;
	this->active_request_id_ = 0U;
	this->active_choice_ = {};
	error_code = this->script_bridge_.destroy();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = this->engine_.destroy();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->engine_.~card_game_engine();
	::new (&this->engine_) card_game_engine();
	return (FT_ERR_SUCCESS);
}

uint32_t RegenerationCardSession::card_definition_for_item(
	int32_t item_definition_id) noexcept
{
	switch (item_definition_id)
	{
		case MinecraftItemCatalog::BIOME_EXCLUSION_CARD_ITEM_ID: return (1U);
		case MinecraftItemCatalog::DEEP_SEAM_CARD_ITEM_ID: return (2U);
		case MinecraftItemCatalog::VILLAGE_CHARTER_CARD_ITEM_ID: return (3U);
		case MinecraftItemCatalog::SURFACE_MAGMA_CARD_ITEM_ID: return (4U);
		case MinecraftItemCatalog::DOUBLE_DRAFT_CARD_ITEM_ID: return (5U);
		case MinecraftItemCatalog::BIOME_COMPASS_CARD_ITEM_ID: return (6U);
		case MinecraftItemCatalog::SURVEYORS_REACH_CARD_ITEM_ID: return (7U);
		case MinecraftItemCatalog::VOLCANO_MARK_CARD_ITEM_ID:
			return (RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID);
		case MinecraftItemCatalog::ORE_OVERGROWTH_CARD_ITEM_ID:
			return (RegenerationCardSession::ORE_OVERGROWTH_CARD_DEFINITION_ID);
		case MinecraftItemCatalog::ULTIMATE_GREED_CARD_ITEM_ID:
			return (RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID);
		case MinecraftItemCatalog::BIOME_RUPTURE_CARD_ITEM_ID:
			return (RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID);
		case MinecraftItemCatalog::FORTUNES_DRAW_CARD_ITEM_ID:
			return (RegenerationCardSession::FORTUNES_DRAW_CARD_DEFINITION_ID);
		case MinecraftItemCatalog::VOLATILE_STABILIZER_CARD_ITEM_ID:
			return (RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID);
		case MinecraftItemCatalog::RUINED_VILLAGE_RELIC_CARD_ITEM_ID:
			return (RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID);
		case MinecraftItemCatalog::SHIPWRECK_CHART_CARD_ITEM_ID:
			return (RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID);
		case MinecraftItemCatalog::UNSTABLE_CATALYST_CARD_ITEM_ID:
			return (RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID);
		case MinecraftItemCatalog::DESERT_PYRAMID_SEAL_CARD_ITEM_ID:
			return (RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID);
		case MinecraftItemCatalog::ANCIENT_CANOPY_SEAL_CARD_ITEM_ID:
			return (RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID);
		case MinecraftItemCatalog::SPIDER_NEST_SEAL_CARD_ITEM_ID:
			return (RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID);
		default: return (0U);
	}
}

int32_t RegenerationCardSession::count_card_definition(
	uint32_t card_definition_id, uint32_t *count_out) const noexcept
{
	const uint32_t auxiliary_zone_ids[] = {
		RegenerationCardSession::RESOLUTION_ZONE_ID,
		RegenerationCardSession::DISCARD_ZONE_ID,
		RegenerationCardSession::BURNED_ZONE_ID};
	uint32_t total = 0U;

	if (count_out == nullptr || card_definition_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	uint32_t draw_count = 0U;
	uint32_t hand_count = 0U;
	int32_t error_code = this->engine_.get_deck_count(
		RegenerationCardSession::PLAYER_ID, &draw_count);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->engine_.get_hand_count(
			RegenerationCardSession::PLAYER_ID, &hand_count);
	for (uint32_t index = 0U; index < draw_count
		&& error_code == FT_ERR_SUCCESS; ++index)
	{
		card_game_deck_card card = {};
		error_code = this->engine_.deck_inspect(
			RegenerationCardSession::PLAYER_ID, index, &card);
		if (error_code == FT_ERR_SUCCESS && card.card_id == card_definition_id)
			++total;
	}
	for (uint32_t index = 0U; index < hand_count
		&& error_code == FT_ERR_SUCCESS; ++index)
	{
		card_game_deck_card card = {};
		error_code = this->engine_.hand_inspect(
			RegenerationCardSession::PLAYER_ID, index, &card);
		if (error_code == FT_ERR_SUCCESS && card.card_id == card_definition_id)
			++total;
	}
	for (uint32_t zone_index = 0U; error_code == FT_ERR_SUCCESS
		&& zone_index < sizeof(auxiliary_zone_ids)
			/ sizeof(auxiliary_zone_ids[0]); ++zone_index)
	{
		uint32_t count = 0U;
		error_code = this->engine_.get_zone_count(
			RegenerationCardSession::PLAYER_ID,
			auxiliary_zone_ids[zone_index], &count);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		for (uint32_t card_index = 0U; card_index < count; ++card_index)
		{
			card_game_deck_card card = {};
			error_code = this->engine_.zone_inspect(
				RegenerationCardSession::PLAYER_ID,
				auxiliary_zone_ids[zone_index],
				card_index, &card);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			if (card.card_id == card_definition_id)
				++total;
		}
	}
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	*count_out = total;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::count_owned_session_cards(
	uint32_t *count_out) const noexcept
{
	const uint32_t auxiliary_zone_ids[] = {
		RegenerationCardSession::RESOLUTION_ZONE_ID,
		RegenerationCardSession::DISCARD_ZONE_ID,
		RegenerationCardSession::BURNED_ZONE_ID};
	uint32_t total = 0U;
	uint32_t draw_count = 0U;
	uint32_t hand_count = 0U;

	if (count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	int32_t error_code = this->engine_.get_deck_count(
		RegenerationCardSession::PLAYER_ID, &draw_count);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->engine_.get_hand_count(
			RegenerationCardSession::PLAYER_ID, &hand_count);
	if (error_code != FT_ERR_SUCCESS
		|| draw_count > UINT32_MAX - hand_count)
		return (error_code == FT_ERR_SUCCESS ? FT_ERR_OUT_OF_RANGE : error_code);
	total = draw_count + hand_count;
	for (uint32_t zone_index = 0U;
		zone_index < sizeof(auxiliary_zone_ids)
			/ sizeof(auxiliary_zone_ids[0]); ++zone_index)
	{
		uint32_t count = 0U;
		error_code = this->engine_.get_zone_count(
			RegenerationCardSession::PLAYER_ID,
			auxiliary_zone_ids[zone_index], &count);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (count > UINT32_MAX - total)
			return (FT_ERR_OUT_OF_RANGE);
		total += count;
	}
	*count_out = total;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::import_card_item_to_deck(
	game_inventory &inventory, int32_t inventory_slot,
	uint64_t expected_inventory_revision, uint64_t request_id,
	card_game_deck_card *card_out) noexcept
{
	game_inventory_snapshot inventory_snapshot;
	game_inventory_item_snapshot item = {};
	card_game_snapshot engine_snapshot;
	std::map<uint64_t, CachedCardImport>::iterator cached;
	CachedCardImport request = {};
	uint32_t card_definition_id = 0U;
	uint32_t owned_card_count = 0U;
	uint32_t definition_copy_count = 0U;
	uint32_t instance_id = 0U;
	bool found_item = false;
	bool engine_snapshot_captured = false;
	bool engine_mutated = false;
	bool physical_mapping_inserted = false;
	bool engine_rollback_succeeded = true;
	int32_t error_code;

	if (!this->initialized_ || card_out == nullptr || request_id == 0U
		|| inventory_slot < 0)
		return (FT_ERR_INVALID_ARGUMENT);
	*card_out = {};
	cached = this->card_import_cache_.find(request_id);
	if (cached != this->card_import_cache_.end())
	{
		if (cached->second.inventory_slot != inventory_slot
			|| cached->second.expected_inventory_revision
				!= expected_inventory_revision)
			return (FT_ERR_INVALID_ARGUMENT);
		*card_out = cached->second.result_card;
		return (cached->second.result_code);
	}
	if (this->card_export_cache_.find(request_id)
		!= this->card_export_cache_.end()
		|| request_id <= this->highest_card_transfer_request_id_)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->card_import_cache_.size() + this->card_export_cache_.size()
		>= RegenerationCardSession::MAXIMUM_IMPORTED_CARD_REQUESTS)
		return (FT_ERR_FULL);
	request.inventory_slot = inventory_slot;
	request.expected_inventory_revision = expected_inventory_revision;
	request.result_code = FT_ERR_INVALID_STATE;
	try
	{
		cached = this->card_import_cache_.emplace(request_id, request).first;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	this->highest_card_transfer_request_id_ = request_id;
	error_code = inventory.get_snapshot(inventory_snapshot);
	if (error_code == FT_ERR_SUCCESS
		&& inventory_snapshot.revision != expected_inventory_revision)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
	{
		for (const game_inventory_item_snapshot &candidate
			: inventory_snapshot.items)
		{
			if (candidate.slot == inventory_slot)
			{
				item = candidate;
				found_item = true;
				break ;
			}
		}
		if (!found_item)
			error_code = FT_ERR_NOT_FOUND;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		card_definition_id = RegenerationCardSession::card_definition_for_item(
			item.item_id);
		if (item.instance_id == 0U || item.stack_size != 1
			|| card_definition_id == 0U)
			error_code = FT_ERR_INVALID_ARGUMENT;
		else if (this->physical_card_instances_.find(item.instance_id)
			!= this->physical_card_instances_.end())
			error_code = FT_ERR_ALREADY_EXISTS;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->count_owned_session_cards(&owned_card_count);
	if (error_code == FT_ERR_SUCCESS
		&& owned_card_count >= RegenerationCardSession::MAXIMUM_DECK_SIZE)
		error_code = FT_ERR_FULL;
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->count_card_definition(card_definition_id,
			&definition_copy_count);
	if (error_code == FT_ERR_SUCCESS && definition_copy_count >= 4U)
		error_code = FT_ERR_FULL;
	if (error_code == FT_ERR_SUCCESS)
	{
		uint32_t scanned = 0U;
		bool instance_used = false;
		while (scanned <= owned_card_count)
		{
			uint32_t draw_count = 0U;
			uint32_t hand_count = 0U;
			instance_id = this->next_imported_card_instance_id_;
			if (instance_id == 0U)
				instance_id = 0x80000000U;
			this->next_imported_card_instance_id_ = instance_id + 1U;
			if (this->next_imported_card_instance_id_ == 0U)
				this->next_imported_card_instance_id_ = 0x80000000U;
			instance_used = false;
			if (this->engine_.get_deck_count(
					RegenerationCardSession::PLAYER_ID, &draw_count)
					!= FT_ERR_SUCCESS
				|| this->engine_.get_hand_count(
					RegenerationCardSession::PLAYER_ID, &hand_count)
					!= FT_ERR_SUCCESS)
				error_code = FT_ERR_INVALID_STATE;
			for (uint32_t zone_kind = 0U; zone_kind < 5U && !instance_used
				&& error_code == FT_ERR_SUCCESS; ++zone_kind)
			{
				uint32_t entry_count = 0U;
				if (zone_kind == 0U)
					entry_count = draw_count;
				else if (zone_kind == 1U)
					entry_count = hand_count;
				else if (this->engine_.get_zone_count(
						RegenerationCardSession::PLAYER_ID, zone_kind + 2U,
						&entry_count) != FT_ERR_SUCCESS)
				{
					error_code = FT_ERR_INVALID_STATE;
					break ;
				}
				for (uint32_t index = 0U; index < entry_count; ++index)
				{
					card_game_deck_card existing = {};
					int32_t inspect_error;
					if (zone_kind == 0U)
						inspect_error = this->engine_.deck_inspect(
							RegenerationCardSession::PLAYER_ID, index, &existing);
					else if (zone_kind == 1U)
						inspect_error = this->engine_.hand_inspect(
							RegenerationCardSession::PLAYER_ID, index, &existing);
					else
						inspect_error = this->engine_.zone_inspect(
							RegenerationCardSession::PLAYER_ID, zone_kind + 2U,
							index, &existing);
					if (inspect_error != FT_ERR_SUCCESS)
					{
						error_code = FT_ERR_INVALID_STATE;
						break ;
					}
					if (existing.instance_id == instance_id)
					{
						instance_used = true;
						break ;
					}
				}
			}
			if (error_code != FT_ERR_SUCCESS || !instance_used)
				break ;
			++scanned;
		}
		if (scanned > owned_card_count)
			error_code = FT_ERR_FULL;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		try
		{
			PhysicalCardLink link = {};
			link.card_instance_id = instance_id;
			link.item = item;
			physical_mapping_inserted = this->physical_card_instances_.emplace(
				item.instance_id, link).second;
			if (!physical_mapping_inserted)
				error_code = FT_ERR_ALREADY_EXISTS;
		}
		catch (const std::bad_alloc &)
		{
			error_code = FT_ERR_NO_MEMORY;
		}
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = this->engine_.get_snapshot(&engine_snapshot);
		engine_snapshot_captured = (error_code == FT_ERR_SUCCESS);
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = this->engine_.deck_push_bottom_instance(
			RegenerationCardSession::PLAYER_ID, instance_id,
			card_definition_id);
		engine_mutated = (error_code == FT_ERR_SUCCESS);
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = inventory.remove_item_instance(inventory_slot,
			expected_inventory_revision, item.instance_id);
	if (error_code != FT_ERR_SUCCESS)
	{
		if (engine_snapshot_captured && engine_mutated)
		{
			const int32_t restore_error =
				this->engine_.apply_snapshot(engine_snapshot);
			if (restore_error != FT_ERR_SUCCESS)
			{
				card_game_deck_card removed_card = {};
				engine_rollback_succeeded =
					this->engine_.deck_draw_instance(
						RegenerationCardSession::PLAYER_ID, instance_id,
						&removed_card) == FT_ERR_SUCCESS;
				if (!engine_rollback_succeeded)
					error_code = FT_ERR_INVALID_STATE;
			}
		}
		if (physical_mapping_inserted && engine_rollback_succeeded)
			this->physical_card_instances_.erase(item.instance_id);
		if (engine_rollback_succeeded)
			instance_id = 0U;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		cached->second.physical_item_instance_id = item.instance_id;
		cached->second.item_definition_id = item.item_id;
		cached->second.result_card.instance_id = instance_id;
		cached->second.result_card.card_id = card_definition_id;
	}
	cached->second.result_code = error_code;
	*card_out = cached->second.result_card;
	return (error_code);
}

int32_t RegenerationCardSession::resolve_physical_card_instance(
	uint64_t physical_item_instance_id,
	uint32_t *card_instance_id_out) const noexcept
{
	if (!this->initialized_ || physical_item_instance_id == 0U
		|| card_instance_id_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*card_instance_id_out = 0U;
	std::map<uint64_t, PhysicalCardLink>::const_iterator entry =
		this->physical_card_instances_.find(physical_item_instance_id);
	if (entry == this->physical_card_instances_.end())
		return (FT_ERR_NOT_FOUND);
	*card_instance_id_out = entry->second.card_instance_id;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::get_max_physical_item_instance_id(
	uint64_t *instance_id_out) const noexcept
{
	if (!this->initialized_ || instance_id_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*instance_id_out = this->physical_card_instances_.empty() ? 0U
		: this->physical_card_instances_.rbegin()->first;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::copy_physical_item_instance_ids(
	std::vector<uint64_t> &instance_ids_out) const noexcept
{
	std::vector<uint64_t> staged;
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	try
	{
		staged.reserve(this->physical_card_instances_.size());
		for (const auto &entry : this->physical_card_instances_)
			staged.push_back(entry.first);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	instance_ids_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::export_card_to_inventory(
	game_inventory &inventory, uint32_t card_instance_id,
	uint64_t expected_inventory_revision, uint64_t request_id,
	RegenerationCardTransferResult *result_out) noexcept
{
	std::map<uint64_t, CachedCardExport>::iterator cached;
	std::map<uint64_t, PhysicalCardLink>::iterator physical_link;
	ft_sharedptr<game_item> item;
	card_game_deck_card card = {};
	RegenerationCardTransferResult result = {};
	uint64_t physical_item_id = 0U;
	uint64_t inventory_revision_after_add = 0U;
	uint32_t owned_count = 0U;
	uint32_t hand_count = 0U;
	uint32_t index = 0U;
	uint32_t source_zone = 0U;
	uint32_t expected_card_definition = 0U;
	int32_t inventory_slot = -1;
	int32_t error_code;

	if (!this->initialized_ || result_out == nullptr || request_id == 0U
		|| card_instance_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	*result_out = {};
	cached = this->card_export_cache_.find(request_id);
	if (cached != this->card_export_cache_.end())
	{
		if (cached->second.card_instance_id != card_instance_id
			|| cached->second.expected_inventory_revision
				!= expected_inventory_revision)
			return (FT_ERR_INVALID_ARGUMENT);
		*result_out = cached->second.result;
		result_out->replayed = true;
		return (result_out->status);
	}
	if (this->card_import_cache_.find(request_id)
		!= this->card_import_cache_.end()
		|| request_id <= this->highest_card_transfer_request_id_)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->card_import_cache_.size() + this->card_export_cache_.size()
		>= RegenerationCardSession::MAXIMUM_IMPORTED_CARD_REQUESTS)
		return (FT_ERR_FULL);
	try
	{
		CachedCardExport pending = {};
		pending.card_instance_id = card_instance_id;
		pending.expected_inventory_revision = expected_inventory_revision;
		cached = this->card_export_cache_.emplace(request_id, pending).first;
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	this->highest_card_transfer_request_id_ = request_id;
	physical_link = this->physical_card_instances_.begin();
	while (physical_link != this->physical_card_instances_.end()
		&& physical_link->second.card_instance_id != card_instance_id)
		++physical_link;
	if (physical_link == this->physical_card_instances_.end())
		error_code = FT_ERR_NOT_FOUND;
	else
	{
		physical_item_id = physical_link->first;
		expected_card_definition =
			RegenerationCardSession::card_definition_for_item(
				physical_link->second.item.item_id);
		if (expected_card_definition == 0U)
			error_code = FT_ERR_INVALID_STATE;
		else
			error_code = FT_ERR_NOT_FOUND;
	}
	if (error_code == FT_ERR_NOT_FOUND)
	{
		card_game_deck_card candidate = {};
		if (this->engine_.deck_get_instance(
				RegenerationCardSession::PLAYER_ID, card_instance_id,
				&candidate) == FT_ERR_SUCCESS)
		{
			card = candidate;
			source_zone = 0U;
			error_code = FT_ERR_SUCCESS;
		}
	}
	if (error_code == FT_ERR_NOT_FOUND)
	{
		if (this->engine_.get_hand_count(RegenerationCardSession::PLAYER_ID,
				&hand_count) != FT_ERR_SUCCESS)
			error_code = FT_ERR_INVALID_STATE;
		index = 0U;
		while (error_code == FT_ERR_NOT_FOUND && index < hand_count)
		{
			card_game_deck_card candidate = {};
			if (this->engine_.hand_inspect(
					RegenerationCardSession::PLAYER_ID, index,
					&candidate) != FT_ERR_SUCCESS)
				error_code = FT_ERR_INVALID_STATE;
			else if (candidate.instance_id == card_instance_id)
			{
				card = candidate;
				source_zone = RegenerationCardSession::HAND_ZONE_ID;
				error_code = FT_ERR_SUCCESS;
			}
			++index;
		}
	}
	for (uint32_t zone_id = RegenerationCardSession::RESOLUTION_ZONE_ID;
		zone_id <= RegenerationCardSession::BURNED_ZONE_ID
			&& error_code == FT_ERR_NOT_FOUND; ++zone_id)
	{
		uint32_t zone_count = 0U;
		if (this->engine_.get_zone_count(RegenerationCardSession::PLAYER_ID,
				zone_id, &zone_count) != FT_ERR_SUCCESS)
		{
			error_code = FT_ERR_INVALID_STATE;
			break ;
		}
		for (index = 0U; index < zone_count; ++index)
		{
			card_game_deck_card candidate = {};
			if (this->engine_.zone_inspect(RegenerationCardSession::PLAYER_ID,
					zone_id, index, &candidate) != FT_ERR_SUCCESS)
			{
				error_code = FT_ERR_INVALID_STATE;
				break ;
			}
			if (candidate.instance_id == card_instance_id)
			{
				card = candidate;
				source_zone = zone_id;
				error_code = FT_ERR_SUCCESS;
				break ;
			}
		}
	}
	if (error_code == FT_ERR_SUCCESS
		&& card.card_id != expected_card_definition)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->count_owned_session_cards(&owned_count);
	if (error_code == FT_ERR_SUCCESS && owned_count <= 20U)
		error_code = FT_ERR_FULL;
	if (error_code == FT_ERR_SUCCESS)
	{
		const game_inventory_item_snapshot &source_item =
			physical_link->second.item;
		error_code = item.initialize(new (std::nothrow) game_item());
		if (error_code == FT_ERR_SUCCESS)
			error_code = item->initialize();
		if (error_code == FT_ERR_SUCCESS)
		{
			item->set_item_id(source_item.item_id);
			item->set_stack_size(source_item.stack_size);
			item->set_max_stack(source_item.max_stack);
			item->set_rarity(source_item.rarity);
			item->set_width(source_item.width);
			item->set_height(source_item.height);
			item->set_instance_id(source_item.instance_id);
			item->set_modifier1_id(source_item.modifier1_id);
			item->set_modifier1_value(source_item.modifier1_value);
			item->set_modifier2_id(source_item.modifier2_id);
			item->set_modifier2_value(source_item.modifier2_value);
			item->set_modifier3_id(source_item.modifier3_id);
			item->set_modifier3_value(source_item.modifier3_value);
			item->set_modifier4_id(source_item.modifier4_id);
			item->set_modifier4_value(source_item.modifier4_value);
			error_code = inventory.add_item_revisioned(item,
				expected_inventory_revision, &inventory_slot);
		}
	}
	if (error_code == FT_ERR_SUCCESS)
		inventory_revision_after_add = expected_inventory_revision + 1U;
	if (error_code == FT_ERR_SUCCESS)
	{
		card_game_deck_card removed = {};
		if (source_zone == 0U)
			error_code = this->engine_.deck_draw_instance(
				RegenerationCardSession::PLAYER_ID, card_instance_id, &removed);
		else if (source_zone == RegenerationCardSession::HAND_ZONE_ID)
			error_code = this->engine_.hand_remove_instance(
				RegenerationCardSession::PLAYER_ID, card_instance_id, &removed);
		else
			error_code = this->engine_.zone_remove_instance(
				RegenerationCardSession::PLAYER_ID, source_zone,
				card_instance_id, &removed);
		if (error_code == FT_ERR_SUCCESS
			&& (removed.instance_id != card_instance_id
				|| removed.card_id != card.card_id))
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code != FT_ERR_SUCCESS && inventory_slot >= 0)
	{
		if (inventory_revision_after_add == 0U)
			error_code = FT_ERR_INVALID_STATE;
		else if (inventory.remove_item_instance(inventory_slot,
				inventory_revision_after_add, physical_item_id)
			!= FT_ERR_SUCCESS)
			error_code = FT_ERR_INVALID_STATE;
	}
	if (error_code == FT_ERR_SUCCESS)
	{
		this->physical_card_instances_.erase(physical_item_id);
		result.request_id = request_id;
		result.physical_item_instance_id = physical_item_id;
		result.card_instance_id = card_instance_id;
		result.inventory_slot = inventory_slot;
		result.status = FT_ERR_SUCCESS;
		result.replayed = false;
		cached->second.result = result;
	}
	else
	{
		result.request_id = request_id;
		result.physical_item_instance_id = physical_item_id;
		result.card_instance_id = card_instance_id;
		result.inventory_slot = inventory_slot;
		result.status = error_code;
		result.replayed = false;
		cached->second.result = result;
	}
	*result_out = cached->second.result;
	return (error_code);
}

bool RegenerationCardSession::is_initialized() const noexcept
{
	return (this->initialized_);
}

int32_t RegenerationCardSession::register_script_context_key(
	uint32_t key_id) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->script_bridge_.register_context_key(key_id));
}

int32_t RegenerationCardSession::get_script_context_keys(
	std::vector<uint32_t> &keys_out) const noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->script_bridge_.get_registered_context_keys(keys_out));
}

int32_t RegenerationCardSession::load_script_catalog(const uint8_t *bytes,
	uint32_t byte_count, uint64_t trusted_manifest_hash,
	uint64_t policy_manifest_hash) noexcept
{
	uint32_t pending_intent_count = 0U;
	int32_t error_code;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (bytes == nullptr || byte_count == 0U || trusted_manifest_hash == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->play_cache_.empty() || !this->draw_cache_.empty()
		|| !this->discard_cache_.empty() || !this->card_import_cache_.empty()
		|| !this->card_export_cache_.empty()
		|| this->highest_card_transfer_request_id_ != 0U
		|| this->next_command_sequence_ != 1U
		|| this->pending_required_discard_count_ != 0U
		|| this->active_request_id_ != 0U)
		return (FT_ERR_INVALID_OPERATION);
	error_code = this->engine_.get_pending_host_intent_count(
		&pending_intent_count);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (pending_intent_count != 0U)
		return (FT_ERR_INVALID_OPERATION);
	error_code = this->script_bridge_.set_policy_manifest_hash(
		policy_manifest_hash);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->script_bridge_.load_script_catalog(bytes, byte_count,
			trusted_manifest_hash);
	return (error_code);
}

int32_t RegenerationCardSession::get_script_manifest_hash(
	uint64_t *hash_out) const noexcept
{
	if (!this->initialized_ || hash_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (this->script_bridge_.get_manifest_hash(hash_out));
}

bool RegenerationCardSession::has_cached_play_request(
	uint64_t request_id) const noexcept
{
	return (this->initialized_ && request_id != 0U
		&& this->play_cache_.find(request_id) != this->play_cache_.end());
}

int32_t RegenerationCardSession::get_cached_play_parameters(
	uint64_t request_id, uint32_t *risk_basis_points_out,
	uint32_t *ordinal_out, uint32_t catalyst_options_out[7],
	uint32_t *catalyst_option_count_out) const noexcept
{
	const auto cached = this->play_cache_.find(request_id);
	if (!this->initialized_ || request_id == 0U
		|| risk_basis_points_out == nullptr || ordinal_out == nullptr
		|| catalyst_options_out == nullptr
		|| catalyst_option_count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (cached == this->play_cache_.end())
		return (FT_ERR_NOT_FOUND);
	*risk_basis_points_out = cached->second.has_retry_payload
		? cached->second.stabilizer_risk_basis_points : 0U;
	*ordinal_out = cached->second.has_retry_payload
		? cached->second.stabilizer_ordinal : 0U;
	*catalyst_option_count_out = cached->second.has_retry_payload
		? cached->second.catalyst_option_count : 0U;
	std::copy(cached->second.catalyst_options,
		cached->second.catalyst_options + 7U, catalyst_options_out);
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::set_script_context_value(uint32_t key_id,
	int64_t value) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->script_bridge_.set_context_value(key_id, value));
}

int32_t RegenerationCardSession::clear_script_context_value(
	uint32_t key_id) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->script_bridge_.clear_context_value(key_id));
}

int32_t RegenerationCardSession::get_draw_count(uint32_t *count_out) const noexcept
{
	if (!this->initialized_ || count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (this->engine_.get_deck_count(RegenerationCardSession::PLAYER_ID,
		count_out));
}

int32_t RegenerationCardSession::draw_inspect(uint32_t index,
	card_game_deck_card *card_out) const noexcept
{
	if (!this->initialized_ || card_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (this->engine_.deck_inspect(RegenerationCardSession::PLAYER_ID,
		index, card_out));
}

int32_t RegenerationCardSession::get_hand_count(uint32_t *count_out) const noexcept
{
	if (!this->initialized_ || count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (this->engine_.get_hand_count(RegenerationCardSession::PLAYER_ID,
		count_out));
}

int32_t RegenerationCardSession::hand_inspect(uint32_t index,
	card_game_deck_card *card_out) const noexcept
{
	if (!this->initialized_ || card_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (this->engine_.hand_inspect(RegenerationCardSession::PLAYER_ID,
		index, card_out));
}

int32_t RegenerationCardSession::get_discard_count(
	uint32_t *count_out) const noexcept
{
	if (!this->initialized_ || count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (this->engine_.get_zone_count(RegenerationCardSession::PLAYER_ID,
		RegenerationCardSession::DISCARD_ZONE_ID, count_out));
}

int32_t RegenerationCardSession::discard_inspect(uint32_t index,
	card_game_deck_card *card_out) const noexcept
{
	if (!this->initialized_ || card_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (this->engine_.zone_inspect(RegenerationCardSession::PLAYER_ID,
		RegenerationCardSession::DISCARD_ZONE_ID, index, card_out));
}

int32_t RegenerationCardSession::get_pending_intent_count(
	uint32_t *count_out) const noexcept
{
	if (!this->initialized_ || count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (this->engine_.get_pending_host_intent_count(count_out));
}

int32_t RegenerationCardSession::is_host_intent_pending(uint64_t sequence,
	ft_bool *pending_out) const noexcept
{
	uint32_t pending_count = 0U;
	uint32_t index = 0U;
	uint64_t next_sequence = 0U;
	int32_t result;

	if (!this->initialized_ || sequence == 0U || pending_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*pending_out = FT_FALSE;
	result = this->engine_.get_next_host_intent_sequence(&next_sequence);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (sequence >= next_sequence)
		return (FT_ERR_NOT_FOUND);
	result = this->engine_.get_pending_host_intent_count(&pending_count);
	if (result != FT_ERR_SUCCESS)
		return (result);
	while (index < pending_count)
	{
		card_game_host_intent_record record = {};
		result = this->engine_.inspect_pending_host_intent(index, &record);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (record.sequence == sequence)
		{
			*pending_out = FT_TRUE;
			return (FT_ERR_SUCCESS);
		}
		++index;
	}
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::copy_pending_intents(
	std::vector<card_game_host_intent_record> &records_out) const noexcept
{
	std::vector<card_game_host_intent_record> staged;
	uint32_t count = 0U;
	uint32_t index = 0U;
	int32_t result;

	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	result = this->engine_.get_pending_host_intent_count(&count);
	if (result != FT_ERR_SUCCESS)
		return (result);
	try
	{
		staged.reserve(count);
		while (index < count)
		{
			card_game_host_intent_record record = {};
			result = this->engine_.inspect_pending_host_intent(index, &record);
			if (result != FT_ERR_SUCCESS)
				return (result);
			staged.push_back(record);
			++index;
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	records_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::get_pending_required_discard_count(
	uint32_t *count_out) const noexcept
{
	if (!this->initialized_ || count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*count_out = this->pending_required_discard_count_;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::resolve_pending_required_discards_to_default(
	uint64_t request_namespace, uint32_t *resolved_count_out) noexcept
{
	uint32_t pending_count = 0U;
	uint32_t resolved_count = 0U;
	uint64_t source_draw_request_id = 0U;
	uint64_t latest_draw_sequence = 0U;
	int32_t error_code;

	if (!this->initialized_ || request_namespace == 0U
		|| resolved_count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*resolved_count_out = 0U;
	error_code = this->get_pending_required_discard_count(&pending_count);
	if (error_code != FT_ERR_SUCCESS || pending_count == 0U)
		return (error_code);
	for (const std::pair<const uint64_t, CachedDraw> &entry : this->draw_cache_)
	{
		if (entry.second.required_discard_count != 0U
			&& entry.second.result.command_sequence > latest_draw_sequence)
		{
			latest_draw_sequence = entry.second.result.command_sequence;
			source_draw_request_id = entry.first;
		}
	}
	if (source_draw_request_id == 0U)
		return (FT_ERR_INVALID_STATE);
	while (resolved_count < pending_count)
	{
		uint32_t hand_count = 0U;
		uint32_t card_instance_id = UINT32_MAX;
		uint32_t hand_index = 0U;
		uint64_t request_id = request_namespace
			^ 0xD15CA4D1A11D0001ULL
			^ (source_draw_request_id * 0x9E3779B97F4A7C15ULL)
			^ (static_cast<uint64_t>(resolved_count) + 1U);
		RegenerationCardActionResult discard_result = {};
		bool request_id_available = false;
		error_code = this->engine_.get_hand_count(
			RegenerationCardSession::PLAYER_ID, &hand_count);
		if (error_code != FT_ERR_SUCCESS || hand_count == 0U)
			return (error_code == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE
				: error_code);
		while (hand_index < hand_count)
		{
			card_game_deck_card card = {};
			error_code = this->engine_.hand_inspect(
				RegenerationCardSession::PLAYER_ID, hand_index, &card);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			if (card.instance_id < card_instance_id)
				card_instance_id = card.instance_id;
			++hand_index;
		}
		for (uint32_t attempt = 0U; attempt <= this->play_cache_.size()
			+ this->draw_cache_.size() + this->discard_cache_.size(); ++attempt)
		{
			if (request_id == 0U)
				request_id = 1U;
			if (this->play_cache_.find(request_id) == this->play_cache_.end()
				&& this->draw_cache_.find(request_id) == this->draw_cache_.end()
				&& this->discard_cache_.find(request_id)
					== this->discard_cache_.end()
				&& this->card_import_cache_.find(request_id)
					== this->card_import_cache_.end()
				&& this->card_export_cache_.find(request_id)
					== this->card_export_cache_.end())
			{
				request_id_available = true;
				break ;
			}
			++request_id;
		}
		if (!request_id_available)
			return (FT_ERR_FULL);
		error_code = this->discard_hand_card(request_id,
			source_draw_request_id, card_instance_id, &discard_result);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		++resolved_count;
	}
	*resolved_count_out = resolved_count;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::get_pending_choice_count(
	uint32_t *count_out) const noexcept
{
	card_game_snapshot snapshot = {};
	uint32_t pending_count = 0U;
	uint32_t index;
	int32_t result;

	if (!this->initialized_ || count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	result = this->engine_.get_snapshot(&snapshot);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (snapshot.choices.count > FT_CARD_GAME_MAX_CHOICES
		|| (snapshot.choices.count != 0U
			&& snapshot.choices.choices == nullptr))
		return (FT_ERR_INVALID_STATE);
	index = 0U;
	while (index < snapshot.choices.count)
	{
		if (snapshot.choices.choices[index].resolved == FT_FALSE)
			++pending_count;
		++index;
	}
	*count_out = pending_count;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::count_pending_card_definition(
	uint32_t card_definition_id, uint32_t *count_out) const noexcept
{
	uint32_t pending_count;
	uint32_t index;
	uint32_t matched_count = 0U;
	int32_t error_code;
	if (!this->initialized_ || count_out == nullptr
		|| RegenerationCardSession::is_supported_card_definition(
			card_definition_id) == false)
		return (FT_ERR_INVALID_ARGUMENT);
	*count_out = 0U;
	error_code = this->engine_.get_pending_host_intent_count(&pending_count);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	for (index = 0U; index < pending_count; ++index)
	{
		card_game_host_intent_record record = {};
		RegenerationPolicyIntent intent = {};
		error_code = this->engine_.inspect_pending_host_intent(index, &record);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (record.intent.type_id
			!= RegenerationCardSession::POLICY_INTENT_TYPE_ID)
			continue ;
		error_code = RegenerationCardSession::decode_policy_intent(record,
			&intent);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (intent.card_definition_id == card_definition_id)
			++matched_count;
	}
	*count_out = matched_count;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::peek_pending_intent(
	card_game_host_intent_record *record_out) const noexcept
{
	if (!this->initialized_ || record_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (this->engine_.peek_pending_host_intent(record_out));
}

int32_t RegenerationCardSession::peek_pending_card_intent(
	RegenerationPolicyIntent *intent_out) const noexcept
{
	card_game_host_intent_record record = {};
	int32_t error_code;

	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (intent_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = this->engine_.peek_pending_host_intent(&record);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (RegenerationCardSession::decode_policy_intent(record, intent_out));
}

int32_t RegenerationCardSession::decode_policy_intent(
	const card_game_host_intent_record &record,
	RegenerationPolicyIntent *intent_out) noexcept
{
	uint32_t card_id;
	uint32_t effect_kind;
	uint32_t choice_kind;
	uint32_t content_version;
	uint32_t index;
	if (intent_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (record.intent.type_id != RegenerationCardSession::POLICY_INTENT_TYPE_ID
		|| record.intent.version != RegenerationCardSession::CONTENT_VERSION
		|| record.intent.field_count != 8U)
		return (FT_ERR_INVALID_ARGUMENT);
	index = 0U;
	while (index < 7U)
	{
		if (record.intent.fields[index].type != CARD_GAME_HOST_INTENT_U32
			|| record.intent.fields[index].value > UINT32_MAX)
			return (FT_ERR_INVALID_ARGUMENT);
		++index;
	}
	if (record.intent.fields[7].type != CARD_GAME_HOST_INTENT_U64)
		return (FT_ERR_INVALID_ARGUMENT);
	card_id = static_cast<uint32_t>(record.intent.fields[0].value);
	effect_kind = static_cast<uint32_t>(record.intent.fields[2].value);
	choice_kind = static_cast<uint32_t>(record.intent.fields[3].value);
	content_version = static_cast<uint32_t>(record.intent.fields[6].value);
	if (!RegenerationCardSession::is_supported_card_definition(card_id)
		|| effect_kind != static_cast<uint32_t>(
			RegenerationCardSession::effect_kind_for_card(card_id))
		|| content_version != RegenerationCardSession::CONTENT_VERSION)
		return (FT_ERR_INVALID_ARGUMENT);
	RegenerationPolicyIntent intent = {};
	intent.request_id = record.intent.fields[7].value;
	intent.host_intent_sequence = record.sequence;
	intent.content_version = content_version;
	intent.card_definition_id = card_id;
	intent.card_instance_id = static_cast<uint32_t>(
		record.intent.fields[1].value);
	intent.effect_kind = static_cast<RegenerationCardEffectKind>(effect_kind);
	intent.choice.kind = static_cast<RegenerationCardChoiceKind>(choice_kind);
	if (card_id
		== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
	{
		intent.private_result = static_cast<uint32_t>(
			record.intent.fields[4].value);
		intent.private_ordinal = static_cast<uint32_t>(
			record.intent.fields[5].value);
		if (intent.private_result > 1U || intent.private_ordinal == 0U)
			return (FT_ERR_INVALID_ARGUMENT);
	}
	else if (card_id
		== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
	{
		intent.private_result = static_cast<uint32_t>(
			record.intent.fields[4].value);
		intent.private_ordinal = static_cast<uint32_t>(
			record.intent.fields[5].value);
		if (intent.private_result == 0U || intent.private_result > 7U
			|| intent.private_ordinal == 0U)
			return (FT_ERR_INVALID_ARGUMENT);
	}
	else
	{
		intent.choice.value_primary = static_cast<uint32_t>(
			record.intent.fields[4].value);
		intent.choice.value_secondary = static_cast<uint32_t>(
			record.intent.fields[5].value);
	}
	intent.replayed = false;
	if (intent.request_id == 0U || intent.card_instance_id == 0U
		|| RegenerationCardSession::choice_is_valid(card_id, intent.choice)
		!= FT_TRUE)
		return (FT_ERR_INVALID_ARGUMENT);
	*intent_out = intent;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::is_draw_notice(
	const card_game_host_intent_record &record,
	bool *is_notice_out) const noexcept
{
	if (!this->initialized_ || is_notice_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*is_notice_out = false;
	if (record.intent.type_id != RegenerationCardSession::EMPTY_DRAW_INTENT_TYPE_ID
		&& record.intent.type_id
			!= RegenerationCardSession::HAND_OVERFLOW_INTENT_TYPE_ID)
		return (FT_ERR_SUCCESS);
	if (record.intent.version != RegenerationCardSession::CONTENT_VERSION
		|| record.intent.field_count != 2U
		|| record.intent.fields[0].type != CARD_GAME_HOST_INTENT_U32
		|| record.intent.fields[0].value
			!= RegenerationCardSession::PLAYER_ID
		|| record.intent.fields[1].type != CARD_GAME_HOST_INTENT_U32
		|| (record.intent.type_id
			== RegenerationCardSession::EMPTY_DRAW_INTENT_TYPE_ID
			&& record.intent.fields[1].value != 0U)
		|| (record.intent.type_id
			== RegenerationCardSession::HAND_OVERFLOW_INTENT_TYPE_ID
			&& (record.intent.fields[1].value == 0U
				|| record.intent.fields[1].value > UINT32_MAX)))
		return (FT_ERR_INVALID_ARGUMENT);
	*is_notice_out = true;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::acknowledge_draw_notice(
	uint64_t host_intent_sequence) noexcept
{
	card_game_host_intent_record record = {};
	bool is_notice = false;
	int32_t result;

	if (!this->initialized_ || host_intent_sequence == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	result = this->engine_.peek_pending_host_intent(&record);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (record.sequence != host_intent_sequence)
		return (FT_ERR_INVALID_STATE);
	result = this->is_draw_notice(record, &is_notice);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (!is_notice)
		return (FT_ERR_INVALID_ARGUMENT);
	return (this->engine_.acknowledge_host_intent(host_intent_sequence));
}

int32_t RegenerationCardSession::acknowledge_draw_notice_if_pending(
	uint64_t host_intent_sequence, bool *was_pending_out) noexcept
{
	uint32_t count = 0U;
	uint32_t index = 0U;
	int32_t result;

	if (!this->initialized_ || host_intent_sequence == 0U
		|| was_pending_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*was_pending_out = false;
	result = this->engine_.get_pending_host_intent_count(&count);
	if (result != FT_ERR_SUCCESS)
		return (result);
	while (index < count)
	{
		card_game_host_intent_record record = {};
		bool is_notice = false;

		result = this->engine_.inspect_pending_host_intent(index, &record);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (record.sequence == host_intent_sequence)
		{
			if (index != 0U)
				return (FT_ERR_INVALID_STATE);
			result = this->is_draw_notice(record, &is_notice);
			if (result != FT_ERR_SUCCESS)
				return (result);
			if (!is_notice)
				return (FT_ERR_INVALID_ARGUMENT);
			result = this->engine_.acknowledge_host_intent(
				host_intent_sequence);
			if (result == FT_ERR_SUCCESS)
				*was_pending_out = true;
			return (result);
		}
		++index;
	}
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::locate_hand_card(uint32_t card_instance_id,
	card_game_deck_card *card_out) const noexcept
{
	uint32_t hand_count;
	uint32_t index = 0U;
	int32_t error_code;

	if (card_instance_id == 0U || card_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = this->get_hand_count(&hand_count);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	while (index < hand_count)
	{
		card_game_deck_card card = {};

		error_code = this->engine_.hand_inspect(
			RegenerationCardSession::PLAYER_ID, index, &card);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (card.instance_id == card_instance_id)
		{
			*card_out = card;
			return (FT_ERR_SUCCESS);
		}
		++index;
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t RegenerationCardSession::play_card(uint64_t request_id,
	uint32_t card_instance_id, const RegenerationCardChoice &choice,
	const voxel_generation_config &world_config,
	RegenerationPolicyIntent *intent_out,
	uint32_t stabilizer_risk_basis_points,
	uint32_t stabilizer_ordinal, const uint32_t *catalyst_options,
	uint32_t catalyst_option_count,
	uint64_t authenticated_actor_id) noexcept
{
	std::map<uint64_t, CachedPlay>::iterator cached;
	card_game_snapshot before_state = {};
	card_game_action action = {};
	card_game_action_result action_result = {};
	card_game_deck_card hand_card = {};
	card_game_host_intent_record host_intent_record = {};
	uint32_t pending_intent_count;
	uint32_t card_definition_id;
	uint64_t original_command_sequence;
	int32_t error_code;
	bool cache_inserted = false;

	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (intent_out == nullptr || request_id == 0U || card_instance_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	cached = this->play_cache_.find(request_id);
	if (cached != this->play_cache_.end())
	{
		if (cached->second.authenticated_actor_id != authenticated_actor_id
			|| cached->second.card_instance_id != card_instance_id
			|| cached->second.choice.kind != choice.kind
			|| cached->second.choice.value_primary != choice.value_primary
			|| cached->second.choice.value_secondary != choice.value_secondary
			|| (cached->second.has_retry_payload
				&& cached->second.stabilizer_risk_basis_points
				!= stabilizer_risk_basis_points
				)
			|| (cached->second.has_retry_payload
				&& cached->second.stabilizer_ordinal != stabilizer_ordinal)
			|| catalyst_option_count > 7U
			|| (catalyst_option_count != 0U && catalyst_options == nullptr))
			return (FT_ERR_INVALID_ARGUMENT);
		if (cached->second.result.card_definition_id
			!= RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID
			&& (catalyst_option_count != 0U || catalyst_options != nullptr))
			return (FT_ERR_INVALID_ARGUMENT);
		uint32_t retry_option_count = catalyst_option_count;
		uint32_t retry_options[7] = {};
		if (cached->second.result.card_definition_id
			== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID
			&& retry_option_count == 0U)
		{
			static const uint32_t default_options[] = {1U, 2U, 3U, 4U};
			retry_option_count = 4U;
			std::copy(default_options, default_options + 4U, retry_options);
		}
		else if (retry_option_count != 0U)
			std::copy(catalyst_options, catalyst_options + retry_option_count,
				retry_options);
		if (cached->second.has_retry_payload
			&& (retry_option_count != cached->second.catalyst_option_count
			|| !std::equal(retry_options, retry_options + 7U,
				cached->second.catalyst_options)))
			return (FT_ERR_INVALID_ARGUMENT);
		*intent_out = cached->second.result;
		intent_out->replayed = true;
		return (FT_ERR_SUCCESS);
	}
	if (this->pending_double_draft_intent_sequence_ != 0U
		|| this->pending_required_discard_count_ != 0U)
		return (FT_ERR_PERMISSION_DENIED);
	error_code = this->locate_hand_card(card_instance_id, &hand_card);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	card_definition_id = hand_card.card_id;
	if (card_definition_id
		== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
	{
		if (catalyst_option_count > 7U
			|| (catalyst_option_count != 0U && catalyst_options == nullptr))
			return (FT_ERR_INVALID_ARGUMENT);
		for (uint32_t index = 0U; index < catalyst_option_count; ++index)
		{
			if (catalyst_options[index] == 0U
				|| catalyst_options[index] > 7U)
				return (FT_ERR_INVALID_ARGUMENT);
			for (uint32_t previous = 0U; previous < index; ++previous)
				if (catalyst_options[index] == catalyst_options[previous])
					return (FT_ERR_ALREADY_EXISTS);
		}
	}
	else if (catalyst_option_count != 0U || catalyst_options != nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (card_definition_id
		== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID
		&& (stabilizer_risk_basis_points > 10000U
			|| stabilizer_ordinal == 0U))
		return (FT_ERR_CONFIGURATION);
	if (card_definition_id
		== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID
		&& (stabilizer_risk_basis_points != 0U || stabilizer_ordinal == 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	if (card_definition_id
		!= RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID
		&& card_definition_id
		!= RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID
		&& (stabilizer_risk_basis_points != 0U || stabilizer_ordinal != 0U))
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = RegenerationCardSession::validate_choice_in_world(
		card_definition_id, choice, world_config);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = this->engine_.get_pending_host_intent_count(
		&pending_intent_count);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (pending_intent_count != 0U)
		return (FT_ERR_INVALID_OPERATION);
	if (this->draw_cache_.find(request_id) != this->draw_cache_.end()
		|| this->discard_cache_.find(request_id)
			!= this->discard_cache_.end()
		|| this->expiry_cache_.find(request_id)
			!= this->expiry_cache_.end()
		|| this->choice_cache_.find(request_id)
			!= this->choice_cache_.end())
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->play_cache_.size() + this->draw_cache_.size()
		+ this->discard_cache_.size() + this->expiry_cache_.size()
		+ this->choice_cache_.size()
		>= RegenerationCardSession::MAXIMUM_PLAY_REQUESTS)
		return (FT_ERR_FULL);
	if (this->next_command_sequence_ == UINT64_MAX)
		return (FT_ERR_FULL);
	original_command_sequence = this->next_command_sequence_;
	error_code = this->engine_.get_snapshot(&before_state);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	try
	{
		CachedPlay pending = {};

		pending.authenticated_actor_id = authenticated_actor_id;
		pending.card_instance_id = card_instance_id;
		pending.choice = choice;
		if (!this->play_cache_.emplace(request_id, pending).second)
			return (FT_ERR_ALREADY_EXISTS);
		cache_inserted = true;
	}
	catch (const std::bad_alloc &)
	{
		this->play_cache_.erase(request_id);
		return (FT_ERR_NO_MEMORY);
	}
	this->active_request_id_ = request_id;
	this->active_choice_ = choice;
	this->active_private_result_ = stabilizer_risk_basis_points;
	this->active_private_ordinal_ = stabilizer_ordinal;
	this->active_catalyst_option_count_ = 0U;
	std::fill(this->active_catalyst_options_,
		this->active_catalyst_options_ + 7U, 0U);
	if (card_definition_id
		== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
	{
		if (catalyst_option_count == 0U)
		{
			static const uint32_t default_options[] = {1U, 2U, 3U, 4U};
			catalyst_options = default_options;
			catalyst_option_count = 4U;
		}
		for (uint32_t index = 0U; index < catalyst_option_count; ++index)
			this->active_catalyst_options_[index] = catalyst_options[index];
		this->active_catalyst_option_count_ = catalyst_option_count;
	}
	cached = this->play_cache_.find(request_id);
	if (cached == this->play_cache_.end())
		return (FT_ERR_INVALID_STATE);
	cached->second.stabilizer_risk_basis_points = stabilizer_risk_basis_points;
	cached->second.stabilizer_ordinal = stabilizer_ordinal;
	cached->second.catalyst_option_count =
		this->active_catalyst_option_count_;
	cached->second.has_retry_payload = true;
	std::copy(this->active_catalyst_options_,
		this->active_catalyst_options_ + 7U,
		cached->second.catalyst_options);
	action.schema_version = this->action_session_id_ != 0U ? 3U : 2U;
	action.session_id = this->action_session_id_;
	action.request_id = request_id;
	action.command.command_sequence = this->next_command_sequence_;
	action.command.player_id = RegenerationCardSession::PLAYER_ID;
	action.command.type = CARD_GAME_INTENT_PLAY_EFFECT;
	action.command.card_id = card_definition_id;
	action.command.source_instance_id = card_instance_id;
	action.command.target_instance =
		RegenerationCardSession::CARD_EFFECT_EVENT_BASE + card_definition_id;
	action.command.destination_zone_id =
		RegenerationCardSession::DISCARD_ZONE_ID;
	regeneration_payload_write_u64(action.command.play_payload, 0U,
		request_id);
	regeneration_payload_write_u32(action.command.play_payload, 8U,
		static_cast<uint32_t>(choice.kind));
	regeneration_payload_write_u32(action.command.play_payload, 12U,
		choice.value_primary);
	regeneration_payload_write_u32(action.command.play_payload, 16U,
		choice.value_secondary);
	regeneration_payload_write_u32(action.command.play_payload, 20U,
		stabilizer_risk_basis_points);
	regeneration_payload_write_u32(action.command.play_payload, 24U,
		stabilizer_ordinal);
	regeneration_payload_write_u32(action.command.play_payload, 28U,
		this->active_catalyst_option_count_);
	for (uint32_t option_index = 0U; option_index < 7U; ++option_index)
		regeneration_payload_write_u32(action.command.play_payload,
			32U + option_index * 4U,
			this->active_catalyst_options_[option_index]);
	regeneration_payload_write_u32(action.command.play_payload, 60U,
		0x52435031U);
	if (this->action_session_id_ != 0U)
		error_code = this->engine_.submit_action(action, &action_result,
			nullptr);
	else
		error_code = this->engine_.submit_command(action.command, nullptr);
	if (error_code == FT_ERR_SUCCESS)
		++this->next_command_sequence_;
	this->active_request_id_ = 0U;
	this->active_choice_ = {};
	this->active_private_result_ = 0U;
	this->active_private_ordinal_ = 0U;
	this->active_catalyst_option_count_ = 0U;
	std::fill(this->active_catalyst_options_,
		this->active_catalyst_options_ + 7U, 0U);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->engine_.get_pending_host_intent_count(
			&pending_intent_count);
	if (error_code == FT_ERR_SUCCESS && pending_intent_count != 1U)
		error_code = FT_ERR_INVALID_STATE;
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->engine_.peek_pending_host_intent(
			&host_intent_record);
	if (error_code != FT_ERR_SUCCESS)
	{
		(void)this->engine_.apply_snapshot(before_state);
		this->next_command_sequence_ = original_command_sequence;
		if (cache_inserted)
			this->play_cache_.erase(request_id);
		return (error_code);
	}
	RegenerationPolicyIntent result = {};
	result.request_id = request_id;
	result.host_intent_sequence = host_intent_record.sequence;
	result.content_version = RegenerationCardSession::CONTENT_VERSION;
	result.card_definition_id = card_definition_id;
	result.card_instance_id = card_instance_id;
	result.effect_kind = RegenerationCardSession::effect_kind_for_card(
		card_definition_id);
	result.choice = choice;
	result.replayed = false;
	cached = this->play_cache_.find(request_id);
	if (cached == this->play_cache_.end())
		return (FT_ERR_INVALID_STATE);
	cached->second.result = result;
	if (result.effect_kind == REGENERATION_CARD_EFFECT_DOUBLE_DRAFT)
		this->pending_double_draft_intent_sequence_ =
			result.host_intent_sequence;
	*intent_out = result;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::acknowledge_intent(
	uint64_t host_intent_sequence) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (host_intent_sequence == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	return (this->engine_.acknowledge_host_intent(host_intent_sequence));
}

int32_t RegenerationCardSession::set_cached_play_no_effect_choice_mask(
	uint64_t request_id, uint32_t no_effect_choice_mask) noexcept
{
	std::map<uint64_t, CachedPlay>::iterator cached;

	if (!this->initialized_ || request_id == 0U
		|| (no_effect_choice_mask & ~3U) != 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	cached = this->play_cache_.find(request_id);
	if (cached == this->play_cache_.end())
		return (FT_ERR_NOT_FOUND);
	if (cached->second.result.card_definition_id
		!= RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID
		&& no_effect_choice_mask != 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	cached->second.result.no_effect_choice_mask = no_effect_choice_mask;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::resolve_expired_choices(
	uint64_t request_id, uint64_t authoritative_epoch,
	uint32_t *resolved_count_out) noexcept
{
	std::map<uint64_t, CachedExpiry>::iterator cached;
	card_game_action action = {};
	card_game_action_result action_result = {};
	int32_t result;

	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (request_id == 0U || authoritative_epoch == 0U
		|| resolved_count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*resolved_count_out = 0U;
	cached = this->expiry_cache_.find(request_id);
	if (cached != this->expiry_cache_.end())
	{
		/* The caller cannot supply the epoch: it is read from the host clock.
		 * A later-clock exact retry must replay the first accepted result. */
		if (authoritative_epoch < cached->second.authoritative_epoch)
			return (FT_ERR_INVALID_ARGUMENT);
		*resolved_count_out = cached->second.resolved_count;
		return (FT_ERR_SUCCESS);
	}
	if (this->play_cache_.find(request_id) != this->play_cache_.end()
		|| this->draw_cache_.find(request_id) != this->draw_cache_.end()
		|| this->discard_cache_.find(request_id) != this->discard_cache_.end()
		|| this->choice_cache_.find(request_id) != this->choice_cache_.end())
		return (FT_ERR_ALREADY_EXISTS);
	if (this->play_cache_.size() + this->draw_cache_.size()
		+ this->discard_cache_.size() + this->expiry_cache_.size()
		+ this->choice_cache_.size()
		>= MAXIMUM_PLAY_REQUESTS
		|| this->next_command_sequence_ == UINT64_MAX)
		return (FT_ERR_FULL);
	try
	{
		CachedExpiry pending = {};
		pending.authoritative_epoch = authoritative_epoch;
		pending.command_sequence = this->next_command_sequence_;
		if (!this->expiry_cache_.emplace(request_id, pending).second)
			return (FT_ERR_ALREADY_EXISTS);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	action.command.command_sequence = this->next_command_sequence_;
	action.command.type = CARD_GAME_INTENT_RESOLVE_EXPIRED_CHOICES;
	action.schema_version = this->action_session_id_ != 0U ? 3U : 1U;
	action.session_id = this->action_session_id_;
	action.request_id = request_id;
	result = this->engine_.submit_host_action(action, &action_result, nullptr,
		authoritative_epoch);
	if (result != FT_ERR_SUCCESS)
	{
		this->expiry_cache_.erase(request_id);
		return (result);
	}
	cached = this->expiry_cache_.find(request_id);
	if (cached == this->expiry_cache_.end())
		return (FT_ERR_INVALID_STATE);
	cached->second.resolved_count = action_result.result_value;
	++this->next_command_sequence_;
	*resolved_count_out = action_result.result_value;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::choose_pending_option(
	uint64_t authenticated_actor_id, uint64_t request_id,
	uint32_t choice_id, uint32_t option_id, uint64_t authoritative_epoch,
	RegenerationCardActionResult *result_out) noexcept
{
	auto cached = this->choice_cache_.find(request_id);
	card_game_action action = {};
	card_game_action_result action_result = {};
	CachedChoice pending = {};
	int32_t result;
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (authenticated_actor_id == 0U || request_id == 0U || choice_id == 0U
		|| option_id == 0U || authoritative_epoch == 0U || result_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*result_out = {};
	if (cached != this->choice_cache_.end())
	{
		if (cached->second.authenticated_actor_id != authenticated_actor_id
			|| cached->second.choice_id != choice_id
			|| cached->second.option_id != option_id
			|| authoritative_epoch < cached->second.authoritative_epoch)
			return (FT_ERR_INVALID_ARGUMENT);
		*result_out = cached->second.result;
		result_out->replayed = true;
		return (FT_ERR_SUCCESS);
	}
	if (this->play_cache_.find(request_id) != this->play_cache_.end()
		|| this->draw_cache_.find(request_id) != this->draw_cache_.end()
		|| this->discard_cache_.find(request_id) != this->discard_cache_.end()
		|| this->expiry_cache_.find(request_id) != this->expiry_cache_.end())
		return (FT_ERR_ALREADY_EXISTS);
	if (this->play_cache_.size() + this->draw_cache_.size()
		+ this->discard_cache_.size() + this->expiry_cache_.size()
		+ this->choice_cache_.size() >= MAXIMUM_PLAY_REQUESTS
		|| this->next_command_sequence_ == UINT64_MAX)
		return (FT_ERR_FULL);
	pending.authenticated_actor_id = authenticated_actor_id;
	pending.choice_id = choice_id;
	pending.option_id = option_id;
	pending.authoritative_epoch = authoritative_epoch;
	try
	{
		if (!this->choice_cache_.emplace(request_id, pending).second)
			return (FT_ERR_ALREADY_EXISTS);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	action.schema_version = this->action_session_id_ != 0U ? 3U : 1U;
	action.session_id = this->action_session_id_;
	action.request_id = request_id;
	action.command.command_sequence = this->next_command_sequence_;
	action.command.player_id = RegenerationCardSession::PLAYER_ID;
	action.command.type = CARD_GAME_INTENT_CHOOSE_OPTION;
	action.command.card_id = choice_id;
	action.command.target_instance = option_id;
	action.command.authoritative_epoch = authoritative_epoch;
	result = this->engine_.submit_host_action(action, &action_result, nullptr,
		authoritative_epoch);
	if (result != FT_ERR_SUCCESS)
	{
		this->choice_cache_.erase(request_id);
		return (result);
	}
	pending.result.session_id = this->action_session_id_;
	pending.result.request_id = request_id;
	pending.result.command_sequence = this->next_command_sequence_;
	pending.result.count = 1U;
	pending.result.replayed = false;
	auto committed = this->choice_cache_.find(request_id);
	if (committed == this->choice_cache_.end())
		return (FT_ERR_INVALID_STATE);
	committed->second.result = pending.result;
	++this->next_command_sequence_;
	*result_out = pending.result;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::query_expiry_result(uint64_t request_id,
	bool *found_out, uint32_t *resolved_count_out) const noexcept
{
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (request_id == 0U || found_out == nullptr
		|| resolved_count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	const auto cached = this->expiry_cache_.find(request_id);
	*found_out = cached != this->expiry_cache_.end();
	*resolved_count_out = cached == this->expiry_cache_.end()
		? 0U : cached->second.resolved_count;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::resolve_all_pending_choices_to_default(
	uint32_t *resolved_count_out) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (resolved_count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (this->engine_.resolve_all_pending_choices_to_default(
		resolved_count_out));
}

int32_t RegenerationCardSession::draw_cards(uint64_t request_id,
	uint64_t source_host_intent_sequence, uint32_t draw_count,
	uint32_t required_discard_count,
	RegenerationCardActionResult *result_out,
	uint64_t authenticated_actor_id) noexcept
{
	std::map<uint64_t, CachedDraw>::iterator cached;
	std::map<uint64_t, CachedDraw>::const_iterator prior_draw;
	std::map<uint64_t, CachedPlay>::const_iterator play;
	card_game_action action = {};
	card_game_action_result action_result = {};
	RegenerationCardActionResult result = {};
	uint32_t pending_intent_count;
	uint32_t post_draw_hand_count;
	int32_t error_code;

	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (result_out == nullptr || request_id == 0U
		|| source_host_intent_sequence == 0U || draw_count == 0U
		|| draw_count > FT_CARD_GAME_MAX_DRAW_COUNT
		|| required_discard_count > FT_CARD_GAME_MAX_REQUIRED_DISCARD_COUNT)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->expiry_cache_.find(request_id) != this->expiry_cache_.end()
		|| this->choice_cache_.find(request_id) != this->choice_cache_.end())
		return (FT_ERR_ALREADY_EXISTS);
	cached = this->draw_cache_.find(request_id);
	if (cached != this->draw_cache_.end())
	{
		if (cached->second.source_host_intent_sequence
				!= source_host_intent_sequence
			|| cached->second.draw_count != draw_count
			|| cached->second.required_discard_count != required_discard_count
			|| (authenticated_actor_id != 0U
				&& cached->second.authenticated_actor_id
					!= authenticated_actor_id))
			return (FT_ERR_INVALID_ARGUMENT);
		*result_out = cached->second.result;
		result_out->replayed = true;
		return (FT_ERR_SUCCESS);
	}
	if (!((draw_count == 2U && required_discard_count == 1U)
			|| (draw_count == 3U && required_discard_count == 0U)
			|| (draw_count == 1U && required_discard_count == 0U)))
		return (FT_ERR_CONFIGURATION);
	play = this->play_cache_.find(request_id);
	if (play != this->play_cache_.end()
		&& (play->second.result.host_intent_sequence
			!= source_host_intent_sequence
			|| (play->second.result.effect_kind
				!= REGENERATION_CARD_EFFECT_ULTIMATE_GREED
				&& play->second.result.effect_kind
				!= REGENERATION_CARD_EFFECT_FORTUNES_DRAW)))
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->discard_cache_.find(request_id)
			!= this->discard_cache_.end())
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->play_cache_.size() + this->draw_cache_.size()
		+ this->discard_cache_.size() + this->expiry_cache_.size()
		+ this->choice_cache_.size()
		>= RegenerationCardSession::MAXIMUM_PLAY_REQUESTS
		|| this->next_command_sequence_ == UINT64_MAX)
		return (FT_ERR_FULL);
	error_code = this->engine_.get_pending_host_intent_count(
		&pending_intent_count);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (pending_intent_count != 0U)
		return (FT_ERR_INVALID_OPERATION);
	play = this->play_cache_.begin();
	while (play != this->play_cache_.end()
		&& (play->second.result.host_intent_sequence
			!= source_host_intent_sequence
			|| (play->second.result.effect_kind
				!= REGENERATION_CARD_EFFECT_DOUBLE_DRAFT
				&& play->second.result.effect_kind
				!= REGENERATION_CARD_EFFECT_ULTIMATE_GREED
				&& play->second.result.effect_kind
				!= REGENERATION_CARD_EFFECT_FORTUNES_DRAW)))
		++play;
	if (play == this->play_cache_.end())
		return (FT_ERR_PERMISSION_DENIED);
	if (authenticated_actor_id != 0U
		&& play->second.authenticated_actor_id != authenticated_actor_id)
		return (FT_ERR_PERMISSION_DENIED);
	if ((play->second.result.effect_kind
			== REGENERATION_CARD_EFFECT_DOUBLE_DRAFT
			&& (draw_count != 2U || required_discard_count != 1U))
		|| (play->second.result.effect_kind
			== REGENERATION_CARD_EFFECT_ULTIMATE_GREED
			&& (draw_count != 3U || required_discard_count != 0U))
		|| (play->second.result.effect_kind
			== REGENERATION_CARD_EFFECT_FORTUNES_DRAW
			&& (draw_count != 1U || required_discard_count != 0U)))
		return (FT_ERR_PERMISSION_DENIED);
	prior_draw = this->draw_cache_.begin();
	while (prior_draw != this->draw_cache_.end())
	{
		if (prior_draw->second.source_host_intent_sequence
			== source_host_intent_sequence)
			return (FT_ERR_PERMISSION_DENIED);
		++prior_draw;
	}
	try
	{
		CachedDraw pending = {};
		pending.authenticated_actor_id = play->second.authenticated_actor_id;
		pending.source_host_intent_sequence = source_host_intent_sequence;
		pending.draw_count = draw_count;
		pending.required_discard_count = required_discard_count;
		if (!this->draw_cache_.emplace(request_id, pending).second)
			return (FT_ERR_ALREADY_EXISTS);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	action.schema_version = this->action_session_id_ != 0U ? 3U : 1U;
	action.session_id = this->action_session_id_;
	action.request_id = request_id;
	action.command.command_sequence = this->next_command_sequence_;
	action.command.player_id = RegenerationCardSession::PLAYER_ID;
	action.command.type = CARD_GAME_INTENT_DRAW_CARD;
	action.command.draw_count = draw_count;
	action.command.required_discard_count = required_discard_count;
	if (this->action_session_id_ != 0U)
		error_code = this->engine_.submit_action(action, &action_result,
			nullptr);
	else
		error_code = this->engine_.submit_command(action.command, nullptr);
	if (error_code != FT_ERR_SUCCESS)
	{
		this->draw_cache_.erase(request_id);
		return (error_code);
	}
	result.request_id = request_id;
	result.session_id = this->action_session_id_;
	result.command_sequence = this->next_command_sequence_;
	result.count = draw_count;
	result.replayed = false;
	++this->next_command_sequence_;
	cached = this->draw_cache_.find(request_id);
	if (cached == this->draw_cache_.end())
		return (FT_ERR_INVALID_STATE);
	cached->second.result = result;
	error_code = this->engine_.get_hand_count(
		RegenerationCardSession::PLAYER_ID, &post_draw_hand_count);
	if (error_code != FT_ERR_SUCCESS)
		return (FT_ERR_INVALID_STATE);
	this->pending_double_draft_intent_sequence_ = 0U;
	this->pending_required_discard_count_ = post_draw_hand_count
		< required_discard_count ? post_draw_hand_count
		: required_discard_count;
	*result_out = result;
	return (FT_ERR_SUCCESS);
}

int32_t RegenerationCardSession::discard_hand_card(uint64_t request_id,
	uint64_t source_draw_request_id, uint32_t card_instance_id,
	RegenerationCardActionResult *result_out,
	uint64_t authenticated_actor_id) noexcept
{
	std::map<uint64_t, CachedDiscard>::iterator cached;
	std::map<uint64_t, CachedDraw>::const_iterator draw;
	card_game_action action = {};
	card_game_action_result action_result = {};
	RegenerationCardActionResult result = {};
	uint32_t pending_intent_count;
	int32_t error_code;

	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	if (result_out == nullptr || request_id == 0U
		|| source_draw_request_id == 0U || card_instance_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->expiry_cache_.find(request_id) != this->expiry_cache_.end()
		|| this->choice_cache_.find(request_id) != this->choice_cache_.end())
		return (FT_ERR_ALREADY_EXISTS);
	cached = this->discard_cache_.find(request_id);
	if (cached != this->discard_cache_.end())
	{
		if (cached->second.authenticated_actor_id != authenticated_actor_id
			|| cached->second.source_draw_request_id != source_draw_request_id
			|| cached->second.card_instance_id != card_instance_id)
			return (FT_ERR_INVALID_ARGUMENT);
		*result_out = cached->second.result;
		result_out->replayed = true;
		return (FT_ERR_SUCCESS);
	}
	if (this->play_cache_.find(request_id) != this->play_cache_.end()
		|| this->draw_cache_.find(request_id) != this->draw_cache_.end()
		|| this->expiry_cache_.find(request_id) != this->expiry_cache_.end()
		|| this->choice_cache_.find(request_id) != this->choice_cache_.end())
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->play_cache_.size() + this->draw_cache_.size()
		+ this->discard_cache_.size() + this->expiry_cache_.size()
		+ this->choice_cache_.size()
		>= RegenerationCardSession::MAXIMUM_PLAY_REQUESTS
		|| this->next_command_sequence_ == UINT64_MAX)
		return (FT_ERR_FULL);
	error_code = this->engine_.get_pending_host_intent_count(
		&pending_intent_count);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (pending_intent_count != 0U)
		return (FT_ERR_INVALID_OPERATION);
	draw = this->draw_cache_.find(source_draw_request_id);
	if (draw == this->draw_cache_.end()
		|| draw->second.authenticated_actor_id != authenticated_actor_id
		|| draw->second.required_discard_count == 0U)
		return (FT_ERR_PERMISSION_DENIED);
	if (this->pending_required_discard_count_ == 0U)
		return (FT_ERR_PERMISSION_DENIED);
	try
	{
		CachedDiscard pending = {};
		pending.authenticated_actor_id = authenticated_actor_id;
		pending.source_draw_request_id = source_draw_request_id;
		pending.card_instance_id = card_instance_id;
		if (!this->discard_cache_.emplace(request_id, pending).second)
			return (FT_ERR_ALREADY_EXISTS);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	action.schema_version = this->action_session_id_ != 0U ? 3U : 1U;
	action.session_id = this->action_session_id_;
	action.request_id = request_id;
	action.command.command_sequence = this->next_command_sequence_;
	action.command.player_id = RegenerationCardSession::PLAYER_ID;
	action.command.type = CARD_GAME_INTENT_DISCARD_HAND;
	action.command.card_id = RegenerationCardSession::DISCARD_ZONE_ID;
	action.command.target_instance = card_instance_id;
	if (this->action_session_id_ != 0U)
		error_code = this->engine_.submit_action(action, &action_result,
			nullptr);
	else
		error_code = this->engine_.submit_command(action.command, nullptr);
	if (error_code != FT_ERR_SUCCESS)
	{
		this->discard_cache_.erase(request_id);
		return (error_code);
	}
	result.request_id = request_id;
	result.session_id = this->action_session_id_;
	result.command_sequence = this->next_command_sequence_;
	result.count = 1U;
	result.replayed = false;
	++this->next_command_sequence_;
	cached = this->discard_cache_.find(request_id);
	if (cached == this->discard_cache_.end())
		return (FT_ERR_INVALID_STATE);
	cached->second.result = result;
	this->pending_required_discard_count_ -= 1U;
	*result_out = result;
	return (FT_ERR_SUCCESS);
}
