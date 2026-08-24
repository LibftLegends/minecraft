#ifndef PROTOCOL_MESSAGES_HPP
#define PROTOCOL_MESSAGES_HPP

#ifndef GAME_USE_VOXEL_REGION_BACKEND
#define GAME_USE_VOXEL_REGION_BACKEND
#endif
#include "../ft_vox.hpp"
#include "../../Libft/Modules/Buffer/byte_buffer.hpp"
#include "../../Libft/Modules/Game/game_block_edit_op.hpp"
#include "../../src/entities/EntityState.hpp"
#include <string>
#include <vector>

/*
 * M0 shared contract: network message schema. Shapes and wire encoding
 * only (join/leave, chunk request/response, edit broadcast, entity update,
 * chat/ping) -- the actual socket read/write loop is Track B's B3
 * milestone, not part of this contract. Binary via ft_byte_buffer, the
 * same encoding style as the chunk (M0 item 1) and block-edit-op (M0 item
 * 2) formats, so one framing convention covers every message type.
 *
 * Every message is sent as: ProtocolMessageHeader (message_type,
 * payload_size) followed by payload_size bytes produced by that message's
 * own protocol_*_serialize function.
 */
enum ProtocolMessageType
{
    PROTOCOL_MESSAGE_JOIN = 0U,
    PROTOCOL_MESSAGE_LEAVE = 1U,
    PROTOCOL_MESSAGE_CHUNK_REQUEST = 2U,
    PROTOCOL_MESSAGE_CHUNK_RESPONSE = 3U,
    PROTOCOL_MESSAGE_EDIT_BROADCAST = 4U,
    PROTOCOL_MESSAGE_ENTITY_UPDATE = 5U,
    PROTOCOL_MESSAGE_CHAT = 6U,
    PROTOCOL_MESSAGE_PING = 7U,
    PROTOCOL_MESSAGE_PONG = 8U
};

struct ProtocolMessageHeader
{
    uint32_t message_type;
    uint32_t payload_size;
};

struct ProtocolJoinMessage
{
    uint32_t client_id;
    std::string player_name;
    std::string auth_token;
};

struct ProtocolLeaveMessage
{
    uint32_t client_id;
};

struct ProtocolChunkRequestMessage
{
    int32_t chunk_x;
    int32_t chunk_z;
};

struct ProtocolChunkResponseMessage
{
    int32_t chunk_x;
    int32_t chunk_z;
    /* Verbatim game_voxel_chunk::serialize() bytes -- M0 item 1's format
     * reused as-is for chunk dispatch. */
    std::vector<uint8_t> chunk_payload;
};

struct ProtocolEditBroadcastMessage
{
    uint32_t client_id;
    game_block_edit_op edit;
};

struct ProtocolEntityUpdateMessage
{
    EntityState entity;
};

struct ProtocolChatMessage
{
    uint32_t client_id;
    std::string text;
};

struct ProtocolPingMessage
{
    uint64_t client_send_tick;
};

struct ProtocolPongMessage
{
    uint64_t client_send_tick;
    uint64_t server_tick;
};

int32_t protocol_header_serialize(const ProtocolMessageHeader &header,
    ft_byte_buffer &buffer) noexcept;
int32_t protocol_header_deserialize(ProtocolMessageHeader &header,
    ft_byte_buffer &buffer) noexcept;

int32_t protocol_join_serialize(const ProtocolJoinMessage &message,
    ft_byte_buffer &buffer) noexcept;
int32_t protocol_join_deserialize(ProtocolJoinMessage &message,
    ft_byte_buffer &buffer) noexcept;

int32_t protocol_leave_serialize(const ProtocolLeaveMessage &message,
    ft_byte_buffer &buffer) noexcept;
int32_t protocol_leave_deserialize(ProtocolLeaveMessage &message,
    ft_byte_buffer &buffer) noexcept;

int32_t protocol_chunk_request_serialize(
    const ProtocolChunkRequestMessage &message,
    ft_byte_buffer &buffer) noexcept;
int32_t protocol_chunk_request_deserialize(
    ProtocolChunkRequestMessage &message, ft_byte_buffer &buffer) noexcept;

int32_t protocol_chunk_response_serialize(
    const ProtocolChunkResponseMessage &message,
    ft_byte_buffer &buffer) noexcept;
int32_t protocol_chunk_response_deserialize(
    ProtocolChunkResponseMessage &message, ft_byte_buffer &buffer) noexcept;

int32_t protocol_edit_broadcast_serialize(
    const ProtocolEditBroadcastMessage &message,
    ft_byte_buffer &buffer) noexcept;
int32_t protocol_edit_broadcast_deserialize(
    ProtocolEditBroadcastMessage &message, ft_byte_buffer &buffer) noexcept;

int32_t protocol_entity_update_serialize(
    const ProtocolEntityUpdateMessage &message,
    ft_byte_buffer &buffer) noexcept;
int32_t protocol_entity_update_deserialize(
    ProtocolEntityUpdateMessage &message, ft_byte_buffer &buffer) noexcept;

int32_t protocol_chat_serialize(const ProtocolChatMessage &message,
    ft_byte_buffer &buffer) noexcept;
int32_t protocol_chat_deserialize(ProtocolChatMessage &message,
    ft_byte_buffer &buffer) noexcept;

int32_t protocol_ping_serialize(const ProtocolPingMessage &message,
    ft_byte_buffer &buffer) noexcept;
int32_t protocol_ping_deserialize(ProtocolPingMessage &message,
    ft_byte_buffer &buffer) noexcept;

int32_t protocol_pong_serialize(const ProtocolPongMessage &message,
    ft_byte_buffer &buffer) noexcept;
int32_t protocol_pong_deserialize(ProtocolPongMessage &message,
    ft_byte_buffer &buffer) noexcept;

#endif
