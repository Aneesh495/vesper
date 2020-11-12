# Project contract

Vesper uses metre world coordinates, nanometre wavelengths, and second arrival times. Transport integrates radiance density over a wavelength interval using an explicit sampling density. The CPU path uses double precision. The Vulkan portability target uses 32-bit float and portable compute storage buffers.

A path starts at the sensor. The first segment is part of the propagation time. Exposure time selects a geometric configuration, while propagation time accumulates across every finite segment in that configuration. Finite light sources may contribute to both films. Infinite lighting requires an explicit timing convention to contribute to a transient film.

The CPU implementation, GPU implementation, and independent checks have distinct roles. Comparisons declare a precision contract, a sampling protocol, and an independent expectation. The production renderer, result reporter, and evidence verifier have separate responsibilities.

## Neighboring projects

| Project | Defining mechanism in the reviewed source | Vesper counterpart |
|---|---|---|
| Cathode | CPU raster pipeline, C++ tracer and CRT-oriented processing | Original C spectral reference and Vulkan compute |
| Emberfall | Raster render graph for a game engine | Progressive light-transport jobs |
| Umbra | Relativistic geodesic integration | Geometric optics through authored surfaces and media |
