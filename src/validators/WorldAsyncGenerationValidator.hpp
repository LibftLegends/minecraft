#ifndef WORLD_ASYNC_GENERATION_VALIDATOR_HPP
# define WORLD_ASYNC_GENERATION_VALIDATOR_HPP

# include "../../src/validators/IValidator.hpp"
# include "../../src/world/World.hpp"

class WorldAsyncGenerationValidator : public IValidator
{
  private:
	static bool chunks_equal(const game_voxel_chunk &left,
		const game_voxel_chunk &right) noexcept;
	static bool mesh_payload_is_valid(const chunk_mesh &mesh) noexcept;
	static int validate_diagonal_lighting_halo() noexcept;
	static int validate_cardinal_lighting_propagation() noexcept;
	static int validate_diagonal_lighting_propagation() noexcept;
	static int validate_light_scheduler_configuration() noexcept;
	static int validate_remesh_priority_metrics() noexcept;
	static bool playable_area_is_ready(const World &world) noexcept;
	static void report_playable_area_gaps(const World &world) noexcept;
	static const WorldChunk *stream_until_ready(World &world,
		int32_t *frame, bool *startup_edit_applied,
		std::size_t *remesh_queue_peak,
		int32_t *first_visible_mesh_frame,
		bool *interactive_priority_observed,
		bool *generation_progress_with_priority_pending) noexcept;
	static void report_failure(const World &world, int32_t frame) noexcept;

  public:
	WorldAsyncGenerationValidator();
	WorldAsyncGenerationValidator(const WorldAsyncGenerationValidator &other);
	~WorldAsyncGenerationValidator();
	WorldAsyncGenerationValidator &operator=(const WorldAsyncGenerationValidator &other);

	int validate() const override;
};

#endif
