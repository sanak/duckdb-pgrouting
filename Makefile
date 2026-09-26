# SPDX-License-Identifier: GPL-2.0-or-later
PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=pgrouting
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# No duckdb-spatial binary is published for an unreleased DuckDB, so on this line every unittest
# run, native or Wasm, skips the tests tagged spatial. Drop this when the branch builds against
# a released DuckDB.
export DUCKDB_TEST_CONFIG := $(CURDIR)/test/configs/skip_spatial.json

HOOKS_SRC ?= $(PROJ_DIR)scripts/git-hooks

# Installs the local commit guards. Patterns live in .git/info/forbidden-patterns (untracked).
install-hooks:
	@hooks_dir="$$(git rev-parse --git-path hooks)"; \
	cp "$(HOOKS_SRC)/commit-msg" "$(HOOKS_SRC)/pre-commit" "$$hooks_dir/"; \
	chmod +x "$$hooks_dir/commit-msg" "$$hooks_dir/pre-commit"; \
	test -f "$$(git rev-parse --git-path info/forbidden-patterns)" \
		|| echo "note: create .git/info/forbidden-patterns (one ERE per line) to enable the checks"

.PHONY: install-hooks

#### W1: DuckDB's unittest built with Emscripten (extension statically linked), run with Node.
# The published duckdb-wasm packages track DuckDB releases, not main, so per-change Wasm testing
# builds the test runner itself. A per-triplet VCPKG_INSTALLED_DIR keeps the wasm32-emscripten
# packages from being pruned by a native configure sharing one installed root.
W1_VARIANT ?= wasm_eh
W1_VARIANTS = wasm_eh wasm_mvp wasm_threads
# An unknown variant would expand the per-variant flag variables to empty and DuckDB does not
# validate DUCKDB_EXPLICIT_PLATFORM, so a typo would silently build the wrong thing. Expanded as
# the first line of the W1 recipes, i.e. only when a W1 target actually runs.
W1_REQUIRE_VARIANT = $(if $(filter $(W1_VARIANT),$(W1_VARIANTS)),,\
	$(error W1_VARIANT='$(W1_VARIANT)' is not supported; use one of: $(W1_VARIANTS)))
# emsdk's `upstream/emscripten` directory is prepended to PATH by the environment setup, and it
# contains a subdirectory literally named `cmake`. A recipe line with no shell metacharacters is
# exec'd by make directly, and its PATH search accepts that directory because the execute bit is
# set, so a bare `cmake` dies with "Permission denied". Resolving through the shell skips matches
# that are not executable files. Do not simplify this back to a bare `cmake`.
W1_CMAKE := $(shell command -v cmake 2>/dev/null)
W1_REQUIRE_CMAKE = $(if $(W1_CMAKE),,\
	$(error cmake was not found in PATH; install it or add it to PATH))
W1_BUILD_DIR = build/$(W1_VARIANT)_unittest
W1_CXX_FLAGS_wasm_eh = -fwasm-exceptions -DWEBDB_FAST_EXCEPTIONS=1 -DDUCKDB_NO_THREADS=1
W1_LINK_FLAGS_wasm_eh = -fwasm-exceptions
W1_CXX_FLAGS_wasm_mvp = -fexceptions -DDUCKDB_NO_THREADS=1
W1_LINK_FLAGS_wasm_mvp = -fexceptions
W1_CXX_FLAGS_wasm_threads = -fwasm-exceptions -DWEBDB_FAST_EXCEPTIONS=1 -DWITH_WASM_THREADS=1 -DWITH_WASM_SIMD=1 -DWITH_WASM_BULK_MEMORY=1 -pthread
# -sPROXY_TO_PTHREAD moves main() off the Node main thread. DuckDB's unittest blocks there (the
# task scheduler joins its workers, queries wait on condition variables), and Emscripten's manual
# is explicit that "if the main thread blocks while a worker attempts to proxy to it, a deadlock
# can occur" and recommends PROXY_TO_PTHREAD to avoid it. Without it this variant deadlocked in
# CI after printing the Catch2 banner and produced no further output; it is timing-dependent, so
# a passing run does not mean the flag is unnecessary.
W1_LINK_FLAGS_wasm_threads = -fwasm-exceptions -pthread -sPTHREAD_POOL_SIZE=8 -sPROXY_TO_PTHREAD
W1_C_FLAGS_wasm_threads = -pthread
W1_COMMON_LINK_FLAGS = -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=8MB -sEXIT_RUNTIME=1 --pre-js $(CURDIR)/scripts/wasm_unittest_pre.js

wasm_unittest:
	$(W1_REQUIRE_VARIANT)
	$(W1_REQUIRE_CMAKE)
	@echo "W1: building $(W1_VARIANT) with cmake at $(W1_CMAKE)"
	mkdir -p $(W1_BUILD_DIR)
	emcmake cmake $(GENERATOR) $(BUILD_FLAGS) $(VCPKG_MANIFEST_FLAGS) $(VCPKG_EMSDK_FLAGS) \
		-DVCPKG_TARGET_TRIPLET=wasm32-emscripten -DWASM_LOADABLE_EXTENSIONS=1 -DBUILD_SHELL=FALSE \
		-DDUCKDB_EXPLICIT_PLATFORM=$(W1_VARIANT) -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_CXX_FLAGS="$(W1_CXX_FLAGS_$(W1_VARIANT))" -DCMAKE_C_FLAGS="$(W1_C_FLAGS_$(W1_VARIANT))" \
		-DCMAKE_EXE_LINKER_FLAGS="$(W1_LINK_FLAGS_$(W1_VARIANT)) $(W1_COMMON_LINK_FLAGS)" \
		-DVCPKG_INSTALLED_DIR=$(CURDIR)/build/vcpkg_installed_wasm \
		-S $(DUCKDB_SRCDIR) -B $(W1_BUILD_DIR)
# --parallel keeps the build parallel with Make generators too, not only with GEN=ninja. The job
# count is explicit because a bare --parallel becomes an unbounded `make -j`; 8 is what the
# inherited duckdb_extension.Makefile uses for its own wasm targets.
	$(W1_CMAKE) --build $(W1_BUILD_DIR) --target unittest --parallel 8

# Not depending on wasm_unittest on purpose: CI keeps build and test as separate steps so a
# failure is attributed to the right one.
# The Wasm unittest has no network client, so it cannot INSTALL duckdb-spatial: the tests tagged
# spatial are skipped there (see test/configs/skip_spatial.json).
test_wasm_unittest:
	$(W1_REQUIRE_VARIANT)
	@test -f $(W1_BUILD_DIR)/test/unittest.js || { \
		echo "error: $(W1_BUILD_DIR)/test/unittest.js not found."; \
		echo "       Build it first: make wasm_unittest W1_VARIANT=$(W1_VARIANT)"; \
		exit 1; }
	node $(W1_BUILD_DIR)/test/unittest.js --test-config test/configs/skip_spatial.json "test/*"

.PHONY: wasm_unittest test_wasm_unittest
