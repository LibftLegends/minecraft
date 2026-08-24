#ifndef BMP_IMAGE_LOADER_HPP
# define BMP_IMAGE_LOADER_HPP

# include "../../src/frame/RendererColor.hpp"
# include "../ft_vox.hpp"

class BmpImageLoader
{
  public:
	BmpImageLoader();
	BmpImageLoader(const BmpImageLoader &other);
	~BmpImageLoader();
	BmpImageLoader &operator=(const BmpImageLoader &other);

	static bool load(const char *path, std::vector<uint32_t> &pixels,
		int32_t &width, int32_t &height);

  private:
	struct			BmpHeader
	{
		uint32_t	data_offset;
		int32_t		width;
		int32_t		height;
		int32_t		height_signed;
		uint16_t	bits_per_pixel;
		uint32_t	compression;
	};

	static uint16_t read_le_u16(const uint8_t *data);
	static uint32_t read_le_u32(const uint8_t *data);
	static int32_t read_le_i32(const uint8_t *data);
	static bool read_bmp_header(FILE *file, BmpHeader &hdr);
	static bool read_bmp_pixels(FILE *file, std::vector<uint32_t> &pixels,
		const BmpHeader &hdr);
};

#endif
