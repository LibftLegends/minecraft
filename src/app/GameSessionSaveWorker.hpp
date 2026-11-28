#ifndef GAME_SESSION_SAVE_WORKER_HPP
# define GAME_SESSION_SAVE_WORKER_HPP

# include <condition_variable>
# include <cstdint>
# include <mutex>
# include <string>
# include <thread>
# include <vector>
# include "../world/WorldTerrainChunkStore.hpp"

class GameSessionSaveWorker
{
  public:
	GameSessionSaveWorker() = default;
	~GameSessionSaveWorker() noexcept;
	GameSessionSaveWorker(const GameSessionSaveWorker &) = delete;
	GameSessionSaveWorker &operator=(const GameSessionSaveWorker &) = delete;

	/* Transfers bytes on success. A result must be collected before the next
	 * write can start, including when the disk operation has already finished. */
	int32_t submit(const char *path, std::vector<uint8_t> &snapshot,
		std::unique_ptr<WorldTerrainChunkStore::PreparedEdit> terrain = nullptr) noexcept;
	int32_t collect(bool wait, bool *pending = nullptr,
		std::unique_ptr<WorldTerrainChunkStore::PreparedEdit> *terrain = nullptr) noexcept;

  private:
	enum class State { IDLE, QUEUED, WRITING, FINISHED };
	void run() noexcept;

	std::mutex mutex_;
	std::condition_variable wake_;
	std::thread worker_;
	std::string path_;
	std::vector<uint8_t> snapshot_;
	std::unique_ptr<WorldTerrainChunkStore::PreparedEdit> terrain_;
	State state_ = State::IDLE;
	bool stopping_ = false;
	int32_t result_ = 0;
};

#endif
