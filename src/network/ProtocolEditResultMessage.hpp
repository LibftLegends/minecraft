#ifndef PROTOCOL_EDIT_RESULT_MESSAGE_HPP
# define PROTOCOL_EDIT_RESULT_MESSAGE_HPP

# include "../ft_vox.hpp"

class ProtocolEditResultMessage
{
  public:
	uint16_t protocol_version;
	uint64_t session_id;
	uint64_t request_id;
	uint64_t authoritative_revision;
	uint8_t accepted;
	int32_t result_code;
	game_block_delta delta;

	ProtocolEditResultMessage();
	ProtocolEditResultMessage(const ProtocolEditResultMessage &other);
	~ProtocolEditResultMessage();
	ProtocolEditResultMessage &operator=(
		const ProtocolEditResultMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
