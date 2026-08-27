#ifndef WORLD_REGENERATION_PROGRESS_HPP
# define WORLD_REGENERATION_PROGRESS_HPP

# include "../ft_vox.hpp"

class WorldRegenerationProgress
{
  public:
	WorldRegenerationProgress();
	WorldRegenerationProgress(const WorldRegenerationProgress &other);
	~WorldRegenerationProgress();
	WorldRegenerationProgress &operator=(const WorldRegenerationProgress &other);

	void reset() noexcept;
	void begin(uint64_t generation_epoch) noexcept;
	void cancel(uint64_t next_generation_epoch) noexcept;
	bool active() const noexcept;
	uint64_t generation_epoch() const noexcept;
	bool is_active_for(uint64_t relevance_epoch) const noexcept;
	void set_job_count(std::size_t job_count) noexcept;
	std::size_t job_count() const noexcept;
	void record_completed() noexcept;
	void record_error(int32_t error_code) noexcept;
	void record_skipped() noexcept;
	void record_success() noexcept;
	bool all_jobs_done() const noexcept;
	int32_t error() const noexcept;
	int32_t regenerated_count() const noexcept;
	int32_t skipped_count() const noexcept;
	void finish() noexcept;

  private:
	bool active_;
	uint64_t generation_epoch_;
	std::size_t job_count_;
	std::size_t completed_job_count_;
	int32_t regenerated_count_;
	int32_t skipped_count_;
	int32_t error_;
};

#endif
