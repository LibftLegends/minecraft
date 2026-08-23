#include "WorldRevisionServer.hpp"

WorldRevisionServer::WorldRevisionServer(World &world)
    : world_(&world), server_(), initialized_(false)
{
}

WorldRevisionServer::~WorldRevisionServer()
{
    this->destroy();
}

int32_t WorldRevisionServer::initialize()
{
    if (this->world_ == nullptr || this->initialized_)
        return (FT_ERR_INVALID_OPERATION);
    int32_t error_code = this->server_.initialize();
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    this->server_.set_message_callback(&WorldRevisionServer::handle_message, this);
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

int32_t WorldRevisionServer::handle_revision_message(const char *message)
{
    if (message == nullptr || this->world_ == nullptr)
        return (FT_ERR_INVALID_ARGUMENT);
    json_group *groups = json_read_from_string(message);
    if (groups == nullptr)
        return (FT_ERR_INVALID_ARGUMENT);
    json_group *revision = json_find_group(groups, "world_revision");
    json_item *action_item = revision == nullptr ? nullptr
        : json_find_item(revision, "action");
    if (action_item == nullptr)
    {
        json_free_groups(groups);
        return (FT_ERR_INVALID_ARGUMENT);
    }
    const char *action = action_item->value;
    int32_t result = FT_ERR_INVALID_ARGUMENT;
    if (std::strcmp(action, "begin") == 0)
    {
        json_item *mode_item = json_find_item(revision, "mode");
        int32_t mode = mode_item == nullptr ? -1 : ft_atoi(mode_item->value);
        if (mode >= World::REGEN_DECORATION_REFRESH && mode <= World::REGEN_FULL)
        {
            terrain_generation_config revision_config;
            result = revision_config.initialize(
                this->world_->terrain_generation_settings());
            json_item *sizes_enabled = json_find_item(revision,
                "biome_sizes_enabled");
            json_item *size_min = json_find_item(revision, "biome_size_min");
            json_item *size_max = json_find_item(revision, "biome_size_max");
            if (result == FT_ERR_SUCCESS && sizes_enabled != nullptr)
            {
                int32_t enabled = ft_atoi(sizes_enabled->value);
                if (enabled < 0 || enabled > 1)
                    result = FT_ERR_INVALID_ARGUMENT;
                else
                    result = revision_config.set_biome_size_control_enabled(
                        static_cast<ft_bool>(enabled));
            }
            if (result == FT_ERR_SUCCESS && (size_min != nullptr
                || size_max != nullptr))
            {
                if (size_min == nullptr || size_max == nullptr)
                    result = FT_ERR_INVALID_ARGUMENT;
                else
                    result = revision_config.set_biome_size_range(
                        ft_atoi(size_min->value), ft_atoi(size_max->value));
            }
            json_item *biome_index = json_find_item(revision, "biome_index");
            json_item *biome_min = json_find_item(revision, "biome_min");
            json_item *biome_max = json_find_item(revision, "biome_max");
            json_item *biome_override = json_find_item(revision,
                "biome_size_override_enabled");
            if (result == FT_ERR_SUCCESS && (biome_index != nullptr
                || biome_min != nullptr || biome_max != nullptr
                || biome_override != nullptr))
            {
                uint32_t index = biome_index == nullptr ? TERRAIN_MAX_CUSTOM_BIOMES
                    : static_cast<uint32_t>(ft_atoi(biome_index->value));
                if (biome_index == nullptr || index >= TERRAIN_MAX_CUSTOM_BIOMES)
                    result = FT_ERR_INVALID_ARGUMENT;
                else if (biome_min != nullptr || biome_max != nullptr)
                {
                    if (biome_min == nullptr || biome_max == nullptr)
                        result = FT_ERR_INVALID_ARGUMENT;
                    else
                        result = revision_config.set_biome_size_range_for_biome(
                            index, ft_atoi(biome_min->value),
                            ft_atoi(biome_max->value));
                }
                if (result == FT_ERR_SUCCESS && biome_override != nullptr)
                {
                    int32_t enabled = ft_atoi(biome_override->value);
                    if (enabled < 0 || enabled > 1)
                        result = FT_ERR_INVALID_ARGUMENT;
                    else
                        result = revision_config.set_biome_size_override_enabled(
                            index, static_cast<ft_bool>(enabled));
                }
            }
            if (result == FT_ERR_SUCCESS)
                result = this->world_->begin_world_revision(revision_config,
                    static_cast<World::RegenerationMode>(mode));
        }
    }
    else if (std::strcmp(action, "select") == 0 || std::strcmp(action, "protect") == 0)
    {
        json_item *x_item = json_find_item(revision, "chunk_x");
        json_item *z_item = json_find_item(revision, "chunk_z");
        if (x_item != nullptr && z_item != nullptr)
        {
            const int32_t chunk_x = ft_atoi(x_item->value);
            const int32_t chunk_z = ft_atoi(z_item->value);
            if (std::strcmp(action, "select") == 0)
                result = this->world_->select_revision_chunk(chunk_x, chunk_z, true);
            else
                result = this->world_->set_chunk_protected(chunk_x, chunk_z, true);
        }
    }
    else if (std::strcmp(action, "confirm") == 0)
    {
        result = this->world_->start_revision_regeneration();
    }
    json_free_groups(groups);
    return (result);
}
