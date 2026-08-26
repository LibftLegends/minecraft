#ifndef ENTITY_STATE_HPP
# define ENTITY_STATE_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../../Libft/Modules/Buffer/byte_buffer.hpp"
# include "../ft_vox.hpp"

/*
 * M0 shared contract: the entity state used by both the monster AI
 * (Track A) and entity sync (Track B). Plain flat fields (matching this
 * project's Camera/RenderDebug convention) rather than Libft's heavier
 * class lifecycle, since this class owns no resources. Serialized as
 * binary via ft_byte_buffer for consistency with the chunk/edit-op formats
 * so entity updates can go over the network wire cheaply.
 */
class EntityState
{
  public:
	enum class Kind
	{
		PLAYER,
		ZOMBIE,
		CREEPER,
		SKELETON
	};

	enum class ActionState
	{
		IDLE,
		WALKING,
		ATTACKING,
		SWIMMING,
		DEAD
	};

	uint32_t id;
	uint32_t type;
	double position_x;
	double position_y;
	double position_z;
	double velocity_x;
	double velocity_y;
	double velocity_z;
	double orientation_yaw;
	double orientation_pitch;
	int32_t health;
	uint32_t action_state;
	uint64_t tick;

	EntityState();
	EntityState(const EntityState &other);
	~EntityState();
	EntityState &operator=(const EntityState &other);

	int32_t serialize(ft_byte_buffer &buffer) const noexcept;
	int32_t deserialize(ft_byte_buffer &buffer) noexcept;

  private:
	static uint64_t double_to_bits(double value) noexcept;
	static double bits_to_double(uint64_t bits) noexcept;
	static int32_t append_double(ft_byte_buffer &buffer, double value) noexcept;
	static int32_t read_double(ft_byte_buffer &buffer,
		double *value_out) noexcept;

	int32_t serialize_motion(ft_byte_buffer &buffer) const noexcept;
};

#endif
