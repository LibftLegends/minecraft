#include "GameSessionSaveWorker.hpp"
#include "GameSessionPersistence.hpp"

GameSessionSaveWorker::~GameSessionSaveWorker() noexcept
{
	{
		std::lock_guard<std::mutex> lock(this->mutex_);
		this->stopping_ = true;
	}
	this->wake_.notify_all();
	if (this->worker_.joinable())
		this->worker_.join();
}

int32_t GameSessionSaveWorker::submit(const char *path,
	std::vector<uint8_t> &snapshot,
	std::unique_ptr<WorldTerrainChunkStore::PreparedEdit> terrain) noexcept
{
	if (path == nullptr || path[0] == '\0' || snapshot.empty())
		return (FT_ERR_INVALID_ARGUMENT);
	{
		std::lock_guard<std::mutex> lock(this->mutex_);
		if (this->state_ != State::IDLE)
			return (FT_ERR_THREAD_BUSY);
		try
		{
			this->path_.assign(path);
			if (!this->worker_.joinable())
				this->worker_ = std::thread(&GameSessionSaveWorker::run, this);
		}
		catch (...)
		{
			return (FT_ERR_NO_MEMORY);
		}
		this->snapshot_.swap(snapshot);
		this->terrain_ = std::move(terrain);
		this->state_ = State::QUEUED;
	}
	this->wake_.notify_all();
	return (FT_ERR_SUCCESS);
}

int32_t GameSessionSaveWorker::collect(bool wait, bool *pending,
	std::unique_ptr<WorldTerrainChunkStore::PreparedEdit> *terrain) noexcept
{
	std::unique_lock<std::mutex> lock(this->mutex_);
	if (pending != nullptr)
		*pending = false;
	if (this->state_ == State::IDLE)
		return (FT_ERR_SUCCESS);
	if (wait)
		this->wake_.wait(lock, [this]()
		{
			return this->state_ == State::FINISHED || this->state_ == State::IDLE;
		});
	if (this->state_ != State::FINISHED)
	{
		if (pending != nullptr && this->state_ != State::IDLE)
			*pending = true;
		return (FT_ERR_SUCCESS);
	}
	if (terrain != nullptr)
		*terrain = std::move(this->terrain_);
	else
		this->terrain_.reset();
	this->state_ = State::IDLE;
	return (this->result_);
}

void GameSessionSaveWorker::run() noexcept
{
	while (true)
	{
		std::string path;
		std::vector<uint8_t> snapshot;
		std::unique_ptr<WorldTerrainChunkStore::PreparedEdit> terrain;
		{
			std::unique_lock<std::mutex> lock(this->mutex_);
			this->wake_.wait(lock, [this]()
			{
				return this->stopping_ || this->state_ == State::QUEUED;
			});
			if (this->state_ != State::QUEUED)
				return ;
			path.swap(this->path_);
			snapshot.swap(this->snapshot_);
			terrain = std::move(this->terrain_);
			this->state_ = State::WRITING;
		}
		int32_t result = FT_ERR_SUCCESS;
		if (terrain != nullptr)
		{
			result = terrain->file.prepare(terrain->destination, terrain->chunk_x,
				terrain->chunk_z, terrain->voxels);
			(void)terrain->voxels.destroy();
		}
		if (result == FT_ERR_SUCCESS)
			result = GameSessionPersistence::write_serialized_snapshot(path.c_str(), snapshot);
		if (result != FT_ERR_SUCCESS)
			terrain.reset();
		{
			std::lock_guard<std::mutex> lock(this->mutex_);
			this->result_ = result;
			this->terrain_ = std::move(terrain);
			this->state_ = State::FINISHED;
		}
		this->wake_.notify_all();
	}
}
