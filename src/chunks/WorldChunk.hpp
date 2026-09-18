#ifndef WORLD_CHUNK_HPP
# define WORLD_CHUNK_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../ft_vox.hpp"
# include "../world/WorldLightVersion.hpp"
# include <atomic>
# include <memory>
# include <vector>

struct WorldChunkReadState
{
	int32_t chunk_x;
	int32_t chunk_z;
	int32_t world_x;
	int32_t world_z;
	uint64_t voxel_revision;
	uint64_t light_revision;
	uint16_t content_version;
	uint16_t light_input_version;
	uint16_t light_version;
	uint16_t computed_light_input_version;
	ft_bool initialized;
	ft_bool light_valid;
	game_voxel_generation_metadata generation_metadata;
	std::vector<uint32_t> blocks;
	std::vector<uint8_t> light;
	/* Block edits use a copy-on-write overlay.  The live writer publishes the
	 * changed cell immediately without copying the complete 256-column chunk;
	 * the capture worker materializes the immutable chain before it starts a
	 * mesh/light solve. */
	std::shared_ptr<const WorldChunkReadState> block_parent;
	std::size_t block_overlay_index;
	uint32_t block_overlay_value;
	ft_bool has_block_overlay;

	int32_t materialize_blocks(std::vector<uint32_t> &output) const noexcept;
};

class WorldChunk
{
  public:
	int32_t chunk_x;
	int32_t chunk_z;
	int32_t world_x;
	int32_t world_z;
	uint64_t mesh_revision;
	uint64_t voxel_revision;
	uint64_t light_revision;
	uint16_t content_version;
	uint16_t light_version;
	uint16_t light_input_version;
	uint16_t computed_light_input_version;
	uint64_t pending_mesh_request_id;
	ft_bool pending_mesh_request_interactive;
	uint64_t pending_mesh_request_voxel_revision;
	uint16_t pending_mesh_request_content_version;
	uint16_t pending_mesh_request_light_input_version;
	ft_bool last_mesh_publication_interactive;
	uint64_t last_mesh_publication_voxel_revision;
	/* Per-chunk completion marker used by validators and diagnostics.  A
	 * completed mesh may be geometry-only, so this records the final light
	 * publication mode for the latest mesh revision. */
	ft_bool last_light_remesh_incremental;
	/* Unlike the latest-result marker above, these identify the content
	 * revision for which an incremental light result was actually committed.
	 * A later neighbour/full remesh must not erase evidence for the edit that
	 * caused the current content revision. */
	uint16_t last_incremental_light_content_version;
	uint64_t last_incremental_light_voxel_revision;
	uint64_t incremental_light_protection_until_frame;
	/* Do not expose a generated mesh until its light result is published. */
	bool light_ready_for_render;
	std::shared_ptr<std::atomic<uint64_t>> remesh_cancellation_token;
	bool mesh_dirty;
	/* A boundary edit may expose a face in a loaded neighbor.  Keep the
	 * changed chunk's old GPU publication until those neighbor faces are
	 * uploaded, preventing a transient hole between two chunks. */
	bool border_mesh_publication_pending;
	uint64_t border_mesh_publication_voxel_revision;
	bool initialized;
	bool waits_for_neighbor_light;
	int32_t light_dependency_chunk_x;
	int32_t light_dependency_chunk_z;
	/* Cached summaries for the four source/target face pairings.  Index 0
	 * describes this chunk's east light against a neighbour's west boundary;
	 * index 1 is west/east, index 2 is south/north and index 3 is
	 * north/south.  They let streaming avoid rescanning an entire face while
	 * the world write lock is held. */
	uint8_t boundary_source_light_max[4];
	bool boundary_target_has_transparent[4];
	/* Keep frequently scanned render/stream metadata contiguous. The voxel
	 * storage and mesh payloads are cold during slot discovery and culling. */
	game_voxel_chunk chunk;
	voxel_light_chunk light;
	chunk_mesh mesh;
	std::shared_ptr<const WorldChunkReadState> read_state;

	WorldChunk();
	WorldChunk(const WorldChunk &other);
	~WorldChunk();
	WorldChunk &operator=(const WorldChunk &other);
	int32_t move(WorldChunk &other) noexcept;
	void cancel_remesh_work() noexcept;
	void mark_content_changed() noexcept;
	void mark_light_input_changed() noexcept;
	void set_light_dependency(int32_t chunk_x, int32_t chunk_z) noexcept;
	void clear_light_dependency() noexcept;
	bool light_buffer_is_valid() const noexcept;
	bool light_is_current() const noexcept;
	uint8_t get_boundary_source_light_max(uint8_t face) const noexcept;
	bool boundary_has_transparent_target(uint8_t face) const noexcept;
	int32_t publish_read_state() noexcept;
	int32_t publish_read_state_after_block_edit(int32_t local_x,
		int32_t local_y, int32_t local_z, uint32_t block_id) noexcept;
	void set_pending_remesh_request(uint64_t request_id,
		ft_bool interactive) noexcept;
	void clear_pending_remesh_request() noexcept;
	bool owns_current_interactive_remesh() const noexcept;
	void set_border_mesh_publication_pending(bool pending) noexcept;
	bool border_mesh_publication_is_current() const noexcept;

	void reset_coordinates();
	void destroy();
	static bool mesh_is_drawable(const chunk_mesh &mesh) noexcept;
};

#endif
