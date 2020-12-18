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

SCENE ?= scenes/afterglow.vs
SIZE ?= 320
HEIGHT ?= 200
SPP ?= 32
SEED ?= 1789
OUT ?= run/vesper
RENDER_ARGS ?=
.PHONY: render demo cpu-reference transient data
render: build
	mkdir -p "$$(dirname "$(OUT)")"
	"$(BUILD_DIR)/vesper" render "$(SCENE)" --width "$(SIZE)" --height "$(HEIGHT)" --spp "$(SPP)" --seed "$(SEED)" --prefix "$(OUT)" $(RENDER_ARGS)
demo: render
cpu-reference: test
transient: RENDER_ARGS += --bins 512 --step-ns .125 --checkpoint "$(OUT).vfilm"
transient: render
data:
	cmake -S . -B "$(BUILD_DIR)"

.PHONY: gpu-probe
gpu-probe: build
	"$(BUILD_DIR)/vesper" gpu-probe scenes/afterglow.vs --width 32 --height 24 --spp 8 --depth 4 --bins 128 --step-ns .5
