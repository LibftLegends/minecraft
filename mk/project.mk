ifeq ($(OS),Windows_NT)
    EXE_EXT := .exe
else
    EXE_EXT :=
endif

NAME        = ft_vox$(EXE_EXT)
NAME_DEBUG     = build/bin/ft_vox_debug$(EXE_EXT)
NAME_ANALYTICS = ft_vox_analytics$(EXE_EXT)
TEST_NAME      = build/bin/automated_tests$(EXE_EXT)

ifdef FORWARD_TRANSLATION
export FT_VOX_ENABLE_FORWARD_TRANSLATION := $(FORWARD_TRANSLATION)
endif
