# Reproduction

The diagnostic campaign requires a clean checkout at the hosted main tip, a C17 compiler, CMake, Python 3.11 or newer, SDL2, glslang, a hardware Vulkan ICD and the Khronos validation layer. The software Vulkan path cannot satisfy the hardware probe. CMake builds a CPU-only route when Vulkan is unavailable, and the CLI reports a missing requested backend explicitly.

Run from the repository root.

```sh
make test
make acceptance PROFILE=diagnostic RUN_ID=local-m2-01
make verify EVIDENCE=evidence/local-m2-01
make negative-controls EVIDENCE=evidence/local-m2-01 CONTROL_LOG=evidence/local-m2-01-controls.jsonl
```

The run ID must be new. The diagnostic bundle retains raw EXR, temporal checkpoints, trace records and a native SDL render capture. The six transient films and two workbench checkpoints occupy about 3 GiB in total. The exact replay executables are the binaries in the build directory identified in the environment record. Pass that directory through BUILD_DIR if you used an external build.

On macOS with Homebrew Vulkan libraries, run the validation probe with the library search path applied directly to the executable.

```sh
env DYLD_LIBRARY_PATH=/opt/homebrew/lib VESPER_VALIDATION=1 \
  build/vesper gpu-probe scenes/afterglow.vs --width 32 --height 24 --spp 8
```

Use PROFILE=full to check full acceptance readiness. The current preflight stops with an explicit failed status while required capabilities and full campaigns remain open. Release verification requires full acceptance. The [experiment plan](EXPERIMENT_PLAN.md) defines the diagnostic and full workloads and the [build status](BUILD_STATUS.md) identifies open gates.

## Long showcase renders

Run **make showcase-full OUT=run/full-gallery** for the six fixed Vulkan jobs at 1920 by 1080 and 1,024 samples per pixel. The script uses the same stratified sampler, seed 1789 and depth 8 for Afterglow, Veil, Prism, Iris, Shutter and Array. Its per-scene logs report completed row blocks and checkpoint writes. A checkpoint is published after at least 128 additional rows of retained work, plus at clean completion or interruption. Set FULL_SCENE to run one of the six jobs. The same command resumes from existing checkpoints after matching scene, device and render contracts. The output is locked to the exact source and binary, and the checker confirms 1,024 samples in every raw EXR pixel. Use a fresh output directory after changing the shader, scene or integrator. These outputs contribute to G13 and require separate full campaign verification.
