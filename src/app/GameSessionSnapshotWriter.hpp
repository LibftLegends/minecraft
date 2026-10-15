#ifndef GAME_SESSION_SNAPSHOT_WRITER_HPP
# define GAME_SESSION_SNAPSHOT_WRITER_HPP

# include <cstdint>
# include "../../Libft/Modules/Errno/errno.hpp"

struct GameSessionSnapshotWriter
{
	int32_t (*callback)(void *context) noexcept;
	void *context;

	int32_t save() const noexcept
	{
		if (this->callback == nullptr || this->context == nullptr)
			return (FT_ERR_INVALID_ARGUMENT);
		return (this->callback(this->context));
	}
};

#endif
