#include "../../src/entities/EntityState.hpp"
#include <cstring>

static uint64_t double_to_bits(double value) noexcept
{
    uint64_t bits;

    std::memcpy(&bits, &value, sizeof(bits));
    return (bits);
}

static double bits_to_double(uint64_t bits) noexcept
{
    double value;

    std::memcpy(&value, &bits, sizeof(value));
    return (value);
}

static int32_t append_double(ft_byte_buffer &buffer, double value) noexcept
{
    return (buffer.append_u64_le(double_to_bits(value)));
}

static int32_t read_double(ft_byte_buffer &buffer, double *value_out) noexcept
{
    uint64_t bits;
    int32_t error_code;

    error_code = buffer.read_u64_le(&bits);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    *value_out = bits_to_double(bits);
    return (FT_ERR_SUCCESS);
}

int32_t entity_state_serialize(const EntityState &state,
    ft_byte_buffer &buffer) noexcept
{
    int32_t error_code;

    error_code = buffer.append_u32_le(state.id);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = buffer.append_u32_le(state.type);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = append_double(buffer, state.position_x);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = append_double(buffer, state.position_y);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = append_double(buffer, state.position_z);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = append_double(buffer, state.velocity_x);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = append_double(buffer, state.velocity_y);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = append_double(buffer, state.velocity_z);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = append_double(buffer, state.orientation_yaw);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = append_double(buffer, state.orientation_pitch);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = buffer.append_u32_le(static_cast<uint32_t>(state.health));
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = buffer.append_u32_le(state.action_state);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    return (buffer.append_u64_le(state.tick));
}

int32_t entity_state_deserialize(EntityState &state,
    ft_byte_buffer &buffer) noexcept
{
    uint32_t raw_health;
    int32_t error_code;

    error_code = buffer.read_u32_le(&state.id);
    if (error_code == FT_ERR_SUCCESS)
        error_code = buffer.read_u32_le(&state.type);
    if (error_code == FT_ERR_SUCCESS)
        error_code = read_double(buffer, &state.position_x);
    if (error_code == FT_ERR_SUCCESS)
        error_code = read_double(buffer, &state.position_y);
    if (error_code == FT_ERR_SUCCESS)
        error_code = read_double(buffer, &state.position_z);
    if (error_code == FT_ERR_SUCCESS)
        error_code = read_double(buffer, &state.velocity_x);
    if (error_code == FT_ERR_SUCCESS)
        error_code = read_double(buffer, &state.velocity_y);
    if (error_code == FT_ERR_SUCCESS)
        error_code = read_double(buffer, &state.velocity_z);
    if (error_code == FT_ERR_SUCCESS)
        error_code = read_double(buffer, &state.orientation_yaw);
    if (error_code == FT_ERR_SUCCESS)
        error_code = read_double(buffer, &state.orientation_pitch);
    if (error_code == FT_ERR_SUCCESS)
        error_code = buffer.read_u32_le(&raw_health);
    if (error_code == FT_ERR_SUCCESS)
        error_code = buffer.read_u32_le(&state.action_state);
    if (error_code == FT_ERR_SUCCESS)
        error_code = buffer.read_u64_le(&state.tick);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    state.health = static_cast<int32_t>(raw_health);
    return (FT_ERR_SUCCESS);
}
