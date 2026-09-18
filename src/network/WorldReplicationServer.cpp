#include "../../src/network/WorldReplicationServer.hpp"

WorldReplicationServer::WorldReplicationServer()
	: service_(nullptr), peers_(), peer_active_(), delta_journal_(),
	light_journal_(), initialized_(false)
	, repair_request_callback_(nullptr), repair_request_user_data_(nullptr)
{
}

WorldReplicationServer::WorldReplicationServer(
	const WorldReplicationServer &other)
	: service_(nullptr), peers_(), peer_active_(), delta_journal_(),
	light_journal_(), initialized_(false)
	, repair_request_callback_(nullptr), repair_request_user_data_(nullptr)
{
	(void)other;
}

WorldReplicationServer::~WorldReplicationServer()
{
	if (this->destroy() != FT_ERR_SUCCESS)
		return ;
}

WorldReplicationServer &WorldReplicationServer::operator=(
	const WorldReplicationServer &other)
{
	(void)other;
	return (*this);
}

int32_t WorldReplicationServer::initialize(WorldReplicationService &service)
{
	int32_t error_code;
	int32_t cleanup_error;

	if (this->initialized_)
		return (FT_ERR_ALREADY_INITIALISED);
	if (service.server_instance_id() == 0U || service.world_id() == 0U)
		return (FT_ERR_INVALID_STATE);
	this->service_ = &service;
	error_code = this->delta_journal_.initialize(4096U);
	if (error_code != FT_ERR_SUCCESS)
	{
		this->service_ = nullptr;
		return (error_code);
	}
	error_code = this->light_journal_.initialize(4096U);
	if (error_code != FT_ERR_SUCCESS)
	{
		cleanup_error = this->delta_journal_.destroy();
		this->service_ = nullptr;
		if (cleanup_error != FT_ERR_SUCCESS)
			return (cleanup_error);
		return (error_code);
	}
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationServer::add_peer(
	networking_message_connection &connection, uint64_t session_id)
{
	return (this->add_peer_internal(connection, session_id, FT_FALSE));
}

int32_t WorldReplicationServer::add_authenticated_peer(
	networking_message_connection &connection, uint64_t session_id)
{
	return (this->add_peer_internal(connection, session_id, FT_TRUE));
}

int32_t WorldReplicationServer::add_peer_internal(
	networking_message_connection &connection, uint64_t session_id,
	ft_bool require_authenticated)
{
	uint32_t index;
	int32_t error_code;
	networking_message_peer_identity identity;

	if (!this->initialized_ || this->service_ == nullptr)
		return (FT_ERR_NOT_INITIALISED);
	if (connection.get_id() == 0U || session_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	if (require_authenticated != FT_FALSE)
	{
		if (connection.get_state()
			!= networking_message_connection_state::CONNECTED)
			return (FT_ERR_PERMISSION_DENIED);
		error_code = connection.get_remote_identity(identity);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		if (identity.authenticated == FT_FALSE)
			return (FT_ERR_PERMISSION_DENIED);
	}
	index = 0U;
	while (index < WORLD_REPLICATION_MAX_PEERS)
	{
		if (this->peer_active_[index] != 0U
			&& this->peers_[index].connection_id() == connection.get_id())
			return (FT_ERR_ALREADY_EXISTS);
		index += 1U;
	}
	index = 0U;
	while (index < WORLD_REPLICATION_MAX_PEERS
		&& this->peer_active_[index] == 0U)
		index += 1U;
	if (index == WORLD_REPLICATION_MAX_PEERS)
		return (FT_ERR_FULL);
	error_code = this->peers_[index].initialize(*this->service_, connection,
		this->service_->server_instance_id(), session_id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->peers_[index].set_delta_broadcast_callback(
			&WorldReplicationServer::broadcast_block_delta, this);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->peers_[index].set_sync_request_callback(
			&WorldReplicationServer::synchronize_request, this);
	if (error_code == FT_ERR_SUCCESS
		&& this->repair_request_callback_ != nullptr)
		error_code = this->peers_[index].set_repair_request_callback(
			this->repair_request_callback_, this->repair_request_user_data_);
	if (error_code != FT_ERR_SUCCESS)
	{
		int32_t cleanup_error = this->peers_[index].destroy();
		if (error_code == FT_ERR_SUCCESS)
			error_code = cleanup_error;
		return (error_code);
	}
	this->peer_active_[index] = 1U;
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationServer::set_repair_request_callback(
	world_replication_repair_request_callback callback, void *user_data)
{
	uint32_t index;
	int32_t error_code;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (callback == nullptr)
		return (FT_ERR_INVALID_POINTER);
	this->repair_request_callback_ = callback;
	this->repair_request_user_data_ = user_data;
	index = 0U;
	while (index < WORLD_REPLICATION_MAX_PEERS)
	{
		if (this->peer_active_[index] != 0U)
		{
			error_code = this->peers_[index].set_repair_request_callback(
				callback, user_data);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
		}
		index += 1U;
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationServer::remove_peer(uint64_t connection_id)
{
	uint32_t index;
	int32_t error_code;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (connection_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	index = 0U;
	while (index < WORLD_REPLICATION_MAX_PEERS)
	{
		if (this->peer_active_[index] != 0U
			&& this->peers_[index].connection_id() == connection_id)
		{
			error_code = this->peers_[index].destroy();
			this->peer_active_[index] = 0U;
			if (error_code == FT_ERR_SUCCESS)
				error_code = this->prune_acknowledged_history();
			return (error_code);
		}
		index += 1U;
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldReplicationServer::process_message(
	const networking_received_message &message)
{
	uint32_t index;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (message.connection_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	index = 0U;
	while (index < WORLD_REPLICATION_MAX_PEERS)
	{
		if (this->peer_active_[index] != 0U
			&& this->peers_[index].connection_id() == message.connection_id)
		{
			int32_t error_code;
			int32_t prune_error;

			error_code = this->peers_[index].process_message(message);
			prune_error = this->prune_acknowledged_history();
			if (error_code == FT_ERR_SUCCESS)
				error_code = prune_error;
			return (error_code);
		}
		index += 1U;
	}
	return (FT_ERR_NOT_FOUND);
}

int32_t WorldReplicationServer::synchronize_peer(uint64_t connection_id,
	int32_t chunk_x, int32_t chunk_z, uint64_t base_revision)
{
	uint32_t index;
	uint64_t latest_revision;
	uint64_t expected_revision;
	game_block_delta delta;
	int32_t error_code;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (connection_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	index = 0U;
	while (index < WORLD_REPLICATION_MAX_PEERS
		&& (this->peer_active_[index] == 0U
			|| this->peers_[index].connection_id() != connection_id))
		index += 1U;
	if (index == WORLD_REPLICATION_MAX_PEERS)
		return (FT_ERR_NOT_FOUND);
	if (this->peers_[index].is_subscribed(chunk_x, chunk_z) == FT_FALSE)
		return (FT_ERR_PERMISSION_DENIED);
	if (this->delta_journal_.latest_chunk_revision(chunk_x, chunk_z,
		latest_revision) != FT_ERR_SUCCESS)
		return (this->peers_[index].send_snapshot(chunk_x, chunk_z));
	if (base_revision > latest_revision)
		return (this->peers_[index].send_snapshot(chunk_x, chunk_z));
	if (this->delta_journal_.needs_snapshot(chunk_x, chunk_z,
		base_revision) == FT_TRUE)
		return (this->peers_[index].send_snapshot(chunk_x, chunk_z));
	if (base_revision >= latest_revision)
		return (FT_ERR_SUCCESS);
	expected_revision = base_revision;
	while (expected_revision < latest_revision)
	{
		error_code = this->delta_journal_.next_delta(chunk_x, chunk_z,
			expected_revision, delta);
		if (error_code != FT_ERR_SUCCESS)
			return (this->peers_[index].send_snapshot(chunk_x, chunk_z));
		error_code = this->peers_[index].publish_block_delta(delta);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		expected_revision = delta.revision;
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationServer::broadcast_light_delta(
	const ProtocolChunkLightDeltaMessage &delta) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (this->light_journal_.append(delta) != FT_ERR_SUCCESS)
		return (FT_ERR_INTERNAL);
	return (this->fanout_light_delta(delta));
}

int32_t WorldReplicationServer::broadcast_hash_manifest(
	const ProtocolChunkHashManifestMessage &manifest) noexcept
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->fanout_hash_manifest(manifest));
}

int32_t WorldReplicationServer::synchronize_light_peer(
	uint64_t connection_id, int32_t chunk_x, int32_t chunk_z,
	uint64_t base_revision)
{
	uint32_t index;
	uint64_t latest_revision;
	uint64_t expected_revision;
	ProtocolChunkLightDeltaMessage delta;
	int32_t error_code;

	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	if (connection_id == 0U)
		return (FT_ERR_INVALID_ARGUMENT);
	index = 0U;
	while (index < WORLD_REPLICATION_MAX_PEERS
		&& (this->peer_active_[index] == 0U
			|| this->peers_[index].connection_id() != connection_id))
		index += 1U;
	if (index == WORLD_REPLICATION_MAX_PEERS)
		return (FT_ERR_NOT_FOUND);
	if (this->peers_[index].is_subscribed(chunk_x, chunk_z) == FT_FALSE)
		return (FT_ERR_PERMISSION_DENIED);
	if (this->light_journal_.latest_chunk_revision(chunk_x, chunk_z,
		latest_revision) != FT_ERR_SUCCESS
		|| base_revision > latest_revision
		|| this->light_journal_.needs_snapshot(chunk_x, chunk_z,
			base_revision) == FT_TRUE)
		return (this->peers_[index].send_snapshot(chunk_x, chunk_z));
	if (base_revision >= latest_revision)
		return (FT_ERR_SUCCESS);
	expected_revision = base_revision;
	while (expected_revision < latest_revision)
	{
		error_code = this->light_journal_.next_delta(chunk_x, chunk_z,
			expected_revision, delta);
		if (error_code != FT_ERR_SUCCESS)
			return (this->peers_[index].send_snapshot(chunk_x, chunk_z));
		error_code = this->peers_[index].publish_light_delta(delta);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
		expected_revision = delta.final_light_revision;
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationServer::replay_next_block_delta(int32_t chunk_x,
	int32_t chunk_z, uint64_t base_revision, game_block_delta &delta) const
{
	if (!this->initialized_)
		return (FT_ERR_NOT_INITIALISED);
	return (this->delta_journal_.next_delta(chunk_x, chunk_z,
		base_revision, delta));
}

ft_bool WorldReplicationServer::needs_snapshot(int32_t chunk_x,
	int32_t chunk_z, uint64_t base_revision) const
{
	if (!this->initialized_)
		return (FT_TRUE);
	return (this->delta_journal_.needs_snapshot(chunk_x, chunk_z,
		base_revision));
}

int32_t WorldReplicationServer::broadcast_block_delta(
	const game_block_delta &delta, uint64_t source_connection_id,
	void *user_data) noexcept
{
	WorldReplicationServer *server;

	server = static_cast<WorldReplicationServer *>(user_data);
	if (server == nullptr)
		return (FT_ERR_INVALID_POINTER);
	if (server->delta_journal_.append(delta) != FT_ERR_SUCCESS)
		return (FT_ERR_INTERNAL);
	return (server->fanout_block_delta(delta, source_connection_id));
}

int32_t WorldReplicationServer::synchronize_request(
	const ProtocolChunkSyncRequestMessage &request, uint64_t connection_id,
	void *user_data) noexcept
{
	WorldReplicationServer *server;
	int32_t error_code;

	server = static_cast<WorldReplicationServer *>(user_data);
	if (server == nullptr)
		return (FT_ERR_INVALID_POINTER);
	error_code = server->synchronize_peer(connection_id, request.chunk_x,
		request.chunk_z, request.block_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = server->synchronize_light_peer(connection_id,
		request.chunk_x, request.chunk_z, request.light_revision);
	return (error_code);
}

int32_t WorldReplicationServer::fanout_block_delta(
	const game_block_delta &delta, uint64_t source_connection_id) noexcept
{
	uint32_t index;
	int32_t first_error;
	int32_t error_code;

	first_error = FT_ERR_SUCCESS;
	index = 0U;
	while (index < WORLD_REPLICATION_MAX_PEERS)
	{
		if (this->peer_active_[index] != 0U
			&& this->peers_[index].connection_id() != source_connection_id
			&& this->peers_[index].is_subscribed(delta.chunk_x,
				delta.chunk_z) == FT_TRUE)
		{
			error_code = this->peers_[index].publish_block_delta(delta);
			if (error_code != FT_ERR_SUCCESS
				&& first_error == FT_ERR_SUCCESS)
				first_error = error_code;
		}
		index += 1U;
	}
	return (first_error);
}

int32_t WorldReplicationServer::fanout_light_delta(
	const ProtocolChunkLightDeltaMessage &delta) noexcept
{
	uint32_t index;
	int32_t first_error;
	int32_t error_code;

	first_error = FT_ERR_SUCCESS;
	index = 0U;
	while (index < WORLD_REPLICATION_MAX_PEERS)
	{
		if (this->peer_active_[index] != 0U
			&& this->peers_[index].is_subscribed(delta.chunk_x,
				delta.chunk_z) == FT_TRUE)
		{
			error_code = this->peers_[index].publish_light_delta(delta);
			if (error_code != FT_ERR_SUCCESS
				&& first_error == FT_ERR_SUCCESS)
				first_error = error_code;
		}
		index += 1U;
	}
	return (first_error);
}

int32_t WorldReplicationServer::fanout_hash_manifest(
	const ProtocolChunkHashManifestMessage &manifest) noexcept
{
	uint32_t index;
	int32_t first_error;
	int32_t error_code;

	first_error = FT_ERR_SUCCESS;
	index = 0U;
	while (index < WORLD_REPLICATION_MAX_PEERS)
	{
		if (this->peer_active_[index] != 0U
			&& this->peers_[index].is_subscribed(manifest.chunk_x,
				manifest.chunk_z) == FT_TRUE)
		{
			error_code = this->peers_[index].publish_hash_manifest(manifest);
			if (first_error == FT_ERR_SUCCESS
				&& error_code != FT_ERR_SUCCESS)
				first_error = error_code;
		}
		index += 1U;
	}
	return (first_error);
}

int32_t WorldReplicationServer::prune_acknowledged_chunk(int32_t chunk_x,
	int32_t chunk_z) noexcept
{
	uint32_t index;
	uint64_t block_revision;
	uint64_t light_revision;
	uint64_t generation_epoch;
	uint8_t snapshot_acknowledged;
	uint64_t minimum_block_revision;
	uint64_t minimum_light_revision;
	bool found_peer;
	int32_t error_code;

	minimum_block_revision = UINT64_MAX;
	minimum_light_revision = UINT64_MAX;
	found_peer = false;
	index = 0U;
	while (index < WORLD_REPLICATION_MAX_PEERS)
	{
		if (this->peer_active_[index] != 0U
			&& this->peers_[index].is_subscribed(chunk_x, chunk_z) == FT_TRUE)
		{
			error_code = this->peers_[index].get_chunk_acknowledgement(
				chunk_x, chunk_z, block_revision, light_revision,
				generation_epoch, snapshot_acknowledged);
			if (error_code != FT_ERR_SUCCESS)
				return (error_code);
			if (snapshot_acknowledged == 0U)
				return (FT_ERR_SUCCESS);
			if (block_revision < minimum_block_revision)
				minimum_block_revision = block_revision;
			if (light_revision < minimum_light_revision)
				minimum_light_revision = light_revision;
			found_peer = true;
		}
		index += 1U;
	}
	if (!found_peer)
		return (FT_ERR_SUCCESS);
	error_code = this->delta_journal_.prune_through(chunk_x, chunk_z,
		minimum_block_revision);
	if (error_code == FT_ERR_SUCCESS)
		error_code = this->light_journal_.prune_through(chunk_x, chunk_z,
		minimum_light_revision);
	return (error_code);
}

int32_t WorldReplicationServer::prune_acknowledged_history() noexcept
{
	uint32_t peer_index;
	uint32_t subscription_index;
	int32_t chunk_x;
	int32_t chunk_z;
	int32_t error_code;

	peer_index = 0U;
	while (peer_index < WORLD_REPLICATION_MAX_PEERS)
	{
		if (this->peer_active_[peer_index] != 0U)
		{
			subscription_index = 0U;
			while (subscription_index
				< this->peers_[peer_index].subscription_count())
			{
				error_code = this->peers_[peer_index].subscription_at(
					subscription_index, chunk_x, chunk_z);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				error_code = this->prune_acknowledged_chunk(chunk_x, chunk_z);
				if (error_code != FT_ERR_SUCCESS)
					return (error_code);
				subscription_index += 1U;
			}
		}
		peer_index += 1U;
	}
	return (FT_ERR_SUCCESS);
}

int32_t WorldReplicationServer::destroy()
{
	uint32_t index;
	int32_t first_error;
	int32_t error_code;

	first_error = FT_ERR_SUCCESS;
	index = 0U;
	while (index < WORLD_REPLICATION_MAX_PEERS)
	{
		if (this->peer_active_[index] != 0U)
		{
			error_code = this->peers_[index].destroy();
			if (error_code != FT_ERR_SUCCESS
				&& first_error == FT_ERR_SUCCESS)
				first_error = error_code;
			this->peer_active_[index] = 0U;
		}
		index += 1U;
	}
	this->service_ = nullptr;
	this->repair_request_callback_ = nullptr;
	this->repair_request_user_data_ = nullptr;
	if (this->delta_journal_.destroy() != FT_ERR_SUCCESS
		&& first_error == FT_ERR_SUCCESS)
		first_error = FT_ERR_INTERNAL;
	if (this->light_journal_.destroy() != FT_ERR_SUCCESS
		&& first_error == FT_ERR_SUCCESS)
		first_error = FT_ERR_INTERNAL;
	this->initialized_ = false;
	return (first_error);
}
