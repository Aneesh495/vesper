# Build status

Vesper has a working C17 spectral and transient renderer, a separate float64 CPU implementation, a hardware Vulkan wavefront backend, a native workbench and a calibrated synthetic inverse lab. Full acceptance remains open.

| Gate | State | Scope |
|---|---|---|
| G01, G20 | Diagnostic | Source audit and project distinction are documented. |
| G02 | Diagnostic | A million randomized rays and adversarial edges run against the independent oracle. |
| G03, G04 | Diagnostic | Spectral quadrature, PDF mass, reciprocity, Fresnel and CPU/GPU comparisons are covered. Broader histogram and material coverage is open. |
| G05, G06, G07, G08 | Diagnostic | Area MIS, volume transmission, path depth and time cases are implemented. Broader multi-seed convergence, roulette and nested-scene baselines are open. |
| G09, G10 | Diagnostic | The Apple M2 executes validated wavefront rendering and the million-triangle benchmark. Detailed hardware captures and performance characterization are open. |
| G11, G12 | Diagnostic | Native workbench smoke and interrupted CPU replay are covered. Broader interaction, focus and GPU resume campaigns are open. |
| G13 | Open | Twelve showcase scenes and six transient films are rendered in diagnostics. The six required 1920 by 1080, 1,024-spp outputs remain outstanding. |
| G14 | Diagnostic | The separate verifier checks raw sample counts, source identity and wavelength replay. Broader semantic controls remain open. |
| G15 | Diagnostic | CMake builds the API, CLI and workbench with separate CPU-only and hardware paths. |
| G16 | Open | Full campaign and release qualification remain outstanding. |
| G17 | Diagnostic | Direct, bounce, dispersive chord, convolution and gate energy checks execute. |
| G18 | Diagnostic | Synthetic calibrated recovery and ambiguity controls execute. Physical sensor measurements remain outside the available evidence. |
| G19 | Diagnostic | Rehashed arrival and inverse-calibration controls are implemented. |

Current limits include position-only OBJ input, a direction-constant environment, no texture or normal-map path, fixed sample targets, bounded in-memory geometry and a single host-coherent Vulkan allocation strategy. Full acceptance also requires additional independent references, hardware capture, recovery coverage and release evidence. The diagnostic profile never stands in for full acceptance.
