LIBFT_DIR    = Libft
LIBFT_UNAME_S := $(shell uname -s)

LIBFT_COMPILE_FLAGS = -Wall -Wextra -Werror -std=c++17 \
                      -Wmissing-declarations -Wshadow -Wformat=2 -Wundef \
                      -Wfloat-equal -Wodr \
                      -DLIBFT_INTERNAL_HEADERS -DGAME_USE_VOXEL_REGION_BACKEND $(OPT_FLAGS)

ifeq ($(OS),Windows_NT)
    LIBFT_COMPILE_FLAGS += -Wold-style-cast -Wconversion -Wuseless-cast \
                           -Wzero-as-null-pointer-constant -Wmaybe-uninitialized
else ifeq ($(LIBFT_UNAME_S),Darwin)
    LIBFT_COMPILE_FLAGS += -Wno-format-nonliteral -Wno-tautological-compare
else
    LIBFT_COMPILE_FLAGS += -Wold-style-cast -Wconversion -Wuseless-cast \
                           -Wzero-as-null-pointer-constant -Wmaybe-uninitialized
endif

LIBFT_ARCHIVE_SUFFIX      := _ft_vox_cfg$(FT_VOX_CONFIG_FINGERPRINT)
LIBFT_FULL_LIB           = $(LIBFT_DIR)/Full_Libft$(LIBFT_ARCHIVE_SUFFIX).a
LIBFT_FULL_DEBUG_LIB     = $(LIBFT_DIR)/Full_Libft_debug$(LIBFT_ARCHIVE_SUFFIX).a
ifeq ($(DEBUG),1)
LIBFT_LINK_LIB           = $(LIBFT_FULL_DEBUG_LIB)
else
LIBFT_LINK_LIB           = $(LIBFT_FULL_LIB)
endif
LIBFT_LINK_FLAGS         = $(LIBFT_LINK_LIB)
LIBFT_BUILD_OUTPUT_SUFFIX = _ft_vox_opt$(OPT_LEVEL)_cfg$(FT_VOX_CONFIG_FINGERPRINT)

LIBFT_JOBS ?= 4
LIBFT_PARALLEL_FLAGS = $(if $(filter -j%,$(MAKEFLAGS)),,-j$(LIBFT_JOBS))
LIBFT_CHILD_BATCH_OUTPUT = $(if $(filter -j%,$(MAKEFLAGS)),$(FT_VOX_BATCH_OUTPUT),$(if $(filter 1,$(LIBFT_JOBS)),0,1))
