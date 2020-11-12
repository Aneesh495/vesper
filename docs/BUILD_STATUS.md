# Build status

The first milestone establishes build configuration and the numeric contract. Full acceptance is pending.

The reference machine is an Apple M2 with 16 GiB of memory. Vulkan enumerates the integrated GPU through MoltenVK 1.4.2 and reports no shader float64. Vulkan enumeration establishes device availability. A transport dispatch and its readback establish execution.
