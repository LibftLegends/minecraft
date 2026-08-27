#include "../../src/gpur/GpuTextureAtlas.hpp"

// clang-format off
const uint32_t GpuTextureAtlas::BLOCK_IDS[63] = {
	TERRAIN_GENERATOR_GRASS_BLOCK, TERRAIN_GENERATOR_DIRT_BLOCK,
	TERRAIN_GENERATOR_STONE_BLOCK, TERRAIN_GENERATOR_SHRUB_BLOCK,
	TERRAIN_GENERATOR_OAK_LOG_BLOCK, TERRAIN_GENERATOR_OAK_LEAVES_BLOCK,
	TERRAIN_GENERATOR_CACTUS_BLOCK, TERRAIN_GENERATOR_WATER_BLOCK,
	TERRAIN_GENERATOR_BEDROCK_BLOCK, TERRAIN_GENERATOR_SAND_BLOCK,
	TERRAIN_GENERATOR_SNOW_BLOCK, TERRAIN_GENERATOR_PERMAFROST_BLOCK,
	TERRAIN_GENERATOR_CANYON_ROCK_BLOCK, TERRAIN_GENERATOR_SLATE_BLOCK,
	TERRAIN_GENERATOR_MOSS_ROCK_BLOCK, TERRAIN_GENERATOR_COAL_ORE_BLOCK,
	TERRAIN_GENERATOR_IRON_ORE_BLOCK, TERRAIN_GENERATOR_GOLD_ORE_BLOCK,
	TERRAIN_GENERATOR_GRANITE_BLOCK, TERRAIN_GENERATOR_ANDESITE_BLOCK,
	TERRAIN_GENERATOR_DIORITE_BLOCK, TERRAIN_GENERATOR_GRAVEL_BLOCK,
	TERRAIN_GENERATOR_CLAY_BLOCK, TERRAIN_GENERATOR_OBSIDIAN_BLOCK,
	TERRAIN_GENERATOR_MOSSY_STONE_BLOCK, TERRAIN_GENERATOR_CRACKED_STONE_BLOCK,
	TERRAIN_GENERATOR_LIMESTONE_BLOCK, TERRAIN_GENERATOR_BASALT_BLOCK,
	TERRAIN_GENERATOR_DIAMOND_ORE_BLOCK, TERRAIN_GENERATOR_EMERALD_ORE_BLOCK,
	TERRAIN_GENERATOR_COPPER_ORE_BLOCK, TERRAIN_GENERATOR_PINE_LOG_BLOCK,
	TERRAIN_GENERATOR_PINE_LEAVES_BLOCK, TERRAIN_GENERATOR_BIRCH_LOG_BLOCK,
	TERRAIN_GENERATOR_BIRCH_LEAVES_BLOCK, TERRAIN_GENERATOR_ICE_BLOCK,
	TERRAIN_GENERATOR_PACKED_ICE_BLOCK, TERRAIN_GENERATOR_RED_FLOWER_BLOCK,
	TERRAIN_GENERATOR_YELLOW_FLOWER_BLOCK, TERRAIN_GENERATOR_TALL_GRASS_BLOCK,
	TERRAIN_GENERATOR_FERN_BLOCK, TERRAIN_GENERATOR_DEAD_BUSH_BLOCK,
	TERRAIN_GENERATOR_RED_MUSHROOM_BLOCK, TERRAIN_GENERATOR_BROWN_MUSHROOM_BLOCK,
	TERRAIN_GENERATOR_MUSHROOM_STEM_BLOCK, TERRAIN_GENERATOR_LILY_PAD_BLOCK,
	TERRAIN_GENERATOR_SEAGRASS_BLOCK, TERRAIN_GENERATOR_COARSE_DIRT_BLOCK,
	TERRAIN_GENERATOR_PODZOL_BLOCK, TERRAIN_GENERATOR_MUD_BLOCK,
	TERRAIN_GENERATOR_FROZEN_STONE_BLOCK, TERRAIN_GENERATOR_CHALK_BLOCK,
	TERRAIN_GENERATOR_RED_SAND_BLOCK, TERRAIN_GENERATOR_TERRACOTTA_BLOCK,
	TERRAIN_GENERATOR_SALT_BLOCK, TERRAIN_GENERATOR_VOLCANIC_ROCK_BLOCK,
	TERRAIN_GENERATOR_QUARTZ_BLOCK, TERRAIN_GENERATOR_AMETHYST_BLOCK,
	TERRAIN_GENERATOR_PACKED_SNOW_BLOCK, TERRAIN_GENERATOR_WET_SAND_BLOCK,
	TERRAIN_GENERATOR_AMBER_BLOCK, TERRAIN_GENERATOR_FROST_CRYSTAL_BLOCK,
	TERRAIN_GENERATOR_SHIMMER_STONE_BLOCK};
// clang-format on
const int GpuTextureAtlas::BLOCK_COUNT = 63;

GpuTextureAtlas::GpuTextureAtlas() : _tex(0), _loaded(false)
{
}

GpuTextureAtlas::GpuTextureAtlas(const GpuTextureAtlas &other)
{
	(void)other;
}

GpuTextureAtlas::~GpuTextureAtlas()
{
	destroy();
}

GpuTextureAtlas &GpuTextureAtlas::operator=(const GpuTextureAtlas &other)
{
	(void)other;
	return (*this);
}

bool GpuTextureAtlas::upload(const TextureAtlas &atlas)
{
	int			w;
	int			h;
	uint32_t	px;

	destroy();
	if (atlas.is_loaded() == false)
		return (false);
	w = atlas.get_width();
	h = atlas.get_height();
	if (w <= 0 || h <= 0)
		return (false);
	std::vector<uint8_t> rgba;
	rgba.reserve(static_cast<size_t>(w * h * 4));
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			px = atlas.pixel_at(x, y);
			rgba.push_back(static_cast<uint8_t>((px >> 16) & 0xFFU));
			rgba.push_back(static_cast<uint8_t>((px >> 8) & 0xFFU));
			rgba.push_back(static_cast<uint8_t>(px & 0xFFU));
			rgba.push_back(0xFFU);
		}
	}
	glGenTextures(1, &_tex);
	glBindTexture(GL_TEXTURE_2D, _tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
		rgba.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);
	_loaded = true;
	return (true);
}

void GpuTextureAtlas::bind(int unit) const
{
	glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
	glBindTexture(GL_TEXTURE_2D, _loaded ? _tex : 0);
}

void GpuTextureAtlas::destroy()
{
	if (_tex != 0)
	{
		glDeleteTextures(1, &_tex);
		_tex = 0;
	}
	_loaded = false;
}

void GpuTextureAtlas::fill_tile_uvs(const TextureAtlas &atlas, float uv_out[384
	* 4]) const
{
	std::memset(uv_out, 0, 384 * 4 * sizeof(float));
	if (atlas.is_loaded() == false)
		return ;

	float atw = static_cast<float>(atlas.get_width());
	float ath = static_cast<float>(atlas.get_height());
	int tw = atlas.get_tile_width();
	int th = atlas.get_tile_height();
	int mx = std::max(2, tw / 32);
	int my = std::max(2, th / 32);

	for (int bi = 0; bi < BLOCK_COUNT; ++bi)
	{
		uint32_t bid = BLOCK_IDS[bi];
		if (bid >= 64U)
			continue ;
		for (int face = 0; face < 6; ++face)
		{
			int tx = 2;
			int ty = 2;
			TextureAtlas::atlas_tile_for_block(bid, static_cast<uint8_t>(face),
				&tx, &ty);
			float u0 = static_cast<float>(tx * tw + mx) / atw;
			float v0 = static_cast<float>(ty * th + my) / ath;
			float us = static_cast<float>(tw - mx * 2) / atw;
			float vs = static_cast<float>(th - my * 2) / ath;
			size_t idx = (static_cast<size_t>(bid) * 6U
					+ static_cast<size_t>(face)) * 4U;
			uv_out[idx + 0] = u0;
			uv_out[idx + 1] = v0;
			uv_out[idx + 2] = us;
			uv_out[idx + 3] = vs;
		}
	}
}

void GpuTextureAtlas::fill_fallback_colors(float colors_out[64 * 3]) const
{
	std::memset(colors_out, 0, 64 * 3 * sizeof(float));
	for (uint32_t bid = 0; bid < 64U; ++bid)
	{
		uint32_t c = RendererColor::block_color(bid,
				static_cast<uint8_t>(CHUNK_MESH_FACE_UP));
		colors_out[bid * 3 + 0] = static_cast<float>((c >> 16) & 0xFFU)
			/ 255.0f;
		colors_out[bid * 3 + 1] = static_cast<float>((c >> 8) & 0xFFU) / 255.0f;
		colors_out[bid * 3 + 2] = static_cast<float>(c & 0xFFU) / 255.0f;
	}
}

bool GpuTextureAtlas::is_loaded() const
{
	return (_loaded);
}
