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
            result = this->world_->begin_world_revision(
                this->world_->terrain_generation_settings(),
                static_cast<World::RegenerationMode>(mode));
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
        int32_t regenerated;
        int32_t skipped;
        result = this->world_->regenerate_selected_chunks(&regenerated, &skipped);
    }
    json_free_groups(groups);
    return (result);
}
