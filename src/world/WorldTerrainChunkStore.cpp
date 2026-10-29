#include "../../src/world/WorldTerrainChunkStore.hpp"
#include "../../Libft/Modules/Buffer/byte_buffer.hpp"
#include "../../Libft/Modules/File/file_utils.hpp"
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

namespace
{
	static const uint32_t WORLD_TERRAIN_CHUNK_VERSION = 1U;
	static const std::size_t WORLD_TERRAIN_CHUNK_MAX_BYTES = 16U * 1024U * 1024U;
	static const uint64_t WORLD_TERRAIN_CHECKSUM_OFFSET =
		UINT64_C(14695981039346656037);
	static const uint64_t WORLD_TERRAIN_CHECKSUM_PRIME = UINT64_C(1099511628211);

	static uint64_t world_terrain_checksum(const uint8_t *bytes,
		std::size_t size) noexcept
	{
		uint64_t value = WORLD_TERRAIN_CHECKSUM_OFFSET;
		for (std::size_t index = 0U; index < size; ++index)
		{
			value ^= bytes[index];
			value *= WORLD_TERRAIN_CHECKSUM_PRIME;
		}
		return (value);
	}

	static void append_u32(std::vector<uint8_t> &bytes, uint32_t value)
	{
		for (uint32_t shift = 0U; shift < 32U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	static void append_u64(std::vector<uint8_t> &bytes, uint64_t value)
	{
		for (uint32_t shift = 0U; shift < 64U; shift += 8U)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	static uint32_t read_u32(const uint8_t *bytes) noexcept
	{
		return (static_cast<uint32_t>(bytes[0U])
			| (static_cast<uint32_t>(bytes[1U]) << 8U)
			| (static_cast<uint32_t>(bytes[2U]) << 16U)
			| (static_cast<uint32_t>(bytes[3U]) << 24U));
	}

	static uint64_t read_u64(const uint8_t *bytes) noexcept
	{
		uint64_t value = 0U;
		for (uint32_t index = 0U; index < 8U; ++index)
			value |= static_cast<uint64_t>(bytes[index]) << (index * 8U);
		return (value);
	}

	static int32_t read_file(const char *path,
		std::vector<uint8_t> &bytes_out) noexcept
	{
		ft_string contents;
		ft_size_t file_size = 0U;
		int32_t result = contents.initialize();
		const bool contents_initialized = result == FT_ERR_SUCCESS;
		if (result == FT_ERR_SUCCESS)
			result = file_get_size(path, &file_size);
		if (result == FT_ERR_SUCCESS
			&& file_size > WORLD_TERRAIN_CHUNK_MAX_BYTES)
			result = FT_ERR_OUT_OF_RANGE;
		if (result == FT_ERR_SUCCESS)
			result = file_read_all(path, contents);
		if (result == FT_ERR_SUCCESS)
		{
			try
			{
				bytes_out.assign(
					reinterpret_cast<const uint8_t *>(contents.data()),
					reinterpret_cast<const uint8_t *>(contents.data())
						+ contents.size());
			}
			catch (const std::bad_alloc &)
			{
				result = FT_ERR_NO_MEMORY;
			}
		}
		if (contents_initialized)
		{
			const int32_t destroy_result = contents.destroy();
			if (result == FT_ERR_SUCCESS)
				result = destroy_result;
		}
		return (result);
	}
}

WorldTerrainChunkStore::WorldTerrainChunkStore() noexcept
	: directory_(), initialized_(false)
{
}

WorldTerrainChunkStore::WorldTerrainChunkStore(
	const WorldTerrainChunkStore &other)
	: directory_(), initialized_(false)
{
	(void)other;
}

WorldTerrainChunkStore::~WorldTerrainChunkStore()
{
}

WorldTerrainChunkStore &WorldTerrainChunkStore::operator=(
	const WorldTerrainChunkStore &other)
{
	(void)other;
	return (*this);
}

int32_t WorldTerrainChunkStore::initialize(
	const uint8_t world_id[32]) noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	if (world_id == nullptr || this->initialized_)
		return (world_id == nullptr ? FT_ERR_INVALID_ARGUMENT
			: FT_ERR_ALREADY_EXISTS);
	bool has_nonzero_id = false;
	for (std::size_t index = 0U; index < 32U; ++index)
		if (world_id[index] != 0U)
			has_nonzero_id = true;
	if (!has_nonzero_id)
		return (FT_ERR_INVALID_ARGUMENT);
	char encoded_id[65];
	for (std::size_t index = 0U; index < 32U; ++index)
		if (std::snprintf(encoded_id + index * 2U,
				sizeof(encoded_id) - index * 2U, "%02x",
				static_cast<unsigned int>(world_id[index])) != 2)
			return (FT_ERR_IO);
	encoded_id[64] = '\0';
	try
	{
		this->directory_.assign("saves/terrain/");
		this->directory_.append(encoded_id);
	}
	catch (const std::bad_alloc &)
	{
		this->directory_.clear();
		return (FT_ERR_NO_MEMORY);
	}
	int32_t result = file_create_directories("saves/terrain", 0700);
	if (result == FT_ERR_SUCCESS)
		result = file_create_directories(this->directory_.c_str(), 0700);
	if (result == FT_ERR_SUCCESS)
		result = file_validate_path_resolution_inside_root("saves/terrain",
			this->directory_.c_str());
	if (result != FT_ERR_SUCCESS)
	{
		this->directory_.clear();
		return (result);
	}
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldTerrainChunkStore::chunk_path(int32_t chunk_x, int32_t chunk_z,
	std::string &path_out) const noexcept
{
	char filename[64];
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	const int written = std::snprintf(filename, sizeof(filename),
		"%d_%d.vxc", chunk_x, chunk_z);
	if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(filename))
		return (FT_ERR_OUT_OF_RANGE);
	try
	{
		path_out.assign(this->directory_);
		path_out.push_back('/');
		path_out.append(filename, static_cast<std::size_t>(written));
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
	return (file_validate_path_resolution_inside_root("saves/terrain",
		path_out.c_str()));
}

int32_t WorldTerrainChunkStore::save_chunk(int32_t chunk_x, int32_t chunk_z,
	const game_voxel_chunk &chunk) const noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	ft_byte_buffer payload;
	std::vector<uint8_t> record;
	std::string path;
	int32_t result = this->chunk_path(chunk_x, chunk_z, path);
	if (result != FT_ERR_SUCCESS)
		return (result);
	result = payload.initialize();
	const bool payload_initialized = result == FT_ERR_SUCCESS;
	if (result == FT_ERR_SUCCESS)
		result = chunk.serialize(payload);
	if (result != FT_ERR_SUCCESS)
	{
		if (payload_initialized)
			(void)payload.destroy();
		return (result);
	}
	if (payload.size() > WORLD_TERRAIN_CHUNK_MAX_BYTES - 32U)
	{
		(void)payload.destroy();
		return (FT_ERR_OUT_OF_RANGE);
	}
	try
	{
		record.reserve(32U + payload.size());
		record.insert(record.end(), {'W', 'V', 'C', '1'});
		append_u32(record, WORLD_TERRAIN_CHUNK_VERSION);
		append_u32(record, static_cast<uint32_t>(chunk_x));
		append_u32(record, static_cast<uint32_t>(chunk_z));
		append_u64(record, payload.size());
		record.insert(record.end(), payload.data(),
			payload.data() + payload.size());
		append_u64(record, world_terrain_checksum(record.data(), record.size()));
	}
	catch (const std::bad_alloc &)
	{
		(void)payload.destroy();
		return (FT_ERR_NO_MEMORY);
	}
	const int32_t destroy_result = payload.destroy();
	if (destroy_result != FT_ERR_SUCCESS)
		return (destroy_result);
	return (file_write_all_atomic(path.c_str(),
		reinterpret_cast<const char *>(record.data()),
		static_cast<ft_size_t>(record.size())));
}

int32_t WorldTerrainChunkStore::load_chunk(int32_t chunk_x, int32_t chunk_z,
	game_voxel_chunk &chunk) const noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	std::string path;
	std::vector<uint8_t> record;
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	int32_t result = this->chunk_path(chunk_x, chunk_z, path);
	if (result != FT_ERR_SUCCESS)
		return (result);
	const file_type type = file_get_type(path.c_str());
	if (type == FILE_TYPE_MISSING)
		return (FT_ERR_NOT_FOUND);
	if (type != FILE_TYPE_REGULAR)
		return (FT_ERR_INVALID_PATH);
	result = file_validate_regular_file_inside_root("saves/terrain",
		path.c_str());
	if (result == FT_ERR_SUCCESS)
		result = read_file(path.c_str(), record);
	if (result != FT_ERR_SUCCESS)
		return (result);
	if (record.size() < 32U || std::memcmp(record.data(), "WVC1", 4U) != 0
		|| read_u32(record.data() + 4U) != WORLD_TERRAIN_CHUNK_VERSION
		|| read_u32(record.data() + 8U) != static_cast<uint32_t>(chunk_x)
		|| read_u32(record.data() + 12U) != static_cast<uint32_t>(chunk_z))
		return (FT_ERR_INVALID_ARGUMENT);
	const uint64_t payload_size = read_u64(record.data() + 16U);
	if (payload_size > WORLD_TERRAIN_CHUNK_MAX_BYTES
		|| payload_size != record.size() - 32U
		|| read_u64(record.data() + record.size() - 8U)
			!= world_terrain_checksum(record.data(), record.size() - 8U))
		return (FT_ERR_INVALID_ARGUMENT);
	ft_byte_buffer payload;
	result = payload.initialize(static_cast<ft_size_t>(payload_size), FT_TRUE);
	const bool payload_initialized = result == FT_ERR_SUCCESS;
	if (result == FT_ERR_SUCCESS)
		result = payload.append(record.data() + 24U,
			static_cast<ft_size_t>(payload_size));
	if (result == FT_ERR_SUCCESS)
		result = payload.reset_read_position();
	if (result == FT_ERR_SUCCESS)
		result = chunk.deserialize(payload);
	if (result == FT_ERR_SUCCESS && payload.remaining() != 0U)
		result = FT_ERR_INVALID_ARGUMENT;
	if (payload_initialized)
	{
		const int32_t destroy_result = payload.destroy();
		if (result == FT_ERR_SUCCESS)
			result = destroy_result;
	}
	return (result);
}

int32_t WorldTerrainChunkStore::remove_chunk(int32_t chunk_x,
	int32_t chunk_z) const noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	std::string path;
	int32_t result = this->chunk_path(chunk_x, chunk_z, path);
	if (result != FT_ERR_SUCCESS)
		return (result);
	const file_type type = file_get_type(path.c_str());
	if (type == FILE_TYPE_MISSING)
		return (FT_ERR_SUCCESS);
	if (type != FILE_TYPE_REGULAR)
		return (FT_ERR_INVALID_PATH);
	result = file_validate_regular_file_inside_root("saves/terrain",
		path.c_str());
	return (result == FT_ERR_SUCCESS ? file_delete(path.c_str()) : result);
}

int32_t WorldTerrainChunkStore::has_chunk(int32_t chunk_x, int32_t chunk_z,
	bool *exists_out) const noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	std::string path;
	int32_t result;
	if (exists_out == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	*exists_out = false;
	result = this->chunk_path(chunk_x, chunk_z, path);
	if (result != FT_ERR_SUCCESS)
		return (result);
	const file_type type = file_get_type(path.c_str());
	if (type == FILE_TYPE_MISSING)
		return (FT_ERR_SUCCESS);
	if (type != FILE_TYPE_REGULAR)
		return (FT_ERR_INVALID_PATH);
	result = file_validate_regular_file_inside_root("saves/terrain",
		path.c_str());
	if (result == FT_ERR_SUCCESS)
		*exists_out = true;
	return (result);
}

void WorldTerrainChunkStore::reset() noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	this->directory_.clear();
	this->initialized_ = false;
}

bool WorldTerrainChunkStore::is_initialized() const noexcept
{
	std::lock_guard<std::mutex> lock(this->mutex_);
	return (this->initialized_);
}
