#include "../../src/network/WorldReplicationClientRuntime.hpp"

WorldReplicationClientRuntime::WorldReplicationClientRuntime()
	: client_(), pump_(), transport_(nullptr), cursor_store_(nullptr),
	worker_started_(false),
	initialized_(false)
{
}

WorldReplicationClientRuntime::WorldReplicationClientRuntime(
	const WorldReplicationClientRuntime &other)
	: client_(), pump_(), transport_(nullptr), cursor_store_(nullptr),
	worker_started_(false),
	initialized_(false)
{
	(void)other;
}

WorldReplicationClientRuntime::~WorldReplicationClientRuntime()
{
	if (this->destroy() != FT_ERR_SUCCESS)
		return ;
}

WorldReplicationClientRuntime &WorldReplicationClientRuntime::operator=(
	const WorldReplicationClientRuntime &other)
{
	(void)other;
	return (*this);
}

int32_t WorldReplicationClientRuntime::initialize(
	networking_message_transport &transport, uint64_t server_instance_id,
	uint64_t session_id, uint64_t world_id, uint32_t maximum_messages,
	uint32_t maximum_payload_bytes, uint32_t maximum_operations)
{
	int32_t error_code;

	if (this->initialized_)
		return (FT_ERR_ALREADY_INITIALISED);
	error_code = this->client_.initialize(server_instance_id, session_id,
		world_id, maximum_messages, maximum_payload_bytes,
		maximum_operations);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->pump_.initialize_client(transport, this->client_);
	if (error_code != FT_ERR_SUCCESS)
	{
		int32_t cleanup_error;

		cleanup_error = this->client_.destroy();
		if (error_code == FT_ERR_SUCCESS)
			error_code = cleanup_error;
		return (error_code);
	}
	this->transport_ = &transport;
	this->cursor_store_ = nullptr;
	this->worker_started_ = false;
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationClientRuntime::set_callbacks(
	world_replication_snapshot_callback snapshot,
	world_replication_block_delta_callback block_delta,
	world_replication_light_delta_callback light_delta,
	world_replication_edit_result_callback edit_result,
	void *user_data) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->client_.set_callbacks(snapshot, block_delta, light_delta,
		edit_result, user_data));
}

int32_t WorldReplicationClientRuntime::set_replica_store(
	WorldReplicationReplicaStore *store) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->client_.set_replica_store(store));
}

int32_t WorldReplicationClientRuntime::set_cursor_store(
	WorldReplicationCursorStore *store) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	this->cursor_store_ = store;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationClientRuntime::restore_cursor(
	uint64_t subscription_id,
	networking_replication_peer_cursor &cursor) const
{
	if (!this->initialized_ || this->cursor_store_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	return (this->cursor_store_->load_for_session(
		this->client_.server_instance_id(), this->client_.session_id(),
		subscription_id, cursor));
}

int32_t WorldReplicationClientRuntime::persist_cursor(
	const networking_replication_peer_cursor &cursor) const
{
	if (!this->initialized_ || this->cursor_store_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	if (cursor.server_instance_id != this->client_.server_instance_id()
		|| cursor.session_id != this->client_.session_id())
		return (FT_ERR_PERMISSION_DENIED);
	return (this->cursor_store_->save(cursor));
}

int32_t WorldReplicationClientRuntime::set_repair_callback(
	world_replication_repair_callback repair, void *user_data) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->client_.set_repair_callback(repair, user_data));
}

int32_t WorldReplicationClientRuntime::set_hash_manifest_callback(
	world_replication_hash_manifest_callback callback, void *user_data) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->client_.set_hash_manifest_callback(callback, user_data));
}

int32_t WorldReplicationClientRuntime::pump_transport(
	int32_t timeout_milliseconds, uint32_t maximum_messages,
	uint32_t &processed_messages)
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->pump_.pump(timeout_milliseconds, maximum_messages,
		processed_messages));
}

int32_t WorldReplicationClientRuntime::start_worker()
{
	int32_t error_code;

	if (!this->initialized_ || this->transport_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	if (this->worker_started_)
		return (FT_ERR_ALREADY_INITIALISED);
	error_code = this->transport_->start_worker();
	if (error_code == FT_ERR_SUCCESS)
		this->worker_started_ = true;
	return (error_code);
}

int32_t WorldReplicationClientRuntime::advance_frame(
	uint32_t maximum_messages, uint32_t &processed_messages)
{
	int32_t error_code;

	processed_messages = 0U;
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (maximum_messages == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = this->client_.reset_budget();
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->client_.drain_received_messages(maximum_messages,
			processed_messages);
	return (error_code);
}

int32_t WorldReplicationClientRuntime::tick(
	int32_t timeout_milliseconds, uint32_t maximum_transport_messages,
	uint32_t maximum_application_messages,
	uint32_t &processed_transport_messages,
	uint32_t &processed_application_messages)
{
	int32_t transport_error;
	int32_t application_error;

	processed_transport_messages = 0U;
	processed_application_messages = 0U;
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (maximum_transport_messages == 0U
		|| maximum_application_messages == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	transport_error = this->pump_transport(timeout_milliseconds,
		maximum_transport_messages, processed_transport_messages);
	application_error = this->advance_frame(maximum_application_messages,
		processed_application_messages);
	if (transport_error != FT_ERR_SUCCESS)
		return (transport_error);
	return (application_error);
}

int32_t WorldReplicationClientRuntime::request_chunk_sync(
	networking_message_connection &connection, int32_t chunk_x, int32_t chunk_z,
	uint64_t block_revision, uint64_t light_revision,
	uint64_t message_sequence) const
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->client_.request_chunk_sync(connection, chunk_x, chunk_z,
		block_revision, light_revision, message_sequence));
}

WorldReplicationClient &WorldReplicationClientRuntime::client() noexcept
{
	return (this->client_);
}

int32_t WorldReplicationClientRuntime::stop_worker()
{
	int32_t error_code;

	if (!this->initialized_ || this->transport_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	if (!this->worker_started_)
		return (FT_ERR_SUCCESS);
	error_code = this->transport_->stop_worker();
	if (error_code == FT_ERR_SUCCESS)
		this->worker_started_ = false;
	return (error_code);
}

int32_t WorldReplicationClientRuntime::destroy()
{
	int32_t error_code;
	int32_t operation_error;

	error_code = FT_ERR_SUCCESS;
	if (this->worker_started_ && this->transport_ != nullptr)
	{
		operation_error = this->transport_->stop_worker();
		if (operation_error != FT_ERR_SUCCESS)
			return (operation_error);
		this->worker_started_ = false;
	}
	operation_error = this->pump_.destroy();
	if (error_code == FT_ERR_SUCCESS && operation_error != FT_ERR_SUCCESS)
		error_code = operation_error;
	operation_error = this->client_.destroy();
	if (error_code == FT_ERR_SUCCESS && operation_error != FT_ERR_SUCCESS)
		error_code = operation_error;
	this->transport_ = nullptr;
	this->cursor_store_ = nullptr;
	this->initialized_ = false;
	return (error_code);
}
