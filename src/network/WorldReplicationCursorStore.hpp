#ifndef WORLD_REPLICATION_CURSOR_STORE_HPP
# define WORLD_REPLICATION_CURSOR_STORE_HPP

# include "../../Libft/Modules/Networking/networking_replication_protocol.hpp"
# include <string>

class WorldReplicationCursorStore
{
  public:
	WorldReplicationCursorStore();
	WorldReplicationCursorStore(const WorldReplicationCursorStore &other);
	~WorldReplicationCursorStore();
	WorldReplicationCursorStore &operator=(
		const WorldReplicationCursorStore &other);

	int32_t initialize(const char *path);
	int32_t load(networking_replication_peer_cursor &cursor) const;
	int32_t load_for_session(uint64_t server_instance_id,
		uint64_t session_id, uint64_t subscription_id,
		networking_replication_peer_cursor &cursor) const;
	int32_t save(const networking_replication_peer_cursor &cursor) const;
	int32_t clear() const;
	bool initialized() const noexcept;

  private:
	std::string path_;
	bool initialized_;
};

#endif
