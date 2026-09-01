ifeq ($(OS),Windows_NT)
SHELL := C:/Progra~1/Git/bin/bash.exe
.SHELLFLAGS := -lc
export SHELL
else
SHELL := /bin/bash
endif

MAKEFLAGS += -r
.DEFAULT_GOAL := all
# BUILD_PLAN_MODE=1 is used by the stale-work planning wrapper.  Recipes emit
# machine-readable markers in that mode and concise status lines otherwise.
BUILD_PLAN_MODE ?= 0
BUILD_PROGRESS_ACTIVE ?= 0
BUILD_PROGRESS_SESSION_DIR ?=
# The parent graph uses GNU Make features available in 3.81. Output
# synchronization is optional and must not prevent builds on Apple Make 3.81.
FT_VOX_SUPPORTED_MAKE_VERSION := $(filter 3.81 3.82 4.% 5.% 6.% 7.% 8.% 9.%,$(MAKE_VERSION))
ifeq ($(FT_VOX_SUPPORTED_MAKE_VERSION),)
$(error GNU Make 3.81 or newer is required; found $(MAKE_VERSION))
endif
include mk/project.mk
include mk/tools.mk
include mk/output.mk
include mk/sources.mk
include mk/directories.mk
include mk/compiler.mk
include mk/libft.mk
include mk/objects.mk

ifeq ($(FT_VOX_ANALYTICS),1)
COMPILE_FLAGS += -DLIBFT_ENABLE_ANALYTICS=1
LIBFT_COMPILE_FLAGS += -DLIBFT_ENABLE_ANALYTICS=1
endif

# Include Libft's canonical object/archive graph in the parent graph.  The
# graph is configured with Libft-relative paths so one GNU Make scheduler can
# compile ft_vox and Libft objects together.
FT_VOX_LIBFT_TARGET := $(TARGET)
FT_VOX_LIBFT_DEBUG_TARGET := $(NAME_DEBUG)
FT_VOX_LIBFT_COMPILE_FLAGS := $(COMPILE_FLAGS)
FT_VOX_OBJECTS := $(OBJS)
FT_VOX_TEST_OBJECTS := $(TEST_OBJS)
FT_VOX_DEPENDENCIES := $(DEPS)
FT_VOX_CC := $(CC)
FT_VOX_CFLAGS := $(CFLAGS)
LIBFT_GLOBAL_GRAPH := 1
LIBFT_GLOBAL_GRAPH_PREFIX := Libft/
LIBFT_GLOBAL_ARCHIVE_SUFFIX := $(LIBFT_ARCHIVE_SUFFIX)
BUILD_OUTPUT_SUFFIX := $(LIBFT_BUILD_OUTPUT_SUFFIX)
COMPILE_FLAGS := $(LIBFT_COMPILE_FLAGS)
include Libft/mk/global_graph.mk
LIBFT_PARENT_SELECTED_BUILD_ROOT := $(LIBFT_GLOBAL_ROOT)
COMPILE_FLAGS := $(FT_VOX_LIBFT_COMPILE_FLAGS)
TARGET := $(FT_VOX_LIBFT_TARGET)
OBJS := $(FT_VOX_OBJECTS)
TEST_OBJS := $(FT_VOX_TEST_OBJECTS)
DEPS := $(FT_VOX_DEPENDENCIES)
CC := $(FT_VOX_CC)
CFLAGS := $(FT_VOX_CFLAGS)

.SECONDEXPANSION:
FT_VOX_OBJECT_DIRECTORIES := $(sort $(dir $(OBJS) $(TEST_OBJS)))

$(FT_VOX_OBJECT_DIRECTORIES):
	@-$(MKDIR) $@

FT_VOX_BUILD_CONFIG_INPUTS := mk/compiler.mk mk/project.mk mk/libft.mk
LIBFT_PARENT_GLOBAL_TARGET := $(LIBFT_FULL_LIB)
LIBFT_PARENT_GLOBAL_DEBUG_TARGET := $(LIBFT_FULL_DEBUG_LIB)
LIBFT_PARENT_SELECTED_ARCHIVES := $(LIBFT_GLOBAL_RELEASE_ARCHIVES) \
        $(LIBFT_GLOBAL_DEBUG_ARCHIVES) $(LIBFT_PARENT_GLOBAL_TARGET) \
        $(LIBFT_PARENT_GLOBAL_DEBUG_TARGET)

define LIBFT_PARENT_ARCHIVE_RULE
$(1): $(2) $(LIBFT_GLOBAL_ARCHIVE_CONFIG_INPUTS)
	@if [ "$$(BUILD_PLAN_MODE)" = "1" ]; then printf '%s\n' "__BUILD_PLAN__|archive|libft|Full_Libft|$(1)"; else printf '\033[1;35m[LIBFT] Archiving %s\033[0m\n' "$(1)"; fi
	@$(MKDIR) $(dir $$@)
	@$(RM) $$@.tmp
	@if [ "$(UNAME_S)" = "Darwin" ]; then \
		libtool -static -o "$$@.tmp" $(2); \
	else \
		{ printf 'CREATE %s\n' "$$@.tmp"; \
		  for lib in $(2); do printf 'ADDLIB %s\n' "$$$$lib"; done; \
		  printf 'SAVE\nEND\n'; } | $(AR) -M >/dev/null; \
	fi
	@mv $$@.tmp $$@
	@printf '\033[1;35m[LIBFT] Archive ready: %s\033[0m\n' "$(1)"
	@if [ "$$(BUILD_PROGRESS_ACTIVE)" = "1" ]; then \
		sh Libft/mk/update_build_progress.sh "$$(BUILD_PROGRESS_SESSION_DIR)" archive libft Full_Libft || true; \
	fi
endef

$(eval $(call LIBFT_PARENT_ARCHIVE_RULE,$(LIBFT_PARENT_GLOBAL_TARGET),$(LIBFT_GLOBAL_RELEASE_ARCHIVES)))
$(eval $(call LIBFT_PARENT_ARCHIVE_RULE,$(LIBFT_PARENT_GLOBAL_DEBUG_TARGET),$(LIBFT_GLOBAL_DEBUG_ARCHIVES)))

ifeq ($(OS),Windows_NT)
SUBMODULE_UPDATE_CMD = tools\update_libft.cmd
else
SUBMODULE_UPDATE_CMD = sh tools/update_libft.sh
endif

all: normal analytics

normal:
	@sh Libft/mk/run_build_with_progress.sh "$(MAKE)" internal-all
	@test -f "$(TARGET)" && printf '\033[1;35m[MINECRAFT] Normal voxel ready: %s\033[0m\n' "$(TARGET)"

analytics:
	@$(MAKE) --no-print-directory FT_VOX_ANALYTICS=1 \
		LIBFT_ARCHIVE_SUFFIX=_analytics \
		LIBFT_BUILD_OUTPUT_SUFFIX=_analytics \
		TARGET=ft_vox_analytics$(EXE_EXT) internal-all
	@test -f "ft_vox_analytics$(EXE_EXT)" && printf '\033[1;35m[MINECRAFT][Analytics] Ready: %s\033[0m\n' "ft_vox_analytics$(EXE_EXT)"

plan:
	@sh Libft/mk/print_build_plan.sh "$(MAKE)" internal-all

internal-all: $(TARGET)

tests:
	@sh Libft/mk/run_build_with_progress.sh "$(MAKE)" internal-tests

internal-tests: $(TEST_NAME)

dirs:
	@-$(MKDIR) $(OBJ_DIR)
	@-$(MKDIR) $(OBJ_DIR_DEBUG)
	@-$(MKDIR) $(OBJ_DIR_TEST)

$(BUILD_LOG_DIR):
	@-$(MKDIR) $(BUILD_LOG_DIR)

install_cobc:
	@if ! command -v cobc >/dev/null 2>&1; then \
		apt-get update && apt-get install -y gnucobol; \
	else \
		printf 'cobc already installed.\n'; \
	fi

tests_with_cobc: install_cobc
	$(MAKE) tests
	$(MAKE) test

submodule_init:
	@$(SUBMODULE_UPDATE_CMD)

submodule_update:
	@$(SUBMODULE_UPDATE_CMD)

debug:
	@sh Libft/mk/run_build_with_progress.sh "$(MAKE)" internal-debug

internal-debug: $(NAME_DEBUG)

$(TARGET): $(OBJS) $(LIBFT_LINK_LIB) $(FT_VOX_BUILD_CONFIG_INPUTS)
	@if [ "$(BUILD_PLAN_MODE)" = "1" ]; then printf '%s\n' "__BUILD_PLAN__|link|minecraft|Minecraft|$@"; else printf '\033[1;35m[MINECRAFT] Linking %s\033[0m\n' "$@"; fi
	$(file >$@.rsp,$(OBJS) $(LIBFT_LINK_FLAGS))
	@$(CC) $(CFLAGS) -o $@ @$@.rsp $(LDFLAGS)
	@printf '\033[1;35m[MINECRAFT] Link ready: %s\033[0m\n' "$@"

$(TEST_NAME): $(TEST_OBJS) $(OBJS_NO_MAIN) $(TARGET) $(LIBFT_FULL_LIB) \
        $(FT_VOX_BUILD_CONFIG_INPUTS)
	@if [ "$(BUILD_PLAN_MODE)" = "1" ]; then printf '%s\n' "__BUILD_PLAN__|link|minecraft|MinecraftTest|$@"; else printf '\033[1;35m[MINECRAFT][Test] Linking %s\033[0m\n' "$@"; fi
	$(file >$@.rsp,$(TEST_OBJS) $(OBJS_NO_MAIN) $(LIBFT_LINK_FLAGS))
	@$(CC) $(CFLAGS) -o $@ @$@.rsp $(LDFLAGS)
	@printf '\033[1;35m[MINECRAFT][Test] Link ready: %s\033[0m\n' "$@"

$(OBJ_DIR)/%.o: %.cpp | $$(dir $$@)
	@if [ "$(BUILD_PLAN_MODE)" = "1" ]; then printf '%s\n' "__BUILD_PLAN__|compile|minecraft|Minecraft|$<"; fi
	@$(CC) $(CFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@
	@if [ "$(BUILD_PROGRESS_ACTIVE)" = "1" ]; then \
		sh Libft/mk/update_build_progress.sh "$(BUILD_PROGRESS_SESSION_DIR)" compile minecraft Minecraft "$<" || true; \
	else \
		printf '\033[1;36m[MINECRAFT] Compiling %s\033[0m\n' "$<"; \
	fi

$(OBJ_DIR)/%.o: %.mm | $$(dir $$@)
	@if [ "$(BUILD_PLAN_MODE)" = "1" ]; then printf '%s\n' "__BUILD_PLAN__|compile|minecraft|Minecraft|$<"; fi
	@$(CC) $(CFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@
	@if [ "$(BUILD_PROGRESS_ACTIVE)" = "1" ]; then \
		sh Libft/mk/update_build_progress.sh "$(BUILD_PROGRESS_SESSION_DIR)" compile minecraft Minecraft "$<" || true; \
	else \
		printf '\033[1;36m[MINECRAFT] Compiling %s\033[0m\n' "$<"; \
	fi

$(OBJ_DIR_TEST)/%.o: %.cpp | $$(dir $$@)
	@if [ "$(BUILD_PLAN_MODE)" = "1" ]; then printf '%s\n' "__BUILD_PLAN__|compile|minecraft|MinecraftTest|$<"; fi
	@$(CC) $(CFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@
	@if [ "$(BUILD_PROGRESS_ACTIVE)" = "1" ]; then \
		sh Libft/mk/update_build_progress.sh "$(BUILD_PROGRESS_SESSION_DIR)" compile minecraft MinecraftTest "$<" || true; \
	else \
		printf '\033[1;36m[MINECRAFT][Test] Compiling %s\033[0m\n' "$<"; \
	fi

-include $(DEPS)

clean:
	-$(RMDIR) $(OBJ_DIR) $(OBJ_DIR_DEBUG) $(OBJ_DIR_TEST)
	-$(RM) test_example_compiler.c test_example_compiler.bin test_example_compiler.txt
	-$(RM) test_example_invalid_compiler.c test_example_invalid_compiler.bin test_example_invalid_compiler.log
	-$(RM) test_runtime_file.txt
	-$(RMDIR) $(BUILD_LOG_DIR)

fclean: clean
	-$(RMDIR) $(LIBFT_PARENT_SELECTED_BUILD_ROOT)
	-$(RMDIR) $(LIBFT_PARENT_SELECTED_BUILD_ROOT)
	-$(RM) $(LIBFT_PARENT_SELECTED_ARCHIVES)
	-$(RM) $(NAME) $(NAME_DEBUG) $(TEST_NAME)
	-$(RMDIR) $(OBJ_DIR) $(OBJ_DIR_DEBUG) $(OBJ_DIR_TEST) data

re:
	@$(MAKE) fclean
	@$(MAKE) all

test: tests
	@./$(TEST_NAME) --validate-camera-speed
	@./$(TEST_NAME) --validate-collision
	@./$(TEST_NAME) --validate-block-edit
	@./$(TEST_NAME) --validate-visible-distance
	@./$(TEST_NAME) --validate-terrain-determinism
	@./$(TEST_NAME) --validate-world-scale
	@./$(TEST_NAME) --validate-caves
	@./$(TEST_NAME) --validate-terrain-configuration
	@./$(TEST_NAME) --validate-world-revision
	@./$(TEST_NAME) --validate-async-generation

both: all debug

re_both: re both

lint:
	$(PYTHON) $(LINT_SCRIPT)

coverage:
	@-$(RMDIR) $(OBJ_DIR)
	@-$(RMDIR) $(OBJ_DIR_DEBUG)
	@-$(RMDIR) $(OBJ_DIR_TEST)
	@-$(RM) $(TARGET)
	@-$(RM) $(NAME_DEBUG)
	@-$(RM) $(TEST_NAME)
	$(MAKE) tests OPT_LEVEL=0 COVERAGE=1
	$(MAKE) test COVERAGE=1
	$(PYTHON) $(COVERAGE_SCRIPT) --object-dir $(OBJ_DIR) --threshold-lines $(COVERAGE_LINE_THRESHOLD) --threshold-branches $(COVERAGE_BRANCH_THRESHOLD)

ci-coverage:
	$(MAKE) coverage

ci-build:
	$(MAKE) fclean
	$(MAKE) all OPT_LEVEL=2
	$(MAKE) debug

ci-test:
	$(MAKE) test

ci-lint: lint

ci:
	$(MAKE) ci-build
	$(MAKE) ci-test
	$(MAKE) ci-lint
	$(MAKE) ci-coverage

.PHONY: all normal analytics plan internal-all dirs clean fclean re debug internal-debug both re_both tests internal-tests test lint coverage \
        ci-build ci-test ci-lint ci-coverage ci submodule_init submodule_update \
        ft_vox install_cobc tests_with_cobc
