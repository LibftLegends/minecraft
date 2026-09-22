#ifndef LIGHTING_TEST_HARNESS_HPP
# define LIGHTING_TEST_HARNESS_HPP

# include "../../Libft/Modules/Errno/errno.hpp"
# include "../../src/validators/IValidator.hpp"
# include <cstdint>
# include <deque>
# include <string>
# include <vector>

class World;

struct LightingFrameObservation
{
	uint64_t frame_index;
	uint64_t edit_id;
	int32_t chunk_x;
	int32_t chunk_z;
	uint64_t voxel_revision;
	uint64_t light_revision;
	uint16_t content_version;
	uint16_t light_version;
	uint16_t light_input_version;
	uint16_t computed_light_input_version;
	uint64_t mesh_revision;
	uint64_t pending_mesh_request_id;
	uint64_t pending_mesh_request_voxel_revision;
	uint16_t pending_mesh_request_content_version;
	uint16_t pending_mesh_request_light_input_version;
	bool block_state_visible;
	bool light_ready_for_render;
	bool light_buffer_valid;
	bool light_current;
	bool mesh_dirty;
	bool remesh_pending;
	uint8_t minimum_vertex_light;
	uint8_t maximum_vertex_light;
	uint32_t nonzero_light_vertices;
	uint32_t visible_face_count;
	size_t remesh_queue_depth;
	size_t priority_queue_depth;
	size_t interactive_queue_depth;
	size_t active_generation_count;
	size_t playable_failed_count;
	size_t playable_required_count;
	size_t playable_drawable_count;
	size_t deferred_edit_count;
	size_t deferred_edit_cursor;
	size_t candidate_count;
	size_t ready_count;
	size_t pending_count;
	size_t retryable_count;
	size_t failed_count;
	uint64_t stream_frame;
	uint64_t stream_progress_frame;
	uint64_t remesh_starvation_promotions;
	uint64_t oldest_remesh_queue_age;
	uint64_t remesh_snapshot_bytes;
	uint64_t remesh_capture_duration_nanoseconds;
	uint64_t remesh_capture_count;
	uint64_t remesh_light_queue_peak;
	uint64_t remesh_geometry_only_count;
	uint64_t oldest_result_age_nanoseconds;
	uint64_t oldest_pending_age;
	uint64_t remesh_queue_peak;
	uint64_t stale_remesh_count;
	uint64_t stale_remesh_capture_count;
	uint64_t stale_remesh_dependency_count;
	uint64_t stale_remesh_pending_count;
	uint64_t stale_remesh_revision_count;
	uint64_t remesh_completed_count;
	uint64_t remesh_incremental_completed_count;
	uint64_t remesh_full_completed_count;
	uint64_t remesh_canceled_count;
	uint64_t remesh_scanned_cells;
	uint64_t remesh_propagated_cells;
	int32_t stream_last_error;
};

struct LightingEditObservation
{
	uint64_t edit_id;
	uint64_t start_frame;
	uint64_t authoritative_frame;
	uint64_t light_frame;
	uint64_t mesh_frame;
	uint64_t authoritative_elapsed_milliseconds;
	uint64_t light_elapsed_milliseconds;
	uint64_t light_stage_elapsed_milliseconds;
	uint64_t mesh_elapsed_milliseconds;
	uint64_t visible_elapsed_milliseconds;
	uint64_t visible_stage_elapsed_milliseconds;
	uint64_t elapsed_milliseconds;
	uint64_t observed_frames;
	uint64_t visible_frame;
	uint64_t start_scanned_cells;
	uint64_t end_scanned_cells;
	uint64_t start_incremental_completed_count;
	uint64_t end_incremental_completed_count;
	uint64_t start_full_completed_count;
	uint64_t end_full_completed_count;
	bool incremental_light_publication;
	int32_t world_x;
	int32_t world_y;
	int32_t world_z;
	bool superseded;
	uint64_t superseded_frame;
	bool completed;
};

struct LightingScenarioObservation
{
	std::string name;
	int32_t result;
};

class LightingObservationHarness
{
  private:
	std::deque<LightingFrameObservation> frames_;
	std::vector<LightingFrameObservation> last_observations_;
	std::vector<LightingEditObservation> edits_;
	std::vector<LightingScenarioObservation> scenarios_;
	std::size_t frame_retention_limit_;
	uint64_t dropped_frame_count_;
	int32_t storage_error_;
	int32_t observation_invariant_error_;

	static int32_t mesh_light_summary(const World &world,
		int32_t chunk_x, int32_t chunk_z, uint8_t *minimum,
		uint8_t *maximum, uint32_t *nonzero, uint32_t *faces) noexcept;
	static bool revision_is_older(uint64_t current, uint64_t previous) noexcept;
	static int32_t publication_transition_error(
		const LightingFrameObservation &previous,
		const LightingFrameObservation &current) noexcept;

  public:
	LightingObservationHarness();
	LightingObservationHarness(const LightingObservationHarness &other);
	~LightingObservationHarness();
	LightingObservationHarness &operator=(
		const LightingObservationHarness &other);

	int32_t observe(const World &world, int32_t chunk_x, int32_t chunk_z,
		uint64_t frame_index, uint64_t edit_id) noexcept;
	int32_t begin_edit(uint64_t edit_id, uint64_t frame_index,
		int32_t world_x, int32_t world_y, int32_t world_z,
		uint64_t scanned_cells, uint64_t incremental_completed_count,
		uint64_t full_completed_count) noexcept;
	int32_t mark_authoritative(uint64_t edit_id, uint64_t frame_index,
		uint64_t elapsed_milliseconds) noexcept;
	int32_t mark_light_ready(uint64_t edit_id, uint64_t frame_index,
		uint64_t elapsed_milliseconds) noexcept;
	int32_t mark_mesh_ready(uint64_t edit_id, uint64_t frame_index,
		uint64_t elapsed_milliseconds, uint64_t scanned_cells,
		uint64_t incremental_completed_count,
		uint64_t full_completed_count,
		bool incremental_light_publication) noexcept;
	int32_t mark_superseded(uint64_t edit_id, uint64_t frame_index,
		uint64_t elapsed_milliseconds) noexcept;
	int32_t validate_publication_invariants() const noexcept;
	int32_t validate_latency(uint64_t maximum_frames,
		uint64_t maximum_milliseconds,
		std::size_t minimum_samples = 1U) const noexcept;
	int32_t write_report(const char *path) const noexcept;
	int32_t record_scenario(const char *name, int32_t result) noexcept;
	int32_t set_frame_retention_limit(std::size_t maximum_frames) noexcept;

	const std::deque<LightingFrameObservation> &frames() const noexcept;
	const std::vector<LightingEditObservation> &edits() const noexcept;
};

class LightingTestHarness : public IValidator
{
  private:
	static int validate_initial_light_oracle() noexcept;
	static int validate_edit_matrix() noexcept;

  public:
	LightingTestHarness();
	LightingTestHarness(const LightingTestHarness &other);
	~LightingTestHarness();
	LightingTestHarness &operator=(const LightingTestHarness &other);

	static int validate_stress(uint32_t edit_count,
		const char *report_suffix) noexcept;
	static int validate_lifecycle() noexcept;
	int validate() const override;
};

#endif
