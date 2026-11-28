#ifndef GAME_SESSION_SNAPSHOT_WRITER_HPP
# define GAME_SESSION_SNAPSHOT_WRITER_HPP

# include <cstdint>
# include "../../Libft/Modules/Errno/errno.hpp"

struct GameSessionSnapshotWriter
{
	int32_t (*callback)(void *context) noexcept;
	void *context;
	int32_t (*deferred_callback)(void *context) noexcept = nullptr;
	int32_t (*mining_prepare_callback)(void *context, uint64_t request_id) noexcept = nullptr;

	int32_t save() const noexcept
	{
		if (this->callback == nullptr || this->context == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		return (this->callback(this->context));
	}

	int32_t save_deferred() const noexcept
	{
		if (this->deferred_callback == nullptr)
			return (this->save());
		if (this->context == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		return (this->deferred_callback(this->context));
	}

	int32_t save_mining_prepare(uint64_t request_id) const noexcept
	{
		if (this->mining_prepare_callback == nullptr)
			return (this->save_deferred());
		if (this->context == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		return (this->mining_prepare_callback(this->context, request_id));
	}
};

#endif
