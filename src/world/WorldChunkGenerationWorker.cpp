#include "../../src/world/WorldChunkGenerationWorker.hpp"
#include "../../src/world/WorldTerrainChunkStore.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <utility>
#include <vector>

namespace
{
	struct VillageFootprint
	{
		int32_t center_x;
		int32_t center_z;
	};

	int32_t place_village(WorldChunk &chunk,
		const WorldGenerationPipeline::GenerationFeaturePlan &plan,
		std::vector<VillageFootprint> &placed_villages) noexcept
	{
		const uint64_t hash = plan.host_intent_sequence
			^ (static_cast<uint64_t>(plan.ordinal) * 0x9e3779b97f4a7c15ULL);
		for (int32_t attempt = 0; attempt <= 144; ++attempt)
		{
			const bool fallback = attempt == 144;
			const int32_t center_x = fallback ? 8 : 5 + static_cast<int32_t>(
				(hash + static_cast<uint64_t>(attempt) * 11U) % 6U);
			const int32_t center_z = fallback ? 8 : 5 + static_cast<int32_t>(
				((hash >> 16U) + static_cast<uint64_t>(attempt) * 7U) % 6U);
			bool overlaps = false;
			for (const VillageFootprint &placed : placed_villages)
			{
				if (center_x - 4 <= placed.center_x + 4
					&& center_x + 4 >= placed.center_x - 4
					&& center_z - 4 <= placed.center_z + 4
					&& center_z + 4 >= placed.center_z - 4)
				{
					overlaps = true;
					break ;
				}
			}
			if (overlaps)
				continue ;
			int32_t ground_y = -1;
			for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 7; y >= 3; --y)
			{
				uint32_t block_id;
				if (chunk.chunk.read_block(center_x, y, center_z, &block_id)
					!= FT_ERR_SUCCESS)
					return (FT_ERR_INVALID_STATE);
				if (voxel_block_is_solid(block_id) != FT_FALSE)
				{
					ground_y = y;
					break ;
				}
			}
			if (ground_y < 3)
			{
				if (!fallback)
					continue ;
				/* The chunk bottom remains intact; make a supported pad one
				 * block above it when this generated chunk has no natural floor. */
				ground_y = 1;
			}
			bool valid_site = true;
			int32_t surface_heights[9][9] = {};
			for (int32_t dz = -4; dz <= 4 && valid_site; ++dz)
			{
				for (int32_t dx = -4; dx <= 4; ++dx)
				{
					int32_t surface_y = fallback ? ground_y : -1;
					for (int32_t y = ground_y + 2;
						!fallback && y >= ground_y - 2; --y)
					{
						uint32_t block_id;
						if (chunk.chunk.read_block(center_x + dx, y,
							center_z + dz, &block_id) != FT_ERR_SUCCESS)
							return (FT_ERR_INVALID_STATE);
						if (voxel_block_is_solid(block_id) != FT_FALSE)
						{
							surface_y = y;
							break ;
						}
					}
					if (!fallback && (surface_y < ground_y - 2
						|| surface_y > ground_y + 2))
					{
						valid_site = false;
						break ;
					}
					surface_heights[dx + 4][dz + 4] = surface_y;
				}
			}
			if (!valid_site)
				continue ;
			try
			{
				placed_villages.push_back({center_x, center_z});
			}
			catch (...)
			{
				return (FT_ERR_NO_MEMORY);
			}
			for (int32_t dz = -4; dz <= 4; ++dz)
			{
				for (int32_t dx = -4; dx <= 4; ++dx)
				{
					const int32_t surface_y = surface_heights[dx + 4][dz + 4];
					for (int32_t y = surface_y + 1; y <= ground_y + 4; ++y)
					{
						const int32_t error_code = chunk.chunk.write_generated_block(
							center_x + dx, y, center_z + dz,
							GAME_VOXEL_AIR_BLOCK);
						if (error_code != FT_ERR_SUCCESS)
							return (error_code);
					}
					if (fallback)
					{
						const int32_t error_code = chunk.chunk.write_generated_block(
							center_x + dx, ground_y, center_z + dz,
							VOXEL_GENERATOR_DIRT_BLOCK);
						if (error_code != FT_ERR_SUCCESS)
							return (error_code);
					}
					if (surface_y < ground_y)
					{
						for (int32_t y = surface_y + 1; y <= ground_y; ++y)
						{
							const int32_t error_code = chunk.chunk.write_generated_block(
								center_x + dx, y, center_z + dz,
								VOXEL_GENERATOR_DIRT_BLOCK);
							if (error_code != FT_ERR_SUCCESS)
								return (error_code);
						}
					}
					else if (surface_y > ground_y)
					{
						for (int32_t y = ground_y + 1; y <= surface_y; ++y)
						{
							const int32_t error_code = chunk.chunk.write_generated_block(
								center_x + dx, y, center_z + dz,
								GAME_VOXEL_AIR_BLOCK);
							if (error_code != FT_ERR_SUCCESS)
								return (error_code);
						}
					}
					if ((dx == 0 || dz == 0)
						&& !(dx == 0 && dz == 0))
					{
						const int32_t error_code = chunk.chunk.write_generated_block(
							center_x + dx, ground_y + 1, center_z + dz,
							VOXEL_GENERATOR_COARSE_DIRT_BLOCK);
						if (error_code != FT_ERR_SUCCESS)
							return (error_code);
					}
				}
			}
			for (int32_t house = 0; house < 2; ++house)
			{
				const int32_t house_x = center_x + (house == 0 ? -2 : 2);
				for (int32_t dz = -1; dz <= 1; ++dz)
				{
					for (int32_t dx = -1; dx <= 1; ++dx)
					{
						int32_t error_code = chunk.chunk.write_generated_block(
							house_x + dx, ground_y + 1, center_z + dz,
							VOXEL_GENERATOR_STONE_BLOCK);
						if (error_code != FT_ERR_SUCCESS)
							return (error_code);
						if (dx == -1 || dx == 1 || dz == -1 || dz == 1)
						{
							for (int32_t wall_y = ground_y + 2;
								wall_y <= ground_y + 3; ++wall_y)
							{
								const bool doorway = dz == 0
									&& dx == (house == 0 ? 1 : -1);
								if (doorway)
									continue ;
								error_code = chunk.chunk.write_generated_block(
									house_x + dx, wall_y, center_z + dz,
									VOXEL_GENERATOR_OAK_LOG_BLOCK);
								if (error_code != FT_ERR_SUCCESS)
									return (error_code);
							}
						}
					}
				}
				for (int32_t dz = -1; dz <= 1; ++dz)
				{
					for (int32_t dx = -1; dx <= 1; ++dx)
					{
						const int32_t error_code = chunk.chunk.write_generated_block(
							house_x + dx, ground_y + 4, center_z + dz,
							VOXEL_GENERATOR_OAK_LEAVES_BLOCK);
						if (error_code != FT_ERR_SUCCESS)
							return (error_code);
					}
				}
			}
			return (FT_ERR_SUCCESS);
		}
		return (FT_ERR_NOT_FOUND);
	}

	int32_t place_ruined_village(WorldChunk &chunk) noexcept
	{
		/* A compact, deterministic ruin centered in the reserved chunk. The
		 * support pad makes the hostile spawner positions stable across terrain
		 * profiles and keeps every block edit inside this chunk. */
		for (int32_t z = 3; z <= 12; ++z)
		{
			for (int32_t x = 3; x <= 12; ++x)
			{
				const int32_t error_code = chunk.chunk.write_generated_block(
					x, 3, z, VOXEL_GENERATOR_COARSE_DIRT_BLOCK);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
			}
		}
		for (int32_t house = 0; house < 2; ++house)
		{
			const int32_t center_x = house == 0 ? 6 : 10;
			for (int32_t z = 6; z <= 10; ++z)
			{
				for (int32_t x = center_x - 1; x <= center_x + 1; ++x)
				{
					int32_t error_code = chunk.chunk.write_generated_block(x, 4,
						z, VOXEL_GENERATOR_STONE_BLOCK);
					if (error_code != FT_ERR_SUCCESS)
						return (error_code);
					if ((x == center_x - 1 || x == center_x + 1
							|| z == 6 || z == 10)
						&& ((x + z + house) % 3 != 0))
					{
						error_code = chunk.chunk.write_generated_block(x, 5, z,
							VOXEL_GENERATOR_OAK_LOG_BLOCK);
						if (error_code != FT_ERR_SUCCESS)
							return (error_code);
					}
				}
			}
			for (int32_t x = center_x - 1; x <= center_x + 1; ++x)
			{
				const int32_t error_code = chunk.chunk.write_generated_block(
					x, 7, 8, VOXEL_GENERATOR_OAK_LEAVES_BLOCK);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
			}
		}
		for (int32_t x = 8; x <= 9; ++x)
		{
			const int32_t error_code = chunk.chunk.write_generated_block(x, 4,
				8, VOXEL_GENERATOR_GOLD_ORE_BLOCK);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
		}
		return (FT_ERR_SUCCESS);
	}

	int32_t place_shipwreck(WorldChunk &chunk) noexcept
	{
		int32_t waterline = -1;
		for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1; y >= 1; --y)
		{
			uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
			if (chunk.chunk.read_block(8, y, 8, &block_id) != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_STATE);
			if (block_id == VOXEL_GENERATOR_WATER_BLOCK)
			{
				waterline = y;
				break ;
			}
		}
		if (waterline < 16)
			return (FT_ERR_NOT_FOUND);
		const int32_t keel_y = waterline - 9;
		for (int32_t z = 3; z <= 13; ++z)
		{
			const int32_t keel_error = chunk.chunk.write_generated_block(8,
				keel_y, z, VOXEL_GENERATOR_OAK_LOG_BLOCK);
			if (keel_error != FT_ERR_SUCCESS)
				return (keel_error);
			for (int32_t side_x : {4, 5, 11, 12})
			{
				for (int32_t y = keel_y + 1; y <= keel_y + 3; ++y)
				{
					const int32_t error_code = chunk.chunk.write_generated_block(
						side_x, y, z, VOXEL_GENERATOR_OAK_LOG_BLOCK);
					if (error_code != FT_ERR_SUCCESS)
						return (error_code);
				}
			}
		}
		for (int32_t x = 5; x <= 11; ++x)
		{
			for (int32_t z : {3, 13})
			{
				if (x == 8 && z == 3)
					continue ;
				const int32_t error_code = chunk.chunk.write_generated_block(x,
					keel_y + 2, z, VOXEL_GENERATOR_OAK_LOG_BLOCK);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
			}
		}
		for (int32_t z = 4; z <= 12; ++z)
		{
			for (int32_t x = 5; x <= 11; ++x)
			{
				const bool deck_edge = x == 5 || x == 11 || z == 4 || z == 12;
				if (deck_edge)
				{
					const int32_t error_code = chunk.chunk.write_generated_block(
						x, keel_y + 4, z, VOXEL_GENERATOR_OAK_LOG_BLOCK);
					if (error_code != FT_ERR_SUCCESS)
						return (error_code);
				}
				for (int32_t y = keel_y + 1; y <= keel_y + 4; ++y)
				{
					const int32_t error_code = chunk.chunk.write_generated_block(
						x, y, z, GAME_VOXEL_AIR_BLOCK);
					if (error_code != FT_ERR_SUCCESS)
						return (error_code);
				}
			}
		}
		/* An open forward hatch connects the surrounding water to a clear,
		 * seven-by-nine lower hull so a player can swim into and through it. */
		for (int32_t y = keel_y + 1; y <= keel_y + 3; ++y)
		{
			const int32_t error_code = chunk.chunk.write_generated_block(8, y, 3,
				GAME_VOXEL_AIR_BLOCK);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
		}
		for (int32_t z = 9; z <= 12; ++z)
		{
			for (int32_t x = 6; x <= 10; ++x)
			{
				for (int32_t y = keel_y + 5; y <= keel_y + 6; ++y)
				{
					const bool wall = x == 6 || x == 10 || z == 9 || z == 12;
					const bool doorway = x == 8 && z == 9;
					if (!wall || doorway)
						continue ;
					const int32_t error_code = chunk.chunk.write_generated_block(
						x, y, z, VOXEL_GENERATOR_OAK_LOG_BLOCK);
					if (error_code != FT_ERR_SUCCESS)
						return (error_code);
				}
			}
		}
		for (int32_t x = 6; x <= 10; ++x)
		{
			const int32_t error_code = chunk.chunk.write_generated_block(x,
				keel_y + 7, 10, VOXEL_GENERATOR_OAK_LEAVES_BLOCK);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
		}
		for (int32_t y = keel_y + 4; y <= keel_y + 8; ++y)
		{
			if ((y - keel_y) % 2 == 0)
			{
				const int32_t error_code = chunk.chunk.write_generated_block(8, y,
					7, VOXEL_GENERATOR_OAK_LOG_BLOCK);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
			}
		}
		return (FT_ERR_SUCCESS);
	}

	int32_t place_desert_pyramid(WorldChunk &chunk) noexcept
	{
		int32_t ground_y = -1;
		for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1; y >= 1; --y)
		{
			uint32_t block = GAME_VOXEL_AIR_BLOCK;
			if (chunk.chunk.read_block(8, y, 8, &block) != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_STATE);
			if (voxel_block_is_solid(block) != FT_FALSE)
			{
				ground_y = y;
				break ;
			}
		}
		if (ground_y < 1 || ground_y + 7 >= GAME_VOXEL_CHUNK_HEIGHT)
			return (FT_ERR_NOT_FOUND);
		/* A hollow stepped pyramid with a serviceable central chamber. */
		for (int32_t layer = 0; layer < 5; ++layer)
		{
			const int32_t radius = 6 - layer;
			const int32_t y = ground_y + 1 + layer;
			for (int32_t z = 8 - radius; z <= 8 + radius; ++z)
				for (int32_t x = 8 - radius; x <= 8 + radius; ++x)
				{
					if (layer > 0 && x > 8 - radius + 1
						&& x < 8 + radius - 1 && z > 8 - radius + 1
						&& z < 8 + radius - 1)
						continue ;
					const int32_t error = chunk.chunk.write_generated_block(x, y,
						z, VOXEL_GENERATOR_SANDSTONE_BLOCK);
					if (error != FT_ERR_SUCCESS)
						return (error);
				}
		}
		for (int32_t y = ground_y + 1; y <= ground_y + 3; ++y)
			for (int32_t z = 6; z <= 10; ++z)
				for (int32_t x = 6; x <= 10; ++x)
				{
					const int32_t error = chunk.chunk.write_generated_block(x, y,
						z, GAME_VOXEL_AIR_BLOCK);
					if (error != FT_ERR_SUCCESS)
						return (error);
				}
		for (int32_t y = ground_y + 2; y <= ground_y + 3; ++y)
		{
			const int32_t error = chunk.chunk.write_generated_block(8, y, 2,
				GAME_VOXEL_AIR_BLOCK);
			if (error != FT_ERR_SUCCESS)
				return (error);
		}
		return (chunk.chunk.write_generated_block(8, ground_y + 1, 8,
			VOXEL_GENERATOR_GOLD_ORE_BLOCK));
	}

	int32_t place_ancient_canopy(WorldChunk &chunk) noexcept
	{
		int32_t ground_y = -1;
		for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1; y >= 1; --y)
		{
			uint32_t block = GAME_VOXEL_AIR_BLOCK;
			if (chunk.chunk.read_block(8, y, 8, &block) != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_STATE);
			if (voxel_block_is_solid(block) != FT_FALSE)
			{
				ground_y = y;
				break ;
			}
		}
		if (ground_y < 1 || ground_y + 16 >= GAME_VOXEL_CHUNK_HEIGHT)
			return (FT_ERR_NOT_FOUND);
		for (int32_t y = ground_y + 1; y <= ground_y + 13; ++y)
		{
			const int32_t error = chunk.chunk.write_generated_block(8, y, 8,
				VOXEL_GENERATOR_OAK_LOG_BLOCK);
			if (error != FT_ERR_SUCCESS)
				return (error);
		}
		for (int32_t y = ground_y + 8; y <= ground_y + 11; ++y)
			for (int32_t z = 5; z <= 11; ++z)
				for (int32_t x = 5; x <= 11; ++x)
				{
					const bool edge = x == 5 || x == 11 || z == 5 || z == 11;
					if (!edge && !(x == 8 && z == 8))
						continue ;
					const int32_t error = chunk.chunk.write_generated_block(x, y,
						z, edge ? VOXEL_GENERATOR_OAK_LOG_BLOCK
							: VOXEL_GENERATOR_OAK_LEAVES_BLOCK);
					if (error != FT_ERR_SUCCESS)
						return (error);
				}
		for (int32_t z = 3; z <= 13; ++z)
			for (int32_t x = 3; x <= 13; ++x)
			{
				const int32_t dx = x - 8;
				const int32_t dz = z - 8;
				if (dx * dx + dz * dz > 25)
					continue ;
				const int32_t error = chunk.chunk.write_generated_block(x,
					ground_y + 12, z,
					VOXEL_GENERATOR_OAK_LEAVES_BLOCK);
				if (error != FT_ERR_SUCCESS)
					return (error);
			}
		for (int32_t y = ground_y + 8; y <= ground_y + 11; ++y)
			for (int32_t z = 7; z <= 9; ++z)
				for (int32_t x = 7; x <= 9; ++x)
				{
					const int32_t error = chunk.chunk.write_generated_block(x, y,
						z, GAME_VOXEL_AIR_BLOCK);
					if (error != FT_ERR_SUCCESS)
						return (error);
				}
		/* A solid canopy roof closes the hostile room to skylight. The leaf
		 * crown sits above it, while this opaque layer keeps its chamber dark. */
		for (int32_t z = 5; z <= 11; ++z)
			for (int32_t x = 5; x <= 11; ++x)
			{
				const uint32_t roof_block = x == 8 && z == 7
					? VOXEL_GENERATOR_TRAPDOOR_CLOSED_BLOCK
					: VOXEL_GENERATOR_OAK_LOG_BLOCK;
				const int32_t error = chunk.chunk.write_generated_block(x,
					ground_y + 12, z, roof_block);
				if (error != FT_ERR_SUCCESS)
					return (error);
			}
		/* A continuous ladder gives the hollow trunk a real climb route. */
		for (int32_t y = ground_y + 8; y <= ground_y + 11; ++y)
		{
			const int32_t error = chunk.chunk.write_generated_block(8, y,
				7, VOXEL_GENERATOR_LADDER_BLOCK);
			if (error != FT_ERR_SUCCESS)
				return (error);
		}
		return (FT_ERR_SUCCESS);
	}

	int32_t place_spider_nest(WorldChunk &chunk) noexcept
	{
		/* Require an actual below-surface air cavity before adding the stone
		 * chamber; a dry surface chunk is not silently turned into a cave. */
		int32_t cave_y = -1;
		for (int32_t y = 48; y >= 8 && cave_y < 0; --y)
		{
			uint32_t block = GAME_VOXEL_AIR_BLOCK;
			if (chunk.chunk.read_block(8, y, 8, &block) != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_STATE);
			if (block == GAME_VOXEL_AIR_BLOCK)
			{
				uint32_t floor = GAME_VOXEL_AIR_BLOCK;
				uint32_t side = GAME_VOXEL_AIR_BLOCK;
				if (chunk.chunk.read_block(8, y - 1, 8, &floor)
					!= FT_ERR_SUCCESS
					|| chunk.chunk.read_block(9, y, 8, &side)
					!= FT_ERR_SUCCESS)
					return (FT_ERR_INVALID_STATE);
				if (side == GAME_VOXEL_AIR_BLOCK
					&& floor != GAME_VOXEL_AIR_BLOCK
					&& floor != VOXEL_GENERATOR_WATER_BLOCK)
					cave_y = y;
			}
		}
		if (cave_y < 0)
			return (FT_ERR_NOT_FOUND);
		for (int32_t z = 5; z <= 11; ++z)
			for (int32_t x = 5; x <= 11; ++x)
			{
				const bool wall = x == 5 || x == 11 || z == 5 || z == 11;
				if (!wall)
					continue ;
				const int32_t error = chunk.chunk.write_generated_block(x,
					cave_y, z, VOXEL_GENERATOR_MOSSY_STONE_BLOCK);
				if (error != FT_ERR_SUCCESS)
					return (error);
			}
		return (chunk.chunk.write_generated_block(8, cave_y, 8,
			VOXEL_GENERATOR_SPIDER_CORPSE_BLOCK));
	}

	int32_t place_instability_overlay(WorldChunk &chunk,
		uint32_t variant, int32_t center_x = 8,
		int32_t center_z = 8) noexcept
	{
		if (variant == 0U || variant > 8U)
			return (FT_ERR_INVALID_ARGUMENT);
		if (variant == 6U)
			return (place_shipwreck(chunk));
		if (variant == 8U)
			return (place_ruined_village(chunk));
		for (int32_t z = center_z - 3; z <= center_z + 3; ++z)
			for (int32_t x = center_x - 3; x <= center_x + 3; ++x)
			{
				const int32_t error = chunk.chunk.write_generated_block(x, 2, z,
					variant == 1U ? VOXEL_GENERATOR_COARSE_DIRT_BLOCK
						: VOXEL_GENERATOR_CRACKED_STONE_BLOCK);
				if (error != FT_ERR_SUCCESS)
					return (error);
			}
		if (variant == 1U)
		{
			for (int32_t x = center_x - 3; x <= center_x + 3; x += 2)
				for (int32_t z : {center_z - 3, center_z + 3})
				{
					const int32_t error = chunk.chunk.write_generated_block(x, 3,
						z, VOXEL_GENERATOR_OAK_LOG_BLOCK);
					if (error != FT_ERR_SUCCESS)
						return (error);
				}
			return (FT_ERR_SUCCESS);
		}
		if (variant == 2U)
		{
			for (int32_t x : {center_x - 2, center_x + 2})
				for (int32_t y = 3; y <= 7; ++y)
				{
					const int32_t error = chunk.chunk.write_generated_block(x, y,
						center_z, VOXEL_GENERATOR_CRACKED_STONE_BLOCK);
					if (error != FT_ERR_SUCCESS)
						return (error);
				}
			return (chunk.chunk.write_generated_block(center_x, 3, center_z,
				VOXEL_GENERATOR_GOLD_ORE_BLOCK));
		}
		if (variant == 3U)
		{
			for (int32_t y = 3; y <= 13; ++y)
				for (int32_t x : {center_x - 3, center_x + 3})
					for (int32_t z : {center_z - 3, center_z + 3})
					{
						const int32_t error = chunk.chunk.write_generated_block(x, y,
							z, VOXEL_GENERATOR_MOSSY_STONE_BLOCK);
						if (error != FT_ERR_SUCCESS)
							return (error);
					}
			for (int32_t z = center_z - 2; z <= center_z + 2; ++z)
				for (int32_t x = center_x - 2; x <= center_x + 2; ++x)
				{
					const int32_t error = chunk.chunk.write_generated_block(x,
						8, z, (x == center_x - 2 || x == center_x + 2
							|| z == center_z - 2 || z == center_z + 2)
							? VOXEL_GENERATOR_OAK_LOG_BLOCK : GAME_VOXEL_AIR_BLOCK);
					if (error != FT_ERR_SUCCESS)
						return (error);
				}
			return (FT_ERR_SUCCESS);
		}
		for (int32_t y = 3; y <= 6; ++y)
		for (int32_t z = center_z - 2; z <= center_z + 2; ++z)
			for (int32_t x = center_x - 2; x <= center_x + 2; ++x)
			{
				const bool wall = x == center_x - 2 || x == center_x + 2
					|| z == center_z - 2 || z == center_z + 2;
				if (!wall)
					continue ;
				const int32_t error = chunk.chunk.write_generated_block(x, y, z,
					VOXEL_GENERATOR_MOSSY_STONE_BLOCK);
				if (error != FT_ERR_SUCCESS)
					return (error);
			}
		if (variant == 5U)
		{
			for (int32_t y = 3; y <= 8; ++y)
				for (int32_t x : {center_x - 3, center_x + 3})
					for (int32_t z : {center_z - 3, center_z + 3})
					{
						const int32_t error = chunk.chunk.write_generated_block(
							x, y, z, VOXEL_GENERATOR_OBSIDIAN_BLOCK);
						if (error != FT_ERR_SUCCESS)
							return (error);
					}
			return (chunk.chunk.write_generated_block(center_x, 3, center_z,
				VOXEL_GENERATOR_DIAMOND_ORE_BLOCK));
		}
		if (variant == 7U)
		{
			int32_t ground_y = -1;
			for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1; y >= 1; --y)
			{
				uint32_t block = GAME_VOXEL_AIR_BLOCK;
				if (chunk.chunk.read_block(8, y, 8, &block) != FT_ERR_SUCCESS)
					return (FT_ERR_INVALID_STATE);
				if (voxel_block_is_solid(block) != FT_FALSE)
				{
					ground_y = y;
					break ;
				}
			}
			if (ground_y < 1 || ground_y + 5 >= GAME_VOXEL_CHUNK_HEIGHT)
				return (FT_ERR_NOT_FOUND);
			for (int32_t z = 6; z <= 10; ++z)
				for (int32_t x = 6; x <= 10; ++x)
				{
					const bool rim = x == 6 || x == 10 || z == 6 || z == 10;
					const int32_t error = chunk.chunk.write_generated_block(x,
						ground_y + 1, z, rim ? VOXEL_GENERATOR_BASALT_BLOCK
							: GAME_VOXEL_AIR_BLOCK);
					if (error != FT_ERR_SUCCESS)
						return (error);
				}
			for (int32_t y = ground_y + 2; y <= ground_y + 4; ++y)
				for (int32_t x : {6, 10})
					for (int32_t z : {6, 10})
					{
						const int32_t error = chunk.chunk.write_generated_block(
							x, y, z, VOXEL_GENERATOR_BASALT_BLOCK);
						if (error != FT_ERR_SUCCESS)
							return (error);
					}
			return (chunk.chunk.write_generated_block(8, ground_y + 2, 8,
				VOXEL_GENERATOR_OBSIDIAN_BLOCK));
		}
		return (FT_ERR_SUCCESS);
	}

	int32_t place_instability_opportunity(WorldChunk &chunk,
		uint32_t variant, int32_t center_x, int32_t center_z) noexcept
	{
		int32_t ground_y = -1;
		for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 8; y >= 2; --y)
		{
			uint32_t block = GAME_VOXEL_AIR_BLOCK;
			if (chunk.chunk.read_block(center_x, y, center_z, &block)
				!= FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_STATE);
			if (voxel_block_is_solid(block) != FT_FALSE)
			{
				ground_y = y;
				break ;
			}
			if (block != GAME_VOXEL_AIR_BLOCK)
				return (FT_ERR_NOT_FOUND);
		}
	if (ground_y < 2 || ground_y + 7 >= GAME_VOXEL_CHUNK_HEIGHT
		|| variant == 0U || variant > 5U)
		return (FT_ERR_NOT_FOUND);
	for (int32_t z = center_z - 2; z <= center_z + 2; ++z)
		for (int32_t x = center_x - 2; x <= center_x + 2; ++x)
		{
			const uint32_t floor_block = variant == 1U
				? VOXEL_GENERATOR_COARSE_DIRT_BLOCK
				: VOXEL_GENERATOR_CRACKED_STONE_BLOCK;
			const int32_t error = chunk.chunk.write_generated_block(x,
				ground_y + 1, z, floor_block);
			if (error != FT_ERR_SUCCESS)
				return (error);
		}
	if (variant == 1U)
	{
		for (int32_t x : {center_x - 2, center_x + 2})
			for (int32_t z : {center_z - 2, center_z + 2})
				for (int32_t y = ground_y + 2; y <= ground_y + 3; ++y)
				{
					const int32_t error = chunk.chunk.write_generated_block(x, y,
						z, VOXEL_GENERATOR_OAK_LOG_BLOCK);
					if (error != FT_ERR_SUCCESS)
						return (error);
				}
		return (FT_ERR_SUCCESS);
	}
	if (variant == 2U)
	{
		for (int32_t x : {center_x - 2, center_x + 2})
			for (int32_t y = ground_y + 2; y <= ground_y + 5; ++y)
			{
				const int32_t error = chunk.chunk.write_generated_block(x, y,
					center_z, VOXEL_GENERATOR_CRACKED_STONE_BLOCK);
				if (error != FT_ERR_SUCCESS)
					return (error);
			}
		return (chunk.chunk.write_generated_block(center_x, ground_y + 2,
			center_z, VOXEL_GENERATOR_GOLD_ORE_BLOCK));
	}
	if (variant == 3U)
	{
		for (int32_t y = ground_y + 2; y <= ground_y + 6; ++y)
			for (int32_t x : {center_x - 2, center_x + 2})
				for (int32_t z : {center_z - 2, center_z + 2})
				{
					const int32_t error = chunk.chunk.write_generated_block(x, y,
						z, VOXEL_GENERATOR_MOSSY_STONE_BLOCK);
					if (error != FT_ERR_SUCCESS)
						return (error);
				}
		for (int32_t x = center_x - 1; x <= center_x + 1; ++x)
			for (int32_t z = center_z - 1; z <= center_z + 1; ++z)
			{
				const int32_t error = chunk.chunk.write_generated_block(x,
					ground_y + 4, z,
					(x == center_x - 1 || x == center_x + 1
						|| z == center_z - 1 || z == center_z + 1)
						? VOXEL_GENERATOR_OAK_LOG_BLOCK : GAME_VOXEL_AIR_BLOCK);
				if (error != FT_ERR_SUCCESS)
					return (error);
			}
		return (FT_ERR_SUCCESS);
	}
	const uint32_t wall_block = variant == 5U
		? VOXEL_GENERATOR_OBSIDIAN_BLOCK
		: VOXEL_GENERATOR_MOSSY_STONE_BLOCK;
	for (int32_t y = ground_y + 2; y <= ground_y + 3; ++y)
		for (int32_t offset = -2; offset <= 2; ++offset)
		{
			if (!(variant >= 4U && offset == 0))
			{
				int32_t error = chunk.chunk.write_generated_block(
					center_x + offset, y, center_z - 2, wall_block);
				if (error != FT_ERR_SUCCESS)
					return (error);
				error = chunk.chunk.write_generated_block(center_x + offset, y,
					center_z + 2, wall_block);
				if (error != FT_ERR_SUCCESS)
					return (error);
			}
			const int32_t left_error = chunk.chunk.write_generated_block(
				center_x - 2, y, center_z + offset, wall_block);
			const int32_t right_error = chunk.chunk.write_generated_block(
				center_x + 2, y, center_z + offset, wall_block);
			if (left_error != FT_ERR_SUCCESS || right_error != FT_ERR_SUCCESS)
				return (left_error != FT_ERR_SUCCESS ? left_error : right_error);
		}
	return (FT_ERR_SUCCESS);
	}

	int32_t place_volcano(WorldChunk &chunk,
		const WorldGenerationPipeline::GenerationFeaturePlan &plan) noexcept
	{
		static const int32_t BASE_Y = 4;
		static const int32_t CRATER_FLOOR_Y = 18;
		static const int32_t ARENA_FLOOR_Y = 20;
		static const int32_t PROFILE_CENTER_BLOCK = 72;
		if (plan.volcano_footprint_chunks < 9U
			|| plan.volcano_footprint_chunks > 16U)
			return (FT_ERR_INVALID_ARGUMENT);
		const int64_t relative_chunk_x = static_cast<int64_t>(chunk.chunk_x)
			- plan.volcano_anchor_chunk_x;
		const int64_t relative_chunk_z = static_cast<int64_t>(chunk.chunk_z)
			- plan.volcano_anchor_chunk_z;
		if (relative_chunk_x < 0
			|| relative_chunk_x >= plan.volcano_footprint_chunks
			|| relative_chunk_z < 0
			|| relative_chunk_z >= plan.volcano_footprint_chunks)
			return (FT_ERR_OUT_OF_RANGE);
		for (int32_t local_z = 0; local_z < GAME_VOXEL_CHUNK_DEPTH; ++local_z)
		{
			for (int32_t local_x = 0; local_x < GAME_VOXEL_CHUNK_WIDTH; ++local_x)
			{
				const int64_t raw_x = relative_chunk_x
					* GAME_VOXEL_CHUNK_WIDTH + local_x;
				const int64_t raw_z = relative_chunk_z
					* GAME_VOXEL_CHUNK_DEPTH + local_z;
				const int32_t global_x = static_cast<int32_t>(
					raw_x * 9 / plan.volcano_footprint_chunks);
				const int32_t global_z = static_cast<int32_t>(
					raw_z * 9 / plan.volcano_footprint_chunks);
				const int32_t dx = global_x - PROFILE_CENTER_BLOCK;
				const int32_t dz = global_z - PROFILE_CENTER_BLOCK;
				const int32_t distance_squared = dx * dx + dz * dz;
				int32_t radius = 0;
				while ((radius + 1) * (radius + 1) <= distance_squared)
					++radius;
				int32_t surface_y = BASE_Y;
				if (radius <= 10)
				surface_y = CRATER_FLOOR_Y;
				else if (radius <= 16)
					surface_y = CRATER_FLOOR_Y + (radius - 10) * 2;
				else if (radius <= 60)
					surface_y = 30 - ((radius - 16) * 25) / 44;
				for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
				{
					/* Preserve the world's bottom support/bedrock layer. */
					if (y == 0)
						continue ;
					uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
					if (y <= BASE_Y || (y > BASE_Y && y <= surface_y))
						block_id = VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK;
					if (radius <= 7 && y == CRATER_FLOOR_Y)
						block_id = VOXEL_GENERATOR_LAVA_BLOCK;
					const int32_t error_code = chunk.chunk.write_generated_block(
						local_x, y, local_z, block_id);
					if (error_code != FT_ERR_SUCCESS)
						return (error_code);
				}
				/* A supported arena shelf and a two-block-wide passage connect it
				 * to the inner crater. These global-coordinate tests match on both
				 * sides of every chunk seam. */
				const bool arena = global_x >= PROFILE_CENTER_BLOCK + 20
					&& global_x <= PROFILE_CENTER_BLOCK + 34
					&& global_z >= PROFILE_CENTER_BLOCK - 8
					&& global_z <= PROFILE_CENTER_BLOCK + 8;
				const bool passage = global_x >= PROFILE_CENTER_BLOCK + 8
					&& global_x <= PROFILE_CENTER_BLOCK + 20
					&& global_z >= PROFILE_CENTER_BLOCK - 2
					&& global_z <= PROFILE_CENTER_BLOCK + 2;
				if (arena)
				{
					for (int32_t y = BASE_Y + 1; y <= ARENA_FLOOR_Y; ++y)
					{
						const int32_t error_code = chunk.chunk.write_generated_block(
							local_x, y, local_z,
							VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK);
						if (error_code != FT_ERR_SUCCESS)
							return (error_code);
					}
					for (int32_t y = ARENA_FLOOR_Y + 1; y <= 26; ++y)
					{
						const int32_t error_code = chunk.chunk.write_generated_block(
							local_x, y, local_z, GAME_VOXEL_AIR_BLOCK);
						if (error_code != FT_ERR_SUCCESS)
							return (error_code);
					}
				}
				if (passage)
				{
					for (int32_t y = CRATER_FLOOR_Y + 1; y <= 22; ++y)
					{
						const int32_t error_code = chunk.chunk.write_generated_block(
							local_x, y, local_z, GAME_VOXEL_AIR_BLOCK);
						if (error_code != FT_ERR_SUCCESS)
							return (error_code);
					}
				}
			}
		}
		return (FT_ERR_SUCCESS);
	}
}

int32_t WorldChunkGenerationWorker::apply_feature_plans(WorldChunk &chunk,
	const std::vector<WorldGenerationPipeline::GenerationFeaturePlan>
		&feature_plans) noexcept
{
	std::vector<VillageFootprint> placed_villages;
	try
	{
		placed_villages.reserve(feature_plans.size());
	}
	catch (...)
	{
		return (FT_ERR_NO_MEMORY);
	}
	for (const auto &plan : feature_plans)
	{
		if (plan.kind
			== WorldGenerationPipeline::GenerationFeaturePlan::VOLCANO)
		{
			const int32_t volcano_error = place_volcano(chunk, plan);
			if (volcano_error != FT_ERR_SUCCESS)
				return (volcano_error);
			continue ;
		}
		if (plan.kind == WorldGenerationPipeline::GenerationFeaturePlan::VILLAGE)
		{
			const int32_t village_error = place_village(chunk, plan,
				placed_villages);
			if (village_error != FT_ERR_SUCCESS)
				return (village_error);
			continue ;
		}
		if (plan.kind
			== WorldGenerationPipeline::GenerationFeaturePlan::RUINED_VILLAGE)
		{
			const int32_t ruined_error = place_ruined_village(chunk);
			if (ruined_error != FT_ERR_SUCCESS)
				return (ruined_error);
			continue ;
		}
		if (plan.kind
			== WorldGenerationPipeline::GenerationFeaturePlan::SHIPWRECK)
		{
			const int32_t shipwreck_error = place_shipwreck(chunk);
			if (shipwreck_error != FT_ERR_SUCCESS)
				return (shipwreck_error);
			continue ;
		}
		if (plan.kind
			== WorldGenerationPipeline::GenerationFeaturePlan::DESERT_PYRAMID)
		{
			const int32_t error = place_desert_pyramid(chunk);
			if (error != FT_ERR_SUCCESS)
				return (error);
			continue ;
		}
		if (plan.kind
			== WorldGenerationPipeline::GenerationFeaturePlan::ANCIENT_CANOPY)
		{
			const int32_t error = place_ancient_canopy(chunk);
			if (error != FT_ERR_SUCCESS)
				return (error);
			continue ;
		}
		if (plan.kind
			== WorldGenerationPipeline::GenerationFeaturePlan::SPIDER_NEST)
		{
			const int32_t error = place_spider_nest(chunk);
			if (error != FT_ERR_SUCCESS)
				return (error);
			continue ;
		}
		if (plan.kind
			== WorldGenerationPipeline::GenerationFeaturePlan::INSTABILITY_OVERLAY)
		{
			int32_t center_x = 8;
			int32_t center_z = 8;
			uint32_t variant = plan.ordinal;
			bool opportunity = false;
			if (plan.ordinal >= 111U && plan.ordinal <= 135U)
			{
				static const int32_t OPPORTUNITY_CENTERS[3][2] = {
					{4, 4}, {12, 4}, {8, 12}};
				const uint32_t tier = plan.ordinal / 10U - 10U;
				variant = plan.ordinal % 10U;
				if (tier == 0U || tier > 3U || variant == 0U || variant > 5U)
					return (FT_ERR_INVALID_ARGUMENT);
				center_x = OPPORTUNITY_CENTERS[tier - 1U][0];
				center_z = OPPORTUNITY_CENTERS[tier - 1U][1];
				opportunity = true;
			}
			const int32_t error = opportunity
				? place_instability_opportunity(chunk, variant, center_x, center_z)
				: place_instability_overlay(chunk, variant, center_x, center_z);
			if (error != FT_ERR_SUCCESS)
				return (error);
			continue ;
		}
		if (plan.kind
			!= WorldGenerationPipeline::GenerationFeaturePlan::SURFACE_MAGMA_POOL)
			return (FT_ERR_INVALID_OPERATION);
		uint64_t hash = plan.host_intent_sequence
			^ (static_cast<uint64_t>(plan.ordinal) * 0x9e3779b97f4a7c15ULL);
		bool placed = false;
		for (int32_t attempt = 0; attempt <= 144 && !placed; ++attempt)
		{
			const bool fallback = attempt == 144;
			const int32_t center_x = fallback ? 8 : 2
				+ static_cast<int32_t>((hash + attempt) % 12U);
			const int32_t center_z = fallback ? 8 : 2 + static_cast<int32_t>(
				((hash >> 16U) + static_cast<uint64_t>(attempt) * 7U) % 12U);
			int32_t ground_y = -1;
			for (int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 2; y >= 1; --y)
			{
				uint32_t block_id;
				if (chunk.chunk.read_block(center_x, y, center_z, &block_id)
					!= FT_ERR_SUCCESS)
					return (FT_ERR_INVALID_STATE);
				if (voxel_block_is_solid(block_id) != FT_FALSE)
				{
					ground_y = y;
					break ;
				}
			}
			if (fallback)
			{
				if (ground_y < 1)
					ground_y = 1;
				if (ground_y + 2 >= GAME_VOXEL_CHUNK_HEIGHT)
					ground_y = GAME_VOXEL_CHUNK_HEIGHT - 3;
			}
			else if (ground_y < 1
				|| ground_y + 2 >= GAME_VOXEL_CHUNK_HEIGHT)
				continue ;
			bool valid_site = true;
			int32_t surface_heights[5][5] = {};
			for (int32_t dz = -2; !fallback && dz <= 2 && valid_site; ++dz)
			{
				for (int32_t dx = -2; !fallback && dx <= 2; ++dx)
				{
					int32_t surface_y = fallback ? ground_y - 1 : -1;
					for (int32_t y = ground_y + 1; y >= ground_y - 1; --y)
					{
						uint32_t floor_id;
						if (chunk.chunk.read_block(center_x + dx, y,
							center_z + dz, &floor_id) != FT_ERR_SUCCESS)
							return (FT_ERR_INVALID_STATE);
						if (voxel_block_is_solid(floor_id) != FT_FALSE)
						{
							surface_y = y;
							break ;
						}
					}
					if (surface_y < ground_y - 1 || surface_y > ground_y)
					{
						valid_site = false;
						break ;
					}
					surface_heights[dx + 2][dz + 2] = surface_y;
					for (int32_t y = surface_y + 1; y <= ground_y + 1; ++y)
					{
						uint32_t above_id;
						if (chunk.chunk.read_block(center_x + dx, y,
							center_z + dz, &above_id) != FT_ERR_SUCCESS
							|| above_id != GAME_VOXEL_AIR_BLOCK)
						{
							valid_site = false;
							break ;
						}
					}
					if (!valid_site)
						break ;
				}
			}
			if (!valid_site)
				continue ;
			if (fallback)
			{
				for (int32_t dz = -2; dz <= 2; ++dz)
				{
					for (int32_t dx = -2; dx <= 2; ++dx)
						surface_heights[dx + 2][dz + 2] = ground_y - 1;
				}
			}
			for (int32_t dz = -2; dz <= 2; ++dz)
			{
				for (int32_t dx = -2; dx <= 2; ++dx)
				{
					for (int32_t y = surface_heights[dx + 2][dz + 2] + 1;
						y <= ground_y; ++y)
					{
						const int32_t fill_error = chunk.chunk.write_generated_block(
							center_x + dx, y, center_z + dz,
							VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK);
						if (fill_error != FT_ERR_SUCCESS)
							return (fill_error);
					}
					const bool rim = dx == -2 || dx == 2 || dz == -2 || dz == 2;
					const uint32_t block_id = rim
						? VOXEL_GENERATOR_VOLCANIC_ROCK_BLOCK
						: VOXEL_GENERATOR_LAVA_BLOCK;
					const int32_t error_code = chunk.chunk.write_generated_block(
						center_x + dx, ground_y + 1, center_z + dz, block_id);
					if (error_code != FT_ERR_SUCCESS)
						return (error_code);
				}
			}
		placed = true;
		}
		if (!placed)
			return (FT_ERR_NOT_FOUND);
	}
	return (FT_ERR_SUCCESS);
}

namespace
{
	bool remesh_request_is_cancelled(
		const WorldGenerationPipeline::Request &request) noexcept
	{
		if (request.cancellation_token == nullptr)
			return (false);
		return (request.cancellation_token->load(std::memory_order_acquire)
			!= request.cancellation_token_value);
	}

	bool incremental_request_is_cancelled(
		const std::shared_ptr<std::atomic<uint64_t>> &cancellation_token,
		uint64_t cancellation_token_value) noexcept
	{
		if (cancellation_token == nullptr)
			return (false);
		return (cancellation_token->load(std::memory_order_acquire)
			!= cancellation_token_value);
	}

	using incremental_light_node =
		WorldGenerationPipeline::IncrementalLightNode;

	using incremental_removal_node =
		WorldGenerationPipeline::IncrementalRemovalNode;


	using incremental_light_scratch =
		WorldGenerationPipeline::IncrementalLightScratch;

	struct IncrementalLightSlice
	{
		const voxel_light_update_config &config;
		std::chrono::steady_clock::time_point deadline;

		bool permits(uint64_t processed) const noexcept
		{
			if (processed >= config.max_nodes_per_frame)
				return (false);
			if (processed < config.min_nodes_per_frame)
				return (true);
			return (processed < config.target_nodes_per_frame
				&& ((processed != config.min_nodes_per_frame && (processed & 31U) != 0U)
					|| std::chrono::steady_clock::now() < deadline));
		}
	};

	int32_t write_incremental_light(voxel_light_chunk &light,
		incremental_light_scratch &scratch, int32_t x, int32_t y, int32_t z,
		uint8_t value) noexcept
	{
		const int32_t section_height = GAME_VOXEL_CHUNK_HEIGHT
			/ GAME_VOXEL_CHUNK_SECTION_COUNT;
		scratch.modified_sections |= 1U << (y / section_height);
		return (light.set(x, y, z, value));
	}

	void describe_light_boundary_changes(
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		WorldGenerationPipeline::Result &result) noexcept
	{
		if (snapshot.existing_light_valid == FT_FALSE)
			return ;
		result.boundary_read_states = snapshot.boundary_read_states;
		std::array<std::shared_ptr<const WorldChunkReadState>, 4> neighbors;
		for (std::size_t face = 0; face < neighbors.size(); ++face)
			neighbors[face] = snapshot.boundary_read_states[face].lock();
		const auto compare = [&](uint8_t face, int32_t x, int32_t y,
			int32_t z, uint8_t after) noexcept
		{
			const std::size_t cell = WorldChunkReadState::light_offset(x, y, z);
			const uint8_t before = snapshot.existing_light[cell];
			if (before == after)
				return ;
			const uint8_t bit = static_cast<uint8_t>(1U << face);
			result.changed_light_faces |= bit;
			const int32_t neighbor_x = face < 2 ? GAME_VOXEL_CHUNK_WIDTH - 1 - x : x;
			const int32_t neighbor_z = face >= 2 ? GAME_VOXEL_CHUNK_DEPTH - 1 - z : z;
			const WorldGenerationPipeline::BoundaryLightChange change = {
				static_cast<uint8_t>(neighbor_x), static_cast<uint8_t>(neighbor_z),
				static_cast<uint16_t>(y), before, after,
				voxel_block_occludes_faces(snapshot.blocks[cell]) == FT_FALSE};
			auto &face_changes = result.boundary_light_changes[face];
			if (face_changes.count < face_changes.cells.size())
				face_changes.cells[face_changes.count++] = change;
			else
				face_changes.complete = false;
			const auto &neighbor = neighbors[face];
			if (neighbor == nullptr || neighbor->initialized == FT_FALSE)
				return ;
			const uint8_t effect = change.effect_on(*neighbor);
			if ((effect & WorldGenerationPipeline::BoundaryLightChange::RELIGHT) != 0U)
				result.neighbor_relight_faces |= bit;
			if ((effect & WorldGenerationPipeline::BoundaryLightChange::REMESH) != 0U)
				result.neighbor_remesh_faces |= bit;
		};
		if (result.incremental_light_deltas_complete != FT_FALSE)
		{
			for (const auto &delta : result.incremental_light_deltas)
			{
				const int32_t x = delta.local_x();
				const int32_t y = delta.local_y();
				const int32_t z = delta.local_z();
				const uint8_t after = delta.packed_light();
				if (x == 0)
					compare(0U, x, y, z, after);
				if (x == GAME_VOXEL_CHUNK_WIDTH - 1)
					compare(1U, x, y, z, after);
				if (z == 0)
					compare(2U, x, y, z, after);
				if (z == GAME_VOXEL_CHUNK_DEPTH - 1)
					compare(3U, x, y, z, after);
			}
			return ;
		}
		for (uint8_t face = 0; face < neighbors.size(); ++face)
		{
			const bool fixed_x = face < 2;
			const int32_t limit = fixed_x ? GAME_VOXEL_CHUNK_DEPTH : GAME_VOXEL_CHUNK_WIDTH;
			const int32_t fixed = (face & 1U) == 0U ? 0
				: (fixed_x ? GAME_VOXEL_CHUNK_WIDTH : GAME_VOXEL_CHUNK_DEPTH) - 1;
			for (int32_t y = 0; y < GAME_VOXEL_CHUNK_HEIGHT; ++y)
			{
				for (int32_t tangent = 0; tangent < limit; ++tangent)
				{
					const int32_t x = fixed_x ? fixed : tangent;
					const int32_t z = fixed_x ? tangent : fixed;
					compare(face, x, y, z, result.light->get(x, y, z));
				}
			}
		}
	}


	int32_t collect_incremental_light_deltas(
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		const voxel_light_chunk &light, uint32_t modified_sections,
		std::vector<WorldGenerationPipeline::IncrementalLightDelta> &deltas,
		ft_bool *complete) noexcept
	{
		if (complete == nullptr
			|| snapshot.existing_light.size() < static_cast<std::size_t>(
				GAME_VOXEL_CHUNK_WIDTH) * GAME_VOXEL_CHUNK_HEIGHT
				* GAME_VOXEL_CHUNK_DEPTH)
			return (FT_ERR_INVALID_ARGUMENT);
		*complete = FT_FALSE;
		try
		{
			deltas.clear();
			deltas.reserve(256U);
			const int32_t section_height = GAME_VOXEL_CHUNK_HEIGHT
				/ GAME_VOXEL_CHUNK_SECTION_COUNT;
			for (int32_t section = 0; section < GAME_VOXEL_CHUNK_SECTION_COUNT;
				++section)
			{
				if ((modified_sections & (1U << section)) == 0U)
					continue ;
				const int32_t first_y = section * section_height;
				for (int32_t local_z = 0; local_z < GAME_VOXEL_CHUNK_DEPTH; ++local_z)
				{
					for (int32_t local_y = first_y; local_y < first_y + section_height;
						++local_y)
					{
						for (int32_t local_x = 0; local_x < GAME_VOXEL_CHUNK_WIDTH;
							++local_x)
						{
							const std::size_t index = (static_cast<std::size_t>(local_z)
								* GAME_VOXEL_CHUNK_HEIGHT + local_y)
								* GAME_VOXEL_CHUNK_WIDTH + local_x;
							const uint8_t value = light.get(local_x, local_y, local_z);
							if (value == snapshot.existing_light[index])
								continue ;
							WorldGenerationPipeline::IncrementalLightDelta delta;

							delta.set(static_cast<uint8_t>(local_x),
								static_cast<uint16_t>(local_y),
								static_cast<uint8_t>(local_z), value);
							deltas.push_back(delta);
						}
					}
				}
			}
		}
		catch (...)
		{
			deltas.clear();
			return (FT_ERR_NO_MEMORY);
		}
		*complete = FT_TRUE;
		return (FT_ERR_SUCCESS);
	}

	int32_t build_preview_mesh_light(
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		voxel_light_chunk &mesh_light) noexcept
	{
		int32_t error_code;
		std::size_t index;

		if (snapshot.existing_light.size() < static_cast<std::size_t>(
			GAME_VOXEL_CHUNK_WIDTH) * GAME_VOXEL_CHUNK_HEIGHT
			* GAME_VOXEL_CHUNK_DEPTH)
			return (FT_ERR_INVALID_ARGUMENT);
		error_code = mesh_light.initialize();
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		index = 0U;
		for (int32_t local_z = 0;
			local_z < GAME_VOXEL_CHUNK_DEPTH; ++local_z)
		{
			for (int32_t local_y = 0;
				local_y < GAME_VOXEL_CHUNK_HEIGHT; ++local_y)
			{
				for (int32_t local_x = 0;
					local_x < GAME_VOXEL_CHUNK_WIDTH; ++local_x)
				{
					const uint8_t light_value = snapshot.existing_light[index];
					error_code = mesh_light.set(local_x, local_y, local_z,
						light_value);
					if (error_code != FT_ERR_SUCCESS)
						return (error_code);
					index += 1U;
				}
			}
		}
		return (FT_ERR_SUCCESS);
	}


	int32_t lookup_local_light_block(void *user_data, int32_t world_x,
		int32_t world_y, int32_t world_z, uint32_t *block_id) noexcept
	{
		const auto *chunk = static_cast<const game_voxel_chunk *>(user_data);
		if (chunk == nullptr || block_id == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		if (world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT)
			return (FT_ERR_OUT_OF_RANGE);
		world_x %= GAME_VOXEL_CHUNK_WIDTH;
		world_z %= GAME_VOXEL_CHUNK_DEPTH;
		if (world_x < 0)
			world_x += GAME_VOXEL_CHUNK_WIDTH;
		if (world_z < 0)
			world_z += GAME_VOXEL_CHUNK_DEPTH;
		/* The generation worker owns this chunk until its mesh is ready.
		 * Read its packed sections directly without taking a lock per sample. */
		const uint8_t section = static_cast<uint8_t>(world_y / GAME_VOXEL_SECTION_EDGE);
		const uint16_t index = static_cast<uint16_t>(world_x
			+ world_z * GAME_VOXEL_CHUNK_WIDTH
			+ (world_y % GAME_VOXEL_SECTION_EDGE) * GAME_VOXEL_SECTION_EDGE
				* GAME_VOXEL_SECTION_EDGE);
		*block_id = chunk->get_section(section).get_block(index);
		return (FT_ERR_SUCCESS);
	}

	bool snapshot_column_has_direct_sky(
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		incremental_light_scratch &scratch, int32_t local_x, int32_t local_y,
		int32_t local_z) noexcept
	{
		uint16_t &first_y_plus_one = scratch.first_direct_sky_y_plus_one[
			local_z * GAME_VOXEL_CHUNK_WIDTH + local_x];

		if (first_y_plus_one == 0U)
		{
			const std::size_t column = static_cast<std::size_t>(local_z)
				* GAME_VOXEL_CHUNK_HEIGHT * GAME_VOXEL_CHUNK_WIDTH + local_x;
			int32_t y = GAME_VOXEL_CHUNK_HEIGHT - 1;
			while (y >= 0)
			{
				const uint32_t block_id = snapshot.blocks[column
					+ static_cast<std::size_t>(y) * GAME_VOXEL_CHUNK_WIDTH];
				if (block_id != GAME_VOXEL_AIR_BLOCK)
				{
					const voxel_block_metadata &metadata = voxel_get_block_metadata(block_id);
					if (metadata.light_attenuation >= 15U || metadata.transparent == FT_FALSE)
						break ;
				}
				--y;
			}
			first_y_plus_one = static_cast<uint16_t>(y + 2);
		}
		return (local_y + 1 >= first_y_plus_one);
	}

	uint8_t snapshot_sky_after_block_change(
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		incremental_light_scratch &scratch,
		int32_t chunk_x, int32_t chunk_z, int32_t local_x, int32_t local_y,
		int32_t local_z, uint32_t new_block_id) noexcept
	{
		const voxel_block_metadata *metadata =
			&voxel_get_block_metadata(new_block_id);
		uint8_t source_sky;
		uint8_t attenuation;

		if (metadata->occludes_faces != FT_FALSE
			|| metadata->light_attenuation >= 15U)
			return (0U);
		/* The above cell may still carry the old dark value because the
		 * opaque block being removed prevented its prior skylight. Re-open a
		 * true sky column from voxel geometry instead of inheriting that stale
		 * derived light sample. */
		if (local_y + 1 >= GAME_VOXEL_CHUNK_HEIGHT
			|| snapshot_column_has_direct_sky(snapshot, scratch,
				local_x, local_y + 1, local_z))
			return (15U);
		attenuation = metadata->light_attenuation;
		if (local_y + 1 >= GAME_VOXEL_CHUNK_HEIGHT)
			source_sky = 15U;
		else if (WorldChunkSnapshotReader::lookup_snapshot_light(
			const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
			chunk_x * GAME_VOXEL_CHUNK_WIDTH + local_x, local_y + 1,
			chunk_z * GAME_VOXEL_CHUNK_DEPTH + local_z, &source_sky)
			!= FT_ERR_SUCCESS)
			return (0U);
		source_sky = voxel_light_sky(source_sky);
		return (source_sky > attenuation
			? static_cast<uint8_t>(source_sky - attenuation) : 0U);
	}

	bool snapshot_light_face_is_blocked(
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		int32_t local_x, int32_t local_z) noexcept
	{
		return ((local_x < 0 && (snapshot.blocked_light_faces & 1U) != 0U)
			|| (local_x >= GAME_VOXEL_CHUNK_WIDTH
				&& (snapshot.blocked_light_faces & 2U) != 0U)
			|| (local_z < 0 && (snapshot.blocked_light_faces & 4U) != 0U)
			|| (local_z >= GAME_VOXEL_CHUNK_DEPTH
				&& (snapshot.blocked_light_faces & 8U) != 0U));
	}

	int32_t build_incremental_additive_light(voxel_light_chunk &light,
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		incremental_light_scratch &scratch,
		int32_t chunk_x, int32_t chunk_z, int32_t changed_x,
		int32_t changed_y, int32_t changed_z, uint64_t *processed,
		ft_bool preserve_existing,
		const std::shared_ptr<std::atomic<uint64_t>> &cancellation_token,
		uint64_t cancellation_token_value,
		std::vector<incremental_light_node> &queue,
		std::size_t *queue_index, ft_bool *state_initialized,
		const IncrementalLightSlice &slice, uint64_t previous_nodes,
		ft_bool *complete) noexcept
	{
		std::size_t index;
		int32_t local_z;
		uint32_t processed_this_call;

		if (processed == nullptr || changed_x < 0
			|| changed_x >= GAME_VOXEL_CHUNK_WIDTH || changed_z < 0
			|| changed_z >= GAME_VOXEL_CHUNK_DEPTH || changed_y < 0
			|| changed_y >= GAME_VOXEL_CHUNK_HEIGHT
			|| snapshot.existing_light.size()
			< static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_DEPTH)
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT))
			return (FT_ERR_INVALID_ARGUMENT);
		if (queue_index == nullptr || state_initialized == nullptr
			|| complete == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		if (*state_initialized == FT_FALSE)
		{
			if (preserve_existing == FT_FALSE)
			{
				if (light.initialize(0U) != FT_ERR_SUCCESS)
					return (FT_ERR_NO_MEMORY);
				local_z = 0;
				while (local_z < GAME_VOXEL_CHUNK_DEPTH)
				{
					int32_t local_y = 0;
					while (local_y < GAME_VOXEL_CHUNK_HEIGHT)
					{
						int32_t local_x = 0;
						while (local_x < GAME_VOXEL_CHUNK_WIDTH)
						{
							index = (static_cast<std::size_t>(local_z)
								* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
								+ static_cast<std::size_t>(local_y))
								* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
								+ static_cast<std::size_t>(local_x);
							if (light.set(local_x, local_y, local_z,
								snapshot.existing_light[index]) != FT_ERR_SUCCESS)
								return (FT_ERR_NO_MEMORY);
							local_x += 1;
						}
						local_y += 1;
					}
					local_z += 1;
				}
			}
			if (snapshot_column_has_direct_sky(snapshot, scratch,
			changed_x, changed_y, changed_z))
			{
				const uint8_t current = light.get(changed_x, changed_y, changed_z);
				if (write_incremental_light(light, scratch, changed_x, changed_y, changed_z,
					voxel_light_pack(15U, voxel_light_block(current)))
					!= FT_ERR_SUCCESS)
					return (FT_ERR_NO_MEMORY);
			}
			{
			static const int32_t delta_x[6] = {1, -1, 0, 0, 0, 0};
			static const int32_t delta_y[6] = {0, 0, 1, -1, 0, 0};
			static const int32_t delta_z[6] = {0, 0, 0, 0, 1, -1};
			uint32_t changed_block_id = GAME_VOXEL_AIR_BLOCK;
			const voxel_block_metadata *metadata;
			uint8_t direction = 0U;

			if (WorldChunkSnapshotReader::lookup_snapshot_block(
				const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
				chunk_x * GAME_VOXEL_CHUNK_WIDTH + changed_x, changed_y,
				chunk_z * GAME_VOXEL_CHUNK_DEPTH + changed_z,
				&changed_block_id) != FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_OPERATION);
			metadata = &voxel_get_block_metadata(changed_block_id);
			while (direction < 6U)
			{
				const int32_t neighbour_x = changed_x + delta_x[direction];
				const int32_t neighbour_y = changed_y + delta_y[direction];
				const int32_t neighbour_z = changed_z + delta_z[direction];
				if (neighbour_x >= 0 && neighbour_x < GAME_VOXEL_CHUNK_WIDTH
					&& neighbour_y >= 0
					&& neighbour_y < GAME_VOXEL_CHUNK_HEIGHT
					&& neighbour_z >= 0
					&& neighbour_z < GAME_VOXEL_CHUNK_DEPTH)
				{
					direction += 1U;
					continue ;
				}
				uint8_t neighbour_light = 0U;
				uint8_t cost = metadata->light_attenuation;
				if (!snapshot_light_face_is_blocked(snapshot, neighbour_x, neighbour_z)
					&& WorldChunkSnapshotReader::lookup_snapshot_light(
					const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
					chunk_x * GAME_VOXEL_CHUNK_WIDTH + neighbour_x, neighbour_y,
					chunk_z * GAME_VOXEL_CHUNK_DEPTH + neighbour_z,
					&neighbour_light) == FT_ERR_SUCCESS)
				{
					if (cost < 1U)
						cost = 1U;
					const uint8_t candidate_sky =
						voxel_light_sky(neighbour_light) > cost
						? static_cast<uint8_t>(voxel_light_sky(neighbour_light) - cost)
						: 0U;
					const uint8_t candidate_block =
						voxel_light_block(neighbour_light) > cost
						? static_cast<uint8_t>(voxel_light_block(neighbour_light) - cost)
						: 0U;
					const uint8_t current = light.get(changed_x, changed_y,
						changed_z);
					const uint8_t result_sky = candidate_sky
						> voxel_light_sky(current) ? candidate_sky
						: voxel_light_sky(current);
					const uint8_t result_block = candidate_block
						> voxel_light_block(current) ? candidate_block
						: voxel_light_block(current);
					if (write_incremental_light(light, scratch, changed_x, changed_y, changed_z,
						voxel_light_pack(result_sky, result_block))
						!= FT_ERR_SUCCESS)
						return (FT_ERR_NO_MEMORY);
				}
				direction += 1U;
			}
		}
			try
		{
			queue.clear();
			queue.reserve(64U);
			queue.push_back({changed_x, changed_y, changed_z});
			queue.push_back({changed_x - 1, changed_y, changed_z});
			queue.push_back({changed_x + 1, changed_y, changed_z});
			queue.push_back({changed_x, changed_y - 1, changed_z});
			queue.push_back({changed_x, changed_y + 1, changed_z});
			queue.push_back({changed_x, changed_y, changed_z - 1});
			queue.push_back({changed_x, changed_y, changed_z + 1});
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
			*queue_index = 0U;
			*state_initialized = FT_TRUE;
		}
		index = *queue_index;
		processed_this_call = 0U;
		*processed = 0U;
		while (index < queue.size()
			&& slice.permits(previous_nodes + processed_this_call))
		{
			if ((index & 255U) == 0U
				&& incremental_request_is_cancelled(cancellation_token,
					cancellation_token_value))
				return (FT_ERR_INVALID_STATE);
			const incremental_light_node node = queue[index];
			static const int32_t delta_x[6] = {1, -1, 0, 0, 0, 0};
			static const int32_t delta_y[6] = {0, 0, 1, -1, 0, 0};
			static const int32_t delta_z[6] = {0, 0, 0, 0, 1, -1};
			uint8_t direction = 0U;
			if (node.x < 0 || node.x >= GAME_VOXEL_CHUNK_WIDTH
				|| node.y < 0 || node.y >= GAME_VOXEL_CHUNK_HEIGHT
				|| node.z < 0 || node.z >= GAME_VOXEL_CHUNK_DEPTH)
			{
				index += 1U;
				processed_this_call += 1U;
				continue ;
			}
			const uint8_t source = light.get(node.x, node.y, node.z);

			while (direction < 6U)
			{
				const int32_t x = node.x + delta_x[direction];
				const int32_t y = node.y + delta_y[direction];
				const int32_t z = node.z + delta_z[direction];
				uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
				uint8_t destination;
				uint8_t candidate_sky;
				uint8_t candidate_block;
				const voxel_block_metadata *metadata;

				if (x >= 0 && x < GAME_VOXEL_CHUNK_WIDTH && y >= 0
					&& y < GAME_VOXEL_CHUNK_HEIGHT && z >= 0
					&& z < GAME_VOXEL_CHUNK_DEPTH)
				{
					if (WorldChunkSnapshotReader::lookup_snapshot_block(
						const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(
							&snapshot), chunk_x * GAME_VOXEL_CHUNK_WIDTH + x,
							 y, chunk_z * GAME_VOXEL_CHUNK_DEPTH + z,
							&block_id) != FT_ERR_SUCCESS)
						return (FT_ERR_INVALID_OPERATION);
					metadata = &voxel_get_block_metadata(block_id);
					if (metadata->light_attenuation < 15U
						&& metadata->occludes_faces == FT_FALSE)
					{
						uint8_t cost = metadata->light_attenuation;
						if (cost < 1U)
							cost = 1U;
						if (snapshot_column_has_direct_sky(snapshot, scratch,
							x, y, z))
							candidate_sky = 15U;
						else
							candidate_sky = voxel_light_sky(source) > cost
								? static_cast<uint8_t>(voxel_light_sky(source) - cost) : 0U;
						candidate_block = voxel_light_block(source) > cost
							? static_cast<uint8_t>(voxel_light_block(source) - cost) : 0U;
						destination = light.get(x, y, z);
						if (candidate_sky > voxel_light_sky(destination)
							|| candidate_block > voxel_light_block(destination))
						{
							uint8_t result_sky = candidate_sky
								> voxel_light_sky(destination) ? candidate_sky
								: voxel_light_sky(destination);
							uint8_t result_block = candidate_block
								> voxel_light_block(destination) ? candidate_block
								: voxel_light_block(destination);
							if (write_incremental_light(light, scratch, x, y, z,
								voxel_light_pack(result_sky, result_block))
								!= FT_ERR_SUCCESS)
								return (FT_ERR_NO_MEMORY);
							try
							{
								queue.push_back({x, y, z});
							}
							catch (...)
							{
								return (FT_ERR_NO_MEMORY);
							}
						}
					}
				}
				direction += 1U;
			}
			index += 1U;
			*processed += 1U;
			processed_this_call += 1U;
		}
		*queue_index = index;
		*complete = index >= queue.size() ? FT_TRUE : FT_FALSE;
		if (*complete != FT_FALSE)
		{
			*state_initialized = FT_FALSE;
			queue.clear();
		}
		return (FT_ERR_SUCCESS);
	}

	uint8_t incremental_channel_value(uint8_t packed_light,
		uint8_t channel) noexcept
	{
		if (channel == 0U)
			return (voxel_light_sky(packed_light));
		return (voxel_light_block(packed_light));
	}

	uint8_t incremental_set_channel(uint8_t packed_light, uint8_t channel,
		uint8_t value) noexcept
	{
		uint8_t sky_light = voxel_light_sky(packed_light);
		uint8_t block_light = voxel_light_block(packed_light);

		if (channel == 0U)
			sky_light = value;
		else
			block_light = value;
		return (voxel_light_pack(sky_light, block_light));
	}

	std::size_t incremental_removal_seen_index(int32_t x, int32_t y,
		int32_t z, uint8_t channel) noexcept
	{
		const std::size_t voxel_index =
			(static_cast<std::size_t>(z)
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
				+ static_cast<std::size_t>(y))
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
			+ static_cast<std::size_t>(x);
		return (voxel_index * 2U + static_cast<std::size_t>(channel));
	}

	uint8_t incremental_recomputed_channel(const voxel_light_chunk &light,
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		incremental_light_scratch &scratch,
		int32_t chunk_x, int32_t chunk_z, int32_t x, int32_t y, int32_t z,
		uint8_t channel) noexcept
	{
		static const int32_t delta_x[6] = {1, -1, 0, 0, 0, 0};
		static const int32_t delta_y[6] = {0, 0, 1, -1, 0, 0};
		static const int32_t delta_z[6] = {0, 0, 0, 0, 1, -1};
		uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
		const voxel_block_metadata *metadata;
		uint8_t result = 0U;
		uint8_t direction = 0U;

		if (WorldChunkSnapshotReader::lookup_snapshot_block(
			const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
			chunk_x * GAME_VOXEL_CHUNK_WIDTH + x, y,
			chunk_z * GAME_VOXEL_CHUNK_DEPTH + z, &block_id) != FT_ERR_SUCCESS)
			return (0U);
		metadata = &voxel_get_block_metadata(block_id);
		if (channel == 1U)
		{
			result = voxel_block_emitted_light_level(block_id);
			if (result != 0U)
				return (result);
		}
		if (metadata->light_attenuation >= 15U
			|| metadata->occludes_faces != FT_FALSE)
			return (0U);
		if (channel == 0U
			&& snapshot_column_has_direct_sky(snapshot, scratch,
				x, y, z))
			result = 15U;
		while (direction < 6U)
		{
			const int32_t source_x = x + delta_x[direction];
			const int32_t source_y = y + delta_y[direction];
			const int32_t source_z = z + delta_z[direction];
			uint8_t source_light = 0U;
			uint8_t source_value;
			uint8_t cost;

			if (source_x >= 0 && source_x < GAME_VOXEL_CHUNK_WIDTH
				&& source_y >= 0 && source_y < GAME_VOXEL_CHUNK_HEIGHT
				&& source_z >= 0 && source_z < GAME_VOXEL_CHUNK_DEPTH)
				source_light = light.get(source_x, source_y, source_z);
			else if (snapshot_light_face_is_blocked(snapshot, source_x, source_z)
				|| WorldChunkSnapshotReader::lookup_snapshot_light(
					const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(
						&snapshot),
					chunk_x * GAME_VOXEL_CHUNK_WIDTH + source_x, source_y,
					chunk_z * GAME_VOXEL_CHUNK_DEPTH + source_z, &source_light)
					!= FT_ERR_SUCCESS)
			{
				direction += 1U;
				continue ;
			}
			cost = metadata->light_attenuation;
			if (cost < 1U)
				cost = 1U;
			source_value = channel == 0U
				? voxel_light_sky(source_light) : voxel_light_block(source_light);
			if (source_value > cost
				&& static_cast<uint8_t>(source_value - cost) > result)
				result = static_cast<uint8_t>(source_value - cost);
			direction += 1U;
		}
		return (result);
	}

	int32_t build_incremental_removal_light(voxel_light_chunk &light,
		const WorldGenerationPipeline::WorldChunkSnapshot &snapshot,
		incremental_light_scratch &scratch,
		int32_t chunk_x, int32_t chunk_z, int32_t changed_x,
		int32_t changed_y, int32_t changed_z, uint32_t old_block_id,
		uint32_t new_block_id, uint64_t *processed,
		ft_bool preserve_existing, ft_bool external_source,
		uint8_t old_source_light,
		const std::shared_ptr<std::atomic<uint64_t>> &cancellation_token,
		uint64_t cancellation_token_value,
		std::vector<incremental_removal_node> &removal_queue,
		std::vector<uint8_t> &removal_queued_cells,
		std::size_t *removal_queue_index,
		std::vector<incremental_removal_node> &addition_queue,
		std::size_t *addition_queue_index,
		ft_bool *state_initialized,
		ft_bool *addition_started, const IncrementalLightSlice &slice,
		uint64_t previous_nodes, ft_bool *complete) noexcept
	{
		std::size_t index;
		int32_t local_z;

		if (processed == nullptr || removal_queue_index == nullptr
			|| addition_queue_index == nullptr || state_initialized == nullptr
			|| addition_started == nullptr || complete == nullptr
			|| changed_x < 0
			|| changed_x >= GAME_VOXEL_CHUNK_WIDTH || changed_z < 0
			|| changed_z >= GAME_VOXEL_CHUNK_DEPTH || changed_y < 0
			|| changed_y >= GAME_VOXEL_CHUNK_HEIGHT
			|| snapshot.existing_light.size()
			< static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_DEPTH)
				* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT))
			return (FT_ERR_INVALID_ARGUMENT);
		*processed = 0U;
		*complete = FT_FALSE;
		if (*state_initialized == FT_FALSE && preserve_existing == FT_FALSE)
		{
			if (light.initialize(0U) != FT_ERR_SUCCESS)
				return (FT_ERR_NO_MEMORY);
			local_z = 0;
			while (local_z < GAME_VOXEL_CHUNK_DEPTH)
			{
				int32_t local_y = 0;
				while (local_y < GAME_VOXEL_CHUNK_HEIGHT)
				{
					int32_t local_x = 0;
					while (local_x < GAME_VOXEL_CHUNK_WIDTH)
					{
						const std::size_t source_index =
							(static_cast<std::size_t>(local_z)
							* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
							+ static_cast<std::size_t>(local_y))
							* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
							+ static_cast<std::size_t>(local_x);
						if (light.set(local_x, local_y, local_z,
							snapshot.existing_light[source_index]) != FT_ERR_SUCCESS)
							return (FT_ERR_NO_MEMORY);
						local_x += 1;
					}
					local_y += 1;
				}
				local_z += 1;
			}
		}
		if (*state_initialized == FT_FALSE)
		{
			try
			{
				const std::size_t voxel_count =
					static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
					* static_cast<std::size_t>(GAME_VOXEL_CHUNK_DEPTH)
					* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT);
				removal_queued_cells.assign(voxel_count * 2U, 0U);
				removal_queue.reserve(128U);
				addition_queue.reserve(128U);
			}
			catch (...)
			{
				return (FT_ERR_NO_MEMORY);
			}
		}
		auto enqueue_removal = [&](int32_t x, int32_t y, int32_t z,
			uint8_t channel, uint8_t level) -> int32_t
		{
			const std::size_t seen_index = incremental_removal_seen_index(
				x, y, z, channel);
			/* Keep the strongest frontier level seen at this cell.  A weaker
			 * arrival must not fan out repeatedly, but a later stronger path must
			 * be allowed through so removal remains correct in branching light
			 * fields. */
			if (removal_queued_cells[seen_index] >= level)
				return (FT_ERR_SUCCESS);
			try
			{
				removal_queue.push_back({x, y, z, channel, level});
			}
			catch (...)
			{
				return (FT_ERR_NO_MEMORY);
			}
			removal_queued_cells[seen_index] = level;
			return (FT_ERR_SUCCESS);
		};
		if (*state_initialized == FT_FALSE && external_source != FT_FALSE)
		{
			uint32_t target_block_id = GAME_VOXEL_AIR_BLOCK;
			if (WorldChunkSnapshotReader::lookup_snapshot_block(
				const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
				chunk_x * GAME_VOXEL_CHUNK_WIDTH + changed_x, changed_y,
				chunk_z * GAME_VOXEL_CHUNK_DEPTH + changed_z, &target_block_id)
				!= FT_ERR_SUCCESS)
				return (FT_ERR_INVALID_STATE);
			const voxel_block_metadata *target_metadata;
			uint8_t attenuation;
			target_metadata = &voxel_get_block_metadata(target_block_id);
			if (target_metadata->occludes_faces != FT_FALSE
				|| target_metadata->light_attenuation >= 15U)
				attenuation = 15U;
			else
			{
				attenuation = target_metadata->light_attenuation;
				if (attenuation < 1U)
					attenuation = 1U;
			}
			for (uint8_t channel = 0U; channel < 2U; ++channel)
			{
				const uint8_t old_source_level = channel == 0U
					? voxel_light_sky(old_source_light)
					: voxel_light_block(old_source_light);
				const uint8_t old_contribution = old_source_level > attenuation
					? static_cast<uint8_t>(old_source_level - attenuation) : 0U;
				if (old_contribution > 0U)
				{
					if (enqueue_removal(changed_x, changed_y, changed_z,
						channel, old_contribution) != FT_ERR_SUCCESS)
						return (FT_ERR_NO_MEMORY);
					if (incremental_channel_value(light.get(changed_x,
						changed_y, changed_z), channel) <= old_contribution
						&& write_incremental_light(light, scratch, changed_x, changed_y, changed_z,
						incremental_set_channel(light.get(changed_x, changed_y,
						changed_z), channel, 0U)) != FT_ERR_SUCCESS)
						return (FT_ERR_NO_MEMORY);
				}
			}
		}
		else if (*state_initialized == FT_FALSE)
		{
			/* Clear the root once. Later slices may already have restored
			 * this cell from an independent source. */
			const uint8_t old_light = light.get(changed_x, changed_y, changed_z);
			if (voxel_light_sky(old_light) != 0U)
			{
				if (enqueue_removal(changed_x, changed_y, changed_z, 0U,
					voxel_light_sky(old_light)) != FT_ERR_SUCCESS)
					return (FT_ERR_NO_MEMORY);
			}
			if (voxel_light_block(old_light) != 0U)
			{
				if (enqueue_removal(changed_x, changed_y, changed_z, 1U,
					voxel_light_block(old_light)) != FT_ERR_SUCCESS)
					return (FT_ERR_NO_MEMORY);
			}
			if (write_incremental_light(light, scratch, changed_x, changed_y, changed_z, 0U)
				!= FT_ERR_SUCCESS)
				return (FT_ERR_NO_MEMORY);
		}
		if (*state_initialized == FT_FALSE && external_source == FT_FALSE)
		{
			const uint8_t new_emission =
				voxel_block_emitted_light_level(new_block_id);
			const voxel_block_metadata *old_metadata =
				&voxel_get_block_metadata(old_block_id);
			const voxel_block_metadata *new_metadata =
				&voxel_get_block_metadata(new_block_id);
			const bool old_blocks_sky = old_metadata->occludes_faces != FT_FALSE
				|| old_metadata->light_attenuation >= 15U;
			const bool new_blocks_sky = new_metadata->occludes_faces != FT_FALSE
				|| new_metadata->light_attenuation >= 15U;
			if (new_emission != 0U)
			{
				if (write_incremental_light(light, scratch, changed_x, changed_y, changed_z,
					voxel_light_pack(0U, new_emission)) != FT_ERR_SUCCESS)
					return (FT_ERR_NO_MEMORY);
				addition_queue.push_back({changed_x, changed_y, changed_z, 1U,
					new_emission});
			}
			uint8_t restored_sky = snapshot_sky_after_block_change(snapshot, scratch,
				chunk_x, chunk_z, changed_x, changed_y, changed_z, new_block_id);
			/* The edit snapshot already contains the authoritative sky value at
		 * this cell.  Preserve it when the edit only removed block light; the
		 * above-cell estimate is still needed when an opaque block previously
		 * masked a newly opened sky column. */
			{
				const std::size_t changed_index =
					(static_cast<std::size_t>(changed_z)
					* static_cast<std::size_t>(GAME_VOXEL_CHUNK_HEIGHT)
					+ static_cast<std::size_t>(changed_y))
					* static_cast<std::size_t>(GAME_VOXEL_CHUNK_WIDTH)
					+ static_cast<std::size_t>(changed_x);
				const uint8_t previous_sky = voxel_light_sky(
					snapshot.existing_light[changed_index]);
				/* Removing an emissive block must not reinterpret the old sky
				 * channel as a fresh open-column solve.  The snapshot value is the
				 * authoritative pre-edit sky state; use it directly for that case.
				 * For an opaque-block removal, retain the larger derived value so a
				 * newly opened sky column can still be restored. */
				if (new_blocks_sky)
					restored_sky = 0U;
				else if (voxel_block_emitted_light_level(old_block_id) != 0U
					&& voxel_block_emitted_light_level(new_block_id) == 0U)
					restored_sky = previous_sky;
				else if (old_blocks_sky && previous_sky > restored_sky)
					restored_sky = previous_sky;
			}
			if (restored_sky != 0U)
			{
				if (write_incremental_light(light, scratch, changed_x, changed_y, changed_z,
					voxel_light_pack(restored_sky,
						voxel_light_block(light.get(changed_x, changed_y, changed_z))))
					!= FT_ERR_SUCCESS)
					return (FT_ERR_NO_MEMORY);
				addition_queue.push_back({changed_x, changed_y, changed_z, 0U,
					restored_sky});
			}
		}
		if (*state_initialized == FT_FALSE)
		{
			*removal_queue_index = 0U;
			*addition_queue_index = 0U;
			*addition_started = FT_FALSE;
			*state_initialized = FT_TRUE;
		}
		index = *removal_queue_index;
		while (index < removal_queue.size()
			&& slice.permits(previous_nodes + *processed))
		{
			if ((index & 255U) == 0U
				&& incremental_request_is_cancelled(cancellation_token,
					cancellation_token_value))
				return (FT_ERR_INVALID_STATE);
			const incremental_removal_node node = removal_queue[index];
			static const int32_t delta_x[6] = {1, -1, 0, 0, 0, 0};
			static const int32_t delta_y[6] = {0, 0, 1, -1, 0, 0};
			static const int32_t delta_z[6] = {0, 0, 0, 0, 1, -1};
			uint8_t direction = 0U;

			while (direction < 6U)
			{
				const int32_t x = node.x + delta_x[direction];
				const int32_t y = node.y + delta_y[direction];
				const int32_t z = node.z + delta_z[direction];
				if (x >= 0 && x < GAME_VOXEL_CHUNK_WIDTH && y >= 0
					&& y < GAME_VOXEL_CHUNK_HEIGHT && z >= 0
					&& z < GAME_VOXEL_CHUNK_DEPTH)
				{
					const uint8_t current = incremental_channel_value(
						light.get(x, y, z), node.channel);
					const bool direct_sky = node.channel == 0U
						&& snapshot_column_has_direct_sky(snapshot, scratch,
							x, y, z);
					if (direct_sky)
					{
						/* A column that is still open to the top of the world is
						 * an independent source.  It must not be darkened merely
						 * because the removed path had the same level. */
						if (current < 15U
							&& write_incremental_light(light, scratch, x, y, z, incremental_set_channel(
								light.get(x, y, z), node.channel, 15U))
							!= FT_ERR_SUCCESS)
							return (FT_ERR_NO_MEMORY);
					}
					else if (current != 0U)
					{
						uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
						if (WorldChunkSnapshotReader::lookup_snapshot_block(
							const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(&snapshot),
							chunk_x * GAME_VOXEL_CHUNK_WIDTH + x, y,
							chunk_z * GAME_VOXEL_CHUNK_DEPTH + z,
							&block_id) != FT_ERR_SUCCESS)
							return (FT_ERR_INVALID_OPERATION);
						const auto &metadata = voxel_get_block_metadata(block_id);
						const uint8_t cost = std::max<uint8_t>(1U,
							metadata.light_attenuation);
						/* A former direct-sky column can carry 15 straight
						 * down even though the new roof now blocks that path. */
						const bool removed_direct_sky = node.channel == 0U
							&& node.level == 15U && delta_y[direction] == -1
							&& current == 15U && cost < 15U
							&& metadata.transparent != FT_FALSE;
						const bool could_supply = removed_direct_sky
							|| (metadata.occludes_faces == FT_FALSE
								&& node.level > cost && current <= node.level - cost);
						const bool emits = node.channel == 1U
							&& voxel_block_emitted_light_level(block_id) >= current;
						if (!could_supply || emits)
						{
							try
							{
								addition_queue.push_back({x, y, z,
									node.channel, current});
							}
							catch (...)
							{
								return (FT_ERR_NO_MEMORY);
							}
							direction += 1U;
							continue ;
						}
						const std::size_t seen_index =
							incremental_removal_seen_index(x, y, z,
								node.channel);

						if (removal_queued_cells[seen_index] < current)
						{
							if (write_incremental_light(light, scratch, x, y, z,
								incremental_set_channel(light.get(x, y, z),
									node.channel, 0U)) != FT_ERR_SUCCESS)
								return (FT_ERR_NO_MEMORY);
							if (enqueue_removal(x, y, z, node.channel,
								current) != FT_ERR_SUCCESS)
								return (FT_ERR_NO_MEMORY);
							/* Defer replacement discovery until the complete removal
							 * frontier has drained. Re-seeding from neighbors while
							 * doomed light is still present resurrects the removed source. */
							try
							{
								addition_queue.push_back({x, y, z,
									node.channel, 0U});
							}
							catch (...)
							{
								return (FT_ERR_NO_MEMORY);
							}
						}
					}
				}
				direction += 1U;
			}
			index += 1U;
			*processed += 1U;
		}
		*removal_queue_index = index;
		if (index < removal_queue.size())
			return (FT_ERR_SUCCESS);
		if (*addition_started == FT_FALSE)
		{
			try
			{
				if (external_source != FT_FALSE)
				{
					/* Corner receivers can retain light from the other face. */
					addition_queue.push_back({changed_x, changed_y, changed_z, 0U, 0U});
					addition_queue.push_back({changed_x, changed_y, changed_z, 1U, 0U});
				}
				else if (old_block_id != new_block_id
					&& snapshot_column_has_direct_sky(snapshot, scratch,
						changed_x, changed_y, changed_z))
				{
					/* Removing an opaque emitter both withdraws block light and
					 * can open a sky column. Seed sky while propagation can still
					 * carry it to cells below the opened block. */
					const uint8_t packed = light.get(changed_x, changed_y, changed_z);
					if (write_incremental_light(light, scratch, changed_x, changed_y,
						changed_z, voxel_light_pack(15U, voxel_light_block(packed)))
						!= FT_ERR_SUCCESS)
						return (FT_ERR_NO_MEMORY);
					addition_queue.push_back({changed_x, changed_y, changed_z, 0U, 15U});
				}
			}
			catch (...)
			{
				return (FT_ERR_NO_MEMORY);
			}
			*addition_started = FT_TRUE;
		}
		index = *addition_queue_index;
		while (index < addition_queue.size()
			&& slice.permits(previous_nodes + *processed))
		{
			if ((index & 255U) == 0U
				&& incremental_request_is_cancelled(cancellation_token,
					cancellation_token_value))
				return (FT_ERR_INVALID_STATE);
			const incremental_removal_node node = addition_queue[index];
			if (node.level == 0U)
			{
				const uint8_t replacement = incremental_recomputed_channel(light,
					snapshot, scratch, chunk_x, chunk_z, node.x, node.y, node.z,
					node.channel);
				const uint8_t current = incremental_channel_value(
					light.get(node.x, node.y, node.z), node.channel);
				if (replacement > current)
				{
					if (write_incremental_light(light, scratch, node.x, node.y, node.z,
						incremental_set_channel(light.get(node.x, node.y, node.z),
							node.channel, replacement)) != FT_ERR_SUCCESS)
						return (FT_ERR_NO_MEMORY);
					try
					{
						addition_queue.push_back({node.x, node.y, node.z,
							node.channel, replacement});
					}
					catch (...)
					{
						return (FT_ERR_NO_MEMORY);
					}
				}
				index += 1U;
				*processed += 1U;
				continue ;
			}
			/* A stronger removal path may have cleared this queued source
			 * later in the removal phase. Read its surviving level now. */
			const uint8_t source_level = incremental_channel_value(
				light.get(node.x, node.y, node.z), node.channel);
			static const int32_t delta_x[6] = {1, -1, 0, 0, 0, 0};
			static const int32_t delta_y[6] = {0, 0, 1, -1, 0, 0};
			static const int32_t delta_z[6] = {0, 0, 0, 0, 1, -1};
			uint8_t direction = 0U;

			while (direction < 6U)
			{
				const int32_t x = node.x + delta_x[direction];
				const int32_t y = node.y + delta_y[direction];
				const int32_t z = node.z + delta_z[direction];
				uint32_t block_id = GAME_VOXEL_AIR_BLOCK;
				if (x >= 0 && x < GAME_VOXEL_CHUNK_WIDTH && y >= 0
					&& y < GAME_VOXEL_CHUNK_HEIGHT && z >= 0
					&& z < GAME_VOXEL_CHUNK_DEPTH
					&& WorldChunkSnapshotReader::lookup_snapshot_block(
						const_cast<WorldGenerationPipeline::WorldChunkSnapshot *>(
							&snapshot), chunk_x * GAME_VOXEL_CHUNK_WIDTH + x,
						y, chunk_z * GAME_VOXEL_CHUNK_DEPTH + z,
						&block_id) == FT_ERR_SUCCESS)
				{
					const voxel_block_metadata &metadata =
						voxel_get_block_metadata(block_id);
						if (metadata.light_attenuation < 15U
							&& metadata.occludes_faces == FT_FALSE)
						{
							uint8_t cost = metadata.light_attenuation;
							if (cost < 1U)
								cost = 1U;
							const bool direct_sky = node.channel == 0U
								&& source_level == 15U && delta_y[direction] == -1
								&& snapshot_column_has_direct_sky(snapshot, scratch, x, y, z);
							if (direct_sky || source_level > cost)
							{
								const uint8_t candidate = direct_sky ? 15U
									: static_cast<uint8_t>(source_level - cost);
								const uint8_t current = incremental_channel_value(
									light.get(x, y, z), node.channel);
								if (candidate > current)
								{
									if (write_incremental_light(light, scratch, x, y, z,
										incremental_set_channel(light.get(x, y, z),
											node.channel, candidate))
										!= FT_ERR_SUCCESS)
										return (FT_ERR_NO_MEMORY);
									try
									{
										addition_queue.push_back({x, y, z, node.channel,
											candidate});
									}
									catch (...)
									{
										return (FT_ERR_NO_MEMORY);
									}
								}
							}
						}
					}
				direction += 1U;
			}
			index += 1U;
			*processed += 1U;
		}
		*addition_queue_index = index;
		if (index < addition_queue.size())
			return (FT_ERR_SUCCESS);
		removal_queue.clear();
		addition_queue.clear();
		*removal_queue_index = 0U;
		*addition_queue_index = 0U;
		*addition_started = FT_FALSE;
		*state_initialized = FT_FALSE;
		*complete = FT_TRUE;
		return (FT_ERR_SUCCESS);
	}
}

WorldChunkGenerationWorker::WorldChunkGenerationWorker()
{
}

WorldChunkGenerationWorker::WorldChunkGenerationWorker(const WorldChunkGenerationWorker &other)
{
	(void)other;
}

WorldChunkGenerationWorker::~WorldChunkGenerationWorker()
{
}

WorldChunkGenerationWorker &WorldChunkGenerationWorker::operator=(const WorldChunkGenerationWorker &other)
{
	(void)other;
	return (*this);
}

int32_t WorldChunkGenerationWorker::initialize_chunk_for_generation(WorldChunk &chunk,
	int32_t chunk_x, int32_t chunk_z, const char *seed,
	voxel_generation_config &config, uint32_t stage_mask,
	std::vector<WorldGenerationPipeline::WorldDeferredBlockEdit> &deferred_edits,
	const std::vector<WorldGenerationPipeline::GenerationFeaturePlan>
		&feature_plans,
	const WorldGenerationPipeline::WorldChunkSnapshot *source_snapshot,
	const std::string &terrain_chunk_path, bool &loaded_from_terrain_store,
	uint64_t *generation_duration_nanoseconds,
	uint64_t *mesh_duration_nanoseconds) noexcept
{
	int32_t error_code;
	std::chrono::steady_clock::time_point phase_start;

	chunk.chunk_x = chunk_x;
	chunk.chunk_z = chunk_z;
	chunk.world_x = chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	chunk.world_z = chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	if (source_snapshot == nullptr)
		error_code = chunk.chunk.initialize();
	else
	{
	#if defined(DEBUG)
		std::fprintf(stderr,
			"[WorldRevision] worker snapshot-init chunk=(%d,%d) blocks=%zu "
			"metadata_valid=%d stages=%u\n", chunk_x, chunk_z,
			source_snapshot->blocks.size(),
			source_snapshot->generation_metadata.valid != FT_FALSE ? 1 : 0,
			source_snapshot->generation_metadata.completed_stage_mask);
	#endif
		error_code = WorldChunkSnapshotReader::initialize_snapshot_chunk(chunk.chunk,
				*source_snapshot);
	}
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (chunk_mesh_initialize(chunk.mesh) != FT_ERR_SUCCESS)
	{
		(void)chunk.chunk.destroy();
		return (FT_ERR_NO_MEMORY);
	}
	phase_start = std::chrono::steady_clock::now();
	#if defined(DEBUG)
	if (source_snapshot != nullptr)
		std::fprintf(stderr,
			"[WorldRevision] worker generate chunk=(%d,%d) stages=%u\n",
			chunk_x, chunk_z, stage_mask);
	#endif
	error_code = WorldTerrainChunkStore::load_stream_chunk(terrain_chunk_path,
		chunk_x, chunk_z, chunk.chunk);
	loaded_from_terrain_store = error_code == FT_ERR_SUCCESS;
	if (error_code == FT_ERR_NOT_FOUND)
	{
		error_code = voxel_generate_chunk_with_stage_mask(chunk.chunk,
			chunk.world_x, chunk.world_z, seed, config, stage_mask);
		if (error_code == FT_ERR_SUCCESS && !feature_plans.empty())
			error_code = WorldChunkGenerationWorker::apply_feature_plans(chunk,
				feature_plans);
	}
	if (generation_duration_nanoseconds != nullptr)
		*generation_duration_nanoseconds = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - phase_start).count());
	phase_start = std::chrono::steady_clock::now();
	if (error_code == FT_ERR_SUCCESS)
	{
		error_code = voxel_light_build_chunk_local(chunk.light, chunk.world_x,
			chunk.world_z, lookup_local_light_block, &chunk.chunk);
		if (error_code == FT_ERR_SUCCESS)
			error_code = chunk_mesh_generate_from_chunk_with_light(chunk.mesh,
				chunk.chunk, chunk.light);
	}
	if (mesh_duration_nanoseconds != nullptr)
		*mesh_duration_nanoseconds = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - phase_start).count());
	if (error_code != FT_ERR_SUCCESS)
	{
		(void)chunk_mesh_destroy(chunk.mesh);
		(void)chunk.chunk.destroy();
		return (error_code);
	}
	chunk.light_version = chunk.content_version;
	chunk.computed_light_input_version = chunk.light_input_version;
	/* The worker has already built both the chunk-local light field and the
	 * mesh from that field.  This is a valid baseline for rendering.  A later
	 * neighbour-aware remesh may refine border values, but it must not make the
	 * renderer treat this complete local result as an all-zero/black light
	 * buffer while it waits for that refinement. */
	chunk.light_ready_for_render = true;
	#if defined(DEBUG)
	if (source_snapshot != nullptr)
		std::fprintf(stderr,
			"[WorldRevision] worker generate complete chunk=(%d,%d)\n",
			chunk_x, chunk_z);
	#endif
	chunk.initialized = true;
	(void)deferred_edits;
	return (FT_ERR_SUCCESS);
}

std::unique_ptr<WorldGenerationPipeline::Result> WorldChunkGenerationWorker::process_generation(WorldGenerationPipeline::Request &request) noexcept
{
	std::unique_ptr<WorldGenerationPipeline::Result> result(new (std::nothrow) WorldGenerationPipeline::Result());

	if (result == nullptr)
		return (nullptr);
	result->request_id = request.request_id;
	result->world_epoch = request.world_epoch;
	result->relevance_epoch = request.relevance_epoch;
	result->generation_revision = request.generation_revision;
	result->configuration_signature = request.configuration_signature;
	result->stage_mask = request.stage_mask;
	result->voxel_revision = 0U;
	result->light_revision = 0U;
	result->content_version = 1U;
	result->light_input_version = 1U;
	result->chunk_x = request.chunk_x;
	result->chunk_z = request.chunk_z;
	result->operation = request.operation;
	result->error_code = FT_ERR_SUCCESS;
	result->generation_duration_nanoseconds = 0U;
	result->mesh_duration_nanoseconds = 0U;
	result->light_scanned_cells = 0U;
	result->light_propagated_cells = 0U;
	result->light_queue_peak = 0U;
	result->chunk.reset(new (std::nothrow) WorldChunk());
	if (result->chunk == nullptr)
	{
		result->error_code = FT_ERR_NO_MEMORY;
		return (result);
	}
	result->error_code = WorldChunkGenerationWorker::initialize_chunk_for_generation(*result->chunk,
			request.chunk_x, request.chunk_z, request.seed.c_str(),
			request.config, request.stage_mask, request.deferred_edits,
			request.feature_plans,
			request.snapshot.get(), request.terrain_chunk_path,
			result->loaded_from_terrain_store, &result->generation_duration_nanoseconds,
			&result->mesh_duration_nanoseconds);
	if (result->error_code != FT_ERR_SUCCESS)
	{
		result->chunk.reset();
		return (result);
	}
	if (!result->loaded_from_terrain_store)
	{
		try
		{
			result->applied_feature_plans = request.feature_plans;
		}
		catch (...)
		{
			result->chunk.reset();
			result->error_code = FT_ERR_NO_MEMORY;
			return (result);
		}
		result = WorldChunkGenerationWorker::finish_generation_result(
			std::move(result), request);
	}
	if (result->error_code != FT_ERR_SUCCESS
		|| request.operation != WorldGenerationPipeline::WorldGenerationOperation::STREAM)
		return (result);

	WorldChunk &chunk = *result->chunk;
	chunk.voxel_revision = result->loaded_from_terrain_store
		? std::max<uint64_t>(1U, chunk.chunk.get_revision()) : 1U;
	chunk.light_revision = 1U;
	result->error_code = chunk.publish_read_state();
	if (result->error_code == FT_ERR_SUCCESS && !result->loaded_from_terrain_store
		&& !request.terrain_chunk_path.empty())
	{
		result->error_code = result->staged_terrain_chunk.prepare(
			request.terrain_chunk_path, request.chunk_x, request.chunk_z, chunk.chunk);
	}
	return (result);
}

std::unique_ptr<WorldGenerationPipeline::Result> WorldChunkGenerationWorker::finish_generation_result(std::unique_ptr<WorldGenerationPipeline::Result> result,
	WorldGenerationPipeline::Request &request) noexcept
{
	game_voxel_generation_metadata metadata;
	int32_t error_code;

	metadata = result->chunk->chunk.get_generation_metadata();
	#if defined(DEBUG)
	std::fprintf(stderr,
		"[WorldRevision] worker finalize request=%llu chunk=(%d,%d)\n",
		static_cast<unsigned long long>(request.request_id), request.chunk_x,
		request.chunk_z);
	#endif
	metadata.configuration_signature = request.configuration_signature;
	error_code = result->chunk->chunk.set_generation_metadata(metadata);
	if (error_code != FT_ERR_SUCCESS)
	{
		result->error_code = error_code;
		result->chunk.reset();
		return (result);
	}
	result->deferred_edits = std::move(request.deferred_edits);
	for (WorldGenerationPipeline::WorldDeferredBlockEdit &edit : result->deferred_edits)
		edit.request_id = request.request_id;
	return (result);
}

std::unique_ptr<WorldGenerationPipeline::Result> WorldChunkGenerationWorker::process_remesh(WorldGenerationPipeline::Request &request) noexcept
{
	std::unique_ptr<WorldGenerationPipeline::Result> result(new (std::nothrow) WorldGenerationPipeline::Result());
	voxel_light_build_stats light_stats;
	voxel_light_update_config light_config;
	ft_bool light_complete;
	int32_t error_code;
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	std::chrono::steady_clock::time_point light_step_start;
	std::chrono::steady_clock::time_point mesh_start;
#endif

	if (result == nullptr)
		return (nullptr);
	result->request_id = request.request_id;
	result->world_epoch = request.world_epoch;
	result->relevance_epoch = request.relevance_epoch;
	result->generation_revision = request.generation_revision;
	result->configuration_signature = 0U;
	result->stage_mask = 0U;
	result->voxel_revision = request.voxel_revision;
	result->light_revision = request.light_revision;
	result->content_version = request.content_version;
	result->light_input_version = request.light_input_version;
	result->chunk_x = request.chunk_x;
	result->chunk_z = request.chunk_z;
	result->operation = request.operation;
	result->error_code = FT_ERR_SUCCESS;
	result->generation_duration_nanoseconds = 0U;
	result->mesh_duration_nanoseconds = 0U;
	result->light_scanned_cells = 0U;
	result->light_propagated_cells = 0U;
	result->light_queue_peak = 0U;
	result->incremental_light = FT_FALSE;
	result->incremental_light_deltas_complete = FT_FALSE;
	result->interactive_remesh = request.remesh_interactive;
	result->lighting_halo_valid = request.snapshot != nullptr
		&& request.snapshot->lighting_halo_valid != FT_FALSE
		? FT_TRUE : FT_FALSE;
	result->lighting_cardinal_halo_valid = request.snapshot != nullptr
		&& request.snapshot->lighting_cardinal_halo_valid != FT_FALSE
		? FT_TRUE : FT_FALSE;
	result->lighting_halo_blocked = request.snapshot != nullptr
		&& request.snapshot->lighting_halo_blocked != FT_FALSE
		? FT_TRUE : FT_FALSE;
	result->dependency_valid = request.snapshot != nullptr
		&& request.snapshot->dependency_valid != FT_FALSE
		? FT_TRUE : FT_FALSE;
	result->dependency_chunk_x = request.snapshot != nullptr
		? request.snapshot->dependency_chunk_x : 0;
	result->dependency_chunk_z = request.snapshot != nullptr
		? request.snapshot->dependency_chunk_z : 0;
	result->dependency_voxel_revision = request.snapshot != nullptr
		? request.snapshot->dependency_voxel_revision : 0U;
	result->dependency_light_revision = request.snapshot != nullptr
		? request.snapshot->dependency_light_revision : 0U;
	result->dependency_content_version = request.snapshot != nullptr
		? request.snapshot->dependency_content_version : 0U;
	result->dependency_light_input_version = request.snapshot != nullptr
		? request.snapshot->dependency_light_input_version : 0U;
	if (request.snapshot == nullptr)
	{
		result->error_code = FT_ERR_NO_MEMORY;
		return (result);
	}
	if (request.snapshot->existing_light_valid == FT_FALSE)
	{
	#if defined(DEBUG)
		if (request.incremental_additive_light != FT_FALSE
			|| request.incremental_removal_light != FT_FALSE)
			std::fprintf(stderr,
				"[WorldGen] incremental request downgraded to full light "
				"request=%llu chunk=(%d,%d)\n",
				static_cast<unsigned long long>(request.request_id),
				request.chunk_x, request.chunk_z);
	#endif
		request.incremental_additive_light = FT_FALSE;
		request.incremental_removal_light = FT_FALSE;
		request.incremental_light_seeds.clear();
		request.remesh_geometry_only = FT_FALSE;
	}
	/* Record the path actually selected after validity fallback.  This keeps
	 * diagnostics and completion accounting honest when an edit has to use a
	 * full rebuild because no previous light buffer exists. */
	if (request.incremental_additive_light != FT_FALSE
		|| request.incremental_removal_light != FT_FALSE)
		result->incremental_light = FT_TRUE;
	if (request.remesh_target == nullptr)
	{
		request.remesh_target.reset(new (std::nothrow) game_voxel_chunk());
		if (request.remesh_target == nullptr)
		{
			result->error_code = FT_ERR_NO_MEMORY;
			return (result);
		}
		error_code = WorldChunkSnapshotReader::initialize_snapshot_chunk(
			*request.remesh_target, *request.snapshot);
		if (error_code != FT_ERR_SUCCESS)
		{
			result->error_code = error_code;
			return (result);
		}
	}
	/* Give a bounded relight one slice to finish before building a deletion
	 * preview. A quick solve can publish a single lit mesh. A slow solve still
	 * exposes the edit early while the remaining lighting work yields. */
	const bool geometry_final = request.remesh_geometry_only != FT_FALSE;
	const bool reuse_light_after_reversal = request.incremental_light_seeds.size() == 1U
		&& request.incremental_light_seeds.front().additive == FT_FALSE
		&& request.incremental_light_seeds.front().removal == FT_FALSE
		&& request.incremental_light_seeds.front().old_block_id
			== request.incremental_light_seeds.front().new_block_id;
	const bool needs_early_mesh = request.incremental_light_seeds.empty()
		|| std::any_of(request.incremental_light_seeds.begin(),
			request.incremental_light_seeds.end(),
			[](const WorldGenerationPipeline::IncrementalLightSeed &seed)
			{
				return (seed.external_source == FT_FALSE
					&& seed.old_block_id != GAME_VOXEL_AIR_BLOCK
					&& seed.new_block_id == GAME_VOXEL_AIR_BLOCK);
			});
	const bool geometry_preview = needs_early_mesh
		&& request.remesh_geometry_published == FT_FALSE
		&& request.snapshot->existing_light_valid != FT_FALSE
		&& request.remesh_interactive != FT_FALSE;
	const auto build_cached_mesh = [&]() noexcept
	{
		voxel_light_chunk snapshot_light;
		result->mesh.reset(new (std::nothrow) chunk_mesh());
		if (result->mesh == nullptr)
		{
			result->error_code = FT_ERR_NO_MEMORY;
			request.remesh_in_progress = FT_FALSE;
			return ;
		}
		error_code = chunk_mesh_initialize(*result->mesh);
		if (error_code == FT_ERR_SUCCESS)
			error_code = build_preview_mesh_light(*request.snapshot,
				snapshot_light);
		if (error_code == FT_ERR_SUCCESS)
			error_code = chunk_mesh_generate_from_chunk_with_neighbors_and_light_lookup(
				*result->mesh, *request.remesh_target, request.chunk_x,
				request.chunk_z, &WorldChunkSnapshotReader::lookup_snapshot_mesh_block,
				request.snapshot.get(), &snapshot_light,
				&WorldChunkSnapshotReader::lookup_snapshot_light,
				request.snapshot.get());
		if (error_code != FT_ERR_SUCCESS)
		{
			result->error_code = error_code;
			request.remesh_in_progress = FT_FALSE;
			return ;
		}
		if (reuse_light_after_reversal)
		{
			/* The published field is complete for the new voxel revision.
			 * Commit an empty patch with the final mesh and advance its versions. */
			result->incremental_light = FT_TRUE;
			result->incremental_light_deltas_complete = FT_TRUE;
			request.remesh_in_progress = FT_FALSE;
			return ;
		}
		result->stage_mask = WorldGenerationPipeline::Result::STAGE_GEOMETRY_ONLY;
		if (geometry_final)
			result->stage_mask |= WorldGenerationPipeline::Result::STAGE_GEOMETRY_FINAL;
		else
			request.remesh_geometry_published = FT_TRUE;
		request.remesh_in_progress = geometry_final ? FT_FALSE : FT_TRUE;
		return ;
	};
	if (geometry_final || reuse_light_after_reversal
		|| (geometry_preview && request.incremental_light_seeds.empty()))
	{
		build_cached_mesh();
		return (result);
	}
	if (remesh_request_is_cancelled(request))
	{
		result->error_code = FT_ERR_INVALID_STATE;
		request.remesh_in_progress = FT_FALSE;
		return (result);
	}
	if (!request.incremental_light_seeds.empty())
	{
		/*
		 * Mixed edit seeds are still an incremental operation.  Removal must run
		 * first so it can subtract obsolete contributions; additive propagation
		 * then restores surviving/new sources from the same mutable worker-owned
		 * light buffer.  The seed order is made stable and in-place so no second
		 * allocation is needed at the worker boundary.
		 */
		std::size_t removal_insert = 0U;
		std::size_t seed_index = 0U;
		while (seed_index < request.incremental_light_seeds.size())
		{
			if (request.incremental_light_seeds[seed_index].removal
				!= FT_FALSE)
			{
				std::swap(request.incremental_light_seeds[removal_insert],
					request.incremental_light_seeds[seed_index]);
				removal_insert += 1U;
			}
			seed_index += 1U;
		}
		ft_bool seed_complete = FT_FALSE;
		uint64_t processed_in_slice = 0U;
		const auto &config = request.light_update_config;
		if (voxel_light_update_config_is_valid(config) == FT_FALSE)
		{
			result->error_code = FT_ERR_INVALID_ARGUMENT;
			request.remesh_in_progress = FT_FALSE;
			return (result);
		}
		const IncrementalLightSlice slice = {config,
			std::chrono::steady_clock::now()
				+ std::chrono::microseconds(config.time_budget_microseconds)};
		if (request.remesh_light == nullptr)
		{
			request.remesh_light.reset(new (std::nothrow)
				voxel_light_chunk());
			if (request.remesh_light == nullptr)
			{
				result->error_code = FT_ERR_NO_MEMORY;
				return (result);
			}
		}
		while (request.incremental_seed_index
			< request.incremental_light_seeds.size() && slice.permits(processed_in_slice))
		{
			const WorldGenerationPipeline::IncrementalLightSeed &seed =
				request.incremental_light_seeds[
					request.incremental_seed_index];
			uint64_t seed_processed = 0U;
			#if defined(LIBFT_ENABLE_ANALYTICS)
			const uint64_t processed_before =
				request.incremental_processed_cells;
			#endif
			ft_bool preserve_existing =
				(request.incremental_seed_index == 0U
					&& request.incremental_removal_state_initialized == FT_FALSE
					&& request.incremental_additive_state_initialized == FT_FALSE)
				? FT_FALSE : FT_TRUE;

			if (seed.removal == FT_FALSE)
				error_code = build_incremental_additive_light(
					*request.remesh_light, *request.snapshot,
					request.incremental_light_scratch, request.chunk_x,
					request.chunk_z, seed.local_x, seed.local_y, seed.local_z,
					&seed_processed, preserve_existing,
					request.cancellation_token,
					request.cancellation_token_value,
					request.incremental_additive_queue,
					&request.incremental_additive_queue_index,
					&request.incremental_additive_state_initialized,
					slice, processed_in_slice, &seed_complete);
			else
				error_code = build_incremental_removal_light(
					*request.remesh_light, *request.snapshot,
					request.incremental_light_scratch, request.chunk_x,
					request.chunk_z, seed.local_x, seed.local_y, seed.local_z,
					seed.old_block_id, seed.new_block_id, &seed_processed,
					preserve_existing, seed.external_source,
					seed.old_source_light, request.cancellation_token,
					request.cancellation_token_value,
					request.incremental_removal_queue,
					request.incremental_removal_queued_cells,
					&request.incremental_removal_queue_index,
					request.incremental_removal_addition_queue,
					&request.incremental_removal_addition_queue_index,
					&request.incremental_removal_state_initialized,
					&request.incremental_removal_addition_started,
					slice, processed_in_slice, &seed_complete);
			request.incremental_processed_cells += seed_processed;
		#if defined(LIBFT_ENABLE_ANALYTICS)
			if (seed.removal != FT_FALSE
				&& request.incremental_processed_cells / 65536U
					> processed_before / 65536U)
			{
				std::fprintf(stderr,
					"[WorldLight] removal progress request=%llu chunk=(%d,%d) "
					"processed=%llu slice=%llu remove=%zu/%zu add=%zu/%zu\n",
					static_cast<unsigned long long>(request.request_id),
					request.chunk_x, request.chunk_z,
					static_cast<unsigned long long>(
						request.incremental_processed_cells),
					static_cast<unsigned long long>(seed_processed),
					request.incremental_removal_queue_index,
					request.incremental_removal_queue.size(),
					request.incremental_removal_addition_queue_index,
					request.incremental_removal_addition_queue.size());
			}
		#endif
			if (error_code != FT_ERR_SUCCESS)
			{
				result->error_code = error_code;
				request.remesh_in_progress = FT_FALSE;
				return (result);
			}
			if (seed_complete == FT_FALSE)
				break ;
			request.incremental_seed_index += 1U;
			processed_in_slice += seed_processed;
		}
		if (request.incremental_seed_index < request.incremental_light_seeds.size())
		{
			if (geometry_preview)
			{
				build_cached_mesh();
				return (result);
			}
			request.remesh_in_progress = FT_TRUE;
			return (nullptr);
		}
		result->light_scanned_cells = request.incremental_processed_cells;
		result->light = std::move(request.remesh_light);
		error_code = collect_incremental_light_deltas(*request.snapshot,
			*result->light, request.incremental_light_scratch.modified_sections,
			result->incremental_light_deltas,
			&result->incremental_light_deltas_complete);
		if (error_code != FT_ERR_SUCCESS)
		{
			result->error_code = error_code;
			return (result);
		}
		result->mesh.reset(new (std::nothrow) chunk_mesh());
		if (result->mesh == nullptr
			|| chunk_mesh_initialize(*result->mesh) != FT_ERR_SUCCESS)
		{
			result->error_code = FT_ERR_NO_MEMORY;
			return (result);
		}
		result->error_code =
			chunk_mesh_generate_from_chunk_with_neighbors_and_light_lookup(
				*result->mesh, *request.remesh_target, request.chunk_x,
				request.chunk_z, &WorldChunkSnapshotReader::lookup_snapshot_mesh_block,
				request.snapshot.get(), result->light.get(),
				&WorldChunkSnapshotReader::lookup_snapshot_light,
				request.snapshot.get());
		request.remesh_target.reset();
		request.remesh_in_progress = FT_FALSE;
		if (result->error_code != FT_ERR_SUCCESS)
			result->mesh.reset();
		else if (result->incremental_light_deltas_complete != FT_FALSE)
		{
			describe_light_boundary_changes(*request.snapshot, *result);
			if (!result->incremental_light_deltas.empty())
			{
				try
				{
					auto samples = std::make_shared<std::vector<uint8_t>>(
						std::move(request.snapshot->existing_light));
					for (const auto &delta : result->incremental_light_deltas)
						(*samples)[WorldChunkReadState::light_offset(delta.local_x(),
							delta.local_y(), delta.local_z())] = delta.packed_light();
					result->incremental_light_samples = std::move(samples);
				}
				catch (const std::bad_alloc &)
				{
					result->error_code = FT_ERR_NO_MEMORY;
				}
			}
			result->light.reset();
		}
		return (result);
	}
	if (request.remesh_light_operation == nullptr)
	{
		request.remesh_light.reset(new (std::nothrow) voxel_light_chunk());
		request.remesh_light_operation.reset(
			new (std::nothrow) voxel_light_build_operation());
		if (request.remesh_light == nullptr || request.remesh_light_operation == nullptr)
		{
			result->error_code = FT_ERR_NO_MEMORY;
			return (result);
		}
		error_code = request.remesh_light_operation->initialize(
			*request.remesh_light,
			request.chunk_x * GAME_VOXEL_CHUNK_WIDTH,
			request.chunk_z * GAME_VOXEL_CHUNK_DEPTH,
			&WorldChunkSnapshotReader::lookup_snapshot_block,
			request.snapshot.get(), FT_TRUE);
		if (error_code != FT_ERR_SUCCESS)
		{
			result->error_code = error_code;
			return (result);
		}
	}
	light_config = request.light_update_config;
	if (voxel_light_update_config_is_valid(light_config) == FT_FALSE)
	{
		result->error_code = FT_ERR_INVALID_ARGUMENT;
		return (result);
	}
	light_complete = FT_FALSE;
	#if defined(DEBUG)
	light_step_start = std::chrono::steady_clock::now();
	#endif
	error_code = request.remesh_light_operation->step(light_config,
			&light_stats, &light_complete);
	#if defined(DEBUG)
	{
		const uint64_t light_step_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - light_step_start).count());
		if (light_step_us >= 100000U)
			std::fprintf(stderr,
				"[WorldGen] slow remesh light step request=%llu chunk=(%d,%d) "
				"duration_us=%llu scanned=%llu propagated=%llu complete=%d\n",
				static_cast<unsigned long long>(request.request_id),
				request.chunk_x, request.chunk_z,
				static_cast<unsigned long long>(light_step_us),
				static_cast<unsigned long long>(light_stats.scanned_cells),
				static_cast<unsigned long long>(light_stats.propagated_cells),
				light_complete != FT_FALSE ? 1 : 0);
	}
	#endif
	result->light_scanned_cells = light_stats.scanned_cells;
	result->light_propagated_cells = light_stats.propagated_cells;
	result->light_queue_peak = light_stats.queue_peak;
	if (remesh_request_is_cancelled(request))
	{
		result->error_code = FT_ERR_INVALID_STATE;
		request.remesh_in_progress = FT_FALSE;
		return (result);
	}
	if (error_code != FT_ERR_SUCCESS)
	{
		result->error_code = error_code;
		return (result);
	}
	if (light_complete == FT_FALSE)
	{
		request.remesh_in_progress = FT_TRUE;
	#if defined(DEBUG)
		if (request.request_id % 32U == 0U && light_stats.scanned_cells % 65536U == 0U)
			std::fprintf(stderr,
				"[WorldGen] remesh lighting slice chunk=(%d,%d) scanned=%llu "
				"propagated=%llu queue_peak=%llu\n", request.chunk_x,
				request.chunk_z,
				static_cast<unsigned long long>(light_stats.scanned_cells),
				static_cast<unsigned long long>(light_stats.propagated_cells),
				static_cast<unsigned long long>(light_stats.queue_peak));
	#endif
		return (nullptr);
	}
	result->mesh.reset(new (std::nothrow) chunk_mesh());
	if (result->mesh == nullptr)
	{
		result->error_code = FT_ERR_NO_MEMORY;
		return (result);
	}
	if (chunk_mesh_initialize(*result->mesh) != FT_ERR_SUCCESS)
	{
		result->error_code = FT_ERR_NO_MEMORY;
		return (result);
	}
	result->light = std::move(request.remesh_light);
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	mesh_start = std::chrono::steady_clock::now();
	#endif
	result->error_code =
		chunk_mesh_generate_from_chunk_with_neighbors_and_light_lookup(
		*result->mesh, *request.remesh_target, request.chunk_x, request.chunk_z,
		&WorldChunkSnapshotReader::lookup_snapshot_mesh_block, request.snapshot.get(),
		result->light.get(), &voxel_light_build_operation_lookup,
		request.remesh_light_operation.get());
	#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	{
		const uint64_t mesh_us = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - mesh_start).count());
		if (mesh_us >= 100000U)
			std::fprintf(stderr,
				"[WorldGen] slow remesh mesh request=%llu chunk=(%d,%d) "
				"duration_us=%llu error=%d\n",
				static_cast<unsigned long long>(request.request_id),
				request.chunk_x, request.chunk_z,
				static_cast<unsigned long long>(mesh_us), result->error_code);
	}
	#endif
	request.remesh_light_operation.reset();
	request.remesh_target.reset();
	request.remesh_in_progress = FT_FALSE;
	if (result->error_code != FT_ERR_SUCCESS)
		result->mesh.reset();
	else
		describe_light_boundary_changes(*request.snapshot, *result);
	return (result);
}
