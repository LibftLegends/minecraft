#include "../../src/chunks/ChunkNeighborMesher.hpp"

ChunkNeighborMesher::ChunkNeighborMesher()
{
}

ChunkNeighborMesher::ChunkNeighborMesher(const ChunkNeighborMesher &other)
{
	(void)other;
}

ChunkNeighborMesher::~ChunkNeighborMesher()
{
}

ChunkNeighborMesher &ChunkNeighborMesher::operator=(const ChunkNeighborMesher &other)
{
	(void)other;
	return (*this);
}

ChunkNeighborMesher::NeighborContext ChunkNeighborMesher::build_context(int32_t chunk_x,
	int32_t chunk_z, WorldChunk *chunks, int32_t chunk_count)
{
	NeighborContext	ctx;

	ctx.west = WorldChunkStore::find_chunk(chunks, chunk_count, chunk_x - 1,
			chunk_z);
	ctx.east = WorldChunkStore::find_chunk(chunks, chunk_count, chunk_x + 1,
			chunk_z);
	ctx.north = WorldChunkStore::find_chunk(chunks, chunk_count, chunk_x,
			chunk_z - 1);
	ctx.south = WorldChunkStore::find_chunk(chunks, chunk_count, chunk_x,
			chunk_z + 1);
	ctx.world_origin_x = chunk_x * GAME_VOXEL_CHUNK_WIDTH;
	ctx.world_origin_z = chunk_z * GAME_VOXEL_CHUNK_DEPTH;
	return (ctx);
}

void ChunkNeighborMesher::resolve_neighbor_read(const NeighborContext &ctx,
	int32_t local_x, int32_t local_z, const WorldChunk **out_chunk,
	int32_t *out_read_x, int32_t *out_read_z)
{
	*out_chunk = nullptr;
	*out_read_x = local_x;
	*out_read_z = local_z;
	if (local_x == -1 && local_z >= 0 && local_z < GAME_VOXEL_CHUNK_DEPTH)
	{
		*out_chunk = ctx.west;
		*out_read_x = GAME_VOXEL_CHUNK_WIDTH - 1;
	}
	else if (local_x == GAME_VOXEL_CHUNK_WIDTH && local_z >= 0
		&& local_z < GAME_VOXEL_CHUNK_DEPTH)
	{
		*out_chunk = ctx.east;
		*out_read_x = 0;
	}
	else if (local_z == -1 && local_x >= 0 && local_x < GAME_VOXEL_CHUNK_WIDTH)
	{
		*out_chunk = ctx.north;
		*out_read_z = GAME_VOXEL_CHUNK_DEPTH - 1;
	}
	else if (local_z == GAME_VOXEL_CHUNK_DEPTH && local_x >= 0
		&& local_x < GAME_VOXEL_CHUNK_WIDTH)
	{
		*out_chunk = ctx.south;
		*out_read_z = 0;
	}
}

int32_t ChunkNeighborMesher::lookup_block(void *user_data, int32_t world_x,
	int32_t world_y, int32_t world_z, uint32_t *block_id)
{
	NeighborContext		*ctx;
	const WorldChunk	*wc;
	int32_t				local_x;
	int32_t				local_z;
	int32_t				read_x;
	int32_t				read_z;

	if (!user_data || !block_id)
		return (FT_ERR_INVALID_ARGUMENT);
	ctx = static_cast<NeighborContext *>(user_data);
	if (world_y < 0 || world_y >= GAME_VOXEL_CHUNK_HEIGHT)
	{
		*block_id = GAME_VOXEL_AIR_BLOCK;
		return (FT_ERR_SUCCESS);
	}
	local_x = world_x - ctx->world_origin_x;
	local_z = world_z - ctx->world_origin_z;
	resolve_neighbor_read(*ctx, local_x, local_z, &wc, &read_x, &read_z);
	if (!wc || !wc->initialized)
	{
		*block_id = GAME_VOXEL_AIR_BLOCK;
		return (FT_ERR_SUCCESS);
	}
	return (wc->chunk.read_block(read_x, world_y, read_z, block_id));
}

int32_t ChunkNeighborMesher::generate_with_neighbors(chunk_mesh &mesh,
	const game_voxel_chunk &chunk, int32_t chunk_x, int32_t chunk_z,
	WorldChunk *chunks, int32_t chunk_count)
{
	NeighborContext	ctx;

	ctx = build_context(chunk_x, chunk_z, chunks, chunk_count);
	return (chunk_mesh_generate_from_chunk_with_neighbors(mesh, chunk, chunk_x,
			chunk_z, &lookup_block, &ctx));
}
