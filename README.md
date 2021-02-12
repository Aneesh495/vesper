# Vesper

Vesper is a C17 spectral light-transport engine with a Vulkan wavefront backend, a float64 CPU reference, and time-of-flight film. It traces surfaces and media at a sampled wavelength, then integrates the result into raw XYZ, wavelength bands and photon arrival bins.

![Afterglow rendered on the Vulkan backend](docs/images/afterglow.png)

[Native workbench capture](docs/images/workbench.png)

## Build and run

Use CMake 3.22 or later and a C17 compiler. Vulkan development libraries and glslang enable the GPU backend. SDL2 2.0.22 or later enables the native workbench. The observer table is included and checked at configure time.

```sh
make doctor
make test
make render SCENE=scenes/afterglow.vs SIZE=320 HEIGHT=200 SPP=64
make gpu-probe
make workbench SCENE=scenes/veil.vs WORKBENCH_BACKEND=vulkan
make transient SCENE=scenes/prism.vs SIZE=256 HEIGHT=256 SPP=32 RENDER_ARGS="--backend vulkan" OUT=run/prism
make inverse
```

The default render writes raw OpenEXR and a display PPM under run. Use the CPU-only option with **CMAKE_FLAGS="-DVESPER_VULKAN=OFF"**. Select Vulkan explicitly with **RENDER_ARGS="--backend vulkan"**. Hardware dispatch has been exercised on Apple M2 with MoltenVK. The CPU route builds on Linux with the standard toolchain.

## Mechanisms and evidence

- Original binned SAH traversal, spectral BSDFs and delta/ratio tracking. [Transport notes](docs/TRANSPORT.md)
- Compacted Vulkan queues and host-owned accumulation with explicit barriers. [Architecture and diagrams](docs/ARCHITECTURE.md)
- Group-index propagation time, pulse convolution and raw gated energy. [Formats and recovery](docs/ARTIFACTS.md)
- Bounded calibrated reflectance, emitter and extinction fits. [Inverse experiments](docs/INVERSE.md)
- Twelve authored studies, including nested glass, heterogeneous fog and 144 lights. [Gallery](scenes/README.md)
- Numerical rules, supported features and open acceptance work. [Experiment plan](docs/EXPERIMENT_PLAN.md), [Build status](docs/BUILD_STATUS.md)

See the [scene format](docs/SCENE_FORMAT.md), [workbench controls](docs/WORKBENCH.md), [C API example](examples/create_scene.c), and [dependency policy](docs/DEPENDENCIES.md).
