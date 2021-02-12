# 01 Portable compute baseline

Vulkan storage and compute instead of requiring a hardware ray-tracing extension.

Runs on MoltenVK and other Vulkan 1.1 hardware with the declared storage limits. The host owns the BVH and precision policy. [Implementation](../../src/gpu.c).
