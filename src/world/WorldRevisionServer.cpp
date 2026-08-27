#include "../../src/world/WorldRevisionServer.hpp"

WorldRevisionServer::WorldRevisionServer(World &world) : world_(&world),
	server_(), initialized_(false)
{
}

WorldRevisionServer::WorldRevisionServer(const WorldRevisionServer &other) : world_(nullptr),
	server_(), initialized_(false)
{
	(void)other;
}

WorldRevisionServer::~WorldRevisionServer()
{
	this->destroy();
}

WorldRevisionServer &WorldRevisionServer::operator=(const WorldRevisionServer &other)
{
	(void)other;
	return (*this);
}

int32_t WorldRevisionServer::initialize()
{
	int32_t	error_code;

	if (this->world_ == nullptr || this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	error_code = this->server_.initialize();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->server_.set_message_callback(&WorldRevisionServer::handle_message,
		this);
	this->initialized_ = true;
	return (FT_ERR_SUCCESS);
}

int32_t WorldRevisionServer::start(const char *ip, uint16_t port)
{
	if (!this->initialized_)
		return (FT_ERR_INVALID_OPERATION);
	return (this->server_.start(ip, port));
}

void WorldRevisionServer::run_once()
{
	if (this->initialized_)
		this->server_.run_once();
}

void WorldRevisionServer::destroy()
{
	if (this->initialized_)
		(void)this->server_.destroy();
	this->initialized_ = false;
}

int32_t WorldRevisionServer::handle_message(int32_t client_handle,
	const char *message, void *user_data)
{
	(void)client_handle;
	if (user_data == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (static_cast<WorldRevisionServer *>(user_data)->handle_revision_message(message));
}

int32_t WorldRevisionServer::apply_biome_size_range_override(json_group *revision,
	terrain_generation_config &revision_config)
{
	json_item	*size_min;
	json_item	*size_max;

	size_min = json_find_item(revision, "biome_size_min");
	size_max = json_find_item(revision, "biome_size_max");
	if (size_min == nullptr && size_max == nullptr)
		return (FT_ERR_SUCCESS);
	if (size_min == nullptr || size_max == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	return (revision_config.set_biome_size_range(ft_atoi(size_min->value),
			ft_atoi(size_max->value)));
}

int32_t WorldRevisionServer::apply_biome_specific_override(json_group *revision,
	terrain_generation_config &revision_config)
{
	json_item	*biome_index;
	json_item	*biome_min;
	json_item	*biome_max;
	json_item	*biome_override;
	uint32_t	index;
	int32_t		enabled;
	int32_t		result;

	biome_index = json_find_item(revision, "biome_index");
	biome_min = json_find_item(revision, "biome_min");
	biome_max = json_find_item(revision, "biome_max");
	biome_override = json_find_item(revision, "biome_size_override_enabled");
	if (biome_index == nullptr && biome_min == nullptr && biome_max == nullptr
		&& biome_override == nullptr)
		return (FT_ERR_SUCCESS);
	if (biome_index == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	index = static_cast<uint32_t>(ft_atoi(biome_index->value));
	if (index >= TERRAIN_MAX_CUSTOM_BIOMES)
		return (FT_ERR_INVALID_ARGUMENT);
	result = FT_ERR_SUCCESS;
	if (biome_min != nullptr || biome_max != nullptr)
	{
		if (biome_min == nullptr || biome_max == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		result = revision_config.set_biome_size_range_for_biome(index,
				ft_atoi(biome_min->value), ft_atoi(biome_max->value));
	}
	if (result == FT_ERR_SUCCESS && biome_override != nullptr)
	{
		enabled = ft_atoi(biome_override->value);
		if (enabled < 0 || enabled > 1)
			return (FT_ERR_INVALID_ARGUMENT);
		result = revision_config.set_biome_size_override_enabled(index,
				static_cast<ft_bool>(enabled));
	}
	return (result);
}

int32_t WorldRevisionServer::apply_biome_config_overrides(json_group *revision,
	terrain_generation_config &revision_config)
{
	json_item	*sizes_enabled;
	int32_t		enabled;
	int32_t		result;

	sizes_enabled = json_find_item(revision, "biome_sizes_enabled");
	result = FT_ERR_SUCCESS;
	if (sizes_enabled != nullptr)
	{
		enabled = ft_atoi(sizes_enabled->value);
		if (enabled < 0 || enabled > 1)
			return (FT_ERR_INVALID_ARGUMENT);
		result = revision_config.set_biome_size_control_enabled(static_cast<ft_bool>(enabled));
	}
	if (result == FT_ERR_SUCCESS)
		result = this->apply_biome_size_range_override(revision,
				revision_config);
	if (result == FT_ERR_SUCCESS)
		result = this->apply_biome_specific_override(revision, revision_config);
	return (result);
}

int32_t WorldRevisionServer::handle_begin_action(json_group *revision)
{
	json_item					*mode_item;
	int32_t						mode;
	terrain_generation_config	revision_config;
	int32_t						result;

	mode_item = json_find_item(revision, "mode");
	mode = mode_item == nullptr ? -1 : ft_atoi(mode_item->value);
	if (mode < World::REGEN_DECORATION_REFRESH || mode > World::REGEN_FULL)
		return (FT_ERR_INVALID_ARGUMENT);
	result = revision_config.initialize(this->world_->terrain_generation_settings());
	if (result == FT_ERR_SUCCESS)
		result = this->apply_biome_config_overrides(revision, revision_config);
	if (result != FT_ERR_SUCCESS)
		return (result);
	return (this->world_->begin_world_revision(revision_config,
			static_cast<World::RegenerationMode>(mode)));
}

int32_t WorldRevisionServer::handle_select_or_protect_action(json_group *revision,
	const char *action)
{
	json_item	*x_item;
	json_item	*z_item;
	int32_t		chunk_x;
	int32_t		chunk_z;

	x_item = json_find_item(revision, "chunk_x");
	z_item = json_find_item(revision, "chunk_z");
	if (x_item == nullptr || z_item == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	chunk_x = ft_atoi(x_item->value);
	chunk_z = ft_atoi(z_item->value);
	if (std::strcmp(action, "select") == 0)
		return (this->world_->select_revision_chunk(chunk_x, chunk_z, true));
	return (this->world_->set_chunk_protected(chunk_x, chunk_z, true));
}

int32_t WorldRevisionServer::handle_revision_message(const char *message)
{
	json_group	*groups;
	json_group	*revision;
	json_item	*action_item;
	const char	*action;
	int32_t		result;

	if (message == nullptr || this->world_ == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	groups = json_read_from_string(message);
	if (groups == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	revision = json_find_group(groups, "world_revision");
	action_item = revision == nullptr ? nullptr : json_find_item(revision,
			"action");
	if (action_item == nullptr)
	{
		json_free_groups(groups);
		return (FT_ERR_INVALID_ARGUMENT);
	}
	action = action_item->value;
	result = FT_ERR_INVALID_ARGUMENT;
	if (std::strcmp(action, "begin") == 0)
		result = this->handle_begin_action(revision);
	else if (std::strcmp(action, "select") == 0 || std::strcmp(action,
			"protect") == 0)
		result = this->handle_select_or_protect_action(revision, action);
	else if (std::strcmp(action, "confirm") == 0)
		result = this->world_->start_revision_regeneration();
	json_free_groups(groups);
	return (result);
}
