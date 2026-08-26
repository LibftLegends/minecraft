#include "../../src/assets/TextureAtlas.hpp"

TextureAtlas::TextureAtlas() : pixels(), width(0), height(0), tile_width(0),
	tile_height(0), loaded(false), attempted_load(false)
{
}

TextureAtlas::TextureAtlas(const TextureAtlas &other)
{
	*this = other;
}

TextureAtlas::~TextureAtlas()
{
}

TextureAtlas &TextureAtlas::operator=(const TextureAtlas &other)
{
	if (this != &other)
	{
		this->pixels = other.pixels;
		this->width = other.width;
		this->height = other.height;
		this->tile_width = other.tile_width;
		this->tile_height = other.tile_height;
		this->loaded = other.loaded;
		this->attempted_load = other.attempted_load;
	}
	return (*this);
}

bool TextureAtlas::load_bmp_texture_atlas(TextureAtlas *atlas, const char *path)
{
	if (!atlas || !path)
		return (false);
	if (!BmpImageLoader::load(path, atlas->pixels, atlas->width, atlas->height))
		return (false);
	atlas->tile_width = atlas->width / ATLAS_GRID_COLUMNS;
	atlas->tile_height = atlas->height / ATLAS_GRID_ROWS;
	atlas->loaded = atlas->tile_width > 0 && atlas->tile_height > 0;
	return (atlas->loaded);
}

const TextureAtlas &TextureAtlas::instance()
{
	static TextureAtlas	atlas;

	if (!atlas.attempted_load)
	{
		atlas.attempted_load = true;
		load_bmp_texture_atlas(&atlas, "assets/textures/voxel_atlas.bmp");
	}
	return (atlas);
}

void TextureAtlas::atlas_tile_for_block(uint32_t block_id, uint8_t face,
	int32_t *tile_x, int32_t *tile_y)
{
	int	tx;
	int	ty;

	tx = 2;
	ty = 2;
	BlockTextureRegistry::tile_for_block(block_id, face, &tx, &ty);
	if (tile_x)
		*tile_x = tx;
	if (tile_y)
		*tile_y = ty;
}

double TextureAtlas::fraction(double value)
{
	return (value - std::floor(value));
}

void TextureAtlas::compute_tile_geometry(const TextureAtlas &atlas,
	uint32_t block_id, uint8_t face, TriangleTexture &tex)
{
	int32_t	mx;
	int32_t	my;
	int32_t	tile_x;
	int32_t	tile_y;

	atlas_tile_for_block(block_id, face, &tile_x, &tile_y);
	mx = std::max(2, atlas.get_tile_width() / 32);
	my = std::max(2, atlas.get_tile_height() / 32);
	tex.base_x = tile_x * atlas.get_tile_width() + mx;
	tex.base_y = tile_y * atlas.get_tile_height() + my;
	tex.sample_width = std::max(1, atlas.get_tile_width() - mx * 2);
	tex.sample_height = std::max(1, atlas.get_tile_height() - my * 2);
	tex.loaded = true;
}

TriangleTexture TextureAtlas::prepare_triangle_texture(uint32_t block_id,
	uint8_t face)
{
	static TriangleTexture	cache[TEXTURE_CACHE_BLOCKS][TEXTURE_CACHE_FACES];
	static bool				cache_ready[TEXTURE_CACHE_BLOCKS][TEXTURE_CACHE_FACES];
	TriangleTexture			tex;

	const TextureAtlas &atlas = instance();
	if (block_id < static_cast<uint32_t>(TEXTURE_CACHE_BLOCKS)
		&& face < static_cast<uint8_t>(TEXTURE_CACHE_FACES)
		&& cache_ready[block_id][face])
		return (cache[block_id][face]);
	tex.atlas = &atlas;
	tex.fallback_color = RendererColor::block_color(block_id, face);
	tex.base_x = 0;
	tex.base_y = 0;
	tex.sample_width = 1;
	tex.sample_height = 1;
	tex.shade = RendererColor::face_shade(face);
	tex.loaded = false;
	if (!atlas.is_loaded())
		return (tex);
	compute_tile_geometry(atlas, block_id, face, tex);
	if (block_id < static_cast<uint32_t>(TEXTURE_CACHE_BLOCKS)
		&& face < static_cast<uint8_t>(TEXTURE_CACHE_FACES))
	{
		cache[block_id][face] = tex;
		cache_ready[block_id][face] = true;
	}
	return (tex);
}

uint32_t TextureAtlas::sample_clamped(const TriangleTexture &tex, double u,
	double v)
{
	double	fu;
	double	fv;
	int32_t	px;
	int32_t	py;
	int32_t	aw;
	int32_t	ah;

	fu = u - static_cast<double>(static_cast<int64_t>(u));
	fv = v - static_cast<double>(static_cast<int64_t>(v));
	if (fu < 0.0)
		fu += 1.0;
	if (fv < 0.0)
		fv += 1.0;
	px = tex.base_x + static_cast<int32_t>(fu
			* static_cast<double>(tex.sample_width));
	py = tex.base_y + static_cast<int32_t>(fv
			* static_cast<double>(tex.sample_height));
	aw = tex.atlas->get_width();
	ah = tex.atlas->get_height();
	if (px < 0)
		px = 0;
	else if (px >= aw)
		px = aw - 1;
	if (py < 0)
		py = 0;
	else if (py >= ah)
		py = ah - 1;
	return (tex.atlas->pixel_at(px, py));
}

uint32_t TextureAtlas::sample_triangle_texture(const TriangleTexture &texture,
	double texture_u, double texture_v)
{
	int32_t	px;
	int32_t	py;

	if (!texture.loaded || !texture.atlas)
		return (texture.fallback_color);
	if (texture_u >= 0.0 && texture_u < 1.0 && texture_v >= 0.0
		&& texture_v < 1.0)
	{
		px = texture.base_x + static_cast<int32_t>(texture_u
				* static_cast<double>(texture.sample_width));
		py = texture.base_y + static_cast<int32_t>(texture_v
				* static_cast<double>(texture.sample_height));
		return (texture.atlas->pixel_at(px, py));
	}
	return (sample_clamped(texture, texture_u, texture_v));
}

int32_t TextureAtlas::get_width() const
{
	return (this->width);
}

int32_t TextureAtlas::get_height() const
{
	return (this->height);
}

int32_t TextureAtlas::get_tile_width() const
{
	return (this->tile_width);
}

int32_t TextureAtlas::get_tile_height() const
{
	return (this->tile_height);
}

bool TextureAtlas::is_loaded() const
{
	return (this->loaded);
}

uint32_t TextureAtlas::pixel_at(int32_t x, int32_t y) const
{
	return (this->pixels[static_cast<size_t>(y)
		* static_cast<size_t>(this->width) + static_cast<size_t>(x)]);
}
