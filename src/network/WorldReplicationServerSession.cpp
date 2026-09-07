#include "../../src/network/WorldReplicationServerSession.hpp"

WorldReplicationServerSession::WorldReplicationServerSession()
	: service_(nullptr), connection_(nullptr), server_instance_id_(0U),
	session_id_(0U), next_message_sequence_(1U), subscribed_chunk_x_(),
	subscribed_chunk_z_(), acknowledged_block_revision_(),
	acknowledged_light_revision_(), acknowledged_generation_epoch_(),
	snapshot_acknowledged_(), subscription_count_(0U),
	recent_edit_requests_(), recent_edit_results_(),
	recent_edit_result_used_(), recent_edit_result_cursor_(0U),
	delta_broadcast_callback_(nullptr), delta_broadcast_user_data_(nullptr),
	repair_request_callback_(nullptr), repair_request_user_data_(nullptr),
	sync_request_callback_(nullptr), sync_request_user_data_(nullptr),
	initialized_(false)
{
}

WorldReplicationServerSession::WorldReplicationServerSession(
	const WorldReplicationServerSession &other)
	: service_(nullptr), connection_(nullptr), server_instance_id_(0U),
	session_id_(0U), next_message_sequence_(1U), subscribed_chunk_x_(),
	subscribed_chunk_z_(), acknowledged_block_revision_(),
	acknowledged_light_revision_(), acknowledged_generation_epoch_(),
	snapshot_acknowledged_(), subscription_count_(0U),
	recent_edit_requests_(), recent_edit_results_(),
	recent_edit_result_used_(), recent_edit_result_cursor_(0U),
	delta_broadcast_callback_(nullptr), delta_broadcast_user_data_(nullptr),
	repair_request_callback_(nullptr), repair_request_user_data_(nullptr),
	sync_request_callback_(nullptr), sync_request_user_data_(nullptr),
	initialized_(false)
{
	(void)other;
}

WorldReplicationServerSession::~WorldReplicationServerSession()
{
	if (this->destroy() != FT_ERR_SUCCESS)
		return ;
}

WorldReplicationServerSession &WorldReplicationServerSession::operator=(
	const WorldReplicationServerSession &other)
{
	(void)other;
	return (*this);
}

int32_t WorldReplicationServerSession::initialize(
	WorldReplicationService &service, networking_message_connection &connection,
	uint64_t server_instance_id, uint64_t session_id)
{
	if (this->initialized_)
		return (FT_ERR_ALREADY_INITIALISED);
	if (server_instance_id == 0U || session_id == 0U
		|| service.server_instance_id() != server_instance_id)
		return (FT_ERR_INVALID_ARGUMENT);
	this->service_ = &service;
	this->connection_ = &connection;
	this->server_instance_id_ = server_instance_id;
	this->session_id_ = session_id;
	this->next_message_sequence_ = 1U;
	this->subscription_count_ = 0U;
	this->recent_edit_result_cursor_ = 0U;
	ft_memset(this->recent_edit_result_used_, 0,
		sizeof(this->recent_edit_result_used_));
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationServerSession::subscribe_chunk(int32_t chunk_x,
	int32_t chunk_z)
{
	uint32_t index;

	index = 0U;
	while (index < this->subscription_count_)
	{
		if (this->subscribed_chunk_x_[index] == chunk_x
			&& this->subscribed_chunk_z_[index] == chunk_z)
			return (FT_ERR_SUCCESS);
		index += 1U;
	}
	if (this->subscription_count_ >= WORLD_REPLICATION_MAX_SUBSCRIPTIONS)
		return (FT_ERR_FULL);
	this->subscribed_chunk_x_[this->subscription_count_] = chunk_x;
	this->subscribed_chunk_z_[this->subscription_count_] = chunk_z;
	this->acknowledged_block_revision_[this->subscription_count_] = 0U;
	this->acknowledged_light_revision_[this->subscription_count_] = 0U;
	this->acknowledged_generation_epoch_[this->subscription_count_] = 0U;
	this->snapshot_acknowledged_[this->subscription_count_] = 0U;
	this->subscription_count_ += 1U;
	return (FT_ERR_SUCCESS);
}

ft_bool WorldReplicationServerSession::is_subscribed(int32_t chunk_x,
	int32_t chunk_z) const
{
	uint32_t index;

	index = 0U;
	while (index < this->subscription_count_)
	{
		if (this->subscribed_chunk_x_[index] == chunk_x
			&& this->subscribed_chunk_z_[index] == chunk_z)
			return (FT_TRUE);
		index += 1U;
	}
	return (FT_FALSE);
}

uint32_t WorldReplicationServerSession::subscription_count() const
{
	return (this->subscription_count_);
}

int32_t WorldReplicationServerSession::subscription_at(uint32_t index,
	int32_t &chunk_x, int32_t &chunk_z) const
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (index >= this->subscription_count_)
		return (FT_ERR_OUT_OF_RANGE);
	chunk_x = this->subscribed_chunk_x_[index];
	chunk_z = this->subscribed_chunk_z_[index];
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationServerSession::get_chunk_acknowledgement(
	int32_t chunk_x, int32_t chunk_z, uint64_t &block_revision,
	uint64_t &light_revision, uint64_t &generation_epoch,
	uint8_t &snapshot_acknowledged) const
{
	uint32_t index;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	index = 0U;
	while (index < this->subscription_count_)
	{
		if (this->subscribed_chunk_x_[index] == chunk_x
			&& this->subscribed_chunk_z_[index] == chunk_z)
		{
			block_revision = this->acknowledged_block_revision_[index];
			light_revision = this->acknowledged_light_revision_[index];
			generation_epoch = this->acknowledged_generation_epoch_[index];
			snapshot_acknowledged = this->snapshot_acknowledged_[index];
			return (FT_ERR_SUCCESS);
		}
		index += 1U;
	}
	return (FT_ERR_NOT_FOUND);
}

uint64_t WorldReplicationServerSession::connection_id() const
{
	if (this->connection_ == nullptr)
		return (0U);
	return (this->connection_->get_id());
}

int32_t WorldReplicationServerSession::set_delta_broadcast_callback(
	world_replication_delta_broadcast_callback callback, void *user_data)
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	this->delta_broadcast_callback_ = callback;
	this->delta_broadcast_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationServerSession::set_repair_request_callback(
	world_replication_repair_request_callback callback, void *user_data)
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (callback == nullptr)
		return (FT_ERR_INVALID_POINTER);
	this->repair_request_callback_ = callback;
	this->repair_request_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationServerSession::set_sync_request_callback(
	world_replication_sync_request_callback callback, void *user_data)
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (callback == nullptr)
		return (FT_ERR_INVALID_POINTER);
	this->sync_request_callback_ = callback;
	this->sync_request_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationServerSession::publish_block_delta(
	const game_block_delta &delta)
{
	ProtocolChunkBlockDeltaMessage block_delta;
	game_block_delta recipient_delta;
	uint64_t sequence;
	int32_t error_code;

	if (!this->initialized_ || this->service_ == nullptr
		|| this->connection_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	if (delta.world_id != this->service_->world_id())
		return (FT_ERR_INVALID_ARGUMENT);
	recipient_delta = delta;
	recipient_delta.session_id = this->session_id_;
	block_delta.delta = recipient_delta;
	error_code = this->next_sequence(sequence);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->service_->send_block_delta(*this->connection_,
			block_delta, this->session_id_, sequence);
	return (error_code);
}

int32_t WorldReplicationServerSession::publish_light_delta(
	const ProtocolChunkLightDeltaMessage &delta)
{
	ProtocolChunkLightDeltaMessage recipient_delta;
	uint64_t sequence;
	int32_t error_code;

	if (!this->initialized_ || this->service_ == nullptr
		|| this->connection_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	if (delta.world_id != this->service_->world_id()
		|| delta.base_light_revision == 0U
		|| delta.final_light_revision <= delta.base_light_revision
		|| delta.source_block_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	recipient_delta = delta;
	recipient_delta.session_id = this->session_id_;
	error_code = this->next_sequence(sequence);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->service_->send_light_delta(*this->connection_,
			recipient_delta, this->session_id_, sequence);
	return (error_code);
}

int32_t WorldReplicationServerSession::publish_hash_manifest(
	const ProtocolChunkHashManifestMessage &manifest)
{
	ProtocolChunkHashManifestMessage recipient_manifest;
	uint64_t sequence;
	int32_t error_code;

	if (!this->initialized_ || this->service_ == nullptr
		|| this->connection_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	if (manifest.world_id != this->service_->world_id())
		return (FT_ERR_INVALID_ARGUMENT);
	recipient_manifest = manifest;
	recipient_manifest.session_id = this->session_id_;
	error_code = this->next_sequence(sequence);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->service_->send_chunk_hash_manifest(
			*this->connection_, recipient_manifest, this->session_id_, sequence);
	return (error_code);
}

int32_t WorldReplicationServerSession::send_snapshot(int32_t chunk_x,
	int32_t chunk_z)
{
	ProtocolChunkSnapshotMessage snapshot;
	uint64_t sequence;
	int32_t error_code;

	if (!this->initialized_ || this->service_ == nullptr
		|| this->connection_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	error_code = this->service_->create_chunk_snapshot(chunk_x, chunk_z,
		this->session_id_, snapshot);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->next_sequence(sequence);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->service_->send_chunk_snapshot(*this->connection_,
		snapshot, this->session_id_, sequence);
	return (error_code);
}

int32_t WorldReplicationServerSession::unsubscribe_chunk(int32_t chunk_x,
	int32_t chunk_z)
{
	uint32_t index;

	index = 0U;
	while (index < this->subscription_count_)
	{
		if (this->subscribed_chunk_x_[index] == chunk_x
			&& this->subscribed_chunk_z_[index] == chunk_z)
		{
			while (index + 1U < this->subscription_count_)
			{
				this->subscribed_chunk_x_[index]
					= this->subscribed_chunk_x_[index + 1U];
				this->subscribed_chunk_z_[index]
					= this->subscribed_chunk_z_[index + 1U];
				this->acknowledged_block_revision_[index]
					= this->acknowledged_block_revision_[index + 1U];
				this->acknowledged_light_revision_[index]
					= this->acknowledged_light_revision_[index + 1U];
				this->acknowledged_generation_epoch_[index]
					= this->acknowledged_generation_epoch_[index + 1U];
				this->snapshot_acknowledged_[index]
					= this->snapshot_acknowledged_[index + 1U];
				index += 1U;
			}
			this->subscription_count_ -= 1U;
			return (FT_ERR_SUCCESS);
		}
		index += 1U;
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldReplicationServerSession::next_sequence(uint64_t &sequence)
{
	if (this->next_message_sequence_ == 0U
		|| this->next_message_sequence_ == UINT64_MAX)
		return (FT_ERR_OUT_OF_RANGE);
	sequence = this->next_message_sequence_;
	this->next_message_sequence_ += 1U;
	return (FT_ERR_SUCCESS);
}

ft_bool WorldReplicationServerSession::edit_requests_match(
	const game_block_change_request &left,
	const game_block_change_request &right)
{
	if (left.protocol_version != right.protocol_version
		|| left.session_id != right.session_id
		|| left.request_id != right.request_id
		|| left.world_id != right.world_id
		|| left.chunk_x != right.chunk_x || left.chunk_z != right.chunk_z
		|| left.expected_revision != right.expected_revision
		|| left.expected_block_id != right.expected_block_id
		|| left.requested_block_id != right.requested_block_id
		|| left.local_x != right.local_x || left.local_y != right.local_y
		|| left.local_z != right.local_z)
		return (FT_FALSE);
	return (FT_TRUE);
}

int32_t WorldReplicationServerSession::cache_edit_result(
	const ProtocolEditIntentMessage &intent,
	const ProtocolEditResultMessage &result)
{
	uint32_t index;

	index = this->recent_edit_result_cursor_;
	this->recent_edit_requests_[index] = intent.request;
	this->recent_edit_results_[index] = result;
	this->recent_edit_result_used_[index] = 1U;
	this->recent_edit_result_cursor_ += 1U;
	if (this->recent_edit_result_cursor_ == WORLD_REPLICATION_MAX_EDIT_RESULTS)
		this->recent_edit_result_cursor_ = 0U;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationServerSession::process_cached_edit(
	const ProtocolEditIntentMessage &intent, uint32_t cache_index)
{
	uint64_t result_sequence;
	int32_t error_code;
	int32_t operation_error;

	if (this->edit_requests_match(this->recent_edit_requests_[cache_index],
		intent.request) == FT_FALSE)
		return (FT_ERR_PERMISSION_DENIED);
	error_code = this->next_sequence(result_sequence);
	if (error_code == FT_ERR_SUCCESS)
		operation_error = this->service_->send_edit_result(*this->connection_,
			this->recent_edit_results_[cache_index], this->session_id_,
			result_sequence);
	else
		operation_error = error_code;
	if (error_code == FT_ERR_SUCCESS && operation_error != FT_ERR_SUCCESS)
		error_code = operation_error;
	if (this->recent_edit_results_[cache_index].accepted != 0U)
	{
		operation_error = this->publish_block_delta(
			this->recent_edit_results_[cache_index].delta);
		if (error_code == FT_ERR_SUCCESS
			&& operation_error != FT_ERR_SUCCESS)
			error_code = operation_error;
	}
	return (error_code);
}

int32_t WorldReplicationServerSession::process_edit_intent(
	ft_byte_buffer &payload, uint64_t incoming_sequence)
{
	ProtocolEditIntentMessage intent;
	ProtocolEditResultMessage result;
	uint32_t cache_index;
	uint64_t result_sequence;
	int32_t operation_error;
	int32_t error_code;

	if (incoming_sequence == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = intent.deserialize(payload);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (intent.request.session_id != this->session_id_)
		return (FT_ERR_PERMISSION_DENIED);
	cache_index = 0U;
	while (cache_index < WORLD_REPLICATION_MAX_EDIT_RESULTS)
	{
		if (this->recent_edit_result_used_[cache_index] != 0U
			&& this->recent_edit_requests_[cache_index].request_id
			== intent.request.request_id)
			return (this->process_cached_edit(intent, cache_index));
		cache_index += 1U;
	}
	error_code = this->service_->handle_edit_intent(intent, result);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = this->cache_edit_result(intent, result);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (result.accepted == 0U)
	{
		error_code = this->next_sequence(result_sequence);
		if (error_code == FT_ERR_SUCCESS)
			error_code = this->service_->send_edit_result(*this->connection_,
				result, this->session_id_, result_sequence);
		return (error_code);
	}
	error_code = FT_ERR_SUCCESS;
	if (this->delta_broadcast_callback_ != nullptr)
	{
		operation_error = this->delta_broadcast_callback_(result.delta,
			this->connection_->get_id(), this->delta_broadcast_user_data_);
		if (error_code == FT_ERR_SUCCESS
			&& operation_error != FT_ERR_SUCCESS)
			error_code = operation_error;
	}
	operation_error = this->publish_block_delta(result.delta);
	if (error_code == FT_ERR_SUCCESS
		&& operation_error != FT_ERR_SUCCESS)
		error_code = operation_error;
	operation_error = this->next_sequence(result_sequence);
	if (operation_error == FT_ERR_SUCCESS)
		operation_error = this->service_->send_edit_result(*this->connection_,
			result, this->session_id_, result_sequence);
	if (error_code == FT_ERR_SUCCESS
		&& operation_error != FT_ERR_SUCCESS)
		error_code = operation_error;
	return (error_code);
}

int32_t WorldReplicationServerSession::process_chunk_request(
	ft_byte_buffer &payload)
{
	ProtocolChunkRequestMessage request;
	int32_t error_code;

	error_code = request.deserialize(payload);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = this->subscribe_chunk(request.chunk_x, request.chunk_z);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = this->send_snapshot(request.chunk_x, request.chunk_z);
	return (error_code);
}

int32_t WorldReplicationServerSession::process_chunk_sync_request(
	ft_byte_buffer &payload)
{
	ProtocolChunkSyncRequestMessage request;
	int32_t error_code;

	error_code = request.deserialize(payload);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (request.session_id != this->session_id_
		|| request.world_id != this->service_->world_id())
		return (FT_ERR_PERMISSION_DENIED);
	if (this->sync_request_callback_ == nullptr)
		return (FT_ERR_INVALID_STATE);
	return (this->sync_request_callback_)(request,
		this->connection_->get_id(), this->sync_request_user_data_);
}

int32_t WorldReplicationServerSession::process_chunk_interest(
	ft_byte_buffer &payload)
{
	ProtocolChunkInterestMessage interest;
	int32_t error_code;

	error_code = interest.deserialize(payload);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (interest.subscribed != 0U)
		return (this->subscribe_chunk(interest.chunk_x, interest.chunk_z));
	error_code = this->unsubscribe_chunk(interest.chunk_x, interest.chunk_z);
	if (error_code == FT_ERR_NOT_FOUND)
		return (FT_ERR_SUCCESS);
	return (error_code);
}

int32_t WorldReplicationServerSession::process_acknowledgement(
	ft_byte_buffer &payload)
{
	ProtocolChunkAcknowledgementMessage acknowledgement;
	int32_t error_code;

	error_code = acknowledgement.deserialize(payload);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = this->service_->validate_chunk_acknowledgement(acknowledgement,
		this->session_id_);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (this->acknowledge_chunk(acknowledgement));
}

int32_t WorldReplicationServerSession::process_repair_request(
	ft_byte_buffer &payload)
{
	ProtocolChunkRepairRequestMessage request;
	ProtocolChunkRepairResponseMessage response;
	uint64_t sequence;
	int32_t error_code;

	error_code = request.deserialize(payload);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (request.session_id != this->session_id_
		|| request.world_id != this->service_->world_id())
		return (FT_ERR_PERMISSION_DENIED);
	if (this->repair_request_callback_ != nullptr)
		error_code = this->repair_request_callback_(request, response,
			this->repair_request_user_data_);
	else
		error_code = this->service_->create_chunk_repair_response(request,
			response);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (response.protocol_version != GAME_WORLD_DELTA_PROTOCOL_VERSION
		|| response.session_id != this->session_id_
		|| response.world_id != this->service_->world_id()
		|| response.chunk_x != request.chunk_x
		|| response.chunk_z != request.chunk_z
		|| response.repaired_section_mask == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = this->next_sequence(sequence);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->service_->send_chunk_repair_response(
			*this->connection_, response, this->session_id_, sequence);
	return (error_code);
}

int32_t WorldReplicationServerSession::acknowledge_chunk(
	const ProtocolChunkAcknowledgementMessage &acknowledgement)
{
	uint32_t index;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	index = 0U;
	while (index < this->subscription_count_)
	{
		if (this->subscribed_chunk_x_[index] == acknowledgement.chunk_x
			&& this->subscribed_chunk_z_[index] == acknowledgement.chunk_z)
		{
			if (acknowledgement.block_revision
				> this->acknowledged_block_revision_[index])
				this->acknowledged_block_revision_[index]
					= acknowledgement.block_revision;
			if (acknowledgement.light_revision
				> this->acknowledged_light_revision_[index])
				this->acknowledged_light_revision_[index]
					= acknowledgement.light_revision;
			if (acknowledgement.generation_epoch
				> this->acknowledged_generation_epoch_[index])
				this->acknowledged_generation_epoch_[index]
					= acknowledgement.generation_epoch;
			if (acknowledgement.snapshot_acknowledged != 0U)
				this->snapshot_acknowledged_[index] = 1U;
			return (FT_ERR_SUCCESS);
		}
		index += 1U;
	}
	return (FT_ERR_PERMISSION_DENIED);
}

int32_t WorldReplicationServerSession::process_message(
	const networking_received_message &message)
{
	networking_replication_envelope envelope;
	ft_byte_buffer payload;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_ || this->service_ == nullptr
		|| this->connection_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	if (message.connection_id != this->connection_->get_id())
		return (FT_ERR_PERMISSION_DENIED);
	if (this->connection_->get_state()
		!= networking_message_connection_state::CONNECTED)
		return (FT_ERR_PERMISSION_DENIED);
	{
		networking_message_peer_identity identity;

		error_code = this->connection_->get_remote_identity(identity);
		if (error_code != FT_ERR_SUCCESS
			|| identity.authenticated == FT_FALSE)
			return (FT_ERR_PERMISSION_DENIED);
	}
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = networking_replication_decode_message(message, envelope,
			payload);
	if (error_code == FT_ERR_SUCCESS
		&& (envelope.server_instance_id != this->server_instance_id_
			|| envelope.session_id != this->session_id_
			|| envelope.message_sequence != message.sequence))
		error_code = FT_ERR_PERMISSION_DENIED;
	if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::EDIT_INTENT))
		error_code = this->process_edit_intent(payload,
		envelope.message_sequence);
	else if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::CHUNK_REQUEST))
		error_code = this->process_chunk_request(payload);
	else if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::CHUNK_SYNC_REQUEST))
		error_code = this->process_chunk_sync_request(payload);
	else if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::CHUNK_INTEREST))
		error_code = this->process_chunk_interest(payload);
	else if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::CHUNK_ACKNOWLEDGEMENT))
		error_code = this->process_acknowledgement(payload);
	else if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::CHUNK_REPAIR_REQUEST))
		error_code = this->process_repair_request(payload);
	else if (error_code == FT_ERR_SUCCESS)
		error_code = FT_ERR_UNSUPPORTED_TYPE;
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationServerSession::destroy()
{
	this->service_ = nullptr;
	this->connection_ = nullptr;
	this->server_instance_id_ = 0U;
	this->session_id_ = 0U;
	this->next_message_sequence_ = 1U;
	this->subscription_count_ = 0U;
	this->delta_broadcast_callback_ = nullptr;
	this->delta_broadcast_user_data_ = nullptr;
	this->repair_request_callback_ = nullptr;
	this->repair_request_user_data_ = nullptr;
	this->sync_request_callback_ = nullptr;
	this->sync_request_user_data_ = nullptr;
	this->initialized_ = false;
	return (FT_ERR_SUCCESS);
}
