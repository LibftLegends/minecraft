#include "../../src/network/WorldReplicationServerRuntime.hpp"

WorldReplicationServerRuntime::WorldReplicationServerRuntime()
	: service_(), server_(), pump_(), transport_(nullptr),
	worker_started_(false), initialized_(false)
{
}

WorldReplicationServerRuntime::WorldReplicationServerRuntime(
	const WorldReplicationServerRuntime &other)
	: service_(), server_(), pump_(), transport_(nullptr),
	worker_started_(false), initialized_(false)
{
	(void)other;
}

WorldReplicationServerRuntime::~WorldReplicationServerRuntime()
{
	if (this->destroy() != FT_ERR_SUCCESS)
		return ;
}

WorldReplicationServerRuntime &WorldReplicationServerRuntime::operator=(
	const WorldReplicationServerRuntime &other)
{
	(void)other;
	return (*this);
}

int32_t WorldReplicationServerRuntime::initialize(
	World &world, networking_message_transport &transport,
	uint64_t server_instance_id, uint64_t world_id)
{
	int32_t error_code;

	if (this->initialized_)
		return (FT_ERR_ALREADY_INITIALISED);
	this->transport_ = &transport;
	this->worker_started_ = false;
	error_code = this->service_.initialize(world, server_instance_id,
		world_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->server_.initialize(this->service_);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->pump_.initialize_server(transport, this->server_);
	if (error_code != FT_ERR_SUCCESS)
	{
		int32_t cleanup_error;

		cleanup_error = this->server_.destroy();
		this->service_.destroy();
		this->transport_ = nullptr;
		if (error_code == FT_ERR_SUCCESS)
			error_code = cleanup_error;
		return (error_code);
	}
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationServerRuntime::listen(
	const networking_message_endpoint &endpoint)
{
	if (!this->initialized_ || this->transport_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	return (this->transport_->listen(endpoint));
}

int32_t WorldReplicationServerRuntime::add_peer(
	networking_message_connection &connection, uint64_t session_id)
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->server_.add_peer(connection, session_id));
}

int32_t WorldReplicationServerRuntime::add_authenticated_peer(
	networking_message_connection &connection, uint64_t session_id)
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->server_.add_authenticated_peer(connection, session_id));
}

int32_t WorldReplicationServerRuntime::set_repair_request_callback(
	world_replication_repair_request_callback callback, void *user_data)
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->server_.set_repair_request_callback(callback, user_data));
}

int32_t WorldReplicationServerRuntime::create_chunk_hash_manifest(
	int32_t chunk_x, int32_t chunk_z, uint64_t session_id,
	ProtocolChunkHashManifestMessage &manifest) const
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->service_.create_chunk_hash_manifest(chunk_x, chunk_z,
		session_id, manifest));
}

int32_t WorldReplicationServerRuntime::broadcast_hash_manifest(
	const ProtocolChunkHashManifestMessage &manifest) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->server_.broadcast_hash_manifest(manifest));
}

int32_t WorldReplicationServerRuntime::start_worker()
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

int32_t WorldReplicationServerRuntime::pump(int32_t timeout_milliseconds,
	uint32_t maximum_messages, uint32_t &processed_messages)
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->pump_.pump(timeout_milliseconds, maximum_messages,
		processed_messages));
}

int32_t WorldReplicationServerRuntime::tick(int32_t timeout_milliseconds,
	uint32_t maximum_messages, uint32_t &processed_messages)
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->pump(timeout_milliseconds, maximum_messages,
		processed_messages));
}

int32_t WorldReplicationServerRuntime::stop_worker()
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

int32_t WorldReplicationServerRuntime::destroy()
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
	operation_error = this->server_.destroy();
	if (error_code == FT_ERR_SUCCESS && operation_error != FT_ERR_SUCCESS)
		error_code = operation_error;
	this->service_.destroy();
	this->transport_ = nullptr;
	this->initialized_ = false;
	return (error_code);
}
