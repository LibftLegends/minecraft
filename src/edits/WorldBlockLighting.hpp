#ifndef WORLD_BLOCK_LIGHTING_HPP
# define WORLD_BLOCK_LIGHTING_HPP

# include "../../Libft/Modules/Voxel/voxel_api.hpp"

namespace world_block_lighting
{
	inline bool inputs_differ(uint32_t before, uint32_t after) noexcept
	{
		const voxel_block_metadata &old_metadata = voxel_get_block_metadata(before);
		const voxel_block_metadata &new_metadata = voxel_get_block_metadata(after);
		return (old_metadata.transparent != new_metadata.transparent
			|| old_metadata.occludes_faces != new_metadata.occludes_faces
			|| old_metadata.light_attenuation != new_metadata.light_attenuation
			|| old_metadata.emitted_light_level != new_metadata.emitted_light_level);
	}
}

#endif
