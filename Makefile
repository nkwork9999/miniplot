PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Runtime metadata stays separate from the SDK source commit.
DUCKDB_VERSION ?= v2.0.0

# Configuration of extension
EXT_NAME=miniplot
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile
