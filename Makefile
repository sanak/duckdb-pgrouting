# SPDX-License-Identifier: GPL-2.0-or-later
PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=routing
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

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
W1_BUILD_DIR = build/$(W1_VARIANT)_unittest
W1_CXX_FLAGS_wasm_eh = -fwasm-exceptions -DWEBDB_FAST_EXCEPTIONS=1 -DDUCKDB_NO_THREADS=1
W1_LINK_FLAGS_wasm_eh = -fwasm-exceptions
W1_CXX_FLAGS_wasm_mvp = -fexceptions -DDUCKDB_NO_THREADS=1
W1_LINK_FLAGS_wasm_mvp = -fexceptions
W1_CXX_FLAGS_wasm_threads = -fwasm-exceptions -DWEBDB_FAST_EXCEPTIONS=1 -DWITH_WASM_THREADS=1 -DWITH_WASM_SIMD=1 -DWITH_WASM_BULK_MEMORY=1 -pthread
W1_LINK_FLAGS_wasm_threads = -fwasm-exceptions -pthread -sPTHREAD_POOL_SIZE=8
W1_C_FLAGS_wasm_threads = -pthread
W1_COMMON_LINK_FLAGS = -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=8MB -sEXIT_RUNTIME=1 --pre-js $(CURDIR)/scripts/wasm_unittest_pre.js

wasm_unittest:
	mkdir -p $(W1_BUILD_DIR)
	emcmake cmake $(GENERATOR) $(BUILD_FLAGS) $(VCPKG_MANIFEST_FLAGS) $(VCPKG_EMSDK_FLAGS) \
		-DVCPKG_TARGET_TRIPLET=wasm32-emscripten -DWASM_LOADABLE_EXTENSIONS=1 -DBUILD_SHELL=FALSE \
		-DDUCKDB_EXPLICIT_PLATFORM=$(W1_VARIANT) -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_CXX_FLAGS="$(W1_CXX_FLAGS_$(W1_VARIANT))" -DCMAKE_C_FLAGS="$(W1_C_FLAGS_$(W1_VARIANT))" \
		-DCMAKE_EXE_LINKER_FLAGS="$(W1_LINK_FLAGS_$(W1_VARIANT)) $(W1_COMMON_LINK_FLAGS)" \
		-DVCPKG_INSTALLED_DIR=$(CURDIR)/build/vcpkg_installed_wasm \
		-S $(DUCKDB_SRCDIR) -B $(W1_BUILD_DIR)
	cmake --build $(W1_BUILD_DIR) --target unittest

test_wasm_unittest:
	node $(W1_BUILD_DIR)/test/unittest.js "test/*"

.PHONY: wasm_unittest test_wasm_unittest
