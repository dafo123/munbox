# Convenience Makefile wrapping common CMake workflows
# Allows typical developer commands: make, make build, make test, make install, make clean

BUILD_DIR ?= build
GENERATOR ?=
CMAKE ?= cmake

.DEFAULT_GOAL := all

configure:
	@mkdir -p $(BUILD_DIR)
	$(CMAKE) -S . -B $(BUILD_DIR) $(GENERATOR)

all: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel

build: all

clean:
	@echo "Removing $(BUILD_DIR)" && rm -rf $(BUILD_DIR)

reconfigure: clean all

install: all
	$(CMAKE) --install $(BUILD_DIR)

# Run the aggregated CTest suite
check test: all
	$(CMAKE) --build $(BUILD_DIR) --target check || $(CMAKE) --build $(BUILD_DIR) --target test || true
	cd $(BUILD_DIR) && ctest --output-on-failure || true

# Run the legacy shell test harness directly
shell-tests: all
	bash test/run_tests.sh --munbox $(BUILD_DIR)/cmd/munbox

.PHONY: configure all build clean reconfigure install check test shell-tests
