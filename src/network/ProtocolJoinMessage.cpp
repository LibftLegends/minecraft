#include "../../src/network/ProtocolJoinMessage.hpp"

ProtocolJoinMessage::ProtocolJoinMessage() : client_id(0U), player_name(),
	auth_token()
{
}

ProtocolJoinMessage::ProtocolJoinMessage(const ProtocolJoinMessage &other)
	: client_id(other.client_id),
	player_name(other.player_name), auth_token(other.auth_token)
{
}

ProtocolJoinMessage::~ProtocolJoinMessage()
{
}

ProtocolJoinMessage &ProtocolJoinMessage::operator=(const ProtocolJoinMessage &other)
{
	if (this != &other)
	{
		this->client_id = other.client_id;
		this->player_name = other.player_name;
		this->auth_token = other.auth_token;
	}
	return (*this);
}

int32_t ProtocolJoinMessage::write_string(ft_byte_buffer &buffer,
	const std::string &value) noexcept
{
	int32_t error_code;

	error_code = buffer.append_u32_le(static_cast<uint32_t>(value.size()));
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	if (value.size() == 0U)
		return (FT_ERR_SUCCESS);
	return (buffer.append(value.data(), value.size()));
}

int32_t ProtocolJoinMessage::read_string(ft_byte_buffer &buffer,
	std::string &value) noexcept
{
	uint32_t length;
	int32_t error_code;

	error_code = buffer.read_u32_le(&length);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	value.resize(length);
	if (length == 0U)
		return (FT_ERR_SUCCESS);
	return (buffer.read(&value[0], length));
}

int32_t ProtocolJoinMessage::serialize(ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	error_code = buffer.append_u32_le(this->client_id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = write_string(buffer, this->player_name);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (write_string(buffer, this->auth_token));
}

int32_t ProtocolJoinMessage::deserialize(ft_byte_buffer &buffer) noexcept
{
	int32_t error_code;

	error_code = buffer.read_u32_le(&this->client_id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = read_string(buffer, this->player_name);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (read_string(buffer, this->auth_token));
}
