#include "../../src/network/ProtocolMessages.hpp"

static int32_t write_string(ft_byte_buffer &buffer,
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

static int32_t read_string(ft_byte_buffer &buffer,
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

int32_t protocol_header_serialize(const ProtocolMessageHeader &header,
    ft_byte_buffer &buffer) noexcept
{
    int32_t error_code;

    error_code = buffer.append_u32_le(header.message_type);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    return (buffer.append_u32_le(header.payload_size));
}

int32_t protocol_header_deserialize(ProtocolMessageHeader &header,
    ft_byte_buffer &buffer) noexcept
{
    int32_t error_code;

    error_code = buffer.read_u32_le(&header.message_type);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    return (buffer.read_u32_le(&header.payload_size));
}

int32_t protocol_join_serialize(const ProtocolJoinMessage &message,
    ft_byte_buffer &buffer) noexcept
{
    int32_t error_code;

    error_code = buffer.append_u32_le(message.client_id);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = write_string(buffer, message.player_name);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    return (write_string(buffer, message.auth_token));
}

int32_t protocol_join_deserialize(ProtocolJoinMessage &message,
    ft_byte_buffer &buffer) noexcept
{
    int32_t error_code;

    error_code = buffer.read_u32_le(&message.client_id);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = read_string(buffer, message.player_name);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    return (read_string(buffer, message.auth_token));
}

int32_t protocol_leave_serialize(const ProtocolLeaveMessage &message,
    ft_byte_buffer &buffer) noexcept
{
    return (buffer.append_u32_le(message.client_id));
}

int32_t protocol_leave_deserialize(ProtocolLeaveMessage &message,
    ft_byte_buffer &buffer) noexcept
{
    return (buffer.read_u32_le(&message.client_id));
}

int32_t protocol_chunk_request_serialize(
    const ProtocolChunkRequestMessage &message,
    ft_byte_buffer &buffer) noexcept
{
    int32_t error_code;

    error_code = buffer.append_u32_le(
        static_cast<uint32_t>(message.chunk_x));
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    return (buffer.append_u32_le(static_cast<uint32_t>(message.chunk_z)));
}

int32_t protocol_chunk_request_deserialize(
    ProtocolChunkRequestMessage &message, ft_byte_buffer &buffer) noexcept
{
    uint32_t raw_x;
    uint32_t raw_z;
    int32_t error_code;

    error_code = buffer.read_u32_le(&raw_x);
    if (error_code == FT_ERR_SUCCESS)
        error_code = buffer.read_u32_le(&raw_z);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    message.chunk_x = static_cast<int32_t>(raw_x);
    message.chunk_z = static_cast<int32_t>(raw_z);
    return (FT_ERR_SUCCESS);
}

int32_t protocol_chunk_response_serialize(
    const ProtocolChunkResponseMessage &message,
    ft_byte_buffer &buffer) noexcept
{
    int32_t error_code;

    error_code = buffer.append_u32_le(
        static_cast<uint32_t>(message.chunk_x));
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = buffer.append_u32_le(
        static_cast<uint32_t>(message.chunk_z));
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    error_code = buffer.append_u32_le(
        static_cast<uint32_t>(message.chunk_payload.size()));
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    if (message.chunk_payload.size() == 0U)
        return (FT_ERR_SUCCESS);
    return (buffer.append(message.chunk_payload.data(),
        message.chunk_payload.size()));
}

int32_t protocol_chunk_response_deserialize(
    ProtocolChunkResponseMessage &message, ft_byte_buffer &buffer) noexcept
{
    uint32_t raw_x;
    uint32_t raw_z;
    uint32_t payload_size;
    int32_t error_code;

    error_code = buffer.read_u32_le(&raw_x);
    if (error_code == FT_ERR_SUCCESS)
        error_code = buffer.read_u32_le(&raw_z);
    if (error_code == FT_ERR_SUCCESS)
        error_code = buffer.read_u32_le(&payload_size);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    message.chunk_x = static_cast<int32_t>(raw_x);
    message.chunk_z = static_cast<int32_t>(raw_z);
    message.chunk_payload.resize(payload_size);
    if (payload_size == 0U)
        return (FT_ERR_SUCCESS);
    return (buffer.read(message.chunk_payload.data(), payload_size));
}

int32_t protocol_edit_broadcast_serialize(
    const ProtocolEditBroadcastMessage &message,
    ft_byte_buffer &buffer) noexcept
{
    int32_t error_code;

    error_code = buffer.append_u32_le(message.client_id);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    return (game_block_edit_op_serialize(message.edit, buffer));
}

int32_t protocol_edit_broadcast_deserialize(
    ProtocolEditBroadcastMessage &message, ft_byte_buffer &buffer) noexcept
{
    int32_t error_code;

    error_code = buffer.read_u32_le(&message.client_id);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    return (game_block_edit_op_deserialize(message.edit, buffer));
}

int32_t protocol_entity_update_serialize(
    const ProtocolEntityUpdateMessage &message,
    ft_byte_buffer &buffer) noexcept
{
    return (entity_state_serialize(message.entity, buffer));
}

int32_t protocol_entity_update_deserialize(
    ProtocolEntityUpdateMessage &message, ft_byte_buffer &buffer) noexcept
{
    return (entity_state_deserialize(message.entity, buffer));
}

int32_t protocol_chat_serialize(const ProtocolChatMessage &message,
    ft_byte_buffer &buffer) noexcept
{
    int32_t error_code;

    error_code = buffer.append_u32_le(message.client_id);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    return (write_string(buffer, message.text));
}

int32_t protocol_chat_deserialize(ProtocolChatMessage &message,
    ft_byte_buffer &buffer) noexcept
{
    int32_t error_code;

    error_code = buffer.read_u32_le(&message.client_id);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    return (read_string(buffer, message.text));
}

int32_t protocol_ping_serialize(const ProtocolPingMessage &message,
    ft_byte_buffer &buffer) noexcept
{
    return (buffer.append_u64_le(message.client_send_tick));
}

int32_t protocol_ping_deserialize(ProtocolPingMessage &message,
    ft_byte_buffer &buffer) noexcept
{
    return (buffer.read_u64_le(&message.client_send_tick));
}

int32_t protocol_pong_serialize(const ProtocolPongMessage &message,
    ft_byte_buffer &buffer) noexcept
{
    int32_t error_code;

    error_code = buffer.append_u64_le(message.client_send_tick);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    return (buffer.append_u64_le(message.server_tick));
}

int32_t protocol_pong_deserialize(ProtocolPongMessage &message,
    ft_byte_buffer &buffer) noexcept
{
    int32_t error_code;

    error_code = buffer.read_u64_le(&message.client_send_tick);
    if (error_code != FT_ERR_SUCCESS)
        return (error_code);
    return (buffer.read_u64_le(&message.server_tick));
}
