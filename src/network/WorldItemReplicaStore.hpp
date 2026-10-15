#ifndef WORLD_ITEM_REPLICA_STORE_HPP
# define WORLD_ITEM_REPLICA_STORE_HPP

# include "ProtocolWorldItemSnapshotMessage.hpp"
# include <map>
# include <mutex>

class WorldItemReplicaStore
{
  public:
	WorldItemReplicaStore();
	int32_t initialize(uint64_t world_id) noexcept;
	int32_t replace_chunk(const ProtocolWorldItemSnapshotMessage &snapshot) noexcept;
	int32_t query_chunk(int32_t chunk_x, int32_t chunk_z,
		std::vector<WorldItemSnapshot> *items_out) const noexcept;
	int32_t destroy() noexcept;

  private:
	static const std::size_t MAX_ITEMS = 100000U;
	mutable std::mutex mutex_;
	std::map<uint64_t, WorldItemSnapshot> items_;
	uint64_t world_id_;
	bool initialized_;
};

#endif
