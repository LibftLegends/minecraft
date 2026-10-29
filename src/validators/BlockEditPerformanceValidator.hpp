#ifndef BLOCK_EDIT_PERFORMANCE_VALIDATOR_HPP
# define BLOCK_EDIT_PERFORMANCE_VALIDATOR_HPP

# include "../../src/diagnostics/BlockEditWorkloadAnalytics.hpp"
# include "../../src/validators/IValidator.hpp"
# include "../../src/world/World.hpp"

class BlockEditPerformanceValidator : public IValidator
{
  private:
	static uint64_t count_dirty_remeshes(const World &world) noexcept;
	static uint64_t counter_delta(uint64_t before, uint64_t after) noexcept;
	static int32_t capture_frame(World &world, uint64_t frame_index,
		const World::StreamDiagnostics &before,
		std::vector<BlockEditWorkloadFrame> &frames) noexcept;
	static int32_t wait_for_revision(World &world, int32_t world_x,
		int32_t world_z, uint64_t previous_revision, uint64_t frame_index,
		std::vector<BlockEditWorkloadFrame> &frames,
		uint64_t &latency_us) noexcept;
	static int32_t prepare_target(World &world, int32_t world_x,
		int32_t world_z, int32_t &world_y) noexcept;
	static int32_t restore_block(World &world, int32_t world_x,
		int32_t world_y, int32_t world_z, uint64_t &frame_index,
		std::vector<BlockEditWorkloadFrame> &frames) noexcept;
	static int32_t run_workload(World &world, int32_t world_y,
		uint64_t requested_edits,
		std::vector<BlockEditWorkloadFrame> &frames,
		std::vector<uint64_t> &latencies_us, uint64_t &completed_edits,
		uint64_t &failed_edits) noexcept;

  public:
	BlockEditPerformanceValidator();
	BlockEditPerformanceValidator(const BlockEditPerformanceValidator &other);
	~BlockEditPerformanceValidator();
	BlockEditPerformanceValidator &operator=(
		const BlockEditPerformanceValidator &other);

	virtual int validate() const override;
};

#endif
