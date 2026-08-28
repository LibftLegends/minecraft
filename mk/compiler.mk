CC = g++

OPT_LEVEL ?= 2
UNAME_S    := $(shell uname -s)

FREETYPE_VERSION       ?= 2.14.3
FREETYPE_LOCAL_PREFIX  := $(CURDIR)/.deps/freetype/$(FREETYPE_VERSION)
FREETYPE_CFLAGS        :=
FREETYPE_LIBS          :=
HAVE_FREETYPE           := 0

ifneq ($(shell pkg-config --exists freetype2 2>/dev/null && echo yes),)
    FREETYPE_CFLAGS := $(shell pkg-config --cflags freetype2)
    FREETYPE_LIBS   := $(shell pkg-config --libs freetype2)
    HAVE_FREETYPE   := 1
else ifneq ($(wildcard $(FREETYPE_LOCAL_PREFIX)/include/freetype2/ft2build.h),)
    FREETYPE_CFLAGS := -I$(FREETYPE_LOCAL_PREFIX)/include/freetype2
    FREETYPE_LIBS   := -L$(FREETYPE_LOCAL_PREFIX)/lib -lfreetype
    HAVE_FREETYPE   := 1
endif

ifeq ($(OPT_LEVEL),0)
    OPT_FLAGS = -O0 -g
else ifeq ($(OPT_LEVEL),1)
    OPT_FLAGS = -O1
else ifeq ($(OPT_LEVEL),2)
    OPT_FLAGS = -O2
else ifeq ($(OPT_LEVEL),3)
    OPT_FLAGS = -O3
else
    $(error Unsupported OPT_LEVEL=$(OPT_LEVEL))
endif

COMPILE_FLAGS = -Wall -Werror -Wextra -std=c++17 -Wmissing-declarations \
                -Wold-style-cast -Wshadow -Wconversion -Wformat=2 -Wundef \
                -Wfloat-equal -Wzero-as-null-pointer-constant -Wodr \
                -I. -DLIBFT_INTERNAL_HEADERS -DGAME_USE_VOXEL_REGION_BACKEND $(OPT_FLAGS)

ifeq ($(HAVE_FREETYPE),1)
    COMPILE_FLAGS += -DFT_VOX_HAVE_FREETYPE=1 $(subst -I,-isystem ,$(FREETYPE_CFLAGS))
endif

ifeq ($(OS),Windows_NT)
    COMPILE_FLAGS += -Wuseless-cast -Wmaybe-uninitialized
    ifneq ($(OPT_LEVEL),0)
        COMPILE_FLAGS += -ffunction-sections -fdata-sections
    endif
else ifeq ($(UNAME_S),Darwin)
    COMPILE_FLAGS += -Wno-format-nonliteral -Wno-tautological-compare
    ifneq ($(OPT_LEVEL),0)
        COMPILE_FLAGS += -flto -ffunction-sections -fdata-sections
    endif
else
    COMPILE_FLAGS += -Wuseless-cast -Wmaybe-uninitialized
    ifneq ($(OPT_LEVEL),0)
        COMPILE_FLAGS += -flto -ffunction-sections -fdata-sections
    endif
endif

DEPFLAGS = -MMD -MP
CFLAGS = $(COMPILE_FLAGS)

REPRODUCIBLE ?= 1
SOURCE_DATE_EPOCH ?= 1700000000

ifneq ($(REPRODUCIBLE),0)
    export SOURCE_DATE_EPOCH
    COMPILE_FLAGS += -ffile-prefix-map=$(CURDIR)=. -fmacro-prefix-map=$(CURDIR)=. \
                     -fdebug-prefix-map=$(CURDIR)=. -frandom-seed=ft_vox
endif

ENABLE_LTO  ?= 0
ENABLE_PGO  ?= 0
COVERAGE    ?= 0
COVERAGE_LINE_THRESHOLD ?= 60
COVERAGE_BRANCH_THRESHOLD ?= 65
export ENABLE_LTO ENABLE_PGO COVERAGE

ifeq ($(ENABLE_LTO),1)
    COMPILE_FLAGS += -flto
    LDFLAGS       += -flto
endif

ifeq ($(ENABLE_PGO),1)
    COMPILE_FLAGS += -fprofile-generate
endif

ifeq ($(COVERAGE),1)
    COMPILE_FLAGS += --coverage
    LDFLAGS       += --coverage
endif

ifeq ($(DEBUG),1)
    CFLAGS          += -DDEBUG=1 -fno-omit-frame-pointer
    OBJ_DIR          = $(OBJ_DIR_DEBUG)
    TARGET           = $(NAME_DEBUG)
    LIBFT_LINK_LIB   = $(LIBFT_FULL_DEBUG_LIB)
    LIBFT_LINK_FLAGS = $(LIBFT_LINK_LIB)
else
    TARGET           = $(NAME)
endif

export COMPILE_FLAGS

ifeq ($(OS),Windows_NT)
    LDFLAGS = -lopengl32 -lgdi32 -lws2_32 -ldbghelp -lz
    ifneq ($(OPT_LEVEL),0)
        LDFLAGS += -s -Wl,--gc-sections
    endif
else ifeq ($(UNAME_S),Darwin)
    LDFLAGS = -lz -framework Cocoa -framework CoreGraphics \
              -framework ApplicationServices -framework QuartzCore \
              -framework AudioToolbox -framework OpenGL \
              -framework Carbon -lobjc -lpthread -lsqlite3
    ifneq ($(OPT_LEVEL),0)
        LDFLAGS += -flto -Wl,-dead_strip
    endif
else
    LDFLAGS = -lX11 -lXext -lXi -lGL -lz
    ifneq ($(OPT_LEVEL),0)
        LDFLAGS += -flto -s -Wl,--gc-sections
    endif
endif

ifeq ($(DEBUG),1)
    ifneq ($(OS),Windows_NT)
        ifneq ($(UNAME_S),Darwin)
            LDFLAGS += -rdynamic
        endif
    endif
endif

ifeq ($(HAVE_FREETYPE),1)
    LDFLAGS += $(FREETYPE_LIBS)
endif

# Native ft_vox objects must never be reused across incompatible compiler,
# feature, or link configurations.  Compute one invocation-level identity;
# do not launch a fingerprinting process per object.
FT_VOX_CONFIG_FINGERPRINT := $(shell printf '%s\n' \
    '$(CC)|$(CFLAGS)|$(COMPILE_FLAGS)|$(LDFLAGS)|$(OPT_LEVEL)|$(DEBUG)|$(COVERAGE)|$(ENABLE_LTO)|$(ENABLE_PGO)|$(HAVE_FREETYPE)|$(REPRODUCIBLE)' | \
    cksum | awk '{print $$1}')
LIBFT_BUILD_OUTPUT_SUFFIX := _ft_vox_opt$(OPT_LEVEL)_cfg$(FT_VOX_CONFIG_FINGERPRINT)
