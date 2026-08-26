#ifndef WORLD_COORDINATES_HPP
# define WORLD_COORDINATES_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../ft_vox.hpp"

class WorldCoordinates
{
  public:
	static const int STREAM_CHUNK_RADIUS;
	static const int CACHE_CHUNK_RADIUS;
	static const int CHUNK_RADIUS;
	static constexpr int CHUNK_COUNT = ((12 * 2 + 1) * (12 * 2 + 1));
	static const int MIN_RENDER_DISTANCE;
	static const int REQUIRED_VISIBLE_DISTANCE;

	WorldCoordinates();
	WorldCoordinates(const WorldCoordinates &other);
	~WorldCoordinates();
	WorldCoordinates &operator=(const WorldCoordinates &other);

	static int32_t floor_divide(int32_t value, int32_t divisor);
	static int32_t positive_modulo(int32_t value, int32_t divisor);
	static int32_t clamp_int(int32_t value, int32_t minimum, int32_t maximum);
	static int32_t chunk_distance_squared(int32_t chunk_x, int32_t chunk_z,
		int32_t center_chunk_x, int32_t center_chunk_z);
	static int32_t render_distance_to_chunk_radius(int32_t render_distance);
};

#endif
