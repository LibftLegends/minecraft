#ifndef WORLD_CHUNK_HPP
# define WORLD_CHUNK_HPP

# ifndef GAME_USE_VOXEL_REGION_BACKEND
#  define GAME_USE_VOXEL_REGION_BACKEND
# endif
# include "../ft_vox.hpp"
# include "../world/WorldLightVersion.hpp"
# include <array>
# include <atomic>
# include <memory>
# include <vector>

struct WorldChunkBlockEdit
{
	int32_t local_x;
	int32_t local_y;
	int32_t local_z;
	uint32_t expected_block_id;
	uint32_t replacement_block_id;
};

struct WorldChunkReadState
{
	static constexpr std::size_t LIGHT_SAMPLE_COUNT = GAME_VOXEL_CHUNK_WIDTH
		* GAME_VOXEL_CHUNK_HEIGHT * GAME_VOXEL_CHUNK_DEPTH;
	static constexpr std::size_t light_offset(int32_t x, int32_t y, int32_t z) noexcept
	{
		return ((static_cast<std::size_t>(z) * GAME_VOXEL_CHUNK_HEIGHT + y)
			* GAME_VOXEL_CHUNK_WIDTH + x);
	}

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
	using BlockSection = std::array<uint32_t, GAME_VOXEL_SECTION_BLOCKS>;
	/* Readers own the sections they captured. An edit replaces only the
	 * affected section and shares the previously published light samples. */
	std::array<std::shared_ptr<const BlockSection>,
		GAME_VOXEL_CHUNK_SECTION_COUNT> block_sections;
	std::shared_ptr<const std::vector<uint8_t>> light;
	uint8_t edited_faces;

	uint32_t block_at(int32_t x, int32_t y, int32_t z) const noexcept;
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
	bool pending_mesh_request_preview_only = false;
	uint64_t pending_mesh_request_voxel_revision;
	uint16_t pending_mesh_request_content_version;
	uint16_t pending_mesh_request_light_input_version;
	ft_bool last_mesh_publication_interactive;
	uint64_t last_mesh_publication_voxel_revision;
	struct BorderDeleteDependency
	{
		uint64_t source_voxel_revision;
		uint64_t receiver_mesh_revision;
	};
	/* A published mesh carries the requirements for the edits it actually
	 * contains. Later edits accumulate separately until that mesh is built. */
	std::array<BorderDeleteDependency, 4> border_delete_dependencies = {};
	std::array<BorderDeleteDependency, 4> published_border_delete_dependencies = {};
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
	void record_mesh_publication(uint64_t revision) noexcept;
	void mark_content_changed() noexcept;
	void mark_content_changed(bool light_input_changed) noexcept;
	void mark_light_input_changed() noexcept;
	void set_light_dependency(int32_t chunk_x, int32_t chunk_z) noexcept;
	void clear_light_dependency() noexcept;
	bool light_buffer_is_valid() const noexcept;
	bool light_is_current() const noexcept;
	uint8_t get_boundary_source_light_max(uint8_t face) const noexcept;
	bool boundary_has_transparent_target(uint8_t face) const noexcept;
	int32_t publish_read_state() noexcept;
	int32_t prepare_read_state_after_light_patch(
		const std::shared_ptr<const std::vector<uint8_t>> &updated_samples,
		std::shared_ptr<WorldChunkReadState> &prepared_state) const noexcept;
	int32_t prepare_read_state_after_block_edit(int32_t local_x,
		int32_t local_y, int32_t local_z, uint32_t block_id,
		std::shared_ptr<WorldChunkReadState> &prepared_state) noexcept;
	int32_t prepare_read_state_after_block_edits(const WorldChunkBlockEdit *edits,
		std::size_t count, std::shared_ptr<WorldChunkReadState> &prepared_state) noexcept;
	void commit_prepared_read_state(
		std::shared_ptr<WorldChunkReadState> &prepared_state,
		uint8_t changed_light_faces = 0U) noexcept;
	int32_t publish_read_state_after_block_edit(int32_t local_x,
		int32_t local_y, int32_t local_z, uint32_t block_id) noexcept;
	void set_pending_remesh_request(uint64_t request_id,
		ft_bool interactive) noexcept;
	void clear_pending_remesh_request() noexcept;
	bool owns_current_interactive_remesh() const noexcept;

	void reset_coordinates();
	void destroy();
	static bool mesh_is_drawable(const chunk_mesh &mesh) noexcept;
};

#endif
