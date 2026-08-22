FT_VOX_PARALLEL_JOBS := $(filter -j%,$(MAKEFLAGS))
FT_VOX_JOBSERVER := $(filter --jobserver-auth=%,$(MAKEFLAGS))
FT_VOX_BATCH_OUTPUT := 0
ifneq ($(FT_VOX_JOBSERVER),)
    FT_VOX_BATCH_OUTPUT := 0
else ifneq ($(FT_VOX_PARALLEL_JOBS),)
    ifneq ($(FT_VOX_PARALLEL_JOBS),-j1)
        FT_VOX_BATCH_OUTPUT := 0
    endif
endif
