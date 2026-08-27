#include "../../src/network/ProtocolChatMessage.hpp"

ProtocolChatMessage::ProtocolChatMessage() : client_id(0U), text()
{
}

ProtocolChatMessage::ProtocolChatMessage(const ProtocolChatMessage &other)
	: client_id(other.client_id),
	text(other.text)
{
}

ProtocolChatMessage::~ProtocolChatMessage()
{
}

ProtocolChatMessage &ProtocolChatMessage::operator=(const ProtocolChatMessage &other)
{
	if (this != &other)
	{
		this->client_id = other.client_id;
		this->text = other.text;
	}
	return (*this);
}

int32_t ProtocolChatMessage::write_string(ft_byte_buffer &buffer,
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

int32_t ProtocolChatMessage::read_string(ft_byte_buffer &buffer,
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

int32_t ProtocolChatMessage::serialize(ft_byte_buffer &buffer) const noexcept
{
	int32_t error_code;

	error_code = buffer.append_u32_le(this->client_id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (write_string(buffer, this->text));
}

int32_t ProtocolChatMessage::deserialize(ft_byte_buffer &buffer) noexcept
{
	int32_t error_code;

	error_code = buffer.read_u32_le(&this->client_id);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	return (read_string(buffer, this->text));
}
