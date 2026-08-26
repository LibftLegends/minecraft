#include "../../src/entities/EntityState.hpp"
#include <cstring>

EntityState::EntityState() : id(0U), type(0U), position_x(0.0), position_y(0.0),
	position_z(0.0), velocity_x(0.0), velocity_y(0.0), velocity_z(0.0),
	orientation_yaw(0.0), orientation_pitch(0.0), health(0), action_state(0U),
	tick(0U)
{
}

EntityState::EntityState(const EntityState &other)
{
	*this = other;
}

EntityState::~EntityState()
{
}

EntityState &EntityState::operator=(const EntityState &other)
{
	if (this != &other)
	{
		id = other.id;
		type = other.type;
		position_x = other.position_x;
		position_y = other.position_y;
		position_z = other.position_z;
		velocity_x = other.velocity_x;
		velocity_y = other.velocity_y;
		velocity_z = other.velocity_z;
		orientation_yaw = other.orientation_yaw;
		orientation_pitch = other.orientation_pitch;
		health = other.health;
		action_state = other.action_state;
		tick = other.tick;
	}
	return (*this);
}

uint64_t EntityState::double_to_bits(double value) noexcept
{
	uint64_t bits;

	std::memcpy(&bits, &value, sizeof(bits));
	return (bits);
}

double EntityState::bits_to_double(uint64_t bits) noexcept
{
	double value;

	std::memcpy(&value, &bits, sizeof(value));
	return (value);
}

int32_t EntityState::append_double(ft_byte_buffer &buffer,
	double value) noexcept
{
	return (buffer.append_u64_le(double_to_bits(value)));
}

int32_t EntityState::read_double(ft_byte_buffer &buffer,
	double *value_out) noexcept
{
	uint64_t bits;
	int32_t error_code;

	error_code = buffer.read_u64_le(&bits);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	*value_out = bits_to_double(bits);
	return (FT_ERR_SUCCESS);
}

int32_t EntityState::serialize_motion(ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	error_code = append_double(buffer, position_x);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = append_double(buffer, position_y);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = append_double(buffer, position_z);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = append_double(buffer, velocity_x);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = append_double(buffer, velocity_y);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = append_double(buffer, velocity_z);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = append_double(buffer, orientation_yaw);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (append_double(buffer, orientation_pitch));
}

int32_t EntityState::serialize(ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	error_code = buffer.append_u32_le(id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = buffer.append_u32_le(type);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = serialize_motion(buffer);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = buffer.append_u32_le(static_cast<uint32_t>(health));
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = buffer.append_u32_le(action_state);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (buffer.append_u64_le(tick));
}

int32_t EntityState::deserialize(ft_byte_buffer &buffer) noexcept
{
	uint32_t raw_health;
	int32_t error_code;

	error_code = buffer.read_u32_le(&id);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&type);
	if (error_code == FT_ERR_SUCCESS)
		error_code = read_double(buffer, &position_x);
	if (error_code == FT_ERR_SUCCESS)
		error_code = read_double(buffer, &position_y);
	if (error_code == FT_ERR_SUCCESS)
		error_code = read_double(buffer, &position_z);
	if (error_code == FT_ERR_SUCCESS)
		error_code = read_double(buffer, &velocity_x);
	if (error_code == FT_ERR_SUCCESS)
		error_code = read_double(buffer, &velocity_y);
	if (error_code == FT_ERR_SUCCESS)
		error_code = read_double(buffer, &velocity_z);
	if (error_code == FT_ERR_SUCCESS)
		error_code = read_double(buffer, &orientation_yaw);
	if (error_code == FT_ERR_SUCCESS)
		error_code = read_double(buffer, &orientation_pitch);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&raw_health);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u32_le(&action_state);
	if (error_code == FT_ERR_SUCCESS)
		error_code = buffer.read_u64_le(&tick);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	health = static_cast<int32_t>(raw_health);
	return (FT_ERR_SUCCESS);
}
