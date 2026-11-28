#include "../../src/network/WorldReplicationCursorStore.hpp"
#include "../../Libft/Modules/File/file_utils.hpp"

WorldReplicationCursorStore::WorldReplicationCursorStore()
	: path_(), initialized_(false)
{
}

WorldReplicationCursorStore::WorldReplicationCursorStore(
	const WorldReplicationCursorStore &other)
	: path_(other.path_), initialized_(other.initialized_)
{
}

WorldReplicationCursorStore::~WorldReplicationCursorStore()
{
}

WorldReplicationCursorStore &WorldReplicationCursorStore::operator=(
	const WorldReplicationCursorStore &other)
{
	if (this != &other)
	{
		this->path_ = other.path_;
		this->initialized_ = other.initialized_;
	}
	return (*this);
}

int32_t WorldReplicationCursorStore::initialize(const char *path)
{
	if (path == nullptr || path[0] == '\0')
		return (FT_ERR_INVALID_ARGUMENT);
	this->path_ = path;
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationCursorStore::load(
	networking_replication_peer_cursor &cursor) const
{
	ft_string file_data;
	ft_byte_buffer encoded_cursor;
	networking_replication_peer_cursor decoded_cursor;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (file_get_type(this->path_.c_str()) == FILE_TYPE_MISSING)
		return (FT_ERR_NOT_FOUND);
	error_code = file_data.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = file_read_all(this->path_.c_str(), file_data);
	if (error_code == FT_ERR_SUCCESS
		&& file_data.size() != NETWORKING_REPLICATION_CURSOR_SIZE)
		error_code = FT_ERR_INVALID_ARGUMENT;
	if (error_code == FT_ERR_SUCCESS)
		error_code = encoded_cursor.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = encoded_cursor.append(
		reinterpret_cast<const uint8_t *>(file_data.data()), file_data.size());
	if (error_code == FT_ERR_SUCCESS)
		error_code = networking_replication_peer_cursor_deserialize(
		decoded_cursor, encoded_cursor);
	if (error_code == FT_ERR_SUCCESS && encoded_cursor.remaining() != 0U)
		error_code = FT_ERR_INVALID_ARGUMENT;
	if (error_code == FT_ERR_SUCCESS)
		cursor = decoded_cursor;
	destroy_error = encoded_cursor.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = file_data.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationCursorStore::load_for_session(
	uint64_t server_instance_id, uint64_t session_id,
	uint64_t subscription_id,
	networking_replication_peer_cursor &cursor) const
{
	networking_replication_peer_cursor loaded_cursor;
	int32_t error_code;

	if (server_instance_id == 0U || session_id == 0U
		|| subscription_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = this->load(loaded_cursor);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (loaded_cursor.server_instance_id != server_instance_id
		|| loaded_cursor.session_id != session_id
		|| loaded_cursor.subscription_id != subscription_id)
	{
		if (this->clear() != FT_ERR_SUCCESS)
			return (FT_ERR_IO);
		return (FT_ERR_PERMISSION_DENIED);
	}
	cursor = loaded_cursor;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationCursorStore::save(
	const networking_replication_peer_cursor &cursor) const
{
	ft_byte_buffer encoded_cursor;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	error_code = encoded_cursor.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = networking_replication_peer_cursor_serialize(cursor,
		encoded_cursor);
	if (error_code == FT_ERR_SUCCESS)
		error_code = file_replace_safe(this->path_.c_str(),
		reinterpret_cast<const char *>(encoded_cursor.data()),
		encoded_cursor.size());
	destroy_error = encoded_cursor.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationCursorStore::clear() const
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (file_get_type(this->path_.c_str()) == FILE_TYPE_MISSING)
		return (FT_ERR_SUCCESS);
	return (file_delete(this->path_.c_str()));
}

bool WorldReplicationCursorStore::initialized() const noexcept
{
	return (this->initialized_);
}
