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

.PHONY: inverse
inverse: build
	"$(BUILD_DIR)/vesper-inverse" scenes/inverse-extinction.csv

.PHONY: spectral-check bsdf-check volume-check stress
spectral-check: build
	"$(BUILD_DIR)/vesper-numerical-check" spectral
bsdf-check: build
	"$(BUILD_DIR)/vesper-numerical-check" bsdf
volume-check: build
	"$(BUILD_DIR)/vesper-numerical-check" volume
stress: build
	"$(BUILD_DIR)/vesper-geometry-check" 1000000

.PHONY: workbench
WORKBENCH_BACKEND ?= cpu
workbench: build
	"$(BUILD_DIR)/vesper-workbench" "$(SCENE)" "$(WORKBENCH_BACKEND)"

.PHONY: scene-gallery
BACKEND ?= cpu
scene-gallery: build
	sh scripts/gallery.sh "$(BUILD_DIR)/vesper" "$(OUT)" "$(BACKEND)" "$(SIZE)" "$(HEIGHT)" "$(SPP)"

.PHONY: temporal-check
temporal-check: build
	"$(BUILD_DIR)/vesper-temporal-check"

.PHONY: bench
BENCH_SPP ?= 16
bench: build
	"$(BUILD_DIR)/vesper-benchmark" "$(BENCH_SPP)"

.PHONY: transient-gallery
transient-gallery: build
	sh scripts/transient_gallery.sh "$(BUILD_DIR)/vesper" "$(OUT)" "$(BACKEND)" "$(SPP)"

.PHONY: inverse-recovery
inverse-recovery: build
	mkdir -p run
	python3 scripts/inverse_recovery.py "$(BUILD_DIR)/vesper-inverse" "$$(mktemp -u run/inverse.XXXXXX)"
