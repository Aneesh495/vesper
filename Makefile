BUILD_DIR ?= build
CMAKE ?= cmake
JOBS ?= 8
CMAKE_FLAGS ?=

.PHONY: bootstrap doctor build test
bootstrap:
	$(CMAKE) -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=Release $(CMAKE_FLAGS)
doctor:
	@sh scripts/doctor.sh
build: bootstrap
	$(CMAKE) --build "$(BUILD_DIR)" --parallel "$(JOBS)"
test: build
	ctest --test-dir "$(BUILD_DIR)" --output-on-failure
