#include "../../src/world/WorldRevisionMetadataStore.hpp"

WorldRevisionMetadataStore::WorldRevisionMetadataStore()
{
}

WorldRevisionMetadataStore::WorldRevisionMetadataStore(const WorldRevisionMetadataStore &other)
{
	(void)other;
}

WorldRevisionMetadataStore::~WorldRevisionMetadataStore()
{
}

WorldRevisionMetadataStore &WorldRevisionMetadataStore::operator=(const WorldRevisionMetadataStore &other)
{
	(void)other;
	return (*this);
}

int32_t WorldRevisionMetadataStore::save(const char *file_path,
	uint32_t revision_id,
	const std::vector<WorldRevisionManager::RevisionChunk> &manual_protected) noexcept
{
	FILE *file;
	uint32_t magic;
	uint32_t version;
	uint32_t manual_count;
	bool ok;

	if (file_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	file = std::fopen(file_path, "wb");
	if (file == nullptr)
		return (FT_ERR_IO);
	magic = 0x57525631U;
	version = 1U;
	manual_count = static_cast<uint32_t>(manual_protected.size());
	ok = std::fwrite(&magic, sizeof(magic), 1, file) == 1
		&& std::fwrite(&version, sizeof(version), 1, file) == 1
		&& std::fwrite(&revision_id, sizeof(revision_id), 1, file) == 1
		&& std::fwrite(&manual_count, sizeof(manual_count), 1, file) == 1;
	for (const WorldRevisionManager::RevisionChunk &entry : manual_protected)
	{
		ok = ok && std::fwrite(&entry.chunk_x, sizeof(entry.chunk_x), 1,
				file) == 1 && std::fwrite(&entry.chunk_z, sizeof(entry.chunk_z),
				1, file) == 1;
	}
	if (std::fclose(file) != 0)
		ok = false;
	return (ok ? FT_ERR_SUCCESS : FT_ERR_IO);
}

int32_t WorldRevisionMetadataStore::load(const char *file_path,
	uint32_t &revision_id,
	std::vector<WorldRevisionManager::RevisionChunk> &manual_protected) noexcept
{
	FILE *file;
	uint32_t magic;
	uint32_t version;
	uint32_t manual_count;
	bool ok;
	WorldRevisionManager::RevisionChunk entry;

	if (file_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	file = std::fopen(file_path, "rb");
	if (file == nullptr)
		return (FT_ERR_IO);
	ok = std::fread(&magic, sizeof(magic), 1, file) == 1 && std::fread(&version,
			sizeof(version), 1, file) == 1 && std::fread(&revision_id,
			sizeof(revision_id), 1, file) == 1 && std::fread(&manual_count,
			sizeof(manual_count), 1, file) == 1;
	if (!ok || magic != 0x57525631U || version != 1U || manual_count > 100000U)
	{
		std::fclose(file);
		return (FT_ERR_INVALID_ARGUMENT);
	}
	manual_protected.clear();
	for (uint32_t index = 0; index < manual_count; ++index)
	{
		if (std::fread(&entry.chunk_x, sizeof(entry.chunk_x), 1, file) != 1
			|| std::fread(&entry.chunk_z, sizeof(entry.chunk_z), 1, file) != 1)
		{
			std::fclose(file);
			manual_protected.clear();
			return (FT_ERR_IO);
		}
		manual_protected.push_back(entry);
	}
	if (std::fclose(file) != 0)
		return (FT_ERR_IO);
	return (FT_ERR_SUCCESS);
}
