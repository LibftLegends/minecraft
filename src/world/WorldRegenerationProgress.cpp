#include "../../src/world/WorldRegenerationProgress.hpp"

WorldRegenerationProgress::WorldRegenerationProgress() : active_(false),
	generation_epoch_(0U), job_count_(0U), completed_job_count_(0U),
	regenerated_count_(0), skipped_count_(0), error_(FT_ERR_SUCCESS)
{
}

WorldRegenerationProgress::WorldRegenerationProgress(const WorldRegenerationProgress &other) : active_(false),
	generation_epoch_(0U), job_count_(0U), completed_job_count_(0U),
	regenerated_count_(0), skipped_count_(0), error_(FT_ERR_SUCCESS)
{
	(void)other;
}

WorldRegenerationProgress::~WorldRegenerationProgress()
{
}

WorldRegenerationProgress &WorldRegenerationProgress::operator=(const WorldRegenerationProgress &other)
{
	(void)other;
	return (*this);
}

void WorldRegenerationProgress::reset() noexcept
{
	this->active_ = false;
	this->job_count_ = 0U;
	this->completed_job_count_ = 0U;
	this->regenerated_count_ = 0;
	this->skipped_count_ = 0;
	this->error_ = FT_ERR_SUCCESS;
}

void WorldRegenerationProgress::begin(uint64_t generation_epoch) noexcept
{
	this->active_ = true;
	this->generation_epoch_ = generation_epoch;
	this->job_count_ = 0U;
	this->completed_job_count_ = 0U;
	this->regenerated_count_ = 0;
	this->skipped_count_ = 0;
	this->error_ = FT_ERR_SUCCESS;
}

void WorldRegenerationProgress::cancel(uint64_t next_generation_epoch) noexcept
{
	this->generation_epoch_ = next_generation_epoch;
	this->active_ = false;
}

bool WorldRegenerationProgress::active() const noexcept
{
	return (this->active_);
}

uint64_t WorldRegenerationProgress::generation_epoch() const noexcept
{
	return (this->generation_epoch_);
}

bool WorldRegenerationProgress::is_active_for(uint64_t relevance_epoch) const noexcept
{
	return (this->active_ && relevance_epoch == this->generation_epoch_);
}

void WorldRegenerationProgress::set_job_count(std::size_t job_count) noexcept
{
	this->job_count_ = job_count;
}

std::size_t WorldRegenerationProgress::job_count() const noexcept
{
	return (this->job_count_);
}

void WorldRegenerationProgress::record_completed() noexcept
{
	this->completed_job_count_ += 1U;
}

void WorldRegenerationProgress::record_error(int32_t error_code) noexcept
{
	if (this->error_ == FT_ERR_SUCCESS)
		this->error_ = error_code;
}

void WorldRegenerationProgress::record_skipped() noexcept
{
	this->skipped_count_ += 1;
}

void WorldRegenerationProgress::record_success() noexcept
{
	this->regenerated_count_ += 1;
}

bool WorldRegenerationProgress::all_jobs_done() const noexcept
{
	return (this->completed_job_count_ >= this->job_count_);
}

int32_t WorldRegenerationProgress::error() const noexcept
{
	return (this->error_);
}

int32_t WorldRegenerationProgress::regenerated_count() const noexcept
{
	return (this->regenerated_count_);
}

int32_t WorldRegenerationProgress::skipped_count() const noexcept
{
	return (this->skipped_count_);
}

void WorldRegenerationProgress::finish() noexcept
{
	this->active_ = false;
}
