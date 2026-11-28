#include "../../src/diagnostics/BlockEditWorkloadAnalytics.hpp"
#include "../../Libft/Modules/Basic/limits.hpp"
#include "../../Libft/Modules/Errno/errno.hpp"
#include <algorithm>
#include <cstdio>

namespace
{
	static uint64_t percentile(const std::vector<uint64_t> &samples,
		uint64_t percentage) noexcept
	{
		size_t index;

		if (samples.empty())
			return (0U);
		index = (samples.size() - 1U) * percentage / 100U;
		return (samples[index]);
	}

	static uint64_t sum_frame_field(
		const std::vector<BlockEditWorkloadFrame> &frames,
		uint64_t BlockEditWorkloadFrame::*field) noexcept
	{
		uint64_t total;
		size_t index;

		total = 0U;
		index = 0U;
		while (index < frames.size())
		{
			total += frames[index].*field;
			index += 1U;
		}
		return (total);
	}

	static uint64_t maximum_frame_field(
		const std::vector<BlockEditWorkloadFrame> &frames,
		uint64_t BlockEditWorkloadFrame::*field) noexcept
	{
		uint64_t maximum;
		size_t index;

		maximum = 0U;
		index = 0U;
		while (index < frames.size())
		{
			if (frames[index].*field > maximum)
				maximum = frames[index].*field;
			index += 1U;
		}
		return (maximum);
	}
}

BlockEditWorkloadAnalytics::BlockEditWorkloadAnalytics()
{
}

BlockEditWorkloadAnalytics::BlockEditWorkloadAnalytics(
	const BlockEditWorkloadAnalytics &other)
{
	(void)other;
}

BlockEditWorkloadAnalytics::~BlockEditWorkloadAnalytics()
{
}

BlockEditWorkloadAnalytics &BlockEditWorkloadAnalytics::operator=(
	const BlockEditWorkloadAnalytics &other)
{
	(void)other;
	return (*this);
}

int32_t BlockEditWorkloadAnalytics::write_report(const char *path,
	const std::vector<BlockEditWorkloadFrame> &frames,
	const std::vector<uint64_t> &edit_latencies_us,
	uint64_t requested_edits, uint64_t completed_edits,
	uint64_t failed_edits) noexcept
{
	std::FILE *file;
	std::vector<uint64_t> sorted_latencies;
	size_t frame_index;
	uint64_t stale_total;
	uint64_t nodes_total;
	uint64_t snapshot_bytes_total;
	uint64_t completed_total;
	uint64_t drain_total;
	uint64_t queue_peak;
	uint64_t dirty_peak;
	int result;

	if (path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	file = std::fopen(path, "w");
	if (file == nullptr)
		return (FT_ERR_IO);
	sorted_latencies = edit_latencies_us;
	std::sort(sorted_latencies.begin(), sorted_latencies.end());
	stale_total = sum_frame_field(frames,
		&BlockEditWorkloadFrame::stale_results);
	nodes_total = sum_frame_field(frames,
		&BlockEditWorkloadFrame::light_nodes_processed);
	snapshot_bytes_total = sum_frame_field(frames,
		&BlockEditWorkloadFrame::snapshot_bytes);
	completed_total = sum_frame_field(frames,
		&BlockEditWorkloadFrame::remesh_completed);
	drain_total = sum_frame_field(frames,
		&BlockEditWorkloadFrame::world_drain_us);
	queue_peak = maximum_frame_field(frames,
		&BlockEditWorkloadFrame::light_queue_peak);
	dirty_peak = maximum_frame_field(frames,
		&BlockEditWorkloadFrame::dirty_remesh_count);
	result = std::fprintf(file,
		"{\"type\":\"metadata\",\"workload\":\"repeated_block_break\","
			"\"requested_edits\":" FT_UINT64_DECIMAL_FORMAT
			",\"completed_edits\":" FT_UINT64_DECIMAL_FORMAT
			",\"failed_edits\":" FT_UINT64_DECIMAL_FORMAT "}\n",
		requested_edits, completed_edits, failed_edits);
	if (result < 0)
	{
		std::fclose(file);
		return (FT_ERR_IO);
	}
	frame_index = 0U;
	while (frame_index < frames.size())
	{
		const BlockEditWorkloadFrame &frame = frames[frame_index];

		result = std::fprintf(file,
			"{\"type\":\"frame\",\"frame\":"
			FT_UINT64_DECIMAL_FORMAT ",\"world_drain_us\":"
			FT_UINT64_DECIMAL_FORMAT ",\"frame_total_us\":"
			FT_UINT64_DECIMAL_FORMAT ",\"light_nodes_processed\":"
			FT_UINT64_DECIMAL_FORMAT ",\"snapshot_bytes\":"
			FT_UINT64_DECIMAL_FORMAT ",\"light_queue_peak\":"
			FT_UINT64_DECIMAL_FORMAT ",\"dirty_remesh_count\":"
			FT_UINT64_DECIMAL_FORMAT ",\"stale_results\":"
			FT_UINT64_DECIMAL_FORMAT ",\"remesh_completed\":"
			FT_UINT64_DECIMAL_FORMAT "}\n", frame.frame_index,
			frame.world_drain_us, frame.frame_total_us,
			frame.light_nodes_processed, frame.snapshot_bytes,
			frame.light_queue_peak,
			frame.dirty_remesh_count, frame.stale_results,
			frame.remesh_completed);
		if (result < 0)
		{
			std::fclose(file);
			return (FT_ERR_IO);
		}
		frame_index += 1U;
	}
	result = std::fprintf(file,
		"{\"type\":\"summary\",\"frames\":%zu"
		",\"latency_samples\":%zu,\"latency_p50_us\":"
		FT_UINT64_DECIMAL_FORMAT ",\"latency_p95_us\":"
		FT_UINT64_DECIMAL_FORMAT ",\"latency_p99_us\":"
		FT_UINT64_DECIMAL_FORMAT ",\"world_drain_total_us\":"
		FT_UINT64_DECIMAL_FORMAT ",\"light_nodes_total\":"
		FT_UINT64_DECIMAL_FORMAT ",\"light_queue_peak\":"
		FT_UINT64_DECIMAL_FORMAT ",\"snapshot_bytes_total\":"
		FT_UINT64_DECIMAL_FORMAT ",\"dirty_remesh_peak\":"
		FT_UINT64_DECIMAL_FORMAT ",\"stale_results\":"
		FT_UINT64_DECIMAL_FORMAT ",\"remesh_completed\":"
		FT_UINT64_DECIMAL_FORMAT "}\n", frames.size(),
		sorted_latencies.size(),
		percentile(sorted_latencies, 50U), percentile(sorted_latencies, 95U),
		percentile(sorted_latencies, 99U), drain_total, nodes_total,
		queue_peak, snapshot_bytes_total, dirty_peak, stale_total,
		completed_total);
	if (result < 0)
	{
		std::fclose(file);
		return (FT_ERR_IO);
	}
	if (std::fclose(file) != 0)
		return (FT_ERR_IO);
	std::printf("block-edit-performance: report=%s frames=%zu edits=%zu "
		"p50_us=" FT_UINT64_DECIMAL_FORMAT
		" p95_us=" FT_UINT64_DECIMAL_FORMAT
		" p99_us=" FT_UINT64_DECIMAL_FORMAT
		" light_nodes=" FT_UINT64_DECIMAL_FORMAT
		" snapshot_bytes=" FT_UINT64_DECIMAL_FORMAT
		" queue_peak=" FT_UINT64_DECIMAL_FORMAT
		" dirty_peak=" FT_UINT64_DECIMAL_FORMAT
		" stale=" FT_UINT64_DECIMAL_FORMAT "\n", path, frames.size(),
		sorted_latencies.size(), percentile(sorted_latencies, 50U),
		percentile(sorted_latencies, 95U), percentile(sorted_latencies, 99U),
		nodes_total, snapshot_bytes_total, queue_peak, dirty_peak,
		stale_total);
	return (FT_ERR_SUCCESS);
}
