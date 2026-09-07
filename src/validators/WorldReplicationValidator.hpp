#ifndef WORLD_REPLICATION_VALIDATOR_HPP
# define WORLD_REPLICATION_VALIDATOR_HPP

class WorldReplicationValidator
{
  public:
	WorldReplicationValidator();
	WorldReplicationValidator(const WorldReplicationValidator &other);
	~WorldReplicationValidator();
	WorldReplicationValidator &operator=(
		const WorldReplicationValidator &other);

	int validate() const;
};

#endif
