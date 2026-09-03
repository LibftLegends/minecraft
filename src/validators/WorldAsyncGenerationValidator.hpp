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
	static bool playable_area_is_ready(const World &world) noexcept;
	static void report_playable_area_gaps(const World &world) noexcept;
	static const WorldChunk *stream_until_ready(World &world,
		int32_t *frame) noexcept;
	static void report_failure(const World &world, int32_t frame) noexcept;

  public:
	WorldAsyncGenerationValidator();
	WorldAsyncGenerationValidator(const WorldAsyncGenerationValidator &other);
	~WorldAsyncGenerationValidator();
	WorldAsyncGenerationValidator &operator=(const WorldAsyncGenerationValidator &other);

	int validate() const override;
};

#endif
