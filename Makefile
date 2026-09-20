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
