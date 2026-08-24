#ifndef TEXTURE_ATLAS_HPP
# define TEXTURE_ATLAS_HPP

# include "../../src/assets/BlockTextureRegistry.hpp"
# include "../../src/assets/BmpImageLoader.hpp"
# include "../../src/frame/RendererColor.hpp"
# include "../../src/geometry/TriangleTexture.hpp"
# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../ft_vox.hpp"

class TextureAtlas
{
  public:
	TextureAtlas();
	TextureAtlas(const TextureAtlas &other);
	~TextureAtlas();
	TextureAtlas &operator=(const TextureAtlas &other);

	static const TextureAtlas &instance();
	static void atlas_tile_for_block(uint32_t block_id, uint8_t face,
		int32_t *tile_x, int32_t *tile_y);
	static TriangleTexture prepare_triangle_texture(uint32_t block_id,
		uint8_t face);
	static uint32_t sample_triangle_texture(const TriangleTexture &texture,
		double texture_u, double texture_v);

	int32_t get_width() const;
	int32_t get_height() const;
	int32_t get_tile_width() const;
	int32_t get_tile_height() const;
	bool is_loaded() const;
	uint32_t pixel_at(int32_t x, int32_t y) const;

  private:
	static const int TEXTURE_CACHE_BLOCKS = 64;
	static const int TEXTURE_CACHE_FACES = 6;
	static const int ATLAS_GRID_COLUMNS = 8;
	static const int ATLAS_GRID_ROWS = 8;

	std::vector<uint32_t> pixels;
	int32_t width;
	int32_t height;
	int32_t tile_width;
	int32_t tile_height;
	bool loaded;
	bool attempted_load;

	static bool load_bmp_texture_atlas(TextureAtlas *atlas, const char *path);
	static double fraction(double value);
	static uint32_t sample_clamped(const TriangleTexture &tex, double u,
		double v);
};

#endif
