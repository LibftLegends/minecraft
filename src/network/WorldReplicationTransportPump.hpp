#ifndef WORLD_REPLICATION_TRANSPORT_PUMP_HPP
# define WORLD_REPLICATION_TRANSPORT_PUMP_HPP

# include "WorldReplicationServer.hpp"
# include "WorldReplicationClient.hpp"
# include "../../Libft/Modules/Networking/message_transport.hpp"

enum class WorldReplicationTransportPumpRole : uint8_t
{
	NONE = 0U,
	SERVER = 1U,
	CLIENT = 2U
};

class WorldReplicationTransportPump
{
  public:
	WorldReplicationTransportPump();
	WorldReplicationTransportPump(
		const WorldReplicationTransportPump &other);
	~WorldReplicationTransportPump();
	WorldReplicationTransportPump &operator=(
		const WorldReplicationTransportPump &other);

	int32_t initialize_server(networking_message_transport &transport,
		WorldReplicationServer &server);
	int32_t initialize_client(networking_message_transport &transport,
		WorldReplicationClient &client);
	int32_t pump(int32_t timeout_milliseconds, uint32_t maximum_messages,
		uint32_t &processed_messages);
	WorldReplicationTransportPumpRole role() const;
	int32_t destroy();

  private:
	 networking_message_transport *transport_;
	 WorldReplicationServer *server_;
	 WorldReplicationClient *client_;
	 networking_received_message *pending_message_;
	 WorldReplicationTransportPumpRole role_;
	 bool initialized_;

	int32_t initialize_common(networking_message_transport &transport);
};

#endif
