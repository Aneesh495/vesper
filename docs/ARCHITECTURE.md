# Architecture and ownership

A scene owns its material, light, medium and primitive arrays, each imported grid, and the compiled tree. Compilation invalidates the identity before checking input and rebuilding the tree. APIs that replace an existing output require a zero-initialized or owned object. Destructors accept zeroed state and clear their object. Callers that edit fields directly must recompile before rendering. Scene, film and GPU contexts have a single caller owner. Use independent contexts for simultaneous renders.

The Vulkan device and film have independent ownership. The GPU context owns host-coherent storage, a command buffer and a fence. The film owns mean, variance, spectral bands and raw gate storage. GPU output stays in per-path event slots until the fence signals, then the host folds slots into the film in sample-index order. A failed dispatch produces no readback into the film. A failed film update preserves earlier complete samples. The CPU renderer uses the same film contract with its own traversal and estimator implementation.

## Source walkthroughs

1. [Scene parsing](../src/scene_io.c) validates explicit units and dispatches asset commands. [Asset loading](../src/assets.c) bounds the importer and grid arrays, then [scene compilation](../src/scene.c) resolves emitters and constructs a [BVH](../src/bvh.c).
2. [CPU path execution](../src/transport.c) uses [surface scattering](../src/scatter.c) and [media tracking](../src/volume.c). [Film accumulation](../src/film.c) converts wavelength contributions to raw XYZ and temporal energy.
3. [The Vulkan host](../src/gpu.c) packs the [shader ABI](../shaders/layout.glsl), fences [wavefront execution](../shaders/wavefront.comp), and folds the same event format into the film. The Vulkan device fingerprint includes shader bytes.
4. [Recovery](../src/checkpoint.c) checks the complete contract header before reading records into a temporary film. The [interruption probe](../scripts/recovery.py) drives the CLI, resumes and compares encoded bytes against an uninterrupted run.
5. [Inverse calibration](../app/inverse_main.c) reads measurements, applies the [constrained C optimizer](../src/inverse.c), and records rank, residual and uncertainty. [Synthetic recovery](../scripts/inverse_recovery.py) supplies calibrated inputs and separate ambiguity controls.

## Diagrams

Each image has an adjacent editable Graphviz source.

| Subject | SVG |
|---|---|
| System boundaries | [Context](diagrams/context.svg) |
| Scene compilation | [Compilation](diagrams/compile.svg) |
| Paths, queues and barriers | [Wavefront](diagrams/wavefront.svg) |
| Estimator probability measures | [MIS](diagrams/mis.svg) |
| Nested interior state | [Media](diagrams/media.svg) |
| Optical time convention | [Timing](diagrams/timing.svg) |
| Sensor and film channels | [Film](diagrams/film.svg) |
| Accumulation invalidation | [Workbench](diagrams/workbench.svg) |
| Checkpoint lifecycle | [Recovery](diagrams/recovery.svg) |
| Observation and calibration | [Inverse](diagrams/inverse.svg) |
| Manifest and replay | [Evidence](diagrams/evidence.svg) |
| Build and runtime deployments | [Build](diagrams/build.svg) |
