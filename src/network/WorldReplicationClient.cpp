#include "../../src/network/WorldReplicationClient.hpp"
#include <new>

WorldReplicationClient::WorldReplicationClient()
	: chunk_revisions_(), apply_budget_(),
	snapshot_callback_(nullptr), block_delta_callback_(nullptr),
	light_delta_callback_(nullptr), edit_result_callback_(nullptr),
	boat_move_result_callback_(nullptr),
	entity_update_callback_(nullptr),
	world_item_snapshot_callback_(nullptr),
	repair_callback_(nullptr),
	hash_manifest_callback_(nullptr),
	callback_user_data_(nullptr), boat_move_result_callback_user_data_(nullptr),
	entity_update_callback_user_data_(nullptr),
	world_item_snapshot_callback_user_data_(nullptr),
	repair_callback_user_data_(nullptr),
	hash_manifest_callback_user_data_(nullptr),
	replica_store_(nullptr), world_item_replica_store_(nullptr),
	server_instance_id_(0U), session_id_(0U),
	world_id_(0U), received_messages_(), maximum_queued_messages_(0U),
	maximum_queued_bytes_(0U), queued_bytes_(0U), queue_initialized_(false),
	initialized_(false)
{
}

WorldReplicationClient::WorldReplicationClient(
	const WorldReplicationClient &other) : chunk_revisions_(), apply_budget_(),
	snapshot_callback_(nullptr), block_delta_callback_(nullptr),
	light_delta_callback_(nullptr), edit_result_callback_(nullptr),
	boat_move_result_callback_(nullptr),
	entity_update_callback_(nullptr),
	world_item_snapshot_callback_(nullptr),
	repair_callback_(nullptr),
	hash_manifest_callback_(nullptr),
	callback_user_data_(nullptr), boat_move_result_callback_user_data_(nullptr),
	entity_update_callback_user_data_(nullptr),
	world_item_snapshot_callback_user_data_(nullptr),
	repair_callback_user_data_(nullptr),
	hash_manifest_callback_user_data_(nullptr),
	replica_store_(nullptr), world_item_replica_store_(nullptr),
	server_instance_id_(0U), session_id_(0U),
	world_id_(0U), received_messages_(), maximum_queued_messages_(0U),
	maximum_queued_bytes_(0U), queued_bytes_(0U), queue_initialized_(false),
	initialized_(false)
{
	(void)other;
}

WorldReplicationClient::~WorldReplicationClient()
{
	if (this->destroy() != FT_ERR_SUCCESS)
		return ;
}

WorldReplicationClient &WorldReplicationClient::operator=(
	const WorldReplicationClient &other)
{
	(void)other;
	return (*this);
}

int32_t WorldReplicationClient::initialize(uint64_t server_instance_id,
	uint64_t session_id, uint64_t world_id, uint32_t maximum_messages,
	uint32_t maximum_payload_bytes, uint32_t maximum_operations)
{
	int32_t error_code;

	if (this->initialized_)
		return (FT_ERR_ALREADY_INITIALISED);
	if (server_instance_id == 0U || session_id == 0U || world_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (maximum_messages == 0U || maximum_payload_bytes == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = networking_replication_apply_budget_initialize(this->apply_budget_,
		maximum_messages, maximum_payload_bytes, maximum_operations);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = this->received_messages_.initialize();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->queue_initialized_ = true;
	this->server_instance_id_ = server_instance_id;
	this->session_id_ = session_id;
	this->world_id_ = world_id;
	this->maximum_queued_messages_ = maximum_messages;
	this->maximum_queued_bytes_ = maximum_payload_bytes;
	this->queued_bytes_ = 0U;
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationClient::set_callbacks(
	world_replication_snapshot_callback snapshot,
	world_replication_block_delta_callback block_delta,
	world_replication_light_delta_callback light_delta,
	world_replication_edit_result_callback edit_result, void *user_data) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (snapshot == nullptr || block_delta == nullptr || light_delta == nullptr
		|| edit_result == nullptr)
		return (FT_ERR_INVALID_POINTER);
	this->snapshot_callback_ = snapshot;
	this->block_delta_callback_ = block_delta;
	this->light_delta_callback_ = light_delta;
	this->edit_result_callback_ = edit_result;
	this->callback_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationClient::set_replica_store(
	WorldReplicationReplicaStore *store) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	this->replica_store_ = store;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationClient::set_repair_callback(
	world_replication_repair_callback repair, void *user_data) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (repair == nullptr)
		return (FT_ERR_INVALID_POINTER);
	this->repair_callback_ = repair;
	this->repair_callback_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationClient::set_hash_manifest_callback(
	world_replication_hash_manifest_callback callback, void *user_data) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (callback == nullptr)
		return (FT_ERR_INVALID_POINTER);
	this->hash_manifest_callback_ = callback;
	this->hash_manifest_callback_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationClient::set_boat_move_result_callback(
	world_replication_boat_move_result_callback callback,
	void *user_data) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (callback == nullptr)
		return (FT_ERR_INVALID_POINTER);
	this->boat_move_result_callback_ = callback;
	this->boat_move_result_callback_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationClient::set_entity_update_callback(
	world_replication_entity_update_callback callback,
	void *user_data) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (callback == nullptr)
		return (FT_ERR_INVALID_POINTER);
	this->entity_update_callback_ = callback;
	this->entity_update_callback_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationClient::set_world_item_snapshot_callback(
	world_replication_world_item_snapshot_callback callback,
	void *user_data) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (callback == nullptr)
		return (FT_ERR_INVALID_POINTER);
	this->world_item_snapshot_callback_ = callback;
	this->world_item_snapshot_callback_user_data_ = user_data;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationClient::set_world_item_replica_store(
	WorldItemReplicaStore *store) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (store == nullptr)
		return (FT_ERR_INVALID_POINTER);
	this->world_item_replica_store_ = store;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationClient::send_edit_intent(
	networking_message_connection &connection,
	const ProtocolEditIntentMessage &intent, uint64_t message_sequence) const
{
	ft_byte_buffer payload;
	networking_replication_sender sender;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (message_sequence == 0U
		|| intent.request.session_id != this->session_id_)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = intent.serialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = sender.send_reliable_control(connection,
		static_cast<uint16_t>(ProtocolMessageHeader::Type::EDIT_INTENT),
		payload, this->server_instance_id_, this->session_id_, message_sequence);
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationClient::send_boat_move_intent(
	networking_message_connection &connection,
	const ProtocolBoatMoveIntentMessage &intent,
	uint64_t message_sequence) const
{
	ft_byte_buffer payload;
	networking_replication_sender sender;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (message_sequence == 0U || intent.session_id != this->session_id_)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = intent.serialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = sender.send_reliable_control(connection,
		static_cast<uint16_t>(ProtocolMessageHeader::Type::BOAT_MOVE_INTENT),
		payload, this->server_instance_id_, this->session_id_, message_sequence);
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationClient::request_chunk(
	networking_message_connection &connection, int32_t chunk_x,
	int32_t chunk_z, uint64_t message_sequence) const
{
	ProtocolChunkRequestMessage request;
	ft_byte_buffer payload;
	networking_replication_sender sender;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_ || message_sequence == 0U)
		return (this->initialized_ ? FT_ERR_INVALID_ARGUMENT
			: FT_ERR_NOT_INITIALISED);
	request.chunk_x = chunk_x;
	request.chunk_z = chunk_z;
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = request.serialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = sender.send_reliable_control(connection,
		static_cast<uint16_t>(ProtocolMessageHeader::Type::CHUNK_REQUEST),
		payload, this->server_instance_id_, this->session_id_, message_sequence);
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationClient::request_chunk_sync(
	networking_message_connection &connection, int32_t chunk_x,
	int32_t chunk_z, uint64_t block_revision, uint64_t light_revision,
	uint64_t message_sequence) const
{
	ProtocolChunkSyncRequestMessage request;
	ft_byte_buffer payload;
	networking_replication_sender sender;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (message_sequence == 0U || this->session_id_ == 0U
		|| this->world_id_ == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	request.session_id = this->session_id_;
	request.world_id = this->world_id_;
	request.chunk_x = chunk_x;
	request.chunk_z = chunk_z;
	request.block_revision = block_revision;
	request.light_revision = light_revision;
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = request.serialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = sender.send_reliable_control(connection,
		static_cast<uint16_t>(ProtocolMessageHeader::Type::CHUNK_SYNC_REQUEST),
		payload, this->server_instance_id_, this->session_id_, message_sequence);
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationClient::set_chunk_interest(
	networking_message_connection &connection, int32_t chunk_x,
	int32_t chunk_z, uint8_t subscribed, uint64_t message_sequence) const
{
	ProtocolChunkInterestMessage interest;
	ft_byte_buffer payload;
	networking_replication_sender sender;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (message_sequence == 0U || subscribed > 1U)
		return (FT_ERR_INVALID_ARGUMENT);
	interest.chunk_x = chunk_x;
	interest.chunk_z = chunk_z;
	interest.subscribed = subscribed;
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = interest.serialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = sender.send_reliable_control(connection,
		static_cast<uint16_t>(ProtocolMessageHeader::Type::CHUNK_INTEREST),
		payload, this->server_instance_id_, this->session_id_, message_sequence);
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationClient::send_chunk_acknowledgement(
	networking_message_connection &connection, int32_t chunk_x,
	int32_t chunk_z, uint64_t block_revision, uint64_t light_revision,
	uint64_t generation_epoch, uint8_t snapshot_acknowledged,
	uint64_t message_sequence) const
{
	ProtocolChunkAcknowledgementMessage acknowledgement;
	ft_byte_buffer payload;
	networking_replication_sender sender;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (message_sequence == 0U || block_revision == 0U
		|| light_revision == 0U || generation_epoch == 0U
		|| snapshot_acknowledged > 1U)
		return (FT_ERR_INVALID_ARGUMENT);
	acknowledgement.protocol_version = GAME_WORLD_DELTA_PROTOCOL_VERSION;
	acknowledgement.session_id = this->session_id_;
	acknowledgement.world_id = this->world_id_;
	acknowledgement.chunk_x = chunk_x;
	acknowledgement.chunk_z = chunk_z;
	acknowledgement.block_revision = block_revision;
	acknowledgement.light_revision = light_revision;
	acknowledgement.generation_epoch = generation_epoch;
	acknowledgement.snapshot_acknowledged = snapshot_acknowledged;
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = acknowledgement.serialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = sender.send_reliable_control(connection,
		static_cast<uint16_t>(ProtocolMessageHeader::Type::CHUNK_ACKNOWLEDGEMENT),
		payload, this->server_instance_id_, this->session_id_, message_sequence);
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationClient::send_chunk_repair_request(
	networking_message_connection &connection,
	const ProtocolChunkRepairRequestMessage &request,
	uint64_t message_sequence) const
{
	ft_byte_buffer payload;
	networking_replication_sender sender;
	int32_t error_code;
	int32_t destroy_error;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (message_sequence == 0U || request.session_id != this->session_id_
		|| request.world_id != this->world_id_)
		return (FT_ERR_INVALID_ARGUMENT);
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = request.serialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = sender.send_reliable_control(connection,
		static_cast<uint16_t>(ProtocolMessageHeader::Type::CHUNK_REPAIR_REQUEST),
		payload, this->server_instance_id_, this->session_id_, message_sequence);
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

uint64_t WorldReplicationClient::server_instance_id() const noexcept
{
	return (this->server_instance_id_);
}

uint64_t WorldReplicationClient::session_id() const noexcept
{
	return (this->session_id_);
}

int32_t WorldReplicationClient::reset_budget()
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (networking_replication_apply_budget_reset(this->apply_budget_));
}

int32_t WorldReplicationClient::enqueue_received_message(
	const networking_received_message &message)
{
	networking_received_message *queued_message;
	int32_t error_code;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (message.payload.size() > this->maximum_queued_bytes_
		|| this->queued_bytes_ > this->maximum_queued_bytes_
			- message.payload.size()
		|| this->received_messages_.size() >= this->maximum_queued_messages_)
		return (FT_ERR_FULL);
	queued_message = new (std::nothrow) networking_received_message();
	if (queued_message == nullptr)
		return (FT_ERR_NO_MEMORY);
	queued_message->connection_id = message.connection_id;
	queued_message->channel = message.channel;
	queued_message->lane = message.lane;
	queued_message->delivery = message.delivery;
	queued_message->sequence = message.sequence;
	error_code = queued_message->payload.initialize(message.payload);
	if (error_code != FT_ERR_SUCCESS)
	{
		delete queued_message;
		return (error_code);
	}
	this->received_messages_.push_back(queued_message);
	error_code = this->received_messages_.get_error();
	if (error_code != FT_ERR_SUCCESS)
	{
		this->received_messages_.pop_back();
		delete queued_message;
		return (error_code);
	}
	this->queued_bytes_ += message.payload.size();
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationClient::drain_received_messages(
	uint32_t maximum_messages, uint32_t &processed_messages)
{
	networking_received_message *message;
	int32_t error_code;

	processed_messages = 0U;
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (maximum_messages == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	while (processed_messages < maximum_messages
		&& !this->received_messages_.empty())
	{
		message = this->received_messages_.front();
		if (message == nullptr)
			return (FT_ERR_INTERNAL);
		bool deferred_for_budget = false;
		error_code = this->apply_received_message(*message, deferred_for_budget);
		if (deferred_for_budget)
			return (FT_ERR_SUCCESS);
		this->received_messages_.pop_front();
		if (this->queued_bytes_ >= message->payload.size())
			this->queued_bytes_ -= message->payload.size();
		else
			this->queued_bytes_ = 0U;
		delete message;
		processed_messages += 1U;
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
	}
	return (FT_ERR_SUCCESS);
}

void WorldReplicationClient::clear_received_messages()
{
	networking_received_message *message;

	if (!this->queue_initialized_)
		return ;
	while (!this->received_messages_.empty())
	{
		message = this->received_messages_.pop_front();
		delete message;
	}
	this->queued_bytes_ = 0U;
}

int32_t WorldReplicationClient::apply_snapshot(
	const ProtocolChunkSnapshotMessage &snapshot)
{
	if (snapshot_callback_ == nullptr)
		return (FT_ERR_INVALID_POINTER);
	if (snapshot.session_id != session_id_ || snapshot.world_id != world_id_)
		return (FT_ERR_PERMISSION_DENIED);
	if (snapshot.block_revision == 0U || snapshot.light_revision == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	try
	{
		const auto key = std::make_pair(snapshot.chunk_x, snapshot.chunk_z);
		auto entry = chunk_revisions_.try_emplace(key);
		auto &revision = entry.first->second;
		int32_t error_code = FT_ERR_SUCCESS;
		if (entry.second)
			error_code = revision.initialize();
		if (error_code == FT_ERR_SUCCESS && replica_store_ != nullptr)
			error_code = replica_store_->apply_snapshot(snapshot);
		if (error_code == FT_ERR_SUCCESS && replica_store_ == nullptr)
			error_code = snapshot_callback_(snapshot, callback_user_data_);
		if (error_code == FT_ERR_SUCCESS)
			error_code = revision.accept_snapshot(snapshot.block_revision,
				snapshot.light_revision, snapshot.generation_epoch);
		if (error_code != FT_ERR_SUCCESS && entry.second)
			chunk_revisions_.erase(entry.first);
		if (error_code == FT_ERR_SUCCESS && replica_store_ != nullptr)
			error_code = snapshot_callback_(snapshot, callback_user_data_);
		return (error_code);
	}
	catch (const std::bad_alloc &)
	{
		return (FT_ERR_NO_MEMORY);
	}
}

int32_t WorldReplicationClient::apply_block_delta(
	const ProtocolChunkBlockDeltaMessage &message, uint32_t payload_size,
	bool &deferred_for_budget)
{
	const game_block_delta &delta = message.delta;
	if (block_delta_callback_ == nullptr)
		return (FT_ERR_INVALID_POINTER);
	if (delta.session_id != session_id_ || delta.world_id != world_id_)
		return (FT_ERR_PERMISSION_DENIED);
	auto entry = chunk_revisions_.find({delta.chunk_x, delta.chunk_z});
	if (entry == chunk_revisions_.end()
		|| entry->second.is_snapshot_ready() == FT_FALSE)
		return (FT_ERR_INVALID_STATE);
	/* A cached edit result can resend a block delta after the replica already
	 * reached it. Discard the replay ahead of the per-frame apply budget. */
	if (delta.revision <= entry->second.get_block_revision())
		return (FT_ERR_SUCCESS);
	if (entry->second.can_accept_block_delta(delta.previous_revision,
			delta.revision) == FT_FALSE)
		return (FT_ERR_INVALID_STATE);
	int32_t error_code = networking_replication_apply_budget_consume(
		this->apply_budget_, payload_size, 1U);
	if (error_code != FT_ERR_SUCCESS)
	{
		deferred_for_budget = error_code == FT_ERR_FULL;
		return (error_code);
	}
	if (replica_store_ != nullptr)
		error_code = replica_store_->apply_block_delta(message);
	else
		error_code = block_delta_callback_(message, callback_user_data_);
	if (error_code == FT_ERR_SUCCESS)
		error_code = entry->second.accept_block_delta(delta.previous_revision,
			delta.revision);
	if (error_code == FT_ERR_SUCCESS && replica_store_ != nullptr)
		error_code = block_delta_callback_(message, callback_user_data_);
	return (error_code);
}

int32_t WorldReplicationClient::apply_light_delta(
	const ProtocolChunkLightDeltaMessage &delta, uint32_t payload_size,
	bool &deferred_for_budget)
{
	if (light_delta_callback_ == nullptr)
		return (FT_ERR_INVALID_POINTER);
	if (delta.session_id != session_id_ || delta.world_id != world_id_)
		return (FT_ERR_PERMISSION_DENIED);
	auto entry = chunk_revisions_.find({delta.chunk_x, delta.chunk_z});
	if (entry == chunk_revisions_.end()
		|| entry->second.is_snapshot_ready() == FT_FALSE
		|| entry->second.get_snapshot_generation() != delta.generation_epoch)
		return (FT_ERR_INVALID_STATE);
	/* A replay from the current generation cannot advance light that the
	 * replica already published. Keep the apply slot for the next real edit. */
	if (delta.final_light_revision <= entry->second.get_light_revision())
		return (FT_ERR_SUCCESS);
	if (entry->second.can_accept_light_delta(delta.base_light_revision,
			delta.final_light_revision, delta.source_block_revision) == FT_FALSE)
		return (FT_ERR_INVALID_STATE);
	int32_t error_code = networking_replication_apply_budget_consume(
		this->apply_budget_, payload_size, 1U);
	if (error_code != FT_ERR_SUCCESS)
	{
		deferred_for_budget = error_code == FT_ERR_FULL;
		return (error_code);
	}
	if (replica_store_ != nullptr)
		error_code = replica_store_->apply_light_delta(delta);
	else
		error_code = light_delta_callback_(delta, callback_user_data_);
	if (error_code == FT_ERR_SUCCESS)
		error_code = entry->second.accept_light_delta(delta.base_light_revision,
			delta.final_light_revision, delta.source_block_revision);
	if (error_code == FT_ERR_SUCCESS && replica_store_ != nullptr)
		error_code = light_delta_callback_(delta, callback_user_data_);
	return (error_code);
}

int32_t WorldReplicationClient::apply_edit_result_payload(
	const ft_byte_buffer &payload)
{
	ProtocolEditResultMessage result;
	ft_byte_buffer decoded_payload;
	int32_t error_code;
	int32_t destroy_error;

	if (this->edit_result_callback_ == nullptr)
		return (FT_ERR_INVALID_POINTER);
	error_code = decoded_payload.initialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = result.deserialize(decoded_payload);
	if (error_code == FT_ERR_SUCCESS
		&& (result.session_id != this->session_id_
			|| (result.accepted != 0U
				&& result.delta.world_id != this->world_id_)))
		error_code = FT_ERR_PERMISSION_DENIED;
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->edit_result_callback_(result,
			this->callback_user_data_);
	destroy_error = decoded_payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationClient::apply_boat_move_result_payload(
	const ft_byte_buffer &payload)
{
	ProtocolBoatMoveResultMessage result;
	ft_byte_buffer decoded_payload;
	int32_t error_code;
	int32_t destroy_error;

	if (this->boat_move_result_callback_ == nullptr)
		return (FT_ERR_INVALID_POINTER);
	error_code = decoded_payload.initialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = result.deserialize(decoded_payload);
	if (error_code == FT_ERR_SUCCESS && result.session_id != this->session_id_)
		error_code = FT_ERR_PERMISSION_DENIED;
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->boat_move_result_callback_(result,
			this->boat_move_result_callback_user_data_);
	destroy_error = decoded_payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationClient::apply_entity_update_payload(
	const ft_byte_buffer &payload)
{
	ProtocolEntityUpdateMessage update;
	ft_byte_buffer decoded_payload;
	int32_t error_code;
	int32_t destroy_error;

	if (this->entity_update_callback_ == nullptr)
		return (FT_ERR_INVALID_POINTER);
	error_code = decoded_payload.initialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = update.deserialize(decoded_payload);
	if (error_code == FT_ERR_SUCCESS && decoded_payload.remaining() != 0U)
		error_code = FT_ERR_INVALID_ARGUMENT;
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->entity_update_callback_(update.entity,
			this->entity_update_callback_user_data_);
	destroy_error = decoded_payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationClient::apply_world_item_snapshot_payload(
	const ft_byte_buffer &payload)
{
	ProtocolWorldItemSnapshotMessage snapshot;
	ft_byte_buffer decoded;
	int32_t error;
	int32_t destroy_error;

	if (this->world_item_snapshot_callback_ == nullptr
		&& this->world_item_replica_store_ == nullptr)
		return (FT_ERR_INVALID_POINTER);
	error = decoded.initialize(payload);
	if (error == FT_ERR_SUCCESS) error = snapshot.deserialize(decoded);
	if (error == FT_ERR_SUCCESS && decoded.remaining() != 0U)
		error = FT_ERR_INVALID_ARGUMENT;
	if (error == FT_ERR_SUCCESS && snapshot.session_id != this->session_id_)
		error = FT_ERR_PERMISSION_DENIED;
	if (error == FT_ERR_SUCCESS && snapshot.world_id != this->world_id_)
		error = FT_ERR_PERMISSION_DENIED;
	if (error == FT_ERR_SUCCESS && this->world_item_replica_store_ != nullptr)
		error = this->world_item_replica_store_->replace_chunk(snapshot);
	if (error == FT_ERR_SUCCESS)
	{
		if (this->world_item_snapshot_callback_ != nullptr)
			error = this->world_item_snapshot_callback_(snapshot,
				this->world_item_snapshot_callback_user_data_);
	}
	destroy_error = decoded.destroy();
	if (error == FT_ERR_SUCCESS) error = destroy_error;
	return (error);
}

int32_t WorldReplicationClient::apply_repair_response_payload(
	const ft_byte_buffer &payload)
{
	ProtocolChunkRepairResponseMessage response;
	ft_byte_buffer decoded_payload;
	ft_byte_buffer repair_bytes;
	uint8_t calculated_hash[PROTOCOL_CHUNK_HASH_SIZE];
	int32_t error_code;
	int32_t destroy_error;

	if (this->repair_callback_ == nullptr)
		return (FT_ERR_INVALID_POINTER);
	error_code = decoded_payload.initialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = response.deserialize(decoded_payload);
	if (error_code == FT_ERR_SUCCESS
		&& (response.session_id != this->session_id_
			|| response.world_id != this->world_id_))
		error_code = FT_ERR_PERMISSION_DENIED;
	if (error_code == FT_ERR_SUCCESS
		&& response.payload_format
			== PROTOCOL_CHUNK_REPAIR_PAYLOAD_CANONICAL_BLOCKS)
	{
		error_code = repair_bytes.initialize();
		if (error_code == FT_ERR_SUCCESS && !response.repair_payload.empty())
			error_code = repair_bytes.append(&response.repair_payload[0],
				response.repair_payload.size());
		if (error_code == FT_ERR_SUCCESS)
			error_code = networking_replication_hash_payload(repair_bytes,
				calculated_hash);
		if (error_code == FT_ERR_SUCCESS
			&& ft_memcmp(calculated_hash, response.content_hash,
				PROTOCOL_CHUNK_HASH_SIZE) != 0)
			error_code = FT_ERR_INVALID_ARGUMENT;
	}
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->repair_callback_(response,
		this->repair_callback_user_data_);
	destroy_error = repair_bytes.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	destroy_error = decoded_payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationClient::apply_hash_manifest_payload(
	const ft_byte_buffer &payload)
{
	ProtocolChunkHashManifestMessage manifest;
	ft_byte_buffer decoded_payload;
	int32_t error_code;
	int32_t destroy_error;

	if (this->hash_manifest_callback_ == nullptr)
		return (FT_ERR_INVALID_POINTER);
	error_code = decoded_payload.initialize(payload);
	if (error_code == FT_ERR_SUCCESS)
		error_code = manifest.deserialize(decoded_payload);
	if (error_code == FT_ERR_SUCCESS
		&& (manifest.session_id != this->session_id_
			|| manifest.world_id != this->world_id_))
		error_code = FT_ERR_PERMISSION_DENIED;
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->hash_manifest_callback_(manifest,
		this->hash_manifest_callback_user_data_);
	destroy_error = decoded_payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationClient::apply_received_message(
	const networking_received_message &message)
{
	bool deferred_for_budget = false;
	return (this->apply_received_message(message, deferred_for_budget));
}

int32_t WorldReplicationClient::apply_received_message(
	const networking_received_message &message, bool &deferred_for_budget)
{
	networking_replication_envelope envelope;
	ft_byte_buffer payload;
	ProtocolChunkSnapshotMessage snapshot;
	ProtocolChunkBlockDeltaMessage block_delta;
	ProtocolChunkLightDeltaMessage light_delta;
	int32_t error_code;
	int32_t destroy_error;

	deferred_for_budget = false;
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	error_code = payload.initialize();
	if (error_code == FT_ERR_SUCCESS)
		error_code = networking_replication_decode_message(message, envelope,
			payload);
	if (error_code == FT_ERR_SUCCESS
		&& (envelope.server_instance_id != this->server_instance_id_
			|| envelope.session_id != this->session_id_))
		error_code = FT_ERR_PERMISSION_DENIED;
	if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::CHUNK_SNAPSHOT))
	{
		if (envelope.payload_size > this->maximum_queued_bytes_)
			error_code = FT_ERR_OUT_OF_RANGE;
		else
		{
			error_code = networking_replication_apply_budget_consume(
				this->apply_budget_, envelope.payload_size, 1U);
			/* Only budget exhaustion is retryable without repeating application
			 * side effects. Leave the message at the queue head for next frame. */
			deferred_for_budget = error_code == FT_ERR_FULL;
		}
	}
	if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::CHUNK_SNAPSHOT))
	{
		error_code = snapshot.deserialize(payload);
		if (error_code == FT_ERR_SUCCESS)
			error_code = this->apply_snapshot(snapshot);
	}
	else if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::CHUNK_BLOCK_DELTA))
	{
		error_code = envelope.payload_size <= this->maximum_queued_bytes_
			? block_delta.deserialize(payload) : FT_ERR_OUT_OF_RANGE;
		if (error_code == FT_ERR_SUCCESS)
			error_code = this->apply_block_delta(block_delta,
				envelope.payload_size, deferred_for_budget);
	}
	else if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::CHUNK_LIGHT_DELTA))
	{
		error_code = envelope.payload_size <= this->maximum_queued_bytes_
			? light_delta.deserialize(payload) : FT_ERR_OUT_OF_RANGE;
		if (error_code == FT_ERR_SUCCESS)
			error_code = this->apply_light_delta(light_delta,
				envelope.payload_size, deferred_for_budget);
	}
	else if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::EDIT_RESULT))
		error_code = this->apply_edit_result_payload(payload);
	else if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::BOAT_MOVE_RESULT))
		error_code = this->apply_boat_move_result_payload(payload);
	else if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::ENTITY_UPDATE))
		error_code = this->apply_entity_update_payload(payload);
	else if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::WORLD_ITEM_SNAPSHOT))
		error_code = this->apply_world_item_snapshot_payload(payload);
	else if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::CHUNK_REPAIR_RESPONSE))
		error_code = this->apply_repair_response_payload(payload);
	else if (error_code == FT_ERR_SUCCESS
		&& envelope.message_type == static_cast<uint16_t>(
			ProtocolMessageHeader::Type::CHUNK_HASH_MANIFEST))
		error_code = this->apply_hash_manifest_payload(payload);
	else if (error_code == FT_ERR_SUCCESS)
		error_code = FT_ERR_UNSUPPORTED_TYPE;
	destroy_error = payload.destroy();
	if (error_code == FT_ERR_SUCCESS)
		error_code = destroy_error;
	return (error_code);
}

int32_t WorldReplicationClient::destroy()
{
	int32_t error_code;

	this->clear_received_messages();
	error_code = FT_ERR_SUCCESS;
	if (this->queue_initialized_)
	{
		error_code = this->received_messages_.destroy();
		this->queue_initialized_ = false;
	}
	this->chunk_revisions_.clear();
	this->snapshot_callback_ = nullptr;
	this->block_delta_callback_ = nullptr;
	this->light_delta_callback_ = nullptr;
	this->edit_result_callback_ = nullptr;
	this->boat_move_result_callback_ = nullptr;
	this->entity_update_callback_ = nullptr;
	this->world_item_snapshot_callback_ = nullptr;
	this->repair_callback_ = nullptr;
	this->hash_manifest_callback_ = nullptr;
	this->callback_user_data_ = nullptr;
	this->boat_move_result_callback_user_data_ = nullptr;
	this->entity_update_callback_user_data_ = nullptr;
	this->world_item_snapshot_callback_user_data_ = nullptr;
	this->repair_callback_user_data_ = nullptr;
	this->hash_manifest_callback_user_data_ = nullptr;
	this->replica_store_ = nullptr;
	this->world_item_replica_store_ = nullptr;
	this->server_instance_id_ = 0U;
	this->session_id_ = 0U;
	this->world_id_ = 0U;
	this->maximum_queued_messages_ = 0U;
	this->maximum_queued_bytes_ = 0U;
	this->queued_bytes_ = 0U;
	this->initialized_ = false;
	return (error_code);
}
