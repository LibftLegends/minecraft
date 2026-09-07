#ifndef BLOCK_EDIT_VALIDATOR_HPP
# define BLOCK_EDIT_VALIDATOR_HPP

# include "../../src/coordinates/WorldCoordinates.hpp"
# include "../../src/diagnostics/ApplicationError.hpp"
# include "../../src/validators/IValidator.hpp"
# include "../../src/world/World.hpp"
# include "../ft_vox.hpp"

class BlockEditValidator : public IValidator
{
  private:
	static size_t mesh_index_count_for_block(const World &world,
		int32_t block_x, int32_t block_z);
	static uint64_t mesh_revision_for_block(const World &world,
		int32_t block_x, int32_t block_z);
	static int wait_for_mesh_revision(World &world, int32_t block_x,
		int32_t block_z, uint64_t previous_revision,
		uint64_t *latency_milliseconds = nullptr) noexcept;
	static int wait_for_region_convergence(World &world, int32_t block_x,
		int32_t block_z, uint64_t previous_revision) noexcept;
	static int wait_for_boundary_convergence(World &world, int32_t block_x,
		int32_t block_z, const uint64_t previous_revisions[4],
		const uint64_t previous_light_revisions[4]) noexcept;
	static int wait_for_boundary_idle(World &world, int32_t block_x,
		int32_t block_z) noexcept;
	static int prepare_edit_target(World &world, int32_t x, int32_t z,
		int32_t &y, size_t &mesh_before);
	static int verify_place_block(World &world, int32_t x, int32_t y, int32_t z,
		size_t &mesh_after);
	static int verify_delete_block(World &world, int32_t x, int32_t y,
		int32_t z, size_t &mesh_after);
	static int validate_repeated_edits(World &world, int32_t x, int32_t y,
		int32_t z) noexcept;
	static int validate_boundary_edit(World &world) noexcept;
	static int report_mesh_result(size_t before, size_t after_place,
		size_t after_delete);

  public:
	BlockEditValidator();
	BlockEditValidator(const BlockEditValidator &other);
	~BlockEditValidator();
	BlockEditValidator &operator=(const BlockEditValidator &other);

	virtual int validate() const override;
};

#endif
