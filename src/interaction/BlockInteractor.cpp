#include "../../src/interaction/BlockInteractor.hpp"
#include "../../src/inventory/MinecraftLootCatalog.hpp"
#include "../../src/coordinates/WorldCoordinates.hpp"
#include "../../Libft/Modules/Game/game_inventory.hpp"
#include "../../Libft/Modules/Game/game_character.hpp"

namespace
{
	static int32_t block_interactor_noop_checkpoint(void *) noexcept
	{
		return (FT_ERR_SUCCESS);
	}
}

BlockInteractor::BlockInteractor()
{
}
BlockInteractor::BlockInteractor(const BlockInteractor &other)
{
	(void)other;
}
BlockInteractor::~BlockInteractor()
{
}
BlockInteractor &BlockInteractor::operator=(const BlockInteractor &other)
{
	(void)other;
	return (*this);
}

void BlockInteractor::get_camera_direction(const Camera &camera, double &dx,
	double &dy, double &dz)
{
	PlayerController::camera_forward(camera, &dx, &dy, &dz);
}

int32_t BlockInteractor::try_delete_target_block(World *world,
	const Camera &camera)
{
	if (!world)
		return (FT_ERR_INVALID_ARGUMENT);
	double dx, dy, dz;
	get_camera_direction(camera, dx, dy, dz);
	int32_t bx, by, bz;
	if (world->raycast_solid(camera.x, camera.y, camera.z, dx, dy, dz, 10.0,
			&bx, &by, &bz) != FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	DeleteBlockCommand cmd(bx, by, bz);
	return (cmd.execute(*world));
}

int32_t BlockInteractor::try_delete_target_block(World *world,
	const Camera &camera, MinecraftItemCatalog &catalog,
	WorldItemStore &world_items, uint64_t owner_id, uint64_t server_tick)
{
	GameTransactionJournal journal;
	const GameSessionSnapshotWriter writer = {
		&block_interactor_noop_checkpoint, &journal};
	int32_t result = journal.initialize();
	if (result != FT_ERR_SUCCESS)
		return (result);
	/* Compatibility entrypoint for isolated interaction fixtures. Session
	 * gameplay uses the durable overload with its snapshot writer. */
	result = try_delete_target_block(world, camera, catalog, world_items,
		journal, &writer, "volatile-block-mining", owner_id, server_tick);
	(void)journal.destroy();
	return (result);
}

int32_t BlockInteractor::try_delete_target_block(World *world,
	const Camera &camera, MinecraftItemCatalog &catalog,
	WorldItemStore &world_items, GameTransactionJournal &journal,
	const GameSessionSnapshotWriter *snapshot_writer,
	const char *snapshot_path, uint64_t owner_id, uint64_t server_tick,
	game_inventory *inventory, game_character *character)
{
	MinecraftBlockItemRule rule = {};
	MinecraftLootDrop bonus_drop = {};
	WorldMiningDrop drops[2] = {};
	uint32_t drop_count = 0U;
	uint32_t block_id;
	const WorldChunk *target_chunk;
	int32_t bx;
	int32_t by;
	int32_t bz;
	int32_t place_x;
	int32_t place_y;
	int32_t place_z;
	int32_t error_code;
	int32_t drop_error = FT_ERR_SUCCESS;
	int32_t bonus_item_id = 0;
	uint64_t request_id = 0U;
	double dx;
	double dy;
	double dz;

	if (world == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = WorldBlockMiningTransaction::recover_pending(snapshot_path,
		*world, catalog, world_items, journal, snapshot_writer, inventory,
		character);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	int32_t tool_slot = -1;
	int32_t tool_item_id = 0;
	uint64_t tool_instance_id = 0U;
	uint64_t inventory_revision = 0U;
	MinecraftMiningToolClass required_class = MINECRAFT_MINING_TOOL_NONE;
	uint32_t required_power = 0U;
	BlockInteractor::get_camera_direction(camera, dx, dy, dz);
	uint32_t hit_block_id = GAME_VOXEL_AIR_BLOCK;
	if (world->raycast_edit_target(camera.x, camera.y, camera.z, dx, dy, dz,
			10.0, &bx, &by, &bz, &place_x, &place_y, &place_z,
			&hit_block_id) != FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	if (!world->block_id_at(bx, by, bz, &block_id)
		|| voxel_block_is_breakable(block_id) == FT_FALSE)
		return (FT_ERR_SUCCESS);
	error_code = catalog.resolve_block_mining_requirement(block_id,
		&required_class, &required_power);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if ((inventory != nullptr || character != nullptr)
		&& required_class != MINECRAFT_MINING_TOOL_NONE)
	{
		game_inventory_snapshot snapshot = {};
		game_inventory *authoritative_inventory = inventory;
		if (authoritative_inventory == nullptr && character != nullptr)
			authoritative_inventory = &character->get_inventory();
		error_code = authoritative_inventory->get_snapshot(snapshot);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (character != nullptr)
		{
			ft_sharedptr<game_item> *equipped_tool =
				character->get_equipped_item(EQUIP_WEAPON);
			if (equipped_tool == nullptr)
				return (character->get_error());
			if (!(*equipped_tool))
				error_code = FT_ERR_PERMISSION_DENIED;
			else
			{
				const int32_t item_id = (*equipped_tool)->get_item_id();
				const bool matching_class =
					required_class == MINECRAFT_MINING_TOOL_PICKAXE
						? item_id >= MinecraftItemCatalog::WOOD_PICKAXE_ITEM_ID
							&& item_id <= MinecraftItemCatalog::DIAMOND_PICKAXE_ITEM_ID
						: item_id >= MinecraftItemCatalog::WOOD_AXE_ITEM_ID
							&& item_id <= MinecraftItemCatalog::DIAMOND_AXE_ITEM_ID;
				MinecraftEquipmentRule equipped_rule = {};
				game_inventory_item_snapshot equipped_snapshot = {};
				uint32_t durability = 0U;
				uint32_t maximum = 0U;
				equipped_snapshot.item_id = item_id;
				equipped_snapshot.instance_id =
					(*equipped_tool)->get_instance_id();
				equipped_snapshot.stack_size = 1;
				equipped_snapshot.max_stack = 1;
				equipped_snapshot.modifier1_id =
					(*equipped_tool)->get_modifier1_id();
				equipped_snapshot.modifier1_value =
					(*equipped_tool)->get_modifier1_value();
				error_code = catalog.resolve_equipment_rule(item_id,
					&equipped_rule);
				if (error_code == FT_ERR_SUCCESS
					&& (!matching_class
						|| equipped_rule.slot != MINECRAFT_EQUIPMENT_MAIN_HAND
						|| equipped_rule.mining_power < required_power))
					error_code = FT_ERR_PERMISSION_DENIED;
				if (error_code == FT_ERR_SUCCESS)
					error_code = catalog.query_equipment_durability(
						equipped_snapshot, &durability, &maximum);
				if (error_code == FT_ERR_SUCCESS
					&& (durability == 0U || equipped_snapshot.instance_id == 0U))
					error_code = FT_ERR_PERMISSION_DENIED;
			if (error_code == FT_ERR_SUCCESS)
			{
				tool_slot = -2;
				tool_item_id = item_id;
				tool_instance_id = equipped_snapshot.instance_id;
				inventory_revision = snapshot.revision;
			}
			(void)maximum;
			(void)equipped_tool->destroy();
			delete equipped_tool;
		}
		}
		else
		{
			MinecraftEquipmentRule best_rule = {};
			for (const game_inventory_item_snapshot &candidate : snapshot.items)
			{
				const bool matching_class =
					required_class == MINECRAFT_MINING_TOOL_PICKAXE
						? candidate.item_id
							>= MinecraftItemCatalog::WOOD_PICKAXE_ITEM_ID
							&& candidate.item_id
							<= MinecraftItemCatalog::DIAMOND_PICKAXE_ITEM_ID
						: candidate.item_id
							>= MinecraftItemCatalog::WOOD_AXE_ITEM_ID
							&& candidate.item_id
							<= MinecraftItemCatalog::DIAMOND_AXE_ITEM_ID;
				MinecraftEquipmentRule candidate_rule = {};
				uint32_t durability = 0U;
				uint32_t maximum = 0U;
				if (!matching_class
					|| catalog.resolve_equipment_rule(candidate.item_id,
						&candidate_rule) != FT_ERR_SUCCESS
					|| candidate_rule.mining_power < required_power
					|| catalog.query_equipment_durability(candidate, &durability,
						&maximum) != FT_ERR_SUCCESS || durability == 0U
					|| candidate.instance_id == 0U)
					continue ;
				(void)maximum;
				if (tool_slot < 0
					|| candidate_rule.mining_power > best_rule.mining_power)
				{
					tool_slot = candidate.slot;
					tool_item_id = candidate.item_id;
					tool_instance_id = candidate.instance_id;
					best_rule = candidate_rule;
				}
			}
			if (tool_slot < 0)
				return (FT_ERR_PERMISSION_DENIED);
			inventory_revision = snapshot.revision;
		}
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
	}
	error_code = catalog.resolve_block_item_rule(block_id, &rule);
	if (error_code == FT_ERR_NOT_FOUND)
		return (FT_ERR_INVALID_STATE);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (block_id == VOXEL_GENERATOR_WHEAT_CROP_MATURE_BLOCK_ID)
		bonus_item_id = MinecraftItemCatalog::SEEDS_ITEM_ID;
	else
	{
		drop_error = MinecraftLootCatalog::resolve_tree_fruit(block_id, bx, by,
			bz, server_tick, &bonus_drop);
		if (drop_error == FT_ERR_SUCCESS)
			bonus_item_id = bonus_drop.item_id;
		else if (drop_error != FT_ERR_NOT_FOUND)
			return (drop_error);
	}
	if (rule.policy == MINECRAFT_BLOCK_ACQUISITION_SELF_ITEM
		|| rule.policy == MINECRAFT_BLOCK_ACQUISITION_OTHER_ITEM)
	{
		drops[drop_count++] = {rule.item_id, 1};
		if (bonus_item_id > 0)
			drops[drop_count++] = {bonus_item_id,
				bonus_drop.quantity > 0 ? bonus_drop.quantity : 1};
	}
	target_chunk = world->find_chunk(WorldCoordinates::floor_divide(bx,
		GAME_VOXEL_CHUNK_WIDTH), WorldCoordinates::floor_divide(bz,
		GAME_VOXEL_CHUNK_DEPTH));
	if (target_chunk == nullptr)
		return (FT_ERR_NOT_FOUND);
	error_code = journal.next_request_id(
		WorldBlockMiningTransaction::journal_scope(), &request_id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (WorldBlockMiningTransaction::execute(snapshot_path, *world, catalog,
		world_items, journal, request_id, owner_id, server_tick, bx, by, bz,
		block_id, block_id == VOXEL_GENERATOR_PAPER_REED_BLOCK_ID
			? VOXEL_GENERATOR_PAPER_REED_IMMATURE_BLOCK_ID
			: GAME_VOXEL_AIR_BLOCK, target_chunk->voxel_revision, drops,
		drop_count, snapshot_writer, inventory,
		tool_slot == -2 || (inventory != nullptr && character == nullptr
			&& required_class != MINECRAFT_MINING_TOOL_NONE)
			? tool_slot : -1,
		tool_item_id, tool_instance_id, inventory_revision, character));
}

int32_t BlockInteractor::try_pick_target_block(const World *world,
	const Camera &camera, uint32_t *selected_block_id)
{
	uint32_t	hit_id;

	if (!world || !selected_block_id)
		return (FT_ERR_INVALID_ARGUMENT);
	double dx, dy, dz;
	get_camera_direction(camera, dx, dy, dz);
	int32_t hx, hy, hz, px, py, pz;
	if (world->raycast_edit_target(camera.x, camera.y, camera.z, dx, dy, dz,
			10.0, &hx, &hy, &hz, &px, &py, &pz, &hit_id) != FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	if (hit_id != GAME_VOXEL_AIR_BLOCK
		&& voxel_block_is_known(hit_id) == FT_TRUE
		&& voxel_block_is_breakable(hit_id) == FT_TRUE)
		*selected_block_id = hit_id;
	return (FT_ERR_SUCCESS);
}

int32_t BlockInteractor::try_place_selected_block(World *world,
	const Camera &camera, uint32_t selected_block_id)
{
	uint32_t	hit_id;
	int32_t		err;

	if (!world || selected_block_id == GAME_VOXEL_AIR_BLOCK)
		return (FT_ERR_INVALID_ARGUMENT);
	if (voxel_block_is_liquid(selected_block_id) == FT_TRUE)
		return (FT_ERR_PERMISSION_DENIED);
	double dx, dy, dz;
	get_camera_direction(camera, dx, dy, dz);
	int32_t hx, hy, hz, px, py, pz;
	if (world->raycast_edit_target(camera.x, camera.y, camera.z, dx, dy, dz,
			10.0, &hx, &hy, &hz, &px, &py, &pz, &hit_id) != FT_ERR_SUCCESS)
		return (FT_ERR_SUCCESS);
	if (PlayerController::block_overlaps_player(camera, px, py, pz))
		return (FT_ERR_SUCCESS);
	PlaceBlockCommand cmd(px, py, pz, selected_block_id);
	err = cmd.execute(*world);
	if (err == FT_ERR_ALREADY_EXISTS || err == FT_ERR_NOT_FOUND)
		return (FT_ERR_SUCCESS);
	return (err);
}
