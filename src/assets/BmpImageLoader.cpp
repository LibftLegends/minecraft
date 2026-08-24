#include "../../src/assets/BmpImageLoader.hpp"

BmpImageLoader::BmpImageLoader()
{
}

BmpImageLoader::BmpImageLoader(const BmpImageLoader &other)
{
	(void)other;
}

BmpImageLoader::~BmpImageLoader()
{
}

BmpImageLoader &BmpImageLoader::operator=(const BmpImageLoader &other)
{
	(void)other;
	return (*this);
}

uint16_t BmpImageLoader::read_le_u16(const uint8_t *data)
{
	return (static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8U));
}

uint32_t BmpImageLoader::read_le_u32(const uint8_t *data)
{
	return (static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8U) | (static_cast<uint32_t>(data[2]) << 16U) | (static_cast<uint32_t>(data[3]) << 24U));
}

int32_t BmpImageLoader::read_le_i32(const uint8_t *data)
{
	return (static_cast<int32_t>(read_le_u32(data)));
}

bool BmpImageLoader::read_bmp_header(FILE *file, BmpHeader &hdr)
{
	uint8_t	header[54];

	if (std::fread(header, 1U, sizeof(header), file) != sizeof(header))
		return (false);
	if (header[0] != 'B' || header[1] != 'M')
		return (false);
	hdr.data_offset = read_le_u32(header + 10);
	hdr.width = read_le_i32(header + 18);
	hdr.height_signed = read_le_i32(header + 22);
	hdr.bits_per_pixel = read_le_u16(header + 28);
	hdr.compression = read_le_u32(header + 30);
	hdr.height = hdr.height_signed < 0 ? -hdr.height_signed : hdr.height_signed;
	if (hdr.width <= 0 || hdr.height_signed == 0 || hdr.bits_per_pixel != 24U
		|| hdr.compression != 0U)
		return (false);
	return (true);
}

bool BmpImageLoader::read_bmp_pixels(FILE *file, std::vector<uint32_t> &pixels,
	const BmpHeader &hdr)
{
	size_t	row_stride;
	size_t	src_y;
	long	row_off;
	uint8_t	px[3];

	row_stride = ((static_cast<size_t>(hdr.width) * 3U)
			+ 3U) & ~static_cast<size_t>(3U);
	pixels.assign(static_cast<size_t>(hdr.width)
		* static_cast<size_t>(hdr.height), 0U);
	for (size_t y = 0; y < static_cast<size_t>(hdr.height); ++y)
	{
		src_y = hdr.height_signed < 0 ? y : static_cast<size_t>(hdr.height - 1)
			- y;
		row_off = static_cast<long>(hdr.data_offset + src_y * row_stride);
		if (std::fseek(file, row_off, SEEK_SET) != 0)
			return (false);
		for (size_t x = 0; x < static_cast<size_t>(hdr.width); ++x)
		{
			if (std::fread(px, 1U, sizeof(px), file) != sizeof(px))
				return (false);
			pixels[y * static_cast<size_t>(hdr.width)
				+ x] = RendererColor::rgba(px[2], px[1], px[0]);
		}
	}
	return (true);
}

bool BmpImageLoader::load(const char *path, std::vector<uint32_t> &pixels,
	int32_t &width, int32_t &height)
{
	FILE		*file;
	BmpHeader	hdr;

	if (!path)
		return (false);
	file = std::fopen(path, "rb");
	if (!file)
		return (false);
	if (!read_bmp_header(file, hdr) || !read_bmp_pixels(file, pixels, hdr))
	{
		std::fclose(file);
		return (false);
	}
	std::fclose(file);
	width = hdr.width;
	height = hdr.height;
	return (true);
}
