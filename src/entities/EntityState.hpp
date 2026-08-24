#ifndef ENTITY_STATE_HPP
#define ENTITY_STATE_HPP

#ifndef GAME_USE_VOXEL_REGION_BACKEND
#define GAME_USE_VOXEL_REGION_BACKEND
#endif
#include "../ft_vox.hpp"
#include "../../Libft/Modules/Buffer/byte_buffer.hpp"

/*
 * M0 shared contract: the entity state struct used by both the monster AI
 * (Track A) and entity sync (Track B). Plain flat fields (matching this
 * project's Camera/PlaceBlockCommand convention) rather than Libft's heavier
 * class lifecycle, since this struct owns no resources. Serialized as
 * binary via ft_byte_buffer for consistency with the chunk/edit-op formats
 * so entity updates can go over the network wire cheaply.
 */
enum EntityKind
{
    ENTITY_KIND_PLAYER = 0,
    ENTITY_KIND_ZOMBIE = 1,
    ENTITY_KIND_CREEPER = 2,
    ENTITY_KIND_SKELETON = 3
};

enum EntityActionState
{
    ENTITY_ACTION_IDLE = 0,
    ENTITY_ACTION_WALKING = 1,
    ENTITY_ACTION_ATTACKING = 2,
    ENTITY_ACTION_SWIMMING = 3,
    ENTITY_ACTION_DEAD = 4
};

struct EntityState
{
    uint32_t id;
    uint32_t type;
    double position_x;
    double position_y;
    double position_z;
    double velocity_x;
    double velocity_y;
    double velocity_z;
    double orientation_yaw;
    double orientation_pitch;
    int32_t health;
    uint32_t action_state;
    uint64_t tick;
};

int32_t entity_state_serialize(const EntityState &state,
    ft_byte_buffer &buffer) noexcept;
int32_t entity_state_deserialize(EntityState &state,
    ft_byte_buffer &buffer) noexcept;

#endif
