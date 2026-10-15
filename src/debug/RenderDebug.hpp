#ifndef RENDER_DEBUG_HPP
# define RENDER_DEBUG_HPP

# include "../ft_vox.hpp"
class RenderDebug
{
  public:
	enum RevisionProtectionMask : uint8_t
	{
		REVISION_PROTECTION_GENERATION = 1U,
		REVISION_PROTECTION_LOOM = 2U,
		REVISION_PROTECTION_MANUAL = 4U,
		REVISION_PROTECTION_OCCUPIED = 8U
	};
	enum RevisionPreviewSize : uint32_t
	{
		REVISION_PREVIEW_RADIUS = 8U,
		REVISION_PREVIEW_WIDTH = REVISION_PREVIEW_RADIUS * 2U + 1U,
		REVISION_PREVIEW_CELL_COUNT = REVISION_PREVIEW_WIDTH
			* REVISION_PREVIEW_WIDTH
	};
	double fps;
	double frame_ms;
	int32_t visible_chunks;
	int32_t loaded_chunks;
	int32_t render_distance;
	uint32_t selected_block_id;
	uint32_t ram_mb;
	uint32_t vram_approx_mb;
	double camera_x;
	double camera_y;
	double camera_z;
	double camera_speed;
	double boost_speed;
	char seed[32];
	char biome_name[24];
	bool revision_preview_visible;
	bool revision_pending;
	bool revision_barriers_active;
	uint8_t revision_map_radius;
	uint8_t revision_map[REVISION_PREVIEW_CELL_COUNT];
	uint8_t revision_protection_reasons[REVISION_PREVIEW_CELL_COUNT];
	uint8_t revision_feature_highlights[REVISION_PREVIEW_CELL_COUNT];
	uint32_t revision_protected_count;
	uint32_t revision_selected_count;
	uint32_t revision_transition_count;
	uint32_t revision_unchanged_count;

	RenderDebug();
	RenderDebug(const RenderDebug &other);
	~RenderDebug();
	RenderDebug &operator=(const RenderDebug &other);

	void clear();
};

#endif
