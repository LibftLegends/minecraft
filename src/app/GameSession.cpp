#include "../../src/app/GameSession.hpp"
#include "../../src/app/GameSessionPersistence.hpp"
#include "../../src/app/GameSessionEntityState.hpp"
#include "../../src/entities/WorldAnimalSimulation.hpp"
#include "../../src/entities/WorldProjectileSimulation.hpp"
#include "../../src/inventory/MinecraftLootCatalog.hpp"
#include "../../src/transactions/WorldItemPickupTransaction.hpp"
#include "../../src/transactions/WorldItemCraftTransaction.hpp"
#include "../../src/transactions/WorldItemProcessingTransaction.hpp"
#include "../../src/transactions/WorldItemProcessingAdvanceTransaction.hpp"
#include "../../src/transactions/WorldItemCardTransferTransaction.hpp"
#include "../../src/transactions/WorldBlockMiningTransaction.hpp"
#include "../../src/transactions/WorldEquipmentRepairTransaction.hpp"
#include "../../src/transactions/WorldEquipmentLoadoutTransaction.hpp"
#include "../../src/transactions/WorldProjectileLaunchTransaction.hpp"
#include "../../src/transactions/RecipeDiscoveryTransaction.hpp"
#include "../../src/world/WorldRevisionServer.hpp"
#include "../../src/world/WorldRevisionChunkSet.hpp"
#include "../../src/network/ProtocolBoatMoveIntentMessage.hpp"
#include "../../src/network/ProtocolBoatMoveResultMessage.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"
#include "../../Libft/Modules/Encryption/encryption.hpp"
#include "../../Libft/Modules/File/file_utils.hpp"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <vector>
#if defined(__APPLE__)
# include <mach/mach.h>
# include <mach/task.h>
#endif

const char *GameSession::BIOME_NAMES[5] = {"PLAINS", "HILLS", "DESERT", "SNOW", "MOUNTAINS"};

namespace
{
	static const int32_t PLAYER_INITIAL_HIT_POINTS = 100;
	static const uint64_t DRAW_NOTICE_TRANSACTION_SCOPE =
		0x574F524C44445257ULL;
	static const uint32_t DRAW_NOTICE_TRANSACTION_TYPE = 12U;
	static const uint32_t REGENERATION_HOST_OUTPUT_TRANSACTION_TYPE = 13U;
	static const uint64_t RECIPE_RESEARCH_TRANSACTION_SCOPE =
		0x4D43524553454152ULL;
	static const uint32_t RECIPE_RESEARCH_TRANSACTION_TYPE = 14U;
	static const uint32_t RECIPE_RESEARCH_SOURCE_TYPE = 0x52535243U;
	static const int32_t RECIPE_RESEARCH_COST_ITEM_ID =
		MinecraftItemCatalog::CARD_STOCK_ITEM_ID;
	static const uint32_t RECIPE_RESEARCH_COST_QUANTITY = 2U;
	static const uint32_t RECIPE_RESEARCH_PAYLOAD_SIZE = 60U;
	static const uint32_t RECIPE_RESEARCH_RECIPE_IDS[] = {
		MinecraftRecipeCatalog::VOLCANO_MARK_CARD_RECIPE_ID,
		MinecraftRecipeCatalog::ULTIMATE_GREED_CARD_RECIPE_ID,
		MinecraftRecipeCatalog::BIOME_RUPTURE_CARD_RECIPE_ID,
		MinecraftRecipeCatalog::ORE_OVERGROWTH_CARD_RECIPE_ID,
		MinecraftRecipeCatalog::SHIPWRECK_CHART_CARD_RECIPE_ID,
		MinecraftRecipeCatalog::RUINED_VILLAGE_RELIC_CARD_RECIPE_ID,
		MinecraftRecipeCatalog::UNSTABLE_CATALYST_CARD_RECIPE_ID,
		MinecraftRecipeCatalog::DESERT_PYRAMID_SEAL_CARD_RECIPE_ID,
		MinecraftRecipeCatalog::ANCIENT_CANOPY_SEAL_CARD_RECIPE_ID,
		MinecraftRecipeCatalog::SPIDER_NEST_SEAL_CARD_RECIPE_ID,
		MinecraftRecipeCatalog::VOLATILE_STABILIZER_CARD_RECIPE_ID,
		MinecraftRecipeCatalog::FORTUNES_DRAW_CARD_RECIPE_ID};
	static const uint64_t REGENERATION_HOST_OUTPUT_TRANSACTION_SCOPE_SALT =
		0x524547484F53544FULL;

	static uint64_t game_session_regeneration_host_output_scope(
		uint64_t session_id) noexcept
	{
		const uint64_t scope = session_id
			^ REGENERATION_HOST_OUTPUT_TRANSACTION_SCOPE_SALT;
		return (scope == 0U ? REGENERATION_HOST_OUTPUT_TRANSACTION_SCOPE_SALT
			: scope);
	}
	static const uint32_t DRAW_NOTICE_TRANSACTION_VERSION = 1U;
	static const uint32_t DRAW_NOTICE_TRANSACTION_HEADER_BYTES = 32U;
	static const uint32_t DRAW_NOTICE_TRANSACTION_ENTRY_BYTES = 24U;
	static const uint32_t DRAW_NOTICE_TRANSACTION_MAXIMUM_CHUNKS = 64U;
	static int32_t game_session_collect_equipped_instance_ids(
		const game_character &character, std::vector<uint64_t> &instance_ids,
		uint64_t *maximum_instance_id_out) noexcept
	{
		const int32_t equipment_slots[] = {EQUIP_HEAD, EQUIP_CHEST,
			EQUIP_WEAPON, EQUIP_LEGS, EQUIP_FEET, EQUIP_OFF_HAND};
		if (maximum_instance_id_out == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		for (uint32_t index = 0U;
			index < sizeof(equipment_slots) / sizeof(equipment_slots[0]); ++index)
		{
			ft_sharedptr<game_item> *item = character.get_equipped_item(
				equipment_slots[index]);
			if (item == nullptr)
				return (character.get_error());
			if (*item)
			{
				const uint64_t instance_id = (*item)->get_instance_id();
				if (instance_id != 0U)
				{
					try
					{
						instance_ids.push_back(instance_id);
					}
					catch (const std::bad_alloc &)
					{
						(void)item->destroy();
						delete item;
						return (FT_ERR_NO_MEMORY);
					}
					*maximum_instance_id_out = std::max(*maximum_instance_id_out,
						instance_id);
				}
			}
			(void)item->destroy();
			delete item;
		}
		return (FT_ERR_SUCCESS);
	}
	static int32_t game_session_validate_equipped_catalog(
		const game_character &character,
		const MinecraftItemCatalog &catalog) noexcept
	{
		struct SlotBinding
		{
			int32_t character_slot;
			MinecraftEquipmentSlot catalog_slot;
		};
		const SlotBinding slots[] = {
			{EQUIP_WEAPON, MINECRAFT_EQUIPMENT_MAIN_HAND},
			{EQUIP_OFF_HAND, MINECRAFT_EQUIPMENT_OFF_HAND},
			{EQUIP_HEAD, MINECRAFT_EQUIPMENT_HEAD},
			{EQUIP_CHEST, MINECRAFT_EQUIPMENT_CHEST},
			{EQUIP_LEGS, MINECRAFT_EQUIPMENT_LEGS},
			{EQUIP_FEET, MINECRAFT_EQUIPMENT_FEET}};
		for (uint32_t index = 0U;
			index < sizeof(slots) / sizeof(slots[0]); ++index)
		{
			ft_sharedptr<game_item> *item = character.get_equipped_item(
				slots[index].character_slot);
			if (item == nullptr)
				return (character.get_error());
			if (*item)
			{
				MinecraftEquipmentRule rule = {};
				game_inventory_item_snapshot snapshot = {};
				uint32_t current_durability = 0U;
				uint32_t maximum_durability = 0U;
				int32_t result = catalog.resolve_equipment_rule(
					(*item)->get_item_id(), &rule);
				if (result == FT_ERR_SUCCESS
					&& (rule.slot != slots[index].catalog_slot
						|| (*item)->get_instance_id() == 0U
						|| (*item)->get_stack_size() != 1
						|| (*item)->get_max_stack() != 1))
					result = FT_ERR_INVALID_STATE;
				snapshot.item_id = (*item)->get_item_id();
				snapshot.instance_id = (*item)->get_instance_id();
				snapshot.stack_size = (*item)->get_stack_size();
				snapshot.max_stack = (*item)->get_max_stack();
				snapshot.modifier1_id = (*item)->get_modifier1_id();
				snapshot.modifier1_value = (*item)->get_modifier1_value();
				if (result == FT_ERR_SUCCESS)
					result = catalog.query_equipment_durability(snapshot,
						&current_durability, &maximum_durability);
				(void)item->destroy();
				delete item;
				if (result != FT_ERR_SUCCESS)
					return (result);
			}
			else
			{
				(void)item->destroy();
				delete item;
			}
		}
		return (FT_ERR_SUCCESS);
	}
	static int32_t game_session_query_equipped_armor(
		const game_character &character, const MinecraftItemCatalog &catalog,
		uint32_t *armor_points_out) noexcept
	{
		static const uint32_t FULL_ARMOR_SET_BONUS = 2U;
		const int32_t character_slots[] = {EQUIP_HEAD, EQUIP_CHEST,
			EQUIP_LEGS, EQUIP_FEET};
		const MinecraftEquipmentSlot catalog_slots[] = {
			MINECRAFT_EQUIPMENT_HEAD, MINECRAFT_EQUIPMENT_CHEST,
			MINECRAFT_EQUIPMENT_LEGS, MINECRAFT_EQUIPMENT_FEET};
		uint64_t total_armor = 0U;
		uint32_t functional_piece_mask = 0U;
		MinecraftMaterialTier set_tier = MINECRAFT_MATERIAL_CLOTH;
		bool set_tier_initialized = false;
		bool matching_set_tier = true;
		if (armor_points_out == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		for (uint32_t index = 0U;
			index < sizeof(character_slots) / sizeof(character_slots[0]); ++index)
		{
			ft_sharedptr<game_item> *item = character.get_equipped_item(
				character_slots[index]);
			if (item == nullptr)
				return (character.get_error());
			if (*item)
			{
				MinecraftEquipmentRule rule = {};
				game_inventory_item_snapshot snapshot = {};
				uint32_t current_durability = 0U;
				uint32_t maximum_durability = 0U;
				int32_t result = catalog.resolve_equipment_rule(
					(*item)->get_item_id(), &rule);
				if (result == FT_ERR_SUCCESS
					&& rule.slot != catalog_slots[index])
					result = FT_ERR_INVALID_STATE;
				snapshot.item_id = (*item)->get_item_id();
				snapshot.modifier1_id = (*item)->get_modifier1_id();
				snapshot.modifier1_value = (*item)->get_modifier1_value();
				if (result == FT_ERR_SUCCESS)
					result = catalog.query_equipment_durability(snapshot,
						&current_durability, &maximum_durability);
				if (result == FT_ERR_SUCCESS && current_durability > 0U)
				{
					total_armor += rule.armor_points;
					functional_piece_mask |= (1U << index);
					if (!set_tier_initialized)
					{
						set_tier = rule.material_tier;
						set_tier_initialized = true;
					}
					else if (set_tier != rule.material_tier)
						matching_set_tier = false;
				}
			(void)item->destroy();
			delete item;
			if (result != FT_ERR_SUCCESS)
				return (result);
		}
		else
		{
			(void)item->destroy();
			delete item;
		}
		}
		if (functional_piece_mask == 0x0FU && matching_set_tier)
			total_armor += FULL_ARMOR_SET_BONUS;
		*armor_points_out = static_cast<uint32_t>(std::min<uint64_t>(
			total_armor, static_cast<uint64_t>(
				std::numeric_limits<uint32_t>::max())));
		return (FT_ERR_SUCCESS);
	}
	static int32_t game_session_query_shield_guard(
		const game_character &character, const MinecraftItemCatalog &catalog,
		uint32_t *reduction_out) noexcept
	{
		if (reduction_out == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		*reduction_out = 0U;
		ft_sharedptr<game_item> *shield = character.get_equipped_item(
			EQUIP_OFF_HAND);
		if (shield == nullptr)
			return (character.get_error());
		int32_t result = FT_ERR_SUCCESS;
		if (*shield)
		{
			MinecraftEquipmentRule rule = {};
			game_inventory_item_snapshot snapshot = {};
			uint32_t durability = 0U;
			uint32_t maximum_durability = 0U;
			result = catalog.resolve_equipment_rule((*shield)->get_item_id(),
				&rule);
			if (result == FT_ERR_SUCCESS
				&& rule.slot != MINECRAFT_EQUIPMENT_OFF_HAND)
				result = FT_ERR_INVALID_STATE;
			if (result == FT_ERR_SUCCESS)
			{
				snapshot.item_id = (*shield)->get_item_id();
				snapshot.modifier1_id = (*shield)->get_modifier1_id();
				snapshot.modifier1_value = (*shield)->get_modifier1_value();
				result = catalog.query_equipment_durability(snapshot,
					&durability, &maximum_durability);
			}
			if (result == FT_ERR_SUCCESS && durability > 0U)
				*reduction_out = rule.armor_points;
		}
		(void)shield->destroy();
		delete shield;
		return (result);
	}
	static int32_t game_session_apply_equipped_armor_wear(
		game_character &character, const MinecraftItemCatalog &catalog,
		uint32_t wear_points, int32_t modifier_id_before_out[4],
		int32_t modifier_value_before_out[4], ft_bool equipped_out[4]) noexcept
	{
		const int32_t character_slots[] = {EQUIP_HEAD, EQUIP_CHEST,
			EQUIP_LEGS, EQUIP_FEET};
		uint32_t index = 0U;
		while (index < 4U)
		{
			ft_sharedptr<game_item> *item = character.get_equipped_item(
				character_slots[index]);
			if (item == nullptr)
				return (character.get_error());
			modifier_id_before_out[index] = 0;
			modifier_value_before_out[index] = 0;
			equipped_out[index] = FT_FALSE;
			int32_t result = FT_ERR_SUCCESS;
			if (*item)
			{
				modifier_id_before_out[index] =
					(*item)->get_modifier1_id();
				modifier_value_before_out[index] =
					(*item)->get_modifier1_value();
				MinecraftEquipmentRule rule = {};
				game_inventory_item_snapshot snapshot = {};
				uint32_t current_durability = 0U;
				uint32_t maximum_durability = 0U;
				result = catalog.resolve_equipment_rule((*item)->get_item_id(),
					&rule);
				if (result == FT_ERR_SUCCESS)
				{
					snapshot.item_id = (*item)->get_item_id();
					snapshot.modifier1_id = (*item)->get_modifier1_id();
					snapshot.modifier1_value = (*item)->get_modifier1_value();
					result = catalog.query_equipment_durability(snapshot,
						&current_durability, &maximum_durability);
				}
				if (result == FT_ERR_SUCCESS && current_durability > 0U)
				{
					equipped_out[index] = FT_TRUE;
					(*item)->set_modifier1_id(
						MinecraftItemCatalog::EQUIPMENT_DURABILITY_MODIFIER_ID);
					(*item)->set_modifier1_value(static_cast<int32_t>(
						current_durability > wear_points
							? current_durability - wear_points : 0U));
				}
			}
			(void)item->destroy();
			delete item;
			if (result != FT_ERR_SUCCESS)
				return (result);
			index += 1U;
		}
		return (FT_ERR_SUCCESS);
	}
	static int32_t game_session_restore_equipped_armor_wear(
		game_character &character, const int32_t modifier_id_before[4],
		const int32_t modifier_value_before[4],
		const ft_bool equipped[4]) noexcept
	{
		const int32_t character_slots[] = {EQUIP_HEAD, EQUIP_CHEST,
			EQUIP_LEGS, EQUIP_FEET};
		uint32_t index = 0U;
		while (index < 4U)
		{
			if (equipped[index] == FT_TRUE)
			{
				ft_sharedptr<game_item> *item = character.get_equipped_item(
					character_slots[index]);
				if (item == nullptr)
					return (character.get_error());
				if (*item)
				{
					(*item)->set_modifier1_id(modifier_id_before[index]);
					(*item)->set_modifier1_value(
						modifier_value_before[index]);
				}
				(void)item->destroy();
				delete item;
			}
			index += 1U;
		}
		return (FT_ERR_SUCCESS);
	}
	static int32_t game_session_apply_shield_wear(
		game_character &character, const MinecraftItemCatalog &catalog,
		uint32_t wear_points, int32_t *modifier_id_before_out,
		int32_t *modifier_value_before_out, ft_bool *wear_applied_out)
		noexcept
	{
		if (modifier_id_before_out == nullptr
			|| modifier_value_before_out == nullptr || wear_applied_out == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		*modifier_id_before_out = 0;
		*modifier_value_before_out = 0;
		*wear_applied_out = FT_FALSE;
		ft_sharedptr<game_item> *shield = character.get_equipped_item(
			EQUIP_OFF_HAND);
		if (shield == nullptr)
			return (character.get_error());
		int32_t result = FT_ERR_SUCCESS;
		if (*shield)
		{
			MinecraftEquipmentRule rule = {};
			game_inventory_item_snapshot snapshot = {};
			uint32_t durability = 0U;
			uint32_t maximum_durability = 0U;
			*modifier_id_before_out = (*shield)->get_modifier1_id();
			*modifier_value_before_out = (*shield)->get_modifier1_value();
			result = catalog.resolve_equipment_rule((*shield)->get_item_id(),
				&rule);
			if (result == FT_ERR_SUCCESS
				&& rule.slot != MINECRAFT_EQUIPMENT_OFF_HAND)
				result = FT_ERR_INVALID_STATE;
			if (result == FT_ERR_SUCCESS)
			{
				snapshot.item_id = (*shield)->get_item_id();
				snapshot.modifier1_id = *modifier_id_before_out;
				snapshot.modifier1_value = *modifier_value_before_out;
				result = catalog.query_equipment_durability(snapshot,
					&durability, &maximum_durability);
			}
			if (result == FT_ERR_SUCCESS && durability > 0U)
			{
				(*shield)->set_modifier1_id(
					MinecraftItemCatalog::EQUIPMENT_DURABILITY_MODIFIER_ID);
				(*shield)->set_modifier1_value(static_cast<int32_t>(
					durability > wear_points ? durability - wear_points : 0U));
				*wear_applied_out = FT_TRUE;
			}
		}
		(void)shield->destroy();
		delete shield;
		return (result);
	}
	static int32_t game_session_restore_shield_wear(
		game_character &character, int32_t modifier_id,
		int32_t modifier_value, ft_bool wear_applied) noexcept
	{
		if (wear_applied != FT_TRUE)
			return (FT_ERR_SUCCESS);
		ft_sharedptr<game_item> *shield = character.get_equipped_item(
			EQUIP_OFF_HAND);
		if (shield == nullptr)
			return (character.get_error());
		if (*shield)
		{
			(*shield)->set_modifier1_id(modifier_id);
			(*shield)->set_modifier1_value(modifier_value);
		}
		(void)shield->destroy();
		delete shield;
		return (FT_ERR_SUCCESS);
	}
	static int32_t game_session_assign_equipped_instance_ids(
		game_character &character, MinecraftRecipeCatalog &recipes) noexcept
	{
		const int32_t equipment_slots[] = {EQUIP_HEAD, EQUIP_CHEST,
			EQUIP_WEAPON, EQUIP_LEGS, EQUIP_FEET, EQUIP_OFF_HAND};
		for (uint32_t index = 0U;
			index < sizeof(equipment_slots) / sizeof(equipment_slots[0]); ++index)
		{
			ft_sharedptr<game_item> *item = character.get_equipped_item(
				equipment_slots[index]);
			if (item == nullptr)
				return (character.get_error());
			if (*item && (*item)->get_instance_id() == 0U)
			{
				uint64_t instance_id = 0U;
				const int32_t result = recipes.allocate_output_instance_id(
					&instance_id);
				if (result != FT_ERR_SUCCESS)
				{
					(void)item->destroy();
					delete item;
					return (result);
				}
				(*item)->set_instance_id(instance_id);
			}
			(void)item->destroy();
			delete item;
		}
		return (FT_ERR_SUCCESS);
	}

	static uint32_t game_session_card_recipe_id(
		uint32_t card_definition_id) noexcept
	{
		if (card_definition_id == 15U)
			return (MinecraftRecipeCatalog::VOLCANO_MARK_CARD_RECIPE_ID);
		if (card_definition_id == 12U)
			return (MinecraftRecipeCatalog::ULTIMATE_GREED_CARD_RECIPE_ID);
		if (card_definition_id == 13U)
			return (MinecraftRecipeCatalog::BIOME_RUPTURE_CARD_RECIPE_ID);
		if (card_definition_id == 11U)
			return (MinecraftRecipeCatalog::ORE_OVERGROWTH_CARD_RECIPE_ID);
		if (card_definition_id == 18U)
			return (MinecraftRecipeCatalog::SHIPWRECK_CHART_CARD_RECIPE_ID);
		if (card_definition_id == 17U)
			return (MinecraftRecipeCatalog::RUINED_VILLAGE_RELIC_CARD_RECIPE_ID);
		if (card_definition_id == 19U)
			return (MinecraftRecipeCatalog::UNSTABLE_CATALYST_CARD_RECIPE_ID);
		if (card_definition_id == 20U)
			return (MinecraftRecipeCatalog::DESERT_PYRAMID_SEAL_CARD_RECIPE_ID);
		if (card_definition_id == 21U)
			return (MinecraftRecipeCatalog::ANCIENT_CANOPY_SEAL_CARD_RECIPE_ID);
		if (card_definition_id == 22U)
			return (MinecraftRecipeCatalog::SPIDER_NEST_SEAL_CARD_RECIPE_ID);
		if (card_definition_id == 16U)
			return (MinecraftRecipeCatalog::VOLATILE_STABILIZER_CARD_RECIPE_ID);
		if (card_definition_id == 14U)
			return (MinecraftRecipeCatalog::FORTUNES_DRAW_CARD_RECIPE_ID);
		return (0U);
	}

	static bool game_session_match_site_job(uint32_t job_index,
		const std::vector<uint8_t> &job_types,
		const std::vector<uint8_t> &site_masks,
		std::vector<int32_t> &chunk_jobs,
		std::vector<uint8_t> &visited_chunks) noexcept
	{
		for (uint32_t chunk_index = 0U;
			chunk_index < static_cast<uint32_t>(site_masks.size()); ++chunk_index)
		{
			if ((site_masks[chunk_index] & job_types[job_index]) == 0U
				|| visited_chunks[chunk_index] != 0U)
				continue ;
			visited_chunks[chunk_index] = 1U;
			if (chunk_jobs[chunk_index] < 0
				|| game_session_match_site_job(
					static_cast<uint32_t>(chunk_jobs[chunk_index]),
					job_types, site_masks, chunk_jobs, visited_chunks))
			{
				chunk_jobs[chunk_index] = static_cast<int32_t>(job_index);
				return (true);
			}
		}
		return (false);
	}
}

namespace
{
	static uint32_t game_session_catalyst_modifier_id(
		uint32_t variant) noexcept
	{
		if (variant >= 1U && variant <= 4U)
			return (static_cast<uint32_t>(REGENERATION_MODIFIER_HOSTILE_CAMP)
				+ variant - 1U);
		if (variant == 5U)
			return (REGENERATION_MODIFIER_HOSTILE_SHIPWRECK);
		if (variant == 6U)
			return (REGENERATION_MODIFIER_VOLCANIC_OUTPOST);
		if (variant == 7U)
			return (REGENERATION_MODIFIER_RUINED_VILLAGE);
		return (0U);
	}

	static bool game_session_chunk_has_ocean_water(const World &world,
		int32_t chunk_x, int32_t chunk_z) noexcept
	{
		const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
		const int32_t sea_level = world.voxel_generation_settings().sea_level;
		const int32_t sample_y = sea_level - 3;
		if (chunk == nullptr || !chunk->initialized || sea_level < 16
			|| sample_y < 1
			|| sample_y >= GAME_VOXEL_CHUNK_HEIGHT)
			return (false);
		for (int32_t z = 2; z <= 14; z += 4)
		{
			for (int32_t x = 2; x <= 14; x += 4)
			{
				uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
				if (chunk->chunk.read_block(x, sample_y, z, &block_id)
					!= FT_ERR_SUCCESS
					|| block_id != VOXEL_GENERATOR_WATER_BLOCK)
					return (false);
			}
		}
		return (true);
	}

	static bool game_session_chunk_has_volcanic_surface(const World &world,
		int32_t chunk_x, int32_t chunk_z) noexcept
	{
		const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
		if (chunk == nullptr || !chunk->initialized)
			return (false);
		uint32_t samples = 0U;
		for (int32_t z = 2; z <= 14; z += 4)
			for (int32_t x = 2; x <= 14; x += 4)
				for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1; y >= 1; --y)
				{
					uint32_t block = GAME_VOXEL_AIR_BLOCK;
					if (chunk->chunk.read_block(x, y, z, &block) != FT_ERR_SUCCESS)
						return (false);
					if (block == GAME_VOXEL_AIR_BLOCK
						|| block == VOXEL_GENERATOR_WATER_BLOCK)
						continue ;
					if (block == VOXEL_GENERATOR_BASALT_BLOCK
						|| block == VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK)
						++samples;
					break ;
				}
		return (samples >= 8U);
	}

	static bool game_session_chunk_has_desert_surface(const World &world,
		int32_t chunk_x, int32_t chunk_z) noexcept
	{
		const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
		if (chunk == nullptr || !chunk->initialized)
			return (false);
		uint32_t samples = 0U;
		for (int32_t z = 2; z <= 14; z += 4)
			for (int32_t x = 2; x <= 14; x += 4)
				for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1; y >= 1; --y)
				{
					uint32_t block = GAME_VOXEL_AIR_BLOCK;
					if (chunk->chunk.read_block(x, y, z, &block) != FT_ERR_SUCCESS)
						return (false);
					if (block == GAME_VOXEL_AIR_BLOCK
						|| block == VOXEL_GENERATOR_WATER_BLOCK)
						continue ;
					if (block == VOXEL_GENERATOR_SAND_BLOCK
						|| block == VOXEL_GENERATOR_RED_SAND_BLOCK
						|| block == VOXEL_GENERATOR_SANDSTONE_BLOCK)
						++samples;
					break ;
				}
		return (samples >= 12U);
	}

	static bool game_session_chunk_has_forest_surface(const World &world,
		int32_t chunk_x, int32_t chunk_z) noexcept
	{
		const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
		if (chunk == nullptr || !chunk->initialized)
			return (false);
		uint32_t samples = 0U;
		for (int32_t z = 2; z <= 14; z += 2)
			for (int32_t x = 2; x <= 14; x += 2)
				for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1; y >= 1; --y)
				{
					uint32_t block = GAME_VOXEL_AIR_BLOCK;
					if (chunk->chunk.read_block(x, y, z, &block) != FT_ERR_SUCCESS)
						return (false);
					if (block == VOXEL_GENERATOR_OAK_LEAVES_BLOCK
						|| block == VOXEL_GENERATOR_PINE_LEAVES_BLOCK
						|| block == VOXEL_GENERATOR_BIRCH_LEAVES_BLOCK)
					{
						++samples;
					break ;
					}
					if (block != GAME_VOXEL_AIR_BLOCK)
						break ;
				}
		return (samples >= 4U);
	}

	static bool game_session_chunk_has_cave_site(const World &world,
		int32_t chunk_x, int32_t chunk_z) noexcept
	{
		const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
		if (chunk == nullptr || !chunk->initialized)
			return (false);
		for (int32_t y = 48; y >= 8; --y)
		{
			uint32_t center = GAME_VOXEL_AIR_BLOCK;
			uint32_t side = GAME_VOXEL_AIR_BLOCK;
			uint32_t floor = GAME_VOXEL_AIR_BLOCK;
			if (chunk->chunk.read_block(8, y, 8, &center) != FT_ERR_SUCCESS
				|| chunk->chunk.read_block(9, y, 8, &side) != FT_ERR_SUCCESS
				|| chunk->chunk.read_block(8, y - 1, 8, &floor) != FT_ERR_SUCCESS)
				return (false);
			if (center == GAME_VOXEL_AIR_BLOCK
				&& side == GAME_VOXEL_AIR_BLOCK
				&& floor != GAME_VOXEL_AIR_BLOCK
				&& floor != VOXEL_GENERATOR_WATER_BLOCK)
				return (true);
		}
		return (false);
	}

	static bool game_session_chunk_has_stabilizer_opportunity_site(
		const World &world, int32_t chunk_x, int32_t chunk_z) noexcept
	{
		static const int32_t CENTERS[3U][2U] = {
			{4, 4}, {12, 4}, {8, 12}};
		const WorldChunk *chunk = world.find_chunk(chunk_x, chunk_z);
		if (chunk == nullptr || !chunk->initialized)
			return (false);
		for (const auto &center : CENTERS)
		{
			int32_t minimum_ground = GAME_VOXEL_CHUNK_HEIGHT;
			int32_t maximum_ground = -1;
			bool valid_site = true;
			for (int32_t offset_z = -2; offset_z <= 2 && valid_site;
				++offset_z)
			{
				for (int32_t offset_x = -2; offset_x <= 2 && valid_site;
					++offset_x)
				{
					int32_t ground_y = -1;
					for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 8;
						y >= 2; --y)
					{
						uint32_t block = GAME_VOXEL_AIR_BLOCK;
						if (chunk->chunk.read_block(center[0] + offset_x, y,
							center[1] + offset_z, &block) != FT_ERR_SUCCESS)
							return (false);
						if (voxel_block_is_solid(block) != FT_FALSE)
						{
							ground_y = y;
							break ;
						}
						if (block != GAME_VOXEL_AIR_BLOCK)
							break ;
					}
					if (ground_y < 2
						|| ground_y + 7 >= GAME_VOXEL_CHUNK_HEIGHT)
					{
						valid_site = false;
						break ;
					}
					for (int32_t y = ground_y + 1; y <= ground_y + 6; ++y)
					{
						uint32_t block = GAME_VOXEL_AIR_BLOCK;
						if (chunk->chunk.read_block(center[0] + offset_x, y,
							center[1] + offset_z, &block) != FT_ERR_SUCCESS
							|| block != GAME_VOXEL_AIR_BLOCK)
						{
							valid_site = false;
							break ;
						}
					}
					minimum_ground = std::min(minimum_ground, ground_y);
					maximum_ground = std::max(maximum_ground, ground_y);
				}
			}
			if (valid_site && maximum_ground == minimum_ground)
				return (true);
		}
		return (false);
	}

	static const uint64_t WORLD_LOOM_START_SCOPE = 0x574F524C444C4F4FULL;
	static const uint32_t WORLD_LOOM_START_TRANSACTION_TYPE = 6U;
	static const uint64_t BLOCK_PLACEMENT_SCOPE = 0x574F524C44424C4BULL;
	static const uint32_t BLOCK_PLACEMENT_TRANSACTION_TYPE = 8U;
	static const std::size_t BLOCK_PLACEMENT_PAYLOAD_SIZE = 56U;

	static int32_t game_session_stage_item_consumption(
		const game_inventory_snapshot &before, int32_t slot, int32_t item_id,
		uint64_t expected_revision,
		game_inventory_snapshot &after) noexcept
	{
		if (before.revision != expected_revision
			|| before.revision == UINT64_MAX || slot < 0 || item_id <= 0)
			return (FT_ERR_INVALID_STATE);
		try
		{
			after = before;
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		for (std::vector<game_inventory_item_snapshot>::iterator item =
			after.items.begin(); item != after.items.end(); ++item)
		{
			if (item->slot != slot)
				continue ;
			if (item->item_id != item_id || item->stack_size <= 0
				|| after.current_weight <= 0)
				return (FT_ERR_INVALID_STATE);
			--item->stack_size;
			--after.current_weight;
			if (item->stack_size == 0)
			{
				after.items.erase(item);
				if (after.used_slots == 0U)
					return (FT_ERR_INVALID_STATE);
				--after.used_slots;
			}
			after.revision += 1U;
			return (FT_ERR_SUCCESS);
		}
		return (FT_ERR_NOT_FOUND);
	}

	static void game_session_append_u32(std::vector<uint8_t> &bytes,
		uint32_t value)
	{
		for (uint32_t shift = 0U; shift < 32U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	static void game_session_append_u64(std::vector<uint8_t> &bytes,
		uint64_t value)
	{
		for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	static uint32_t game_session_read_u32(const std::vector<uint8_t> &bytes,
		std::size_t offset) noexcept
	{
		return (static_cast<uint32_t>(bytes[offset])
			| (static_cast<uint32_t>(bytes[offset + 1U]) << 8U)
			| (static_cast<uint32_t>(bytes[offset + 2U]) << 16U)
			| (static_cast<uint32_t>(bytes[offset + 3U]) << 24U));
	}

	static uint64_t game_session_read_u64(const std::vector<uint8_t> &bytes,
		std::size_t offset) noexcept
	{
		uint64_t value = 0U;
		for (uint32_t index = 0U; index < 8U; ++index)
			value |= static_cast<uint64_t>(bytes[offset + index])
				<< (index * 8U);
		return (value);
	}

	static int32_t game_session_read_i32(const std::vector<uint8_t> &bytes,
		std::size_t offset) noexcept
	{
		const uint32_t value = game_session_read_u32(bytes, offset);
		if (value <= static_cast<uint32_t>(
				std::numeric_limits<int32_t>::max()))
			return (static_cast<int32_t>(value));
		return (-1 - static_cast<int32_t>(UINT32_MAX - value));
	}

	static bool game_session_recipe_research_candidate(uint32_t recipe_id)
	{
		for (uint32_t candidate : RECIPE_RESEARCH_RECIPE_IDS)
			if (candidate == recipe_id)
				return (true);
		return (false);
	}

	static bool game_session_decode_recipe_research(
		const GameTransactionJournal::Record &record,
		uint64_t *inventory_revision, uint32_t *station_id,
		int32_t *station_x, int32_t *station_y, int32_t *station_z,
		uint64_t *station_revision, uint64_t *random_value,
		uint32_t *recipe_id) noexcept
	{
		const std::vector<uint8_t> &payload = record.payload;
		if (record.scope_id != RECIPE_RESEARCH_TRANSACTION_SCOPE
			|| record.transaction_type != RECIPE_RESEARCH_TRANSACTION_TYPE
			|| record.request_id == 0U
			|| payload.size() != RECIPE_RESEARCH_PAYLOAD_SIZE
			|| payload[0] != 'R' || payload[1] != 'S'
			|| payload[2] != 'R' || payload[3] != '1'
			|| game_session_read_u32(payload, 4U) != 1U
			|| game_session_read_u32(payload, 16U)
				!= static_cast<uint32_t>(MinecraftRecipeCatalog::CARD_PRESS_STATION_ID)
			|| game_session_read_u32(payload, 52U)
				!= static_cast<uint32_t>(RECIPE_RESEARCH_COST_ITEM_ID)
			|| game_session_read_u32(payload, 56U)
				!= RECIPE_RESEARCH_COST_QUANTITY)
			return (false);
		*inventory_revision = game_session_read_u64(payload, 8U);
		*station_id = game_session_read_u32(payload, 16U);
		*station_x = game_session_read_i32(payload, 20U);
		*station_y = game_session_read_i32(payload, 24U);
		*station_z = game_session_read_i32(payload, 28U);
		*station_revision = game_session_read_u64(payload, 32U);
		*random_value = game_session_read_u64(payload, 40U);
		*recipe_id = game_session_read_u32(payload, 48U);
		return (*inventory_revision != UINT64_MAX && *station_revision != 0U
			&& game_session_recipe_research_candidate(*recipe_id));
	}

	static uint64_t game_session_recipe_research_next_random(
		uint64_t &state) noexcept
	{
		uint64_t value;
		state += 0x9e3779b97f4a7c15ULL;
		value = state;
		value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
		value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
		return (value ^ (value >> 31U));
	}

	static uint64_t game_session_recipe_research_seed(const char *seed,
		uint64_t request_id, uint64_t inventory_revision,
		uint32_t world_revision) noexcept
	{
		uint64_t state = 14695981039346656037ULL;
		if (seed != nullptr)
		{
			for (std::size_t index = 0U; seed[index] != '\0'; ++index)
			{
				state ^= static_cast<uint8_t>(seed[index]);
				state *= 1099511628211ULL;
			}
		}
		state ^= request_id;
		state *= 1099511628211ULL;
		state ^= inventory_revision;
		state *= 1099511628211ULL;
		state ^= world_revision;
		return (state);
	}

	static int32_t game_session_apply_draw_notice_transaction(
		World &world, const GameTransactionJournal::Record &transaction) noexcept
	{
		const std::vector<uint8_t> &payload = transaction.payload;
		uint32_t entry_count;
		uint32_t notice_type;
		uint32_t index = 0U;

		if (transaction.scope_id != DRAW_NOTICE_TRANSACTION_SCOPE
			|| transaction.transaction_type != DRAW_NOTICE_TRANSACTION_TYPE
			|| transaction.request_id == 0U
			|| payload.size() < DRAW_NOTICE_TRANSACTION_HEADER_BYTES
			|| payload[0] != 'D' || payload[1] != 'R'
			|| payload[2] != 'N' || payload[3] != '1'
			|| game_session_read_u32(payload, 4U)
				!= DRAW_NOTICE_TRANSACTION_VERSION
			|| game_session_read_u64(payload, 8U) != transaction.request_id
			|| game_session_read_u32(payload, 28U) != 0U)
			return (FT_ERR_INVALID_ARGUMENT);
		notice_type = game_session_read_u32(payload, 16U);
		if (notice_type != RegenerationCardSession::EMPTY_DRAW_INTENT_TYPE_ID
			&& notice_type
				!= RegenerationCardSession::HAND_OVERFLOW_INTENT_TYPE_ID)
			return (FT_ERR_INVALID_ARGUMENT);
		const uint32_t delta = game_session_read_u32(payload, 20U);
		entry_count = game_session_read_u32(payload, 24U);
		if (delta == 0U || delta > static_cast<uint32_t>(INT32_MAX)
			|| entry_count == 0U
			|| entry_count > DRAW_NOTICE_TRANSACTION_MAXIMUM_CHUNKS
			|| payload.size() != DRAW_NOTICE_TRANSACTION_HEADER_BYTES
				+ entry_count * DRAW_NOTICE_TRANSACTION_ENTRY_BYTES)
			return (FT_ERR_INVALID_ARGUMENT);
		while (index < entry_count)
		{
			const std::size_t offset = DRAW_NOTICE_TRANSACTION_HEADER_BYTES
				+ index * DRAW_NOTICE_TRANSACTION_ENTRY_BYTES;
			const int32_t chunk_x = static_cast<int32_t>(
				game_session_read_u32(payload, offset));
			const int32_t chunk_z = static_cast<int32_t>(
				game_session_read_u32(payload, offset + 4U));
			const uint64_t expected_revision = game_session_read_u64(
				payload, offset + 8U);
			const uint32_t previous_value = game_session_read_u32(
				payload, offset + 16U);
			const uint32_t target_value = game_session_read_u32(
				payload, offset + 20U);
			WorldChunkInstabilityRecord actual = {};
			int32_t result;

			if (expected_revision == UINT64_MAX
				|| target_value < previous_value
				|| target_value - previous_value > delta)
				return (FT_ERR_INVALID_ARGUMENT);
			result = world.get_chunk_instability(chunk_x, chunk_z, &actual);
			if (result != FT_ERR_SUCCESS)
				return (result);
			if (actual.record_revision == expected_revision
				&& actual.current_value == previous_value)
			{
				if (target_value > previous_value)
					result = world.adjust_chunk_instability(expected_revision,
						chunk_x, chunk_z,
						static_cast<int32_t>(target_value - previous_value),
						nullptr);
			}
			else if (actual.record_revision != expected_revision + 1U
				|| actual.current_value != target_value)
				return (FT_ERR_INVALID_STATE);
			if (result != FT_ERR_SUCCESS)
				return (result);
			++index;
		}
		return (FT_ERR_SUCCESS);
	}

	static uint64_t game_session_crop_state_checksum(const uint8_t *bytes,
		std::size_t byte_count) noexcept
	{
		uint64_t hash = 14695981039346656037ULL;
		for (std::size_t index = 0U; index < byte_count; ++index)
		{
			hash ^= bytes[index];
			hash *= 1099511628211ULL;
		}
		return (hash);
	}

	static bool game_session_crop_deadline_less(
		const CropChunkGrowthDeadline &left,
		const CropChunkGrowthDeadline &right) noexcept
	{
		return (left.chunk_x < right.chunk_x
			|| (left.chunk_x == right.chunk_x
				&& left.chunk_z < right.chunk_z));
	}

	static int32_t game_session_serialize_crop_growth_state(
		const CropGrowthPolicy &policy, uint64_t current_tick,
		const std::vector<CropChunkGrowthDeadline> &deadlines,
		std::vector<uint8_t> &bytes_out) noexcept
	{
		std::vector<uint8_t> staged;
		if (policy.tick_interval == 0U
			|| policy.tick_interval > 120000U
			|| policy.maturity_roll_divisor == 0U
			|| policy.maturity_roll_divisor > 1000000U
			|| policy.wheat_minimum_light_level > 15U
			|| policy.paper_reed_minimum_light_level > 15U
			|| policy.hydration_radius > 8U || deadlines.size() > 4096U)
			return (FT_ERR_INVALID_ARGUMENT);
		for (std::size_t index = 1U; index < deadlines.size(); ++index)
			if (!game_session_crop_deadline_less(deadlines[index - 1U],
				deadlines[index]))
				return (FT_ERR_INVALID_ARGUMENT);
		try
		{
			staged.reserve(40U + deadlines.size() * 16U);
			staged.insert(staged.end(), {'C', 'G', 'S', '1'});
			game_session_append_u32(staged, 1U);
			game_session_append_u32(staged, policy.tick_interval);
			game_session_append_u32(staged, policy.maturity_roll_divisor);
			staged.push_back(policy.wheat_minimum_light_level);
			staged.push_back(policy.paper_reed_minimum_light_level);
			staged.push_back(policy.hydration_radius);
			staged.push_back(0U);
			game_session_append_u64(staged, current_tick);
			game_session_append_u32(staged,
				static_cast<uint32_t>(deadlines.size()));
			for (const CropChunkGrowthDeadline &deadline : deadlines)
			{
				game_session_append_u32(staged,
					static_cast<uint32_t>(deadline.chunk_x));
				game_session_append_u32(staged,
					static_cast<uint32_t>(deadline.chunk_z));
				game_session_append_u64(staged,
					deadline.next_growth_tick);
			}
			game_session_append_u64(staged,
				game_session_crop_state_checksum(staged.data(), staged.size()));
			bytes_out.swap(staged);
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		return (FT_ERR_SUCCESS);
	}

	static int32_t game_session_deserialize_crop_growth_state(
		const std::vector<uint8_t> &bytes, CropGrowthPolicy &policy_out,
		uint64_t &current_tick_out,
		std::vector<CropChunkGrowthDeadline> &deadlines_out) noexcept
	{
		std::vector<CropChunkGrowthDeadline> staged;
		uint32_t count;
		if (bytes.size() < 40U || bytes[0] != 'C' || bytes[1] != 'G'
			|| bytes[2] != 'S' || bytes[3] != '1'
			|| game_session_read_u32(bytes, 4U) != 1U)
			return (FT_ERR_INVALID_ARGUMENT);
		count = game_session_read_u32(bytes, 28U);
		if (count > 4096U || bytes.size() != 40U
			+ static_cast<std::size_t>(count) * 16U
			|| bytes[19U] != 0U
			|| game_session_read_u64(bytes, bytes.size() - 8U)
				!= game_session_crop_state_checksum(bytes.data(),
					bytes.size() - 8U))
			return (FT_ERR_INVALID_ARGUMENT);
		CropGrowthPolicy staged_policy = {game_session_read_u32(bytes, 8U),
			game_session_read_u32(bytes, 12U), bytes[16U], bytes[17U],
			bytes[18U]};
		if (staged_policy.tick_interval == 0U
			|| staged_policy.tick_interval > 120000U
			|| staged_policy.maturity_roll_divisor == 0U
			|| staged_policy.maturity_roll_divisor > 1000000U
			|| staged_policy.wheat_minimum_light_level > 15U
			|| staged_policy.paper_reed_minimum_light_level > 15U
			|| staged_policy.hydration_radius > 8U)
			return (FT_ERR_INVALID_ARGUMENT);
		try
		{
			staged.reserve(count);
			for (uint32_t index = 0U; index < count; ++index)
			{
				const std::size_t offset = 32U
					+ static_cast<std::size_t>(index) * 16U;
				CropChunkGrowthDeadline deadline = {
					static_cast<int32_t>(game_session_read_u32(bytes, offset)),
					static_cast<int32_t>(game_session_read_u32(bytes, offset + 4U)),
					game_session_read_u64(bytes, offset + 8U)};
				if (!staged.empty()
					&& !game_session_crop_deadline_less(staged.back(), deadline))
					return (FT_ERR_INVALID_ARGUMENT);
				staged.push_back(deadline);
			}
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
		policy_out = staged_policy;
		current_tick_out = game_session_read_u64(bytes, 20U);
		deadlines_out.swap(staged);
		return (FT_ERR_SUCCESS);
	}

	static int32_t game_session_find_volcano_square(
		const std::vector<World::RevisionChunkCoordinate> &paid_selected,
		const std::vector<World::RevisionChunkCoordinate> &all_selected,
		uint64_t reserved_chunk_count, uint32_t chunk_width) noexcept
	{
		std::vector<WorldRevisionManager::RevisionChunk> selected_records;
		std::vector<WorldRevisionManager::RevisionChunk> reserved_records;
		int32_t anchor_x = 0;
		int32_t anchor_z = 0;
		int32_t result;

		if (reserved_chunk_count > all_selected.size())
			return (FT_ERR_FULL);
		if (chunk_width < 9U || chunk_width > 16U)
			return (FT_ERR_CONFIGURATION);
		try
		{
			selected_records.reserve(all_selected.size());
			reserved_records.reserve(static_cast<std::size_t>(
				reserved_chunk_count));
			for (const World::RevisionChunkCoordinate &chunk : all_selected)
			{
				const bool bonus = std::find_if(paid_selected.begin(),
					paid_selected.end(), [&chunk](
						const World::RevisionChunkCoordinate &paid)
						{
							return (paid.chunk_x == chunk.chunk_x
								&& paid.chunk_z == chunk.chunk_z);
						}) == paid_selected.end();
				selected_records.push_back({chunk.chunk_x, chunk.chunk_z, bonus});
			}
			for (uint64_t index = 0U; index < reserved_chunk_count; ++index)
				reserved_records.push_back(
					selected_records[static_cast<std::size_t>(index)]);
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		result = WorldRevisionChunkSet::find_paid_square_anchor(
			selected_records, reserved_records, chunk_width, anchor_x, anchor_z);
		return (result);
	}

	static int32_t game_session_build_volcano_feature_preview(
		const std::vector<World::RevisionChunkCoordinate> &selected,
		const std::vector<World::RevisionChunkCoordinate> &paid,
		const std::vector<RegenerationWorldFeaturePlan> &plans,
		std::vector<World::RevisionChunkCoordinate> &highlighted) noexcept
	{
		std::vector<WorldRevisionManager::RevisionChunk> selected_records;
		std::vector<WorldRevisionManager::RevisionChunk> reserved;
		std::vector<World::RevisionChunkCoordinate> staged_highlights;
		try
		{
			selected_records.reserve(selected.size());
			reserved.reserve(selected.size());
			staged_highlights.reserve(selected.size());
			for (const World::RevisionChunkCoordinate &chunk : selected)
			{
				const bool bonus = std::find_if(paid.begin(), paid.end(),
					[&chunk](const World::RevisionChunkCoordinate &entry)
					{
						return (entry.chunk_x == chunk.chunk_x
							&& entry.chunk_z == chunk.chunk_z);
					}) == paid.end();
				selected_records.push_back({chunk.chunk_x, chunk.chunk_z, bonus});
			}
			for (const RegenerationWorldFeaturePlan &plan : plans)
			{
				if (plan.kind != REGENERATION_WORLD_FEATURE_VILLAGE
					&& plan.kind
						!= REGENERATION_WORLD_FEATURE_SURFACE_MAGMA_POOL
					&& plan.kind != REGENERATION_WORLD_FEATURE_RUINED_VILLAGE
					&& plan.kind != REGENERATION_WORLD_FEATURE_SHIPWRECK
					&& plan.kind != REGENERATION_WORLD_FEATURE_DESERT_PYRAMID
					&& plan.kind != REGENERATION_WORLD_FEATURE_ANCIENT_CANOPY
					&& plan.kind != REGENERATION_WORLD_FEATURE_SPIDER_NEST)
					continue ;
				std::size_t index = 0U;
				while (index < selected_records.size()
					&& WorldRevisionChunkSet::contains(reserved,
						selected_records[index].chunk_x,
						selected_records[index].chunk_z))
					++index;
				if (index >= selected_records.size())
					return (FT_ERR_FULL);
				reserved.push_back(selected_records[index]);
			}
			for (const RegenerationWorldFeaturePlan &plan : plans)
			{
				if (plan.kind != REGENERATION_WORLD_FEATURE_VOLCANO)
					continue ;
				int32_t anchor_x = 0;
				int32_t anchor_z = 0;
				const int32_t anchor_result =
					WorldRevisionChunkSet::find_paid_square_anchor(
						selected_records, reserved,
						plan.footprint_chunks, anchor_x, anchor_z);
				if (anchor_result != FT_ERR_SUCCESS)
					return (anchor_result);
				for (uint32_t dz = 0U; dz < plan.footprint_chunks; ++dz)
				{
					for (uint32_t dx = 0U; dx < plan.footprint_chunks; ++dx)
					{
						const int64_t x = static_cast<int64_t>(anchor_x) + dx;
						const int64_t z = static_cast<int64_t>(anchor_z) + dz;
						if (x > INT32_MAX || z > INT32_MAX)
							return (FT_ERR_OUT_OF_RANGE);
						const int32_t chunk_x = static_cast<int32_t>(x);
						const int32_t chunk_z = static_cast<int32_t>(z);
						const auto selected_chunk = std::find_if(
							selected_records.begin(), selected_records.end(),
							[chunk_x, chunk_z](
								const WorldRevisionManager::RevisionChunk &entry)
							{
								return (entry.chunk_x == chunk_x
									&& entry.chunk_z == chunk_z);
							});
						if (selected_chunk == selected_records.end())
							return (FT_ERR_INVALID_STATE);
						reserved.push_back(*selected_chunk);
						staged_highlights.push_back({chunk_x, chunk_z});
					}
				}
			}
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		highlighted.swap(staged_highlights);
		return (FT_ERR_SUCCESS);
	}

	int32_t game_session_item_save_path(const std::string &seed,
		char *path_out, std::size_t path_capacity)
	{
		uint8_t digest[32];
		const char *seed_data = seed.c_str();
		const std::size_t seed_length = std::strlen(seed_data);
		int written;
		std::size_t offset;

		if (path_out == nullptr || path_capacity < 88U)
			return (FT_ERR_INVALID_ARGUMENT);
		sha256_hash(seed_data, seed_length, digest);
		written = std::snprintf(path_out, path_capacity, "saves/world_items/");
		if (written < 0 || static_cast<std::size_t>(written) >= path_capacity)
			return (FT_ERR_OUT_OF_RANGE);
		offset = static_cast<std::size_t>(written);
		for (std::size_t index = 0U; index < sizeof(digest); ++index)
		{
			written = std::snprintf(path_out + offset, path_capacity - offset,
				"%02x", static_cast<unsigned int>(digest[index]));
			if (written != 2)
				return (FT_ERR_OUT_OF_RANGE);
			offset += 2U;
		}
		written = std::snprintf(path_out + offset, path_capacity - offset,
			".wisp");
		if (written != 5 || offset + 5U >= path_capacity)
			return (FT_ERR_OUT_OF_RANGE);
		return (FT_ERR_SUCCESS);
	}

	int32_t game_session_revision_save_path(const char *item_path,
		char *revision_path_out, std::size_t path_capacity)
	{
		const std::size_t item_path_length = item_path == nullptr ? 0U
			: std::strlen(item_path);

		if (item_path_length < 5U || revision_path_out == nullptr
			|| path_capacity <= item_path_length
			|| std::strcmp(item_path + item_path_length - 5U, ".wisp") != 0)
			return (FT_ERR_INVALID_ARGUMENT);
		std::memcpy(revision_path_out, item_path, item_path_length - 5U);
		std::memcpy(revision_path_out + item_path_length - 5U, ".wrv", 5U);
		return (FT_ERR_SUCCESS);
	}

	uint64_t game_session_card_seed(const std::string &seed)
	{
		uint8_t digest[32];
		uint64_t value = 0U;

		sha256_hash(seed.c_str(), std::strlen(seed.c_str()), digest);
		for (uint32_t index = 0U; index < 8U; ++index)
			value |= static_cast<uint64_t>(digest[index]) << (index * 8U);
		return (value == 0U ? 1U : value);
	}

	bool game_session_mesh_is_drawable(const chunk_mesh &mesh)
	{
		return (WorldChunk::mesh_is_drawable(mesh));
	}

	int32_t game_session_decode_chunk_coordinate(uint32_t value) noexcept
	{
		if (value <= static_cast<uint32_t>(
			std::numeric_limits<int32_t>::max()))
			return (static_cast<int32_t>(value));
		return (-1 - static_cast<int32_t>(
			std::numeric_limits<uint32_t>::max() - value));
	}

}

GameSession::GameSession()
		: camera_(), player_character_(), item_catalog_(), recipe_catalog_(), world_items_(), world_entities_(), transaction_journal_(), regeneration_cards_(), regeneration_world_rules_(), regeneration_world_rules_policy_(), regeneration_script_catalog_(), trusted_script_manifest_hash_(0U), regeneration_output_session_id_(0U), configured_regeneration_output_session_id_(0U), chunk_instability_policy_(), chunk_instability_policy_configured_(false), ore_overgrowth_instability_delta_(0), ultimate_greed_instability_delta_(0), biome_rupture_instability_delta_(0), volcano_mark_instability_delta_(0), empty_draw_instability_delta_(0), hand_overflow_instability_delta_(0), volcano_mark_footprint_chunks_(9U), crop_growth_policy_({200U, 3U, 9U, 9U, 4U}), recipe_discovery_authorizer_(nullptr), recipe_discovery_authorizer_user_data_(nullptr), script_catalog_authorizer_(nullptr), script_catalog_authorizer_user_data_(nullptr), processing_station_authorizer_(nullptr), processing_station_authorizer_user_data_(nullptr), processing_station_continuity_validator_(nullptr), processing_station_continuity_user_data_(nullptr), revision_occupancy_provider_(nullptr), revision_occupancy_provider_user_data_(nullptr), revision_clock_provider_(nullptr), revision_clock_provider_user_data_(nullptr), world_(), render_debug_(), revision_occupied_chunks_(), revision_occupied_chunk_x_(0), revision_occupied_chunk_z_(0), revision_occupancy_valid_(false),
      active_render_distance_(WorldCoordinates::REQUIRED_VISIBLE_DISTANCE), fps_accumulator_(0.0),
      rd_accumulator_(0.0), display_fps_(0.0), frame_ms_(0.0), performance_frame_ms_(16.67),
      fps_frame_count_(0U),
      selected_block_id_(VOXEL_GENERATOR_DIRT_BLOCK), boost_enabled_(false), active_(false),
      revision_preview_visible_(false), render_debug_frame_(0U), cached_ram_mb_(0U),
      cached_vram_mb_(0U), cached_biome_world_x_(0), cached_biome_world_z_(0),
      cached_biome_valid_(false), revision_preview_center_x_(0), revision_preview_center_z_(0),
      revision_preview_identifier_(0U), revision_preview_cache_valid_(false),
      revision_preview_cache_(), pickup_request_id_(1U),
	processing_tick_accumulator_(0.0), error_code_(FT_ERR_SUCCESS)
{
	shield_guard_active_ = false;
	shield_guard_until_tick_ = 0U;
	shield_guard_cooldown_until_tick_ = 0U;
	world_loom_start_committed_ = false;
	player_spawn_x_ = 0.0;
	player_spawn_z_ = 0.0;
	seed_[0] = '\0';
	world_item_save_path_[0] = '\0';
	world_revision_save_path_[0] = '\0';
	cached_biome_name_[0] = '\0';
}

GameSession::GameSession(const GameSession &other)
	    : camera_(), player_character_(), item_catalog_(), recipe_catalog_(), world_items_(), world_entities_(), transaction_journal_(), regeneration_cards_(), regeneration_world_rules_(), regeneration_world_rules_policy_(), regeneration_script_catalog_(), trusted_script_manifest_hash_(0U), regeneration_output_session_id_(0U), configured_regeneration_output_session_id_(0U), chunk_instability_policy_(), chunk_instability_policy_configured_(false), ore_overgrowth_instability_delta_(0), ultimate_greed_instability_delta_(0), biome_rupture_instability_delta_(0), empty_draw_instability_delta_(0), hand_overflow_instability_delta_(0), volcano_mark_footprint_chunks_(9U), crop_growth_policy_({200U, 3U, 9U, 9U, 4U}), recipe_discovery_authorizer_(nullptr), recipe_discovery_authorizer_user_data_(nullptr), script_catalog_authorizer_(nullptr), script_catalog_authorizer_user_data_(nullptr), processing_station_authorizer_(nullptr), processing_station_authorizer_user_data_(nullptr), processing_station_continuity_validator_(nullptr), processing_station_continuity_user_data_(nullptr), revision_occupancy_provider_(nullptr), revision_occupancy_provider_user_data_(nullptr), revision_clock_provider_(nullptr), revision_clock_provider_user_data_(nullptr), world_(), render_debug_(), revision_occupied_chunks_(), revision_occupied_chunk_x_(0), revision_occupied_chunk_z_(0), revision_occupancy_valid_(false),
      active_render_distance_(WorldCoordinates::REQUIRED_VISIBLE_DISTANCE), fps_accumulator_(0.0),
      rd_accumulator_(0.0), display_fps_(0.0), frame_ms_(0.0), performance_frame_ms_(16.67),
      fps_frame_count_(0U),
      selected_block_id_(VOXEL_GENERATOR_DIRT_BLOCK), boost_enabled_(false), active_(false),
      revision_preview_visible_(false), render_debug_frame_(0U), cached_ram_mb_(0U),
      cached_vram_mb_(0U), cached_biome_world_x_(0), cached_biome_world_z_(0),
      cached_biome_valid_(false), revision_preview_center_x_(0), revision_preview_center_z_(0),
      revision_preview_identifier_(0U), revision_preview_cache_valid_(false),
      revision_preview_cache_(), pickup_request_id_(1U),
	processing_tick_accumulator_(0.0), error_code_(FT_ERR_SUCCESS)
{
	shield_guard_active_ = false;
	shield_guard_until_tick_ = 0U;
	shield_guard_cooldown_until_tick_ = 0U;
	world_loom_start_committed_ = false;
	(void)other;
	player_spawn_x_ = 0.0;
	player_spawn_z_ = 0.0;
	seed_[0] = '\0';
	world_item_save_path_[0] = '\0';
	world_revision_save_path_[0] = '\0';
	cached_biome_name_[0] = '\0';
}

GameSession::~GameSession()
{
	if (this->active_)
		stop();
}

GameSession &GameSession::operator=(const GameSession &other)
{
	(void)other;
	return (*this);
}

void GameSession::reset_session_state()
{
	player_spawn_x_ = 0.0;
	player_spawn_z_ = 0.0;
	fluid_state_.clear();
	entity_state_.clear();
	active_render_distance_ = Settings::instance().render_distance();
	boost_enabled_ = false;
	selected_block_id_ = VOXEL_GENERATOR_DIRT_BLOCK;
	fps_accumulator_ = 0.0;
	fps_frame_count_ = 0U;
	rd_accumulator_ = 0.0;
	display_fps_ = 0.0;
	frame_ms_ = 0.0;
	performance_frame_ms_ = 16.67;
	error_code_ = FT_ERR_SUCCESS;
	pickup_request_id_ = 1U;
	processing_tick_accumulator_ = 0.0;
	shield_guard_active_ = false;
	shield_guard_until_tick_ = 0U;
	shield_guard_cooldown_until_tick_ = 0U;
	next_melee_attack_tick_ = 0U;
	active_ = true;
	world_loom_start_committed_ = false;
}

int GameSession::start(const std::string &seed, ApplicationWindow &window,
	VoxelRenderer &renderer)
{
	std::vector<uint8_t> card_session_bytes;
	std::vector<uint8_t> regeneration_rules_bytes;
	std::vector<uint8_t> chunk_instability_bytes;
	std::vector<uint8_t> revision_metadata_bytes;
	std::vector<uint8_t> crop_growth_policy_bytes;
	this->revision_server_checkpoint_state_.clear();
	uint64_t restored_output_session_id = 0U;
	const uint64_t card_seed = game_session_card_seed(seed);
	const char *startup_restore_stage = "item-catalog-initialization";

	processing_tick_accumulator_ = 0.0;
	crop_chunk_growth_deadlines_.clear();
	regeneration_output_session_id_ =
		configured_regeneration_output_session_id_;
	error_code_ = item_catalog_.initialize();
	if (error_code_ != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "[Session] startup failed stage=%s error=%d\n",
			startup_restore_stage, error_code_);
		return (error_code_);
	}
	startup_restore_stage = "recipe-catalog-initialization";
	error_code_ = recipe_catalog_.initialize(item_catalog_);
	if (error_code_ != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "[Session] startup failed stage=%s error=%d\n",
			startup_restore_stage, error_code_);
		(void)item_catalog_.destroy();
		return (error_code_);
	}
	for (const ConfiguredRecipeDiscoverySource &source
		: this->recipe_discovery_sources_)
	{
		error_code_ = this->recipe_catalog_.register_discovery_source(
			source.source_type_id, source.source_id, source.recipe_id);
		if (error_code_ != FT_ERR_SUCCESS)
		{
			(void)this->recipe_catalog_.destroy();
			(void)this->item_catalog_.destroy();
			return (error_code_);
		}
	}
	const uint32_t loot_discovery_tables[] = {
		MINECRAFT_LOOT_RUINED_CACHE,
		MINECRAFT_LOOT_SHIPWRECK_CACHE,
		MINECRAFT_LOOT_SHIPWRECK_CACHE_REWARD,
		MINECRAFT_LOOT_SHIPWRECK_CACHE_HIGH_REWARD,
		MINECRAFT_LOOT_DESERT_PYRAMID_CACHE,
		MINECRAFT_LOOT_ANCIENT_CANOPY_CACHE,
		MINECRAFT_LOOT_SPIDER_NEST_CACHE};
	for (uint32_t index = 0U;
		index < sizeof(loot_discovery_tables) / sizeof(loot_discovery_tables[0]);
		++index)
	{
		MinecraftLootRecipeDiscovery discovery = {};
		error_code_ = MinecraftLootCatalog::resolve_recipe_discovery(
			loot_discovery_tables[index], &discovery);
		if (error_code_ == FT_ERR_SUCCESS)
			error_code_ = this->recipe_catalog_.register_discovery_source(
				discovery.source_type_id, discovery.source_id,
				discovery.recipe_id);
		if (error_code_ != FT_ERR_SUCCESS)
		{
			(void)this->recipe_catalog_.destroy();
			(void)this->item_catalog_.destroy();
			return (error_code_);
		}
	}
	for (uint32_t recipe_id : RECIPE_RESEARCH_RECIPE_IDS)
	{
		error_code_ = this->recipe_catalog_.register_discovery_source(
			RECIPE_RESEARCH_SOURCE_TYPE, recipe_id, recipe_id);
		if (error_code_ != FT_ERR_SUCCESS)
		{
			(void)this->recipe_catalog_.destroy();
			(void)this->item_catalog_.destroy();
			return (error_code_);
		}
	}
	startup_restore_stage = "world-item-store-initialization";
	error_code_ = world_items_.initialize();
	if (error_code_ != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "[Session] startup failed stage=%s error=%d\n",
			startup_restore_stage, error_code_);
		(void)recipe_catalog_.destroy();
		(void)item_catalog_.destroy();
		return (error_code_);
	}
	startup_restore_stage = "world-entity-store-initialization";
	error_code_ = world_entities_.initialize();
	if (error_code_ != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "[Session] startup failed stage=%s error=%d\n",
			startup_restore_stage, error_code_);
		(void)world_items_.destroy();
		(void)recipe_catalog_.destroy();
		(void)item_catalog_.destroy();
		return (error_code_);
	}
	startup_restore_stage = "world-spawner-store-initialization";
	error_code_ = world_spawners_.initialize();
	if (error_code_ != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "[Session] startup failed stage=%s error=%d\n",
			startup_restore_stage, error_code_);
		(void)world_entities_.destroy();
		(void)world_items_.destroy();
		(void)recipe_catalog_.destroy();
		(void)item_catalog_.destroy();
		return (error_code_);
	}
	startup_restore_stage = "transaction-journal-initialization";
	error_code_ = transaction_journal_.initialize();
	if (error_code_ != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "[Session] startup failed stage=%s error=%d\n",
			startup_restore_stage, error_code_);
		(void)world_entities_.destroy();
		(void)world_items_.destroy();
		(void)recipe_catalog_.destroy();
		(void)item_catalog_.destroy();
		return (error_code_);
	}
	startup_restore_stage = "save-path-setup";
	error_code_ = game_session_item_save_path(seed, world_item_save_path_,
		sizeof(world_item_save_path_));
	if (error_code_ == FT_ERR_SUCCESS)
		error_code_ = game_session_revision_save_path(world_item_save_path_,
			world_revision_save_path_, sizeof(world_revision_save_path_));
	if (error_code_ == FT_ERR_SUCCESS)
		error_code_ = file_create_directories("saves/world_items", 0700);
	if (error_code_ != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "[Session] startup failed stage=%s error=%d\n",
			startup_restore_stage, error_code_);
		(void)transaction_journal_.destroy();
		(void)world_entities_.destroy();
		(void)world_items_.destroy();
		(void)recipe_catalog_.destroy();
		(void)item_catalog_.destroy();
		return (error_code_);
	}
	startup_restore_stage = "world-initialization";
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	std::fprintf(stderr, "World startup: initializing world\n");
	#endif
    error_code_ = world_.initialize(seed.c_str());
    if (error_code_ != FT_ERR_SUCCESS)
    {
		std::fprintf(stderr, "[Session] startup failed stage=%s error=%d\n",
			startup_restore_stage, error_code_);
		(void)transaction_journal_.destroy();
		(void)world_entities_.destroy();
		(void)world_items_.destroy();
		(void)recipe_catalog_.destroy();
		(void)item_catalog_.destroy();
        return error_code_;
    }
	error_code_ = world_.attach_entity_stores(world_entities_,
		world_spawners_, world_items_, item_catalog_);
	if (error_code_ != FT_ERR_SUCCESS)
	{
		world_.destroy();
		(void)transaction_journal_.destroy();
		(void)world_spawners_.destroy();
		(void)world_entities_.destroy();
		(void)world_items_.destroy();
		(void)recipe_catalog_.destroy();
		(void)item_catalog_.destroy();
		return (error_code_);
	}
	error_code_ = world_.set_generation_checkpoint_callback(
		&GameSession::save_snapshot_callback, this);
	if (error_code_ != FT_ERR_SUCCESS)
	{
		world_.destroy();
		(void)transaction_journal_.destroy();
		(void)world_spawners_.destroy();
		(void)world_entities_.destroy();
		(void)world_items_.destroy();
		(void)recipe_catalog_.destroy();
		(void)item_catalog_.destroy();
		return (error_code_);
	}
	if (error_code_ == FT_ERR_SUCCESS && chunk_instability_policy_configured_)
		error_code_ = world_.configure_chunk_instability(
			chunk_instability_policy_);
	if (error_code_ != FT_ERR_SUCCESS)
	{
		world_.destroy();
		(void)transaction_journal_.destroy();
		(void)world_entities_.destroy();
		(void)world_items_.destroy();
		(void)recipe_catalog_.destroy();
		(void)item_catalog_.destroy();
		return (error_code_);
	}
	startup_restore_stage = "regeneration-world-rules-initialization";
	error_code_ = initialize_regeneration_world_rules();
	if (error_code_ != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr, "[Session] startup failed stage=%s error=%d\n",
			startup_restore_stage, error_code_);
		world_.destroy();
		(void)transaction_journal_.destroy();
		(void)world_entities_.destroy();
		(void)world_items_.destroy();
		(void)recipe_catalog_.destroy();
		(void)item_catalog_.destroy();
		return (error_code_);
	}
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	std::fprintf(stderr, "World startup: world initialized\n");
	#endif
    error_code_ = player_character_.initialize();
    if (error_code_ != FT_ERR_SUCCESS)
    {
        (void)regeneration_world_rules_.destroy();
        world_.destroy();
		(void)transaction_journal_.destroy();
		(void)world_entities_.destroy();
		(void)world_items_.destroy();
		(void)recipe_catalog_.destroy();
		(void)item_catalog_.destroy();
        return error_code_;
    }
	/* Snapshot restore below replaces this default for existing worlds. */
	player_character_.set_hit_points(PLAYER_INITIAL_HIT_POINTS);
	{
		game_inventory &inventory = player_character_.get_inventory();

		inventory.resize(30U);
		if (inventory.get_error() == FT_ERR_SUCCESS)
			inventory.set_weight_limit(std::numeric_limits<int32_t>::max());
		error_code_ = inventory.get_error();
		if (error_code_ != FT_ERR_SUCCESS)
		{
			(void)regeneration_world_rules_.destroy();
			player_character_.destroy();
			world_.destroy();
			(void)transaction_journal_.destroy();
			(void)world_entities_.destroy();
			(void)world_items_.destroy();
			(void)recipe_catalog_.destroy();
			(void)item_catalog_.destroy();
			return (error_code_);
		}
	}
	{
		startup_restore_stage = "load-session-snapshot";
		const file_type save_type = file_get_type(world_item_save_path_);
		if (save_type == FILE_TYPE_REGULAR)
			error_code_ = GameSessionPersistence::
				load_file_with_revision_server_checkpoint(
				world_item_save_path_, player_character_, world_items_,
				recipe_catalog_, transaction_journal_, card_session_bytes,
				regeneration_rules_bytes, chunk_instability_bytes,
				revision_metadata_bytes, fluid_state_, entity_state_,
				crop_growth_policy_bytes, &restored_output_session_id,
				this->revision_server_checkpoint_state_);
		if (error_code_ == FT_ERR_SUCCESS
			&& restored_output_session_id != 0U)
		{
			if (regeneration_output_session_id_ != 0U
				&& regeneration_output_session_id_
					!= restored_output_session_id)
				error_code_ = FT_ERR_INVALID_STATE;
			else
				regeneration_output_session_id_ = restored_output_session_id;
		}
		else if (save_type != FILE_TYPE_REGULAR
			&& save_type != FILE_TYPE_MISSING)
			error_code_ = FT_ERR_INVALID_PATH;
	}
	if (error_code_ == FT_ERR_SUCCESS
		&& regeneration_output_session_id_ == 0U)
	{
		if (this->regeneration_host_output_prepare_ != nullptr)
			error_code_ = FT_ERR_CONFIGURATION;
		else
			regeneration_output_session_id_ = card_seed;
	}
	if (error_code_ == FT_ERR_SUCCESS && !crop_growth_policy_bytes.empty())
	{
		if (crop_growth_policy_bytes.size() >= 40U
			&& crop_growth_policy_bytes[0] == 'C'
			&& crop_growth_policy_bytes[1] == 'G'
			&& crop_growth_policy_bytes[2] == 'S'
			&& crop_growth_policy_bytes[3] == '1')
		{
			CropGrowthPolicy restored_policy = {};
			uint64_t restored_tick = 0U;
			error_code_ = game_session_deserialize_crop_growth_state(
				crop_growth_policy_bytes, restored_policy, restored_tick,
				crop_chunk_growth_deadlines_);
			if (error_code_ == FT_ERR_SUCCESS)
			{
				crop_growth_policy_ = restored_policy;
				world_.current_tick = restored_tick;
			}
		}
		else if (crop_growth_policy_bytes.size() != 16U
			&& crop_growth_policy_bytes.size() != 20U)
			error_code_ = FT_ERR_INVALID_ARGUMENT;
		else if (error_code_ == FT_ERR_SUCCESS)
		{
			CropGrowthPolicy restored_policy = {
				game_session_read_u32(crop_growth_policy_bytes, 0U),
				game_session_read_u32(crop_growth_policy_bytes, 4U),
			crop_growth_policy_bytes[8U], crop_growth_policy_bytes[9U],
			crop_growth_policy_bytes.size() == 20U
				? crop_growth_policy_bytes[10U] : static_cast<uint8_t>(4U)};
			if (restored_policy.tick_interval == 0U
				|| restored_policy.tick_interval > 120000U
				|| restored_policy.maturity_roll_divisor == 0U
				|| restored_policy.maturity_roll_divisor > 1000000U
				|| restored_policy.wheat_minimum_light_level > 15U
				|| restored_policy.paper_reed_minimum_light_level > 15U
				|| (crop_growth_policy_bytes.size() == 16U
					&& crop_growth_policy_bytes[10U] != 0U)
				|| restored_policy.hydration_radius > 8U)
				error_code_ = FT_ERR_INVALID_ARGUMENT;
			else
			{
				const std::size_t padding_start =
					crop_growth_policy_bytes.size() == 20U ? 11U : 10U;
				for (std::size_t index = padding_start;
					index < crop_growth_policy_bytes.size(); ++index)
					if (crop_growth_policy_bytes[index] != 0U)
						error_code_ = FT_ERR_INVALID_ARGUMENT;
			}
			if (error_code_ == FT_ERR_SUCCESS)
				crop_growth_policy_ = restored_policy;
		}
	}
	if (error_code_ == FT_ERR_SUCCESS && !fluid_state_.empty())
	{
		startup_restore_stage = "restore-fluid-state";
		error_code_ = world_.deserialize_fluid_state(fluid_state_.data(),
			fluid_state_.size());
	}
	if (error_code_ == FT_ERR_SUCCESS)
	{
		startup_restore_stage = "restore-entity-and-spawner-state";
		if (entity_state_.empty())
			error_code_ = GameSessionEntityState::serialize(world_entities_,
				world_spawners_, entity_state_);
		if (error_code_ == FT_ERR_SUCCESS && !entity_state_.empty())
			error_code_ = GameSessionEntityState::restore(entity_state_.data(),
				entity_state_.size(), world_entities_, world_spawners_);
	}
	if (error_code_ == FT_ERR_SUCCESS)
	{
		startup_restore_stage = "restore-revision-metadata";
		if (!revision_metadata_bytes.empty())
			error_code_ = world_.deserialize_revision_metadata(
				revision_metadata_bytes.data(), revision_metadata_bytes.size());
		else
		{
			const file_type revision_save_type =
				file_get_type(world_revision_save_path_);
			if (revision_save_type == FILE_TYPE_REGULAR)
				error_code_ = world_.load_revision_metadata(
					world_revision_save_path_);
			else if (revision_save_type != FILE_TYPE_MISSING)
				error_code_ = FT_ERR_INVALID_PATH;
		}
	}
	if (error_code_ == FT_ERR_SUCCESS && !chunk_instability_bytes.empty())
	{
		startup_restore_stage = "restore-chunk-instability";
		if (!world_.is_chunk_instability_configured())
			error_code_ = FT_ERR_CONFIGURATION;
		else
			error_code_ = world_.deserialize_chunk_instability(
				chunk_instability_bytes.data(), chunk_instability_bytes.size());
	}
	if (error_code_ == FT_ERR_SUCCESS)
	{
		startup_restore_stage = "restore-card-session";
		error_code_ = regeneration_cards_.set_action_session_id(
			regeneration_output_session_id_);
	}
	if (error_code_ == FT_ERR_SUCCESS)
	{
		startup_restore_stage = "restore-card-session";
		if (card_session_bytes.empty())
		{
			error_code_ = this->regeneration_cards_.initialize(4U, card_seed);
			std::size_t context_index = 0U;
			while (context_index < this->_regeneration_script_context.size())
			{
				if (error_code_ != FT_ERR_SUCCESS)
					break ;
				error_code_ = this->regeneration_cards_.register_script_context_key(
					this->_regeneration_script_context[context_index].first);
				++context_index;
			}
			if (error_code_ == FT_ERR_SUCCESS
				&& !this->regeneration_script_catalog_.empty())
				error_code_ = this->regeneration_cards_.load_script_catalog(
					this->regeneration_script_catalog_.data(),
					static_cast<uint32_t>(this->regeneration_script_catalog_.size()),
					this->trusted_script_manifest_hash_,
					this->regeneration_world_rules_policy_.generation_manifest_hash);
		}
		else
			error_code_ = this->regeneration_cards_.restore_snapshot(
				card_session_bytes.data(), card_session_bytes.size(),
				this->regeneration_script_catalog_.empty() ? nullptr
					: this->regeneration_script_catalog_.data(),
				static_cast<uint32_t>(this->regeneration_script_catalog_.size()),
				this->trusted_script_manifest_hash_,
				this->regeneration_world_rules_policy_.generation_manifest_hash);
		if (error_code_ == FT_ERR_SUCCESS && !card_session_bytes.empty())
		{
			std::vector<uint32_t> restored_context_keys;
			error_code_ = this->regeneration_cards_.get_script_context_keys(
				restored_context_keys);
			if (error_code_ == FT_ERR_SUCCESS
				&& restored_context_keys.size()
					!= this->_regeneration_script_context.size())
				error_code_ = FT_ERR_CONFIGURATION;
			std::size_t context_index = 0U;
			while (context_index < restored_context_keys.size()
				&& error_code_ == FT_ERR_SUCCESS)
			{
				if (restored_context_keys[context_index]
					!= this->_regeneration_script_context[context_index].first)
					error_code_ = FT_ERR_CONFIGURATION;
				++context_index;
			}
		}
		if (error_code_ == FT_ERR_SUCCESS && card_session_bytes.empty())
		{
			std::size_t context_index = 0U;
			while (context_index < this->_regeneration_script_context.size())
			{
				const auto &entry = this->_regeneration_script_context[context_index];
				if (this->_regeneration_script_context_is_set[context_index] == FT_TRUE)
					error_code_ = this->regeneration_cards_.set_script_context_value(
						entry.first, entry.second);
				else
					error_code_ = this->regeneration_cards_.clear_script_context_value(
						entry.first);
				if (error_code_ != FT_ERR_SUCCESS)
					break ;
				++context_index;
			}
		}
	}
	if (error_code_ == FT_ERR_SUCCESS)
	{
		startup_restore_stage = "synchronize-item-instance-allocator";
		error_code_ = synchronize_item_instance_id_allocator();
	}
	if (error_code_ == FT_ERR_SUCCESS)
	{
		startup_restore_stage = "migrate-equipped-item-identities";
		error_code_ = game_session_assign_equipped_instance_ids(
			this->player_character_, this->recipe_catalog_);
	}
	if (error_code_ == FT_ERR_SUCCESS)
	{
		startup_restore_stage = "validate-physical-item-identities";
		error_code_ = validate_physical_item_identity_uniqueness();
	}
	if (error_code_ == FT_ERR_SUCCESS && !regeneration_rules_bytes.empty())
	{
		startup_restore_stage = "restore-regeneration-rules";
		error_code_ = regeneration_world_rules_.deserialize(
			regeneration_rules_bytes.data(), regeneration_rules_bytes.size(),
			world_.voxel_generation_settings(),
			regeneration_world_rules_policy_);
	}
	if (error_code_ == FT_ERR_SUCCESS)
	{
		startup_restore_stage = "restore-committed-revision";
		error_code_ = world_.restore_committed_revision(
			regeneration_world_rules_);
	}
	if (error_code_ == FT_ERR_SUCCESS)
		world_loom_start_committed_ = world_.revision_barriers_active();
	if (error_code_ == FT_ERR_SUCCESS && world_loom_start_committed_
		&& revision_clock_provider_ == nullptr)
		error_code_ = FT_ERR_CONFIGURATION;
	if (error_code_ == FT_ERR_SUCCESS && world_loom_start_committed_)
		error_code_ = update_revision_occupancy();
	if (error_code_ == FT_ERR_SUCCESS)
	{
		startup_restore_stage = "recover-pending-transactions";
		error_code_ = recover_pending_transactions();
	}
	if (error_code_ == FT_ERR_SUCCESS)
		error_code_ = WorldItemPickupTransaction::next_request_id(
			transaction_journal_, &pickup_request_id_);
	if (error_code_ != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"[Session] startup restore failed stage=%s error=%d\n",
			startup_restore_stage, error_code_);
		(void)regeneration_world_rules_.destroy();
		if (regeneration_cards_.is_initialized())
			(void)regeneration_cards_.destroy();
		player_character_.destroy();
		world_.destroy();
		(void)transaction_journal_.destroy();
		(void)world_entities_.destroy();
		(void)world_spawners_.destroy();
		(void)world_items_.destroy();
		(void)recipe_catalog_.destroy();
		(void)item_catalog_.destroy();
		return (error_code_);
	}
    {
        int32_t analytics_error;

        analytics_error = RuntimeAnalytics::begin_world_session();
        if (analytics_error != FT_ERR_SUCCESS)
            std::fprintf(stderr, "Analytics: unable to open world report (%d)\n",
                analytics_error);
    }
    std::strncpy(seed_, seed.c_str(), sizeof(seed_) - 1);
    seed_[sizeof(seed_) - 1] = '\0';
    camera_.initialize();
    PlayerController::spawn_player_on_ground(&camera_, world_);
    player_spawn_x_ = camera_.x;
    player_spawn_z_ = camera_.z;
    sync_player_character_location();
    /* Only the minimum playable envelope is needed while loading. Streaming
     * the full configured distance here competes with the chunks that gate
     * startup and can make the loading screen appear hung. */
    active_render_distance_ = WorldCoordinates::MIN_RENDER_DISTANCE;
    boost_enabled_ = false;
    revision_preview_visible_ = false;
    render_debug_frame_ = 0U;
    cached_ram_mb_ = 0U;
    cached_vram_mb_ = 0U;
    cached_biome_valid_ = false;
    cached_biome_name_[0] = '\0';
    revision_preview_cache_valid_ = false;
    revision_preview_cache_.clear();
    selected_block_id_ = VOXEL_GENERATOR_DIRT_BLOCK;
    fps_accumulator_ = 0.0;
    fps_frame_count_ = 0U;
    rd_accumulator_ = 0.0;
    display_fps_ = 0.0;
    frame_ms_ = 0.0;
    performance_frame_ms_ = 16.67;
    error_code_ = FT_ERR_SUCCESS;
		processing_tick_accumulator_ = 0.0;
    active_ = true;
	if (this->regeneration_host_output_prepare_ != nullptr)
	{
		error_code_ = this->recover_regeneration_host_outputs();
		if (error_code_ != FT_ERR_SUCCESS)
		{
			this->stop();
			return (error_code_);
		}
	}
    if (window.is_gpu_mode() && window.get_gpu_window() && renderer.get_gpu_renderer() == nullptr)
    {
        error_code_ = renderer.initialize_gpu(window.get_gpu_window()->get_width(),
                                              window.get_gpu_window()->get_height());
        if (error_code_ != FT_ERR_SUCCESS)
        {
            stop();
            return error_code_;
        }
    }
    renderer.preload_assets();
    return FT_ERR_SUCCESS;
}

void GameSession::set_voxel_generation_config(const voxel_generation_config &config)
{
	this->world_.set_voxel_config(config);
}

void GameSession::stop()
{
	if (this->active_)
	{
		int32_t save_error = save_session_snapshot();
		if (save_error != FT_ERR_SUCCESS)
		{
			error_code_ = save_error;
			std::fprintf(stderr,
				"World item save failed path=%s error=%d\n",
				world_item_save_path_, save_error);
		}
		(void)regeneration_world_rules_.destroy();
		int32_t analytics_error = RuntimeAnalytics::end_world_session();
		if (analytics_error != FT_ERR_SUCCESS)
			std::fprintf(stderr, "Analytics: world report close failed (%d)\n",
				analytics_error);
		world_.destroy();
	}
	player_character_.destroy();
	(void)world_entities_.destroy();
	(void)world_spawners_.destroy();
	(void)world_items_.destroy();
	(void)transaction_journal_.destroy();
	if (regeneration_cards_.is_initialized())
		(void)regeneration_cards_.destroy();
	(void)recipe_catalog_.destroy();
	(void)item_catalog_.destroy();
	processing_tick_accumulator_ = 0.0;
	player_spawn_x_ = 0.0;
	player_spawn_z_ = 0.0;
	revision_occupancy_valid_ = false;
	active_ = false;
	world_loom_start_committed_ = false;
}

MinecraftItemCatalog &GameSession::item_catalog()
{
	return (item_catalog_);
}

const MinecraftRecipeCatalog &GameSession::recipe_catalog() const
{
	return (recipe_catalog_);
}

WorldItemStore &GameSession::world_items()
{
	return (world_items_);
}

const WorldEntityStore &GameSession::world_entities() const
{
	return (world_entities_);
}

WorldSpawnerStore &GameSession::world_spawners()
{
	return (world_spawners_);
}

GameTransactionJournal &GameSession::transaction_journal()
{
	return (transaction_journal_);
}

const RegenerationCardSession &GameSession::regeneration_cards() const
{
	return (regeneration_cards_);
}

const RegenerationWorldRules &GameSession::regeneration_world_rules() const
{
	return (regeneration_world_rules_);
}

int32_t GameSession::play_regeneration_card(uint64_t request_id,
	uint32_t card_instance_id, const RegenerationCardChoice &choice,
	RegenerationPolicyIntent *intent_out) noexcept
{
	std::vector<uint8_t> previous_state;
	uint32_t hand_count = 0U;
	uint32_t hand_index = 0U;
	uint32_t card_definition_id = 0U;
	uint32_t stabilizer_ordinal = 0U;
	uint32_t stabilizer_risk_basis_points = 0U;
	uint32_t card_recipe_id = 0U;
	uint32_t catalyst_options[7] = {};
	uint32_t catalyst_option_count = 0U;
	std::vector<World::RevisionChunkCoordinate> instability_targets;
	int32_t action_result;
	int32_t save_result;
	bool stabilizer_resolved = false;
	const bool cached_play =
		this->regeneration_cards_.has_cached_play_request(request_id);

	if (!this->active_ || intent_out == nullptr
		|| !this->world_loom_start_committed_
		|| !this->world_.world_revision().pending)
		return (FT_ERR_INVALID_OPERATION);
	*intent_out = {};
	if (this->world_.world_revision().regenerating)
		return (FT_ERR_INVALID_OPERATION);
	if (cached_play)
	{
		action_result = this->regeneration_cards_.get_cached_play_parameters(
			request_id, &stabilizer_risk_basis_points,
			&stabilizer_ordinal, catalyst_options,
			&catalyst_option_count);
		if (action_result != FT_ERR_SUCCESS)
			return (action_result);
	}
	if (!cached_play)
	{
		action_result = this->regeneration_cards_.get_hand_count(&hand_count);
		if (action_result != FT_ERR_SUCCESS)
			return (action_result);
		while (hand_index < hand_count)
		{
			card_game_deck_card card = {};

			action_result = this->regeneration_cards_.hand_inspect(
				hand_index, &card);
			if (action_result != FT_ERR_SUCCESS)
				return (action_result);
			if (card.instance_id == card_instance_id)
			{
				card_definition_id = card.card_id;
				break;
			}
			++hand_index;
		}
		if (card_definition_id == 0U)
			return (FT_ERR_NOT_FOUND);
		card_recipe_id = game_session_card_recipe_id(card_definition_id);
		if (card_recipe_id != 0U)
		{
			ft_bool recipe_known = FT_FALSE;

			action_result = this->recipe_catalog_.is_recipe_known(
				card_recipe_id, &recipe_known);
			if (action_result != FT_ERR_SUCCESS)
				return (action_result);
			if (recipe_known != FT_TRUE)
				return (FT_ERR_PERMISSION_DENIED);
		}
		if (card_definition_id
			== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID)
		{
			ft_bool volcano_blueprint_known = FT_FALSE;
			action_result = this->recipe_catalog_.is_recipe_known(
				MinecraftRecipeCatalog::VOLCANO_MARK_CARD_RECIPE_ID,
				&volcano_blueprint_known);
			if (action_result != FT_ERR_SUCCESS)
				return (action_result);
			if (volcano_blueprint_known != FT_TRUE)
				return (FT_ERR_PERMISSION_DENIED);
			const World::WorldRevision revision = this->world_.world_revision();
		if (!GameSession::authorize_volcano_mark_use(
				this->volcano_mark_use_authorizer_, card_instance_id,
				revision.identifier,
				this->volcano_mark_use_authorizer_user_data_))
				return (FT_ERR_PERMISSION_DENIED);
		}
		action_result = this->regeneration_world_rules_.preflight_card_effect(
			card_definition_id, choice);
		if (action_result != FT_ERR_SUCCESS)
			return (action_result);
		if (card_definition_id
			== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
		{
			uint32_t previous_uses = 0U;
			action_result = this->regeneration_world_rules_.
				get_volatile_stabilizer_use_count(&previous_uses);
			if (action_result != FT_ERR_SUCCESS)
				return (action_result);
			stabilizer_ordinal = previous_uses + 1U;
			const uint32_t tier = std::min(previous_uses,
				this->volatile_stabilizer_risk_tier_count_ - 1U);
			stabilizer_risk_basis_points =
				this->volatile_stabilizer_risk_basis_points_[tier];
			if (this->volatile_stabilizer_overload_surge_ <= 0
				|| this->volatile_stabilizer_success_reduction_ <= 0
				|| !this->world_.is_chunk_instability_configured())
				return (FT_ERR_CONFIGURATION);
			std::vector<World::RevisionChunkCoordinate> targets;
			action_result = this->world_.copy_selected_revision_chunks(targets);
			if (action_result != FT_ERR_SUCCESS)
				return (action_result);
			if (targets.empty())
				return (FT_ERR_NOT_FOUND);
			uint32_t overload_opportunity_site_count = 0U;
			uint32_t feature_reservation_upper_bound = 0U;
			action_result = this->world_.revision_feature_reservation_upper_bound(
				&feature_reservation_upper_bound);
			if (action_result != FT_ERR_SUCCESS)
				return (action_result);
			for (const auto &target : targets)
			{
				if (game_session_chunk_has_stabilizer_opportunity_site(
					this->world_, target.chunk_x, target.chunk_z))
					++overload_opportunity_site_count;
				WorldChunkInstabilityRecord record = {};
				action_result = this->world_.get_chunk_instability(
					target.chunk_x, target.chunk_z, &record);
				if (action_result != FT_ERR_SUCCESS)
					return (action_result);
				if (record.permanent_modifiers.size()
					>= WorldChunkInstabilityStore::MAXIMUM_ENTRIES_PER_RECORD
					|| record.record_revision > UINT32_MAX - 2U)
					return (FT_ERR_FULL);
			}
			if (overload_opportunity_site_count
				<= feature_reservation_upper_bound)
				return (FT_ERR_NOT_FOUND);
		}
		if (card_definition_id
			== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
		{
			uint32_t previous_uses = 0U;
			action_result = this->regeneration_world_rules_.
				get_unstable_catalyst_use_count(&previous_uses);
			if (action_result != FT_ERR_SUCCESS)
				return (action_result);
			if (this->unstable_catalyst_instability_delta_ <= 0
				|| !this->world_.is_chunk_instability_configured())
				return (FT_ERR_CONFIGURATION);
			std::vector<World::RevisionChunkCoordinate> targets;
			action_result = this->world_.copy_selected_revision_chunks(targets);
			if (action_result != FT_ERR_SUCCESS)
				return (action_result);
			if (targets.empty())
				return (FT_ERR_NOT_FOUND);
			bool has_eligible_target = false;
			bool has_shipwreck_target = false;
			bool has_volcano_target = false;
			for (const auto &target : targets)
			{
				WorldChunkInstabilityRecord record = {};
				action_result = this->world_.get_chunk_instability(
					target.chunk_x, target.chunk_z, &record);
				if (action_result != FT_ERR_SUCCESS)
					return (action_result);
				bool has_overlay = false;
				for (const auto &modifier : record.permanent_modifiers)
					if (modifier.id >= static_cast<uint32_t>(
							REGENERATION_MODIFIER_HOSTILE_CAMP)
						&& modifier.id <= static_cast<uint32_t>(
							REGENERATION_MODIFIER_RUINED_VILLAGE))
						has_overlay = true;
				if (has_overlay
					|| record.permanent_modifiers.size()
						>= WorldChunkInstabilityStore::MAXIMUM_ENTRIES_PER_RECORD
					|| record.record_revision > UINT32_MAX - 2U
					|| static_cast<uint64_t>(record.current_value)
						+ static_cast<uint32_t>(
							this->unstable_catalyst_instability_delta_)
						> this->chunk_instability_policy_.maximum_value)
					continue ;
				has_eligible_target = true;
				if (game_session_chunk_has_ocean_water(this->world_,
					target.chunk_x, target.chunk_z))
					has_shipwreck_target = true;
				if (game_session_chunk_has_volcanic_surface(this->world_,
					target.chunk_x, target.chunk_z))
					has_volcano_target = true;
			}
			if (!has_eligible_target)
				return (FT_ERR_FULL);
			catalyst_options[0] = 1U;
			catalyst_options[1] = 2U;
			catalyst_options[2] = 3U;
			catalyst_options[3] = 4U;
			catalyst_option_count = 4U;
			if (has_shipwreck_target)
				catalyst_options[catalyst_option_count++] = 5U;
			if (has_volcano_target)
				catalyst_options[catalyst_option_count++] = 6U;
			catalyst_options[catalyst_option_count++] = 7U;
			stabilizer_ordinal = previous_uses + 1U;
		}
		if (card_definition_id == 3U || card_definition_id == 4U
			|| card_definition_id
				== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID
			|| card_definition_id
				== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID
			|| card_definition_id
				== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID
			|| card_definition_id
				== RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID
			|| card_definition_id
				== RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID
			|| card_definition_id
				== RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID)
		{
			action_result = this->world_.copy_selected_revision_chunks(
				instability_targets);
			if (action_result != FT_ERR_SUCCESS)
				return (action_result);
			if (instability_targets.empty())
				return (FT_ERR_NOT_FOUND);
			if (card_definition_id
				== RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID
				|| card_definition_id
					== RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID
				|| card_definition_id
					== RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID)
			{
				bool feasible_site = false;
				for (const World::RevisionChunkCoordinate &target
					: instability_targets)
				{
					if (card_definition_id
						== RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID)
						feasible_site = game_session_chunk_has_desert_surface(
							this->world_, target.chunk_x, target.chunk_z);
					else if (card_definition_id
						== RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID)
						feasible_site = game_session_chunk_has_forest_surface(
							this->world_, target.chunk_x, target.chunk_z);
					else
						feasible_site = game_session_chunk_has_cave_site(
							this->world_, target.chunk_x, target.chunk_z);
					if (feasible_site)
						break ;
				}
				if (!feasible_site)
					return (FT_ERR_NOT_FOUND);
			}
			std::vector<RegenerationWorldFeaturePlan> selected_feature_plans;
			uint32_t pending_village_plans = 0U;
			uint32_t pending_magma_cards = 0U;
			uint32_t pending_volcano_plans = 0U;
			uint32_t pending_ruined_village_plans = 0U;
			uint32_t pending_shipwreck_plans = 0U;
			uint32_t pending_pyramid_plans = 0U;
			uint32_t pending_canopy_plans = 0U;
			uint32_t pending_spider_nest_plans = 0U;
			uint64_t reserved_single_feature_chunks = 0U;
			uint64_t reserved_volcano_plans = 0U;
			uint64_t reserved_shipwreck_plans = 0U;
			action_result = this->regeneration_world_rules_.copy_feature_plans(
				selected_feature_plans);
			if (action_result == FT_ERR_SUCCESS)
				action_result = this->regeneration_cards_.count_pending_card_definition(
					3U, &pending_village_plans);
			if (action_result == FT_ERR_SUCCESS)
				action_result = this->regeneration_cards_.count_pending_card_definition(
					4U, &pending_magma_cards);
			if (action_result == FT_ERR_SUCCESS)
				action_result = this->regeneration_cards_.count_pending_card_definition(
					RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID,
					&pending_volcano_plans);
			if (action_result == FT_ERR_SUCCESS)
				action_result = this->regeneration_cards_.count_pending_card_definition(
					RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID,
					&pending_ruined_village_plans);
			if (action_result == FT_ERR_SUCCESS)
				action_result = this->regeneration_cards_.count_pending_card_definition(
					RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID,
					&pending_shipwreck_plans);
			if (action_result == FT_ERR_SUCCESS)
				action_result = this->regeneration_cards_.count_pending_card_definition(
					RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID,
					&pending_pyramid_plans);
			if (action_result == FT_ERR_SUCCESS)
				action_result = this->regeneration_cards_.count_pending_card_definition(
					RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID,
					&pending_canopy_plans);
			if (action_result == FT_ERR_SUCCESS)
				action_result = this->regeneration_cards_.count_pending_card_definition(
					RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID,
					&pending_spider_nest_plans);
			if (action_result != FT_ERR_SUCCESS)
				return (action_result);
			for (const RegenerationWorldFeaturePlan &plan : selected_feature_plans)
			{
				if (plan.kind == REGENERATION_WORLD_FEATURE_VILLAGE)
					++reserved_single_feature_chunks;
				else if (plan.kind
					== REGENERATION_WORLD_FEATURE_SURFACE_MAGMA_POOL)
					++reserved_single_feature_chunks;
				else if (plan.kind == REGENERATION_WORLD_FEATURE_VOLCANO)
					++reserved_volcano_plans;
				else if (plan.kind == REGENERATION_WORLD_FEATURE_RUINED_VILLAGE)
					++reserved_single_feature_chunks;
				else if (plan.kind == REGENERATION_WORLD_FEATURE_SHIPWRECK)
				{
					++reserved_single_feature_chunks;
					++reserved_shipwreck_plans;
				}
				else if (plan.kind == REGENERATION_WORLD_FEATURE_DESERT_PYRAMID
					|| plan.kind == REGENERATION_WORLD_FEATURE_ANCIENT_CANOPY
					|| plan.kind == REGENERATION_WORLD_FEATURE_SPIDER_NEST)
					++reserved_single_feature_chunks;
			}
			reserved_single_feature_chunks += pending_village_plans;
			reserved_single_feature_chunks +=
				static_cast<uint64_t>(pending_magma_cards)
				* this->regeneration_world_rules_policy_.surface_magma_pools_per_card;
			reserved_volcano_plans += pending_volcano_plans;
			reserved_single_feature_chunks += pending_ruined_village_plans;
			reserved_single_feature_chunks += pending_shipwreck_plans;
			reserved_shipwreck_plans += pending_shipwreck_plans;
			reserved_single_feature_chunks += pending_pyramid_plans
				+ pending_canopy_plans + pending_spider_nest_plans;
			if (card_definition_id == 3U)
				++reserved_single_feature_chunks;
			else if (card_definition_id == 4U)
				reserved_single_feature_chunks +=
					this->regeneration_world_rules_policy_.surface_magma_pools_per_card;
			else if (card_definition_id
				== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID)
				++reserved_volcano_plans;
			else if (card_definition_id
				== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID)
				++reserved_single_feature_chunks;
			else if (card_definition_id
				== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID)
			{
				++reserved_single_feature_chunks;
				++reserved_shipwreck_plans;
			}
			else if (card_definition_id == RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID
				|| card_definition_id == RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID
				|| card_definition_id == RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID)
				++reserved_single_feature_chunks;
			else if (card_definition_id == RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID
				|| card_definition_id == RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID
				|| card_definition_id == RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID)
				++reserved_single_feature_chunks;
			const uint64_t volcano_footprint =
				static_cast<uint64_t>(this->volcano_mark_footprint_chunks_)
				* this->volcano_mark_footprint_chunks_;
			if (reserved_single_feature_chunks
				+ reserved_volcano_plans * volcano_footprint
				> instability_targets.size())
				return (FT_ERR_FULL);
			if (reserved_shipwreck_plans > 0U)
			{
				uint32_t ocean_chunks = 0U;
				for (const World::RevisionChunkCoordinate &target
					: instability_targets)
					if (game_session_chunk_has_ocean_water(this->world_,
						target.chunk_x, target.chunk_z))
						++ocean_chunks;
				if (ocean_chunks < reserved_shipwreck_plans)
					return (FT_ERR_NOT_FOUND);
			}
			if (reserved_volcano_plans > 0U)
			{
				std::vector<World::RevisionChunkCoordinate> paid_chunks;
				action_result = this->world_.copy_selected_paid_revision_chunks(
					paid_chunks);
				if (action_result != FT_ERR_SUCCESS)
					return (action_result);
				action_result = game_session_find_volcano_square(paid_chunks,
					instability_targets, reserved_single_feature_chunks,
					this->volcano_mark_footprint_chunks_);
				if (action_result != FT_ERR_SUCCESS)
					return (action_result);
			}
			instability_targets.clear();
		}
		if (card_definition_id
			== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
		{
			if (!this->world_.is_chunk_instability_configured())
				return (FT_ERR_CONFIGURATION);
			action_result = this->world_.copy_selected_revision_chunks(
				instability_targets);
			if (action_result != FT_ERR_SUCCESS || instability_targets.empty())
				return (action_result == FT_ERR_SUCCESS
					? FT_ERR_INVALID_OPERATION : action_result);
			for (const World::RevisionChunkCoordinate &target
				: instability_targets)
			{
				WorldChunkInstabilityRecord record = {};
				action_result = this->world_.get_chunk_instability(
					target.chunk_x, target.chunk_z, &record);
				if (action_result != FT_ERR_SUCCESS)
					return (action_result);
				if (static_cast<uint64_t>(record.current_value)
					+ static_cast<uint32_t>(this->volatile_stabilizer_overload_surge_)
					> this->chunk_instability_policy_.maximum_value)
					return (FT_ERR_OUT_OF_RANGE);
				if (record.permanent_modifiers.size()
					>= WorldChunkInstabilityStore::MAXIMUM_ENTRIES_PER_RECORD
					|| record.record_revision >= UINT32_MAX)
					return (FT_ERR_FULL);
			}
			instability_targets.clear();
		}
		if (card_definition_id == 7U)
		{
			const World::WorldRevision revision =
				this->world_.world_revision();
			action_result = this->world_.validate_revision_bonus_chunk(
				revision.selection_revision, 64U,
				game_session_decode_chunk_coordinate(choice.value_primary),
				game_session_decode_chunk_coordinate(choice.value_secondary));
			if (action_result != FT_ERR_SUCCESS)
				return (action_result);
		}
		if (card_definition_id == 11U
			|| card_definition_id
				== RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID
			|| card_definition_id
				== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID
			|| card_definition_id
				== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID
			|| card_definition_id
				== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID
			|| card_definition_id
				== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID)
		{
			const int32_t instability_delta = card_definition_id == 11U
				? this->ore_overgrowth_instability_delta_
				: (card_definition_id
					== RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID
					? this->ultimate_greed_instability_delta_
					: (card_definition_id
						== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID
						? this->biome_rupture_instability_delta_
						: (card_definition_id
							== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID
							? this->volcano_mark_instability_delta_
							: (card_definition_id
								== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID
								? this->ruined_village_instability_delta_
								: this->shipwreck_instability_delta_))));

			if (!this->world_.is_chunk_instability_configured()
				|| instability_delta <= 0)
				return (FT_ERR_CONFIGURATION);
			action_result = this->world_.copy_selected_revision_chunks(
				instability_targets);
			if (action_result != FT_ERR_SUCCESS)
				return (action_result);
			if (instability_targets.empty())
				return (FT_ERR_INVALID_OPERATION);
			for (const World::RevisionChunkCoordinate &target
				: instability_targets)
			{
				WorldChunkInstabilityRecord record = {};
				action_result = this->world_.get_chunk_instability(
					target.chunk_x, target.chunk_z, &record);
				if (action_result != FT_ERR_SUCCESS)
					return (action_result);
				if (static_cast<uint64_t>(record.current_value)
					+ static_cast<uint32_t>(
						instability_delta)
					> this->chunk_instability_policy_.maximum_value)
					return (FT_ERR_OUT_OF_RANGE);
			}
		}
	}
	action_result = this->regeneration_cards_.serialize_snapshot(previous_state);
	if (action_result != FT_ERR_SUCCESS)
		return (action_result);
	action_result = this->regeneration_cards_.play_card(request_id,
		card_instance_id, choice, this->world_.voxel_generation_settings(),
		intent_out, stabilizer_risk_basis_points, stabilizer_ordinal,
		catalyst_option_count == 0U ? nullptr : catalyst_options,
		catalyst_option_count);
	if (action_result == FT_ERR_SUCCESS && !cached_play
		&& card_definition_id
			== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
	{
		bool replayed = false;
		action_result = this->apply_pending_regeneration_card_intent(
			&replayed);
		stabilizer_resolved = action_result == FT_ERR_SUCCESS;
	}
	if (action_result == FT_ERR_SUCCESS && !cached_play
		&& card_definition_id
			== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
	{
		bool replayed = false;
		action_result = this->apply_pending_regeneration_card_intent(
			&replayed);
	}
	if (action_result == FT_ERR_SUCCESS && !cached_play
		&& card_definition_id
			== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID)
	{
		bool primary_enabled = false;
		bool secondary_enabled = false;
		int32_t primary_weight = 0;
		int32_t secondary_weight = 0;
		uint32_t no_effect_mask = 0U;

		action_result = this->regeneration_world_rules_.get_biome_state(
			choice.value_primary, &primary_enabled, &primary_weight);
		if (action_result == FT_ERR_SUCCESS)
			action_result = this->regeneration_world_rules_.get_biome_state(
				choice.value_secondary, &secondary_enabled, &secondary_weight);
		if (action_result == FT_ERR_SUCCESS)
		{
			if (!primary_enabled)
				no_effect_mask |= 1U;
			if (!secondary_enabled)
				no_effect_mask |= 2U;
			action_result = this->regeneration_cards_.
				set_cached_play_no_effect_choice_mask(request_id,
					no_effect_mask);
		}
		if (action_result == FT_ERR_SUCCESS)
			intent_out->no_effect_choice_mask = no_effect_mask;
	}
	if (action_result != FT_ERR_SUCCESS)
	{
		if (this->regeneration_cards_.restore_snapshot(previous_state.data(),
				previous_state.size()) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
		*intent_out = {};
		return (action_result);
	}
	save_result = stabilizer_resolved
		? FT_ERR_SUCCESS : this->save_session_snapshot();
	if (save_result != FT_ERR_SUCCESS)
	{
		if (this->regeneration_cards_.restore_snapshot(previous_state.data(),
				previous_state.size()) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
		*intent_out = {};
		return (save_result);
	}
	return (action_result);
}

int32_t GameSession::query_regeneration_card_effect_status(
	uint32_t card_instance_id, const RegenerationCardChoice &choice,
	uint32_t *card_definition_id_out, int32_t *status_out,
	uint32_t *no_effect_choice_mask_out) const noexcept
{
	return (this->query_regeneration_card_effect_status(card_instance_id,
		choice, card_definition_id_out, status_out,
		no_effect_choice_mask_out, nullptr));
}

int32_t GameSession::query_regeneration_card_effect_status(
	uint32_t card_instance_id, const RegenerationCardChoice &choice,
	uint32_t *card_definition_id_out, int32_t *status_out,
	uint32_t *no_effect_choice_mask_out,
	const char **reason_out) const noexcept
{
	uint32_t hand_count = 0U;
	uint32_t hand_index = 0U;
	uint32_t definition_id = 0U;
	uint32_t card_recipe_id = 0U;
	bool biome_enabled = false;
	bool eligible_site = false;
	bool recipe_unknown = false;
	int32_t biome_weight = 0;
	int32_t maximum_instability_delta = 0;
	std::vector<World::RevisionChunkCoordinate> selected_chunks;
	std::vector<RegenerationWorldFeaturePlan> feature_plans;
	int32_t result;

	if (reason_out != nullptr)
		*reason_out = "Status unavailable";
	if (card_definition_id_out == nullptr || status_out == nullptr
		|| no_effect_choice_mask_out == nullptr || card_instance_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	*card_definition_id_out = 0U;
	*status_out = FT_ERR_NOT_FOUND;
	*no_effect_choice_mask_out = 0U;
	if (!this->active_ || !this->world_loom_start_committed_
		|| !this->world_.world_revision().pending
		|| this->world_.world_revision().regenerating)
	{
		*status_out = FT_ERR_INVALID_OPERATION;
		if (reason_out != nullptr)
			*reason_out = "The regeneration decision window is not active";
		return (FT_ERR_SUCCESS);
	}
	result = this->regeneration_cards_.get_hand_count(&hand_count);
	if (result != FT_ERR_SUCCESS)
		return (result);
	while (hand_index < hand_count)
	{
		card_game_deck_card card = {};

		result = this->regeneration_cards_.hand_inspect(hand_index, &card);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (card.instance_id == card_instance_id)
		{
			definition_id = card.card_id;
			break;
		}
		++hand_index;
	}
	if (definition_id == 0U)
	{
		if (reason_out != nullptr)
			*reason_out = "This physical card instance is not in the hand";
		return (FT_ERR_SUCCESS);
	}
	*card_definition_id_out = definition_id;
	*status_out = this->regeneration_world_rules_.preflight_card_effect(
		definition_id, choice);
	if (*status_out == FT_ERR_SUCCESS)
	{
		card_recipe_id = game_session_card_recipe_id(definition_id);
		if (card_recipe_id != 0U)
		{
			ft_bool recipe_known = FT_FALSE;

			result = this->recipe_catalog_.is_recipe_known(card_recipe_id,
				&recipe_known);
			if (result != FT_ERR_SUCCESS)
				return (result);
			if (recipe_known != FT_TRUE)
			{
				recipe_unknown = true;
				*status_out = FT_ERR_PERMISSION_DENIED;
			}
		}
	}
	if (*status_out == FT_ERR_SUCCESS && definition_id == 7U)
	{
		const World::WorldRevision revision = this->world_.world_revision();
		*status_out = this->world_.validate_revision_bonus_chunk(
			revision.selection_revision, 64U,
			game_session_decode_chunk_coordinate(choice.value_primary),
			game_session_decode_chunk_coordinate(choice.value_secondary));
	}
	if (*status_out == FT_ERR_SUCCESS
		&& (definition_id == 1U
			|| definition_id
				== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID))
	{
		result = this->regeneration_world_rules_.get_biome_state(
			choice.value_primary, &biome_enabled, &biome_weight);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (!biome_enabled)
			*no_effect_choice_mask_out |= 1U;
		if (definition_id
			== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID)
		{
			result = this->regeneration_world_rules_.get_biome_state(
				choice.value_secondary, &biome_enabled, &biome_weight);
			if (result != FT_ERR_SUCCESS)
				return (result);
			if (!biome_enabled)
				*no_effect_choice_mask_out |= 2U;
		}
	}
	if (*status_out == FT_ERR_SUCCESS
		&& definition_id
			== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID)
	{
		ft_bool blueprint_known = FT_FALSE;

		result = this->recipe_catalog_.is_recipe_known(
			MinecraftRecipeCatalog::VOLCANO_MARK_CARD_RECIPE_ID,
			&blueprint_known);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (blueprint_known != FT_TRUE)
			*status_out = FT_ERR_PERMISSION_DENIED;
		else if (!GameSession::authorize_volcano_mark_use(
				this->volcano_mark_use_authorizer_, card_instance_id,
				this->world_.world_revision().identifier,
				this->volcano_mark_use_authorizer_user_data_))
			*status_out = FT_ERR_PERMISSION_DENIED;
	}
	if (*status_out == FT_ERR_SUCCESS)
	{
		if (definition_id == 11U)
			maximum_instability_delta = this->ore_overgrowth_instability_delta_;
		else if (definition_id
			== RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID)
			maximum_instability_delta = this->ultimate_greed_instability_delta_;
		else if (definition_id
			== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID)
			maximum_instability_delta = this->biome_rupture_instability_delta_;
		else if (definition_id
			== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID)
			maximum_instability_delta = this->volcano_mark_instability_delta_;
		else if (definition_id
			== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
			maximum_instability_delta = this->volatile_stabilizer_overload_surge_;
		else if (definition_id
			== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID)
			maximum_instability_delta = this->ruined_village_instability_delta_;
		else if (definition_id
			== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID)
			maximum_instability_delta = this->shipwreck_instability_delta_;
		else if (definition_id
			== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
			maximum_instability_delta = this->unstable_catalyst_instability_delta_;
		else if (definition_id
			== RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID)
			maximum_instability_delta = this->pyramid_instability_delta_;
		else if (definition_id
			== RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID)
			maximum_instability_delta = this->canopy_instability_delta_;
		else if (definition_id
			== RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID)
			maximum_instability_delta = this->spider_nest_instability_delta_;
		if (maximum_instability_delta != 0)
		{
			if (!this->world_.is_chunk_instability_configured())
				*status_out = FT_ERR_CONFIGURATION;
			else
				result = this->world_.copy_selected_revision_chunks(
					selected_chunks);
			if (*status_out == FT_ERR_SUCCESS && result != FT_ERR_SUCCESS)
				*status_out = result;
			if (*status_out == FT_ERR_SUCCESS && selected_chunks.empty())
				*status_out = FT_ERR_NOT_FOUND;
		}
	}
	if (*status_out == FT_ERR_SUCCESS && maximum_instability_delta > 0
		&& definition_id
			!= RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
	{
		for (const World::RevisionChunkCoordinate &target : selected_chunks)
		{
			WorldChunkInstabilityRecord record = {};
			result = this->world_.get_chunk_instability(target.chunk_x,
				target.chunk_z, &record);
			if (result != FT_ERR_SUCCESS)
			{
				*status_out = result;
				break ;
			}
			if (record.current_value
				> this->chunk_instability_policy_.maximum_value)
			{
				*status_out = FT_ERR_CONFIGURATION;
				break ;
			}
			if (static_cast<uint64_t>(record.current_value)
				+ static_cast<uint32_t>(maximum_instability_delta)
				> this->chunk_instability_policy_.maximum_value)
			{
				*status_out = FT_ERR_OUT_OF_RANGE;
				break ;
			}
			if (definition_id
				== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID
				&& (record.permanent_modifiers.size()
					>= WorldChunkInstabilityStore::MAXIMUM_ENTRIES_PER_RECORD
					|| record.record_revision > UINT32_MAX - 2U))
			{
				*status_out = FT_ERR_FULL;
				break ;
			}
		}
	}
	if (*status_out == FT_ERR_SUCCESS
		&& definition_id
			== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
	{
		eligible_site = false;
		for (const World::RevisionChunkCoordinate &target : selected_chunks)
		{
			WorldChunkInstabilityRecord record = {};
			result = this->world_.get_chunk_instability(target.chunk_x,
				target.chunk_z, &record);
			if (result != FT_ERR_SUCCESS)
			{
				*status_out = result;
				break ;
			}
			bool has_overlay = false;
			for (const auto &modifier : record.permanent_modifiers)
			{
				if (modifier.id >= static_cast<uint32_t>(
						REGENERATION_MODIFIER_HOSTILE_CAMP)
					&& modifier.id <= static_cast<uint32_t>(
						REGENERATION_MODIFIER_RUINED_VILLAGE))
					has_overlay = true;
			}
			if (!has_overlay
				&& record.permanent_modifiers.size()
					< WorldChunkInstabilityStore::MAXIMUM_ENTRIES_PER_RECORD
				&& record.record_revision <= UINT32_MAX - 2U
				&& static_cast<uint64_t>(record.current_value)
					+ static_cast<uint32_t>(maximum_instability_delta)
					<= this->chunk_instability_policy_.maximum_value)
			{
				eligible_site = true;
				break ;
			}
		}
		if (*status_out == FT_ERR_SUCCESS && !eligible_site)
			*status_out = FT_ERR_FULL;
	}
	if (*status_out == FT_ERR_SUCCESS
		&& (definition_id
			== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID
			|| definition_id
				== RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID
			|| definition_id
				== RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID
			|| definition_id
				== RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID))
	{
		for (const World::RevisionChunkCoordinate &target : selected_chunks)
		{
			if ((definition_id
					== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID
				&& game_session_chunk_has_ocean_water(this->world_,
					target.chunk_x, target.chunk_z))
				|| (definition_id
					== RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID
				&& game_session_chunk_has_desert_surface(this->world_,
					target.chunk_x, target.chunk_z))
				|| (definition_id
					== RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID
				&& game_session_chunk_has_forest_surface(this->world_,
					target.chunk_x, target.chunk_z))
				|| (definition_id
					== RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID
				&& game_session_chunk_has_cave_site(this->world_,
					target.chunk_x, target.chunk_z)))
			{
				eligible_site = true;
				break ;
			}
		}
		if (!eligible_site)
			*status_out = FT_ERR_NOT_FOUND;
	}
	if (*status_out == FT_ERR_SUCCESS
		&& (definition_id == 3U || definition_id == 4U
			|| definition_id
				== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID
			|| definition_id
				== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID
			|| definition_id
				== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID
			|| definition_id
				== RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID
			|| definition_id
				== RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID
			|| definition_id
				== RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID))
	{
		uint32_t pending_count = 0U;
		uint64_t reserved_single_chunks = 0U;
		uint64_t reserved_volcano_plans = 0U;
		uint64_t reserved_shipwreck_plans = 0U;
		uint64_t reserved_pyramid_plans = 0U;
		uint64_t reserved_canopy_plans = 0U;
		uint64_t reserved_spider_nest_plans = 0U;
		uint64_t volcano_footprint = static_cast<uint64_t>(
			this->volcano_mark_footprint_chunks_)
			* this->volcano_mark_footprint_chunks_;

		result = this->world_.copy_selected_revision_chunks(selected_chunks);
		if (result == FT_ERR_SUCCESS)
			result = this->regeneration_world_rules_.copy_feature_plans(
				feature_plans);
		for (const RegenerationWorldFeaturePlan &plan : feature_plans)
		{
			if (plan.kind == REGENERATION_WORLD_FEATURE_VOLCANO)
				++reserved_volcano_plans;
			else if (plan.kind == REGENERATION_WORLD_FEATURE_VILLAGE
				|| plan.kind == REGENERATION_WORLD_FEATURE_SURFACE_MAGMA_POOL
				|| plan.kind == REGENERATION_WORLD_FEATURE_RUINED_VILLAGE
				|| plan.kind == REGENERATION_WORLD_FEATURE_SHIPWRECK
				|| plan.kind == REGENERATION_WORLD_FEATURE_DESERT_PYRAMID
				|| plan.kind == REGENERATION_WORLD_FEATURE_ANCIENT_CANOPY
				|| plan.kind == REGENERATION_WORLD_FEATURE_SPIDER_NEST)
				++reserved_single_chunks;
			if (plan.kind == REGENERATION_WORLD_FEATURE_SHIPWRECK)
				++reserved_shipwreck_plans;
			else if (plan.kind == REGENERATION_WORLD_FEATURE_DESERT_PYRAMID)
				++reserved_pyramid_plans;
			else if (plan.kind == REGENERATION_WORLD_FEATURE_ANCIENT_CANOPY)
				++reserved_canopy_plans;
			else if (plan.kind == REGENERATION_WORLD_FEATURE_SPIDER_NEST)
				++reserved_spider_nest_plans;
		}
		if (result == FT_ERR_SUCCESS)
			result = this->regeneration_cards_.count_pending_card_definition(
				3U, &pending_count);
		if (result == FT_ERR_SUCCESS)
			reserved_single_chunks += pending_count;
		if (result == FT_ERR_SUCCESS)
			result = this->regeneration_cards_.count_pending_card_definition(
				4U, &pending_count);
		if (result == FT_ERR_SUCCESS)
			reserved_single_chunks += static_cast<uint64_t>(pending_count)
				* this->regeneration_world_rules_policy_
					.surface_magma_pools_per_card;
		const uint32_t feature_card_ids[] = {
			RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID,
			RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID,
			RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID,
			RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID,
			RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID,
			RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID};
		for (uint32_t card_id : feature_card_ids)
		{
			if (result != FT_ERR_SUCCESS)
				break ;
			result = this->regeneration_cards_.count_pending_card_definition(
				card_id, &pending_count);
			if (result != FT_ERR_SUCCESS)
				break ;
			if (card_id == RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID)
				reserved_volcano_plans += pending_count;
			else
			{
				reserved_single_chunks += pending_count;
				if (card_id
					== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID)
					reserved_shipwreck_plans += pending_count;
				else if (card_id
					== RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID)
					reserved_pyramid_plans += pending_count;
				else if (card_id
					== RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID)
					reserved_canopy_plans += pending_count;
				else if (card_id
					== RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID)
					reserved_spider_nest_plans += pending_count;
			}
		}
		if (result == FT_ERR_SUCCESS)
		{
			if (definition_id == 3U)
				++reserved_single_chunks;
			else if (definition_id == 4U)
				reserved_single_chunks += this->regeneration_world_rules_policy_
					.surface_magma_pools_per_card;
			else if (definition_id
				== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID)
				++reserved_volcano_plans;
			else
			{
				++reserved_single_chunks;
				if (definition_id
					== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID)
					++reserved_shipwreck_plans;
				else if (definition_id
					== RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID)
					++reserved_pyramid_plans;
				else if (definition_id
					== RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID)
					++reserved_canopy_plans;
				else if (definition_id
					== RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID)
					++reserved_spider_nest_plans;
			}
			if (reserved_single_chunks
				+ reserved_volcano_plans * volcano_footprint
				> selected_chunks.size())
				*status_out = FT_ERR_FULL;
		}
		if (*status_out == FT_ERR_SUCCESS && result == FT_ERR_SUCCESS
			&& reserved_shipwreck_plans != 0U)
		{
			uint32_t ocean_count = 0U;
			for (const World::RevisionChunkCoordinate &target : selected_chunks)
				if (game_session_chunk_has_ocean_water(this->world_,
					target.chunk_x, target.chunk_z))
					++ocean_count;
			if (ocean_count < reserved_shipwreck_plans)
				*status_out = FT_ERR_NOT_FOUND;
		}
		if (*status_out == FT_ERR_SUCCESS && result == FT_ERR_SUCCESS
			&& (reserved_shipwreck_plans != 0U
				|| reserved_pyramid_plans != 0U
				|| reserved_canopy_plans != 0U
				|| reserved_spider_nest_plans != 0U))
		{
			std::vector<uint8_t> job_types;
			std::vector<uint8_t> site_masks;
			std::vector<uint8_t> visited_chunks;
			std::vector<int32_t> chunk_jobs;
			const uint64_t job_count = reserved_shipwreck_plans
				+ reserved_pyramid_plans + reserved_canopy_plans
				+ reserved_spider_nest_plans;

			if (job_count > selected_chunks.size())
				*status_out = FT_ERR_NOT_FOUND;
			else
			{
				try
				{
					job_types.reserve(static_cast<std::size_t>(job_count));
					job_types.insert(job_types.end(),
						static_cast<std::size_t>(reserved_shipwreck_plans), 1U);
					job_types.insert(job_types.end(),
						static_cast<std::size_t>(reserved_pyramid_plans), 2U);
					job_types.insert(job_types.end(),
						static_cast<std::size_t>(reserved_canopy_plans), 4U);
					job_types.insert(job_types.end(),
						static_cast<std::size_t>(reserved_spider_nest_plans), 8U);
					site_masks.reserve(selected_chunks.size());
					for (const World::RevisionChunkCoordinate &target
						: selected_chunks)
					{
						uint8_t mask = 0U;
						if (game_session_chunk_has_ocean_water(this->world_,
							target.chunk_x, target.chunk_z))
							mask |= 1U;
						if (game_session_chunk_has_desert_surface(this->world_,
							target.chunk_x, target.chunk_z))
							mask |= 2U;
						if (game_session_chunk_has_forest_surface(this->world_,
							target.chunk_x, target.chunk_z))
							mask |= 4U;
						if (game_session_chunk_has_cave_site(this->world_,
							target.chunk_x, target.chunk_z))
							mask |= 8U;
						site_masks.push_back(mask);
					}
					chunk_jobs.assign(selected_chunks.size(), -1);
					visited_chunks.assign(selected_chunks.size(), 0U);
				}
				catch (const std::bad_alloc &)
				{
					result = FT_ERR_NO_MEMORY;
				}
				uint32_t job_index = 0U;
				while (result == FT_ERR_SUCCESS
					&& *status_out == FT_ERR_SUCCESS
					&& job_index < job_types.size())
				{
					std::fill(visited_chunks.begin(), visited_chunks.end(), 0U);
					if (!game_session_match_site_job(job_index, job_types,
						site_masks, chunk_jobs, visited_chunks))
						*status_out = FT_ERR_NOT_FOUND;
					++job_index;
				}
				if (*status_out == FT_ERR_SUCCESS
					&& result != FT_ERR_SUCCESS)
					*status_out = result;
			}
		}
		if (*status_out == FT_ERR_SUCCESS && result != FT_ERR_SUCCESS)
			*status_out = result;
		if (*status_out == FT_ERR_SUCCESS && reserved_volcano_plans != 0U)
		{
			std::vector<World::RevisionChunkCoordinate> paid_chunks;
			result = this->world_.copy_selected_paid_revision_chunks(paid_chunks);
			if (result == FT_ERR_SUCCESS)
				result = game_session_find_volcano_square(paid_chunks,
					selected_chunks, reserved_single_chunks,
					this->volcano_mark_footprint_chunks_);
			if (result != FT_ERR_SUCCESS)
				*status_out = result;
		}
	}
	if (reason_out != nullptr)
	{
		if (*status_out == FT_ERR_SUCCESS && *no_effect_choice_mask_out != 0U)
			*reason_out = "This choice has no effect because its biome is already excluded";
		else if (*status_out == FT_ERR_SUCCESS)
			*reason_out = "Ready";
		else if (*status_out == FT_ERR_INVALID_OPERATION)
			*reason_out = "The regeneration decision window is not active";
		else if (*status_out == FT_ERR_PERMISSION_DENIED)
			*reason_out = recipe_unknown
				? "This card's recipe blueprint has not been discovered"
				: definition_id
				== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID
				? "Volcano Mark requires its discovered blueprint and host approval"
				: "The host has not authorized this effect";
		else if (*status_out == FT_ERR_CONFIGURATION)
			*reason_out = "Required world policy is not configured";
		else if (*status_out == FT_ERR_OUT_OF_RANGE)
			*reason_out = "The effect exceeds the configured instability limit";
		else if (*status_out == FT_ERR_FULL)
		{
			if (definition_id
				== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
				*reason_out = "No selected chunk can accept another permanent modifier";
			else if (definition_id == 3U || definition_id == 4U
				|| definition_id
					== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID
				|| definition_id
					== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID
				|| definition_id
					== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID
				|| definition_id
					== RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID
				|| definition_id
					== RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID
				|| definition_id
					== RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID)
				*reason_out = "The selected chunk set has no remaining feature capacity";
			else
				*reason_out = "The selected chunk cannot accept this instability change";
		}
		else if (*status_out == FT_ERR_NOT_FOUND
			&& (definition_id
				== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID
			|| definition_id
				== RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID
			|| definition_id
				== RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID
			|| definition_id
				== RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID))
			*reason_out = "No selected chunk has a suitable site for this structure";
		else
			*reason_out = "The proposed choice is not currently legal";
	}
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::query_regeneration_card_effect_choices(
	uint32_t card_instance_id,
	const std::vector<RegenerationTargetChunk> &chunk_candidates,
	std::vector<RegenerationCardChoice> &choices_out) const noexcept
{
	std::vector<RegenerationCardChoice> staged;
	std::vector<RegenerationTargetChunk> eligible_candidates;
	uint32_t hand_count = 0U;
	uint32_t hand_index = 0U;
	uint32_t definition_id = 0U;
	uint32_t card_recipe_id = 0U;
	int32_t result;

	if (card_instance_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->active_ || !this->world_loom_start_committed_
		|| !this->world_.world_revision().pending
		|| this->world_.world_revision().regenerating)
		return (FT_ERR_INVALID_OPERATION);
	result = this->regeneration_cards_.get_hand_count(&hand_count);
	if (result != FT_ERR_SUCCESS)
		return (result);
	while (hand_index < hand_count)
	{
		card_game_deck_card card = {};

		result = this->regeneration_cards_.hand_inspect(hand_index, &card);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (card.instance_id == card_instance_id)
		{
			definition_id = card.card_id;
			break;
		}
		++hand_index;
	}
	if (definition_id == 0U)
		return (FT_ERR_NOT_FOUND);
	if (definition_id == 7U)
	{
		const World::WorldRevision revision = this->world_.world_revision();
		std::size_t candidate_index = 0U;

		try
		{
			while (candidate_index < chunk_candidates.size())
			{
				const RegenerationTargetChunk &candidate =
					chunk_candidates[candidate_index];
				const int32_t chunk_x = game_session_decode_chunk_coordinate(
					static_cast<uint32_t>(candidate.chunk_x));
				const int32_t chunk_z = game_session_decode_chunk_coordinate(
					static_cast<uint32_t>(candidate.chunk_z));
				bool duplicate = false;
				std::size_t prior_index = 0U;

				while (prior_index < eligible_candidates.size())
				{
					if (eligible_candidates[prior_index].chunk_x == chunk_x
						&& eligible_candidates[prior_index].chunk_z == chunk_z)
					{
						duplicate = true;
						break;
					}
					++prior_index;
				}
				if (!duplicate
					&& this->world_.validate_revision_bonus_chunk(
						revision.selection_revision, 64U, chunk_x, chunk_z)
						== FT_ERR_SUCCESS)
				{
					eligible_candidates.push_back(
						{chunk_x, chunk_z});
				}
				++candidate_index;
			}
		}
		catch (const std::bad_alloc &)
		{
			return (FT_ERR_NO_MEMORY);
		}
	}
	result = this->regeneration_world_rules_.query_legal_card_choices(
		definition_id, eligible_candidates, staged);
	if (result != FT_ERR_SUCCESS)
		return (result);
	card_recipe_id = game_session_card_recipe_id(definition_id);
	if (card_recipe_id != 0U && !staged.empty())
	{
		ft_bool recipe_known = FT_FALSE;

		result = this->recipe_catalog_.is_recipe_known(card_recipe_id,
			&recipe_known);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (recipe_known != FT_TRUE)
			staged.clear();
	}
	if (definition_id
		== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID
		&& !staged.empty())
	{
		ft_bool blueprint_known = FT_FALSE;

		result = this->recipe_catalog_.is_recipe_known(
			MinecraftRecipeCatalog::VOLCANO_MARK_CARD_RECIPE_ID,
			&blueprint_known);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (blueprint_known != FT_TRUE
			|| !GameSession::authorize_volcano_mark_use(
				this->volcano_mark_use_authorizer_, card_instance_id,
				this->world_.world_revision().identifier,
				this->volcano_mark_use_authorizer_user_data_))
			staged.clear();
	}
	choices_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::draw_regeneration_cards(uint64_t request_id,
	uint64_t source_host_intent_sequence, uint32_t draw_count,
	uint32_t required_discard_count,
	RegenerationCardActionResult *result_out) noexcept
{
	std::vector<uint8_t> previous_state;
	int32_t action_result;
	int32_t save_result;

	if (!this->active_ || result_out == nullptr
		|| !this->world_loom_start_committed_
		|| !this->world_.world_revision().pending)
		return (FT_ERR_INVALID_OPERATION);
	*result_out = {};
	if (this->world_.world_revision().regenerating)
		return (FT_ERR_INVALID_OPERATION);
	action_result = this->regeneration_cards_.serialize_snapshot(previous_state);
	if (action_result != FT_ERR_SUCCESS)
		return (action_result);
	action_result = this->regeneration_cards_.draw_cards(request_id,
		source_host_intent_sequence, draw_count, required_discard_count,
		result_out);
	save_result = this->save_session_snapshot();
	if (save_result != FT_ERR_SUCCESS)
	{
		if (this->regeneration_cards_.restore_snapshot(previous_state.data(),
				previous_state.size()) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
		*result_out = {};
		return (save_result);
	}
	return (action_result);
}

int32_t GameSession::discard_regeneration_hand_card(uint64_t request_id,
	uint64_t source_draw_request_id, uint32_t card_instance_id,
	RegenerationCardActionResult *result_out) noexcept
{
	std::vector<uint8_t> previous_state;
	int32_t action_result;
	int32_t save_result;

	if (!this->active_ || result_out == nullptr
		|| !this->world_loom_start_committed_
		|| !this->world_.world_revision().pending)
		return (FT_ERR_INVALID_OPERATION);
	*result_out = {};
	if (this->world_.world_revision().regenerating)
		return (FT_ERR_INVALID_OPERATION);
	action_result = this->regeneration_cards_.serialize_snapshot(previous_state);
	if (action_result != FT_ERR_SUCCESS)
		return (action_result);
	action_result = this->regeneration_cards_.discard_hand_card(request_id,
		source_draw_request_id, card_instance_id, result_out);
	save_result = this->save_session_snapshot();
	if (save_result != FT_ERR_SUCCESS)
	{
		if (this->regeneration_cards_.restore_snapshot(previous_state.data(),
				previous_state.size()) != FT_ERR_SUCCESS)
			return (FT_ERR_INVALID_STATE);
		*result_out = {};
		return (save_result);
	}
	return (action_result);
}

int32_t GameSession::import_regeneration_card_item_to_deck(
	int32_t inventory_slot, uint64_t expected_inventory_revision,
	uint64_t request_id, card_game_deck_card *card_out) noexcept
{
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	if (!this->active_ || card_out == nullptr
		|| this->world_.world_revision().pending
		|| this->world_.world_revision().regenerating)
		return (FT_ERR_INVALID_OPERATION);
	*card_out = {};
	const GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};
	return (WorldItemCardTransferTransaction::import_to_deck(
		this->world_item_save_path_, this->player_character_, this->world_items_,
		this->recipe_catalog_, this->regeneration_cards_,
		this->transaction_journal_, inventory_slot,
		expected_inventory_revision, request_id, card_out, &snapshot_writer));
}

int32_t GameSession::export_regeneration_card_to_inventory(
	uint32_t card_instance_id, uint64_t expected_inventory_revision,
	uint64_t request_id,
	RegenerationCardTransferResult *result_out) noexcept
{
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	if (!this->active_ || result_out == nullptr
		|| this->world_.world_revision().pending
		|| this->world_.world_revision().regenerating)
		return (FT_ERR_INVALID_OPERATION);
	*result_out = {};
	const GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};
	return (WorldItemCardTransferTransaction::export_to_inventory(
		this->world_item_save_path_, this->player_character_, this->world_items_,
		this->recipe_catalog_, this->regeneration_cards_,
		this->transaction_journal_, card_instance_id,
		expected_inventory_revision, request_id, result_out, &snapshot_writer));
}

int32_t GameSession::initialize_regeneration_world_rules() noexcept
{
	const voxel_generation_config &base_config =
		this->world_.voxel_generation_settings();

	this->regeneration_world_rules_policy_ = {};
	this->regeneration_world_rules_policy_.surface_magma_pools_per_card = 1U;
	this->regeneration_world_rules_policy_.maximum_village_plans = 4U;
	this->regeneration_world_rules_policy_.maximum_surface_magma_pools = 4U;
	this->regeneration_world_rules_policy_.maximum_volcano_plans = 1U;
	this->regeneration_world_rules_policy_.maximum_ruined_village_plans = 2U;
	this->regeneration_world_rules_policy_.maximum_shipwreck_plans = 2U;
	this->regeneration_world_rules_policy_.maximum_pyramid_plans = 2U;
	this->regeneration_world_rules_policy_.maximum_canopy_plans = 2U;
	this->regeneration_world_rules_policy_.maximum_spider_nest_plans = 2U;
	this->regeneration_world_rules_policy_.maximum_unstable_catalyst_uses = 64U;
	this->regeneration_world_rules_policy_.volcano_footprint_chunks =
		this->volcano_mark_footprint_chunks_;
	this->regeneration_world_rules_policy_.maximum_added_target_chunks = 2U;
	this->regeneration_world_rules_policy_.maximum_ore_scale_numerator = 8U;
	this->regeneration_world_rules_policy_.maximum_ore_scale_denominator = 1U;
	this->regeneration_world_rules_policy_.ore_overgrowth_instability_delta =
		this->ore_overgrowth_instability_delta_;
	this->regeneration_world_rules_policy_.ultimate_greed_instability_delta =
		this->ultimate_greed_instability_delta_;
	this->regeneration_world_rules_policy_.biome_rupture_instability_delta =
		this->biome_rupture_instability_delta_;
	this->regeneration_world_rules_policy_.volcano_mark_instability_delta =
		this->volcano_mark_instability_delta_;
	this->regeneration_world_rules_policy_.ruined_village_instability_delta =
		this->ruined_village_instability_delta_;
	this->regeneration_world_rules_policy_.shipwreck_instability_delta =
		this->shipwreck_instability_delta_;
	this->regeneration_world_rules_policy_.pyramid_instability_delta =
		this->pyramid_instability_delta_;
	this->regeneration_world_rules_policy_.canopy_instability_delta =
		this->canopy_instability_delta_;
	this->regeneration_world_rules_policy_.spider_nest_instability_delta =
		this->spider_nest_instability_delta_;
	this->regeneration_world_rules_policy_.unstable_catalyst_instability_delta =
		this->unstable_catalyst_instability_delta_;
	this->regeneration_world_rules_policy_.empty_draw_instability_delta =
		this->empty_draw_instability_delta_;
	this->regeneration_world_rules_policy_.hand_overflow_instability_delta =
		this->hand_overflow_instability_delta_;
	this->regeneration_world_rules_policy_.volatile_stabilizer_risk_tier_count =
		this->volatile_stabilizer_risk_tier_count_;
	std::copy(this->volatile_stabilizer_risk_basis_points_,
		this->volatile_stabilizer_risk_basis_points_
			+ this->volatile_stabilizer_risk_tier_count_,
		this->regeneration_world_rules_policy_.volatile_stabilizer_overload_basis_points);
	this->regeneration_world_rules_policy_.volatile_stabilizer_success_reduction =
		this->volatile_stabilizer_success_reduction_;
	this->regeneration_world_rules_policy_.volatile_stabilizer_overload_surge =
		this->volatile_stabilizer_overload_surge_;
	this->regeneration_world_rules_policy_.maximum_biome_weight_adjustment = 10;
	for (uint32_t index = 0U; index < base_config.biome_count; ++index)
		this->regeneration_world_rules_policy_.biome_compass_adjustments[index]
			= 1;
	this->regeneration_world_rules_policy_.generation_manifest_hash =
		RegenerationWorldRules::calculate_manifest_hash(base_config,
			this->regeneration_world_rules_policy_);
	if (this->regeneration_world_rules_policy_.generation_manifest_hash == 0U)
		return (FT_ERR_CONFIGURATION);
	return (this->regeneration_world_rules_.initialize(base_config,
		this->regeneration_world_rules_policy_));
}

int32_t GameSession::save_session_snapshot() noexcept
{
	std::vector<uint8_t> card_session_bytes;
	std::vector<uint8_t> regeneration_rules_bytes;
	std::vector<uint8_t> chunk_instability_bytes;
	std::vector<uint8_t> revision_metadata_bytes;
	std::vector<uint8_t> crop_growth_policy_bytes;
	int32_t result = game_session_serialize_crop_growth_state(
		crop_growth_policy_, world_.current_tick,
		crop_chunk_growth_deadlines_, crop_growth_policy_bytes);
	if (result == FT_ERR_SUCCESS)
		result = this->regeneration_cards_.serialize_snapshot(
		card_session_bytes);
	if (result == FT_ERR_SUCCESS)
		result = this->validate_physical_item_identity_uniqueness();
	if (result == FT_ERR_SUCCESS)
		result = this->regeneration_world_rules_.serialize(
			regeneration_rules_bytes);
	if (result == FT_ERR_SUCCESS && world_.is_chunk_instability_configured())
		result = world_.serialize_chunk_instability(chunk_instability_bytes);
	if (result == FT_ERR_SUCCESS)
		result = world_.serialize_revision_metadata(revision_metadata_bytes);
	if (result == FT_ERR_SUCCESS)
		result = world_.serialize_fluid_state(fluid_state_);
	if (result == FT_ERR_SUCCESS)
		result = GameSessionEntityState::serialize(world_entities_,
			world_spawners_, entity_state_);
	if (result == FT_ERR_SUCCESS)
		result = GameSessionPersistence::
			save_file_with_revision_server_checkpoint(
			this->world_item_save_path_,
			this->player_character_, this->world_items_, this->recipe_catalog_,
			this->transaction_journal_, card_session_bytes,
			regeneration_rules_bytes, chunk_instability_bytes,
			revision_metadata_bytes, fluid_state_, entity_state_,
			crop_growth_policy_bytes,
			this->regeneration_output_session_id_,
			this->revision_server_checkpoint_state_);
	return (result);
}

int32_t GameSession::save_snapshot_callback(void *context) noexcept
{
	if (context == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (static_cast<GameSession *>(context)->save_session_snapshot());
}

int32_t GameSession::revision_start_commit_callback(uint64_t actor_id,
	uint64_t loom_id, uint64_t request_id,
	uint64_t expected_selection_revision,
	uint64_t expected_policy_revision, uint32_t opening_card_count,
	void *user_data) noexcept
{
	if (user_data == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (static_cast<GameSession *>(user_data)->commit_regeneration_start(
		actor_id, loom_id, request_id, expected_selection_revision,
		expected_policy_revision, opening_card_count));
}

int32_t GameSession::revision_occupancy_provider_callback(
	std::vector<World::RevisionChunkCoordinate> &chunks_out,
	void *user_data)
{
	GameSession *session;

	if (user_data == nullptr)
		return (FT_ERR_INVALID_POINTER);
	session = static_cast<GameSession *>(user_data);
	if (session->revision_occupancy_provider_ == nullptr)
		return (FT_ERR_PERMISSION_DENIED);
	return (session->revision_occupancy_provider_(chunks_out,
		session->revision_occupancy_provider_user_data_));
}

int32_t GameSession::bind_world_revision_server(
	WorldRevisionServer &server) noexcept
{
	if (!this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (this->revision_occupancy_provider_ == nullptr)
		return (FT_ERR_PERMISSION_DENIED);
	int32_t result = server.set_occupancy_provider(
		&GameSession::revision_occupancy_provider_callback, this);
	if (result == FT_ERR_SUCCESS)
		result = server.set_start_committer(
			&GameSession::revision_start_commit_callback, this);
	if (result == FT_ERR_SUCCESS
		&& !this->revision_server_checkpoint_state_.empty())
		result = server.restore_checkpoint(
			this->revision_server_checkpoint_state_.data(),
			this->revision_server_checkpoint_state_.size());
	if (result == FT_ERR_SUCCESS)
		result = server.set_checkpoint_committer(
			&GameSession::revision_checkpoint_committer_callback, this);
	return (result);
}

int32_t GameSession::revision_checkpoint_committer_callback(
	const uint8_t *bytes, std::size_t byte_count, void *user_data) noexcept
{
	if (user_data == nullptr || bytes == nullptr || byte_count == 0U
		|| byte_count > 64U * 1024U * 1024U)
		return (FT_ERR_INVALID_ARGUMENT);
	GameSession *session = static_cast<GameSession *>(user_data);
	std::vector<uint8_t> staged;
	try
	{
		staged.assign(bytes, bytes + byte_count);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	session->revision_server_checkpoint_state_.swap(staged);
	/* Keep the latest in-memory server checkpoint even when the atomic session
	 * replacement fails. The server has already advanced; reverting only these
	 * bytes would let a later session save pair newer World state with stale
	 * replay state. An exact request retry reserializes and retries this save. */
	return (session->save_session_snapshot());
}

int32_t GameSession::commit_regeneration_start(uint64_t actor_id,
	uint64_t loom_id, uint64_t request_id,
	uint64_t expected_selection_revision,
	uint64_t expected_policy_revision, uint32_t opening_card_count) noexcept
{
	std::vector<uint8_t> payload;
	std::vector<uint8_t> card_state_before;
	std::vector<uint8_t> journal_state_before;
	game_inventory_snapshot inventory_before = {};
	game_inventory_quantity_result currency_result = {};
	RegenerationCardActionResult opening_deal_result = {};
	GameTransactionJournal::Record existing = {};
	WorldRevisionCostQuote quote = {};
	uint64_t journal_sequence = 0U;
	uint64_t inventory_revision = 0U;
	uint64_t deadline_epoch_ms = 0U;
	bool replayed = false;
	int32_t result;

	if (!this->active_ || actor_id == 0U || loom_id == 0U
		|| request_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->revision_clock_provider_ == nullptr)
		return (FT_ERR_INVALID_STATE);
	result = this->transaction_journal_.find(WORLD_LOOM_START_SCOPE,
		request_id, &existing);
	if (result == FT_ERR_SUCCESS)
	{
		if (existing.transaction_type != WORLD_LOOM_START_TRANSACTION_TYPE
			|| existing.payload.size() != 56U
			|| existing.payload[0] != 'W' || existing.payload[1] != 'L'
			|| existing.payload[2] != 'S' || existing.payload[3] != 'T'
			|| game_session_read_u32(existing.payload, 4U) != 1U
			|| game_session_read_u64(existing.payload, 8U) != actor_id
			|| game_session_read_u64(existing.payload, 16U) != loom_id
			|| game_session_read_u64(existing.payload, 24U)
				!= expected_selection_revision
			|| game_session_read_u64(existing.payload, 32U)
				!= expected_policy_revision
			|| game_session_read_u32(existing.payload, 40U)
				!= opening_card_count)
			return (FT_ERR_INVALID_ARGUMENT);
		if (existing.state == GameTransactionJournal::ABORTED)
			return (FT_ERR_INVALID_OPERATION);
		if (existing.state == GameTransactionJournal::COMMITTED)
		{
			const World::WorldRevision committed_revision =
				this->world_.world_revision();
			if (committed_revision.pending
				&& !committed_revision.regenerating
				&& this->world_.revision_decision_deadline_epoch() == 0U)
				return (FT_ERR_INVALID_STATE);
			if (committed_revision.pending
				&& !this->world_.revision_barriers_active())
			{
				result = this->world_.set_revision_barriers_active(true);
				if (result != FT_ERR_SUCCESS)
					return (result);
			}
			if (committed_revision.pending && !committed_revision.regenerating
				&& !this->world_.revision_barriers_active())
				return (FT_ERR_INVALID_STATE);
			/* The WorldRevisionServer owns the request replay record for this
			 * callback. Its checkpoint callback persists that record and this
			 * committed session state together after this callback returns. */
			this->world_loom_start_committed_ = committed_revision.pending
				&& !committed_revision.regenerating;
			return (FT_ERR_SUCCESS);
		}
		if (existing.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_STATE);
	}
	else if (result != FT_ERR_NOT_FOUND)
		return (result);
	result = this->update_revision_occupancy();
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->revision_clock_provider_(&deadline_epoch_ms,
		this->revision_clock_provider_user_data_);
	if (result != FT_ERR_SUCCESS || deadline_epoch_ms == 0U)
		return (result == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE : result);
	if (deadline_epoch_ms > std::numeric_limits<uint64_t>::max() - 300000U)
		return (FT_ERR_OUT_OF_RANGE);
	deadline_epoch_ms += 300000U;
	World::WorldRevision revision = this->world_.world_revision();
	if (!revision.pending || revision.regenerating
		|| this->world_.revision_barriers_active()
		|| this->world_.revision_decision_deadline_epoch() != 0U
		|| revision.selection_revision != expected_selection_revision
		|| revision.cost_policy_revision != expected_policy_revision
		|| revision.selected_count == 0U)
		return (FT_ERR_INVALID_STATE);
	result = this->world_.query_revision_start_cost(
		expected_selection_revision, expected_policy_revision,
		opening_card_count, &quote);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (quote.total_currency_cost
		> static_cast<uint64_t>(std::numeric_limits<int32_t>::max())
		|| quote.currency_item_id > static_cast<uint32_t>(
			std::numeric_limits<int32_t>::max()))
		return (FT_ERR_OUT_OF_RANGE);
	try
	{
		payload.reserve(48U);
		payload.push_back('W');
		payload.push_back('L');
		payload.push_back('S');
		payload.push_back('T');
		game_session_append_u32(payload, 1U);
		game_session_append_u64(payload, actor_id);
		game_session_append_u64(payload, loom_id);
		game_session_append_u64(payload, expected_selection_revision);
		game_session_append_u64(payload, expected_policy_revision);
		game_session_append_u32(payload, opening_card_count);
		game_session_append_u32(payload, quote.currency_item_id);
		game_session_append_u64(payload, quote.total_currency_cost);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (existing.sequence != 0U && existing.payload != payload)
		return (FT_ERR_INVALID_ARGUMENT);
	result = this->player_character_.get_inventory().get_snapshot(
		inventory_before);
	if (result == FT_ERR_SUCCESS)
		result = this->player_character_.get_inventory().get_revision(
			&inventory_revision);
	if (result == FT_ERR_SUCCESS)
		result = this->regeneration_cards_.serialize_snapshot(
			card_state_before);
	if (result == FT_ERR_SUCCESS)
		result = this->transaction_journal_.serialize(journal_state_before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (existing.sequence == 0U)
	{
		result = this->transaction_journal_.prepare(WORLD_LOOM_START_SCOPE,
			request_id, WORLD_LOOM_START_TRANSACTION_TYPE, payload.data(),
			static_cast<uint32_t>(payload.size()), &journal_sequence,
			&replayed);
		if (result != FT_ERR_SUCCESS)
			return (result);
		result = this->save_session_snapshot();
		if (result != FT_ERR_SUCCESS)
		{
			(void)this->transaction_journal_.mark_aborted(
				WORLD_LOOM_START_SCOPE, request_id);
			(void)this->save_session_snapshot();
			return (result);
		}
	}
	(void)journal_sequence;
	(void)replayed;
	if (quote.total_currency_cost != 0U)
	{
		result = this->player_character_.get_inventory().remove_item_quantity(
			static_cast<int32_t>(quote.currency_item_id),
			static_cast<int32_t>(quote.total_currency_cost), inventory_revision,
			WORLD_LOOM_START_SCOPE, request_id, &currency_result);
	}
	else
		result = FT_ERR_SUCCESS;
	if (result == FT_ERR_SUCCESS)
		result = this->regeneration_cards_.deal_starting_hand(request_id,
			opening_card_count, &opening_deal_result);
	if (result != FT_ERR_SUCCESS)
	{
		int32_t rollback_result = this->player_character_.get_inventory()
			.restore_snapshot(inventory_before);
		const int32_t destroy_card_result = this->regeneration_cards_.destroy();
		int32_t restore_card_result = destroy_card_result;
		if (destroy_card_result == FT_ERR_SUCCESS)
			restore_card_result = this->regeneration_cards_.restore_snapshot(
				card_state_before.data(), card_state_before.size());
		if (rollback_result == FT_ERR_SUCCESS
			&& restore_card_result != FT_ERR_SUCCESS)
			rollback_result = restore_card_result;
		const int32_t abort_result = this->transaction_journal_.mark_aborted(
			WORLD_LOOM_START_SCOPE, request_id);
		if (rollback_result == FT_ERR_SUCCESS && abort_result != FT_ERR_SUCCESS)
			rollback_result = abort_result;
		const int32_t save_result = this->save_session_snapshot();
		if (rollback_result == FT_ERR_SUCCESS && save_result != FT_ERR_SUCCESS)
			rollback_result = save_result;
		return (rollback_result == FT_ERR_SUCCESS ? result : rollback_result);
	}
	result = this->world_.set_revision_barriers_active(true);
	if (result == FT_ERR_SUCCESS)
		result = this->world_.set_revision_decision_deadline_epoch(
			deadline_epoch_ms);
	if (result == FT_ERR_SUCCESS)
		result = this->transaction_journal_.mark_committed(
			WORLD_LOOM_START_SCOPE, request_id);
	if (result == FT_ERR_SUCCESS)
		this->world_loom_start_committed_ = true;
	else
	{
		const int32_t operation_result = result;
		int32_t rollback_result = this->player_character_.get_inventory()
			.restore_snapshot(inventory_before);
		const int32_t destroy_card_result = this->regeneration_cards_.destroy();
		int32_t restore_card_result = destroy_card_result;
		if (destroy_card_result == FT_ERR_SUCCESS)
			restore_card_result = this->regeneration_cards_.restore_snapshot(
				card_state_before.data(), card_state_before.size());
		if (rollback_result == FT_ERR_SUCCESS
			&& restore_card_result != FT_ERR_SUCCESS)
			rollback_result = restore_card_result;
		const int32_t barrier_result =
			this->world_.set_revision_barriers_active(false);
		if (rollback_result == FT_ERR_SUCCESS
			&& barrier_result != FT_ERR_SUCCESS)
			rollback_result = barrier_result;
		const int32_t abort_result = this->transaction_journal_.mark_aborted(
			WORLD_LOOM_START_SCOPE, request_id);
		if (rollback_result == FT_ERR_SUCCESS && abort_result != FT_ERR_SUCCESS)
			rollback_result = abort_result;
		const int32_t save_result = this->save_session_snapshot();
		if (rollback_result == FT_ERR_SUCCESS && save_result != FT_ERR_SUCCESS)
			rollback_result = save_result;
		return (rollback_result == FT_ERR_SUCCESS
			? operation_result : rollback_result);
	}
	return (result);
}

int32_t GameSession::finalize_regeneration_start(
	uint64_t authoritative_epoch) noexcept
{
	World::WorldRevision revision;
	uint32_t resolved_count = 0U;
	uint32_t required_discard_count = 0U;
	uint32_t pending_choice_count = 0U;
	uint64_t host_epoch_ms = 0U;
	bool replayed = false;
	int32_t result;

	if (!this->active_ || !this->world_loom_start_committed_)
		return (FT_ERR_INVALID_STATE);
	if (this->revision_clock_provider_ == nullptr)
		return (FT_ERR_INVALID_STATE);
	result = this->revision_clock_provider_(&host_epoch_ms,
		this->revision_clock_provider_user_data_);
	if (result != FT_ERR_SUCCESS || host_epoch_ms == 0U)
		return (result == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE : result);
	authoritative_epoch = host_epoch_ms;
	revision = this->world_.world_revision();
	if (revision.regenerating)
		return (this->save_session_snapshot());
	if (!revision.pending)
		return (FT_ERR_INVALID_STATE);
	const uint64_t deadline_epoch_ms =
		this->world_.revision_decision_deadline_epoch();
	if (deadline_epoch_ms != 0U && authoritative_epoch < deadline_epoch_ms)
		return (FT_ERR_INVALID_OPERATION);
	/* This ID is reserved for the one deadline-expiry command belonging to
	 * the active World Loom session. RCS1 persists its retry result. */
	result = this->resolve_expired_regeneration_choices_internal(UINT64_MAX,
		authoritative_epoch, &resolved_count);
	if (result == FT_ERR_SUCCESS && resolved_count != 0U)
		result = this->save_session_snapshot();
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->regeneration_cards_.get_pending_choice_count(
		&pending_choice_count);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (pending_choice_count != 0U)
		return (FT_ERR_INVALID_OPERATION);
	for (;;)
	{
		card_game_host_intent_record pending_notice = {};
		bool is_draw_notice = false;
		result = this->regeneration_cards_.peek_pending_intent(
			&pending_notice);
		if (result == FT_ERR_SUCCESS)
		{
			result = this->regeneration_cards_.is_draw_notice(pending_notice,
				&is_draw_notice);
			if (result != FT_ERR_SUCCESS)
				return (result);
			if (is_draw_notice)
			{
				result = this->consume_pending_regeneration_draw_notice(
					&pending_notice);
				if (result != FT_ERR_SUCCESS)
					return (result);
				continue ;
			}
		}
		else if (result != FT_ERR_NOT_FOUND)
			return (result);
		result = this->apply_pending_regeneration_card_intent(&replayed);
		if (result == FT_ERR_NOT_FOUND)
			break ;
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	result = this->regeneration_cards_.get_pending_required_discard_count(
		&required_discard_count);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (required_discard_count != 0U)
	{
		std::vector<uint8_t> previous_card_state;
		uint32_t resolved_discards = 0U;
		const uint64_t request_namespace = deadline_epoch_ms == 0U
			? 1U : deadline_epoch_ms;
		result = this->regeneration_cards_.serialize_snapshot(
			previous_card_state);
		if (result == FT_ERR_SUCCESS)
			result = this->regeneration_cards_
				.resolve_pending_required_discards_to_default(
					request_namespace, &resolved_discards);
		if (result == FT_ERR_SUCCESS && resolved_discards != 0U)
			result = this->save_session_snapshot();
		if (result != FT_ERR_SUCCESS && !previous_card_state.empty())
		{
			const int32_t restore_result =
				this->regeneration_cards_.restore_snapshot(
					previous_card_state.data(), previous_card_state.size());
			if (restore_result != FT_ERR_SUCCESS)
				return (restore_result);
		}
		if (result != FT_ERR_SUCCESS)
			return (result);
		result = this->regeneration_cards_.get_pending_required_discard_count(
			&required_discard_count);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (required_discard_count != 0U)
			return (FT_ERR_INVALID_STATE);
	}
	result = this->world_.start_revision_regeneration();
	if (result == FT_ERR_SUCCESS)
		result = this->save_session_snapshot();
	return (result);
}

int32_t GameSession::retry_failed_regeneration() noexcept
{
	if (!this->active_ || !this->world_.revision_barriers_active())
		return (FT_ERR_INVALID_OPERATION);
	const World::WorldRevision revision = this->world_.world_revision();
	if (revision.pending || revision.regenerating)
		return (FT_ERR_INVALID_OPERATION);
	int32_t result = this->world_.retry_failed_revision_regeneration();
	if (result == FT_ERR_SUCCESS)
		result = this->save_session_snapshot();
	return (result);
}

int32_t GameSession::apply_pending_regeneration_card_intent(
	bool *replayed_out) noexcept
{
	std::vector<uint8_t> previous_card_state;
	std::vector<uint8_t> previous_rules_state;
	std::vector<uint8_t> previous_instability_state;
	std::vector<World::RevisionChunkCoordinate> instability_targets;
	RegenerationPolicyIntent intent = {};
	World::WorldRevision revision;
	bool replayed = false;
	bool instability_restore_needed = false;
	int32_t instability_delta = 0;
	int32_t result;
	int32_t rollback_result;

	if (replayed_out == nullptr || !this->active_
		|| !this->regeneration_cards_.is_initialized()
		|| !this->regeneration_world_rules_.is_initialized())
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->world_loom_start_committed_
		|| !this->world_.world_revision().pending)
		return (FT_ERR_INVALID_OPERATION);
	*replayed_out = false;
	result = this->regeneration_cards_.peek_pending_card_intent(&intent);
	revision = this->world_.world_revision();
	if (result == FT_ERR_SUCCESS && revision.regenerating)
		result = FT_ERR_INVALID_OPERATION;
	if (result == FT_ERR_SUCCESS
		&& intent.card_definition_id == 7U && !revision.pending)
		result = FT_ERR_INVALID_OPERATION;
	if (result == FT_ERR_SUCCESS)
		result = this->regeneration_cards_.serialize_snapshot(previous_card_state);
	if (result == FT_ERR_SUCCESS)
		result = this->regeneration_world_rules_.serialize(previous_rules_state);
	if (result == FT_ERR_SUCCESS
		&& (intent.card_definition_id == 11U
			|| intent.card_definition_id
				== RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID))
	{
		if (intent.card_definition_id
			== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID)
			instability_delta = intent.private_result != 0U
				? this->volatile_stabilizer_overload_surge_
				: -this->volatile_stabilizer_success_reduction_;
		else if (intent.card_definition_id == 11U)
			instability_delta = this->ore_overgrowth_instability_delta_;
		else if (intent.card_definition_id
			== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
			instability_delta = this->unstable_catalyst_instability_delta_;
		else if (intent.card_definition_id
			== RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID)
			instability_delta = this->ultimate_greed_instability_delta_;
		else if (intent.card_definition_id
			== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID)
			instability_delta = this->biome_rupture_instability_delta_;
		else if (intent.card_definition_id
			== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID)
			instability_delta = this->volcano_mark_instability_delta_;
		else if (intent.card_definition_id
			== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID)
			instability_delta = this->ruined_village_instability_delta_;
		else if (intent.card_definition_id
			== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID)
			instability_delta = this->shipwreck_instability_delta_;
		else if (intent.card_definition_id
			== RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID)
			instability_delta = this->pyramid_instability_delta_;
		else if (intent.card_definition_id
			== RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID)
			instability_delta = this->canopy_instability_delta_;
		else
			instability_delta = this->spider_nest_instability_delta_;
		if (!this->world_.is_chunk_instability_configured()
			|| instability_delta == 0)
			result = FT_ERR_CONFIGURATION;
		else
			result = this->world_.serialize_chunk_instability(
				previous_instability_state);
		if (result == FT_ERR_SUCCESS)
			result = this->world_.copy_selected_revision_chunks(
				instability_targets);
		if (result == FT_ERR_SUCCESS && instability_targets.empty())
			result = FT_ERR_INVALID_OPERATION;
		if (result == FT_ERR_SUCCESS
			&& intent.card_definition_id
				== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
		{
			const uint32_t modifier_id =
				game_session_catalyst_modifier_id(intent.private_result);
			std::size_t target_index = 0U;
			bool target_found = false;
			for (; target_index < instability_targets.size(); ++target_index)
			{
				const auto &candidate = instability_targets[target_index];
				if (modifier_id
					== static_cast<uint32_t>(
						REGENERATION_MODIFIER_HOSTILE_SHIPWRECK)
					&& !game_session_chunk_has_ocean_water(this->world_,
						candidate.chunk_x, candidate.chunk_z))
					continue ;
				if (modifier_id
					== static_cast<uint32_t>(
						REGENERATION_MODIFIER_VOLCANIC_OUTPOST)
					&& !game_session_chunk_has_volcanic_surface(this->world_,
						candidate.chunk_x, candidate.chunk_z))
					continue ;
				WorldChunkInstabilityRecord record = {};
				result = this->world_.get_chunk_instability(
					candidate.chunk_x, candidate.chunk_z, &record);
				if (result != FT_ERR_SUCCESS)
					break ;
				bool has_overlay = false;
				for (const auto &modifier : record.permanent_modifiers)
					if (modifier.id >= static_cast<uint32_t>(
							REGENERATION_MODIFIER_HOSTILE_CAMP)
						&& modifier.id <= static_cast<uint32_t>(
							REGENERATION_MODIFIER_RUINED_VILLAGE))
						has_overlay = true;
				if (has_overlay
					|| record.permanent_modifiers.size()
						>= WorldChunkInstabilityStore::MAXIMUM_ENTRIES_PER_RECORD
					|| record.record_revision > UINT32_MAX - 2U
					|| static_cast<uint64_t>(record.current_value)
						+ static_cast<uint32_t>(
							this->unstable_catalyst_instability_delta_)
						> this->chunk_instability_policy_.maximum_value)
					continue ;
				target_found = true;
				break ;
			}
			if (result == FT_ERR_SUCCESS && !target_found)
				result = FT_ERR_NOT_FOUND;
			if (result == FT_ERR_SUCCESS && target_index != 0U)
				std::swap(instability_targets[0],
					instability_targets[target_index]);
			if (result == FT_ERR_SUCCESS)
				instability_targets.resize(1U);
		}
		for (const World::RevisionChunkCoordinate &target
			: instability_targets)
		{
			WorldChunkInstabilityRecord record = {};
			if (result == FT_ERR_SUCCESS)
				result = this->world_.get_chunk_instability(target.chunk_x,
					target.chunk_z, &record);
			if (result == FT_ERR_SUCCESS && instability_delta > 0
				&& static_cast<uint64_t>(record.current_value)
					+ static_cast<uint32_t>(instability_delta)
					> this->chunk_instability_policy_.maximum_value)
				result = FT_ERR_OUT_OF_RANGE;
			if (result != FT_ERR_SUCCESS)
				break;
		}
	}
	if (result == FT_ERR_SUCCESS)
		result = this->regeneration_world_rules_.consume_pending_card_intent(
			this->regeneration_cards_, &replayed);
	if (result == FT_ERR_SUCCESS && revision.pending)
		result = this->world_.update_pending_revision_rules(
			this->regeneration_world_rules_);
	if (result == FT_ERR_SUCCESS && intent.card_definition_id
		== RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID
		&& !replayed)
	{
		RegenerationCardActionResult draw_result = {};
		result = this->regeneration_cards_.draw_cards(intent.request_id,
			intent.host_intent_sequence, 3U, 0U, &draw_result);
	}
	if (result == FT_ERR_SUCCESS && intent.card_definition_id
		== RegenerationCardSession::FORTUNES_DRAW_CARD_DEFINITION_ID
		&& !replayed)
	{
		RegenerationCardActionResult draw_result = {};
		result = this->regeneration_cards_.draw_cards(intent.request_id,
			intent.host_intent_sequence, 1U, 0U, &draw_result);
	}
	if (result == FT_ERR_SUCCESS
		&& (intent.card_definition_id == 11U
			|| intent.card_definition_id
				== RegenerationCardSession::ULTIMATE_GREED_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::BIOME_RUPTURE_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::RUINED_VILLAGE_RELIC_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::SHIPWRECK_CHART_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::DESERT_PYRAMID_SEAL_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::ANCIENT_CANOPY_SEAL_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::SPIDER_NEST_SEAL_CARD_DEFINITION_ID
			|| intent.card_definition_id
				== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
		&& !replayed)
	{
		instability_restore_needed = true;
		for (const World::RevisionChunkCoordinate &target : instability_targets)
		{
			WorldChunkInstabilityRecord record = {};
			int32_t applied_delta = instability_delta;
			result = this->world_.get_chunk_instability(target.chunk_x,
				target.chunk_z, &record);
			if (result == FT_ERR_SUCCESS
				&& intent.card_definition_id
					== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID
				&& instability_delta < 0)
				applied_delta = -static_cast<int32_t>(std::min(
					record.current_value,
					static_cast<uint32_t>(-instability_delta)));
			if (result == FT_ERR_SUCCESS && applied_delta != 0)
				result = this->world_.adjust_chunk_instability(
					record.record_revision, target.chunk_x, target.chunk_z,
				applied_delta, &record);
			if (result == FT_ERR_SUCCESS
				&& intent.card_definition_id
					== RegenerationCardSession::UNSTABLE_CATALYST_CARD_DEFINITION_ID)
			{
				const uint32_t modifier_id =
					game_session_catalyst_modifier_id(intent.private_result);
				result = this->world_.add_chunk_permanent_modifier(
					record.record_revision, target.chunk_x, target.chunk_z,
					modifier_id, static_cast<uint32_t>(record.record_revision),
					&record);
			}
			if (result == FT_ERR_SUCCESS
				&& intent.card_definition_id
					== RegenerationCardSession::VOLATILE_STABILIZER_CARD_DEFINITION_ID
				&& intent.private_result != 0U)
			{
				result = this->world_.add_chunk_permanent_modifier(
					record.record_revision, target.chunk_x, target.chunk_z,
					REGENERATION_MODIFIER_STABILIZER_OVERLOAD,
					static_cast<uint32_t>(record.record_revision), &record);
			}
			if (result != FT_ERR_SUCCESS)
				break;
		}
	}
	if (result == FT_ERR_SUCCESS)
		result = this->save_session_snapshot();
	if (result != FT_ERR_SUCCESS && !previous_card_state.empty()
		&& !previous_rules_state.empty())
	{
		rollback_result = FT_ERR_SUCCESS;
		const int32_t restore_card_result =
			this->regeneration_cards_.restore_snapshot(
				previous_card_state.data(), previous_card_state.size());
		if (rollback_result == FT_ERR_SUCCESS
			&& restore_card_result != FT_ERR_SUCCESS)
			rollback_result = restore_card_result;
		const int32_t restore_rules_result =
			this->regeneration_world_rules_.deserialize(
				previous_rules_state.data(), previous_rules_state.size(),
				this->world_.voxel_generation_settings(),
				this->regeneration_world_rules_policy_);
		if (rollback_result == FT_ERR_SUCCESS
			&& restore_rules_result != FT_ERR_SUCCESS)
			rollback_result = restore_rules_result;
		if (instability_restore_needed)
		{
			const int32_t restore_instability_result =
				this->world_.deserialize_chunk_instability(
					previous_instability_state.data(),
					previous_instability_state.size());
			if (rollback_result == FT_ERR_SUCCESS
				&& restore_instability_result != FT_ERR_SUCCESS)
				rollback_result = restore_instability_result;
		}
		if (revision.pending)
		{
			const int32_t restore_revision_rules_result =
				this->world_.update_pending_revision_rules(
					this->regeneration_world_rules_);
			if (rollback_result == FT_ERR_SUCCESS
				&& restore_revision_rules_result != FT_ERR_SUCCESS)
				rollback_result = restore_revision_rules_result;
		}
		return (rollback_result == FT_ERR_SUCCESS ? result : rollback_result);
	}
	*replayed_out = replayed;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::consume_pending_regeneration_draw_notice(
	card_game_host_intent_record *notice_out) noexcept
{
	std::vector<uint8_t> card_state_before;
	std::vector<uint8_t> instability_state_before;
	std::vector<World::RevisionChunkCoordinate> selected_chunks;
	std::vector<uint8_t> transaction_payload;
	GameTransactionJournal::Record existing = {};
	card_game_host_intent_record notice = {};
	bool is_notice = false;
	bool instability_snapshot_taken = false;
	bool participant_checkpointed = false;
	bool journal_replayed = false;
	uint64_t journal_sequence = 0U;
	int32_t instability_delta = 0;
	int32_t result;

	if (!this->active_ || notice_out == nullptr
		|| !this->regeneration_cards_.is_initialized())
		return (FT_ERR_INVALID_ARGUMENT);
	if (!this->world_loom_start_committed_
		|| !this->world_.world_revision().pending)
		return (FT_ERR_INVALID_OPERATION);
	result = this->regeneration_cards_.peek_pending_intent(&notice);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->regeneration_cards_.is_draw_notice(notice, &is_notice);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (!is_notice)
		return (FT_ERR_INVALID_OPERATION);
	result = this->regeneration_cards_.serialize_snapshot(card_state_before);
	if (result == FT_ERR_SUCCESS
		&& notice.intent.type_id
			== RegenerationCardSession::EMPTY_DRAW_INTENT_TYPE_ID)
		instability_delta =
			this->regeneration_world_rules_policy_.empty_draw_instability_delta;
	else if (result == FT_ERR_SUCCESS)
		instability_delta = this->regeneration_world_rules_policy_
			.hand_overflow_instability_delta;
	if (result == FT_ERR_SUCCESS && instability_delta < 0)
		result = FT_ERR_CONFIGURATION;
	if (result == FT_ERR_SUCCESS && instability_delta > 0)
	{
		if (!this->world_.is_chunk_instability_configured())
			result = FT_ERR_CONFIGURATION;
		else
			result = this->world_.serialize_chunk_instability(
				instability_state_before);
		if (result == FT_ERR_SUCCESS)
			instability_snapshot_taken = true;
		if (result == FT_ERR_SUCCESS)
			result = this->world_.copy_selected_revision_chunks(selected_chunks);
		if (result == FT_ERR_SUCCESS && (selected_chunks.empty()
			|| selected_chunks.size() > DRAW_NOTICE_TRANSACTION_MAXIMUM_CHUNKS))
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
		{
			try
			{
				transaction_payload.reserve(DRAW_NOTICE_TRANSACTION_HEADER_BYTES
					+ selected_chunks.size()
						* DRAW_NOTICE_TRANSACTION_ENTRY_BYTES);
				transaction_payload.push_back('D');
				transaction_payload.push_back('R');
				transaction_payload.push_back('N');
				transaction_payload.push_back('1');
				game_session_append_u32(transaction_payload,
					DRAW_NOTICE_TRANSACTION_VERSION);
				game_session_append_u64(transaction_payload, notice.sequence);
				game_session_append_u32(transaction_payload,
					notice.intent.type_id);
				game_session_append_u32(transaction_payload,
					static_cast<uint32_t>(instability_delta));
				game_session_append_u32(transaction_payload,
					static_cast<uint32_t>(selected_chunks.size()));
				game_session_append_u32(transaction_payload, 0U);
			}
			catch (const std::bad_alloc &)
			{
				result = FT_ERR_NO_MEMORY;
			}
		}
		for (const World::RevisionChunkCoordinate &chunk : selected_chunks)
		{
			WorldChunkInstabilityRecord record = {};
			uint32_t applied_delta = 0U;
			if (result == FT_ERR_SUCCESS)
				result = this->world_.get_chunk_instability(chunk.chunk_x,
					chunk.chunk_z, &record);
			if (result == FT_ERR_SUCCESS
				&& record.current_value
					> this->chunk_instability_policy_.maximum_value)
				result = FT_ERR_CONFIGURATION;
			if (result == FT_ERR_SUCCESS
				&& record.record_revision == UINT64_MAX)
				result = FT_ERR_OUT_OF_RANGE;
			if (result == FT_ERR_SUCCESS)
				applied_delta = std::min(static_cast<uint32_t>(instability_delta),
					this->chunk_instability_policy_.maximum_value
						- record.current_value);
			if (result == FT_ERR_SUCCESS)
			{
				try
				{
					game_session_append_u32(transaction_payload,
						static_cast<uint32_t>(chunk.chunk_x));
					game_session_append_u32(transaction_payload,
						static_cast<uint32_t>(chunk.chunk_z));
					game_session_append_u64(transaction_payload,
						record.record_revision);
					game_session_append_u32(transaction_payload,
						record.current_value);
					game_session_append_u32(transaction_payload,
						record.current_value + applied_delta);
				}
				catch (const std::bad_alloc &)
				{
					result = FT_ERR_NO_MEMORY;
				}
			}
		}
		if (result == FT_ERR_SUCCESS)
		{
			result = this->transaction_journal_.find(
				DRAW_NOTICE_TRANSACTION_SCOPE, notice.sequence, &existing);
			if (result == FT_ERR_NOT_FOUND)
				result = this->transaction_journal_.prepare(
					DRAW_NOTICE_TRANSACTION_SCOPE, notice.sequence,
					DRAW_NOTICE_TRANSACTION_TYPE, transaction_payload.data(),
					static_cast<uint32_t>(transaction_payload.size()),
					&journal_sequence, &journal_replayed);
			else if (result == FT_ERR_SUCCESS
				&& (existing.transaction_type
					!= DRAW_NOTICE_TRANSACTION_TYPE
					|| existing.payload != transaction_payload
					|| existing.state != GameTransactionJournal::PREPARED))
				result = FT_ERR_INVALID_STATE;
		}
		if (result == FT_ERR_SUCCESS)
			result = this->save_session_snapshot();
		if (result == FT_ERR_SUCCESS)
		{
			GameTransactionJournal::Record prepared = {};
			result = this->transaction_journal_.find(
				DRAW_NOTICE_TRANSACTION_SCOPE, notice.sequence, &prepared);
			if (result == FT_ERR_SUCCESS)
				result = game_session_apply_draw_notice_transaction(
					this->world_, prepared);
		}
	}
	if (result == FT_ERR_SUCCESS)
		result = this->regeneration_cards_.acknowledge_draw_notice(
			notice.sequence);
	if (result == FT_ERR_SUCCESS)
		result = this->save_session_snapshot();
	if (result == FT_ERR_SUCCESS && instability_delta > 0)
	{
		participant_checkpointed = true;
		result = this->transaction_journal_.mark_committed(
			DRAW_NOTICE_TRANSACTION_SCOPE, notice.sequence);
		if (result == FT_ERR_SUCCESS)
			result = this->transaction_journal_.prune_terminal_scope(
				DRAW_NOTICE_TRANSACTION_SCOPE, notice.sequence);
		if (result == FT_ERR_SUCCESS)
			result = this->save_session_snapshot();
	}
	if (result != FT_ERR_SUCCESS)
	{
		if (participant_checkpointed)
			return (result);
		int32_t restore_result = FT_ERR_SUCCESS;
		if (!card_state_before.empty())
		{
			const int32_t destroy_result = this->regeneration_cards_.destroy();
			restore_result = destroy_result;
			if (destroy_result == FT_ERR_SUCCESS)
				restore_result = this->regeneration_cards_.restore_snapshot(
					card_state_before.data(), card_state_before.size());
		}
		if (instability_snapshot_taken)
		{
			const int32_t restore_instability_result =
				this->world_.deserialize_chunk_instability(
					instability_state_before.data(),
					instability_state_before.size());
			if (restore_result == FT_ERR_SUCCESS
				&& restore_instability_result != FT_ERR_SUCCESS)
				restore_result = restore_instability_result;
		}
		return (restore_result == FT_ERR_SUCCESS ? result : restore_result);
	}
	*notice_out = notice;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::query_pending_regeneration_host_intents(
	std::vector<card_game_host_intent_record> &records_out) const noexcept
{
	if (!this->active_ || !this->regeneration_cards_.is_initialized())
		return (FT_ERR_INVALID_OPERATION);
	return (this->regeneration_cards_.copy_pending_intents(records_out));
}

int32_t GameSession::configure_regeneration_output_session(
	uint64_t session_id) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	/* Zero removes the optional host override; restored snapshots can then
	 * supply their persisted identity during the next start(). */
	this->configured_regeneration_output_session_id_ = session_id;
	this->regeneration_output_session_id_ = session_id;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::get_regeneration_output_session_id(
	uint64_t *session_id_out) const noexcept
{
	if (session_id_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*session_id_out = this->regeneration_output_session_id_;
	if (this->regeneration_output_session_id_ == 0U)
		return (FT_ERR_NOT_FOUND);
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::query_pending_regeneration_host_outputs(
	std::vector<RegenerationHostOutput> &outputs_out) const noexcept
{
	std::vector<card_game_host_intent_record> records;
	std::vector<RegenerationHostOutput> staged;
	int32_t result;

	if (this->regeneration_output_session_id_ == 0U)
		return (FT_ERR_INVALID_STATE);
	result = this->query_pending_regeneration_host_intents(records);
	if (result != FT_ERR_SUCCESS)
		return (result);
	try
	{
		staged.reserve(records.size());
		for (const card_game_host_intent_record &record : records)
		{
			RegenerationHostOutput output = {};
			output.session_id = this->regeneration_output_session_id_;
			output.record = record;
			staged.push_back(output);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	outputs_out.swap(staged);
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::acknowledge_regeneration_host_output(
	uint64_t session_id, uint64_t host_intent_sequence) noexcept
{
	ft_bool is_pending = FT_FALSE;
	int32_t result;

	if (session_id == 0U || session_id != this->regeneration_output_session_id_)
		return (FT_ERR_INVALID_STATE);
	result = this->regeneration_cards_.is_host_intent_pending(
		host_intent_sequence, &is_pending);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (is_pending == FT_FALSE)
		return (FT_ERR_SUCCESS);
	{
		GameTransactionJournal::Record transaction = {};
		result = this->transaction_journal_.find(
			game_session_regeneration_host_output_scope(session_id),
			host_intent_sequence, &transaction);
		if (result == FT_ERR_SUCCESS)
		{
			if (transaction.transaction_type
				!= REGENERATION_HOST_OUTPUT_TRANSACTION_TYPE
				|| transaction.state != GameTransactionJournal::COMMITTED)
				return (FT_ERR_PERMISSION_DENIED);
		}
		else if (result != FT_ERR_NOT_FOUND)
			return (result);
		else if (this->regeneration_host_output_prepare_ != nullptr)
			return (FT_ERR_PERMISSION_DENIED);
	}
	return (this->acknowledge_regeneration_host_intent_internal(
		host_intent_sequence));
}

int32_t GameSession::configure_regeneration_host_output_consumer(
	RegenerationHostOutputPrepare prepare,
	RegenerationHostOutputCommit commit,
	RegenerationHostOutputAbort abort, void *user_data) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if ((prepare == nullptr || commit == nullptr || abort == nullptr)
		&& (prepare != nullptr || commit != nullptr || abort != nullptr))
		return (FT_ERR_INVALID_ARGUMENT);
	if (prepare == nullptr && user_data != nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->regeneration_host_output_prepare_ = prepare;
	this->regeneration_host_output_commit_ = commit;
	this->regeneration_host_output_abort_ = abort;
	this->regeneration_host_output_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::process_regeneration_host_output_transaction(
	const RegenerationHostOutput &output) noexcept
{
	std::vector<uint8_t> payload;
	GameTransactionJournal::Record transaction = {};
	uint64_t journal_sequence = 0U;
	bool replayed = false;
	ft_bool is_pending = FT_FALSE;
	bool is_draw_notice = false;
	int32_t result;

	if (output.session_id == 0U || output.session_id
		!= this->regeneration_output_session_id_
		|| output.record.sequence == 0U
		|| this->regeneration_host_output_prepare_ == nullptr
		|| this->regeneration_host_output_commit_ == nullptr
		|| this->regeneration_host_output_abort_ == nullptr)
		return (FT_ERR_INVALID_STATE);
	if (output.record.intent.field_count > FT_CARD_GAME_MAX_HOST_INTENT_FIELDS
		|| output.record.intent.type_id == 0U
		|| output.record.intent.type_id
		== RegenerationCardSession::POLICY_INTENT_TYPE_ID)
		return (FT_ERR_PERMISSION_DENIED);
	result = this->regeneration_cards_.is_draw_notice(output.record,
		&is_draw_notice);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (is_draw_notice)
		return (FT_ERR_PERMISSION_DENIED);
	try
	{
		payload.reserve(40U + static_cast<std::size_t>(
			output.record.intent.field_count) * 16U);
		payload.push_back('R');
		payload.push_back('H');
		payload.push_back('O');
		payload.push_back('1');
		game_session_append_u32(payload, 1U);
		game_session_append_u64(payload, output.session_id);
		game_session_append_u64(payload, output.record.sequence);
		game_session_append_u32(payload, output.record.intent.type_id);
		game_session_append_u32(payload, output.record.intent.version);
		game_session_append_u32(payload, output.record.intent.field_count);
		game_session_append_u32(payload, 0U);
		for (uint32_t index = 0U;
			index < output.record.intent.field_count; ++index)
		{
			payload.push_back(static_cast<uint8_t>(
				output.record.intent.fields[index].type));
			payload.push_back(0U);
			payload.push_back(0U);
			payload.push_back(0U);
			game_session_append_u32(payload, 0U);
			game_session_append_u64(payload,
				output.record.intent.fields[index].value);
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	const uint64_t transaction_scope =
		game_session_regeneration_host_output_scope(output.session_id);
	result = this->transaction_journal_.find(transaction_scope,
		output.record.sequence, &transaction);
	if (result == FT_ERR_NOT_FOUND)
		result = this->transaction_journal_.prepare(transaction_scope,
			output.record.sequence, REGENERATION_HOST_OUTPUT_TRANSACTION_TYPE,
			payload.data(), static_cast<uint32_t>(payload.size()),
			&journal_sequence, &replayed);
	else if (result == FT_ERR_SUCCESS
		&& (transaction.transaction_type
			!= REGENERATION_HOST_OUTPUT_TRANSACTION_TYPE
			|| transaction.payload != payload))
		result = FT_ERR_INVALID_STATE;
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->save_session_snapshot();
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->regeneration_host_output_prepare_(output,
		this->regeneration_host_output_user_data_);
	if (result != FT_ERR_SUCCESS)
	{
		const int32_t abort_result =
			this->regeneration_host_output_abort_(output,
			this->regeneration_host_output_user_data_);
		if (abort_result != FT_ERR_SUCCESS)
			return (abort_result);
		return (result);
	}
	result = this->regeneration_host_output_commit_(output,
		this->regeneration_host_output_user_data_);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->transaction_journal_.mark_committed(transaction_scope,
		output.record.sequence);
	if (result == FT_ERR_SUCCESS)
		result = this->save_session_snapshot();
	if (result == FT_ERR_SUCCESS)
		result = this->regeneration_cards_.is_host_intent_pending(
			output.record.sequence, &is_pending);
	if (result == FT_ERR_SUCCESS && is_pending != FT_FALSE)
		result = this->acknowledge_regeneration_host_output(output.session_id,
			output.record.sequence);
	if (result == FT_ERR_SUCCESS)
		result = this->transaction_journal_.prune_terminal_scope(
			transaction_scope, output.record.sequence);
	if (result == FT_ERR_SUCCESS)
		result = this->save_session_snapshot();
	return (result);
}

int32_t GameSession::deliver_next_regeneration_host_output() noexcept
{
	std::vector<RegenerationHostOutput> outputs;
	int32_t result = this->recover_regeneration_host_outputs();
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->query_pending_regeneration_host_outputs(outputs);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (outputs.empty())
		return (FT_ERR_NOT_FOUND);
	return (this->process_regeneration_host_output_transaction(outputs[0]));
}

int32_t GameSession::recover_regeneration_host_outputs() noexcept
{
	int32_t result;
	if (this->regeneration_host_output_prepare_ == nullptr
		|| this->regeneration_host_output_commit_ == nullptr
		|| this->regeneration_host_output_abort_ == nullptr
		|| this->regeneration_output_session_id_ == 0U)
		return (FT_ERR_INVALID_STATE);
	while (true)
	{
		std::vector<RegenerationHostOutput> outputs;
		result = this->query_pending_regeneration_host_outputs(outputs);
		if (result != FT_ERR_SUCCESS || outputs.empty())
			return (result);
		const RegenerationHostOutput &output = outputs[0];
		GameTransactionJournal::Record record = {};
		result = this->transaction_journal_.find(
			game_session_regeneration_host_output_scope(output.session_id),
			output.record.sequence, &record);
		if (result == FT_ERR_NOT_FOUND)
			return (FT_ERR_SUCCESS);
		if (result != FT_ERR_SUCCESS
			|| record.transaction_type
				!= REGENERATION_HOST_OUTPUT_TRANSACTION_TYPE
			|| (record.state != GameTransactionJournal::PREPARED
				&& record.state != GameTransactionJournal::COMMITTED))
			return (FT_ERR_INVALID_STATE);
		result = this->process_regeneration_host_output_transaction(output);
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
}

int32_t GameSession::acknowledge_regeneration_host_intent_internal(
	uint64_t host_intent_sequence) noexcept
{
	std::vector<uint8_t> card_state_before;
	card_game_host_intent_record pending = {};
	bool is_draw_notice = false;
	bool acknowledged = false;
	int32_t result;

	if (!this->active_ || host_intent_sequence == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	result = this->regeneration_cards_.peek_pending_intent(&pending);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (pending.sequence != host_intent_sequence)
		return (FT_ERR_INVALID_STATE);
	if (pending.intent.type_id == RegenerationCardSession::POLICY_INTENT_TYPE_ID)
		return (FT_ERR_PERMISSION_DENIED);
	result = this->regeneration_cards_.is_draw_notice(pending, &is_draw_notice);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (is_draw_notice)
		return (FT_ERR_PERMISSION_DENIED);
	result = this->regeneration_cards_.serialize_snapshot(card_state_before);
	if (result == FT_ERR_SUCCESS)
	{
		result = this->regeneration_cards_.acknowledge_intent(
			host_intent_sequence);
		acknowledged = result == FT_ERR_SUCCESS;
	}
	if (result == FT_ERR_SUCCESS)
		result = this->save_session_snapshot();
	if (result != FT_ERR_SUCCESS && acknowledged)
	{
		const int32_t restore_result = this->regeneration_cards_.restore_snapshot(
			card_state_before.data(), card_state_before.size(),
			this->regeneration_script_catalog_.empty() ? nullptr
				: this->regeneration_script_catalog_.data(),
			static_cast<uint32_t>(this->regeneration_script_catalog_.size()),
			this->trusted_script_manifest_hash_,
			this->regeneration_world_rules_policy_.generation_manifest_hash);
		if (restore_result != FT_ERR_SUCCESS)
			return (restore_result);
	}
	return (result);
}

int32_t GameSession::begin_card_regeneration_revision() noexcept
{
	int32_t result;

	if (!this->active_ || !this->regeneration_world_rules_.is_initialized()
		|| !this->world_loom_start_committed_
		|| !this->world_.world_revision().pending)
		return (FT_ERR_INVALID_OPERATION);
	result = this->world_.begin_world_revision(
		this->world_.voxel_generation_settings(),
		this->regeneration_world_rules_, World::REGEN_FULL);
	if (result == FT_ERR_SUCCESS)
		this->world_loom_start_committed_ = false;
	return (result);
}

int32_t GameSession::resolve_expired_regeneration_choices(
	uint64_t request_id, uint32_t *resolved_count_out) noexcept
{
	uint64_t authoritative_epoch = 0U;
	int32_t result;

	if (request_id == UINT64_MAX)
		return (FT_ERR_PERMISSION_DENIED);
	if (this->revision_clock_provider_ == nullptr)
		return (FT_ERR_INVALID_STATE);
	result = this->revision_clock_provider_(&authoritative_epoch,
		this->revision_clock_provider_user_data_);
	if (result != FT_ERR_SUCCESS || authoritative_epoch == 0U)
		return (result == FT_ERR_SUCCESS ? FT_ERR_INVALID_STATE : result);
	return (this->resolve_expired_regeneration_choices_internal(request_id,
		authoritative_epoch, resolved_count_out));
}

int32_t GameSession::resolve_expired_regeneration_choices_internal(
	uint64_t request_id, uint64_t authoritative_epoch,
	uint32_t *resolved_count_out) noexcept
{
	std::vector<uint8_t> previous_card_state;
	uint32_t resolved_count = 0U;
	int32_t result;

	if (!this->active_ || !this->regeneration_cards_.is_initialized()
		|| !this->world_loom_start_committed_
		|| !this->world_.world_revision().pending)
		return (FT_ERR_INVALID_OPERATION);
	if (request_id == 0U || resolved_count_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*resolved_count_out = 0U;
	result = this->regeneration_cards_.serialize_snapshot(previous_card_state);
	if (result == FT_ERR_SUCCESS)
		result = this->regeneration_cards_.resolve_expired_choices(
			request_id, authoritative_epoch, &resolved_count);
	if (result == FT_ERR_SUCCESS)
		result = this->save_session_snapshot();
	if (result != FT_ERR_SUCCESS && !previous_card_state.empty())
	{
		(void)this->regeneration_cards_.restore_snapshot(
			previous_card_state.data(), previous_card_state.size());
		return (result);
	}
	*resolved_count_out = resolved_count;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::query_hand_recipe(uint32_t recipe_id,
	game_crafting_query_result *result,
	ft_vector<game_crafting_missing_ingredient> *missing_groups) const noexcept
{
	if (!this->active_)
		return (FT_ERR_INVALID_OPERATION);
	return (this->recipe_catalog_.query_hand_recipe(
		this->player_character_.get_inventory(), recipe_id, result,
		missing_groups));
}

int32_t GameSession::query_station_recipe(uint32_t recipe_id,
	uint32_t station_id, int32_t station_x, int32_t station_y,
	int32_t station_z, uint64_t expected_station_revision,
	game_crafting_query_result *result,
	ft_vector<game_crafting_missing_ingredient> *missing_groups) const noexcept
{
	if (!this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (result == nullptr || missing_groups == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (station_id <= static_cast<uint32_t>(
			MinecraftRecipeCatalog::HAND_STATION_ID)
		|| station_id > static_cast<uint32_t>(
			MinecraftRecipeCatalog::FURNACE_STATION_ID))
		return (FT_ERR_INVALID_ARGUMENT);
	if (expected_station_revision == 0U
		|| this->processing_station_authorizer_ == nullptr
		|| !this->processing_station_authorizer_(station_id, station_x,
			station_y, station_z, expected_station_revision,
			this->camera_.x, this->camera_.y, this->camera_.z,
			this->processing_station_authorizer_user_data_))
		return (FT_ERR_PERMISSION_DENIED);
	return (this->recipe_catalog_.query_recipe_at_station(
		this->player_character_.get_inventory(), recipe_id,
		static_cast<MinecraftRecipeCatalog::StationId>(station_id), result,
		missing_groups));
}

bool GameSession::authorize_recipe_discovery_event(
	RecipeDiscoveryAuthorizer authorizer, uint32_t source_type_id,
	uint32_t source_id, uint64_t source_event_id,
	void *user_data) noexcept
{
	if (authorizer == nullptr || source_type_id == 0U || source_id == 0U
		|| source_event_id == 0U)
		return (false);
	return (authorizer(source_type_id, source_id, source_event_id, user_data));
}

bool GameSession::authorize_script_catalog(
	ScriptCatalogAuthorizer authorizer, const uint8_t *bytes,
	uint32_t byte_count, uint64_t expected_manifest_hash,
	void *user_data) noexcept
{
	if (authorizer == nullptr || bytes == nullptr || byte_count == 0U
		|| expected_manifest_hash == 0U)
		return (false);
	return (authorizer(bytes, byte_count, expected_manifest_hash, user_data));
}

bool GameSession::authorize_volcano_mark_use(
	VolcanoMarkUseAuthorizer authorizer, uint32_t card_instance_id,
	uint32_t revision_identifier, void *user_data) noexcept
{
	if (authorizer == nullptr || card_instance_id == 0U
		|| revision_identifier == 0U)
		return (false);
	return (authorizer(card_instance_id, revision_identifier, user_data));
}

int32_t GameSession::grant_recipe_discovery(uint32_t source_type_id,
	uint32_t source_id, uint64_t source_event_id,
	ft_bool *newly_unlocked) noexcept
{
	const GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};

	if (!this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (newly_unlocked == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!GameSession::authorize_recipe_discovery_event(
			this->recipe_discovery_authorizer_, source_type_id, source_id,
			source_event_id, this->recipe_discovery_authorizer_user_data_))
		return (FT_ERR_INVALID_OPERATION);
	return (RecipeDiscoveryTransaction::execute(this->recipe_catalog_,
		source_type_id, source_id, source_event_id, snapshot_writer,
		newly_unlocked));
}

int32_t GameSession::configure_recipe_discovery_authorizer(
	RecipeDiscoveryAuthorizer authorizer, void *user_data) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (authorizer == nullptr && user_data != nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->recipe_discovery_authorizer_ = authorizer;
	this->recipe_discovery_authorizer_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_recipe_discovery_source(
	uint32_t source_type_id, uint32_t source_id, uint32_t recipe_id) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (source_type_id == 0U || source_id == 0U || recipe_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	for (const ConfiguredRecipeDiscoverySource &source
		: this->recipe_discovery_sources_)
	{
		if (source.source_type_id != source_type_id
			|| source.source_id != source_id)
			continue ;
		return (source.recipe_id == recipe_id
			? FT_ERR_SUCCESS : FT_ERR_ALREADY_EXISTS);
	}
	if (this->recipe_discovery_sources_.size() >= 4096U)
		return (FT_ERR_FULL);
	try
	{
		this->recipe_discovery_sources_.push_back({source_type_id, source_id,
			recipe_id});
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_processing_recipe_policy(uint32_t recipe_id,
	int32_t fuel_item_id, uint32_t fuel_units_per_item,
	uint32_t ticks_per_item) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	return (this->recipe_catalog_.configure_processing_recipe_policy(
		recipe_id, fuel_item_id, fuel_units_per_item, ticks_per_item));
}

int32_t GameSession::configure_processing_station_authorizer(
	ProcessingStationAuthorizer authorizer, void *user_data) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (authorizer == nullptr && user_data != nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->processing_station_authorizer_ = authorizer;
	this->processing_station_authorizer_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_processing_station_continuity_validator(
	ProcessingStationContinuityValidator validator,
	void *user_data) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (validator == nullptr && user_data != nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->processing_station_continuity_validator_ = validator;
	this->processing_station_continuity_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_local_processing_station_authorizer() noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	int32_t error_code = this->configure_processing_station_continuity_validator(
		&GameSession::validate_local_processing_station_continuity,
		&this->world_);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->configure_processing_station_authorizer(
			&GameSession::authorize_local_processing_station, &this->world_);
	return (error_code);
}

int32_t GameSession::configure_revision_occupancy_provider(
	RevisionOccupancyProvider provider, void *user_data) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (provider == nullptr && user_data != nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->revision_occupancy_provider_ = provider;
	this->revision_occupancy_provider_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_revision_clock_provider(
	RevisionClockProvider provider, void *user_data) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if ((provider == nullptr) != (user_data == nullptr))
		return (FT_ERR_INVALID_ARGUMENT);
	this->revision_clock_provider_ = provider;
	this->revision_clock_provider_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

bool GameSession::validate_local_processing_station_continuity(
	uint32_t station_id, int32_t station_x, int32_t station_y,
	int32_t station_z, uint64_t expected_station_revision,
	void *user_data) noexcept
{
	World *world = static_cast<World *>(user_data);
	const WorldChunk *station_chunk = nullptr;
	uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
	uint32_t expected_block_id = GAME_VOXEL_AIR_BLOCK;
	switch (station_id)
	{
		case static_cast<uint32_t>(MinecraftRecipeCatalog::WORKBENCH_STATION_ID):
			expected_block_id = VOXEL_GENERATOR_WORKBENCH_BLOCK_ID;
			break ;
		case static_cast<uint32_t>(MinecraftRecipeCatalog::PAPER_MILL_STATION_ID):
			expected_block_id = VOXEL_GENERATOR_PAPER_MILL_BLOCK_ID;
			break ;
		case static_cast<uint32_t>(MinecraftRecipeCatalog::CARD_PRESS_STATION_ID):
			expected_block_id = VOXEL_GENERATOR_CARD_PRESS_BLOCK_ID;
			break ;
		case static_cast<uint32_t>(MinecraftRecipeCatalog::FURNACE_STATION_ID):
			expected_block_id = VOXEL_GENERATOR_FURNACE_BLOCK_ID;
			break ;
		default:
			return (false);
	}
	if (world == nullptr
		|| expected_station_revision == 0U
		|| station_y < 0 || station_y >= GAME_VOXEL_CHUNK_HEIGHT
		|| !world->block_id_at(station_x, station_y, station_z, &block_id)
		|| block_id != expected_block_id)
		return (false);
	const int32_t chunk_x = WorldCoordinates::floor_divide(station_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(station_z,
		GAME_VOXEL_CHUNK_DEPTH);
	station_chunk = world->find_chunk(chunk_x, chunk_z);
	return (station_chunk != nullptr
		&& station_chunk->voxel_revision == expected_station_revision);
}

bool GameSession::authorize_local_processing_station(uint32_t station_id,
	int32_t station_x, int32_t station_y, int32_t station_z,
	uint64_t expected_station_revision, double player_x, double player_y,
	double player_z, void *user_data) noexcept
{
	if (!GameSession::validate_local_processing_station_continuity(
			station_id, station_x, station_y, station_z,
			expected_station_revision, user_data))
		return (false);
	const double dx = player_x - (static_cast<double>(station_x) + 0.5);
	const double dy = player_y - (static_cast<double>(station_y) + 0.5);
	const double dz = player_z - (static_cast<double>(station_z) + 0.5);
	return (dx * dx + dy * dy + dz * dz <= 36.0);
}

int32_t GameSession::synchronize_item_instance_id_allocator() noexcept
{
	game_inventory_snapshot inventory_snapshot = {};
	std::vector<uint64_t> equipped_instance_ids;
	uint64_t maximum_instance_id = 0U;
	uint64_t maximum_ground_instance_id = 0U;
	uint64_t maximum_entity_instance_id = 0U;
	uint64_t maximum_card_instance_id = 0U;
	int32_t result = this->player_character_.get_inventory().get_snapshot(
		inventory_snapshot);
	if (result != FT_ERR_SUCCESS)
		return (result);
	for (const game_inventory_item_snapshot &item : inventory_snapshot.items)
		maximum_instance_id = std::max(maximum_instance_id, item.instance_id);
	result = game_session_collect_equipped_instance_ids(this->player_character_,
		equipped_instance_ids, &maximum_instance_id);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->world_items_.get_max_item_instance_id(
		&maximum_ground_instance_id);
	if (result == FT_ERR_SUCCESS)
		result = this->world_entities_.get_max_item_instance_id(
			&maximum_entity_instance_id);
	if (result == FT_ERR_SUCCESS)
		result = this->regeneration_cards_.get_max_physical_item_instance_id(
			&maximum_card_instance_id);
	if (result != FT_ERR_SUCCESS)
		return (result);
	maximum_instance_id = std::max(maximum_instance_id,
		maximum_ground_instance_id);
	maximum_instance_id = std::max(maximum_instance_id,
		maximum_entity_instance_id);
	maximum_instance_id = std::max(maximum_instance_id,
		maximum_card_instance_id);
	return (this->recipe_catalog_.ensure_next_output_instance_id_above(
		maximum_instance_id));
}

int32_t GameSession::validate_physical_item_identity_uniqueness() const noexcept
{
	std::vector<uint64_t> card_instance_ids;
	game_inventory_snapshot inventory_snapshot = {};
	uint64_t ignored_maximum_instance_id = 0U;
	int32_t result = this->regeneration_cards_.copy_physical_item_instance_ids(
		card_instance_ids);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = game_session_validate_equipped_catalog(this->player_character_,
		this->item_catalog_);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = game_session_collect_equipped_instance_ids(
		this->player_character_, card_instance_ids,
		&ignored_maximum_instance_id);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->player_character_.get_inventory().get_snapshot(
		inventory_snapshot);
	if (result == FT_ERR_SUCCESS)
		result = this->item_catalog_.validate_inventory_instance_policy(
			inventory_snapshot);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->world_items_.validate_item_instance_policy(
		this->item_catalog_);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (this->world_items_.validate_instance_id_uniqueness(
		this->player_character_.get_inventory(), &this->world_entities_,
		card_instance_ids));
}

int32_t GameSession::research_card_recipe(uint64_t request_id,
	uint64_t expected_inventory_revision, int32_t station_x,
	int32_t station_y, int32_t station_z,
	uint64_t expected_station_revision,
	MinecraftRecipeResearchResult *result_out) noexcept
{
	if (!this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (request_id == 0U || result_out == nullptr
		|| expected_inventory_revision == UINT64_MAX
		|| expected_station_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	GameTransactionJournal::Record record = {};
	int32_t result = this->transaction_journal_.find(
		RECIPE_RESEARCH_TRANSACTION_SCOPE, request_id, &record);
	if (result == FT_ERR_SUCCESS)
	{
		uint64_t recorded_inventory_revision = 0U;
		uint64_t recorded_station_revision = 0U;
		uint64_t random_value = 0U;
		uint32_t station_id = 0U;
		uint32_t recipe_id = 0U;
		int32_t x = 0;
		int32_t y = 0;
		int32_t z = 0;
		if (!game_session_decode_recipe_research(record,
				&recorded_inventory_revision, &station_id, &x, &y, &z,
				&recorded_station_revision, &random_value, &recipe_id)
			|| recorded_inventory_revision != expected_inventory_revision
			|| recorded_station_revision != expected_station_revision
			|| x != station_x || y != station_y || z != station_z)
			return (FT_ERR_INVALID_ARGUMENT);
		if (record.state == GameTransactionJournal::ABORTED)
			return (FT_ERR_INVALID_OPERATION);
		if (record.state == GameTransactionJournal::PREPARED)
			result = this->recover_pending_recipe_research();
		else if (record.state != GameTransactionJournal::COMMITTED)
			return (FT_ERR_INVALID_STATE);
		if (result != FT_ERR_SUCCESS)
			return (result);
		result = this->save_session_snapshot();
		if (result != FT_ERR_SUCCESS)
			return (result);
		uint64_t inventory_revision = 0U;
		result = this->player_character_.get_inventory().get_revision(
			&inventory_revision);
		if (result != FT_ERR_SUCCESS)
			return (result);
		*result_out = {request_id, inventory_revision, random_value,
			recipe_id, true};
		return (FT_ERR_SUCCESS);
	}
	if (result != FT_ERR_NOT_FOUND)
		return (result);
	if (this->processing_station_authorizer_ == nullptr)
		return (FT_ERR_INVALID_OPERATION);
	if (this->world_.world_revision().regenerating
		|| this->world_.world_revision().pending)
		return (FT_ERR_INVALID_OPERATION);
	if (!this->processing_station_authorizer_(
			static_cast<uint32_t>(MinecraftRecipeCatalog::CARD_PRESS_STATION_ID),
			station_x, station_y, station_z, expected_station_revision,
			this->player_character_.get_x(), this->player_character_.get_y(),
			this->player_character_.get_z(),
			this->processing_station_authorizer_user_data_))
		return (FT_ERR_INVALID_OPERATION);
	game_inventory_snapshot inventory = {};
	result = this->player_character_.get_inventory().get_snapshot(inventory);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (inventory.revision != expected_inventory_revision)
		return (FT_ERR_INVALID_STATE);
	if (this->player_character_.get_inventory().count_item(
			RECIPE_RESEARCH_COST_ITEM_ID)
		< static_cast<int32_t>(RECIPE_RESEARCH_COST_QUANTITY))
		return (FT_ERR_NOT_FOUND);
	std::vector<uint32_t> candidates;
	try
	{
		for (uint32_t recipe_id : RECIPE_RESEARCH_RECIPE_IDS)
		{
			ft_bool known = FT_FALSE;
			result = this->recipe_catalog_.is_recipe_known(recipe_id, &known);
			if (result != FT_ERR_SUCCESS)
				return (result);
			if (known == FT_FALSE)
				candidates.push_back(recipe_id);
		}
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (candidates.empty())
		return (FT_ERR_NOT_FOUND);
	uint64_t random_state = game_session_recipe_research_seed(this->seed_,
		request_id, expected_inventory_revision,
		this->world_.world_revision().identifier);
	const uint64_t bound = candidates.size();
	const uint64_t rejection_threshold = (0U - bound) % bound;
	uint64_t random_value;
	do
	{
		random_value = game_session_recipe_research_next_random(random_state);
	}
	while (random_value < rejection_threshold);
	const uint32_t recipe_id = candidates[random_value % bound];
	std::vector<uint8_t> payload;
	try
	{
		payload.insert(payload.end(), {'R', 'S', 'R', '1'});
		game_session_append_u32(payload, 1U);
		game_session_append_u64(payload, expected_inventory_revision);
		game_session_append_u32(payload, static_cast<uint32_t>(
			MinecraftRecipeCatalog::CARD_PRESS_STATION_ID));
		game_session_append_u32(payload, static_cast<uint32_t>(station_x));
		game_session_append_u32(payload, static_cast<uint32_t>(station_y));
		game_session_append_u32(payload, static_cast<uint32_t>(station_z));
		game_session_append_u64(payload, expected_station_revision);
		game_session_append_u64(payload, random_value);
		game_session_append_u32(payload, recipe_id);
		game_session_append_u32(payload, static_cast<uint32_t>(
			RECIPE_RESEARCH_COST_ITEM_ID));
		game_session_append_u32(payload, RECIPE_RESEARCH_COST_QUANTITY);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	uint64_t sequence = 0U;
	bool replayed = false;
	result = this->transaction_journal_.prepare(
		RECIPE_RESEARCH_TRANSACTION_SCOPE, request_id,
		RECIPE_RESEARCH_TRANSACTION_TYPE, payload.data(),
		static_cast<uint32_t>(payload.size()), &sequence, &replayed);
	(void)sequence;
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (replayed)
		return (FT_ERR_INVALID_STATE);
	result = this->save_session_snapshot();
	if (result != FT_ERR_SUCCESS)
	{
		(void)this->transaction_journal_.mark_aborted(
			RECIPE_RESEARCH_TRANSACTION_SCOPE, request_id);
		(void)this->save_session_snapshot();
		return (result);
	}
	result = this->recover_pending_recipe_research();
	if (result != FT_ERR_SUCCESS)
		return (result);
	uint64_t inventory_revision = 0U;
	result = this->player_character_.get_inventory().get_revision(
		&inventory_revision);
	if (result != FT_ERR_SUCCESS)
		return (result);
	*result_out = {request_id, inventory_revision, random_value,
		recipe_id, false};
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::craft_hand_recipe(uint64_t request_id,
	uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision,
	MinecraftCraftActionResult *result_out) noexcept
{
	const GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	int32_t result;

	if (!this->active_)
		return (FT_ERR_INVALID_OPERATION);
	result = this->synchronize_item_instance_id_allocator();
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (WorldItemCraftTransaction::execute(this->world_item_save_path_,
		this->player_character_, this->world_items_, this->recipe_catalog_,
		this->transaction_journal_, request_id, recipe_id, batch_count,
		expected_inventory_revision,
		static_cast<uint32_t>(MinecraftRecipeCatalog::HAND_STATION_ID),
		0, 0, 0, 0U, result_out, &snapshot_writer));
}

int32_t GameSession::craft_station_recipe(uint64_t request_id,
	uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision, uint32_t station_id,
	int32_t station_x, int32_t station_y, int32_t station_z,
	uint64_t expected_station_revision,
	MinecraftCraftActionResult *result_out) noexcept
{
	const GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};
	if (!this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (result_out == nullptr || request_id == 0U || recipe_id == 0U
		|| batch_count <= 0 || station_id == static_cast<uint32_t>(
			MinecraftRecipeCatalog::HAND_STATION_ID)
		|| station_id > static_cast<uint32_t>(
			MinecraftRecipeCatalog::FURNACE_STATION_ID))
		return (FT_ERR_INVALID_ARGUMENT);
	if (expected_station_revision == 0U
		|| this->processing_station_authorizer_ == nullptr
		|| !this->processing_station_authorizer_(station_id, station_x,
			station_y, station_z, expected_station_revision,
			this->camera_.x, this->camera_.y, this->camera_.z,
			this->processing_station_authorizer_user_data_))
		return (FT_ERR_PERMISSION_DENIED);
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	int32_t result = this->synchronize_item_instance_id_allocator();
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (WorldItemCraftTransaction::execute(this->world_item_save_path_,
		this->player_character_, this->world_items_, this->recipe_catalog_,
		this->transaction_journal_, request_id, recipe_id, batch_count,
		expected_inventory_revision, station_id, station_x, station_y,
		station_z, expected_station_revision, result_out, &snapshot_writer));
}

int32_t GameSession::equip_inventory_item(uint64_t request_id,
	int32_t inventory_slot, uint64_t expected_inventory_revision,
	uint32_t equipment_slot) noexcept
{
	if (!this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (request_id == 0U || inventory_slot < 0
		|| expected_inventory_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	const GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};
	return (WorldEquipmentLoadoutTransaction::equip(
		this->world_item_save_path_, this->player_character_,
		this->item_catalog_, this->transaction_journal_, request_id,
		inventory_slot, expected_inventory_revision, equipment_slot,
		&snapshot_writer));
}

int32_t GameSession::unequip_equipment_item(uint64_t request_id,
	uint64_t expected_inventory_revision, uint32_t equipment_slot) noexcept
{
	if (!this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (request_id == 0U || expected_inventory_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	const GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};
	return (WorldEquipmentLoadoutTransaction::unequip(
		this->world_item_save_path_, this->player_character_,
		this->item_catalog_, this->transaction_journal_, request_id,
		expected_inventory_revision, equipment_slot, &snapshot_writer));
}

int32_t GameSession::repair_equipment(uint64_t request_id,
	int32_t equipment_slot, uint64_t expected_inventory_revision,
	int32_t station_x, int32_t station_y, int32_t station_z,
	uint64_t expected_station_revision) noexcept
{
	if (!this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (request_id == 0U || equipment_slot < 0
		|| expected_inventory_revision == 0U
		|| expected_station_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	const GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};
	GameTransactionJournal::Record existing = {};
	int32_t existing_result;
	{
		std::lock_guard<std::mutex> item_transaction_lock(
			this->item_instance_transaction_mutex_);
		existing_result = this->transaction_journal_.find(
			WorldEquipmentRepairTransaction::journal_scope(), request_id,
			&existing);
		if (existing_result == FT_ERR_SUCCESS)
			return (WorldEquipmentRepairTransaction::execute(
				this->world_item_save_path_, this->player_character_,
				this->item_catalog_, this->transaction_journal_, request_id,
				equipment_slot, expected_inventory_revision, station_x,
				station_y, station_z, expected_station_revision,
				&snapshot_writer));
	}
	if (existing_result != FT_ERR_NOT_FOUND)
		return (existing_result);
	if (this->world_.world_revision().pending
		|| this->world_.world_revision().regenerating)
		return (FT_ERR_INVALID_OPERATION);
	if (this->processing_station_authorizer_ == nullptr
		|| !this->processing_station_authorizer_(
			static_cast<uint32_t>(MinecraftRecipeCatalog::WORKBENCH_STATION_ID),
			station_x, station_y, station_z, expected_station_revision,
			this->camera_.x, this->camera_.y, this->camera_.z,
			this->processing_station_authorizer_user_data_))
		return (FT_ERR_PERMISSION_DENIED);
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	existing_result = this->transaction_journal_.find(
		WorldEquipmentRepairTransaction::journal_scope(), request_id, &existing);
	if (existing_result == FT_ERR_SUCCESS)
		return (WorldEquipmentRepairTransaction::execute(
			this->world_item_save_path_, this->player_character_,
			this->item_catalog_, this->transaction_journal_, request_id,
			equipment_slot, expected_inventory_revision, station_x, station_y,
			station_z, expected_station_revision, &snapshot_writer));
	if (existing_result != FT_ERR_NOT_FOUND)
		return (existing_result);
	if (this->world_.world_revision().pending
		|| this->world_.world_revision().regenerating)
		return (FT_ERR_INVALID_OPERATION);
	uint32_t station_block_id = GAME_VOXEL_AIR_BLOCK;
	if (!this->world_.block_id_at(station_x, station_y, station_z,
			&station_block_id)
		|| station_block_id != VOXEL_GENERATOR_WORKBENCH_BLOCK_ID)
		return (FT_ERR_PERMISSION_DENIED);
	const int32_t chunk_x = WorldCoordinates::floor_divide(station_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(station_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const WorldChunk *station_chunk = this->world_.find_chunk(chunk_x, chunk_z);
	if (station_chunk == nullptr
		|| station_chunk->voxel_revision != expected_station_revision)
		return (FT_ERR_INVALID_STATE);
	return (WorldEquipmentRepairTransaction::execute(
		this->world_item_save_path_, this->player_character_, this->item_catalog_,
		this->transaction_journal_, request_id, equipment_slot,
		expected_inventory_revision, station_x, station_y, station_z,
		expected_station_revision, &snapshot_writer));
}

int32_t GameSession::query_equipment_repair(int32_t equipment_slot,
	uint64_t expected_inventory_revision, int32_t station_x,
	int32_t station_y, int32_t station_z,
	uint64_t expected_station_revision,
	MinecraftEquipmentRepairQuote *quote_out) const noexcept
{
	if (!this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (quote_out == nullptr || equipment_slot < 0
		|| expected_inventory_revision == 0U || expected_station_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->processing_station_authorizer_ == nullptr
		|| !this->processing_station_authorizer_(
			static_cast<uint32_t>(MinecraftRecipeCatalog::WORKBENCH_STATION_ID),
			station_x, station_y, station_z, expected_station_revision,
			this->camera_.x, this->camera_.y, this->camera_.z,
			this->processing_station_authorizer_user_data_))
		return (FT_ERR_PERMISSION_DENIED);
	uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
	if (!this->world_.block_id_at(station_x, station_y, station_z, &block_id)
		|| block_id != VOXEL_GENERATOR_WORKBENCH_BLOCK_ID)
		return (FT_ERR_PERMISSION_DENIED);
	const int32_t chunk_x = WorldCoordinates::floor_divide(station_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(station_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const WorldChunk *chunk = this->world_.find_chunk(chunk_x, chunk_z);
	if (chunk == nullptr || chunk->voxel_revision != expected_station_revision)
		return (FT_ERR_INVALID_STATE);
	game_inventory_snapshot inventory = {};
	int32_t result = this->player_character_.get_inventory().get_snapshot(
		inventory);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (inventory.revision != expected_inventory_revision)
		return (FT_ERR_INVALID_STATE);
	std::vector<game_inventory_item_snapshot>::const_iterator equipment =
		std::find_if(inventory.items.begin(), inventory.items.end(),
			[equipment_slot](const game_inventory_item_snapshot &item)
			{ return (item.slot == equipment_slot); });
	if (equipment == inventory.items.end())
		return (FT_ERR_NOT_FOUND);
	if (equipment->instance_id == 0U || equipment->stack_size != 1
		|| equipment->max_stack != 1)
		return (FT_ERR_INVALID_STATE);
	MinecraftEquipmentRepairQuote quote = {};
	quote.equipment_slot = equipment_slot;
	quote.equipment_item_id = equipment->item_id;
	quote.equipment_instance_id = equipment->instance_id;
	quote.inventory_revision = inventory.revision;
	result = this->item_catalog_.query_equipment_durability(*equipment,
		&quote.current_durability, &quote.maximum_durability);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->item_catalog_.resolve_equipment_repair_cost(
		equipment->item_id, &quote.material_item_id,
		&quote.material_quantity);
	if (result != FT_ERR_SUCCESS)
		return (result);
	uint64_t available_material = 0U;
	for (const game_inventory_item_snapshot &item : inventory.items)
		if (item.item_id == quote.material_item_id && item.instance_id == 0U
			&& item.stack_size > 0 && item.max_stack > 1)
		available_material += static_cast<uint32_t>(item.stack_size);
	quote.material_quantity_available = static_cast<int32_t>(
		std::min<uint64_t>(available_material,
			static_cast<uint64_t>(std::numeric_limits<int32_t>::max())));
	quote.repairable = quote.equipment_instance_id != 0U
		&& quote.current_durability < quote.maximum_durability
		&& quote.material_quantity_available >= quote.material_quantity;
	*quote_out = quote;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::launch_arrow(uint64_t request_id,
	uint64_t expected_inventory_revision) noexcept
{
	return (this->launch_arrow_internal(0U, request_id,
		expected_inventory_revision));
}

int32_t GameSession::launch_arrow_for_actor(uint64_t request_id,
	uint64_t authenticated_actor_id,
	uint64_t expected_inventory_revision) noexcept
{
	if (authenticated_actor_id == 0U
		|| this->combat_actor_authorizer_ == nullptr
			|| !this->combat_actor_authorizer_(authenticated_actor_id,
				this->combat_actor_authorizer_user_data_))
		return (FT_ERR_PERMISSION_DENIED);
	return (this->launch_arrow_internal(authenticated_actor_id, request_id,
		expected_inventory_revision));
}

int32_t GameSession::launch_arrow_internal(uint64_t actor_id,
	uint64_t request_id, uint64_t expected_inventory_revision) noexcept
{
	if (!this->active_ || !this->player_character_.is_alive())
		return (FT_ERR_INVALID_OPERATION);
	if (this->world_.world_revision().pending
		|| this->world_.world_revision().regenerating)
		return (FT_ERR_INVALID_OPERATION);
	if (expected_inventory_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	double direction_x = 0.0;
	double direction_y = 0.0;
	double direction_z = 0.0;
	PlayerController::camera_forward(this->camera_, &direction_x,
		&direction_y, &direction_z);
	const GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};
	return (WorldProjectileLaunchTransaction::launch(
		this->world_item_save_path_, this->player_character_,
		this->world_entities_, this->item_catalog_, this->transaction_journal_,
		actor_id, request_id, expected_inventory_revision,
		this->world_.current_tick, this->camera_.x, this->camera_.y,
		this->camera_.z, direction_x, direction_y, direction_z,
		&snapshot_writer));
}

int32_t GameSession::try_launch_equipped_bow() noexcept
{
	ft_sharedptr<game_item> *bow =
		this->player_character_.get_equipped_item(EQUIP_WEAPON);
	if (bow == nullptr)
		return (this->player_character_.get_error());
	const bool has_bow = *bow
		&& (*bow)->get_item_id() == MinecraftItemCatalog::BOW_ITEM_ID;
	(void)bow->destroy();
	delete bow;
	if (!has_bow)
		return (FT_ERR_NOT_FOUND);
	uint64_t inventory_revision = 0U;
	int32_t result = this->player_character_.get_inventory().get_revision(
		&inventory_revision);
	uint64_t request_id = 0U;
	if (result == FT_ERR_SUCCESS)
		result = this->transaction_journal_.next_request_id(
			WorldProjectileLaunchTransaction::journal_scope(0U), &request_id);
	if (result == FT_ERR_SUCCESS)
		result = this->launch_arrow(request_id, inventory_revision);
	return (result);
}

int32_t GameSession::attack_entity(uint64_t request_id,
	uint32_t target_entity_id, uint64_t expected_entity_revision,
	MinecraftCombatActionResult *result_out) noexcept
{
	return (this->attack_entity_internal(0U, request_id, target_entity_id,
		expected_entity_revision, result_out));
}

int32_t GameSession::attack_entity_for_actor(uint64_t request_id,
	uint64_t authenticated_actor_id, uint32_t target_entity_id,
	uint64_t expected_entity_revision,
	MinecraftCombatActionResult *result_out) noexcept
{
	if (authenticated_actor_id == 0U
		|| this->combat_actor_authorizer_ == nullptr
		|| !this->combat_actor_authorizer_(authenticated_actor_id,
			this->combat_actor_authorizer_user_data_))
		return (FT_ERR_PERMISSION_DENIED);
	return (this->attack_entity_internal(authenticated_actor_id, request_id,
		target_entity_id, expected_entity_revision, result_out));
}

int32_t GameSession::attack_entity_internal(uint64_t actor_id,
	uint64_t request_id,
	uint32_t target_entity_id, uint64_t expected_entity_revision,
	MinecraftCombatActionResult *result_out) noexcept
{
	static const uint64_t attack_scope_salt = 0x4D454C4545415454ULL;
	static const uint32_t attack_type = 16U;
	static const uint32_t attack_version = 2U;
	static const uint32_t attack_payload_size = 68U;
	uint64_t attack_scope = attack_scope_salt ^ actor_id;
	GameTransactionJournal::Record existing = {};
	WorldEntitySnapshot target_before = {};
	WorldEntitySnapshot target_after = {};
	std::vector<uint8_t> journal_before;
	std::vector<uint8_t> payload;
	uint64_t sequence = 0U;
	bool replayed = false;
	int32_t old_modifier_id = 0;
	int32_t old_modifier_value = 0;
	if (!this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (attack_scope == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (request_id == 0U || target_entity_id == 0U
		|| expected_entity_revision == 0U
		|| expected_entity_revision == UINT64_MAX || result_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->world_.world_revision().pending
		|| this->world_.world_revision().regenerating)
		return (FT_ERR_INVALID_OPERATION);
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	int32_t result = this->transaction_journal_.find(attack_scope, request_id,
		&existing);
	if (result == FT_ERR_SUCCESS)
	{
		const uint32_t payload_version = existing.payload.size() >= 8U
			? game_session_read_u32(existing.payload, 4U) : 0U;
		const bool valid_payload =
			(payload_version == 1U && existing.payload.size() == 60U)
			|| (payload_version == attack_version
				&& existing.payload.size() == attack_payload_size);
		if (existing.transaction_type != attack_type
			|| existing.state != GameTransactionJournal::COMMITTED
			|| !valid_payload
			|| existing.payload[0] != 'G' || existing.payload[1] != 'A'
			|| existing.payload[2] != 'T' || existing.payload[3] != 'K'
			|| game_session_read_u64(existing.payload, 8U) != request_id
			|| game_session_read_u32(existing.payload, 16U) != target_entity_id
			|| game_session_read_u64(existing.payload, 20U)
				!= expected_entity_revision)
			return (FT_ERR_INVALID_ARGUMENT);
		MinecraftCombatActionResult replay_result = {};
		replay_result.request_id = request_id;
		replay_result.target_entity_id = target_entity_id;
		replay_result.target_revision = game_session_read_u64(
			existing.payload, 48U);
		replay_result.target_health = game_session_read_i32(
			existing.payload, 44U);
		replay_result.damage = game_session_read_u32(existing.payload, 40U);
		replay_result.lethal = game_session_read_u32(existing.payload, 56U) != 0U;
		replay_result.replayed = true;
		*result_out = replay_result;
		return (FT_ERR_SUCCESS);
	}
	if (result != FT_ERR_NOT_FOUND)
		return (result);
	GameTransactionJournal::Record latest_attack = {};
	result = this->transaction_journal_.latest(attack_scope, attack_type,
		&latest_attack);
	if (result == FT_ERR_SUCCESS)
	{
		if (latest_attack.state != GameTransactionJournal::COMMITTED
			|| latest_attack.payload.size() < 60U
			|| latest_attack.payload[0] != 'G'
			|| latest_attack.payload[1] != 'A'
			|| latest_attack.payload[2] != 'T'
			|| latest_attack.payload[3] != 'K')
			return (FT_ERR_INVALID_STATE);
		const uint32_t latest_version = game_session_read_u32(
			latest_attack.payload, 4U);
		if (latest_version == attack_version
			&& latest_attack.payload.size() == attack_payload_size)
		{
			const uint64_t last_attack_tick = game_session_read_u64(
				latest_attack.payload, 60U);
			const uint64_t restored_cooldown_tick = last_attack_tick
				> UINT64_MAX - this->melee_attack_interval_ticks_ ? UINT64_MAX
				: last_attack_tick + this->melee_attack_interval_ticks_;
			if (restored_cooldown_tick > this->next_melee_attack_tick_)
				this->next_melee_attack_tick_ = restored_cooldown_tick;
		}
		else if (latest_version != 1U || latest_attack.payload.size() != 60U)
			return (FT_ERR_INVALID_STATE);
	}
	else if (result != FT_ERR_NOT_FOUND)
		return (result);
	if (!this->player_character_.is_alive())
		return (FT_ERR_INVALID_OPERATION);
	if (this->world_.current_tick < this->next_melee_attack_tick_)
		return (FT_ERR_INVALID_OPERATION);
	result = this->world_entities_.get(target_entity_id, &target_before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (target_before.revision != expected_entity_revision
		|| target_before.state.health <= 0
		|| target_before.state.action_state
			== static_cast<uint32_t>(EntityState::ActionState::DEAD))
		return (FT_ERR_INVALID_STATE);
	double direction_x = 0.0;
	double direction_y = 0.0;
	double direction_z = 0.0;
	PlayerController::camera_forward(this->camera_, &direction_x,
		&direction_y, &direction_z);
	const double offset_x = target_before.state.position_x - this->camera_.x;
	const double offset_y = target_before.state.position_y + 0.8 - this->camera_.y;
	const double offset_z = target_before.state.position_z - this->camera_.z;
	const double target_distance = std::sqrt(offset_x * offset_x
		+ offset_y * offset_y + offset_z * offset_z);
	if (!(target_distance > 0.05 && target_distance <= 3.5))
		return (FT_ERR_PERMISSION_DENIED);
	const double facing_dot = (offset_x * direction_x + offset_y * direction_y
		+ offset_z * direction_z) / target_distance;
	if (facing_dot < 0.573576436351046)
		return (FT_ERR_PERMISSION_DENIED);
	if (target_distance > 0.45)
	{
		int32_t block_x = 0;
		int32_t block_y = 0;
		int32_t block_z = 0;
		const int32_t ray_result = this->world_.raycast_solid(this->camera_.x,
			this->camera_.y, this->camera_.z, direction_x, direction_y,
			direction_z, target_distance - 0.4, &block_x, &block_y, &block_z);
		if (ray_result == FT_ERR_SUCCESS)
			return (FT_ERR_PERMISSION_DENIED);
		if (ray_result != FT_ERR_INVALID_ARGUMENT)
			return (ray_result);
	}
	ft_sharedptr<game_item> *weapon =
		this->player_character_.get_equipped_item(EQUIP_WEAPON);
	if (weapon == nullptr)
		return (this->player_character_.get_error());
	if (!(*weapon))
	{
		(void)weapon->destroy();
		delete weapon;
		return (FT_ERR_PERMISSION_DENIED);
	}
	const int32_t weapon_id = (*weapon)->get_item_id();
	const uint64_t weapon_instance_id = (*weapon)->get_instance_id();
	old_modifier_id = (*weapon)->get_modifier1_id();
	old_modifier_value = (*weapon)->get_modifier1_value();
	MinecraftEquipmentRule weapon_rule = {};
	result = this->query_main_hand_equipment_rule_cached(*(*weapon),
		&weapon_rule);
	uint32_t current_durability = weapon_rule.durability;
	if (result == FT_ERR_SUCCESS
		&& (weapon_rule.slot != MINECRAFT_EQUIPMENT_MAIN_HAND
			|| weapon_rule.attack_power == 0U
			|| weapon_rule.attack_power > static_cast<uint32_t>(INT32_MAX)
			|| weapon_instance_id == 0U))
		result = FT_ERR_INVALID_OPERATION;
	if (result == FT_ERR_SUCCESS && old_modifier_id != 0)
	{
		if (old_modifier_id != MinecraftItemCatalog::EQUIPMENT_DURABILITY_MODIFIER_ID
			|| old_modifier_value < 0
			|| static_cast<uint32_t>(old_modifier_value)
				> weapon_rule.durability)
			result = FT_ERR_INVALID_STATE;
		else
			current_durability = static_cast<uint32_t>(old_modifier_value);
	}
	if (result == FT_ERR_SUCCESS && current_durability == 0U)
		result = FT_ERR_INVALID_OPERATION;
	if (result != FT_ERR_SUCCESS)
	{
		(void)weapon->destroy();
		delete weapon;
		return (result);
	}
	try
	{
		payload.reserve(attack_payload_size);
		payload.insert(payload.end(), {'G', 'A', 'T', 'K'});
		game_session_append_u32(payload, attack_version);
		game_session_append_u64(payload, request_id);
		game_session_append_u32(payload, target_entity_id);
		game_session_append_u64(payload, expected_entity_revision);
		game_session_append_u32(payload, static_cast<uint32_t>(weapon_id));
		game_session_append_u64(payload, weapon_instance_id);
		game_session_append_u32(payload, weapon_rule.attack_power);
		const int32_t resulting_health = weapon_rule.attack_power
			>= static_cast<uint32_t>(target_before.state.health) ? 0
			: target_before.state.health
				- static_cast<int32_t>(weapon_rule.attack_power);
		game_session_append_u32(payload,
			static_cast<uint32_t>(resulting_health));
		game_session_append_u64(payload, expected_entity_revision + 1U);
		game_session_append_u32(payload, resulting_health == 0 ? 1U : 0U);
		game_session_append_u64(payload, this->world_.current_tick);
	}
	catch (...)
	{
		(void)weapon->destroy();
		delete weapon;
		return (FT_ERR_NO_MEMORY);
	}
	if (payload.size() != attack_payload_size)
		result = FT_ERR_INVALID_STATE;
	if (result == FT_ERR_SUCCESS)
		result = this->transaction_journal_.serialize(journal_before);
	if (result == FT_ERR_SUCCESS)
		result = this->transaction_journal_.prepare(attack_scope, request_id,
			attack_type, payload.data(), static_cast<uint32_t>(payload.size()),
			&sequence, &replayed);
	if (result == FT_ERR_SUCCESS)
		result = this->world_entities_.apply_damage(target_entity_id,
			expected_entity_revision,
			static_cast<int32_t>(weapon_rule.attack_power), &target_after);
	if (result == FT_ERR_SUCCESS)
	{
		(*weapon)->set_modifier1_id(
			MinecraftItemCatalog::EQUIPMENT_DURABILITY_MODIFIER_ID);
		(*weapon)->set_modifier1_value(current_durability > 1U
			? static_cast<int32_t>(current_durability - 1U) : 0);
		result = this->transaction_journal_.mark_committed(attack_scope,
			request_id);
	}
	if (result == FT_ERR_SUCCESS)
		result = this->save_session_snapshot();
	if (result != FT_ERR_SUCCESS && !journal_before.empty())
	{
		int32_t rollback_result = FT_ERR_SUCCESS;
		if (target_after.revision != 0U)
			rollback_result = this->world_entities_.rollback_update(
				target_before, target_after.revision);
		(*weapon)->set_modifier1_id(old_modifier_id);
		(*weapon)->set_modifier1_value(old_modifier_value);
		const int32_t journal_rollback = this->transaction_journal_.deserialize(
			journal_before.data(), journal_before.size());
		if (rollback_result == FT_ERR_SUCCESS
			&& journal_rollback != FT_ERR_SUCCESS)
			rollback_result = journal_rollback;
		if (rollback_result != FT_ERR_SUCCESS)
			result = rollback_result;
	}
	if (result == FT_ERR_SUCCESS)
	{
		this->next_melee_attack_tick_ =
			this->world_.current_tick > UINT64_MAX
				- this->melee_attack_interval_ticks_
			? UINT64_MAX : this->world_.current_tick
				+ this->melee_attack_interval_ticks_;
		MinecraftCombatActionResult action_result = {};
		action_result.request_id = request_id;
		action_result.target_entity_id = target_entity_id;
		action_result.target_revision = target_after.revision;
		action_result.target_health = target_after.state.health;
		action_result.damage = weapon_rule.attack_power;
		action_result.lethal = target_after.state.health == 0;
		action_result.replayed = false;
		*result_out = action_result;
	}
	(void)weapon->destroy();
	delete weapon;
	(void)sequence;
	(void)replayed;
	return (result);
}

int32_t GameSession::try_attack_aimed_entity() noexcept
{
	static const uint64_t local_attack_scope = 0x4D454C4545415454ULL;
	std::vector<WorldEntitySnapshot> nearby_entities;
	WorldEntitySnapshot *target = nullptr;
	double direction_x = 0.0;
	double direction_y = 0.0;
	double direction_z = 0.0;
	double nearest_projection = 3.5;
	int32_t result = this->world_entities_.query_radius(this->camera_.x,
		this->camera_.y, this->camera_.z, 3.5, 128U, nearby_entities);
	if (result != FT_ERR_SUCCESS)
		return (result);
	PlayerController::camera_forward(this->camera_, &direction_x,
		&direction_y, &direction_z);
	for (WorldEntitySnapshot &candidate : nearby_entities)
	{
		if (candidate.state.health <= 0 || candidate.item_instance_id != 0U
			|| candidate.state.type
				== static_cast<uint32_t>(EntityState::Kind::PLAYER)
			|| candidate.state.type
				== static_cast<uint32_t>(EntityState::Kind::BOAT)
			|| candidate.state.action_state
				== static_cast<uint32_t>(EntityState::ActionState::DEAD))
			continue ;
		const double offset_x = candidate.state.position_x - this->camera_.x;
		const double offset_y = candidate.state.position_y + 0.8 - this->camera_.y;
		const double offset_z = candidate.state.position_z - this->camera_.z;
		const double projection = offset_x * direction_x
			+ offset_y * direction_y + offset_z * direction_z;
		if (!(projection > 0.0 && projection < nearest_projection))
			continue ;
		const double offset_squared = offset_x * offset_x + offset_y * offset_y
			+ offset_z * offset_z;
		if (offset_squared > 12.25)
			continue ;
		const double perpendicular_squared = std::max(0.0,
			offset_squared - projection * projection);
		if (perpendicular_squared > 0.3025)
			continue ;
		nearest_projection = projection;
		target = &candidate;
	}
	if (target == nullptr)
		return (FT_ERR_NOT_FOUND);
	if (nearest_projection > 0.4)
	{
		int32_t block_x = 0;
		int32_t block_y = 0;
		int32_t block_z = 0;
		const int32_t ray_result = this->world_.raycast_solid(this->camera_.x,
			this->camera_.y, this->camera_.z, direction_x, direction_y,
			direction_z, nearest_projection - 0.4, &block_x, &block_y, &block_z);
		if (ray_result == FT_ERR_SUCCESS)
			return (FT_ERR_NOT_FOUND);
		if (ray_result != FT_ERR_INVALID_ARGUMENT)
			return (ray_result);
	}
	uint64_t request_id = 0U;
	result = this->transaction_journal_.next_request_id(local_attack_scope,
		&request_id);
	if (result != FT_ERR_SUCCESS)
		return (result);
	MinecraftCombatActionResult action_result = {};
	return (this->attack_entity(request_id, target->state.id,
		target->revision, &action_result));
}

int32_t GameSession::set_shield_guarding(bool guarding) noexcept
{
	if (!this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (!guarding)
	{
		if (this->shield_guard_active_)
		{
			this->shield_guard_active_ = false;
			this->shield_guard_until_tick_ = 0U;
			this->shield_guard_cooldown_until_tick_ =
				this->world_.current_tick > UINT64_MAX - 20U
					? UINT64_MAX : this->world_.current_tick + 20U;
		}
		return (FT_ERR_SUCCESS);
	}
	if (this->shield_guard_active_
		&& this->world_.current_tick < this->shield_guard_until_tick_)
		return (FT_ERR_SUCCESS);
	if (this->world_.current_tick < this->shield_guard_cooldown_until_tick_)
		return (FT_ERR_INVALID_OPERATION);
	ft_sharedptr<game_item> *shield = this->player_character_.get_equipped_item(
		EQUIP_OFF_HAND);
	if (shield == nullptr)
		return (this->player_character_.get_error());
	int32_t result = FT_ERR_NOT_FOUND;
	if (*shield)
	{
		MinecraftEquipmentRule rule = {};
		game_inventory_item_snapshot snapshot = {};
		uint32_t durability = 0U;
		uint32_t maximum_durability = 0U;
		result = this->item_catalog_.resolve_equipment_rule(
			(*shield)->get_item_id(), &rule);
		if (result == FT_ERR_SUCCESS
			&& rule.slot != MINECRAFT_EQUIPMENT_OFF_HAND)
			result = FT_ERR_INVALID_STATE;
		if (result == FT_ERR_SUCCESS)
		{
			snapshot.item_id = (*shield)->get_item_id();
			snapshot.modifier1_id = (*shield)->get_modifier1_id();
			snapshot.modifier1_value = (*shield)->get_modifier1_value();
			result = this->item_catalog_.query_equipment_durability(snapshot,
				&durability, &maximum_durability);
		}
		if (result == FT_ERR_SUCCESS && durability == 0U)
			result = FT_ERR_INVALID_OPERATION;
	}
	(void)shield->destroy();
	delete shield;
	if (result != FT_ERR_SUCCESS)
		return (result);
	this->shield_guard_active_ = true;
	this->shield_guard_until_tick_ = this->world_.current_tick > UINT64_MAX - 40U
		? UINT64_MAX : this->world_.current_tick + 40U;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::start_recipe_processing(uint64_t request_id,
	uint32_t recipe_id, int32_t batch_count,
	uint64_t expected_inventory_revision,
	int32_t station_x, int32_t station_y, int32_t station_z,
	uint64_t expected_station_revision,
	MinecraftProcessingActionResult *result_out) noexcept
{
	const GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};

	if (!this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (result_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (request_id == 0U || recipe_id == 0U || batch_count <= 0
		|| expected_station_revision == 0U
		|| this->processing_station_authorizer_ == nullptr
		|| !this->processing_station_authorizer_(
			static_cast<uint32_t>(MinecraftRecipeCatalog::FURNACE_STATION_ID),
			station_x, station_y, station_z, expected_station_revision,
			this->camera_.x, this->camera_.y, this->camera_.z,
			this->processing_station_authorizer_user_data_))
		return (FT_ERR_PERMISSION_DENIED);
	return (WorldItemProcessingTransaction::execute(
		this->world_item_save_path_, this->player_character_,
		this->world_items_, this->recipe_catalog_, this->transaction_journal_,
		request_id, recipe_id, batch_count, expected_inventory_revision,
		station_x, station_y, station_z, expected_station_revision,
		result_out, &snapshot_writer));
}

int32_t GameSession::till_farmland(uint64_t request_id, int32_t inventory_slot,
	uint64_t expected_inventory_revision, int32_t world_x, int32_t world_y,
	int32_t world_z, uint64_t expected_voxel_revision) noexcept
{
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};
	GameTransactionJournal::Record existing = {};
	int32_t result;

	if (!this->active_ || request_id == 0U || inventory_slot < 0
		|| expected_inventory_revision == 0U || expected_voxel_revision == 0U
		|| world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT)
		return (FT_ERR_INVALID_ARGUMENT);
	result = this->transaction_journal_.find(
		WorldFarmlandTransaction::journal_scope(), request_id, &existing);
	if (result != FT_ERR_SUCCESS && result != FT_ERR_NOT_FOUND)
		return (result);
	if (result == FT_ERR_SUCCESS
		&& (existing.state == GameTransactionJournal::COMMITTED
			|| existing.state == GameTransactionJournal::PREPARED))
		return (WorldFarmlandTransaction::execute(this->world_item_save_path_,
			this->player_character_, this->world_, this->transaction_journal_,
			request_id, inventory_slot, expected_inventory_revision, world_x,
			world_y, world_z, expected_voxel_revision, &snapshot_writer,
			&this->item_catalog_));
	uint32_t hit_block_id = GAME_VOXEL_AIR_BLOCK;
	int32_t hit_x;
	int32_t hit_y;
	int32_t hit_z;
	int32_t place_x;
	int32_t place_y;
	int32_t place_z;
	double direction_x;
	double direction_y;
	double direction_z;
	PlayerController::camera_forward(this->camera_, &direction_x,
		&direction_y, &direction_z);
	result = this->world_.raycast_edit_target(this->camera_.x,
		this->camera_.y, this->camera_.z, direction_x, direction_y,
		direction_z, 6.0, &hit_x, &hit_y, &hit_z, &place_x, &place_y,
		&place_z, &hit_block_id);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (hit_x != world_x || hit_y != world_y || hit_z != world_z
		|| (hit_block_id != VOXEL_GENERATOR_DIRT_BLOCK_ID
			&& hit_block_id != VOXEL_GENERATOR_GRASS_BLOCK_ID))
		return (FT_ERR_PERMISSION_DENIED);
	const double dx = this->camera_.x - (static_cast<double>(world_x) + 0.5);
	const double dy = this->camera_.y - (static_cast<double>(world_y) + 0.5);
	const double dz = this->camera_.z - (static_cast<double>(world_z) + 0.5);
	if (dx * dx + dy * dy + dz * dz > 36.0)
		return (FT_ERR_OUT_OF_RANGE);
	const int32_t chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	if (this->world_.is_chunk_protected(chunk_x, chunk_z))
		return (FT_ERR_PERMISSION_DENIED);
	const WorldChunk *chunk = this->world_.find_chunk(chunk_x, chunk_z);
	if (chunk == nullptr)
		return (FT_ERR_NOT_FOUND);
	if (chunk->voxel_revision != expected_voxel_revision)
		return (FT_ERR_INVALID_STATE);
	return (WorldFarmlandTransaction::execute(this->world_item_save_path_,
		this->player_character_, this->world_, this->transaction_journal_,
		request_id, inventory_slot, expected_inventory_revision, world_x,
		world_y, world_z, expected_voxel_revision, &snapshot_writer,
		&this->item_catalog_));
}
int32_t GameSession::place_inventory_block(uint64_t request_id,
	int32_t inventory_slot, int32_t item_id,
	uint64_t expected_inventory_revision, int32_t world_x, int32_t world_y,
	int32_t world_z, uint64_t expected_voxel_revision) noexcept
{
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	game_inventory &inventory = this->player_character_.get_inventory();
	game_inventory_snapshot inventory_before = {};
	game_inventory_snapshot inventory_after = {};
	MinecraftBlockItemRule block_rule = {};
	std::vector<uint8_t> payload;
	std::vector<uint8_t> journal_before;
	uint64_t journal_sequence = 0U;
	uint64_t voxel_revision = 0U;
	uint32_t block_id = 0U;
	uint32_t current_block_id = 0U;
	uint32_t hit_block_id = 0U;
	int32_t chunk_x;
	int32_t chunk_z;
	int32_t hit_x;
	int32_t hit_y;
	int32_t hit_z;
	int32_t place_x;
	int32_t place_y;
	int32_t place_z;
	int32_t result;
	bool replayed = false;
	bool inventory_changed = false;
	bool world_changed = false;
	GameTransactionJournal::Record existing_record = {};

	if (!this->active_ || request_id == 0U || inventory_slot < 0
		|| item_id <= 0 || expected_voxel_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	result = this->item_catalog_.resolve_placed_block_item(item_id, &block_id);
	if (result != FT_ERR_SUCCESS)
		return (result);
	try
	{
		payload.reserve(BLOCK_PLACEMENT_PAYLOAD_SIZE);
		payload.insert(payload.end(), {'G', 'B', 'P', 'L'});
		game_session_append_u32(payload, 1U);
		game_session_append_u64(payload, request_id);
		game_session_append_u32(payload,
			static_cast<uint32_t>(inventory_slot));
		game_session_append_u32(payload, static_cast<uint32_t>(item_id));
		game_session_append_u32(payload, block_id);
		game_session_append_u64(payload, expected_inventory_revision);
		game_session_append_u32(payload, static_cast<uint32_t>(world_x));
		game_session_append_u32(payload, static_cast<uint32_t>(world_y));
		game_session_append_u32(payload, static_cast<uint32_t>(world_z));
		game_session_append_u64(payload, expected_voxel_revision);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	if (payload.size() != BLOCK_PLACEMENT_PAYLOAD_SIZE)
		return (FT_ERR_INVALID_STATE);
	result = this->transaction_journal_.find(BLOCK_PLACEMENT_SCOPE,
		request_id, &existing_record);
	if (result == FT_ERR_SUCCESS)
	{
		if (existing_record.transaction_type != BLOCK_PLACEMENT_TRANSACTION_TYPE
			|| existing_record.payload != payload)
			return (FT_ERR_INVALID_ARGUMENT);
		if (existing_record.state == GameTransactionJournal::COMMITTED)
			return (FT_ERR_SUCCESS);
		if (existing_record.state == GameTransactionJournal::ABORTED)
			return (FT_ERR_INVALID_STATE);
		if (existing_record.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_STATE);
	}
	else if (result != FT_ERR_NOT_FOUND)
		return (result);
	{
		double direction_x;
		double direction_y;
		double direction_z;
		PlayerController::camera_forward(this->camera_, &direction_x,
			&direction_y, &direction_z);
		result = this->world_.raycast_edit_target(this->camera_.x,
			this->camera_.y, this->camera_.z, direction_x, direction_y,
			direction_z, 10.0, &hit_x, &hit_y, &hit_z, &place_x, &place_y,
			&place_z, &hit_block_id);
	}
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (place_x != world_x || place_y != world_y || place_z != world_z
		|| hit_block_id == GAME_VOXEL_AIR_BLOCK
		|| PlayerController::block_overlaps_player(this->camera_, world_x,
			world_y, world_z))
		return (FT_ERR_PERMISSION_DENIED);
	const double dx = this->camera_.x - (static_cast<double>(world_x) + 0.5);
	const double dy = this->camera_.y - (static_cast<double>(world_y) + 0.5);
	const double dz = this->camera_.z - (static_cast<double>(world_z) + 0.5);
	if (dx * dx + dy * dy + dz * dz > 100.0)
		return (FT_ERR_OUT_OF_RANGE);
	if (!this->world_.block_id_at(world_x, world_y, world_z,
			&current_block_id)
		|| current_block_id != GAME_VOXEL_AIR_BLOCK)
		return (FT_ERR_INVALID_STATE);
	if (block_id == VOXEL_GENERATOR_WHEAT_CROP_IMMATURE_BLOCK_ID)
	{
		uint32_t soil_block_id = GAME_VOXEL_AIR_BLOCK;
		if (world_y <= 0 || !this->world_.block_id_at(world_x, world_y - 1,
				world_z, &soil_block_id)
			|| soil_block_id != VOXEL_GENERATOR_FARMLAND_BLOCK_ID)
			return (FT_ERR_PERMISSION_DENIED);
	}
	else if (block_id == VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID)
	{
		uint32_t soil_block_id = GAME_VOXEL_AIR_BLOCK;
		if (world_y <= 0 || !this->world_.block_id_at(world_x, world_y - 1,
				world_z, &soil_block_id)
			|| (soil_block_id != VOXEL_GENERATOR_GRASS_BLOCK_ID
				&& soil_block_id != VOXEL_GENERATOR_DIRT_BLOCK_ID
				&& soil_block_id != VOXEL_GENERATOR_MUD_BLOCK_ID
				&& soil_block_id != VOXEL_GENERATOR_SAND_BLOCK_ID)
			|| !this->world_.has_water_source_near(world_x, world_y - 1,
				world_z))
			return (FT_ERR_PERMISSION_DENIED);
	}
	chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	World::RevisionProtectionInfo protection = {};
	result = this->world_.get_revision_protection_info(chunk_x, chunk_z,
		&protection);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (protection.loom_safe_zone || protection.manually_protected
		|| protection.occupied_player_exclusion
		|| (protection.generation_protected
			&& block_id != VOXEL_GENERATOR_WHEAT_CROP_IMMATURE_BLOCK_ID
			&& block_id != VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID))
		return (FT_ERR_PERMISSION_DENIED);
	const WorldChunk *chunk = this->world_.find_chunk(chunk_x, chunk_z);
	if (chunk == nullptr)
		return (FT_ERR_NOT_FOUND);
	voxel_revision = chunk->voxel_revision;
	if (voxel_revision != expected_voxel_revision)
		return (FT_ERR_INVALID_STATE);
	result = inventory.get_snapshot(inventory_before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (inventory_before.revision != expected_inventory_revision)
		return (FT_ERR_INVALID_STATE);
	auto inventory_item = std::find_if(inventory_before.items.begin(),
		inventory_before.items.end(), [inventory_slot](
			const game_inventory_item_snapshot &item)
		{
			return (item.slot == inventory_slot);
		});
	if (inventory_item == inventory_before.items.end()
		|| inventory_item->item_id != item_id)
		return (FT_ERR_NOT_FOUND);
	result = this->item_catalog_.resolve_block_item_rule(block_id, &block_rule);
	if (result != FT_ERR_SUCCESS
		|| (block_rule.policy != MINECRAFT_BLOCK_ACQUISITION_SELF_ITEM
			&& block_rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM)
		|| block_rule.item_id != item_id)
		return (result == FT_ERR_SUCCESS ? FT_ERR_PERMISSION_DENIED : result);
	result = game_session_stage_item_consumption(inventory_before,
		inventory_slot, item_id, expected_inventory_revision, inventory_after);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->transaction_journal_.serialize(journal_before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (existing_record.state != GameTransactionJournal::PREPARED)
	{
		result = this->transaction_journal_.prepare(BLOCK_PLACEMENT_SCOPE,
			request_id, BLOCK_PLACEMENT_TRANSACTION_TYPE, payload.data(),
			static_cast<uint32_t>(payload.size()), &journal_sequence, &replayed);
		(void)journal_sequence;
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	result = this->save_session_snapshot();
	if (result != FT_ERR_SUCCESS)
		goto rollback_block_placement;
	result = inventory.restore_snapshot(inventory_after);
	if (result == FT_ERR_SUCCESS)
		inventory_changed = true;
	if (result == FT_ERR_SUCCESS)
	{
		result = this->world_.replace_block_state_at(world_x, world_y, world_z,
			GAME_VOXEL_AIR_BLOCK, block_id, expected_voxel_revision);
		if (result == FT_ERR_SUCCESS)
			world_changed = true;
	}
	if (result == FT_ERR_SUCCESS)
		result = this->transaction_journal_.mark_committed(
			BLOCK_PLACEMENT_SCOPE, request_id);
	if (result == FT_ERR_SUCCESS)
		result = this->save_session_snapshot();
	if (result == FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);

rollback_block_placement:
	if (world_changed)
	{
		const WorldChunk *changed_chunk = this->world_.find_chunk(chunk_x,
			chunk_z);
		const int32_t rollback_result = changed_chunk == nullptr
			? FT_ERR_NOT_FOUND : this->world_.replace_block_state_at(world_x,
				world_y, world_z, block_id, GAME_VOXEL_AIR_BLOCK,
				changed_chunk->voxel_revision);
		if (rollback_result != FT_ERR_SUCCESS)
		{
			/* Keep the world and inventory on the committed side if the world
			 * participant cannot be reverted. The prepared save remains replayable
			 * if the following checkpoint also fails. */
			if (inventory_changed)
				(void)this->transaction_journal_.mark_committed(
					BLOCK_PLACEMENT_SCOPE, request_id);
			const int32_t checkpoint_result = this->save_session_snapshot();
			return (checkpoint_result == FT_ERR_SUCCESS
				? rollback_result : checkpoint_result);
		}
	}
	if (inventory_changed)
	{
		const int32_t inventory_rollback = inventory.restore_snapshot(
			inventory_before);
		if (inventory_rollback != FT_ERR_SUCCESS)
		{
			const WorldChunk *current_chunk = this->world_.find_chunk(chunk_x,
				chunk_z);
			const int32_t world_restore = current_chunk == nullptr
				? FT_ERR_NOT_FOUND : this->world_.replace_block_state_at(world_x,
					world_y, world_z, GAME_VOXEL_AIR_BLOCK, block_id,
					current_chunk->voxel_revision);
			if (world_restore == FT_ERR_SUCCESS)
			{
				(void)this->transaction_journal_.mark_committed(
					BLOCK_PLACEMENT_SCOPE, request_id);
				const int32_t checkpoint_result = this->save_session_snapshot();
				return (checkpoint_result == FT_ERR_SUCCESS
					? inventory_rollback : checkpoint_result);
			}
			return (world_restore);
		}
	}
	(void)this->transaction_journal_.deserialize(journal_before.data(),
		journal_before.size());
	(void)this->save_session_snapshot();
	return (result);
}

int32_t GameSession::transfer_fluid_bucket(uint64_t request_id,
	int32_t inventory_slot, uint64_t expected_inventory_revision,
	bool pickup_source, int32_t world_x, int32_t world_y, int32_t world_z,
	uint64_t expected_voxel_revision) noexcept
{
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	static const uint64_t FLUID_TRANSFER_SCOPE = 0x574F524C44464C44ULL;
	static const uint32_t FLUID_TRANSFER_TYPE = 7U;
	game_inventory &inventory = this->player_character_.get_inventory();
	game_inventory_snapshot inventory_before = {};
	game_inventory_snapshot inventory_after = {};
	std::vector<uint8_t> journal_before;
	std::vector<uint8_t> payload;
	MinecraftFluidContainerRule input_rule = {};
	uint32_t current_block_id = 0U;
	uint32_t replacement_block_id = GAME_VOXEL_AIR_BLOCK;
	uint32_t hit_block_id = 0U;
	MinecraftFluidKind transfer_fluid_kind = MINECRAFT_FLUID_NONE;
	uint64_t journal_sequence = 0U;
	uint64_t current_voxel_revision = 0U;
	uint64_t old_next_instance_id = 0U;
	uint64_t minted_instance_id = 0U;
	bool replayed = false;
	bool inventory_changed = false;
	bool world_changed = false;
	bool allocator_changed = false;
	double direction_x;
	double direction_y;
	double direction_z;
	int32_t hit_x;
	int32_t hit_y;
	int32_t hit_z;
	int32_t place_x;
	int32_t place_y;
	int32_t place_z;
	int32_t chunk_x;
	int32_t chunk_z;
	int32_t result;
	GameTransactionJournal::Record existing_record = {};

	if (!this->active_ || request_id == 0U || inventory_slot < 0
		|| expected_voxel_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		payload.reserve(52U);
		payload.insert(payload.end(), {'G', 'F', 'T', 'R'});
		game_session_append_u32(payload, 1U);
		game_session_append_u32(payload, pickup_source ? 1U : 2U);
		game_session_append_u64(payload, request_id);
		game_session_append_u32(payload, static_cast<uint32_t>(inventory_slot));
		game_session_append_u64(payload, expected_inventory_revision);
		game_session_append_u32(payload, static_cast<uint32_t>(world_x));
		game_session_append_u32(payload, static_cast<uint32_t>(world_y));
		game_session_append_u32(payload, static_cast<uint32_t>(world_z));
		game_session_append_u64(payload, expected_voxel_revision);
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	result = this->transaction_journal_.find(FLUID_TRANSFER_SCOPE,
		request_id, &existing_record);
	if (result == FT_ERR_SUCCESS)
	{
		if (existing_record.transaction_type != FLUID_TRANSFER_TYPE
			|| existing_record.payload != payload)
			return (FT_ERR_INVALID_ARGUMENT);
		if (existing_record.state == GameTransactionJournal::COMMITTED)
			return (FT_ERR_SUCCESS);
		if (existing_record.state == GameTransactionJournal::ABORTED)
			return (FT_ERR_INVALID_STATE);
	}
	else if (result != FT_ERR_NOT_FOUND)
		return (result);
	PlayerController::camera_forward(this->camera_, &direction_x,
		&direction_y, &direction_z);
	result = this->world_.raycast_edit_target(this->camera_.x,
		this->camera_.y, this->camera_.z, direction_x, direction_y,
		direction_z, 10.0, &hit_x, &hit_y, &hit_z, &place_x, &place_y,
		&place_z, &hit_block_id);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (pickup_source)
	{
		if (hit_x != world_x || hit_y != world_y || hit_z != world_z
			|| voxel_block_is_fluid_source(hit_block_id) != FT_TRUE)
			return (FT_ERR_PERMISSION_DENIED);
		current_block_id = hit_block_id;
		if (current_block_id == VOXEL_GENERATOR_WATER_BLOCK_ID)
			transfer_fluid_kind = MINECRAFT_FLUID_WATER;
		else if (current_block_id == VOXEL_GENERATOR_LAVA_BLOCK_ID)
			transfer_fluid_kind = MINECRAFT_FLUID_LAVA;
		else
			return (FT_ERR_PERMISSION_DENIED);
	}
	else
	{
		if (place_x != world_x || place_y != world_y || place_z != world_z
			|| hit_block_id == GAME_VOXEL_AIR_BLOCK)
			return (FT_ERR_PERMISSION_DENIED);
		current_block_id = GAME_VOXEL_AIR_BLOCK;
		if (!this->world_.block_id_at(world_x, world_y, world_z,
				&current_block_id)
			|| current_block_id != GAME_VOXEL_AIR_BLOCK)
			return (FT_ERR_INVALID_STATE);
	}
	const double dx = this->camera_.x - (static_cast<double>(world_x) + 0.5);
	const double dy = this->camera_.y - (static_cast<double>(world_y) + 0.5);
	const double dz = this->camera_.z - (static_cast<double>(world_z) + 0.5);
	if (dx * dx + dy * dy + dz * dz > 100.0)
		return (FT_ERR_OUT_OF_RANGE);
	chunk_x = WorldCoordinates::floor_divide(world_x,
		GAME_VOXEL_CHUNK_WIDTH);
	chunk_z = WorldCoordinates::floor_divide(world_z,
		GAME_VOXEL_CHUNK_DEPTH);
	if (this->world_.is_chunk_protected(chunk_x, chunk_z))
		return (FT_ERR_PERMISSION_DENIED);
	const WorldChunk *chunk = this->world_.find_chunk(chunk_x, chunk_z);
	if (chunk == nullptr)
		return (FT_ERR_NOT_FOUND);
	current_voxel_revision = chunk->voxel_revision;
	if (current_voxel_revision != expected_voxel_revision)
		return (FT_ERR_INVALID_STATE);
	if (!pickup_source)
	{
		if (hit_block_id == VOXEL_GENERATOR_WATER_BLOCK_ID)
			transfer_fluid_kind = MINECRAFT_FLUID_WATER;
		else if (hit_block_id == VOXEL_GENERATOR_LAVA_BLOCK_ID)
			transfer_fluid_kind = MINECRAFT_FLUID_LAVA;
		else
			return (FT_ERR_PERMISSION_DENIED);
	}
	result = inventory.get_snapshot(inventory_before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (inventory_before.revision != expected_inventory_revision)
		return (FT_ERR_INVALID_STATE);
	auto item_position = std::find_if(inventory_before.items.begin(),
		inventory_before.items.end(),
		[inventory_slot](const game_inventory_item_snapshot &entry)
		{
			return (entry.slot == inventory_slot);
		});
	if (item_position == inventory_before.items.end())
		return (FT_ERR_NOT_FOUND);
	result = this->item_catalog_.resolve_fluid_container_rule(
		item_position->item_id, &input_rule);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (pickup_source)
	{
		if (input_rule.fluid_kind != MINECRAFT_FLUID_NONE
			|| item_position->item_id != MinecraftItemCatalog::EMPTY_BUCKET_ITEM_ID)
			return (FT_ERR_INVALID_STATE);
	}
	else if (input_rule.capacity != 1U
		|| input_rule.fluid_kind != transfer_fluid_kind
		|| input_rule.fluid_kind == MINECRAFT_FLUID_NONE)
		return (FT_ERR_INVALID_STATE);
	if (pickup_source)
		replacement_block_id = GAME_VOXEL_AIR_BLOCK;
	result = this->item_catalog_.prepare_fluid_transfer_snapshot(
		inventory_before, inventory_slot, expected_inventory_revision,
		pickup_source, transfer_fluid_kind, inventory_after);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->transaction_journal_.serialize(journal_before);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = this->transaction_journal_.prepare(FLUID_TRANSFER_SCOPE,
		request_id, FLUID_TRANSFER_TYPE, payload.data(),
		static_cast<uint32_t>(payload.size()), &journal_sequence, &replayed);
	(void)journal_sequence;
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (replayed)
	{
		GameTransactionJournal::Record record = {};
		result = this->transaction_journal_.find(FLUID_TRANSFER_SCOPE,
			request_id, &record);
		if (result == FT_ERR_SUCCESS
			&& record.state == GameTransactionJournal::COMMITTED)
			return (FT_ERR_SUCCESS);
		if (result != FT_ERR_SUCCESS || record.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_STATE);
	}
	if (pickup_source && item_position->stack_size > 1)
	{
		old_next_instance_id = this->recipe_catalog_.next_output_instance_id();
		result = this->synchronize_item_instance_id_allocator();
		if (result == FT_ERR_SUCCESS)
		{
			allocator_changed = true;
			result = this->recipe_catalog_.allocate_output_instance_id(
				&minted_instance_id);
		}
		if (result == FT_ERR_SUCCESS)
		{
			bool assigned = false;
			for (game_inventory_item_snapshot &entry : inventory_after.items)
			{
				bool existed = false;
				for (const game_inventory_item_snapshot &previous
					: inventory_before.items)
					if (entry.slot == previous.slot)
					{
						existed = true;
						break ;
					}
				if (!existed && entry.item_id
					== (transfer_fluid_kind == MINECRAFT_FLUID_WATER
						? MinecraftItemCatalog::WATER_BUCKET_ITEM_ID
						: MinecraftItemCatalog::LAVA_BUCKET_ITEM_ID))
				{
					entry.instance_id = minted_instance_id;
					entry.max_stack = 1;
					assigned = true;
					break ;
				}
			}
			if (!assigned)
				result = FT_ERR_INVALID_STATE;
		}
	}
	if (result == FT_ERR_SUCCESS)
		result = inventory.restore_snapshot(inventory_after);
	if (result == FT_ERR_SUCCESS)
		inventory_changed = true;
	if (result == FT_ERR_SUCCESS)
	{
		result = this->world_.replace_fluid_source_at(world_x, world_y, world_z,
			current_block_id, replacement_block_id, expected_voxel_revision);
		if (result == FT_ERR_SUCCESS)
			world_changed = true;
	}
	if (result == FT_ERR_SUCCESS)
		result = this->transaction_journal_.mark_committed(
			FLUID_TRANSFER_SCOPE, request_id);
	if (result == FT_ERR_SUCCESS)
		result = this->transaction_journal_.prune_terminal_scope(
			FLUID_TRANSFER_SCOPE, request_id);
	if (result == FT_ERR_SUCCESS)
		result = this->save_session_snapshot();
	if (result == FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	if (world_changed)
	{
		const WorldChunk *changed_chunk = this->world_.find_chunk(chunk_x, chunk_z);
		if (changed_chunk != nullptr)
			(void)this->world_.replace_fluid_source_at(world_x, world_y, world_z,
				replacement_block_id, current_block_id,
				changed_chunk->voxel_revision);
	}
	if (inventory_changed)
		(void)inventory.restore_snapshot(inventory_before);
	(void)this->transaction_journal_.deserialize(journal_before.data(),
		journal_before.size());
	(void)this->world_.serialize_fluid_state(fluid_state_);
	if (allocator_changed)
		(void)this->recipe_catalog_.restore_next_output_instance_id(
			old_next_instance_id);
	return (result);
}

bool GameSession::is_active() const
{
	return (active_);
}

int32_t GameSession::place_boat_from_inventory(uint64_t request_id,
	int32_t inventory_slot, uint64_t expected_inventory_revision,
	int32_t water_x, int32_t water_y, int32_t water_z,
	uint64_t expected_voxel_revision,
	WorldBoatTransferTransaction::Result *result_out) noexcept
{
	return (this->place_boat_from_inventory_for_actor(request_id, 0U,
		inventory_slot, expected_inventory_revision, water_x, water_y, water_z,
		expected_voxel_revision, result_out));
}

int32_t GameSession::place_boat_from_inventory_for_actor(uint64_t request_id,
	uint64_t authenticated_actor_id, int32_t inventory_slot,
	uint64_t expected_inventory_revision, int32_t water_x, int32_t water_y,
	int32_t water_z, uint64_t expected_voxel_revision,
	WorldBoatTransferTransaction::Result *result_out) noexcept
{
	if (!this->active_ || result_out == nullptr || request_id == 0U
		|| authenticated_actor_id == 0U || inventory_slot < 0
		|| expected_inventory_revision == 0U
		|| expected_voxel_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->boat_actor_authorizer_ == nullptr
		|| !this->boat_actor_authorizer_(authenticated_actor_id,
			this->boat_actor_authorizer_user_data_))
		return (FT_ERR_PERMISSION_DENIED);
	EntityState boat_state;
	boat_state.id = 0U;
	boat_state.type = static_cast<uint32_t>(EntityState::Kind::BOAT);
	boat_state.position_x = static_cast<double>(water_x) + 0.5;
	boat_state.position_y = static_cast<double>(water_y) + 1.0;
	boat_state.position_z = static_cast<double>(water_z) + 0.5;
	boat_state.orientation_yaw = this->camera_.yaw;
	boat_state.orientation_pitch = 0.0;
	boat_state.health = 1;
	boat_state.action_state = static_cast<uint32_t>(
		EntityState::ActionState::IDLE);
	boat_state.tick = this->world_.current_tick;
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	const GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};
	GameTransactionJournal::Record prior = {};
	const int32_t prior_result = this->transaction_journal_.find(
		WorldBoatTransferTransaction::journal_scope(), request_id, &prior);
	if (prior_result == FT_ERR_SUCCESS
		&& prior.state == GameTransactionJournal::COMMITTED)
		return (WorldBoatTransferTransaction::place_for_actor(this->world_item_save_path_,
			this->player_character_, this->world_items_, this->world_entities_,
			this->transaction_journal_, request_id, authenticated_actor_id,
			inventory_slot,
			expected_inventory_revision, boat_state, &snapshot_writer,
			result_out));
	if (prior_result != FT_ERR_SUCCESS && prior_result != FT_ERR_NOT_FOUND)
		return (prior_result);
	if (water_y < 0 || water_y + 1 >= GAME_VOXEL_CHUNK_HEIGHT)
		return (FT_ERR_OUT_OF_RANGE);
	uint32_t water_block = 0U;
	uint32_t above_block = 0U;
	if (!this->world_.block_id_at(water_x, water_y, water_z, &water_block)
		|| water_block != VOXEL_GENERATOR_WATER_BLOCK_ID
		|| !this->world_.block_id_at(water_x, water_y + 1, water_z,
			&above_block)
		|| above_block != GAME_VOXEL_AIR_BLOCK)
		return (FT_ERR_PERMISSION_DENIED);
	const int32_t chunk_x = WorldCoordinates::floor_divide(water_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(water_z,
		GAME_VOXEL_CHUNK_DEPTH);
	if (this->world_.is_chunk_protected(chunk_x, chunk_z)
		|| this->world_.revision_barrier_blocks_chunk(chunk_x, chunk_z))
		return (FT_ERR_PERMISSION_DENIED);
	const WorldChunk *chunk = this->world_.find_chunk(chunk_x, chunk_z);
	if (chunk == nullptr)
		return (FT_ERR_NOT_FOUND);
	if (chunk->voxel_revision != expected_voxel_revision)
		return (FT_ERR_INVALID_STATE);
	const double boat_x = static_cast<double>(water_x) + 0.5;
	const double boat_y = static_cast<double>(water_y) + 1.0;
	const double boat_z = static_cast<double>(water_z) + 0.5;
	const double dx = this->camera_.x - boat_x;
	const double dy = this->camera_.y - boat_y;
	const double dz = this->camera_.z - boat_z;
	if (dx * dx + dy * dy + dz * dz > 36.0)
		return (FT_ERR_OUT_OF_RANGE);
	return (WorldBoatTransferTransaction::place_for_actor(this->world_item_save_path_,
		this->player_character_, this->world_items_, this->world_entities_,
		this->transaction_journal_, request_id, authenticated_actor_id,
		inventory_slot,
		expected_inventory_revision, boat_state, &snapshot_writer, result_out));
}

int32_t GameSession::retrieve_boat_to_inventory(uint64_t request_id,
	uint32_t entity_id, uint64_t expected_entity_revision,
	uint64_t expected_inventory_revision,
	WorldBoatTransferTransaction::Result *result_out) noexcept
{
	return (this->retrieve_boat_to_inventory_for_actor(request_id, 0U,
		entity_id, expected_entity_revision, expected_inventory_revision,
		result_out));
}

int32_t GameSession::retrieve_boat_to_inventory_for_actor(
	uint64_t request_id, uint64_t authenticated_actor_id,
	uint32_t entity_id, uint64_t expected_entity_revision,
	uint64_t expected_inventory_revision,
	WorldBoatTransferTransaction::Result *result_out) noexcept
{
	if (!this->active_ || result_out == nullptr || request_id == 0U
		|| authenticated_actor_id == 0U || entity_id == 0U
		|| expected_entity_revision == 0U
		|| expected_inventory_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->boat_actor_authorizer_ == nullptr
		|| !this->boat_actor_authorizer_(authenticated_actor_id,
			this->boat_actor_authorizer_user_data_))
		return (FT_ERR_PERMISSION_DENIED);
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	const GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};
	GameTransactionJournal::Record prior = {};
	const int32_t prior_result = this->transaction_journal_.find(
		WorldBoatTransferTransaction::journal_scope(), request_id, &prior);
	if (prior_result == FT_ERR_SUCCESS
		&& prior.state == GameTransactionJournal::COMMITTED)
		return (WorldBoatTransferTransaction::retrieve_for_actor(
			this->world_item_save_path_, this->player_character_,
			this->world_items_, this->world_entities_, this->item_catalog_,
			this->transaction_journal_, request_id, authenticated_actor_id,
			entity_id,
			expected_entity_revision, expected_inventory_revision,
			&snapshot_writer, result_out));
	if (prior_result != FT_ERR_SUCCESS && prior_result != FT_ERR_NOT_FOUND)
		return (prior_result);
	WorldEntitySnapshot boat = {};
	int32_t result = this->world_entities_.get(entity_id, &boat);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (boat.revision != expected_entity_revision
		|| boat.state.type != static_cast<uint32_t>(EntityState::Kind::BOAT)
		|| boat.item_instance_id == 0U)
		return (FT_ERR_INVALID_STATE);
	const double dx = this->camera_.x - boat.state.position_x;
	const double dy = this->camera_.y - boat.state.position_y;
	const double dz = this->camera_.z - boat.state.position_z;
	if (dx * dx + dy * dy + dz * dz > 36.0)
		return (FT_ERR_OUT_OF_RANGE);
	if (boat.state.position_x < static_cast<double>(INT32_MIN)
		|| boat.state.position_x > static_cast<double>(INT32_MAX)
		|| boat.state.position_z < static_cast<double>(INT32_MIN)
		|| boat.state.position_z > static_cast<double>(INT32_MAX))
		return (FT_ERR_OUT_OF_RANGE);
	const int32_t block_x = static_cast<int32_t>(std::floor(
		boat.state.position_x));
	const int32_t block_z = static_cast<int32_t>(std::floor(
		boat.state.position_z));
	const int32_t chunk_x = WorldCoordinates::floor_divide(block_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(block_z,
		GAME_VOXEL_CHUNK_DEPTH);
	if (this->world_.is_chunk_protected(chunk_x, chunk_z))
		return (FT_ERR_PERMISSION_DENIED);
	return (WorldBoatTransferTransaction::retrieve_for_actor(this->world_item_save_path_,
		this->player_character_, this->world_items_, this->world_entities_,
		this->item_catalog_, this->transaction_journal_, request_id,
		authenticated_actor_id, entity_id, expected_entity_revision,
		expected_inventory_revision, &snapshot_writer, result_out));
}

int32_t GameSession::move_boat_for_actor(uint64_t request_id,
	uint64_t authenticated_actor_id, uint32_t entity_id,
	uint64_t expected_entity_revision, double next_x, double next_z,
	double yaw, uint64_t *new_entity_revision_out) noexcept
{
	if (new_entity_revision_out != nullptr)
		*new_entity_revision_out = 0U;
	if (!this->active_ || request_id == 0U || authenticated_actor_id == 0U
		|| entity_id == 0U || expected_entity_revision == 0U
		|| new_entity_revision_out == nullptr || !std::isfinite(next_x)
		|| !std::isfinite(next_z) || !std::isfinite(yaw))
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->boat_actor_authorizer_ == nullptr
		|| !this->boat_actor_authorizer_(authenticated_actor_id,
			this->boat_actor_authorizer_user_data_))
		return (FT_ERR_PERMISSION_DENIED);
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	WorldEntitySnapshot boat = {};
	int32_t result = this->world_entities_.get(entity_id, &boat);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (boat.state.type != static_cast<uint32_t>(EntityState::Kind::BOAT)
		|| boat.item_instance_id == 0U)
		return (FT_ERR_INVALID_STATE);
	if (boat.owner_actor_id != authenticated_actor_id)
		return (FT_ERR_PERMISSION_DENIED);
	if (boat.revision != expected_entity_revision)
	{
		EntityState retry_state = boat.state;
		retry_state.position_x = next_x;
		retry_state.position_z = next_z;
		retry_state.orientation_yaw = yaw;
		const GameSessionSnapshotWriter retry_writer = {
			&GameSession::save_snapshot_callback, this};
		return (WorldBoatTransferTransaction::move_for_actor(
			this->world_item_save_path_, this->world_entities_,
			this->transaction_journal_, request_id, authenticated_actor_id,
			entity_id, expected_entity_revision, retry_state, &retry_writer,
			new_entity_revision_out));
	}
	const double move_x = next_x - boat.state.position_x;
	const double move_z = next_z - boat.state.position_z;
	if (move_x * move_x + move_z * move_z > 1.000001)
		return (FT_ERR_OUT_OF_RANGE);
	if (next_x < static_cast<double>(INT32_MIN)
		|| next_x > static_cast<double>(INT32_MAX)
		|| next_z < static_cast<double>(INT32_MIN)
		|| next_z > static_cast<double>(INT32_MAX)
		|| boat.state.position_y < 1.0
		|| boat.state.position_y >= GAME_VOXEL_CHUNK_HEIGHT)
		return (FT_ERR_OUT_OF_RANGE);
	const int32_t block_x = static_cast<int32_t>(std::floor(next_x));
	const int32_t block_y = static_cast<int32_t>(std::floor(
		boat.state.position_y));
	const int32_t block_z = static_cast<int32_t>(std::floor(next_z));
	const int32_t chunk_x = WorldCoordinates::floor_divide(block_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(block_z,
		GAME_VOXEL_CHUNK_DEPTH);
	if (this->world_.is_chunk_protected(chunk_x, chunk_z)
		|| this->world_.revision_barrier_blocks_chunk(chunk_x, chunk_z))
		return (FT_ERR_PERMISSION_DENIED);
	uint32_t water_block = 0U;
	uint32_t above_block = 0U;
	if (!this->world_.block_id_at(block_x, block_y - 1, block_z,
			&water_block)
		|| water_block != VOXEL_GENERATOR_WATER_BLOCK_ID
		|| !this->world_.block_id_at(block_x, block_y, block_z, &above_block)
		|| above_block != GAME_VOXEL_AIR_BLOCK)
		return (FT_ERR_PERMISSION_DENIED);
	const double player_dx = this->camera_.x - boat.state.position_x;
	const double player_dy = this->camera_.y - boat.state.position_y;
	const double player_dz = this->camera_.z - boat.state.position_z;
	if (player_dx * player_dx + player_dy * player_dy
		+ player_dz * player_dz > 36.0)
		return (FT_ERR_OUT_OF_RANGE);
	EntityState next_state = boat.state;
	next_state.position_x = next_x;
	next_state.position_z = next_z;
	next_state.velocity_x = move_x;
	next_state.velocity_z = move_z;
	next_state.orientation_yaw = yaw;
	next_state.tick = this->world_.current_tick;
	const GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};
	return (WorldBoatTransferTransaction::move_for_actor(
		this->world_item_save_path_, this->world_entities_,
		this->transaction_journal_, request_id, authenticated_actor_id,
		entity_id, expected_entity_revision, next_state, &snapshot_writer,
		new_entity_revision_out));
}

void GameSession::activate_configured_render_distance()
{
	if (!active_)
		return ;
	active_render_distance_ = Settings::instance().render_distance();
}

bool GameSession::is_ready_to_play() const
{
	/* Loading readiness is deliberately based on the minimum playable area,
	 * not the complete render-distance envelope. The persistent stream keeps
	 * filling distant chunks after gameplay begins. */
	const int32_t radius = WorldCoordinates::render_distance_to_chunk_radius(
		WorldCoordinates::MIN_RENDER_DISTANCE);
	const int32_t radius_sq = radius * radius;
	int32_t ready_chunks = 0;
	int32_t required_chunks = 0;

	if (!active_)
		return (false);
	for (int32_t z = -radius; z <= radius; ++z)
	{
		for (int32_t x = -radius; x <= radius; ++x)
		{
			if ((x * x) + (z * z) <= radius_sq)
			{
				const WorldChunk *chunk = world_.find_chunk(
					world_.center_chunk_x + x,
					world_.center_chunk_z + z);
				required_chunks += 1;
				/* A published chunk is not playable until its worker-produced
				 * mesh is complete. Counting only the chunk object lets loading
				 * finish while the renderer still has no geometry to draw. */
				if (chunk != nullptr
					&& game_session_mesh_is_drawable(chunk->mesh))
					ready_chunks += 1;
			}
		}
	}
	return (ready_chunks == required_chunks);
}

int GameSession::error_code() const
{
	return (error_code_);
}

int32_t GameSession::configure_regeneration_script_catalog(
	const uint8_t *bytes, uint32_t byte_count,
	uint64_t trusted_manifest_hash) noexcept
{
	std::vector<uint8_t> staged;

	if (active_ || bytes == nullptr || byte_count == 0U
		|| byte_count > 64U * 1024U * 1024U
		|| trusted_manifest_hash == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (!GameSession::authorize_script_catalog(
			this->script_catalog_authorizer_, bytes, byte_count,
			trusted_manifest_hash,
			this->script_catalog_authorizer_user_data_))
		return (FT_ERR_INVALID_OPERATION);
	try
	{
		staged.assign(bytes, bytes + byte_count);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	regeneration_script_catalog_.swap(staged);
	trusted_script_manifest_hash_ = trusted_manifest_hash;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_script_catalog_authorizer(
	ScriptCatalogAuthorizer authorizer, void *user_data) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (authorizer == nullptr && user_data != nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->script_catalog_authorizer_ = authorizer;
	this->script_catalog_authorizer_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_script_context_authorizer(
	ScriptContextAuthorizer authorizer, void *user_data) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if ((authorizer == nullptr && user_data != nullptr)
		|| (authorizer == nullptr && !this->_regeneration_script_context.empty()))
		return (FT_ERR_INVALID_ARGUMENT);
	this->_script_context_authorizer = authorizer;
	this->_script_context_authorizer_user_data = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_regeneration_script_context(
	const uint32_t *key_ids, const int64_t *values,
	uint32_t entry_count) noexcept
{
	std::vector<std::pair<uint32_t, int64_t>> staged;
	std::vector<ft_bool> staged_is_set;
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (entry_count > 64U
		|| (entry_count != 0U && (key_ids == nullptr || values == nullptr)))
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		staged.reserve(entry_count);
		staged_is_set.assign(entry_count, FT_TRUE);
		uint32_t index = 0U;
		while (index < entry_count)
		{
			if (key_ids[index] == 0U
				|| (index != 0U && key_ids[index] <= key_ids[index - 1U]))
				return (FT_ERR_INVALID_ARGUMENT);
			if (this->_script_context_authorizer == nullptr
				|| this->_script_context_authorizer(key_ids[index], values[index],
					FT_TRUE, this->_script_context_authorizer_user_data) != FT_TRUE)
				return (FT_ERR_PERMISSION_DENIED);
			staged.emplace_back(key_ids[index], values[index]);
			++index;
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	this->_regeneration_script_context.swap(staged);
	this->_regeneration_script_context_is_set.swap(staged_is_set);
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::set_regeneration_script_context_value(uint32_t key_id,
	int64_t value) noexcept
{
	std::size_t index = 0U;
	while (index < this->_regeneration_script_context.size())
	{
		auto &entry = this->_regeneration_script_context[index];
		if (entry.first == key_id)
		{
			if (this->_script_context_authorizer == nullptr
				|| this->_script_context_authorizer(key_id, value, FT_TRUE,
					this->_script_context_authorizer_user_data) != FT_TRUE)
				return (FT_ERR_PERMISSION_DENIED);
			const int64_t previous_value = entry.second;
			const ft_bool was_set = this->_regeneration_script_context_is_set[index];
			if (this->active_)
			{
				const int32_t result = this->regeneration_cards_.set_script_context_value(
					key_id, value);
				if (result != FT_ERR_SUCCESS)
					return (result);
			}
			entry.second = value;
			this->_regeneration_script_context_is_set[index] = FT_TRUE;
			if (this->active_)
			{
				const int32_t result = this->save_session_snapshot();
				if (result != FT_ERR_SUCCESS)
				{
					if (was_set == FT_TRUE)
						(void)this->regeneration_cards_.set_script_context_value(
							key_id, previous_value);
					else
						(void)this->regeneration_cards_.clear_script_context_value(key_id);
					entry.second = previous_value;
					this->_regeneration_script_context_is_set[index] = was_set;
					return (result);
				}
			}
			return (FT_ERR_SUCCESS);
		}
		++index;
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t GameSession::clear_regeneration_script_context_value(uint32_t key_id) noexcept
{
	std::size_t index = 0U;
	while (index < this->_regeneration_script_context.size())
	{
		const auto &entry = this->_regeneration_script_context[index];
		if (entry.first == key_id)
		{
			if (this->_script_context_authorizer == nullptr
				|| this->_script_context_authorizer(key_id, entry.second, FT_FALSE,
					this->_script_context_authorizer_user_data) != FT_TRUE)
				return (FT_ERR_PERMISSION_DENIED);
			const ft_bool was_set = this->_regeneration_script_context_is_set[index];
			if (this->active_)
			{
				const int32_t result = this->regeneration_cards_.clear_script_context_value(key_id);
				if (result != FT_ERR_SUCCESS)
					return (result);
			}
			this->_regeneration_script_context_is_set[index] = FT_FALSE;
			if (this->active_)
			{
				const int32_t result = this->save_session_snapshot();
				if (result != FT_ERR_SUCCESS)
				{
					if (was_set == FT_TRUE)
						(void)this->regeneration_cards_.set_script_context_value(
							key_id, entry.second);
					this->_regeneration_script_context_is_set[index] = was_set;
					return (result);
				}
			}
			return (FT_ERR_SUCCESS);
		}
		++index;
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t GameSession::configure_chunk_instability_policy(
	const WorldChunkInstabilityPolicy &policy) noexcept
{
	WorldChunkInstabilityStore validator;
	const int32_t result = validator.configure(policy);

	if (active_)
		return (FT_ERR_INVALID_OPERATION);
	if (ore_overgrowth_instability_delta_ > 0
		&& static_cast<uint32_t>(ore_overgrowth_instability_delta_)
			> policy.maximum_value)
		return (FT_ERR_OUT_OF_RANGE);
	if (ultimate_greed_instability_delta_ > 0
		&& static_cast<uint32_t>(ultimate_greed_instability_delta_)
			> policy.maximum_value)
		return (FT_ERR_OUT_OF_RANGE);
	if (biome_rupture_instability_delta_ > 0
		&& static_cast<uint32_t>(biome_rupture_instability_delta_)
			> policy.maximum_value)
		return (FT_ERR_OUT_OF_RANGE);
	if (volcano_mark_instability_delta_ > 0
		&& static_cast<uint32_t>(volcano_mark_instability_delta_)
			> policy.maximum_value)
		return (FT_ERR_OUT_OF_RANGE);
	if ((volatile_stabilizer_success_reduction_ > 0
			&& static_cast<uint32_t>(volatile_stabilizer_success_reduction_)
			> policy.maximum_value)
		|| (volatile_stabilizer_overload_surge_ > 0
			&& static_cast<uint32_t>(volatile_stabilizer_overload_surge_)
			> policy.maximum_value))
		return (FT_ERR_OUT_OF_RANGE);
	if (static_cast<uint32_t>(empty_draw_instability_delta_)
		> policy.maximum_value
		|| static_cast<uint32_t>(hand_overflow_instability_delta_)
			> policy.maximum_value)
		return (FT_ERR_OUT_OF_RANGE);
	if (result != FT_ERR_SUCCESS)
		return (result);
	chunk_instability_policy_ = policy;
	chunk_instability_policy_configured_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_crop_growth_policy(
	const CropGrowthPolicy &policy) noexcept
{
	static const uint32_t MAXIMUM_TICK_INTERVAL = 120000U;
	static const uint32_t MAXIMUM_ROLL_DIVISOR = 1000000U;
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (policy.tick_interval == 0U || policy.maturity_roll_divisor == 0U
		|| policy.wheat_minimum_light_level > 15U
		|| policy.paper_reed_minimum_light_level > 15U
		|| policy.hydration_radius > 8U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (policy.tick_interval > MAXIMUM_TICK_INTERVAL
		|| policy.maturity_roll_divisor > MAXIMUM_ROLL_DIVISOR)
		return (FT_ERR_OUT_OF_RANGE);
	this->crop_growth_policy_ = policy;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_ore_overgrowth_instability_delta(
	int32_t positive_delta) noexcept
{
	if (active_)
		return (FT_ERR_INVALID_OPERATION);
	if (!chunk_instability_policy_configured_ || positive_delta <= 0
		|| static_cast<uint32_t>(positive_delta)
			> chunk_instability_policy_.maximum_value)
		return (FT_ERR_INVALID_ARGUMENT);
	ore_overgrowth_instability_delta_ = positive_delta;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_ultimate_greed_instability_delta(
	int32_t positive_delta) noexcept
{
	if (active_)
		return (FT_ERR_INVALID_OPERATION);
	if (!chunk_instability_policy_configured_ || positive_delta <= 0
		|| static_cast<uint32_t>(positive_delta)
			> chunk_instability_policy_.maximum_value)
		return (FT_ERR_INVALID_ARGUMENT);
	ultimate_greed_instability_delta_ = positive_delta;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_biome_rupture_instability_delta(
	int32_t positive_delta) noexcept
{
	if (active_)
		return (FT_ERR_INVALID_OPERATION);
	if (!chunk_instability_policy_configured_ || positive_delta <= 0
		|| static_cast<uint32_t>(positive_delta)
			> chunk_instability_policy_.maximum_value)
		return (FT_ERR_INVALID_ARGUMENT);
	biome_rupture_instability_delta_ = positive_delta;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_volcano_mark_instability_delta(
	int32_t positive_delta) noexcept
{
	if (active_)
		return (FT_ERR_INVALID_OPERATION);
	if (!chunk_instability_policy_configured_ || positive_delta <= 0
		|| static_cast<uint32_t>(positive_delta)
			> chunk_instability_policy_.maximum_value)
		return (FT_ERR_INVALID_ARGUMENT);
	volcano_mark_instability_delta_ = positive_delta;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_ruined_village_instability_delta(
	int32_t positive_delta) noexcept
{
	if (active_)
		return (FT_ERR_INVALID_OPERATION);
	if (!chunk_instability_policy_configured_ || positive_delta <= 0
		|| static_cast<uint32_t>(positive_delta)
			> chunk_instability_policy_.maximum_value)
		return (FT_ERR_INVALID_ARGUMENT);
	ruined_village_instability_delta_ = positive_delta;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_shipwreck_instability_delta(
	int32_t positive_delta) noexcept
{
	if (active_)
		return (FT_ERR_INVALID_OPERATION);
	if (!chunk_instability_policy_configured_ || positive_delta <= 0
		|| static_cast<uint32_t>(positive_delta)
			> chunk_instability_policy_.maximum_value)
		return (FT_ERR_INVALID_ARGUMENT);
	shipwreck_instability_delta_ = positive_delta;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_rare_structure_instability_deltas(
	int32_t pyramid_delta, int32_t canopy_delta,
	int32_t spider_nest_delta) noexcept
{
	if (active_)
		return (FT_ERR_INVALID_OPERATION);
	if (!chunk_instability_policy_configured_ || pyramid_delta <= 0
		|| canopy_delta <= 0 || spider_nest_delta <= 0
		|| static_cast<uint32_t>(pyramid_delta)
			> chunk_instability_policy_.maximum_value
		|| static_cast<uint32_t>(canopy_delta)
			> chunk_instability_policy_.maximum_value
		|| static_cast<uint32_t>(spider_nest_delta)
			> chunk_instability_policy_.maximum_value)
		return (FT_ERR_INVALID_ARGUMENT);
	pyramid_instability_delta_ = pyramid_delta;
	canopy_instability_delta_ = canopy_delta;
	spider_nest_instability_delta_ = spider_nest_delta;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_unstable_catalyst_instability_delta(
	int32_t positive_delta) noexcept
{
	if (active_)
		return (FT_ERR_INVALID_OPERATION);
	if (!chunk_instability_policy_configured_ || positive_delta <= 0
		|| static_cast<uint32_t>(positive_delta)
			> chunk_instability_policy_.maximum_value)
		return (FT_ERR_INVALID_ARGUMENT);
	unstable_catalyst_instability_delta_ = positive_delta;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_draw_instability_deltas(
	int32_t empty_draw_delta, int32_t hand_overflow_delta) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (!this->chunk_instability_policy_configured_
		|| empty_draw_delta < 0 || hand_overflow_delta < 0
		|| static_cast<uint32_t>(empty_draw_delta)
			> this->chunk_instability_policy_.maximum_value
		|| static_cast<uint32_t>(hand_overflow_delta)
			> this->chunk_instability_policy_.maximum_value)
		return (FT_ERR_CONFIGURATION);
	this->empty_draw_instability_delta_ = empty_draw_delta;
	this->hand_overflow_instability_delta_ = hand_overflow_delta;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_volatile_stabilizer_policy(
	const uint32_t *risk_basis_points, uint32_t risk_tier_count,
	int32_t success_reduction, int32_t overload_surge) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (!this->chunk_instability_policy_configured_
		|| risk_basis_points == nullptr || risk_tier_count == 0U
		|| risk_tier_count
			> RegenerationWorldRulesPolicy::MAXIMUM_STABILIZER_RISK_TIERS
		|| success_reduction <= 0 || overload_surge <= 0
		|| static_cast<uint32_t>(success_reduction)
			> this->chunk_instability_policy_.maximum_value
		|| static_cast<uint32_t>(overload_surge)
			> this->chunk_instability_policy_.maximum_value)
		return (FT_ERR_INVALID_ARGUMENT);
	for (uint32_t index = 0U; index < risk_tier_count; ++index)
		if (risk_basis_points[index] > 10000U
			|| (index > 0U
				&& risk_basis_points[index] < risk_basis_points[index - 1U]))
			return (FT_ERR_INVALID_ARGUMENT);
	this->volatile_stabilizer_risk_tier_count_ = risk_tier_count;
	std::copy(risk_basis_points, risk_basis_points + risk_tier_count,
		this->volatile_stabilizer_risk_basis_points_);
	std::fill(this->volatile_stabilizer_risk_basis_points_ + risk_tier_count,
		this->volatile_stabilizer_risk_basis_points_
			+ RegenerationWorldRulesPolicy::MAXIMUM_STABILIZER_RISK_TIERS,
		0U);
	this->volatile_stabilizer_success_reduction_ = success_reduction;
	this->volatile_stabilizer_overload_surge_ = overload_surge;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_volcano_mark_footprint(
	uint32_t chunk_width) noexcept
{
	if (active_)
		return (FT_ERR_INVALID_OPERATION);
	if (chunk_width < 9U || chunk_width > 16U)
		return (FT_ERR_INVALID_ARGUMENT);
	volcano_mark_footprint_chunks_ = chunk_width;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::get_regeneration_card_preview(
	uint32_t card_definition_id,
	RegenerationCardPreview *preview_out) const noexcept
{
	int32_t result;

	result = RegenerationCardSession::get_card_preview(card_definition_id,
		preview_out);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (card_definition_id
		== RegenerationCardSession::VOLCANO_MARK_CARD_DEFINITION_ID)
		preview_out->required_chunk_width =
			this->volcano_mark_footprint_chunks_;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_volcano_mark_use_authorizer(
	VolcanoMarkUseAuthorizer authorizer, void *user_data) noexcept
{
	if (active_)
		return (FT_ERR_INVALID_OPERATION);
	if (authorizer == nullptr && user_data != nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	volcano_mark_use_authorizer_ = authorizer;
	volcano_mark_use_authorizer_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_boat_actor_authorizer(
	BoatActorAuthorizer authorizer, void *user_data) noexcept
{
	if (active_)
		return (FT_ERR_INVALID_OPERATION);
	if (authorizer == nullptr && user_data != nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	boat_actor_authorizer_ = authorizer;
	boat_actor_authorizer_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_combat_actor_authorizer(
	CombatActorAuthorizer authorizer, void *user_data) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (authorizer == nullptr && user_data != nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->combat_actor_authorizer_ = authorizer;
	this->combat_actor_authorizer_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_melee_attack_interval(
	uint32_t interval_ticks) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (interval_ticks == 0U || interval_ticks > 200U)
		return (FT_ERR_INVALID_ARGUMENT);
	this->melee_attack_interval_ticks_ = interval_ticks;
	this->next_melee_attack_tick_ = 0U;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::configure_boat_network_actor_resolver(
	BoatNetworkActorResolver resolver, void *user_data) noexcept
{
	if (this->active_)
		return (FT_ERR_INVALID_OPERATION);
	if (resolver == nullptr && user_data != nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	this->boat_network_actor_resolver_ = resolver;
	this->boat_network_actor_resolver_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::handle_network_boat_move(
	const uint8_t authenticated_public_key[32],
	const ProtocolBoatMoveIntentMessage &intent,
	ProtocolBoatMoveResultMessage &result, void *user_data) noexcept
{
	GameSession *session;
	uint64_t actor_id;
	uint64_t entity_revision;
	WorldEntitySnapshot entity_snapshot = {};
	int32_t action_result;

	if (authenticated_public_key == nullptr || user_data == nullptr)
		return (FT_ERR_INVALID_POINTER);
	session = static_cast<GameSession *>(user_data);
	result.session_id = intent.session_id;
	result.request_id = intent.request_id;
	result.entity_id = intent.entity_id;
	result.result_code = FT_ERR_PERMISSION_DENIED;
	result.entity_revision = 0U;
	if (session->boat_network_actor_resolver_ == nullptr)
		return (FT_ERR_SUCCESS);
	actor_id = 0U;
	if (!session->boat_network_actor_resolver_(authenticated_public_key,
			&actor_id, session->boat_network_actor_resolver_user_data_)
		|| actor_id == 0U)
		return (FT_ERR_SUCCESS);
	entity_revision = 0U;
	action_result = session->move_boat_for_actor(intent.request_id, actor_id,
		intent.entity_id, intent.expected_entity_revision, intent.next_x,
		intent.next_z, intent.yaw, &entity_revision);
	result.result_code = action_result;
	if (action_result != FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	action_result = session->world_entities_.get(intent.entity_id,
		&entity_snapshot);
	if (action_result != FT_ERR_SUCCESS)
	{
		result.result_code = action_result;
		return (FT_ERR_SUCCESS);
	}
	if (entity_snapshot.revision != entity_revision)
	{
		result.result_code = FT_ERR_INVALID_STATE;
		return (FT_ERR_SUCCESS);
	}
	result.entity_revision = entity_revision;
	result.entity_state = entity_snapshot.state;
	return (FT_ERR_SUCCESS);
}

int GameSession::loading_tick(const RenderDistanceStrategy &strategy)
{
	const int32_t gen = strategy.generation_budget_for_frame(
		performance_frame_ms_, boost_enabled_);
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	World::StreamDiagnostics diagnostics;
	#endif

	if (!active_)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code_ = world_.update_around(static_cast<double>(player_character_.get_x()),
			static_cast<double>(player_character_.get_z()), gen,
			active_render_distance_);
	if (error_code_ != FT_ERR_SUCCESS)
	{
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		World::StreamDiagnostics failure = world_.stream_diagnostics();
		std::fprintf(stderr,
			"[Session] loading stream update failed error=%d last_error=%d "
			"frame=%llu pending=%zu retryable=%zu failed=%zu "
			"playable_failed=%zu remesh_pending=%zu\n",
			error_code_, world_.stream_last_error(),
			static_cast<unsigned long long>(failure.frame), failure.pending_count,
			failure.retryable_count, failure.failed_count,
			failure.playable_failed_count,
			failure.interactive_remesh_queue_depth);
		/* Positive Libft errors are recoverable outcomes (for example a
		 * temporarily full queue).  Keep the loading session alive and retry on
		 * the next tick; only negative errors are fatal here. */
	#endif
		if (error_code_ > 0)
			return (FT_ERR_SUCCESS);
	}
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	diagnostics = world_.stream_diagnostics();
	if (error_code_ == FT_ERR_SUCCESS
		&& diagnostics.playable_failed_count != 0U
		&& diagnostics.frame % 120U == 0U)
	{
		std::fprintf(stderr,
			"World loading: playable retry pending last_error=%d retryable=%zu\n",
			world_.stream_last_error(), diagnostics.retryable_count);
	}
	if (error_code_ == FT_ERR_SUCCESS && diagnostics.frame % 120U == 0U
		&& !this->is_ready_to_play())
	{
		std::fprintf(stderr,
			"World loading: frame=%llu progress=%llu loaded=%d pending=%zu ready=%zu "
			"drawable=%zu/%zu active=%zu retry=%zu failed=%zu last_error=%d "
			"result_age_ns=%llu\n",
			static_cast<unsigned long long>(diagnostics.frame),
			static_cast<unsigned long long>(diagnostics.progress_frame),
			world_.loaded_chunk_count, diagnostics.pending_count,
			diagnostics.ready_count,
			diagnostics.playable_drawable_count,
			diagnostics.playable_required_count,
			diagnostics.active_generation_count,
			diagnostics.retryable_count, diagnostics.failed_count,
			diagnostics.last_error,
			static_cast<unsigned long long>(
				diagnostics.oldest_result_age_nanoseconds));
		this->report_loading_gaps();
	}
	#endif
	return (error_code_);
}

#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
void GameSession::report_loading_gaps() const
{
	const int32_t radius = WorldCoordinates::render_distance_to_chunk_radius(
		WorldCoordinates::MIN_RENDER_DISTANCE);
	const int32_t radius_squared = radius * radius;
	int32_t offset_z;
	int32_t gap_count;
	int32_t reported;

	gap_count = 0;
	reported = 0;
	offset_z = -radius;
	while (offset_z <= radius)
	{
		int32_t offset_x = -radius;
		while (offset_x <= radius)
		{
			if (offset_x * offset_x + offset_z * offset_z <= radius_squared)
			{
				const int32_t chunk_x = world_.center_chunk_x + offset_x;
				const int32_t chunk_z = world_.center_chunk_z + offset_z;
				const WorldChunk *chunk = world_.find_chunk(chunk_x, chunk_z);
				const bool drawable = chunk != nullptr
					&& game_session_mesh_is_drawable(chunk->mesh);
				if (!drawable)
				{
					gap_count += 1;
					if (reported < 4)
					{
						std::fprintf(stderr,
							"World loading: %s playable chunk=(%d,%d)\n",
							chunk == nullptr ? "missing" : "non-drawable",
							chunk_x, chunk_z);
						reported += 1;
					}
				}
			}
			offset_x += 1;
		}
		offset_z += 1;
	}
	if (gap_count > reported)
		std::fprintf(stderr,
			"World loading: additional_playable_gaps=%d\n",
			gap_count - reported);
}
#endif

GameSession::Action GameSession::handle_navigation(ApplicationWindow &window)
{
	int32_t analytics_error;

	(void)window;
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::INPUT_DEVICE_POLL);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: input poll scope start failed (%d)\n",
			analytics_error);
	ft_dumb_controls_poll();
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: input poll scope end failed (%d)\n",
			analytics_error);
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_BACK) == FT_TRUE)
		return (Action::EXIT_TO_MENU);
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_CONFIRM) == FT_TRUE)
		return (Action::OPEN_SETTINGS);
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_BOOST) == FT_TRUE)
		boost_enabled_ = !boost_enabled_;
	return (Action::CONTINUE);
}

int32_t GameSession::process_spawner_deaths(
	const std::vector<WorldSpawnerDeath> &deaths) noexcept
{
	if (deaths.empty())
		return (FT_ERR_SUCCESS);
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	std::vector<uint8_t> items_before;
	std::vector<uint8_t> entities_before;
	std::vector<uint8_t> recipe_book_before;
	int32_t result = this->recipe_catalog_.serialize_recipe_book(
		recipe_book_before);
	if (result == FT_ERR_SUCCESS)
		result = this->world_items_.serialize(items_before);
	if (result == FT_ERR_SUCCESS)
		result = GameSessionEntityState::serialize(this->world_entities_,
			this->world_spawners_, entities_before);
	for (const WorldSpawnerDeath &death : deaths)
	{
		WorldEntitySnapshot corpse = {};
		std::vector<MinecraftLootDrop> drops;
		if (result == FT_ERR_SUCCESS)
			result = MinecraftLootCatalog::resolve(death.loot_table_id,
				death.entity_id, drops);
		if (result == FT_ERR_SUCCESS)
		{
			MinecraftLootRecipeDiscovery discovery = {};
			const int32_t discovery_result =
				MinecraftLootCatalog::resolve_recipe_discovery(
					death.loot_table_id, &discovery);

			if (discovery_result == FT_ERR_SUCCESS)
			{
				const uint64_t source_event_id =
					(static_cast<uint64_t>(death.spawner_id) << 32U)
					| static_cast<uint64_t>(death.entity_id);
				ft_bool newly_unlocked = FT_FALSE;

				result = this->recipe_catalog_.grant_recipe_from_discovery(
					discovery.source_type_id, discovery.source_id,
					source_event_id, &newly_unlocked);
			}
			else if (discovery_result != FT_ERR_NOT_FOUND)
				result = discovery_result;
		}
		if (result == FT_ERR_SUCCESS)
		{
			result = this->world_entities_.get(death.entity_id, &corpse);
			if (result == FT_ERR_NOT_FOUND)
			{
				corpse.state.position_x = death.position_x;
				corpse.state.position_y = death.position_y;
				corpse.state.position_z = death.position_z;
				corpse.revision = 0U;
				result = FT_ERR_SUCCESS;
			}
		}
		for (const MinecraftLootDrop &loot_drop : drops)
		{
			ft_sharedptr<game_item> drop;
			uint64_t item_entity_id = 0U;
			if (result == FT_ERR_SUCCESS)
				result = this->item_catalog_.create_item(loot_drop.item_id,
					loot_drop.quantity, &drop);
			if (result == FT_ERR_SUCCESS)
				result = this->world_items_.spawn(drop,
					corpse.state.position_x, corpse.state.position_y,
					corpse.state.position_z, world_.current_tick, 0U, 0U,
					&item_entity_id);
			if (result != FT_ERR_SUCCESS)
				break ;
		}
		if (result == FT_ERR_SUCCESS && corpse.revision != 0U)
			result = this->world_entities_.despawn(death.entity_id,
				corpse.revision);
		if (result == FT_ERR_SUCCESS)
			result = this->world_spawners_.acknowledge_death(death.spawner_id,
				death.entity_id);
		if (result != FT_ERR_SUCCESS)
			break ;
	}
	if (result == FT_ERR_SUCCESS)
		result = this->save_session_snapshot();
	if (result == FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	int32_t rollback_result = FT_ERR_SUCCESS;
	if (!items_before.empty())
		rollback_result = this->world_items_.deserialize(items_before.data(),
			items_before.size());
	if (rollback_result == FT_ERR_SUCCESS && !entities_before.empty())
		rollback_result = GameSessionEntityState::restore(entities_before.data(),
			entities_before.size(), this->world_entities_, this->world_spawners_);
	if (rollback_result == FT_ERR_SUCCESS && !recipe_book_before.empty())
		rollback_result = this->recipe_catalog_.restore_recipe_book(
			recipe_book_before.data(), recipe_book_before.size());
	return (rollback_result == FT_ERR_SUCCESS ? result : rollback_result);
}

int32_t GameSession::process_animal_production(
	const std::vector<WorldAnimalProductionEvent> &events) noexcept
{
	if (events.empty())
		return (FT_ERR_SUCCESS);
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	std::vector<uint8_t> items_before;
	int32_t result = this->world_items_.serialize(items_before);
	for (const WorldAnimalProductionEvent &event : events)
	{
		ft_sharedptr<game_item> egg;
		uint64_t item_entity_id = 0U;
		if (result == FT_ERR_SUCCESS)
			result = this->item_catalog_.create_item(
				MinecraftItemCatalog::EGG_ITEM_ID, 1, &egg);
		if (result == FT_ERR_SUCCESS)
			result = this->world_items_.spawn(egg, event.position_x,
				event.position_y, event.position_z, this->world_.current_tick,
				0U, 0U, &item_entity_id);
		if (result != FT_ERR_SUCCESS)
			break ;
	}
	if (result == FT_ERR_SUCCESS)
		result = this->save_session_snapshot();
	if (result == FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	int32_t rollback_result = FT_ERR_SUCCESS;
	if (!items_before.empty())
		rollback_result = this->world_items_.deserialize(items_before.data(),
			items_before.size());
	for (const WorldAnimalProductionEvent &event : events)
	{
		WorldEntitySnapshot animal = {};
		if (rollback_result == FT_ERR_SUCCESS)
			rollback_result = this->world_entities_.get(
				event.animal_entity_id, &animal);
		if (rollback_result == FT_ERR_SUCCESS)
		{
			animal.state.tick = event.previous_tick;
			uint64_t new_revision = 0U;
			rollback_result = this->world_entities_.update(
				event.animal_entity_id, animal.revision, animal.state,
				&new_revision);
		}
		if (rollback_result != FT_ERR_SUCCESS)
			break ;
	}
	return (rollback_result == FT_ERR_SUCCESS ? result : rollback_result);
}

int32_t GameSession::advance_nearby_crop_growth() noexcept
{
	if (this->crop_growth_policy_.tick_interval == 0U
		|| this->crop_growth_policy_.maturity_roll_divisor == 0U)
		return (FT_ERR_INVALID_STATE);
	if (world_.current_tick % this->crop_growth_policy_.tick_interval != 0U)
		return (FT_ERR_SUCCESS);
	struct MaturedCrop
	{
		int32_t x;
		int32_t y;
		int32_t z;
		uint32_t immature_block;
		uint32_t mature_block;
	};
	std::vector<CropChunkGrowthDeadline> staged_deadlines;
	std::vector<std::pair<int32_t, int32_t>> due_chunks;
	std::vector<MaturedCrop> matured_crops;
	bool state_changed = false;
	int32_t result = FT_ERR_SUCCESS;
	try
	{
		staged_deadlines = this->crop_chunk_growth_deadlines_;
		due_chunks.reserve(9U);
		matured_crops.reserve(64U);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	auto find_deadline = [&](int32_t chunk_x, int32_t chunk_z)
	{
		const CropChunkGrowthDeadline key = {chunk_x, chunk_z, 0U};
		return (std::lower_bound(staged_deadlines.begin(),
			staged_deadlines.end(), key, game_session_crop_deadline_less));
	};
	auto rollback_matured_crops = [&]() -> int32_t
	{
		for (std::vector<MaturedCrop>::reverse_iterator entry =
			matured_crops.rbegin(); entry != matured_crops.rend(); ++entry)
		{
			WorldChunk *chunk = world_.find_chunk_mutable(
				WorldCoordinates::floor_divide(entry->x,
					GAME_VOXEL_CHUNK_WIDTH),
				WorldCoordinates::floor_divide(entry->z,
					GAME_VOXEL_CHUNK_DEPTH));
			if (chunk == nullptr)
				return (FT_ERR_NOT_FOUND);
			const int32_t rollback_result = world_.replace_block_state_at(
				entry->x, entry->y, entry->z, entry->mature_block,
				entry->immature_block, chunk->voxel_revision);
			if (rollback_result != FT_ERR_SUCCESS)
				return (rollback_result);
		}
		return (FT_ERR_SUCCESS);
	};
	const int32_t center_x = static_cast<int32_t>(std::floor(camera_.x));
	const int32_t center_y = static_cast<int32_t>(std::floor(camera_.y));
	const int32_t center_z = static_cast<int32_t>(std::floor(camera_.z));
	for (int32_t y = std::max(1, center_y - 16);
		y <= std::min(GAME_VOXEL_CHUNK_HEIGHT - 1, center_y + 16); ++y)
	{
		for (int32_t z = center_z - 8; z <= center_z + 8; ++z)
		{
			for (int32_t x = center_x - 8; x <= center_x + 8; ++x)
			{
				uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
				if (!world_.block_id_at(x, y, z, &block_id)
					|| (block_id
						!= VOXEL_GENERATOR_WHEAT_CROP_IMMATURE_BLOCK_ID
						&& block_id
							!= VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID))
					continue;
				const int32_t chunk_x = WorldCoordinates::floor_divide(x,
					GAME_VOXEL_CHUNK_WIDTH);
				const int32_t chunk_z = WorldCoordinates::floor_divide(z,
					GAME_VOXEL_CHUNK_DEPTH);
			auto deadline = find_deadline(chunk_x, chunk_z);
			if (deadline == staged_deadlines.end()
				|| deadline->chunk_x != chunk_x
				|| deadline->chunk_z != chunk_z)
			{
				if (staged_deadlines.size() >= 4096U)
				{
					result = FT_ERR_FULL;
					break;
				}
				try
				{
					deadline = staged_deadlines.insert(deadline,
						{chunk_x, chunk_z, world_.current_tick});
				}
				catch (const std::bad_alloc &)
				{
					result = FT_ERR_NO_MEMORY;
					break;
				}
				state_changed = true;
			}
			if (deadline->next_growth_tick > world_.current_tick)
				continue;
			const std::pair<int32_t, int32_t> chunk_key(chunk_x, chunk_z);
			if (std::find(due_chunks.begin(), due_chunks.end(), chunk_key)
				== due_chunks.end())
				due_chunks.push_back(chunk_key);
			const bool wheat_crop = block_id
				== VOXEL_GENERATOR_WHEAT_CROP_IMMATURE_BLOCK_ID;
			uint32_t soil_block_id = GAME_VOXEL_AIR_BLOCK;
			if (!world_.block_id_at(x, y - 1, z, &soil_block_id)
				|| (wheat_crop
					? soil_block_id != VOXEL_GENERATOR_FARMLAND_BLOCK_ID
					: (soil_block_id != VOXEL_GENERATOR_GRASS_BLOCK_ID
						&& soil_block_id != VOXEL_GENERATOR_DIRT_BLOCK_ID
						&& soil_block_id != VOXEL_GENERATOR_MUD_BLOCK_ID
						&& soil_block_id != VOXEL_GENERATOR_SAND_BLOCK_ID)))
				continue;
			/* Unloaded water cells do not hydrate soil. */
			if (wheat_crop
				? !world_.farmland_is_hydrated(x, y - 1, z,
					this->crop_growth_policy_.hydration_radius)
				: !world_.has_water_source_near(x, y - 1, z,
					this->crop_growth_policy_.hydration_radius))
				continue;
			uint8_t light_level = 0U;
			if (!world_.light_level_at(x, y, z, &light_level)
				|| light_level < (wheat_crop
					? this->crop_growth_policy_.wheat_minimum_light_level
					: this->crop_growth_policy_
						.paper_reed_minimum_light_level))
				continue;
			const uint32_t hash = static_cast<uint32_t>(x) * 73856093U
				^ static_cast<uint32_t>(y) * 19349663U
				^ static_cast<uint32_t>(z) * 83492791U
				^ static_cast<uint32_t>(world_.current_tick
					/ this->crop_growth_policy_.tick_interval);
			if (hash % this->crop_growth_policy_.maturity_roll_divisor != 0U)
				continue;
			const WorldChunk *chunk = world_.find_chunk(chunk_x, chunk_z);
			if (chunk == nullptr)
				continue;
			const uint32_t mature_block = wheat_crop
				? VOXEL_GENERATOR_WHEAT_CROP_MATURE_BLOCK_ID
				: VOXEL_GENERATOR_PAPER_REED_BLOCK_ID;
			if (matured_crops.size() >= 4096U)
			{
				result = FT_ERR_FULL;
				break;
			}
			try
			{
				matured_crops.push_back({x, y, z, block_id, mature_block});
			}
			catch (const std::bad_alloc &)
			{
				result = FT_ERR_NO_MEMORY;
				break;
			}
			result = world_.replace_block_state_at(x, y, z, block_id,
				mature_block, chunk->voxel_revision);
			if (result == FT_ERR_SUCCESS)
				state_changed = true;
			else
			{
				matured_crops.pop_back();
				if (result == FT_ERR_INVALID_STATE)
					result = FT_ERR_SUCCESS;
				else
					break;
			}
			if (result != FT_ERR_SUCCESS)
				break;
		}
		if (result != FT_ERR_SUCCESS)
			break;
	}
	if (result != FT_ERR_SUCCESS)
		break;
	}
	if (result == FT_ERR_SUCCESS)
	{
		if (std::numeric_limits<uint64_t>::max() - world_.current_tick
			< this->crop_growth_policy_.tick_interval)
			result = FT_ERR_OUT_OF_RANGE;
		else
			for (const std::pair<int32_t, int32_t> &chunk_key : due_chunks)
			{
				auto deadline = find_deadline(chunk_key.first,
					chunk_key.second);
				if (deadline == staged_deadlines.end()
					|| deadline->chunk_x != chunk_key.first
					|| deadline->chunk_z != chunk_key.second)
				{
					result = FT_ERR_INVALID_STATE;
					break;
				}
				deadline->next_growth_tick = world_.current_tick
					+ this->crop_growth_policy_.tick_interval;
				state_changed = true;
			}
	}
	if (result != FT_ERR_SUCCESS)
	{
		const int32_t rollback_result = rollback_matured_crops();
		return (rollback_result == FT_ERR_SUCCESS ? result : rollback_result);
	}
	if (!state_changed)
		return (FT_ERR_SUCCESS);
	this->crop_chunk_growth_deadlines_.swap(staged_deadlines);
	if (!this->active_)
		return (FT_ERR_SUCCESS);
	result = this->save_session_snapshot();
	if (result == FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	this->crop_chunk_growth_deadlines_.swap(staged_deadlines);
	const int32_t rollback_result = rollback_matured_crops();
	return (rollback_result == FT_ERR_SUCCESS ? result : rollback_result);
}

int32_t GameSession::query_equipped_armor_cached(
	uint32_t *armor_points_out) noexcept
{
	const int32_t character_slots[] = {EQUIP_HEAD, EQUIP_CHEST,
		EQUIP_LEGS, EQUIP_FEET};
	uint64_t fingerprint[16] = {};
	if (armor_points_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	for (uint32_t index = 0U;
		index < sizeof(character_slots) / sizeof(character_slots[0]); ++index)
	{
		ft_sharedptr<game_item> *item = this->player_character_.get_equipped_item(
			character_slots[index]);
		if (item == nullptr)
			return (this->player_character_.get_error());
		if (*item)
		{
			const std::size_t offset = index * 4U;
			fingerprint[offset] = static_cast<uint64_t>(
				static_cast<int64_t>((*item)->get_item_id()));
			fingerprint[offset + 1U] = (*item)->get_instance_id();
			fingerprint[offset + 2U] = static_cast<uint64_t>(
				static_cast<int64_t>((*item)->get_modifier1_id()));
			fingerprint[offset + 3U] = static_cast<uint64_t>(
				static_cast<int64_t>((*item)->get_modifier1_value()));
		}
		(void)item->destroy();
		delete item;
	}
	if (this->equipped_armor_cache_valid_
		&& std::memcmp(fingerprint, this->equipped_armor_fingerprint_,
			sizeof(fingerprint)) == 0)
	{
		*armor_points_out = this->equipped_armor_points_cache_;
		return (FT_ERR_SUCCESS);
	}
	uint32_t armor_points = 0U;
	const int32_t result = game_session_query_equipped_armor(
		this->player_character_, this->item_catalog_, &armor_points);
	if (result != FT_ERR_SUCCESS)
	{
		this->equipped_armor_cache_valid_ = false;
		return (result);
	}
	std::memcpy(this->equipped_armor_fingerprint_, fingerprint,
		sizeof(fingerprint));
	this->equipped_armor_points_cache_ = armor_points;
	this->equipped_armor_cache_valid_ = true;
	*armor_points_out = armor_points;
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::query_main_hand_equipment_rule_cached(
	const game_item &item, MinecraftEquipmentRule *rule_out) noexcept
{
	uint64_t fingerprint[4];
	MinecraftEquipmentRule rule = {};
	int32_t error;
	if (rule_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	fingerprint[0] = static_cast<uint64_t>(
		static_cast<int64_t>(item.get_item_id()));
	fingerprint[1] = item.get_instance_id();
	fingerprint[2] = static_cast<uint64_t>(
		static_cast<int64_t>(item.get_modifier1_id()));
	fingerprint[3] = static_cast<uint64_t>(
		static_cast<int64_t>(item.get_modifier1_value()));
	if (this->main_hand_rule_cache_valid_
		&& std::memcmp(fingerprint, this->main_hand_rule_fingerprint_,
			sizeof(fingerprint)) == 0)
	{
		*rule_out = this->main_hand_rule_cache_;
		return (FT_ERR_SUCCESS);
	}
	error = this->item_catalog_.resolve_equipment_rule(item.get_item_id(),
		&rule);
	if (error != FT_ERR_SUCCESS)
	{
		this->main_hand_rule_cache_valid_ = false;
		return (error);
	}
	std::memcpy(this->main_hand_rule_fingerprint_, fingerprint,
		sizeof(fingerprint));
	this->main_hand_rule_cache_ = rule;
	this->main_hand_rule_cache_valid_ = true;
	*rule_out = rule;
	return (FT_ERR_SUCCESS);
}

GameSession::Action GameSession::tick_world(double delta_seconds,
	const RenderDistanceStrategy &strategy)
{
	CameraInput	input;
	int			gen;
	int32_t analytics_error;

	world_.advance_tick();
	if (this->shield_guard_active_
		&& world_.current_tick >= this->shield_guard_until_tick_)
	{
		this->shield_guard_active_ = false;
		this->shield_guard_until_tick_ = 0U;
		this->shield_guard_cooldown_until_tick_ =
			world_.current_tick > UINT64_MAX - 20U
				? UINT64_MAX : world_.current_tick + 20U;
	}
	if (this->advance_nearby_crop_growth() != FT_ERR_SUCCESS)
		return (Action::FAILED);
	{
		bool animal_state_changed = false;
		std::vector<WorldAnimalProductionEvent> animal_production;
		std::vector<WorldHostileAttackEvent> hostile_attacks;
		const int32_t animal_result = WorldAnimalSimulation::tick(world_,
			world_entities_, camera_.x, camera_.y, camera_.z,
			world_.current_tick, &animal_state_changed, &animal_production,
			&hostile_attacks);
		if (animal_result != FT_ERR_SUCCESS)
		{
			error_code_ = animal_result;
			return (Action::FAILED);
		}
		{
			bool projectile_state_changed = false;
			const GameSessionSnapshotWriter snapshot_writer = {
				&GameSession::save_snapshot_callback, this};
			const int32_t projectile_result = WorldProjectileSimulation::tick(
				world_, world_entities_, world_items_, item_catalog_,
				world_.current_tick, &snapshot_writer,
				&projectile_state_changed);
			if (projectile_result != FT_ERR_SUCCESS)
			{
				error_code_ = projectile_result;
				return (Action::FAILED);
			}
		}
		if (!animal_production.empty()
			&& this->process_animal_production(animal_production)
				!= FT_ERR_SUCCESS)
			return (Action::FAILED);
		if (!hostile_attacks.empty()
			&& this->player_character_.get_hit_points() > 0)
		{
			uint32_t equipped_armor_points = 0U;
			uint32_t shield_guard_reduction = 0U;
			const int32_t armor_query_result =
				this->query_equipped_armor_cached(&equipped_armor_points);
			if (armor_query_result != FT_ERR_SUCCESS)
			{
				error_code_ = armor_query_result;
				return (Action::FAILED);
			}
			if (this->shield_guard_active_
				&& world_.current_tick < this->shield_guard_until_tick_)
			{
				const int32_t shield_query_result =
					game_session_query_shield_guard(this->player_character_,
						this->item_catalog_, &shield_guard_reduction);
				if (shield_query_result != FT_ERR_SUCCESS
					|| shield_guard_reduction == 0U)
				{
					this->shield_guard_active_ = false;
					this->shield_guard_until_tick_ = 0U;
					shield_guard_reduction = 0U;
				}
			}
			const int32_t hit_points_before =
				this->player_character_.get_hit_points();
			const int32_t physical_armor_before =
				this->player_character_.get_current_physical_armor();
			const int32_t magic_armor_before =
				this->player_character_.get_current_magic_armor();
			const int32_t character_x_before =
				this->player_character_.get_x();
			const int32_t character_y_before =
				this->player_character_.get_y();
			const int32_t character_z_before =
				this->player_character_.get_z();
			const Camera camera_before = this->camera_;
			int32_t armor_modifier_id_before[4] = {};
			int32_t armor_modifier_value_before[4] = {};
			ft_bool armor_wear_applied[4] = {FT_FALSE, FT_FALSE,
				FT_FALSE, FT_FALSE};
			int32_t shield_modifier_id_before = 0;
			int32_t shield_modifier_value_before = 0;
			ft_bool shield_wear_applied = FT_FALSE;
			uint32_t hostile_attacks_resolved = 0U;
			uint32_t shield_hits_blocked = 0U;
			for (const WorldHostileAttackEvent &attack : hostile_attacks)
			{
				const int32_t current_hit_points =
					this->player_character_.get_hit_points();
				if (current_hit_points <= 0)
					break ;
				const int32_t bounded_damage = std::min(attack.damage,
					current_hit_points);
				const double source_x = attack.source_x - this->camera_.x;
				const double source_z = attack.source_z - this->camera_.z;
				const double source_length = std::sqrt(source_x * source_x
					+ source_z * source_z);
				const double facing_dot = source_length > 0.0001
					? (source_x * std::sin(this->camera_.yaw)
						+ source_z * std::cos(this->camera_.yaw)) / source_length
					: -1.0;
				const bool shield_blocks = shield_guard_reduction > 0U
					&& facing_dot >= 0.5;
				const uint32_t total_reduction = equipped_armor_points
					+ (shield_blocks ? shield_guard_reduction : 0U);
				if (shield_blocks && shield_hits_blocked
					< std::numeric_limits<uint32_t>::max())
					shield_hits_blocked += 1U;
				const int32_t armor_reduction = static_cast<int32_t>(
					std::min<uint32_t>(total_reduction,
						static_cast<uint32_t>(std::max(0,
							bounded_damage - 1))));
				const int32_t mitigated_damage = std::max(1,
					bounded_damage - armor_reduction);
				this->player_character_.take_damage(mitigated_damage,
					FT_DAMAGE_PHYSICAL);
				if (hostile_attacks_resolved
					< std::numeric_limits<uint32_t>::max())
					hostile_attacks_resolved += 1U;
			}
			if (hostile_attacks_resolved != 0U)
			{
				const int32_t wear_result =
					game_session_apply_equipped_armor_wear(
						this->player_character_, this->item_catalog_,
						hostile_attacks_resolved, armor_modifier_id_before,
						armor_modifier_value_before, armor_wear_applied);
				if (wear_result != FT_ERR_SUCCESS)
				{
					(void)game_session_restore_equipped_armor_wear(
						this->player_character_, armor_modifier_id_before,
						armor_modifier_value_before, armor_wear_applied);
					error_code_ = wear_result;
					return (Action::FAILED);
				}
			}
			if (shield_hits_blocked != 0U)
			{
				const int32_t wear_result = game_session_apply_shield_wear(
					this->player_character_, this->item_catalog_,
					shield_hits_blocked, &shield_modifier_id_before,
					&shield_modifier_value_before, &shield_wear_applied);
				if (wear_result != FT_ERR_SUCCESS)
				{
					(void)game_session_restore_equipped_armor_wear(
						this->player_character_, armor_modifier_id_before,
						armor_modifier_value_before, armor_wear_applied);
					error_code_ = wear_result;
					return (Action::FAILED);
				}
			}
			if (this->player_character_.get_hit_points() <= 0)
			{
				/* Survival has a single bounded recovery path. Keep inventory and
				 * world state, and return the player to the safe world spawn. */
				this->camera_.x = this->player_spawn_x_;
				this->camera_.z = this->player_spawn_z_;
				PlayerController::spawn_player_on_ground(&this->camera_,
					this->world_);
				this->player_character_.set_hit_points(
					PLAYER_INITIAL_HIT_POINTS);
				this->player_character_.restore_armor();
				this->sync_player_character_location();
			}
			if (this->save_session_snapshot() != FT_ERR_SUCCESS)
			{
				const int32_t checkpoint_error = error_code_;
				const int32_t armor_restore_error =
					game_session_restore_equipped_armor_wear(
					this->player_character_, armor_modifier_id_before,
					armor_modifier_value_before, armor_wear_applied);
				const int32_t shield_restore_error =
					game_session_restore_shield_wear(
						this->player_character_, shield_modifier_id_before,
						shield_modifier_value_before, shield_wear_applied);
				this->camera_ = camera_before;
				this->player_character_.set_hit_points(hit_points_before);
				this->player_character_.set_current_physical_armor(
					physical_armor_before);
				this->player_character_.set_current_magic_armor(
					magic_armor_before);
				this->player_character_.set_x(character_x_before);
				this->player_character_.set_y(character_y_before);
				this->player_character_.set_z(character_z_before);
				if (armor_restore_error != FT_ERR_SUCCESS)
					error_code_ = armor_restore_error;
				else if (shield_restore_error != FT_ERR_SUCCESS)
					error_code_ = shield_restore_error;
				else
					error_code_ = checkpoint_error;
				return (Action::FAILED);
			}
		}
		if (animal_state_changed && world_.current_tick % 600U == 0U
			&& animal_production.empty()
			&& this->save_session_snapshot() != FT_ERR_SUCCESS)
			return (Action::FAILED);
	}
	{
		std::vector<WorldSpawnerDeath> pending_deaths;
		bool spawner_state_changed = false;
		const int32_t spawner_error = this->world_spawners_.tick(
			world_.current_tick, this->world_entities_, pending_deaths,
			&spawner_state_changed);
		if (spawner_error != FT_ERR_SUCCESS)
		{
			std::fprintf(stderr,
				"[Session] hostile spawner tick failed error=%d\n",
				spawner_error);
			return (Action::FAILED);
		}
		if (!pending_deaths.empty())
		{
			const int32_t drop_error = this->process_spawner_deaths(
				pending_deaths);
		if (drop_error != FT_ERR_SUCCESS)
		{
			std::fprintf(stderr,
				"[Session] hostile death loot checkpoint failed error=%d\n",
				drop_error);
			return (Action::FAILED);
		}
		}
		else if (spawner_state_changed)
		{
			const int32_t checkpoint_error = this->save_session_snapshot();
			if (checkpoint_error != FT_ERR_SUCCESS)
			{
				std::fprintf(stderr,
					"[Session] hostile spawner checkpoint failed error=%d\n",
					checkpoint_error);
				return (Action::FAILED);
			}
		}
	}
	if (std::isfinite(delta_seconds) && delta_seconds > 0.0)
		processing_tick_accumulator_ = std::min(60.0,
			processing_tick_accumulator_ + delta_seconds * 20.0);
	{
		const uint32_t elapsed_ticks = static_cast<uint32_t>(
			processing_tick_accumulator_);
		uint32_t completed_jobs = 0U;
		uint32_t pending_outputs = 0U;
		if (elapsed_ticks > 0U)
		{
			uint32_t processing_job_count = 0U;
			uint64_t advance_request_id = 0U;
			int32_t process_error = recipe_catalog_.get_processing_job_count(
				&processing_job_count);
			bool processing_stations_authorized = true;
			if (process_error == FT_ERR_SUCCESS && processing_job_count != 0U)
			{
				std::vector<MinecraftRecipeCatalog::ProcessingStationContext>
					station_contexts;
				process_error = recipe_catalog_.get_processing_station_contexts(
					station_contexts);
				if (process_error == FT_ERR_SUCCESS
					&& (processing_station_continuity_validator_ == nullptr
						|| station_contexts.size() != processing_job_count))
					processing_stations_authorized = false;
				for (const MinecraftRecipeCatalog::ProcessingStationContext
					&station : station_contexts)
				{
					if (!processing_station_continuity_validator_
						|| !processing_station_continuity_validator_(
							static_cast<uint32_t>(
								MinecraftRecipeCatalog::FURNACE_STATION_ID),
							station.station_x, station.station_y, station.station_z,
							station.expected_station_revision,
							processing_station_continuity_user_data_))
						processing_stations_authorized = false;
				}
			}
			if (process_error == FT_ERR_SUCCESS && processing_job_count != 0U
				&& processing_stations_authorized)
			{
				const GameSessionSnapshotWriter snapshot_writer = {
					&GameSession::save_snapshot_callback, this};
				process_error = WorldItemProcessingAdvanceTransaction::recover_pending(
					world_item_save_path_, player_character_, world_items_,
					recipe_catalog_, transaction_journal_, &snapshot_writer);
			}
			if (process_error == FT_ERR_SUCCESS && processing_job_count != 0U
				&& processing_stations_authorized)
				process_error = transaction_journal_.next_request_id(
					WorldItemProcessingAdvanceTransaction::journal_scope(),
					&advance_request_id);
			if (process_error == FT_ERR_SUCCESS && processing_job_count != 0U
				&& processing_stations_authorized)
			{
				const GameSessionSnapshotWriter snapshot_writer = {
					&GameSession::save_snapshot_callback, this};
				process_error = WorldItemProcessingAdvanceTransaction::execute(
					world_item_save_path_, player_character_, world_items_,
					recipe_catalog_, transaction_journal_, advance_request_id,
					elapsed_ticks, 64U, &completed_jobs, &pending_outputs,
					&snapshot_writer);
			}
			if (process_error != FT_ERR_SUCCESS)
			{
				std::fprintf(stderr,
					"[Session] furnace processing tick failed error=%d\n",
					process_error);
				return (Action::FAILED);
			}
			processing_tick_accumulator_ -= elapsed_ticks;
		}
	}
	input = InputReader::read_camera_input(boost_enabled_);
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::PLAYER_MOTION);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: player motion scope start failed (%d)\n",
			analytics_error);
	PlayerController::update_player_vertical_motion(&camera_, input, world_,
		delta_seconds);
	PlayerController::update_player_horizontal_motion(&camera_, input, world_,
		delta_seconds);
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: player motion scope end failed (%d)\n",
			analytics_error);
	sync_player_character_location();
	error_code_ = update_revision_occupancy();
	if (error_code_ != FT_ERR_SUCCESS)
	{
		std::fprintf(stderr,
			"[Session] failed to update occupied regeneration chunks error=%d\n",
			error_code_);
		return (Action::FAILED);
	}
	{
		const int32_t pickup_error = this->pickup_nearby_ground_items();

		if (pickup_error != FT_ERR_SUCCESS)
			std::fprintf(stderr, "[Session] ground-item pickup failed error=%d\n",
				pickup_error);
	}
	active_render_distance_ = std::min(active_render_distance_,
			Settings::instance().render_distance());
	gen = strategy.generation_budget_for_frame(performance_frame_ms_,
			boost_enabled_);
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::GAME_UPDATE);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: stream scope start failed (%d)\n",
			analytics_error);
	error_code_ = world_.update_around(static_cast<double>(player_character_.get_x()),
			static_cast<double>(player_character_.get_z()), gen,
			active_render_distance_);
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: stream scope end failed (%d)\n",
			analytics_error);
	if (error_code_ != FT_ERR_SUCCESS)
	{
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
		World::StreamDiagnostics failure = world_.stream_diagnostics();
		std::fprintf(stderr,
			"[Session] in-game stream update failed error=%d last_error=%d "
			"frame=%llu pending=%zu retryable=%zu failed=%zu "
			"remesh_pending=%zu oldest_remesh_age=%llu\n",
			error_code_, world_.stream_last_error(),
			static_cast<unsigned long long>(failure.frame), failure.pending_count,
			failure.retryable_count, failure.failed_count,
			failure.interactive_remesh_queue_depth,
			static_cast<unsigned long long>(failure.oldest_remesh_queue_age));
	#endif
		if (error_code_ > 0)
		{
			error_code_ = FT_ERR_SUCCESS;
			return (Action::CONTINUE);
		}
		return (Action::FAILED);
	}
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::BLOCK_INTERACTION);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: interaction scope start failed (%d)\n",
			analytics_error);
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_MOUSE_PRIMARY) == FT_TRUE)
	{
		int32_t interaction_error = this->try_attack_aimed_entity();
		if (interaction_error == FT_ERR_NOT_FOUND)
		{
			std::lock_guard<std::mutex> item_instance_lock(
				this->item_instance_transaction_mutex_);
			const GameSessionSnapshotWriter snapshot_writer = {
				&GameSession::save_snapshot_callback, this};
			interaction_error = BlockInteractor::try_delete_target_block(
				&world_, camera_, item_catalog_, world_items_,
				transaction_journal_, &snapshot_writer, world_item_save_path_,
				0U, world_.current_tick,
				&player_character_.get_inventory(), &player_character_);
		}
		if (interaction_error != FT_ERR_SUCCESS)
		{
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
			std::fprintf(stderr,
				"[Session] primary interaction failed error=%d\n",
				interaction_error);
	#endif
		}
	}
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_MOUSE_TERTIARY) == FT_TRUE)
	{
		int32_t interaction_error = BlockInteractor::try_pick_target_block(
			&world_, camera_, &selected_block_id_);
		if (interaction_error != FT_ERR_SUCCESS)
		{
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
			std::fprintf(stderr, "[Session] pick interaction failed error=%d\n",
				interaction_error);
	#endif
		}
	}
	if (ft_dumb_control_was_pressed(FT_DUMB_CONTROL_MOUSE_SECONDARY) == FT_TRUE)
	{
		int32_t interaction_error = this->try_launch_equipped_bow();
		if (interaction_error == FT_ERR_NOT_FOUND)
			interaction_error = this->try_place_selected_inventory_block();
		if (interaction_error != FT_ERR_SUCCESS)
		{
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
			std::fprintf(stderr, "[Session] place interaction failed error=%d\n",
				interaction_error);
	#endif
		}
	}
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: interaction scope end failed (%d)\n",
			analytics_error);
	return (Action::CONTINUE);
}

int32_t GameSession::try_place_selected_inventory_block() noexcept
{
	const int32_t trapdoor_result = this->try_toggle_target_trapdoor();
	if (trapdoor_result != FT_ERR_NOT_FOUND)
		return (trapdoor_result);
	const int32_t till_result = this->try_till_selected_soil();
	if (till_result != FT_ERR_NOT_FOUND)
		return (till_result);
	game_inventory_snapshot inventory_snapshot = {};
	MinecraftBlockItemRule block_rule = {};
	uint32_t resolved_block_id = GAME_VOXEL_AIR_BLOCK;
	double direction_x;
	double direction_y;
	double direction_z;
	int32_t hit_x;
	int32_t hit_y;
	int32_t hit_z;
	int32_t place_x;
	int32_t place_y;
	int32_t place_z;
	uint32_t hit_block_id = GAME_VOXEL_AIR_BLOCK;
	uint64_t request_id = 0U;
	int32_t result;

	if (this->selected_block_id_ == GAME_VOXEL_AIR_BLOCK)
		return (FT_ERR_INVALID_ARGUMENT);
	result = this->item_catalog_.resolve_block_item_rule(
		this->selected_block_id_, &block_rule);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if ((block_rule.policy != MINECRAFT_BLOCK_ACQUISITION_SELF_ITEM
			&& block_rule.policy != MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM)
		|| this->item_catalog_.resolve_placed_block_item(block_rule.item_id,
			&resolved_block_id) != FT_ERR_SUCCESS
		|| resolved_block_id != this->selected_block_id_)
		return (FT_ERR_PERMISSION_DENIED);
	result = this->player_character_.get_inventory().get_snapshot(
		inventory_snapshot);
	if (result != FT_ERR_SUCCESS)
		return (result);
	const auto item = std::find_if(inventory_snapshot.items.begin(),
		inventory_snapshot.items.end(), [&block_rule](
			const game_inventory_item_snapshot &candidate)
		{
			return (candidate.item_id == block_rule.item_id
				&& candidate.stack_size > 0);
		});
	if (item == inventory_snapshot.items.end())
		return (FT_ERR_NOT_FOUND);
	PlayerController::camera_forward(this->camera_, &direction_x,
		&direction_y, &direction_z);
	result = this->world_.raycast_edit_target(this->camera_.x,
		this->camera_.y, this->camera_.z, direction_x, direction_y,
		direction_z, 10.0, &hit_x, &hit_y, &hit_z, &place_x, &place_y,
		&place_z, &hit_block_id);
	if (result != FT_ERR_SUCCESS)
		return (result);
	const int32_t chunk_x = WorldCoordinates::floor_divide(place_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(place_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const WorldChunk *chunk = this->world_.find_chunk(chunk_x, chunk_z);
	if (chunk == nullptr)
		return (FT_ERR_NOT_FOUND);
	result = this->transaction_journal_.next_request_id(
		BLOCK_PLACEMENT_SCOPE, &request_id);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (this->place_inventory_block(request_id, item->slot,
		block_rule.item_id, inventory_snapshot.revision, place_x, place_y,
		place_z, chunk->voxel_revision));
}

int32_t GameSession::try_toggle_target_trapdoor() noexcept
{
	uint32_t hit_block_id = GAME_VOXEL_AIR_BLOCK;
	int32_t hit_x = 0;
	int32_t hit_y = 0;
	int32_t hit_z = 0;
	int32_t place_x = 0;
	int32_t place_y = 0;
	int32_t place_z = 0;
	double direction_x = 0.0;
	double direction_y = 0.0;
	double direction_z = 0.0;
	PlayerController::camera_forward(this->camera_, &direction_x,
		&direction_y, &direction_z);
	const int32_t hit_result = this->world_.raycast_edit_target(this->camera_.x,
		this->camera_.y, this->camera_.z, direction_x, direction_y,
		direction_z, 10.0, &hit_x, &hit_y, &hit_z, &place_x, &place_y,
		&place_z, &hit_block_id);
	if (hit_result != FT_ERR_SUCCESS)
		return (FT_ERR_NOT_FOUND);
	uint32_t replacement_block_id = GAME_VOXEL_AIR_BLOCK;
	if (hit_block_id == VOXEL_GENERATOR_TRAPDOOR_CLOSED_BLOCK_ID)
		replacement_block_id = VOXEL_GENERATOR_TRAPDOOR_OPEN_BLOCK_ID;
	else if (hit_block_id == VOXEL_GENERATOR_TRAPDOOR_OPEN_BLOCK_ID)
		replacement_block_id = VOXEL_GENERATOR_TRAPDOOR_CLOSED_BLOCK_ID;
	else
		return (FT_ERR_NOT_FOUND);
	const int32_t chunk_x = WorldCoordinates::floor_divide(hit_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(hit_z,
		GAME_VOXEL_CHUNK_DEPTH);
	if (this->world_.is_chunk_protected(chunk_x, chunk_z))
		return (FT_ERR_PERMISSION_DENIED);
	const WorldChunk *chunk = this->world_.find_chunk(chunk_x, chunk_z);
	if (chunk == nullptr)
		return (FT_ERR_NOT_FOUND);
	return (this->world_.replace_block_state_at(hit_x, hit_y, hit_z,
		hit_block_id, replacement_block_id, chunk->voxel_revision));
}

int32_t GameSession::try_till_selected_soil() noexcept
{
	game_inventory_snapshot inventory_snapshot = {};
	uint32_t hit_block_id = GAME_VOXEL_AIR_BLOCK;
	int32_t hit_x;
	int32_t hit_y;
	int32_t hit_z;
	int32_t place_x;
	int32_t place_y;
	int32_t place_z;
	int32_t result = this->player_character_.get_inventory().get_snapshot(
		inventory_snapshot);
	if (result != FT_ERR_SUCCESS)
		return (result);
	auto hoe = std::find_if(inventory_snapshot.items.begin(),
		inventory_snapshot.items.end(), [](
			const game_inventory_item_snapshot &item)
		{
			return (item.item_id == MinecraftItemCatalog::WOOD_HOE_ITEM_ID
				&& item.stack_size > 0);
		});
	if (hoe == inventory_snapshot.items.end())
		return (FT_ERR_NOT_FOUND);
	double direction_x;
	double direction_y;
	double direction_z;
	PlayerController::camera_forward(this->camera_, &direction_x,
		&direction_y, &direction_z);
	result = this->world_.raycast_edit_target(this->camera_.x,
		this->camera_.y, this->camera_.z, direction_x, direction_y,
		direction_z, 6.0, &hit_x, &hit_y, &hit_z, &place_x, &place_y,
		&place_z, &hit_block_id);
	if (result == FT_ERR_NOT_FOUND)
		return (FT_ERR_NOT_FOUND);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (hit_block_id != VOXEL_GENERATOR_DIRT_BLOCK_ID
		&& hit_block_id != VOXEL_GENERATOR_GRASS_BLOCK_ID)
		return (FT_ERR_NOT_FOUND);
	const int32_t chunk_x = WorldCoordinates::floor_divide(hit_x,
		GAME_VOXEL_CHUNK_WIDTH);
	const int32_t chunk_z = WorldCoordinates::floor_divide(hit_z,
		GAME_VOXEL_CHUNK_DEPTH);
	const WorldChunk *chunk = this->world_.find_chunk(chunk_x, chunk_z);
	if (chunk == nullptr)
		return (FT_ERR_NOT_FOUND);
	uint64_t request_id = 0U;
	result = this->transaction_journal_.next_request_id(
		WorldFarmlandTransaction::journal_scope(), &request_id);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (this->till_farmland(request_id, hoe->slot,
		inventory_snapshot.revision, hit_x, hit_y, hit_z,
		chunk->voxel_revision));
}

void GameSession::tick_fps_counter(double delta_seconds,
	const RenderDistanceStrategy &strategy)
{
	const double	bounded_sample = std::min(delta_seconds * 1000.0, 100.0);

	rd_accumulator_ += delta_seconds;
	if (strategy.should_update_render_distance(rd_accumulator_))
	{
		active_render_distance_ = strategy.update_render_distance(active_render_distance_,
				performance_frame_ms_, boost_enabled_);
		active_render_distance_ = std::min(active_render_distance_,
				Settings::instance().render_distance());
		rd_accumulator_ = 0.0;
	}
	frame_ms_ = delta_seconds * 1000.0;
	performance_frame_ms_ += (bounded_sample - performance_frame_ms_) * 0.1;
	fps_accumulator_ += delta_seconds;
	fps_frame_count_ += 1U;
	if (fps_accumulator_ >= 0.25)
	{
		display_fps_ = static_cast<double>(fps_frame_count_) / fps_accumulator_;
		fps_accumulator_ = 0.0;
		fps_frame_count_ = 0U;
	}
}

GameSession::Action GameSession::update(double delta_seconds,
	const RenderDistanceStrategy &strategy, ApplicationWindow &window)
{
	Action	nav;
	Action	world_result;
	uint64_t host_epoch_ms = 0U;
	int32_t finalize_result;

	if (!active_)
		return (Action::FAILED);
	if (this->world_loom_start_committed_
		&& this->world_.world_revision().pending
		&& !this->world_.world_revision().regenerating)
	{
		if (this->revision_clock_provider_ == nullptr)
		{
			error_code_ = FT_ERR_INVALID_STATE;
			return (Action::FAILED);
		}
		finalize_result = this->revision_clock_provider_(&host_epoch_ms,
			this->revision_clock_provider_user_data_);
		if (finalize_result != FT_ERR_SUCCESS || host_epoch_ms == 0U)
		{
			error_code_ = finalize_result == FT_ERR_SUCCESS
				? FT_ERR_INVALID_STATE : finalize_result;
			return (Action::FAILED);
		}
		const uint64_t deadline_epoch_ms =
			this->world_.revision_decision_deadline_epoch();
		if (deadline_epoch_ms == 0U || host_epoch_ms >= deadline_epoch_ms)
		{
			finalize_result = this->finalize_regeneration_start(host_epoch_ms);
			if (finalize_result != FT_ERR_SUCCESS
				&& finalize_result != FT_ERR_INVALID_OPERATION)
			{
				error_code_ = finalize_result;
				return (Action::FAILED);
			}
		}
	}
	nav = handle_navigation(window);
	if (nav != Action::CONTINUE)
		return (nav);
	world_result = tick_world(delta_seconds, strategy);
	if (world_result != Action::CONTINUE)
		return (world_result);
	tick_fps_counter(delta_seconds, strategy);
	return (Action::CONTINUE);
}

void GameSession::build_render_debug(VoxelRenderer &renderer)
{
    render_debug_frame_ += 1U;
    render_debug_.fps = display_fps_;
    render_debug_.frame_ms = frame_ms_;
    render_debug_.visible_chunks = 0;
    render_debug_.loaded_chunks = world_.loaded_chunk_count;
    render_debug_.render_distance = world_.active_render_distance;
    render_debug_.selected_block_id = selected_block_id_;
    render_debug_.camera_x = camera_.x;
    render_debug_.camera_y = camera_.y;
    render_debug_.camera_z = camera_.z;
    render_debug_.camera_speed = camera_.speed;
    render_debug_.boost_speed = camera_.speed * 20.0;
    if (render_debug_frame_ == 1U || render_debug_frame_ % 30U == 0U)
    {
        cached_ram_mb_ = SystemMemoryInfo::resident_set_mb();
        cached_vram_mb_ = renderer.get_gpu_renderer()
            ? renderer.get_gpu_renderer()->gpu_mb_approx() : 0U;
    }
    render_debug_.ram_mb = cached_ram_mb_;
    render_debug_.vram_approx_mb = cached_vram_mb_;
    std::strncpy(render_debug_.seed, seed_, sizeof(render_debug_.seed) - 1);
    render_debug_.seed[sizeof(render_debug_.seed) - 1] = '\0';

    const int32_t biome_world_x = player_character_.get_x();
    const int32_t biome_world_z = player_character_.get_z();
    if (!cached_biome_valid_
        || cached_biome_world_x_ != biome_world_x
        || cached_biome_world_z_ != biome_world_z)
    {
        const uint32_t biome_index = voxel_get_biome_index(
            world_.voxel_generation_settings(), biome_world_x,
            biome_world_z, seed_);
        const char *bname = (biome_index < 5U)
            ? BIOME_NAMES[biome_index] : nullptr;
        if (bname != nullptr)
            std::strncpy(cached_biome_name_, bname,
                sizeof(cached_biome_name_) - 1);
        else
            std::snprintf(cached_biome_name_, sizeof(cached_biome_name_),
                "CUSTOM_%u", biome_index);
        cached_biome_name_[sizeof(cached_biome_name_) - 1] = '\0';
        cached_biome_world_x_ = biome_world_x;
        cached_biome_world_z_ = biome_world_z;
        cached_biome_valid_ = true;
    }
    std::strncpy(render_debug_.biome_name, cached_biome_name_,
        sizeof(render_debug_.biome_name) - 1);
    render_debug_.biome_name[sizeof(render_debug_.biome_name) - 1] = '\0';
    render_debug_.revision_barriers_active =
		world_.revision_barriers_active();
    render_debug_.revision_preview_visible = revision_preview_visible_
		|| render_debug_.revision_barriers_active;
    if (!render_debug_.revision_preview_visible)
        return;
    render_debug_.revision_pending = world_.world_revision().pending;
    int32_t center_x = WorldCoordinates::floor_divide(
        static_cast<int32_t>(std::floor(camera_.x)), GAME_VOXEL_CHUNK_WIDTH);
    int32_t center_z = WorldCoordinates::floor_divide(
        static_cast<int32_t>(std::floor(camera_.z)), GAME_VOXEL_CHUNK_DEPTH);
    const World::WorldRevision revision = world_.world_revision();
    std::vector<World::RevisionChunkCoordinate> volcano_chunks;
    if (revision.pending)
    {
        std::vector<RegenerationWorldFeaturePlan> feature_plans;
        std::vector<World::RevisionChunkCoordinate> selected_chunks;
        std::vector<World::RevisionChunkCoordinate> paid_chunks;
        if (this->regeneration_world_rules_.copy_feature_plans(feature_plans)
                == FT_ERR_SUCCESS
            && this->world_.copy_selected_revision_chunks(selected_chunks)
                == FT_ERR_SUCCESS
            && this->world_.copy_selected_paid_revision_chunks(paid_chunks)
                == FT_ERR_SUCCESS)
            (void)game_session_build_volcano_feature_preview(selected_chunks,
                paid_chunks, feature_plans, volcano_chunks);
    }
    if (!volcano_chunks.empty())
    {
        int32_t minimum_x = volcano_chunks[0].chunk_x;
        int32_t maximum_x = volcano_chunks[0].chunk_x;
        int32_t minimum_z = volcano_chunks[0].chunk_z;
        int32_t maximum_z = volcano_chunks[0].chunk_z;
        for (const World::RevisionChunkCoordinate &chunk : volcano_chunks)
        {
            minimum_x = std::min(minimum_x, chunk.chunk_x);
            maximum_x = std::max(maximum_x, chunk.chunk_x);
            minimum_z = std::min(minimum_z, chunk.chunk_z);
            maximum_z = std::max(maximum_z, chunk.chunk_z);
        }
        center_x = static_cast<int32_t>((static_cast<int64_t>(minimum_x)
            + maximum_x) / 2);
        center_z = static_cast<int32_t>((static_cast<int64_t>(minimum_z)
            + maximum_z) / 2);
    }
    if (!revision_preview_cache_valid_
        || center_x != revision_preview_center_x_
        || center_z != revision_preview_center_z_
        || revision.identifier != revision_preview_identifier_
        || render_debug_frame_ % 15U == 0U)
    {
        revision_preview_cache_.clear();
        if (world_.build_revision_preview(center_x, center_z,
                static_cast<int32_t>(RenderDebug::REVISION_PREVIEW_RADIUS),
                revision_preview_cache_) != FT_ERR_SUCCESS)
            return;
        revision_preview_center_x_ = center_x;
        revision_preview_center_z_ = center_z;
        revision_preview_identifier_ = revision.identifier;
        revision_preview_cache_valid_ = true;
    }
    render_debug_.revision_map_radius = static_cast<uint8_t>(
        RenderDebug::REVISION_PREVIEW_RADIUS);
    std::fill(render_debug_.revision_protection_reasons,
        render_debug_.revision_protection_reasons
            + sizeof(render_debug_.revision_protection_reasons), 0U);
    std::fill(render_debug_.revision_feature_highlights,
        render_debug_.revision_feature_highlights
            + sizeof(render_debug_.revision_feature_highlights), 0U);
    render_debug_.revision_protected_count = 0U;
    render_debug_.revision_selected_count = 0U;
    render_debug_.revision_transition_count = 0U;
    render_debug_.revision_unchanged_count = 0U;
    for (const World::RevisionChunkCoordinate &chunk : volcano_chunks)
    {
		const int64_t local_x = static_cast<int64_t>(chunk.chunk_x)
			- center_x + RenderDebug::REVISION_PREVIEW_RADIUS;
		const int64_t local_z = static_cast<int64_t>(chunk.chunk_z)
			- center_z + RenderDebug::REVISION_PREVIEW_RADIUS;
		if (local_x < 0
			|| local_x >= RenderDebug::REVISION_PREVIEW_WIDTH
			|| local_z < 0
			|| local_z >= RenderDebug::REVISION_PREVIEW_WIDTH)
			continue ;
		const std::size_t map_index = static_cast<std::size_t>(
			local_z * RenderDebug::REVISION_PREVIEW_WIDTH + local_x);
		render_debug_.revision_feature_highlights[map_index] = 1U;
    }
    for (const World::RevisionPreviewEntry &entry : revision_preview_cache_)
    {
        const int64_t local_x = static_cast<int64_t>(entry.chunk_x) - center_x
            + RenderDebug::REVISION_PREVIEW_RADIUS;
        const int64_t local_z = static_cast<int64_t>(entry.chunk_z) - center_z
            + RenderDebug::REVISION_PREVIEW_RADIUS;
        if (local_x < 0
            || local_x >= RenderDebug::REVISION_PREVIEW_WIDTH
            || local_z < 0
            || local_z >= RenderDebug::REVISION_PREVIEW_WIDTH)
            continue ;
        const std::size_t map_index = static_cast<std::size_t>(local_z)
            * RenderDebug::REVISION_PREVIEW_WIDTH
            + static_cast<std::size_t>(local_x);
        render_debug_.revision_map[map_index] =
            static_cast<uint8_t>(entry.state);
        if (entry.protection_info.generation_protected)
            render_debug_.revision_protection_reasons[map_index] |=
                RenderDebug::REVISION_PROTECTION_GENERATION;
        if (entry.protection_info.loom_safe_zone)
            render_debug_.revision_protection_reasons[map_index] |=
                RenderDebug::REVISION_PROTECTION_LOOM;
        if (entry.protection_info.manually_protected)
            render_debug_.revision_protection_reasons[map_index] |=
                RenderDebug::REVISION_PROTECTION_MANUAL;
        if (entry.protection_info.occupied_player_exclusion)
            render_debug_.revision_protection_reasons[map_index] |=
                RenderDebug::REVISION_PROTECTION_OCCUPIED;
        if (entry.state == World::REVISION_PROTECTED)
            render_debug_.revision_protected_count += 1U;
        else if (entry.state == World::REVISION_SELECTED)
            render_debug_.revision_selected_count += 1U;
        else if (entry.state == World::REVISION_TRANSITION)
            render_debug_.revision_transition_count += 1U;
        else
            render_debug_.revision_unchanged_count += 1U;
    }
}

void GameSession::sync_player_character_location()
{
	player_character_.set_x(static_cast<int32_t>(std::floor(camera_.x)));
	player_character_.set_y(static_cast<int32_t>(std::floor(camera_.y)));
	player_character_.set_z(static_cast<int32_t>(std::floor(camera_.z)));
}

int32_t GameSession::update_revision_occupancy() noexcept
{
	if (this->revision_occupancy_provider_ != nullptr)
	{
		std::vector<World::RevisionChunkCoordinate> staged_occupied;
		uint64_t current_selection_revision = 0U;
		int32_t result = this->revision_occupancy_provider_(staged_occupied,
			this->revision_occupancy_provider_user_data_);
		if (result != FT_ERR_SUCCESS)
			return (result);
		result = this->world_.set_revision_occupied_chunks(staged_occupied,
			&current_selection_revision);
		if (result == FT_ERR_SUCCESS)
		{
			this->revision_occupied_chunks_.swap(staged_occupied);
			this->revision_occupancy_valid_ = false;
		}
		return (result);
	}
	const int32_t chunk_width = GAME_VOXEL_CHUNK_WIDTH;
	const int32_t chunk_depth = GAME_VOXEL_CHUNK_DEPTH;
	int32_t player_x;
	int32_t player_z;
	int32_t chunk_x;
	int32_t chunk_z;
	uint64_t current_selection_revision;

	if (chunk_width <= 0 || chunk_depth <= 0)
		return (FT_ERR_INVALID_STATE);
	player_x = this->player_character_.get_x();
	player_z = this->player_character_.get_z();
	chunk_x = player_x / chunk_width;
	chunk_z = player_z / chunk_depth;
	if (player_x < 0 && player_x % chunk_width != 0)
		chunk_x -= 1;
	if (player_z < 0 && player_z % chunk_depth != 0)
		chunk_z -= 1;
	if (this->revision_occupancy_valid_
		&& this->revision_occupied_chunk_x_ == chunk_x
		&& this->revision_occupied_chunk_z_ == chunk_z)
		return (FT_ERR_SUCCESS);
	try
	{
		if (this->revision_occupied_chunks_.empty())
			this->revision_occupied_chunks_.push_back({chunk_x, chunk_z});
		else
		{
			this->revision_occupied_chunks_[0].chunk_x = chunk_x;
			this->revision_occupied_chunks_[0].chunk_z = chunk_z;
		}
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	const int32_t result = this->world_.set_revision_occupied_chunks(
		this->revision_occupied_chunks_, &current_selection_revision);
	if (result == FT_ERR_SUCCESS)
	{
		this->revision_occupied_chunk_x_ = chunk_x;
		this->revision_occupied_chunk_z_ = chunk_z;
		this->revision_occupancy_valid_ = true;
	}
	return (result);
}

int32_t GameSession::recover_pending_transactions() noexcept
{
	const GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};
	std::vector<GameTransactionJournal::Record> pending_records;
	bool uncommitted_revision_cancelled = false;
	int32_t error_code = WorldItemPickupTransaction::recover_pending(
		this->world_item_save_path_, this->player_character_,
		this->world_items_, this->recipe_catalog_,
		this->transaction_journal_, &snapshot_writer);
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemCraftTransaction::recover_pending(
			this->world_item_save_path_, this->player_character_,
			this->world_items_, this->recipe_catalog_,
			this->transaction_journal_, &snapshot_writer);
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemProcessingTransaction::recover_pending(
			this->world_item_save_path_, this->player_character_,
			this->world_items_, this->recipe_catalog_,
			this->transaction_journal_, &snapshot_writer);
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemProcessingAdvanceTransaction::recover_pending(
			this->world_item_save_path_, this->player_character_,
			this->world_items_, this->recipe_catalog_,
			this->transaction_journal_, &snapshot_writer);
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldItemCardTransferTransaction::recover_pending(
			this->world_item_save_path_, this->player_character_,
			this->world_items_, this->recipe_catalog_, this->regeneration_cards_,
			this->transaction_journal_, &snapshot_writer);
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldBoatTransferTransaction::recover_pending(
			this->world_item_save_path_, this->player_character_,
			this->world_items_, this->world_entities_,
			this->transaction_journal_, &snapshot_writer);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->recover_pending_block_placements();
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->recover_pending_farmland_tillings();
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldBlockMiningTransaction::recover_pending(
			this->world_item_save_path_, this->world_, this->item_catalog_,
			this->world_items_, this->transaction_journal_, &snapshot_writer,
			&this->player_character_.get_inventory(),
			&this->player_character_);
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldEquipmentRepairTransaction::recover_pending(
			this->world_item_save_path_, this->player_character_,
			this->item_catalog_, this->transaction_journal_, &snapshot_writer);
	if (error_code == FT_ERR_SUCCESS)
		error_code = WorldEquipmentLoadoutTransaction::recover_pending(
			this->world_item_save_path_, this->player_character_,
			this->item_catalog_, this->transaction_journal_, &snapshot_writer);
	if (error_code == FT_ERR_SUCCESS)
	{
		double direction_x = 0.0;
		double direction_y = 0.0;
		double direction_z = 0.0;
		PlayerController::camera_forward(this->camera_, &direction_x,
			&direction_y, &direction_z);
		error_code = WorldProjectileLaunchTransaction::recover_pending(
			this->world_item_save_path_, this->player_character_,
			this->world_entities_, this->item_catalog_, this->transaction_journal_,
			this->world_.current_tick, this->camera_.x, this->camera_.y,
			this->camera_.z, direction_x, direction_y, direction_z,
			&snapshot_writer);
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->recover_pending_regeneration_draw_notices();
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->recover_pending_recipe_research();
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->transaction_journal_.pending_records(pending_records);
	bool start_record_aborted = false;
	for (const GameTransactionJournal::Record &record : pending_records)
	{
		if (record.scope_id != WORLD_LOOM_START_SCOPE)
			continue ;
		if (record.transaction_type != WORLD_LOOM_START_TRANSACTION_TYPE
			|| record.request_id == 0U || record.payload.size() != 56U
			|| record.payload[0] != 'W' || record.payload[1] != 'L'
			|| record.payload[2] != 'S' || record.payload[3] != 'T'
			|| game_session_read_u32(record.payload, 4U) != 1U
			|| game_session_read_u64(record.payload, 8U) == 0U
			|| game_session_read_u64(record.payload, 16U) == 0U
			|| game_session_read_u32(record.payload, 40U) < 4U
			|| game_session_read_u32(record.payload, 40U) > 7U
			|| game_session_read_u32(record.payload, 44U) == 0U
			|| game_session_read_u32(record.payload, 44U)
				> static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
			|| game_session_read_u64(record.payload, 48U)
				> static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
			return (FT_ERR_INVALID_ARGUMENT);
		/* The prepared checkpoint is pre-debit and pre-deal. Abort it, then
		 * discard the orphaned uncommitted draft below. */
		error_code = this->transaction_journal_.mark_aborted(
			WORLD_LOOM_START_SCOPE, record.request_id);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		start_record_aborted = true;
	}
	const World::WorldRevision restored_revision = this->world_.world_revision();
	if (restored_revision.pending && !restored_revision.regenerating
		&& !this->world_.revision_barriers_active())
	{
		error_code = this->world_.cancel_world_revision();
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		this->world_loom_start_committed_ = false;
		uncommitted_revision_cancelled = true;
	}
	if (start_record_aborted || uncommitted_revision_cancelled)
		error_code = this->save_session_snapshot();
	return (error_code);
}

int32_t GameSession::recover_pending_recipe_research() noexcept
{
	std::vector<GameTransactionJournal::Record> records;
	int32_t result = this->transaction_journal_.pending_records(records);
	if (result != FT_ERR_SUCCESS)
		return (result);
	for (const GameTransactionJournal::Record &record : records)
	{
		if (record.scope_id != RECIPE_RESEARCH_TRANSACTION_SCOPE)
			continue ;
		if (record.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_STATE);
		uint64_t expected_inventory_revision = 0U;
		uint64_t expected_station_revision = 0U;
		uint64_t random_value = 0U;
		uint32_t station_id = 0U;
		uint32_t recipe_id = 0U;
		int32_t station_x = 0;
		int32_t station_y = 0;
		int32_t station_z = 0;
		if (!game_session_decode_recipe_research(record,
				&expected_inventory_revision, &station_id, &station_x,
				&station_y, &station_z, &expected_station_revision,
				&random_value, &recipe_id))
			return (FT_ERR_INVALID_ARGUMENT);
		(void)station_id;
		(void)station_x;
		(void)station_y;
		(void)station_z;
		(void)expected_station_revision;
		(void)random_value;
		game_inventory_snapshot inventory_before = {};
		result = this->player_character_.get_inventory().get_snapshot(
			inventory_before);
		if (result != FT_ERR_SUCCESS)
			return (result);
		if (inventory_before.revision != expected_inventory_revision)
			return (FT_ERR_INVALID_STATE);
		game_inventory_quantity_result debit = {};
		result = this->player_character_.get_inventory().remove_item_quantity(
			RECIPE_RESEARCH_COST_ITEM_ID,
			static_cast<int32_t>(RECIPE_RESEARCH_COST_QUANTITY),
			expected_inventory_revision, RECIPE_RESEARCH_TRANSACTION_SCOPE,
			record.request_id, &debit);
		if (result != FT_ERR_SUCCESS)
			return (result);
		ft_bool newly_unlocked = FT_FALSE;
		result = this->recipe_catalog_.grant_recipe_from_discovery(
			RECIPE_RESEARCH_SOURCE_TYPE, recipe_id, record.request_id,
			&newly_unlocked);
		if (result != FT_ERR_SUCCESS)
		{
			const int32_t grant_error = result;
			const int32_t restore_result =
				this->player_character_.get_inventory().restore_snapshot(
					inventory_before);
			if (restore_result != FT_ERR_SUCCESS)
				return (restore_result);
			result = this->transaction_journal_.mark_aborted(
				RECIPE_RESEARCH_TRANSACTION_SCOPE, record.request_id);
			if (result == FT_ERR_SUCCESS)
				result = this->save_session_snapshot();
			return (result == FT_ERR_SUCCESS ? grant_error : result);
		}
		result = this->transaction_journal_.mark_committed(
			RECIPE_RESEARCH_TRANSACTION_SCOPE, record.request_id);
		if (result == FT_ERR_SUCCESS)
			result = this->save_session_snapshot();
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::recover_pending_regeneration_draw_notices() noexcept
{
	std::vector<GameTransactionJournal::Record> pending_records;
	int32_t result = this->transaction_journal_.pending_records(
		pending_records);
	if (result != FT_ERR_SUCCESS)
		return (result);
	for (const GameTransactionJournal::Record &record : pending_records)
	{
		bool was_pending = false;

		if (record.scope_id != DRAW_NOTICE_TRANSACTION_SCOPE)
			continue ;
		if (record.state != GameTransactionJournal::PREPARED)
			return (FT_ERR_INVALID_STATE);
		result = game_session_apply_draw_notice_transaction(this->world_,
			record);
		if (result == FT_ERR_SUCCESS)
			result = this->regeneration_cards_.
				acknowledge_draw_notice_if_pending(record.request_id,
					&was_pending);
		if (result == FT_ERR_SUCCESS)
			result = this->save_session_snapshot();
		if (result == FT_ERR_SUCCESS)
			result = this->transaction_journal_.mark_committed(
				DRAW_NOTICE_TRANSACTION_SCOPE, record.request_id);
		if (result == FT_ERR_SUCCESS)
			result = this->transaction_journal_.prune_terminal_scope(
				DRAW_NOTICE_TRANSACTION_SCOPE, record.request_id);
		if (result == FT_ERR_SUCCESS)
			result = this->save_session_snapshot();
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::recover_pending_block_placements() noexcept
{
	std::vector<GameTransactionJournal::Record> pending_records;
	int32_t result = this->transaction_journal_.pending_records(pending_records);
	if (result != FT_ERR_SUCCESS)
		return (result);
	for (const GameTransactionJournal::Record &record : pending_records)
	{
		if (record.scope_id != BLOCK_PLACEMENT_SCOPE)
			continue ;
		if (record.transaction_type != BLOCK_PLACEMENT_TRANSACTION_TYPE
			|| record.state != GameTransactionJournal::PREPARED
			|| record.request_id == 0U
			|| record.payload.size() != BLOCK_PLACEMENT_PAYLOAD_SIZE
			|| record.payload[0] != 'G' || record.payload[1] != 'B'
			|| record.payload[2] != 'P' || record.payload[3] != 'L'
			|| game_session_read_u32(record.payload, 4U) != 1U
			|| game_session_read_u64(record.payload, 8U) != record.request_id)
			return (FT_ERR_INVALID_ARGUMENT);
		const int32_t slot = static_cast<int32_t>(
			game_session_read_u32(record.payload, 16U));
		const int32_t item_id = static_cast<int32_t>(
			game_session_read_u32(record.payload, 20U));
		const uint32_t block_id = game_session_read_u32(record.payload, 24U);
		const uint64_t expected_inventory_revision =
			game_session_read_u64(record.payload, 28U);
		const int32_t world_x = static_cast<int32_t>(
			game_session_read_u32(record.payload, 36U));
		const int32_t world_y = static_cast<int32_t>(
			game_session_read_u32(record.payload, 40U));
		const int32_t world_z = static_cast<int32_t>(
			game_session_read_u32(record.payload, 44U));
		const uint64_t expected_voxel_revision =
			game_session_read_u64(record.payload, 48U);
		game_inventory &inventory = this->player_character_.get_inventory();
		game_inventory_snapshot inventory_before = {};
		game_inventory_snapshot inventory_after = {};
		uint32_t mapped_block_id = 0U;
		if (slot < 0 || item_id <= 0 || expected_inventory_revision == 0U
			|| expected_voxel_revision == 0U
			|| this->item_catalog_.resolve_placed_block_item(item_id,
				&mapped_block_id) != FT_ERR_SUCCESS
			|| mapped_block_id != block_id)
			return (FT_ERR_INVALID_ARGUMENT);
		result = inventory.get_snapshot(inventory_before);
		if (result == FT_ERR_SUCCESS)
			result = game_session_stage_item_consumption(inventory_before,
				slot, item_id, expected_inventory_revision, inventory_after);
		if (result != FT_ERR_SUCCESS)
			return (result);
		result = this->world_.set_block_state_override(world_x, world_y,
			world_z, block_id);
		if (result != FT_ERR_SUCCESS)
			return (result);
		result = inventory.restore_snapshot(inventory_after);
		if (result != FT_ERR_SUCCESS)
		{
			(void)this->world_.set_block_state_override(world_x, world_y,
				world_z, GAME_VOXEL_AIR_BLOCK);
			return (result);
		}
		result = this->transaction_journal_.mark_committed(
			BLOCK_PLACEMENT_SCOPE, record.request_id);
		if (result == FT_ERR_SUCCESS)
			result = this->save_session_snapshot();
		if (result != FT_ERR_SUCCESS)
			return (result);
	}
	return (FT_ERR_SUCCESS);
}

int32_t GameSession::recover_pending_farmland_tillings() noexcept
{
	GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};
	return (WorldFarmlandTransaction::recover_pending(
		this->world_item_save_path_, this->world_, this->transaction_journal_,
		&snapshot_writer, &this->player_character_, &this->item_catalog_));
}
int32_t GameSession::pickup_nearby_ground_items() noexcept
{
	static const uint64_t SINGLE_PLAYER_ACTOR_ID = 1U;
	static const double PICKUP_QUERY_RADIUS = 1.5;
	std::lock_guard<std::mutex> item_transaction_lock(
		this->item_instance_transaction_mutex_);
	std::vector<WorldItemSnapshot> nearby_items;
	const GameSessionSnapshotWriter snapshot_writer = {
		&GameSession::save_snapshot_callback, this};
	int32_t error_code;

	error_code = this->world_items_.query_nearby(this->camera_.x,
		this->camera_.y, this->camera_.z, PICKUP_QUERY_RADIUS,
		SINGLE_PLAYER_ACTOR_ID, this->world_.current_tick, &nearby_items);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	for (const WorldItemSnapshot &item : nearby_items)
	{
		uint64_t inventory_revision;
		const uint64_t request_id = this->pickup_request_id_++;
		WorldItemPickupResult pickup_result = {};

		if (request_id == 0U)
			return (FT_ERR_OUT_OF_RANGE);
		error_code = this->player_character_.get_inventory().get_revision(
			&inventory_revision);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		error_code = WorldItemPickupTransaction::execute(
			this->world_item_save_path_, this->player_character_,
			this->world_items_, this->recipe_catalog_,
			this->transaction_journal_, item, SINGLE_PLAYER_ACTOR_ID, request_id,
			inventory_revision, this->camera_.x,
			this->camera_.y, this->camera_.z, this->world_.current_tick,
			&pickup_result, &snapshot_writer);
		if (error_code == FT_ERR_SUCCESS || error_code == FT_ERR_FULL
			|| error_code == FT_ERR_INVALID_STATE
			|| error_code == FT_ERR_NOT_FOUND)
			continue ;
		return (error_code);
	}
	return (FT_ERR_SUCCESS);
}

void GameSession::render(ApplicationWindow &window, VoxelRenderer &renderer,
	bool with_overlay)
{
    int32_t analytics_error = RuntimeAnalytics::begin_scope(
        RuntimeAnalyticsScope::GAME_SESSION_RENDER);
    if (analytics_error != FT_ERR_SUCCESS)
        std::fprintf(stderr, "Analytics: game render scope start failed (%d)\n",
            analytics_error);
    if (!active_)
    {
        analytics_error = RuntimeAnalytics::end_scope();
        if (analytics_error != FT_ERR_SUCCESS)
            std::fprintf(stderr, "Analytics: game render scope end failed (%d)\n",
                analytics_error);
        return;
    }
    const bool debug_enabled = with_overlay
        && (Settings::instance().fps_overlay() || revision_preview_visible_
			|| world_.revision_barriers_active());
    if (!debug_enabled)
    {
        renderer.render_world(window.framebuffer(), camera_, world_,
            static_cast<const RenderDebug *>(nullptr));
        analytics_error = RuntimeAnalytics::end_scope();
        if (analytics_error != FT_ERR_SUCCESS)
            std::fprintf(stderr, "Analytics: game render scope end failed (%d)\n",
                analytics_error);
        return;
    }
    build_render_debug(renderer);
    const RenderDebug *dbg = &render_debug_;
    renderer.render_world(window.framebuffer(), camera_, world_, dbg);
    analytics_error = RuntimeAnalytics::end_scope();
    if (analytics_error != FT_ERR_SUCCESS)
        std::fprintf(stderr, "Analytics: game render scope end failed (%d)\n",
            analytics_error);
}
