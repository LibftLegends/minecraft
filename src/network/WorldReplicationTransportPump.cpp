#include "../../src/network/WorldReplicationTransportPump.hpp"
#include <new>

WorldReplicationTransportPump::WorldReplicationTransportPump()
	: transport_(nullptr), server_(nullptr), client_(nullptr),
	pending_message_(nullptr),
	role_(WorldReplicationTransportPumpRole::NONE), initialized_(false)
{
}

WorldReplicationTransportPump::WorldReplicationTransportPump(
	const WorldReplicationTransportPump &other)
	: transport_(nullptr), server_(nullptr), client_(nullptr),
	pending_message_(nullptr),
	role_(WorldReplicationTransportPumpRole::NONE), initialized_(false)
{
	(void)other;
}

WorldReplicationTransportPump::~WorldReplicationTransportPump()
{
	if (this->destroy() != FT_ERR_SUCCESS)
		return ;
}

WorldReplicationTransportPump &WorldReplicationTransportPump::operator=(
	const WorldReplicationTransportPump &other)
{
	(void)other;
	return (*this);
}

int32_t WorldReplicationTransportPump::initialize_common(
	networking_message_transport &transport)
{
	if (this->initialized_)
		return (FT_ERR_ALREADY_INITIALISED);
	this->transport_ = &transport;
	this->server_ = nullptr;
	this->client_ = nullptr;
	this->pending_message_ = nullptr;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationTransportPump::initialize_server(
	networking_message_transport &transport, WorldReplicationServer &server)
{
	int32_t error_code;

	error_code = this->initialize_common(transport);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->server_ = &server;
	this->role_ = WorldReplicationTransportPumpRole::SERVER;
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationTransportPump::initialize_client(
	networking_message_transport &transport, WorldReplicationClient &client)
{
	int32_t error_code;

	error_code = this->initialize_common(transport);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->client_ = &client;
	this->role_ = WorldReplicationTransportPumpRole::CLIENT;
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationTransportPump::pump(int32_t timeout_milliseconds,
	uint32_t maximum_messages, uint32_t &processed_messages)
{
	int32_t error_code;
	int32_t receive_error;
	int32_t payload_error;
	networking_received_message *pending_message;
	networking_received_message message;

	processed_messages = 0U;
	if (!this->initialized_ || this->transport_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	if (timeout_milliseconds < 0 || maximum_messages == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->transport_->is_worker_running() == FT_FALSE)
	{
		error_code = this->transport_->poll(timeout_milliseconds);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
	}
	if (this->pending_message_ != nullptr)
	{
		if (this->role_ == WorldReplicationTransportPumpRole::SERVER)
			error_code = this->server_->process_message(
				*this->pending_message_);
		else if (this->role_ == WorldReplicationTransportPumpRole::CLIENT)
			error_code = this->client_->enqueue_received_message(
				*this->pending_message_);
		else
			error_code = FT_ERR_INVALID_STATE;
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		delete this->pending_message_;
		this->pending_message_ = nullptr;
		processed_messages += 1U;
	}
	if (processed_messages >= maximum_messages)
		return (FT_ERR_SUCCESS);
	while (processed_messages < maximum_messages)
	{
		receive_error = this->transport_->receive_message(message);
		if (receive_error == FT_ERR_EMPTY)
			return (FT_ERR_SUCCESS);
		if (receive_error != FT_ERR_SUCCESS)
			return (receive_error);
		if (this->role_ == WorldReplicationTransportPumpRole::SERVER)
			error_code = this->server_->process_message(message);
		else if (this->role_ == WorldReplicationTransportPumpRole::CLIENT)
			error_code = this->client_->enqueue_received_message(message);
		else
			error_code = FT_ERR_INVALID_STATE;
		if (error_code == FT_ERR_FULL)
		{
			pending_message = new (std::nothrow) networking_received_message();
			if (pending_message == nullptr)
			{
				payload_error = message.payload.destroy();
				if (payload_error != FT_ERR_SUCCESS)
					return (payload_error);
				return (FT_ERR_NO_MEMORY);
			}
			pending_message->connection_id = message.connection_id;
			pending_message->channel = message.channel;
			pending_message->lane = message.lane;
			pending_message->delivery = message.delivery;
			pending_message->sequence = message.sequence;
			payload_error = pending_message->payload.initialize(message.payload);
			if (payload_error != FT_ERR_SUCCESS)
			{
				delete pending_message;
				payload_error = message.payload.destroy();
				if (payload_error != FT_ERR_SUCCESS)
					return (payload_error);
				return (FT_ERR_NO_MEMORY);
			}
			payload_error = message.payload.destroy();
			if (payload_error != FT_ERR_SUCCESS)
			{
				delete pending_message;
				return (payload_error);
			}
			this->pending_message_ = pending_message;
			return (FT_ERR_FULL);
		}
		payload_error = message.payload.destroy();
		if (payload_error != FT_ERR_SUCCESS
			&& error_code == FT_ERR_SUCCESS)
			error_code = payload_error;
		processed_messages += 1U;
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
	}
	return (FT_ERR_SUCCESS);
}

WorldReplicationTransportPumpRole
	WorldReplicationTransportPump::role() const
{
	return (this->role_);
}

int32_t WorldReplicationTransportPump::destroy()
{
	if (this->pending_message_ != nullptr)
	{
		delete this->pending_message_;
		this->pending_message_ = nullptr;
	}
	this->transport_ = nullptr;
	this->server_ = nullptr;
	this->client_ = nullptr;
	this->role_ = WorldReplicationTransportPumpRole::NONE;
	this->initialized_ = false;
	return (FT_ERR_SUCCESS);
}
