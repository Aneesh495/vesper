# Dependency policy

Vesper authors the traversal, scattering, wavelength estimator, temporal estimator, and inverse optimizer. The CPU target requires a C17 toolchain and CMake 3.22 or later. Vulkan loader and headers provide the device interface, and glslang compiles original shaders to SPIR-V. SDL can provide windowing and input. Renderer installations are reserved for independent comparisons.

| Dependency | Role | Upstream licensing |
|---|---|---|
| [Vulkan loader](https://github.com/KhronosGroup/Vulkan-Loader) and [headers](https://github.com/KhronosGroup/Vulkan-Headers) | Device interface and definitions | Apache-2.0 |
| [glslang](https://github.com/KhronosGroup/glslang) | Shader compiler | BSD-3-Clause and component notices |
| [MoltenVK](https://github.com/KhronosGroup/MoltenVK) | Vulkan implementation on Apple platforms | Apache-2.0 |
| [SDL](https://github.com/libsdl-org/SDL) | Optional workbench window and input | Zlib |


The [vendored cgltf parser](../vendor/cgltf/README.md) is pinned at v1.15 under MIT. Vesper uses it to parse the asset container and implements its own checked geometry conversion.
