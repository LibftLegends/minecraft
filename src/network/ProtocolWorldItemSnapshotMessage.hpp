#ifndef PROTOCOL_WORLD_ITEM_SNAPSHOT_MESSAGE_HPP
# define PROTOCOL_WORLD_ITEM_SNAPSHOT_MESSAGE_HPP

# include "../entities/WorldItemStore.hpp"
# include "../../Libft/Modules/Networking/networking_replication_protocol.hpp"
# include <vector>

class ProtocolWorldItemSnapshotMessage
{
  public:
	uint64_t session_id;
	uint64_t world_id;
	int32_t chunk_x;
	int32_t chunk_z;
	std::vector<WorldItemSnapshot> items;

	ProtocolWorldItemSnapshotMessage();
	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;
};

#endif
