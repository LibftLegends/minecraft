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
	static int prepare_edit_target(World &world, int32_t x, int32_t z,
		int32_t &y, size_t &mesh_before);
	static int verify_place_block(World &world, int32_t x, int32_t y, int32_t z,
		size_t &mesh_after);
	static int verify_delete_block(World &world, int32_t x, int32_t y,
		int32_t z, size_t &mesh_after);
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
