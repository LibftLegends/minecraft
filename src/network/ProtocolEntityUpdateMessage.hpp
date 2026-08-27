#ifndef PROTOCOL_ENTITY_UPDATE_MESSAGE_HPP
# define PROTOCOL_ENTITY_UPDATE_MESSAGE_HPP

# include "../../src/entities/EntityState.hpp"
# include "../ft_vox.hpp"

class ProtocolEntityUpdateMessage
{
  public:
	EntityState entity;

	ProtocolEntityUpdateMessage();
	ProtocolEntityUpdateMessage(const ProtocolEntityUpdateMessage &other);
	~ProtocolEntityUpdateMessage();
	ProtocolEntityUpdateMessage &operator=(const ProtocolEntityUpdateMessage &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
