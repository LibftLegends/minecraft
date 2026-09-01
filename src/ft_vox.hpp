#ifndef FT_VOX_HPP
# define FT_VOX_HPP

# include "../Libft/Modules/Buffer/byte_buffer.hpp"
# include "../Libft/Modules/DUMB/controls.hpp"
# include "../Libft/Modules/DUMB/render_window.hpp"
# include "../Libft/Modules/Debug/debug.hpp"
# include "../Libft/Modules/Errno/errno.hpp"
# include "../Libft/Modules/GPGR/ft_gpu_shader.hpp"
# include "../Libft/Modules/GPGR/ft_gpu_window.hpp"
# include "../Libft/Modules/GPGR/gpgr_gl_funcs.hpp"
# include "../Libft/Modules/Game/game_block_edit_op.hpp"
# include "../Libft/Modules/Game/game_voxel_chunk.hpp"
# include "../Libft/Modules/JSon/json.hpp"
# include "../Libft/Modules/Voxel/voxel_api.hpp"
# include "../Libft/Modules/Voxel/voxel_mesh.hpp"
# include "../Libft/Modules/Template/unique_ptr.hpp"

# include <algorithm>
# include <chrono>
# include <cmath>
# include <cstddef>
# include <cstdint>
# include <cstdio>
# include <cstdlib>
# include <cstring>
# include <ctime>
# include <functional>
# include <memory>
# include <string>
# include <thread>
# include <vector>

# if defined(_WIN32)
#  ifndef NOMINMAX
#   define NOMINMAX
#  endif
#  include <windows.h>
# elif defined(__APPLE__)
#  include <Carbon/Carbon.h>
#  include <mach/mach.h>
#  include <mach/task.h>
# endif

#endif
