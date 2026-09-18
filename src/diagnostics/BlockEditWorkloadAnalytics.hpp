#ifndef BLOCK_EDIT_WORKLOAD_ANALYTICS_HPP
# define BLOCK_EDIT_WORKLOAD_ANALYTICS_HPP

# include <cstdint>
# include <vector>

struct BlockEditWorkloadFrame
{
	uint64_t frame_index;
	uint64_t world_drain_us;
	uint64_t frame_total_us;
	uint64_t light_nodes_processed;
	uint64_t snapshot_bytes;
	uint64_t light_queue_peak;
	uint64_t dirty_remesh_count;
	uint64_t stale_results;
	uint64_t remesh_completed;
};

class BlockEditWorkloadAnalytics
{
  public:
	BlockEditWorkloadAnalytics();
	BlockEditWorkloadAnalytics(const BlockEditWorkloadAnalytics &other);
	~BlockEditWorkloadAnalytics();
	BlockEditWorkloadAnalytics &operator=(
		const BlockEditWorkloadAnalytics &other);

	static int32_t write_report(const char *path,
		const std::vector<BlockEditWorkloadFrame> &frames,
		const std::vector<uint64_t> &edit_latencies_us,
		uint64_t requested_edits, uint64_t completed_edits,
		uint64_t failed_edits) noexcept;
};

#endif
